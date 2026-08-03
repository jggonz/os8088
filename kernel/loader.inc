; =============================================================================
; loader.inc - package loader (SPEC.md 21)
;
; Validates a staged directory entry (SPEC.md 19), allocates a whole-
; paragraph region from the conventional arena at linear 0x65800..the int
; 12h top (SPEC.md 2.5 - every v3 package gets its OWN segment), reads the
; .o88 file into it, zeroes the package bss, calls its entry, and registers
; the new instance in the instance table (SPEC.md 29). There is NO
; eviction: any number of package instances can be resident, and a load
; that cannot fit fails with "Out of memory" while running instances stay
; untouched - and a machine below a package's header memory requirement
; fails with the precise "Needs nnnK" (LD_ENEED, SPEC.md 20.2/21). There
; is no relocation pass: a v3 package is org 0 in its own segment, read
; straight to pkgseg:0000 and far-called at pkgseg:entry (SPEC.md 20.1).
;
; The arena allocator IS the instance table (SPEC.md 29.2 rule 7): a
; package record's [I_SPTR, I_SPTR+I_SIZE) - segment + paragraphs since v3
; (SPEC.md 29.1) - is occupied iff I_STATE != 0, so the store that frees a
; record frees its region. Only the UI task allocates (loader_run runs on
; it exclusively), so allocation never races itself.
;
; loader_run is called only from the UI task (ui.inc step 3) with the gfx
; lock NOT held - it takes and releases the lock itself around wm_destroy /
; wm_show, and the package entry runs with the lock free (SPEC.md 20.2).
; ld_pending is written by files.inc and consumed by ui.inc; loader_run
; itself only receives the already-decoded directory index in AX.
;
; ld_run_body is structured as clusters with local error stubs (mov al /
; ret) because 8086 conditional jumps are short-range only.
; =============================================================================

; --- status codes stored in [ld_status] (SPEC.md 21) -------------------------
LD_OK       equ 0               ; loaded and shown
LD_EDISK    equ 1               ; disk_read failed
LD_EBAD     equ 2               ; not a valid package (entry or header)
LD_EBIG     equ 3               ; too large (file, image or image+bss)
LD_EABORT   equ 4               ; package entry returned CF=1
LD_ENOMEM   equ 5               ; instance table or arena exhausted
LD_ENEED    equ 6               ; the machine is below the package's header
                                ; memory-requirement KB (SPEC.md 20.2):
                                ; [ld_needkb] holds the figure for the
                                ; precise "Needs nnnK" message (SPEC.md 22)

; --- staged directory entry field offsets (SPEC.md 19) ------------------------
LD_DE_TYPE  equ 16              ; word: 1 = application package
LD_DE_CLUS  equ 18              ; word: first cluster of the file's chain
LD_DE_SIZE  equ 20              ; word: size in bytes (= the image, v3)

; --- .o88 v3 header field offsets (SPEC.md 20.2) ------------------------------
LD_H_MAGIC  equ 0               ; word: 0x384F ('O','8')
LD_H_VER    equ 2               ; byte: format version = 3 (v1/v2 rejected)
LD_H_FLAGS  equ 3               ; byte: bit 0 = embedded 16x16 icon
LD_H_LINK   equ 4               ; word: must be ZERO (was the v2 link base -
                                ; org 0 has none, and a stale v2 layout must
                                ; not slip past a bumped version byte)
LD_H_ENTRY  equ 6               ; word: entry offset, image-relative - which
                                ; at org 0 IS the plain segment offset
LD_H_IMG    equ 8               ; word: image size = the file size, exactly
LD_H_BSS    equ 10              ; word: bytes to zero after the image
LD_H_RCNT   equ 12              ; word: must be ZERO (was the v2 relocation
                                ; count - relocation is retired, SPEC 20.2)
LD_H_MEMKB  equ 14              ; word: minimum conventional RAM, KB - the
                                ; precise "Needs nnnK" refusal (SPEC 20.2)
LD_HDR_SIZE equ 0x20

; -----------------------------------------------------------------------------
; loader_init - reset loader state and size the package arena (SPEC.md 2.5)
; in:  nothing
; out: nothing (all registers preserved)
;
; The one int 12h probe the arena ever gets: boot, task 0 (SPEC.md 7's
; only-the-UI-task-calls-BIOS rule - the tm_init precedent). KB x 64 =
; the top in PARAGRAPHS; never assume 0xA000 - SeaBIOS's EBDA reports 639.
; On a 256KB machine the top (0x4000) sits below ARENA_SEG and the arena
; is empty: ld_alloc then refuses every request immediately, which is the
; whole graceful-degradation story - the machine boots and runs built-ins
; identically, and package loads fail with a message, never a crash.
;
; The top is clamped to VID_HERC_SEG for the `make test VIDEO=herc
; HERCSEG=0x7000` harness (SPEC.md 2.5/39.9), whose fake framebuffer sits
; INSIDE the arena on a QEMU boot. viddet.inc defaults the macro to 0xB000
; - real Hercules VRAM, above any possible int 12h top - so on every
; production build the clamp can never bind and costs two instructions.
; -----------------------------------------------------------------------------
loader_init:
    push ax
    push cx
    mov word [ld_pending], 0
    mov word [ld_pwin], 0
    mov byte [ld_status], LD_OK
    mov word [ld_needkb], 0
    int 0x12                    ; AX = conventional KB (BIOS, boot-time)
    mov [ld_ramkb], ax          ; kept raw for the SPEC.md 20.2 header
                                ; memory-requirement check ("Needs nnnK"
                                ; compares KB, not a clamped arena top)
    mov cl, 6
    shl ax, cl                  ; KB x 64 = paragraphs (max 0xA000: fits)
    cmp ax, VID_HERC_SEG        ; the test-knob clamp (see above)
    jbe .top
    mov ax, VID_HERC_SEG
.top:
    mov [ld_arena_top], ax      ; arena = [ARENA_SEG, [ld_arena_top]) paras
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ld_alloc - claim an arena region, first-fit lowest segment (SPEC.md 2.5/21)
; in:  AX = paragraphs needed (a 32-multiple, <= PKG_MAX_PARA)
; out: CF = 0, BX = the region's SEGMENT; CF = 1 no hole fits - including
;      immediately, on a zero arena (a 256KB machine's int 12h top sits
;      below ARENA_SEG), which is how the floor machine refuses loads
; clobbers: nothing else. UI task only (occupancy is derived from the
;           instance table, which only this task grows).
;
; ALL the arithmetic is PARAGRAPHS, where nothing wraps: the largest
; possible top is 0xA000 (640KB) and a region caps at PKG_MAX_PARA, so a
; candidate end never exceeds 0xAFFF. The allocator WAS the instance table
; alone (SPEC.md 29.2 rule 7): a package record's [I_SPTR, I_SPTR+I_SIZE) -
; both in paragraph units since v3 (SPEC.md 29.1) - is occupied iff
; I_STATE != 0, so the store that frees a record frees its region and no
; allocator state exists to leak or to race teardown (since SPEC.md 20.6
; that store can be a worker's task_exit under IF=0, not just the UI task
; under the lock).
;
; Since SPEC.md 2.6 there is a SECOND claim map over the same arena -
; cmem.inc's block table, the memory a package asks for and gives back -
; and the walk that consults both lives there, in cm_fit, precisely so the
; two allocators cannot disagree about what is occupied. This entry point
; survives as the loader's name for it: same contract, same first-fit
; order, one caller.
; -----------------------------------------------------------------------------
ld_alloc:
    jmp cm_fit                  ; in AX = paragraphs; out BX = segment, CF

; -----------------------------------------------------------------------------
; ld_check_hdr - validate a .o88 v3 header (module-internal, SPEC.md 21)
; in:  SI -> 32 readable header bytes IN KERNEL SCRATCH, always -
;      dsk_secbuf-staged at the peek, ld_hdr-staged at the re-check (in v3
;      the loaded header lives in a segment DS cannot reach);
;      [ld_fsz] = the file's byte count
; out: CF = 0 and [ld_img]/[ld_bss]/[ld_ent] filled, or CF = 1 and
;      AL = LD_EBAD / LD_EBIG / LD_ENEED (with [ld_needkb] set)
; clobbers: AX, DX
;
; The image==filesize check is the truncation guard, and it is sound
; because FAT directory sizes are byte-exact and os88disk.py never pads
; the size field (SPEC.md 24) - cluster slack on disk is invisible here.
; The icon rule (entry >= 0x60 with flags bit 0) is enforced by os88pkg,
; not re-checked: a package lying about it corrupts only its own icon.
; -----------------------------------------------------------------------------
ld_check_hdr:
    cmp word [si+LD_H_MAGIC], 0x384F        ; 'O','8'
    jne .bad
    cmp byte [si+LD_H_VER], 3               ; v1/v2 files: "Bad package"
    jne .bad
    cmp word [si+LD_H_LINK], 0              ; +4 and +12 must be zero since
    jne .bad                                ; v3 (SPEC.md 20.2): a stale v2
    cmp word [si+LD_H_RCNT], 0              ; layout must not slip past a
    jne .bad                                ; bumped version byte
    mov ax, [si+LD_H_IMG]
    cmp ax, LD_HDR_SIZE
    jb .bad                                 ; image smaller than the header
    cmp ax, [ld_fsz]                        ; image = file size, exactly
    jne .bad                                ; (the truncation guard)
    mov [ld_img], ax
    mov dx, [si+LD_H_BSS]
    mov [ld_bss], dx
    add dx, ax                              ; img+bss: a 16-bit wrap IS "too
    jc .toobig                              ; big" - both are hostile words
    cmp dx, PKG_MAX_PARA*16                 ; the one-segment budget
    ja .toobig                              ; (65,520 bytes, SPEC.md 20.1)
    mov ax, [si+LD_H_ENTRY]                 ; image-relative entry - at org 0
    cmp ax, LD_HDR_SIZE                     ; the plain segment offset
    jb .bad
    cmp ax, [ld_img]
    jae .bad
    mov [ld_ent], ax
    mov ax, [si+LD_H_MEMKB]                 ; the SPEC.md 20.2 memory
    cmp ax, [ld_ramkb]                      ; requirement, against the boot
    ja .need                                ; int 12h KB - below it, refuse
    clc                                     ; PRECISELY, not obscurely
    ret
.bad:
    mov al, LD_EBAD
    stc
    ret
.toobig:
    mov al, LD_EBIG
    stc
    ret
.need:
    mov [ld_needkb], ax                     ; for the "Needs nnnK" status
    mov al, LD_ENEED                        ; line (SPEC.md 21/22)
    stc
    ret

; -----------------------------------------------------------------------------
; loader_run - load, relocate and launch the package at a directory index
; in:  AX = directory index (0-based), into the listing of the file-manager
;      window named by [ld_pwin] (SPEC.md 22.1) - which may not be the one
;      currently mounted
; out: nothing (all registers preserved); result in [ld_status]; on success
;      a new package instance is live in the instance table (SPEC.md 29)
;
; The sync comes first and the index is banked across it, in that order and
; for a hard reason: disk_mount deliberately zeroes [ld_pending] (SPEC.md
; 18.3), so anything that reads the pending word AFTER a sync reads zero. The
; index arrives here in AX, already consumed by ui.inc, which is what makes
; the order safe - and [ld_pwin] is read before the sync too, so this stays
; true if the drain ever changes.
; -----------------------------------------------------------------------------
loader_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov di, [ld_pwin]           ; the window whose listing this index means
    or di, di
    jz .body                    ; no poster (a package's own request): the
                                ; globals are the only listing there is
    push ax
    call fmv_sync               ; ...make the globals BE that listing
    pop ax
.body:
    call ld_run_body            ; out: AL = LD_* status
    mov [ld_status], al
    call files_refresh          ; no lock held; it takes its own

    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ld_run_body - the load pipeline (module-internal; loader_run saves regs)
; in:  AX = directory index
; out: AL = LD_* status
;
; Steps (SPEC.md 21): validate the dir entry; peek the header via a
; 1-sector read into dsk_secbuf; validate it (v3: version, zero +4/+12,
; image = file size, the memory requirement); size the allocation in
; paragraphs (512-rounded - sector granularity means the full-file read
; can never touch a neighbouring region); reserve an instance record + an
; arena segment; read the whole file to pkgseg:0000; re-stage + re-check
; the header (disk-swap guard); zero the bss; far-call pkgseg:entry;
; register + publish the instance (I_SPTR = segment, I_SIZE = paragraphs,
; name + icon from/via kernel staging) and wm_show its window. Every
; failure path leaves running instances untouched - the reserved record
; was never published, so it (and with it the region) is simply still free.
; -----------------------------------------------------------------------------
ld_run_body:
    ; --- 1. validate the directory entry --------------------------------------
    cmp ax, [disk_nfiles]
    jae .bad1                   ; index out of range
    call dsk_get_dir            ; SI -> the entry, staged out of LOW_SEG into
                                ; the kernel segment (SPEC.md 2.1)
    cmp word [si+LD_DE_TYPE], 1
    jne .bad1                   ; not an application package
    mov ax, [si+LD_DE_SIZE]
    or ax, ax
    jz .bad1                    ; empty file
    cmp ax, PKG_MAX_PARA*16
    ja .big1                    ; past the one-segment budget (SPEC.md 20.1)
    mov [ld_fsz], ax
    mov ax, [si+LD_DE_CLUS]     ; first cluster: re-check the range the
    cmp ax, 2                   ; mount already validated (SPEC.md 21 step
    jb .bad1                    ; 1 - this check stands alone if the mount
    cmp ax, [dsk_maxclus]       ; code ever changes)
    ja .bad1
    mov [ld_clus], ax
    jmp short .peek
.bad1:
    mov al, LD_EBAD
    ret
.big1:
    mov al, LD_EBIG
    ret

    ; --- 2. peek the header: first sector into dsk_secbuf ---------------------
    ; dsk_secbuf is disk.inc's mount scratch - safe to reuse here because
    ; both disk_mount and loader_run run only on the UI task. It lives in
    ; LOW_SEG, so the sector lands there and only the 32 header bytes are
    ; staged back into the kernel segment for ld_check_hdr to read.
.peek:
    call dsk_clus2lba           ; AX: cluster -> its first LBA; the first
    jc .bad1                    ; sector of the first cluster IS the file's
                                ; first 512 bytes, so peek semantics are
                                ; exact (SPEC.md 21 step 2)
    mov bx, LOW_SEG
    mov es, bx
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read              ; in: AX=LBA, CX=1, ES:BX
    jc .disk1

    ; --- 3. validate the peeked header -----------------------------------------
    mov si, dsk_secbuf
    mov di, ld_hdr
    mov cx, LD_HDR_SIZE
    call dsk_copy_in            ; LOW_SEG:dsk_secbuf -> KERNEL_SEG:ld_hdr
    mov si, ld_hdr
    call ld_check_hdr           ; fills ld_img/ld_bss/ld_ent; AL = LD_EBAD /
    jc .ret1                    ; LD_EBIG / LD_ENEED on failure

    ; --- 4. size the allocation, in PARAGRAPHS (SPEC.md 21) --------------------
    ; need = ceil((img+bss)/512) x 32 paragraphs. img+bss >= fsize because
    ; img = fsize exactly (ld_check_hdr) and bss >= 0, so the old
    ; max(img+bss, fsize) is just img+bss - and keeping the 512-byte
    ; rounding inside the paragraph unit is what makes the whole-file read
    ; safe: the chain walk writes ceil(fsize/512)*512 <= need*16 bytes,
    ; never a neighbour's region. The -1/+1 form dodges the 16-bit wrap
    ; that add 511 would hit near the 64KB budget (img >= 0x20, never 0).
    mov ax, [ld_img]
    add ax, [ld_bss]            ; <= 0xFFF0: ld_check_hdr proved it
    dec ax
    mov cl, 9
    shr ax, cl
    inc ax                      ; = ceil(bytes/512), <= 0x80
    mov cl, 5
    shl ax, cl                  ; sectors x 32 = paragraphs (a 32-multiple)
    cmp ax, PKG_MAX_PARA        ; a full-64KB image rounds to 0x1000 paras
    ja .big2                    ; and is refused - the wrap guard's price
    mov [ld_need], ax

    ; --- 5. reserve an instance record + an arena region -----------------------
    call inst_alloc             ; DI = record (unpublished: still free on
    jc .nomem                   ; any later failure)
    mov bp, di                  ; BP = the record for the duration
    call ld_alloc               ; in: AX = paragraphs; out: BX = the region
    jc .nomem                   ; SEGMENT - CF here is also the zero-arena
                                ; refusal (a 256KB machine, SPEC.md 2.5)
    mov [ld_base], bx
    jmp short .read
.disk1:
    mov al, LD_EDISK
.ret1:
    ret
.big2:
    mov al, LD_EBIG
    ret
.nomem:
    mov al, LD_ENOMEM
    ret

    ; --- 6. read the whole file into the region's SEGMENT ----------------------
.read:
    mov ax, [ld_fsz]            ; sectors = ceil(fsize/512), computed -1/+1
    dec ax                      ; because fsize can sit near the 64KB budget
    mov cl, 9                   ; where add 511 would wrap (fsize > 0: step 1)
    shr ax, cl
    inc ax
    mov dx, ax                  ; DX = sector count
    mov ax, [ld_clus]
    mov es, [ld_base]           ; ES:BX = pkgseg:0000 - the v3 destination
    xor bx, bx                  ; (SPEC.md 21 step 6); the write bound holds
                                ; even against a corrupt FAT: the walk is
                                ; size-driven, <= need x 16 bytes
    call dsk_read_chain         ; in: AX=cluster, DX=sectors, ES:BX -> dest.
    jc .ret2                    ; on CF, AL is ALREADY the status: 1 =
                                ; LD_EDISK (disk error), 2 = LD_EBAD (chain
                                ; corrupt is bad data, not a disk error) -
                                ; SPEC.md 21 step 6

    mov es, [ld_base]           ; re-stage the loaded header's 32 bytes back
    xor si, si                  ; into kernel scratch - DS cannot reach the
    mov di, ld_hdr              ; package segment - and re-check: the disk
    mov cx, LD_HDR_SIZE         ; could have been swapped between the peek
    call osapi_copy_n           ; and the read, and a looped or cross-linked
    mov si, ld_hdr              ; chain can produce plausible-length garbage
    call ld_check_hdr
    jc .ret2

    mov ax, [ld_img]            ; the guard's LAST clause (SPEC.md 21 step
    add ax, [ld_bss]            ; 6): the re-check REFILLED ld_img/ld_bss,
    dec ax                      ; and a swapped disk's valid header can
    mov cl, 9                   ; carry a LARGER bss than the peeked one
    shr ax, cl                  ; the region was sized for - step 8's stosb
    inc ax                      ; is bounded by [ld_bss], not by the
    mov cl, 5                   ; allocation. Re-derive the step-4 paragraph
    shl ax, cl                  ; need; anything past what was reserved
    cmp ax, [ld_need]           ; would zero a NEIGHBOUR's region: refuse
    ja .swapped                 ; as "Bad package"

    ; --- 7. (retired: the v2 relocation pass, deleted outright - SPEC.md 20.2.
    ;     Nothing between the read and the bss zero touches a byte of the
    ;     image: a far API call encodes an absolute seg:off and an org-0
    ;     internal address is correct wherever the segment lands.)
    jmp short .zero
.swapped:
    mov al, LD_EBAD
.ret2:
    ret

    ; --- 8. zero the package bss -----------------------------------------------
.zero:
    mov es, [ld_base]
    mov di, [ld_img]
    mov cx, [ld_bss]
    xor al, al
    cld
    rep stosb                   ; ES = the package segment; CX may be 0

    ; --- 9. launch ---------------------------------------------------------------
    ; Far-called at pkgseg:entry with CS = DS = ES = the package segment,
    ; IF=1, gfx lock not held (SPEC.md 20.2); the entry creates its
    ; window(s) lock-free and returns with `retf`, BX = window ptr / CF
    ; set - the CF answer survives inst_cb_call's return path untouched.
    ; [ld_base] is the region SEGMENT (SPEC.md 21 step 5); it can never
    ; equal KERNEL_SEG (the arena starts at ARENA_SEG, 0x6580), so
    ; inst_cb_call always takes its far leg for an entry.
    ;
    ; The entry is a DISPATCHED SITE like every other package callback
    ; (SPEC.md 34.3): stamp the reserved record so a grant made in here -
    ; conventional memory (SPEC.md 2.6), a sound grant, an extended-memory
    ; block - is stamped with the instance that will own it rather than
    ; with the kernel. Without the stamp snd_req_inst answers 0xFF (the UI
    ; task's T_INST) and the claim could never be released. The record is
    ; still UNPUBLISHED, which is exactly right: inst_pkg_spawn tests
    ; I_STATE = 1 independently and still refuses a worker from here.
    push word [snd_inst]
    mov di, bp
    call snd_disp_set
    ;
    ; ...and HOLD the region while it runs. The record above is still
    ; unpublished, so the arena walk (SPEC.md 2.6) has no evidence this
    ; region is taken and would hand the entry proc's own OSAPI_MEM_ALLOC
    ; the segment it is executing in. cm_hold is what says otherwise for the
    ; length of this one call; see the note at cm_hold for what it looked
    ; like when it was missing.
    mov bx, [ld_base]
    mov ax, [ld_need]
    call cm_hold
    push word [ld_base]         ; the segment above the offset:
    push word [ld_ent]          ; inst_cb_call's argument pair
    call inst_cb_call
    call cm_unhold              ; step 10 below publishes the record, which
                                ; is the permanent form of the same claim
    pop word [snd_inst]         ; the outer stamp back - cm_unhold, MOV and
                                ; POP all touch no flags, so the entry's CF
                                ; is still live at the jc
    jc .abort

    ; --- 10. register + publish the instance, show the window ------------------
    mov di, bp
    mov byte [di+I_KIND], KIND_PKG
    mov byte [di+I_TASK], 0xFF  ; born task-less; a callback may claim one
                                ; worker later (SPEC.md 20.6). The entry
                                ; proc cannot: neither I_STATE nor
                                ; wm_owner is published until right here.
    mov ax, [ld_base]
    mov [di+I_SPTR], ax         ; the region SEGMENT (SPEC.md 29.1)...
    mov ax, [ld_need]
    mov [di+I_SIZE], ax         ; ...and its size in PARAGRAPHS: arena
                                ; occupancy + the Task Manager's RAM sum
    mov si, ld_hdr + 16         ; the header name field, from the STAGED
    call inst_set_name          ; header - kernel scratch, so inst_set_name
                                ; keeps its plain-DS contract (SPEC.md 21)
    xor ax, ax
    test byte [ld_hdr+LD_H_FLAGS], 1
    jz .noicon
    mov ax, di                  ; embedded icon: copy the 64-byte body out
    sub ax, inst_tab            ; of the package segment into THIS record's
    shl ax, 1                   ; inst_icons slot (record offset = idx*32,
    add ax, inst_icons          ; doubled = idx*64, the slot stride) so
    push es                     ; I_ICON stays a kernel DS pointer the dock
    mov es, [ld_base]           ; can draw from (SPEC.md 21 step 9 / 29.5)
    mov si, LD_HDR_SIZE         ; pkgseg:0x20 = the icon body...
    mov di, ax
    mov cx, 64
    call osapi_copy_n           ; ...ES:SI -> DS:DI, the kernel-side cache
    pop es
    mov di, bp                  ; the record back (AX still = the slot)
.noicon:
    mov [di+I_ICON], ax
    call inst_bind_win          ; DI = record, BX = window (from the entry)
    mov byte [di+I_STATE], 1    ; publish - the LAST record write
    call gfx_lock
    call wm_show                ; in: BX = window ptr; repaints all
    call gfx_unlock
    mov al, LD_OK
    ret

.abort:
    ; SPEC.md 20.2: an aborting entry returns BX = its half-created window
    ; ptr (or 0). Destroy it, or aborted loads would leak wm_wins slots for
    ; good. BX comes from a package that just declared itself broken, so
    ; trust nothing: only a properly aligned pointer into wm_wins reaches
    ; wm_destroy (which writes through it). The reserved instance record
    ; was never published, so it and the region are simply still free.
    test bx, bx
    jz .abort_clean
    mov ax, bx
    sub ax, wm_wins
    jc .abort_clean             ; below the window table
    cmp ax, MAX_WIN*WIN_SIZE
    jae .abort_clean            ; beyond it
    mov cl, WIN_SIZE
    div cl                      ; AL = idx; AH = offset mod WIN_SIZE
    test ah, ah
    jnz .abort_clean            ; not record-aligned
    call gfx_lock
    call wm_destroy             ; in: BX; clears the record + zord slot
    call gfx_unlock
.abort_clean:
    call gfx_lock               ; the entry may have wm_created MORE windows
    mov ax, [ld_base]           ; than the one it returned (SPEC.md 20.2):
    call wm_destroy_seg         ; sweep them by their wm_wseg creator stamp
    call gfx_unlock             ; (SPEC.md 11) - the region is about to be
                                ; free for reuse, and no instance record
                                ; will ever exist to sweep them later
    mov di, bp                  ; and release whatever the entry claimed
    call snd_release_rec        ; against the record it was stamped with
    call xm_release_rec         ; (step 9's dispatch stamp): the record goes
    call cm_release_rec         ; back to free without ever being published,
                                ; so no teardown path will ever see it and
                                ; the claims would be stranded for the
                                ; session - an arena grant permanently, and
                                ; the arena is where the next package goes
    mov al, LD_EABORT
    ret

; -----------------------------------------------------------------------------
section .bss

ld_pending: resw 1              ; 0 = none, else directory index + 1
                                ; (set by files.inc, consumed by ui.inc)
ld_pwin:    resw 1              ; -> the state block of the file-manager
                                ; window that posted it, 0 = none. The index
                                ; above is meaningless anywhere else, and a
                                ; BACKGROUND window's listing is a real one it
                                ; is entitled to act on (SPEC.md 22.1)
ld_status:  resb 1              ; LD_* result of the last loader_run
ld_clus:    resw 1              ; loader_run scratch: the file's first cluster
ld_fsz:     resw 1              ; file size, bytes (= the image size, v3)
ld_img:     resw 1              ; header: image size
ld_bss:     resw 1              ; header: bss size
ld_ent:     resw 1              ; header: image-relative entry
ld_base:    resw 1              ; allocated region SEGMENT (SPEC.md 21)
ld_need:    resw 1              ; allocated region size in PARAGRAPHS
                                ; (a 32-multiple <= PKG_MAX_PARA)
ld_hdr:     resb LD_HDR_SIZE    ; the peeked .o88 header, staged out of the
                                ; LOW_SEG sector buffer for ld_check_hdr
ld_arena_top: resw 1            ; the arena's exclusive top in PARAGRAPHS:
                                ; the boot int 12h KB x 64, probed once by
                                ; loader_init on task 0 (SPEC.md 2.5),
                                ; clamped to VID_HERC_SEG for the HERCSEG
                                ; test knob. Nothing zeroes .bss: loader_init
                                ; stores it before any load can run
ld_ramkb:   resw 1              ; the raw int 12h KB (unclamped), for the
                                ; SPEC.md 20.2 memory-requirement check
ld_needkb:  resw 1              ; a refusing package's requirement KB, for
                                ; the LD_ENEED "Needs nnnK" message
                                ; (SPEC.md 21/22); loader_init zeroes it

section .text
