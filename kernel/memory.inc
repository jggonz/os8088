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
; (docs/plans/completed/PAINT-NOTES.md records
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

%ifdef KERN_SMALL
MEM_MAX     equ 20              ; TWENTY heap claims on kern_small
                                ; (docs/plans/KERN-SMALL-CUT-PLAN.md D7). The SDK's
                                ; copy STAYS AT 32, on MAX_TASKS' rule:
                                ; apps/os88api.inc derives
                                ; CLAIM_SNAPSHOT_SIZE from it and taskmgr
                                ; allocates that, so a package built at 32
                                ; reading a 20-record snapshot OVER-allocates
                                ; and is safe, where the reverse overflows the
                                ; buffer. The only visible cost is twelve
                                ; empty rows on the Task Manager's heap page.
%else
MEM_MAX  equ 32                 ; claim records; more than this and a claim
%endif
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
MC_DMA   equ 6                  ; word: WHERE THIS CLAIM MAY LAND, in one word,
                                ; because a placement is what both halves are.
                                ;
                                ; Bits 0-14 are the 64KB-page-safe HEAD in
                                ; paragraphs, 0 = no constraint - and
                                ; mem_claim_1 refuses a head over 0x1000, a
                                ; page being 4,096 paragraphs, so the top bit
                                ; is free by that check and not by hope.
                                ;
                                ; Bit 15 (MC_DMA_HI) is the DOOR it came in
                                ; through: set = top-down (SPEC.md 50.3.2), and
                                ; it goes back through it. The compactor's two
                                ; passes each own one direction, and a claim
                                ; dragged the wrong way moves the free run from
                                ; BELOW the whole ceiling stack to BETWEEN the
                                ; movers and the stayers - two runs where there
                                ; was one (docs/plans/HEAP-UNPIN-PLAN.md 5). It
                                ; was a byte of its own and MEM_MAX bytes of
                                ; .lowbss; the head is the only other thing
                                ; that decides where a block may sit, so the
                                ; two share the word and EVERY read of the head
                                ; masks.
                                ;
                                ; Recorded so that a claim
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
MC_DMA_HI   equ 0x8000          ; MC_DMA bit 15: the top-down door
MC_DMA_HEAD equ 0x7FFF          ; ...and the mask every reader of the head takes

MEM_K_SAVE equ 0xFF01           ; kernel tags (SPEC.md 50.2): menu save-under
MEM_K_DRV  equ 0xFF03           ; a loaded driver's image (SPEC.md 51.3)
MEM_K_COPY equ 0xFF04           ; the file manager's copy buffer (22.3): held
                                ; for exactly one Cut/Copy/Paste operation,
                                ; including the part of it that is suspended
                                ; on an overwrite question
MEM_K_ASC  equ 0xFF06           ; one volume's ASSOC.DAT cache (SPEC.md
                                ; 54.7) - shared, reloaded on a volume switch
                                ; 0xFF05 was MEM_K_FATW, one volume's FAT
                                ; window, held for as long as that volume was
                                ; mounted. It is MEM_P_FATW below now: a
                                ; window is a CACHE and the machine may take
                                ; it back (SPEC.md 18.8.4)
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
MEM_K_HIB  equ 0xFF0C           ; the resume's extent list (SPEC.md 87.5):
                                ; claimed on the way into the stub and never
                                ; freed, because the machine it is in is
                                ; about to be overwritten; freed on the one
                                ; path that turns back. 0xFF0C and not the
                                ; retired 0xFF0B above, for the reason above

MEM_K_CMPR equ 0xFF0D           ; the Compress verb's working block (SPEC.md
                                ; 22.22) - the source, the output and the
                                ; compressor's 8KB head[] plus its prev[]
                                ; window, all in ONE claim so the three
                                ; segments are paragraph arithmetic off a
                                ; single base. Held for exactly one compress,
                                ; and it is the LARGEST transient claim in the
                                ; kernel: twice a file plus up to 40KB, which
                                ; is why the window drops until the claim fits
                                ; rather than the verb refusing outright.
                                ; 0xFF0B is skipped (it was MEM_K_OVL and is
                                ; retired above) and so is 0xFF0C, which is
                                ; MEM_K_HIB's
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
MEM_P_FATW  equ MEM_PG_MED<<8 | 0x20    ; one volume's FAT window (SPEC.md
MEM_P_FATW_N equ 8              ; 18.8.4): MED, and it was LOW for one commit
                                ; on a number that was measured wrong. Losing
                                ; a window is not ONE ~400 ms reload - a shed
                                ; volume falls back to the pin, only one volume
                                ; can hold that, so two that alternate evict
                                ; each other and reload NINE SECTORS A SWITCH
                                ; for as long as it is gone. 18.8.1 measured
                                ; that state: 45 mounts, 45 loads, 405 of 873
                                ; sectors on the reference copy, against 3 with
                                ; a window. ~17 s, which is "seconds of work
                                ; the user waits through" and not "a little
                                ; I/O". MEM_P_DIRW stays above it at HIGH,
                                ; still ~5x dearer (202 int 13h on an install,
                                ; 18.95). IT IS A RANGE, MEM_P_WSAVE's shape:
                                ; the VOLUME is added to it, 0x20..0x27, so
                                ; there is one per volume and dsk_fatwc is the
                                ; array mem_pg_own names. MEM_P_FATW_N is
                                ; DVOL_MAX, asserted where that is defined -
                                ; a name ending _N is a length, not a tag
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
; for how the rounding is arranged so that they do, and why none of them is a
; residual any more.
SK_KERN    equ 0                ; word: the whole span (SPEC.md 2)
SK_IMG     equ 2                ; word: image + .bss + the cold segment + the
                                ; planar rung + the kernel's tables in .lowbss
SK_FAT     equ 4                ; word: the mount-time FAT window (18.8)
SK_STK     equ 6                ; word: the MAX_TASKS-1 background stacks
SK_STK0    equ 8                ; word: task 0's - the UI task's
SK_DSK     equ 10               ; word: the mount-owned window (2.1.2) - the
                                ; three disk buffers and nothing else
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
section .ovlw                  ; THE BOOT OVERLAY (SPEC.md 2.5.2,
                                ; docs/plans/LAST-DROP-BYTES.md row 11): kmain sizes
                                ; the heap once. mem_unblob_x, which runs LATER
                                ; and gives the blob back, is a different body
                                ; and stays resident - it has to, because it
                                ; runs at the moment this section stops existing
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

    call COLD_SEG:memf_mem_floor_ax           ; THE FLOOR (SPEC.md 39.22), and it is a proc
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
section .cold                   ; ...and back, and mem_unblob_x is exactly why
                                ; the line is here: it is the routine that frees
                                ; the blob, so it is the one body in this file
                                ; that could not live in it
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
; ...and the far entry the BOOT OVERLAY reaches it through (SPEC.md 2.5.2):
; mem_init_x is in the blob and mem_floor_ax is not, mem_unblob_x still
; near-calling it from `.cold`.
memf_mem_floor_ax:  call mem_floor_ax
                    retf

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
    jz .fail0
    mov cl, 6
    shl ax, cl                  ; AX = paragraphs wanted
    jc .fail0                   ; > 1MB asked for
    cmp word [mem_dma], 0x1000  ; ...and a head bigger than a 64KB page can
    ja .fail0                   ; never sit inside one

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
    jae .fail0

    mov di, mem_tab             ; a free record next: refusing early costs
.rec:                           ; nothing and keeps the scan simple
    cmp word [ss:di+MC_SEG], 0
    je .haverec
    add di, MC_SIZE
    cmp di, mem_tab + MEM_MAX*MC_SIZE
    jb .rec
    jmp short .fail0
.haverec:

    cmp word [mem_dir], 0
    jne .hi
    mov dx, [mem_base]          ; DX = candidate base
    jmp short .retry

.fail0:                         ; .fail is out of SHORT reach of the seven
    jmp .fail                   ; refusals above, so NASM was assembling each
                                ; of them as `jcc $+5 / jmp near .fail` - five
                                ; bytes where two will do. One trampoline, in
                                ; a spot nothing can fall into, and they are
                                ; two again. Re-measure it after any change to
                                ; this routine's length: a trampoline only pays
                                ; while it is itself in short reach.
.hi:
    mov dx, [mem_top]           ; the highest base that still fits
    sub dx, ax
    jc .fail0
    cmp dx, [mem_base]
    jb .fail0
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
    cmp word [mem_dir], 0       ; under, with WHICH DOOR it came in through in
    je .lodoor                  ; the top bit of the same word - the two being
    or ax, MC_DMA_HI            ; the whole of where a later move may put it
.lodoor:                        ; (SPEC.md 50.3.2)
    mov [ss:di+MC_DMA], ax
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
    dw MEM_P_FATW,  dsk_fatwc,   MEM_P_FATW_N
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
; -----------------------------------------------------------------------------
; mem_rank_bh - the claimant's purgeable rank (module internal)
; in:  BH = an owner word's high byte
; out: AL = BH when that is a cache level, MEM_LVL_TOP when it is an ordinary
;      claim - which is every high byte outside the range, 0xFF and instance
;      slots alike
; clobbers: AL, flags
;
; SPEC.md 50.6.4's ladder, in ONE door. mem_shed_one and mem_compact both need
; it and mem_compact's own comment said so while keeping a second copy:
; "putting that rule in one door and not the other is how 50.6.4 gets undone".
; -----------------------------------------------------------------------------
mem_rank_bh:
    mov al, MEM_LVL_TOP
    cmp bh, MEM_PG_MIN
    jb .out
    cmp bh, MEM_PG_MAX
    ja .out
    mov al, bh
.out:
    ret

mem_shed_one:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    pushf
    cli
    call mem_rank_bh            ; AL = the claimant's own rank, which is what
    xor di, di                  ; DI = the best candidate so far, 0 = none
                                ; the scan is seeded with, so that only
                                ; STRICTLY cheaper records win
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [ss:si+MC_SEG], 0
    je .next
    mov dl, [ss:si+MC_OWN+1]
    cmp dl, MEM_PG_MIN          ; not a cache at all: never takeable
    jb .next
    cmp dl, al                  ; at or above the best so far (which starts at
    jae .next                   ; the claimant's rank): leave it
    call mem_fatw_dirty         ; ...and never the FAT window somebody is
    jc .next                    ; part-way through writing (SPEC.md 18.8.4)
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
    jmp kretc_cx

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
    mov cx, bx                  ; A FAT WINDOW HAS MORE NAMING WORDS THAN THIS
    sub cx, MEM_P_FATW          ; PROTOCOL CARRIES (SPEC.md 18.8.4): the LIVE
    cmp cx, MEM_P_FATW_N        ; one is also [dsk_fatseg], which every access
    jb .fatw                    ; reads, and [dsk_fatw0], which says what is
    mov word [si], 0            ; resident - and dsk_fat_window believing that
    jmp short .out              ; returns WITHOUT loading, so the reads and
.fatw:                          ; writes after it land in whatever claim owns
    call dsk_fatw_demote        ; those paragraphs now. So the disk layer is
                                ; handed the owner and clears the word itself
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
; 16-bit offset ever binding - the dskw_rdata idiom (SPEC.md 18.4.1). The two
; blocks MAY overlap, and which way decides both the order the chunks are taken
; in and the direction of the copy inside one; see the note on the ascending
; arm below.
; -----------------------------------------------------------------------------
mem_bcopy:
    push ax
    push cx
    push si
    push di
    cld                         ; DF is 0 except inside .uchunk below
    xor dx, dx                  ; a forward chunk starts at its first word
    cmp di, si
    ja .up                      ; destination above source: see below
.chunk:
    jcxz .done
    call mem_bcut
    call mem_bpara
    add si, ax                  ; ...and both bases step by what moved
    add di, ax
    jmp short .chunk
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

    ; DESTINATION ABOVE SOURCE, which is the descending pass's move and the one
    ; case a plain forward copy gets wrong. The banner is right that mem_hifit's
    ; destination cannot overlap its source - but the compactor's destination
    ; comes from the FILL POINT and not from mem_hifit, and a block sliding up
    ; into a gap SMALLER than itself overlaps its own source.
    ;
    ; BOTH HALVES ARE NEEDED AND NEITHER IS SUFFICIENT. Taking the chunks from
    ; the TOP stops one chunk's destination landing on the NEXT chunk's source;
    ; running the copy backwards inside a chunk stops it landing on its OWN. A
    ; shift of a megabyte needs the first, a shift of one paragraph the second.
    ;
    ; CAPPING THE CHUNK AT THE SHIFT would remove the second - a chunk no
    ; longer than the distance it travels cannot overlap itself - and it is
    ; refused on speed: the shift is the hole above the claim, so a 16-paragraph
    ; hole under an 18KB driver image would copy it in 1,152 passes of 256
    ; bytes. mem_bpara's fixed cost is ~250 clocks, which is 60 ms of overhead
    ; on a 4.77 MHz 8088 against 49 ms of copying - and the pass this runs in is
    ; already the machine's last resort before refusing a claim.
.up:
    add si, cx                  ; both bases to one past their last paragraph
    add di, cx
    inc dx                      ; ...and every chunk from here runs backwards
.uchunk:
    jcxz .done
    call mem_bcut
    sub si, ax                  ; ...then DOWN by this chunk, so the HIGHEST
    sub di, ax                  ; one is the one that moves first
    std                         ; ...and the chunk itself runs backwards
    call mem_bpara
    cld                         ; DF back to 0, which the whole kernel assumes
    jmp short .uchunk           ; (CLAUDE.md's register discipline)

; mem_bcut - AX = the next chunk, and CX loses it (module internal)
; in:  CX = paragraphs left; out: AX = min(CX, 64) - 64 paragraphs is 1KB, the
;      pass size both directions take - and CX -= AX
mem_bcut:
    mov ax, 64
    cmp cx, ax
    jae .last
    mov ax, cx
.last:
    sub cx, ax
    ret

; mem_bpara - copy AX paragraphs from segment SI to segment DI (module
;             internal). DX = 0 walks it forwards from the first word; DX != 0
;             backwards from the last, and the caller has set DF to match.
;             Preserves every register; the caller owns the overlap argument
;             above, and this is where BOTH of its answers land.
mem_bpara:
    push cx
    push si
    push di
    push ds
    push es
    mov ds, si
    mov es, di
    mov cx, ax
    shl cx, 1                   ; paragraphs -> words (x16 bytes / 2)
    shl cx, 1
    shl cx, 1
    xor si, si
    or dx, dx
    jz .go
    mov si, cx                  ; ...or the LAST word, which is where a chunk
    shl si, 1                   ; whose destination overlaps its own source has
    sub si, 2                   ; to start
.go:
    mov di, si
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
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
                                ;
                                ; MC_DMA IS NOT TESTED HERE ANY MORE, and the
                                ; sentence it used to carry - "a bus master's,
                                ; and unrelocatable in principle" - was wrong
                                ; on both halves (SPEC.md 66.4.2). The field is
                                ; a PLACEMENT constraint: the 64KB-page-safe
                                ; head in paragraphs, which mem_cp_dest now
                                ; honours exactly as mem_claim's own scan does,
                                ; and mem_regrow's path 3 has relocated one of
                                ; these page-safely since before compaction
                                ; existed. Of the four MC_DMA claims in the
                                ; tree only the Sound Blaster's ring has a chip
                                ; armed on it, and what keeps that one still is
                                ; the same thing that keeps every other claim
                                ; still: nobody declared it
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
%ifdef OS88_DRIVERS
    je .drvimg                  ; A DRIVER IMAGE MAY MOVE (SPEC.md 66.6.3) -
                                ; it is a CS, and a CS moves when nothing
                                ; anywhere refers to it, which is 66.6.1's rule
                                ; applied to the other kind of CS on the heap
%else
    je .pin                     ; kern_small loads no driver at all (kernel.asm
%endif                          ; defines OS88_DRIVERS for KERN_BIG alone), so
                                ; it carries none of 66.6.3 either
    cmp bx, MEM_K_MOD           ; an on-demand module's is still a CS nothing
    je .pin                     ; asks about (SPEC.md 2.8). The boot overlay
                                ; was a third of these until SPEC.md 2.9.6 put
                                ; it in stage 2's blob, where it is not a claim
                                ; at all and there is nothing to pin
    cmp bx, INST_MAX
    jae .notslot
    call mem_is_region          ; an instance slot owns a package's REGION and
    jnc .ok                     ; a built-in's data claims alike, and only the
    call mem_frameless          ; first of those is a CS - and a CS may move
    jc .pin                     ; when nothing anywhere refers to it (SPEC.md
    jmp short .ok               ; 66.6.1)
.notslot:
    cmp bh, 0xFF
    je .ok                      ; a kernel tag: its proc is the kernel's own
%ifdef OS88_DRIVERS
    ; A CHIP MAY BE POINTING AT IT (SPEC.md 66.6.4). MC_DMA marks a claim the
    ; 64KB page rule placed, and §66.4.2 is right that the rule is about where
    ; it may LAND and not about whether it may move - but one of the four such
    ; claims in the tree has a bus master armed on it, the Sound Blaster's
    ; ring, and the 8237 holds an address into it that no relocation proc can
    ; rewrite while it is playing.
    ;
    ; [drv_wcnt] is the kernel's handle on that and it is exact in the
    ; direction that matters: a stream is open only while its refill or drain
    ; task lives (SPEC.md 34.5), so zero means no stream, and no stream means
    ; nothing is armed. It costs the other three MC_DMA claims a refusal only
    ; while some driver has a worker at all, which is rare and always brief.
    ; PARKED IS NOT ENOUGH, for §66.6.3's reason one claim along: a parked
    ; refill task means the DSP is playing, not that it has stopped.
    mov ax, [ss:si+MC_DMA]      ; the HEAD alone: bit 15 is the door, and a
    and ax, MC_DMA_HEAD         ; top-down claim is not a DMA one
    jz .nodma
    cmp byte [drv_wcnt], 0
    jne .pin
.nodma:
%endif
    call mem_busy_seg           ; ...otherwise a package's or a driver's own
    jc .pin                     ; segment, and somebody may be RUNNING in it
    jmp short .ok
%ifdef OS88_DRIVERS
.drvimg:
    ; THREE FACTS AND NO DECLARATION (SPEC.md 66.6.3). The kernel owns every
    ; word that names a driver image - mem_region_reloc's table walk is all of
    ; them - so what it cannot know from its own tables is the only thing worth
    ; asking, and each of these three ASKS rather than trusting a driver to
    ; have remembered:
    mov bx, [ss:si+MC_SEG]
    call mem_in_nest         ; 1. a frame is standing in it right now
    jc .pin
    cmp byte [drv_wcnt], 0      ; 2. ...or ANY driver worker exists at all -
    jne .pin                    ;    and PARKED IS NOT ENOUGH HERE. A driver's
                                ;    service task runs IN the image, so its
                                ;    stack carries the image's CS as return
                                ;    addresses whether it is parked or not;
                                ;    inst_svc_parked answers the question a
                                ;    driver's DATA claim asks, not the one its
                                ;    IMAGE asks. [drv_wcnt] is exactly right and
                                ;    already exact - "zero means no task's CS
                                ;    points into the image" is drv_unload's own
                                ;    reason for waiting on it before it frees
                                ;    one (SPEC.md 51.7). No [mem_wpin]: a park
                                ;    does not lower this count, only an exit
                                ;    does
%endif
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
; mem_cp_mine - is the claim at SI THIS PASS's business? (module internal)
; in:  SI = record
; out: CF = 0 this pass may move it, CF = 1 leave it alone
; clobbers: nothing else (flags)
;
; A claim goes back through the door it came in by. The ascending pass packs
; the data arena DOWN and must not drag a top-down claim with it: doing so
; moves the free run from BELOW the whole ceiling stack to BETWEEN the movers
; and the stayers, which is two runs where there was one and strictly worse
; than leaving it (SPEC.md 66.4). The descending pass is the mirror.
;
; SO IT IS THREE INSTRUCTIONS AND NO BRANCH AT ALL. The door lives in MC_DMA's
; top bit and the pass direction is a mask of all ones or none, so XOR-ing them
; leaves that bit CLEAR exactly when they agree - and `add ax, ax` lifts it
; straight into the carry the caller wants. `pop` writes no flags, which is
; what lets AX come back between the answer and the `ret`. mem_is_hi was a
; routine of its own for one caller and is folded in here.
; -----------------------------------------------------------------------------
mem_cp_mine:
    push ax
    mov ax, [ss:si+MC_DMA]      ; bit 15 is the door it came in by...
    xor ax, [mem_cp_msk]        ; ...complemented coming down, so it is 0
    add ax, ax                  ; exactly when it agrees with this pass - and
    pop ax                      ; `add` lifts that bit straight into the carry
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
    mov ax, bx                  ; the slot to its record - inst_ptr's own job
    call KERNEL_SEG:cw_inst_ptr ; (SPEC.md 29.4), and the shim already exists.
                                ; mem_can_move has tested cmp bx, INST_MAX /
                                ; jae before calling, so AH is 0 - and inst_ptr
                                ; zeroes it anyway
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
; mem_frameless - is there NO frame anywhere referring to this region? (internal)
; in:  SI = the record, BX = its owner (an instance slot, already tested)
; out: CF = 0 nothing refers to it, CF = 1 something might
; clobbers: nothing else (flags)
;
; SPEC.md 66.6.1, and it is three exact questions rather than a stack scan.
; The scan is refused for SPEC.md 66.3 rule 5's own reason: a heap segment
; number shares a 16-bit range with kernel return addresses and a package's own
; near pointers, so a sweep that PATCHED would corrupt a return address
; silently. And it could not work anyway - the six ways a segment reaches a
; stack (docs/plans/HEAP-UNPIN-PLAN.md 4.1) include whatever the package pushed
; itself, at an offset nothing can compute.
;
; What a design CAN know is that no copy was ever made, and that is counting:
;
;   the RESTART declaration   a region that owns a worker at all is pinned
;                             without one (SPEC.md 66.6.2), because the
;                             worker's stack carries this segment from before
;                             its first instruction and at a depth nothing can
;                             find. That is the only question here that is this
;                             routine's own, and it is asked FIRST so that a
;                             region a park could not have helped never raises
;                             [mem_wpin] and never spends the pass's one park
;   MC_SEG != [ld_base]       it is not the region being loaded right now
;   mem_in_nest               no kernel -> package call into THIS region is in
;                             flight - the walk a driver IMAGE asks for as
;                             well, both being a CS on the heap. The two doors
;                             are AUDITED, not assumed: `call far` into a
;                             package happens at wm_pkgcall and at the loader's
;                             entry call and nowhere else in the kernel
;   mem_busy_seg              ...and then the question a package's DATA claim
;                             already asks: is its worker running.
;                             inst_seg_parked IS "no worker, or one that has
;                             parked at OSAPI_TASK_ALIVE" and raises
;                             [mem_wpin] itself. This used to be two tests of
;                             I_TASK and [sch_parked] written out here, which
;                             had to keep agreeing with the ones next door
;
; WHAT MAKES THE COUNTER SAFE IS ALREADY THERE: mem_compact raises [sch_lock]
; across the plan AND the moves, so no task can raise the depth between this
; test and the copy. The one window is the park request, which drops the lock -
; and the run pass re-calls mem_can_move per block under the re-raised lock, so
; a design that hoisted this out of that loop for speed would be wrong in a way
; nothing would catch.
; -----------------------------------------------------------------------------
mem_frameless:
    push ax
    push bx
    push di
    mov ax, bx                  ; the slot to its record - inst_ptr's own job
    call KERNEL_SEG:cw_inst_ptr ; (mem_is_region's own shim), and mem_can_move
                                ; has tested cmp bx, INST_MAX / jae, so AH is 0
    cmp byte [di+I_TASK], 0xFF
    je .seg                     ; no worker at all: SPEC.md 66.6.1's own case
    ; IT OWNS A WORKER, and 66.6.2 is the only way past that. Has the package
    ; DECLARED a restart point? Without it the worker's stack carries this
    ; segment at a depth nothing can find, and no amount of waiting changes
    ; that - so this arm is a hard pin, and asking for a park would be asking
    ; for nothing. Raising [mem_wpin] here would spend the pass's one park
    ; request on a region that could not have used it, which is why the
    ; declaration is tested BEFORE the running/parked question below.
    shl bx, 1                   ; the side table is WORDS - it holds an offset
    cmp word [bx+inst_restart], 0
    je .nope
.seg:
    mov bx, [ss:si+MC_SEG]
    cmp bx, [ld_base]
    je .nope                    ; the loader is standing in it
    call mem_in_nest            ; ...or a callback is, and this claim is the
    jc .nope                    ; REGION, so a frame's return address points
                                ; into it (SPEC.md 66.6.1). No [mem_wpin]: a
                                ; park does not retire a return address
    call mem_busy_seg           ; ...AND THE REST IS THE QUESTION A PACKAGE'S
    jc .nope                    ; DATA CLAIM ALREADY ASKS: is a frame standing
                                ; in the segment, and is its worker running.
                                ; This used to re-derive both from [wm_pkgs],
                                ; I_TASK and [sch_parked] by hand - three
                                ; answers that had to keep agreeing with
                                ; mem_busy_seg's, for a region rather than for
                                ; a claim the region owns. inst_seg_parked IS
                                ; "no worker, or one that has parked at
                                ; OSAPI_TASK_ALIVE" (SPEC.md 66.5.4), which is
                                ; exactly what 66.6.2 needs once the
                                ; declaration above is in hand - and it raises
                                ; [mem_wpin] itself, so a region asks for the
                                ; park on the same terms a data claim does
    pop di
    pop bx
    pop ax
    clc
    ret
.nope:
    pop di
    pop bx
    pop ax
    stc
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
; **IT DOES NOT ASK THE NEST, AND MUST NOT.** A package's data claim may move
; while the package itself is executing - that is SPEC.md 66.3 rule 2's whole
; bargain, re-read your base at each use - and a package reaches mem_claim only
; from inside its own callback, so a compactor that pinned on "a frame is in
; the owning segment" would refuse every claim of the package that asked. The
; frame question belongs to the CS itself, where mem_frameless and
; mem_can_move's driver-image arm ask it. A DRIVER's claim needs no such test
; here either: it matches no instance, so inst_seg_parked pins it outright.
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
.pin:
    stc
    ret
.free:
    clc
    ret

; -----------------------------------------------------------------------------
; mem_in_nest - is a frame standing in the image at BX? (module internal)
; in:  BX = a segment
; out: CF = 1 somebody is inside it, CF = 0 nobody is
; clobbers: nothing else (flags)
;
; SPEC.md 66.6.1 and 66.6.3, WHICH ARE ONE QUESTION. A package region and a
; driver image are both a CS on the heap, and "may it move out from under a
; return address" has the same answer and the same evidence for either: the
; eight sites that far-call a driver image and the one that far-calls a package
; bracket themselves with wm_nest_pushd / wm_nest_pop, so wm_pkgs[0..wm_pkgd)
; is exactly the set of images the machine is executing in. It was two stacks
; and two walks - and `sbl_v_grant` CLAIMS, so that set is non-empty at
; precisely the moment a driver-triggered compaction runs, which is why neither
; can be a depth.
;
; A segment that is neither cannot collide with one that is, so one table
; answers both callers without either learning about the other.
; -----------------------------------------------------------------------------
mem_in_nest:
    push cx
    push di
    mov cx, [wm_pkgd]
    cmp cx, WM_PKGD_MAX
    ja .yes                     ; deeper than wm_pkgs records: pin everything
    mov di, wm_pkgs
    jcxz .no                    ; nothing is being called at all
.s:
    cmp [di], bx
    je .yes
    inc di
    inc di
    loop .s
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop di
    pop cx
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
; -----------------------------------------------------------------------------
; mem_fatw_dirty - is the record at SI the LIVE FAT window, with FAT edits in
;                  it that have not reached the disk? (SPEC.md 18.8.4)
; in:  SI = record pointer
; out: CF = 1 do not take it, CF = 0 it may go
; clobbers: nothing else (flags)
;
; mem_in_xfer's shape and for mem_in_xfer's reason - TWO callers that must not
; drift, mem_shed_one and mem_cp_drop. Shedding a FAT window costs nine sectors
; to read back and nothing worse, WHICH IS THE WHOLE POINT of it being a cache;
; shedding a DIRTY one throws away a half-built chain instead.
;
; Only the live window can be dirty: dsk_fatw_park flushes the outgoing volume
; at the top of every mount, so a banked window is clean by construction. And
; this is belt to dskw_flush's braces rather than the only thing standing
; there - a demote leaves [dsk_fatw0] = 0xFFFF, which dskw_flush already reads
; as "there is no buffer this range could be written out of" and refuses.
; -----------------------------------------------------------------------------
mem_fatw_dirty:
    push ax
    cmp word [dsk_fatd0], 0xFFFF
    je .no                      ; nothing is dirty anywhere
    mov ax, [dsk_fatseg]
    cmp ax, [ss:si+MC_SEG]      ; ...and is THIS the window it is dirty in?
    jne .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

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
    call mem_fatw_dirty         ; SPEC.md 18.8.4, and mem_shed_one's other half
    jc .no
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
    jmp short mem_pg_cheap
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; mem_pg_cheap - is this record a cache STRICTLY cheaper than [mem_pg_rank]?
; in:  SI = a live claim record
; out: CF = 0 takeable, CF = 1 not
; clobbers: flags
;
; mem_shed_one's two compares, and they have to be the same two: is it a cache
; at all, and is it strictly cheaper than the claimant. The second subsumes the
; range's top - 0xFF is never below a level, and neither is an instance slot.
; Two routines read the same global with the same pair of compares, so it is
; one door: mem_cp_drop falls in here and mem_avail_lvl_x's total calls it.
; (mem_shed_one's own copy is deliberately NOT merged: it is inside the scan
; and holds its candidate's level in AL rather than reading the global.)
; -----------------------------------------------------------------------------
mem_pg_cheap:
    push ax
    mov al, [ss:si+MC_OWN+1]
    cmp al, MEM_PG_MIN
    jb .no
    cmp al, [mem_pg_rank]
    jae .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; mem_cp_next - the live claim the walk has not passed yet (module internal)
; in:  [mem_cp_key] = the base segment to search from (mem_cp_fill0 sets it and
;      mem_cp_step advances it); BX is the FILL POINT and is left alone
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
    push bx
    push cx
    push dx
    push di
    push bp
    mov bx, [mem_cp_key]
    mov bp, [mem_cp_msk]        ; ONE SCAN AND NOT TWO. Complementing a segment
                                ; REVERSES the order, so "the lowest base at or
                                ; above the key" in complemented space IS "the
                                ; highest at or below it" in real space - and
                                ; the descending scan was these same nine
                                ; instructions with every comparison turned
                                ; round. The mask lives in a REGISTER for the
                                ; reason the two loops existed at all: the body
                                ; runs MEM_MAX times a call, and `xor ax, bp`
                                ; is three clocks where a memory read is twenty
    xor bx, bp                  ; the key, in the same space
    xor si, si                  ; SI = best record so far, 0 = none
    mov dx, 0xFFFF              ; DX = its base, and the walk always wants the
    mov di, mem_tab             ; LOWEST one now
    mov cx, MEM_MAX
.scan:
    mov ax, [ss:di+MC_SEG]
    or ax, ax
    je .next                    ; a free record - tested BEFORE the mask, 0
    xor ax, bp                  ; being the VALUE and not a base
    cmp ax, bx
    jb .next                    ; already behind the walk
    cmp ax, dx
    jae .next                   ; not the nearest
    mov dx, ax
    mov si, di
.next:
    add di, MC_SIZE
    loop .scan
    or si, si
    jz .none
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.none:
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; THE EIGHT PLACES THE TWO PASSES DIFFER (SPEC.md 66.4)
;
; One walk, parameterised, rather than a descending copy of it. The two
; directions disagree about eight decisions and about nothing else, so each is
; a routine here and the walk calls them - which is what keeps the two passes
; step for step, the property SPEC.md 66.4 says is the one way this feature can
; promise room it does not deliver.
;
; A duplicated pair would have been ~220 bytes against ~120 and would have to
; be kept in step by hand. The PLAN and the RUN used to need the same care and
; no longer do: they are two entries into one body now (mem_cp_walk), so the
; only thing parameterised here is the direction.
;
; [mem_cp_msk] IS A MASK and not a flag - 0 going up, 0xFFFF coming down -
; because two of the eight want it as one. mem_cp_next complements a segment to
; reverse the ORDER, which turns its two scans into one; mem_cp_mine XORs it
; with the door bit, which turns a two-branch compare into three instructions.
; Every other reader takes the LOW BYTE against zero and cannot tell.
; -----------------------------------------------------------------------------
mem_cp_fill0:                   ; BX = the fill point to start from, and the
    push ax                     ; search key that walks with it - and THE KEY
    mov ax, [mem_cp_msk]        ; STARTS AS THE MASK, both being 0 going up and
    mov [mem_cp_key], ax        ; 0xFFFF coming down: "no claim is behind the
    mov bx, [mem_base]          ; walk yet" is the bottom of the arena in one
    or ax, ax                   ; direction and the top in the other, which is
    jz .out                     ; what complementing means
    mov bx, [mem_top]
.out:
    pop ax
    ret

mem_cp_step:                    ; the key steps past SI's ORIGINAL base, which
    push ax                     ; is the one quantity that reads the same in
    mov ax, [ss:si+MC_SEG]      ; both passes
    cmp byte [mem_cp_msk], 0
    je .up
    dec ax
    jmp short .set
.up:
    inc ax
.set:
    mov [mem_cp_key], ax
    pop ax
    ret

mem_cp_near:                    ; AX = the edge that must MEET the fill point
    mov ax, [ss:si+MC_SEG]      ; for the claim to be already packed: its base
    cmp byte [mem_cp_msk], 0     ; going up, its end coming down
    je .out
    add ax, [ss:si+MC_PARA]
.out:
    ret

mem_cp_far:                     ; AX = where the fill point resumes past a
    mov ax, [ss:si+MC_SEG]      ; barrier: its end going up, its base coming
    cmp byte [mem_cp_msk], 0     ; down
    jne .out
    add ax, [ss:si+MC_PARA]
.out:
    ret

mem_cp_adv:                     ; BX past a claim this pass just packed -
    mov bx, di                  ; FROM ITS DESTINATION and not from the old
    cmp byte [mem_cp_msk], 0     ; fill point, because mem_cp_dest's page bump
    jne .out                    ; can put the two apart (SPEC.md 66.4.2).
    add bx, [ss:si+MC_PARA]     ; Coming down the destination IS the new fill
.out:                           ; point; going up it is a block-length below it
    ret

; mem_cp_dest - DI = where SI's bytes belong at the fill point, PAGE-SAFELY.
;
; Going up that is the fill point; coming down it is a block-length below it.
; And then, for a claim carrying MC_DMA, it is bumped off a 64KB physical page
; boundary its head would straddle - the same arithmetic mem_claim_1's own scan
; does, in the same two directions (SPEC.md 66.4.2).
;
; THE BUMP IS THE ONE THING IN THIS WALK THAT CAN MOVE A DESTINATION THE WRONG
; WAY, and both passes rest on it not doing so: going up a claim only ever
; slides DOWN, coming down it only ever slides UP. So the direction that
; relieves the straddle is chosen to agree - the next page's floor going up,
; the highest base in THIS page going down - and a destination that would
; still pass the claim's own base is turned into "do not move it", which both
; callers already handle because it is the same answer as "already packed".
mem_cp_dest:
    mov di, bx
    cmp byte [mem_cp_msk], 0
    je .page
    sub di, [ss:si+MC_PARA]
.page:
    push ax
    push cx
    mov ax, [ss:si+MC_DMA]      ; AX = the page-safe head, 0 = no constraint
    and ax, MC_DMA_HEAD         ; (bit 15 is the door, and no business of the
    jz .out                     ; arithmetic below)
    mov cx, di                  ; the head's LAST paragraph...
    add cx, ax
    dec cx
    xor cx, di                  ; ...against its first: if they agree in the
    test ch, 0xF0               ; top four bits the whole head is inside one
    jz .out                     ; 64KB physical page, which is the rule
    mov cx, di
    and cx, 0xF000
    add cx, 0x1000              ; the top of this page
    jc .stay                    ; ...past 1MB, so there is no next page at all
    cmp byte [mem_cp_msk], 0
    je .up                      ; going up, that floor IS the answer
    sub cx, ax                  ; coming down, back off by the head: the
    mov di, cx                  ; highest base in THIS page that still fits
    cmp di, [ss:si+MC_SEG]
    jae .out                    ; ...and a bump PAST its own base is not
    jmp short .stay             ; packing, it is moving it
.up:
    mov di, cx
    cmp di, [ss:si+MC_SEG]
    jbe .out                    ; the mirror going up
.stay:
    mov di, [ss:si+MC_SEG]      ; the bump has nowhere legal to go: leave it
.out:
    pop cx
    pop ax
    ret

; mem_cp_gap / mem_cp_tail leave the SUBTRACTION's flags live, and both callers
; read them with `jbe`: zero says the claim is already packed against the fill
; point, and a borrow says the walk has gone wrong and the answer must not be
; taken as a run.
mem_cp_gap:                     ; AX = the hole between the fill point and SI
    call mem_cp_near
    jmp short mem_cp_sub

mem_cp_tail:                    ; AX = the run from the fill point to the end
    mov ax, [mem_top]           ; of the arena this pass is walking into
    cmp byte [mem_cp_msk], 0
    je mem_cp_sub
    mov ax, [mem_base]

; mem_cp_sub - AX = the distance from the fill point in BX to AX, measured the
; way this pass walks, and it leaves the SUBTRACTION's flags live because both
; callers read them with `jbe`: zero says the claim is already packed against
; the fill point, and a borrow says the walk has gone wrong and the answer must
; not be taken as a run. Two callers, one direction test - the gap beside a
; barrier and the run past the last claim are the same measurement.
mem_cp_sub:
    cmp byte [mem_cp_msk], 0
    jne .down
    sub ax, bx
    ret
.down:
    push bx                     ; POP WRITES NO FLAGS, which is what lets the
    xchg ax, bx                 ; fill point come back after the subtraction
    sub ax, bx                  ; that the caller is about to branch on
    pop bx
    ret

; -----------------------------------------------------------------------------
; mem_cp_plan / mem_cp_run - what WOULD compaction achieve, and do it
; mem_cp_plan  out: CX = the largest run it would leave, DX = how many claims
;                   would move or dissolve; AX, BX, SI, DI preserved
; mem_cp_run   in:  nothing; [sch_lock] already raised by mem_compact
;              out: nothing; AX, BX, SI, DI preserved, CX and DX spent
;
; ONE WALK AND TWO ENTRIES. They were two copies of the same twenty-line body,
; and SPEC.md 66.4 rests on them agreeing step for step - the plan promises a
; run the run has to deliver, so a walk that diverged would report room that
; never arrives. Keeping two bodies in step is a thing somebody has to remember;
; one body with the moves behind a flag is a thing nobody can get wrong.
;
; BP IS THE FLAG, and a register rather than a byte because it is read three
; times a claim: 0 counts, 1 moves. It is a VALUE and never an address, so the
; SS != DS rule is untouched (CLAUDE.md), and every routine the walk calls
; either preserves BP or - mem_cp_next, mem_reloc_call - banks it.
;
; THE RUN COUNTS TOO, into CX and DX it does not return. The accounting is four
; instructions a barrier against a `rep movsw` of up to 64KB, and leaving it in
; is what makes the two walks ONE walk rather than one walk with a hole in it.
;
; Order inside one moved block is binding and is the purgeable shed's rule
; turned round: the bytes go first, then the record, then the holder is told -
; so there is no instant in which the map names a block whose contents have not
; arrived, and none in which the holder has been sent to an address the map
; does not agree with.
; -----------------------------------------------------------------------------
mem_cp_plan:
    push bp
    xor bp, bp                  ; COUNT what would move, and touch nothing
    jmp short mem_cp_walk
mem_cp_run:
    push bp
    mov bp, 1                   ; ...or the same walk with the move in it
mem_cp_walk:
    push ax
    push bx
    push si
    push di                     ; mem_cp_dest's answer, and mem_avail plans
                                ; through here (SPEC.md 66.10.3)
    xor dx, dx                  ; movers
    xor cx, cx                  ; largest run
    mov byte [mem_wpin], 0      ; ...and "a worker is what stopped me", which
                                ; mem_busy_seg raises (SPEC.md 66.5)
    call mem_cp_fill0           ; BX = the fill point - the first paragraph
                                ; nothing has been packed into yet - and the
                                ; search key that walks with it. Both depend on
                                ; the direction; nothing else in this body does
.loop:
    call mem_cp_next            ; SI = the next claim this pass has not passed
    jc .tail
    call mem_cp_step            ; step the key past the ORIGINAL base, banked
                                ; here because a move is about to overwrite it
    call mem_can_move
    jc .pinned
    call mem_cp_mine            ; ...and is it THIS pass's to move (66.4)
    jc .pinned
    call mem_cp_dest            ; DI = where its bytes go: the fill point going
    cmp di, [ss:si+MC_SEG]      ; up, a block-length below it coming down, and
    je .stay                    ; off a 64KB page boundary its MC_DMA head
                                ; would straddle. Equal to its own base means
                                ; already where it belongs
    or bp, bp
    jz .count
    call mem_cp_move
.count:
    inc dx
.stay:
    call mem_cp_adv             ; the fill point advances past the DESTINATION
    jmp short .loop
.pinned:
    call mem_cp_drop            ; ...unless it is a cache this pass may
    jc .barrier                 ; dissolve (SPEC.md 66.10): the fill point does
                                ; NOT advance and no barrier is recorded, so
                                ; the plan counts exactly the room the run is
                                ; about to produce. The KEY has already stepped
                                ; past it, so it is not revisited
    or bp, bp
    jz .count2
    call mem_cp_kill
.count2:
    inc dx
    jmp short .loop
.barrier:
    call mem_cp_gap             ; the gap beside a pinned claim is a hole that
    jbe .nogap                  ; compaction cannot close - it is a barrier,
    cmp ax, cx                  ; and claims past it still pack against it
    jbe .nogap
    mov cx, ax
.nogap:
    call mem_cp_far             ; the fill point resumes past it
    mov bx, ax
    jmp short .loop
.tail:
    call mem_cp_tail            ; ...and the run past everything, which is the
    jbe .done                   ; one compaction is usually enlarging
    cmp ax, cx
    jbe .done
    mov cx, ax
.done:
    pop di
    pop si
    pop bx
    pop ax
    pop bp
    ret

; -----------------------------------------------------------------------------
; mem_cp_move - the RUN pass's move (module internal)
; in:  SI = the record, BX = the fill point, DI = where its bytes belong
; out: BX and SI preserved; AX, CX and DX are spent
;
; A DRIVER IMAGE MOVES AT IF=0, AND ONLY A DRIVER IMAGE (SPEC.md 66.6.3.1). Its
; vectors name the OLD segment until mem_region_reloc rewrites them, so an ISR
; taken between the copy and that rewrite runs the old copy - whose bytes are
; still intact but whose BSS is about to be left behind, so a write it makes
; there is lost. Six bytes closes the whole window, for every vector including
; the four SOUND.DRV installs while it is probing, where masking one line would
; not.
;
; WHAT IT COSTS is one image copy's worth of latency, charged once and only
; when a driver image actually moves: ~17 ms for the sound driver's 6KB, and
; ~50 ms - three ticks - for the largest image in the tree, ETHER's 18KB. That
; is the honest bound and not the typical case. What three lost ticks cost: the
; BIOS clock, and at most one mouse PACKET, which mou_isr resyncs on the
; framing bit rather than corrupting. It cannot land inside a floppy transfer -
; dsk_xfer holds [sch_lock] and claims nothing - and it cannot land inside IRQ
; discovery, which runs on the UI task with a frame in the image that
; mem_in_nest already refuses on.
;
; Ordinary compaction is untouched: every other block takes this path with the
; flag unchanged. And no PACKAGE code runs inside it, a driver image's holder
; being mem_region_reloc and not a dispatcher.
; -----------------------------------------------------------------------------
mem_cp_move:
    pushf
    cmp word [ss:si+MC_OWN], MEM_K_DRV
    jne .noff
    cli
.noff:
    push si                     ; the RECORD: SI is about to become the source
    push bx                     ; the fill point
    push di                     ; ...and the destination, for MC_SEG below
    mov cx, [ss:si+MC_PARA]     ; only what is claimed travels
    mov si, [ss:si+MC_SEG]
    call mem_bcopy              ; source SI, destination DI, CX paragraphs -
                                ; segment-stepped, so a block over 64KB moves
                                ; without a 16-bit offset ever binding, and
                                ; from the top down when DI is above SI
    pop dx                      ; DX = the new base
    pop bx
    pop si
    mov ax, [ss:si+MC_SEG]      ; AX = where it WAS, for the notification
    mov [ss:si+MC_SEG], dx      ; the map follows the bytes...
    push bx
    mov bx, ax
    call mem_reloc_call         ; ...and the holder follows the map (BX = old
    pop bx                      ; base, DX = new)
    popf                        ; ...and IF back, whatever it was
    ret

; mem_cp_kill - the RUN pass's dissolve (module internal). The owner is told
; BEFORE the record goes - mem_shed_one's order, for its reason: there is no
; instant in which a kernel word names memory somebody else already holds. BX
; is the FILL POINT and does not move, which is what makes this walk the plan's
; step for step.
mem_cp_kill:
    push bx
    mov bx, [ss:si+MC_OWN]
    call mem_pg_forget
    pop bx
    mov word [ss:si+MC_SEG], 0
    ret

section .text                   ; A KERNEL RELOCATION PROC IS A NEAR PROC IN
                                ; .text: cw_mem_disp reaches it with `call bp`
                                ; and CS = KERNEL_SEG, so a .cold body would be
                                ; entered at the wrong segment - which is why
                                ; this block sits in .text inside a .cold file)
; -----------------------------------------------------------------------------
; mem_rr_walk - re-point every word in a table group from BX to DX (internal)
; in:  DS:SI = the group, ES = the segment its rows address, BX = the segment
;      they may name, DX = the one they should name
; out: SI past the group's terminator
; clobbers: AX, CX, DI (flags)
;
; ONE LOOP FOR FOUR ADDRESS SPACES, which is what makes the eleven holders of
; SPEC.md 66.6 cost a table row each instead of a loop each. A row is
; `dw base` + `db count` + `db stride`: four bytes, and a SCALAR is the row
; with count 1 and stride 0, so a lone kernel word costs the same four bytes it
; cost as an entry in a pointer list with a second loop to read it. The group
; ends at a zero base and the caller moves ES on and calls again - the kernel's
; own .bss through DS, .lowbss through SS, and the interrupt vectors through 0.
;
; SPEC.md 66.6.3.1. SIXTEEN VECTORS AND NOT 256, AND THAT IS THE WHOLE SAFETY
; OF IT. The first build of this walked the entire table on the argument that
; asking the 8086 is right about a driver that has not been written yet - and
; it CORRUPTED A MACHINE, which is the finding worth keeping: **the IVT is not
; a table of pointers.** Its unused slots are scratch, and third parties use
; them as such. Measured on `os8088_xt_hdd`, whose XT-IDE option ROM keeps two
; words at int C1h and int C3h: one of them read 0x8000, a package claim moved
; off 0x8000, this loop rewrote the ROM's word to 0x6000, and the hard disk
; then probed as **"No hardware found"** - a machine with no C: drive, from a
; heap compaction, silently. `tests/hdmove.py` is what caught it.
;
; So the loop is cut to the slots a driver can legitimately own: the hardware
; IRQ vectors. That is a FACT and not a guess - a driver hooks its own IRQ line
; and there is nowhere else for one to go (SPEC.md 51) - and it is the same
; argument SPEC.md 66.3 rule 5 makes against a stack scan, one address space
; along: a sweep that PATCHES on a coincidence corrupts, where one that merely
; refuses is only conservative. The old mem_ivt_names had the identical false
; positive and was harmless, because all it did was pin. The rule survives the
; merge as two ROWS of eight rather than a loop of its own.
; -----------------------------------------------------------------------------
mem_rr_walk:
.row:
    lodsw                       ; AX = the base of this row's first word
    xchg ax, di
    or di, di
    jz .done                    ; a zero base ends the group
    lodsw                       ; AL = how many, AH = the stride between them
    mov cl, al
    xor ch, ch
    mov al, ah
    xor ah, ah                  ; AX = the stride; a SCALAR row's is 0 and its
.l:                             ; count 1, so it never uses it
    cmp [es:di], bx
    jne .n
    mov [es:di], dx
.n:
    add di, ax
    loop .l
    jmp short .row
.done:
    ret

; -----------------------------------------------------------------------------
; mem_region_reloc - THE KERNEL'S HALF of a package REGION's move (SPEC.md 66.6)
; in:  BX = the base segment it WAS at, DX = the base it is at NOW
; out: nothing; every register preserved
;
; SPEC.md 66.6 lists eleven holders and it listed four for a year. Each of the
; seven that were missing fails in a way nobody would trace back to a
; compaction - a stale [menu_seg] draws every bar title out of the wrong
; segment, a stale [fdlg_rqsp] makes the completion callback silently skip so
; the user's Save does nothing at all.
;
; UNCONDITIONAL, and that is the design: a block that is not a region matches
; no row here, and the walk is ~70 compares against a
; rep movsw of up to 64KB. Making it conditional would put a second copy of
; "is this a region" somewhere that has to be kept agreeing with mem_is_region.
;
; THREE ADDRESS SPACES, which is why there are three loops. wm_wins, inst_tab
; and the seven scalars are .bss and reached through DS; menu_bar is .lowbss
; and mem_tab is the claim table, both reached through SS (SPEC.md 2.6).
;
; What needs NOTHING is most of the kernel: sound grants, XMS blocks, toast
; ownership, the dock, the clipboard, wm_owner and every wm_about / wm_onwk /
; wm_oncl / wm_onrc / wm_pref hook are keyed on an instance SLOT or on a near
; offset read live through W_SEG. And a package's own code needs nothing at
; all - it is org 0 with no relocation of any kind, so every near offset inside
; the image survives untouched and only SEGMENT words are ever wrong.
; -----------------------------------------------------------------------------
mem_region_reloc:
    push ax
    push cx
    push si
    push di
    push es
    cld                         ; mem_rr_walk reads its rows with `lodsw`
    mov si, mem_rr_tab
    push ds
    pop es                      ; ES = KERNEL_SEG: wm_wins, inst_tab, drv_tab
    call mem_rr_walk            ; and the eight lone words, all .bss
    push ss
    pop es                      ; ES = LOW_SEG: the menu bar and the claim map
    call mem_rr_walk
%ifdef OS88_DRIVERS
    xor ax, ax
    mov es, ax                  ; ES = 0: the hardware IRQ vectors, the fourth
    call mem_rr_walk            ; address space and the only one outside this
%endif                          ; kernel (SPEC.md 66.6.3.1)
    ; ...AND A WORKER STANDING ON THE OLD SEGMENT is re-entered at the new one
    ; (SPEC.md 66.6.2). The other half of mem_frameless's 66.6.2 arm: that one
    ; decided the move was allowed, this makes it true. A region that owns no
    ; worker, or one whose package never declared a restart point, matches
    ; nothing here and pays four compares - and could not have got this far
    ; anyway, mem_can_move having refused it.
    ;
    ; IT LOOKS UP THE **NEW** SEGMENT, and that is not a shortcut: the walk
    ; above has already put every I_SPTR right, so the record that matches BX
    ; no longer exists and DX is the only thing that finds it. Moving this
    ; above the walk would silently stop finding anything.
    push bx
    mov bx, dx
    call inst_of_seg            ; DI = the live KIND_PKG record at the new base
    jc .nowk
    mov al, [di+I_TASK]
    cmp al, 0xFF
    je .nowk                    ; no worker: nothing to re-enter
    call inst_fhome_idx         ; BX = the slot, the one place the side tables'
    shl bx, 1                   ; indexing lives
    mov cx, [bx+inst_restart]
    jcxz .nowk                  ; never declared: it cannot have moved, and if
    mov bx, dx                  ; it did, the wrong thing to do is guess
    call sch_wk_restart         ; AL = slot, BX = the new segment, CX = entry
.nowk:
    pop bx
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- THE ELEVEN HOLDERS, AS ROWS (SPEC.md 66.6) ------------------------------
; `dw base, db count, db stride`, in three groups by the segment they live in.
; What needs NOTHING is most of the kernel: sound grants, XMS blocks, toast
; ownership, the dock, the clipboard, wm_owner and every wm_about / wm_onwk /
; wm_oncl / wm_onrc / wm_pref hook are keyed on an instance SLOT or on a near
; offset read live through W_SEG. And a package's own code needs nothing at
; all - it is org 0 with no relocation of any kind, so every near offset inside
; the image survives untouched and only SEGMENT words are ever wrong.
%macro MEM_RR_ROW 3
    dw %1
    db %2, %3
%endmacro
%macro MEM_RR_ONE 1
    dw %1
    db 1, 0                     ; a lone word: one of it, and no stride
%endmacro
mem_rr_tab:
    ; --- ES = KERNEL_SEG, the kernel's own .bss --------------------------
    MEM_RR_ROW wm_wins + W_SEG,   MAX_WIN,  WIN_SIZE
    MEM_RR_ROW inst_tab + I_SPTR, INST_MAX, I_RECSZ
    ; A DRIVER IMAGE (SPEC.md 66.6.3) is named the same way a region is: by
    ; kernel words in tables. It is rows here rather than a proc of its own
    ; because mem_region_reloc already runs unconditionally for every move
    ; (mem_reloc_call), and a block that is not a driver matches none of them.
%ifdef OS88_DRIVERS
    MEM_RR_ROW drv_tab + DRVR_SEG, DRV_MAX, DRVR_SIZE
    MEM_RR_ROW drv_fseg,           5,       4   ; the five published class fast
%endif                                          ; paths, contiguous BY DESIGN
                                                ; (driver.inc:841)
    ; A DONATED listing claim (SPEC.md 66.5.10.2): the driver owns it, so the
    ; owner's own proc reaches one of its three naming words and these are the
    ; other two. BY VALUE and over free rows too - a compaction can fire while
    ; a different volume is current, and a free row's DV_SEG is stale rather
    ; than meaningless (SPEC.md 6.1's clear leaves it alone).
    MEM_RR_ROW dsk_vtab + DV_SEG, DVOL_MAX, DV_SIZE
    MEM_RR_ONE dsk_dseg         ; ...and where the listing is READ FROM, which
                                ; equals the row only while that volume is the
                                ; one mounted
    ; TWO ADJACENT WORDS ARE ONE ROW, and each pair carries a %error beside its
    ; own declaration to keep it that way: a row is four bytes whether it names
    ; one word or twelve, so a neighbour rides free - and an edit that puts
    ; something between them fails the build where the words are declared
    ; rather than silently dropping a holder here.
    MEM_RR_ROW menu_seg, 2, 2   ; the bar's owner, and the DROPPED menu, which
                                ; is live in exactly the context SPEC.md 66.3
                                ; rule 3 names
    MEM_RR_ONE fdlg_rqsp        ; the file dialog's staleness cookie
%ifdef KERN_BIG
    MEM_RR_ONE drv_dlg_seg      ; kern_small's driver file-dialog slot is a
%endif                          ; refusing stub and has no state (62.9.15)
    MEM_RR_ROW dskw_seg, 2, 2   ; a CALLER's segment and the advancing one,
    MEM_RR_ONE dskw_czseg       ; banked across a whole file transfer
    ; ...and the driver words that are NOT in drv_tab. Three of them are rows
    ; shaped like a drv_tab row and deliberately outside it (SPEC.md 41.12.5),
    ; and a walk of the table alone would miss every one.
%ifdef OS88_DRIVERS
    MEM_RR_ONE ss_row + DRVR_SEG    ; SAVER.DRV (kernel/blank.inc)
    MEM_RR_ONE xm_row + DRVR_SEG    ; XMEM.DRV (kernel/xmem.inc)
    MEM_RR_ONE drv_blkseg           ; the block-verb far pointer's segment half
%endif
    dw 0
    ; --- ES = LOW_SEG (SPEC.md 2.6) --------------------------------------
    MEM_RR_ROW menu_bar + MB_SEG, MENU_BARMAX, MB_ENTSZ  ; .lowbss (SPEC.md 12.2)
    MEM_RR_ROW mem_tab + MC_OWN,  MEM_MAX,     MC_SIZE   ; a package's DATA
    dw 0                                                 ; claims are owned by
                                                         ; the segment it runs
                                                         ; in (SPEC.md 50.2)
%ifdef OS88_DRIVERS
    ; --- ES = 0, the hardware IRQ vectors --------------------------------
    ; A vector into a driver image names it by SEGMENT and its OFFSET does not
    ; move, so the patch is the one every other row here gets - and it is what
    ; lets SOUND.DRV's image move at all, that driver being the only one in the
    ; tree that hooks a vector and hooking FIVE of them: `sbl_isr` on its IRQ
    ; plus four candidates it installs a stub in while finding it. No driver in
    ; the tree reaches IRQ8..15 yet - sbl_f_irqdisc is `add al, 8` and so 8-bit
    ; only - and a 16-bit card on IRQ 10 would land there, so the second eight
    ; are covered for four bytes rather than left as the next reader's trap.
    MEM_RR_ROW 0x08*4 + 2, 8, 4     ; IRQ0..7  -> int 08h..0Fh, the SEGMENT half
    MEM_RR_ROW 0x70*4 + 2, 8, 4     ; IRQ8..15 -> int 70h..77h
    dw 0
%endif
section .cold

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

    ; THE KERNEL IS A HOLDER OF CLAIMS IT DOES NOT OWN (SPEC.md 66.5.10.2).
    ; osapi_vol_add's DX is a DONATED listing claim: the driver owns it, so the
    ; dispatch below reaches the driver - and dsk_vtab's DV_SEG and [dsk_dseg]
    ; name the same block on this side of the ABI, where that proc cannot go.
    ; So the kernel's half runs FIRST, unconditionally, for every move: a
    ; record that is not a donation matches no row and the walk costs eight
    ; compares against a rep movsb of up to 64KB. Unconditional is the design -
    ; the next driver to donate one is movable the day it declares a proc, with
    ; no kernel change and nothing to remember. BX and DX are the parameters
    ; for the owner's call below and mem_region_reloc preserves both. It is
    ; ONE call and not two: dsk_dseg_reloc was a walk of dsk_vtab's DV_SEG and
    ; the live [dsk_dseg], which are two rows of mem_rr_tab like every other
    ; kernel word that can name a claim.
    mov word [ss:mem_fptr], cw_mem_disp
    mov [ss:mem_fptr+2], ax     ; AX is KERNEL_SEG from the ES load above
    mov ds, ax                  ; DS = KERNEL_SEG: a kernel proc's own
    mov bp, mem_region_reloc    ; THE KERNEL'S HALF, for the reason above and
    call far [ss:mem_fptr]      ; SPEC.md 66.6.1's: a region, a driver image
                                ; and a donated listing alike are named by
                                ; kernel words the owner's own proc cannot
                                ; reach, and a block that is none of them
                                ; matches no row

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
    jae .pkg                    ; a package's or a driver's own SEGMENT
    push bx                     ; an instance slot, and it is TWO things now
    mov bx, ax                  ; (SPEC.md 66.6.1): a built-in's data claim,
    call mem_is_region          ; whose proc is the kernel's, or a package's
    pop bx                      ; REGION, whose holder is the package itself
    jnc .kernel                 ; (pop writes no flags)
    mov ax, [ss:si+MC_SEG]      ; ...AT ITS NEW BASE, which mem_cp_run wrote
                                ; before calling this. The old one is BX and
                                ; there is nothing there any more
.pkg:
    mov word [ss:mem_fptr], PKG_DISP
    jmp short .go               ; DS = the holder's own segment below: the
.kernel:                        ; proc's whole job is to write its own data
                                ; The low word of mem_fptr is ALREADY
                                ; cw_mem_disp - the kernel's half above staged
                                ; it unconditionally, and the only thing that
                                ; overwrites it is the .notk arm, which never
                                ; reaches here. THAT IS A DISTANCE DEPENDENCY
                                ; and is written down deliberately: it holds
                                ; because mem_fptr is named nowhere outside
                                ; this file (tree-wide grep), cw_mem_disp is
                                ; `call bp / retf`, and mem_region_reloc writes
                                ; only the words mem_rr_tab names. Re-check all
                                ; three before putting anything between them.
    mov ax, KERNEL_SEG          ; a kernel relocation proc is a NEAR proc in
                                ; .text, reached through the one shim, so DS
                                ; is the kernel's own
.go:
    mov [ss:mem_fptr+2], ax     ; ONE tail: both arms stage the same two words
    mov ds, ax                  ; from the same register, and differ only in
                                ; the OFFSET they put in the low half
    call far [ss:mem_fptr]
    pop es
    pop ds
    jmp kretc_bp

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
    call mem_rank_bh            ; dissolves nothing. Otherwise the CLAIMANT's
                                ; own level, mem_shed_one's derivation exactly
                                ; - and now literally the same door, which is
                                ; what SPEC.md 50.6.4 needs
.rank:
    mov [mem_pg_rank], al

    inc byte [mem_cp_busy]
    mov word [mem_cp_msk], 0     ; THE ASCENDING PASS FIRST, always: it is the
                                ; cheaper of the two and it is the one that
                                ; answers the ordinary case. mem_avail plans
                                ; through here too (SPEC.md 66.10.3), so this
                                ; byte is left 0 on every path out
    inc byte [sch_lock]         ; pause task switching across the PLAN as well
                                ; as the moves - the dsk_xfer bargain (SPEC.md
                                ; 18.9), where the tick, the mouse and any
                                ; sound IRQ keep running. The plan is pure
                                ; arithmetic over 32 records and costs
                                ; microseconds, and taking it inside the same
                                ; window is what stops the map changing
                                ; between deciding a move is worth making and
                                ; making it
    ; THE ESCALATION, AS A LOOP AND NOT A LADDER (SPEC.md 66.4, 66.5). There
    ; are two independent things to try when a plan comes back short - stand
    ; the workers up, and turn the walk round - and the ladder that spelt them
    ; out in order got the combination wrong: it parked only for the ASCENDING
    ; plan, so a machine whose only blocked mover was a top-down claim owned by
    ; a package with a worker reached the descending pass with that worker
    ; still running, and mem_can_move refused the one block the pass was for.
    ;
    ; As a loop each is asked for whichever plan needs it, and it still
    ; terminates in at most three plans: [mem_parked] admits one park and
    ; [mem_cp_msk] one turn.
.plan:
    call mem_cp_plan            ; CX = the run it would leave, DX = movers
    or dx, dx                   ; IS IT WORTH RUNNING? Nothing can move, so not
    jz .nowt                    ; a byte is touched...
    or di, di
    jz .doit                    ; ..."compact regardless" (SPEC.md 66.10.1)...
    cmp cx, di
    jae .doit                   ; ...or IT STILL WOULD NOT FIT, and the caller
.nowt:                          ; is about to be refused either way: a quarter
                                ; second of memcpy would not change that
                                ; (SPEC.md 66.4)

    ; --- it is not enough. Would standing the workers still make it enough? --
    ;
    ; SPEC.md 66.5. Asking costs up to INST_PARKW ticks, so it is asked ONLY
    ; when the last plan actually refused a claim because somebody might be
    ; running in it - otherwise every failed claim on a machine with a worker
    ; would pay for a park that could not have helped.
    cmp byte [mem_wpin], 0
    je .flip
    cmp byte [mem_parked], 0
    jne .flip                   ; they are already standing: asking again is
                                ; INST_PARKW ticks for a state that cannot
                                ; change while this pass holds the machine
    dec byte [sch_lock]         ; a park needs the machine RUNNING: the workers
    call KERNEL_SEG:inst_park_req    ; have to reach OSAPI_TASK_ALIVE
    inc byte [sch_lock]
    inc byte [mem_parked]       ; ...and it is withdrawn on EVERY path out
    jmp short .plan             ; ...then ask again, with whoever parked now
                                ; movable

    ; --- STILL not enough: pack the CEILING instead (SPEC.md 66.4) ----------
    ; The last resort, and it is asked last for the reason every tier here is:
    ; the top-down claims are the CS-based ones, so a pass over them is the
    ; most expensive copy this machine can make and it is worth nothing at all
    ; unless the claim it is being made for would then fit. mem_cp_worth is
    ; what says so, exactly as it does for the ascending pass.
    ;
    ; The two are ALTERNATIVES and not cumulative: each plan is made against
    ; the layout as it stands, so whichever single pass satisfies the claim is
    ; the one that runs. Running both would spend the ascending copy for a
    ; claim the descending pass was going to have to satisfy anyway.
.flip:
    cmp byte [mem_cp_msk], 0
    jne .undo                   ; the walk has already been turned round: there
    mov word [mem_cp_msk], 0xFFFF     ; is nothing left to try
    jmp short .plan
.doit:
    call mem_cp_run
    call mem_cp_end
    clc
    jmp short .out
.undo:
    call mem_cp_end             ; the two arms that took the lock share one
.none:                          ; way of giving it back; .none never took it
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mem_cp_end - hand the machine back (module internal). Every path that raised
; the lock leaves through here, which is what makes "left as it was found" a
; property of one body rather than of two that have to be kept agreeing. It
; WRITES the flags: both callers set the caller's CF after it returns, which is
; the one place the answer is decided.
mem_cp_end:
    cmp byte [mem_parked], 0    ; release the workers, if this pass ever asked
    je .nopark                  ; them to stand (SPEC.md 66.5)
    mov byte [mem_parked], 0
    call KERNEL_SEG:inst_park_end
.nopark:
    mov word [mem_cp_msk], 0    ; mem_avail plans through mem_cp_plan too, so
    dec byte [sch_lock]         ; the direction goes back as it was found
    dec byte [mem_cp_busy]
    ret

%endif


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
; -----------------------------------------------------------------------------
; mem_find_own - the claim based at DX and owned by BX (module internal)
; in:  DX = a base segment, BX = the owner word
; out: CF = 0 and SI = the record; CF = 1 = no such claim, or not this owner's
; clobbers: SI (the output), flags. CX is banked.
;
; THE FENCE, written once. The three tests and THEIR ORDER are the whole of it
; - MC_SEG != 0 first, because a free record can match a DX of 0; then the
; base; then the owner, which is what stops a package freeing somebody else's
; claim (SPEC.md 50.3/50.4). Four routines spelled it out: mem_free,
; mem_movable, mem_regrow's own find, and mem_own_drv - which did NOT have the
; first test and now does.
; -----------------------------------------------------------------------------
mem_find_own:
    push cx
    mov si, mem_tab
    mov cx, MEM_MAX
.scan:
    cmp word [ss:si+MC_SEG], 0
    je .next                    ; a free record can match a DX of 0
    cmp [ss:si+MC_SEG], dx
    jne .next
    cmp [ss:si+MC_OWN], bx
    je .out                     ; equal, so CF is already 0
    cmp dx, bx                  ; ...OR IT IS THE CALLER ITSELF: a package's
    je .out                     ; REGION is owned by its instance SLOT where BX
                                ; is its SEGMENT (SPEC.md 66.6.1), and a record
                                ; whose base equals the caller's own segment
                                ; can be nothing but that caller's region. Five
                                ; bytes, and it WIDENS the fence rather than
                                ; holing it - the two words still have to agree
                                ; about something
.next:
    add si, MC_SIZE
    loop .scan
    stc
.out:
    pop cx                      ; pop writes no flag: the answer survives it
    ret

mem_movable_x:
    push si
    pushf
    cli
    call mem_find_own           ; the mem_free fence: yours, or not at all
    jc .no
    mov [ss:si+MC_RLOC], ax
    popf
    clc
    jmp short .out
.no:
    popf
    stc
.out:
    pop si
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

    call mem_find_own           ; it must be a base, and yours
    jc .fail
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
                                ; the constraint this block was GRANTED under
    mov di, [ss:si+MC_DMA]         ; travels with it: mem_hifit rejects a base
    and di, MC_DMA_HEAD         ; whose head would straddle, exactly as the
    mov [mem_dma], di           ;
                                ; first claim did. DI is dead here - it held
                                ; .grow's overflow test and is reloaded from
                                ; CX below. Paths 1 and 2 need no such
                                ; care - the rule is on the HEAD, so a shrink
                                ; and an extend upward both keep it
    call mem_hifit              ; CX = its base
    pushf
    mov word [mem_dma], 0       ; a transient, and this window is the only
    popf                        ; place it may be set (the whole op is IF=0)
    jc .fail
    push ax
    push cx
    push si                     ; BANK the record. SI is about to be spent as
    mov di, cx                  ; mem_bcopy's source SEGMENT, and that - not
    mov cx, [ss:si+MC_PARA]        ; anything about the map - is the only
    mov si, dx                  ; reason a bounded re-find walk stood here.
    call mem_bcopy              ; The whole of mem_regrow is one IF=0 window,
    pop si                      ; so nothing can have cleared the record
    pop cx
    pop ax
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
    push si
    pushf
    cli
    cmp byte [mem_cp_busy], 0   ; a relocation proc may not free (SPEC.md 66.3
    jne .busy                   ; rule 3): the walk is standing on the record
    call mem_find_own
    jc .busy
    mov word [ss:si+MC_SEG], 0     ; the record IS the allocator (SPEC.md 50.3)
    popf
    clc
    jmp short .out
.busy:
    popf
    stc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; mem_free_owner - release every claim an owner holds (SPEC.md 50.4)
;
; in:       BX = owner word
; out:      nothing (all registers preserved)
;
; The teardown fence. Called from both SPEC.md 29.4 paths beside
; inst_rel_rec's sound half, so a closed package can no more leak a quarter of a
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
; out: nothing (all registers preserved) - inst_rel_rec's twin
; -----------------------------------------------------------------------------
mem_free_rec_x:
    push bx
    call ld_slot                ; BX = the record's slot - loader.inc's, and
                                ; `.cold` like this, so the call is near.  It
                                ; is the sixth site of an arithmetic that had
                                ; six copies; this was the last one left
    call mem_free_owner_x       ; a built-in's claims are stamped with its
                                ; instance slot (the Disk window's cache)
    test byte [di+I_KIND], KIND_PKG
    jz .done
    mov bx, [di+I_SPTR]         ; ...and a package's with its segment, which
    call mem_free_owner_x       ; is what it could name itself by
.done:
    pop bx                      ; NO AX OR CX BANK: the arithmetic that wrote
    retf                        ; them has moved into ld_slot, which preserves
                                ; both, and mem_free_owner_x banks CX/SI/flags
                                ; and never names AX.  "All registers
                                ; preserved" still holds - by nothing writing
                                ; them rather than by saving them

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
.tot:                           ; in the other register
    cmp word [ss:si+MC_SEG], 0
    je .tnext
    call mem_pg_cheap           ; takeable = it does NOT count against free
    jnc .tnext
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
; mem_sum_kb - the Task Manager's two heap terms
;
; in:  AX = a running KB total, BX = 0 all owners / 1 kernel tags only
; out: AX = that total plus the claimed KB
; clobbers: nothing else (flags)
;
; ADD-style rather than a plain reader because both call sites are building a
; sum in AX and have no spare register to hold a partial total in. It had two
; single-caller wrappers (mem_claimed_kb, mem_kernel_kb) that did nothing but
; set BX and bank it - and BX is dead at both sites, being an output at one
; and already stored at the other.
;
; It does not count a PURGEABLE claim (SPEC.md 50.6.3): SK_CLAIM and SK_KCLAIM
; are the same question asked of two owner sets, so "of which the kernel's own"
; is only honest if both draw the line in the same place.
; -----------------------------------------------------------------------------
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
; also mem_sum_kb's first caller - the wrapper it used to reach it through
; had been written for this and never wired up, which is why the duplicate
; walk survived being noticed.
; -----------------------------------------------------------------------------
mem_total_kb:
    push cx
    xor ax, ax
    xor bx, bx                  ; BX = 0: every owner (and BX is an OUTPUT
    call mem_sum_kb             ; here, written two instructions below)
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
    mov bx, es                       ; a claim that starts exactly here...
    call mem_owner_of_x              ; ...and BX is now WHOSE it is. The walk
    jc .no                           ; that used to be written out here also
                                     ; carried on past a record whose base
                                     ; matched but whose owner failed all
                                     ; three tests below - unreachable, since
                                     ; two live claims cannot share a base
                                     ; (mem_claim_1 refuses a 0 KB claim) and a
                                     ; free record's MC_SEG is 0, which ES
                                     ; never is
    cmp bx, INST_MAX                 ; ...owned by an instance slot: a
    jb .yes                          ; PACKAGE's region...
    cmp bx, MEM_K_DRV                ; ...or by the driver tag: a DRIVER's
    je .yes                          ; image, which is the same kind of thing
                                     ; and the same kind of caller. Without
                                     ; this a driver cannot claim the bulk
                                     ; buffers its own SDK tells it to claim -
                                     ; it has no instance, so the test above
                                     ; can never say yes about it
    push dx                          ; ...or by a SEGMENT that is itself a
    mov dx, bx                       ; driver image: SPEC.md 52.11.7's second
    call mem_own_drv                 ; image, one level and no further. DX is
    pop dx                           ; banked because osapi_mem_free passes the
    jc .no                           ; segment to free in it, and `pop` leaves
                                     ; the flags alone
.yes:
    mov bx, es                       ; the OWNER WORD is that same segment -
    clc                              ; the walk above spent BX on the answer
    ret                              ; to a different question
.no:
    stc
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
;
; Since it became a call to mem_find_own it also tests MC_SEG != 0, which its
; own walk did not. A strict improvement and not a behaviour change here: its
; one caller has already established DX >= INST_MAX (mem_own's `cmp word
; [MC_OWN], INST_MAX / jb .yes` failed), so DX can never be 0 - what the test
; refuses is a FREE record whose stale MC_OWN would otherwise have been read.
; -----------------------------------------------------------------------------
mem_own_drv:
    push bx                     ; BX and SI are mem_own's - it is mid-walk
    push si                     ; with the record in SI when it asks
    mov bx, MEM_K_DRV
    call mem_find_own           ; ...which is exactly "the claim based at DX
    pop si                      ; and owned by BX", one owner word fixed
    pop bx
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
%if MC_SEG != CLS_SEG || MC_PARA != CLS_PARA || MC_OWN != CLS_OWN \
    || CLS_RECSZ != 6 || MC_SIZE < CLS_RECSZ
%error "a claim record is no longer its own snapshot prefix - the copy below"
%endif
osapi_claim_snapshot_x:
    push ds
    push cx
    push si
    push di
    push ss                     ; DS = LOW_SEG for the duration: the claim map
    pop ds                      ; lives there and movs reads DS:SI
    mov si, mem_tab
    mov cx, MEM_MAX
    cld                         ; SPEC.md 1 rule 3 says DF is 0 on entry; this
                                ; is the kernel's own string-op convention and
                                ; costs one byte to not depend on it
.rec:
    movsw                       ; A CLAIM RECORD IS ITS OWN SNAPSHOT PREFIX:
    movsw                       ; MC_SEG/MC_PARA/MC_OWN are its first three
    movsw                       ; words at 0/2/4 and CLS_SEG/PARA/OWN are the
    add si, MC_SIZE - CLS_RECSZ ; snapshot's, with CLS_RECSZ = 6 - so the copy
    loop .rec                   ; is three movsw and a stride, not nine fields
    pop di
    pop si
    pop cx
    pop ds
    mov ax, MEM_MAX
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
; EVERY TERM IS A MEASURED SPAN NOW, and the arithmetic that derives them is
; in kernel.asm beside the ladder they come off (SKB_*/SK_*_KB): the sizes are
; not known until the foot of that file, and a constant belongs with what
; declares it. What is worth knowing HERE is which row holds what.
;
; SK_DSK is the mount-owned window and nothing else - 3,584 bytes of
; disk_dir + disk_icons + dsk_secbuf (SPEC.md 2.1.2). It used to be "the
; buffers less the FAT window and the stacks", which is what "whatever else is
; in .lowbss" meant when .lowbss held nothing else; by the time it held 2,824
; bytes of table it read 6 KB for 3.5 KB of buffer, and nothing bounded the
; error because a residual has no size of its own.
;
; SK_IMG is what is left, and it is the honest name for it: image, .bss, the
; COLD SEGMENT (cold code is code, it is resident for the whole session and it
; is inside the span SK_KERN measures), SPEC.md 39.22's `.vgabuf` rung, and
; the kernel's own tables in .lowbss - glyph table, row table, pair tables,
; claim map, event ring, menu bar, built-in state pools. Those are .bss-class
; data living in LOW_SEG to buy back KERN_CODE_MAX and for no other reason, so
; `Code+data` was always their label. Adding a row of their own would change a
; published block that a package reads (SPEC.md 28) and cost the memory view
; the one row of window height VGA has left, for a figure nobody can act on.
;
; The KB figures are rounded CUMULATIVELY - each part is the difference of two
; rounded running boundaries - so they sum to SK_KERN by construction and no
; part is a kilobyte from its true size. Guard 7 proves both.
; =============================================================================
osapi_sys_kb_x:
    push ax
    push bx
    push di
    mov ax, KERN_KB
    mov bx, SK_IMG_KB           ; ...and on a machine with NO VGA the kernel is
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
                                ;
                                ; It comes off SK_IMG because that is the row
                                ; the rung is IN, and off both figures by the
                                ; SAME amount because the parts have to go on
                                ; summing - which they do for any SK_VGAB_KB
                                ; at all, the two subtractions cancelling.
    test byte [vid_avail], VID_A_VGA
    jnz .kern
    sub ax, SK_VGAB_KB
    sub bx, SK_VGAB_KB
.kern:
%endif
    mov [es:di+SK_KERN], ax
    mov [es:di+SK_IMG],  bx
%if SK_FAT != 4 || SK_STK != 6 || SK_STK0 != 8 || SK_DSK != 10 || SK_BUF != 12
%error "osapi_sys_kb stores five CONSECUTIVE words; these are not consecutive"
%endif
    push di                     ; five build constants into five adjacent
    add di, SK_FAT              ; words: one walking pointer beats five
    cld                         ; displaced immediate stores. AX is free -
    mov ax, SK_FAT_KB           ; SK_KERN and SK_IMG are already written
    stosw
    mov ax, SK_STK_KB
    stosw
    mov ax, SK_STK0_KB
    stosw
    mov ax, SK_DSK_KB
    stosw
    mov ax, KBUF_KB             ; SK_BUF LAST, because it is the highest of
    stosw                       ; the five - it is FAT + stacks + disk
    pop di                      ; buffers together, and reads like a header
    push di
    call mem_total_kb           ; AX = claimed KB, BX = heap size KB
    pop di
    mov [es:di+SK_CLAIM], ax
    mov [es:di+SK_HEAP], bx
    xor ax, ax
    mov bx, 1                   ; BX = 1: kernel tags (0xFF00 and up) only.
    push di                     ; SK_HEAP has already been stored from BX
    call mem_sum_kb             ; AX += the kernel's own tagged claims
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
mmf_mem_claim:            call mem_claim_x
                      retf
mmf_osapi_mem_claim_hi:   call osapi_mem_claim_hi_x
                          retf
mmf_osapi_mem_claim_dma_hi: call osapi_mem_claim_dma_hi_x
                          retf
mmf_mem_free:             call mem_free_x
                      retf
mmf_mem_movable:          call mem_movable_x
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
                                ; (a two-byte mem_av_pad followed, claiming to
                                ; "keep the words aligned": .bss is nobits
                                ; vfollows=.text valign=1, so nothing here is
                                ; aligned by anything, and an EVEN pad cannot
                                ; change any following symbol's parity even if
                                ; it were)
mem_cp_busy: resb 1             ; the compactor is running (SPEC.md 66.4): a
                                ; relocation proc that claims - which 66.3
                                ; rule 3 forbids - would otherwise re-enter
                                ; the walk that is driving it and move the map
                                ; under itself. Bounded rather than trusted
mem_cp_msk:  resw 1             ; THE COMPACTION PASS'S DIRECTION (SPEC.md 66.4),
                                ; as a MASK: 0 = the original ascending pass,
                                ; packing the data arena DOWN against
                                ; [mem_base]; 0xFFFF = the descending pass,
                                ; packing the CS-based claims UP against
                                ; [mem_top]. One word rather than a second copy
                                ; of the walk, because the two differ in eight
                                ; decisions and nothing else - and a claim that
                                ; came in through the top-down door has to go
                                ; back through it or the pass that "helps"
                                ; splits the ceiling.
                                ;
                                ; A MASK AND NOT A FLAG because mem_cp_next
                                ; wants it as one: complementing a segment
                                ; reverses the order, which turns its two scans
                                ; into one. Every other reader takes the LOW
                                ; BYTE against zero and cannot tell the
                                ; difference
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

; -----------------------------------------------------------------------------
; mem_bytes_kb - a byte count as whole kilobytes, rounded UP and never 0
; in:       AX = bytes
; out:      AX = kilobytes, at least 1
; clobbers: flags
;
; The claim heap's unit is the kilobyte, so every caller of mem_claim that
; starts from a byte count spells this out; three of them did, in .text alone.
; CX is banked because one of the three carries the byte count in it, and a
; helper that clobbers a caller's register is worth less than the two bytes.
;
; **A zero answer is refused**, which is the merged form of two shapes that
; differed: a claim of 0 KB gets no memory and the caller has nowhere to put
; what it asked about. The site that had no clamp cannot reach 0 either - its
; count is already past a `jcxz` - so the answer is the same at all three.
; -----------------------------------------------------------------------------
mem_bytes_kb:
    push cx
    add ax, 1023
    mov cl, 10
    shr ax, cl
    or ax, ax
    jnz .out
    inc ax
.out:
    pop cx
    ret

section .cold                   ; COLD_SEG (SPEC.md 2.6)

; -----------------------------------------------------------------------------
; mem_bytes_kb_x - the same, for the four callers that are in .cold
; in:       AX = bytes, and AT LEAST 512 of them - see below
; out:      AX = kilobytes;  CL = 10, which loader.inc's `shl ax, cl` reads
; clobbers: CL, and the flags
;
; A TWIN AND NOT A CALL, because a near call from COLD_SEG to a KERNEL_SEG
; body assembles cleanly and runs wrong (SPEC.md 2.6) - which is exactly why
; the four sites open-coded these three instructions instead of using the
; routine above them.
;
; TWO DELIBERATE DIFFERENCES from the .text original, and both are why this is
; eight bytes rather than fifteen:
;
;   * NO CX BANK. All four callers destroy CL themselves today, so the bank
;     bought nothing - and dropping it is what SATISFIES loader.inc's
;     documented "CL IS STILL 10" dependency rather than breaking it.
;   * NO ZERO CLAMP, because no caller can reach zero: driver and mod have
;     already refused anything under their *_HDR_SZ, filecp's input is a
;     cluster size (>= 512 by the BPB rules), and loader's is
;     `(img + bss + 511) & 0FE00h` with `img >= LD_HDR_SIZE` enforced by
;     ld_check_hdr, so it is at least 512. **A NEW CALLER MUST PROVE THE SAME
;     OR USE mem_bytes_kb**: 0 KB is a claim with nowhere to put anything.
; -----------------------------------------------------------------------------
mem_bytes_kb_x:
    add ax, 1023
    mov cl, 10
    shr ax, cl
    ret

section .text                   ; ...and the file ends in .text (CLAUDE.md 1)
