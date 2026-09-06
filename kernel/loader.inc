; =============================================================================
; loader.inc - package loader (SPEC.md 21)
;
; Validates a staged directory entry (SPEC.md 19), claims a region from the
; HEAP top-down (SPEC.md 20.1/50.3 - there is no package pool any more),
; reads the .o88 **v3** image into it, zeroes the package bss, FAR-calls the
; dispatcher in its header (SPEC.md 21 step 9), and registers the new
; instance in the instance table (SPEC.md 29). There is NO eviction: any
; number of package instances can be resident, and a load that cannot fit
; fails with "Out of memory" while running instances stay untouched.
;
; **v3 HAS NO RELOCATION AT ALL** (SPEC.md 20.2, and step 7 is marked
; retired): a package is linked at org 0 and loaded at a paragraph boundary
; in its own segment, so the file IS the image. This banner described the v2
; loader - a package pool at 0xB000..0xFDFF, a relocation table with two
; entry classes, a near call to the entry - for long enough that all four
; halves of that sentence were false.
;
; The region is freed by the instance record (SPEC.md 29.2 rule 7): mem_free_rec
; releases every claim the slot owns, so the store that frees a record frees
; its region. Only the UI task loads (loader_run runs on it exclusively), so
; the claim never races itself.
;
; loader_run is called only from the UI task (ui.inc step 3) with the gfx
; lock NOT held - it takes and releases the lock itself around wm_destroy /
; wm_show, and the package entry runs with the lock free (SPEC.md 20.2).
; ld_pending is written by files.inc and consumed by ui.inc; loader_run
; itself only receives the already-decoded directory index in AX.
;
; ld_run_body is structured as clusters with local error stubs (mov al /
; ret) because 8086 conditional jumps are short-range only.
;
; **This module is `.cold` (SPEC.md 2.6).** Its code has a CS of its own, so
; a routine the kernel reaches from outside is a six-byte resident thunk in
; kernel.asm under the PUBLIC name, and the body here carries an `_x` suffix;
; calls that leave the cold set go out through `KERNEL_SEG:cw_*`. Two things
; are on the author rather than the build: CS is NOT KERNEL_SEG here, so
; nothing may take a kernel segment from it, and DATA stays in `.text` - the
; section toggles below are that, and a `db`/`dw` added on the wrong side of
; one is read at the wrong segment with no diagnostic at all.
; =============================================================================

; --- status codes stored in [ld_status] (SPEC.md 21) -------------------------
LD_OK       equ 0               ; loaded and shown
LD_EDISK    equ 1               ; disk_read failed
LD_EBAD     equ 2               ; not a valid package (entry or header)
LD_EBIG     equ 3               ; too large (file, image or image+bss)
LD_EABORT   equ 4               ; package entry returned CF=1
LD_ENOMEM   equ 5               ; instance table or pool exhausted

; --- staged directory entry field offsets (SPEC.md 19) ------------------------
LD_DE_TYPE  equ 16              ; word: 1 = application package
LD_NAMELEN  equ 12              ; SPEC.md 19.1's display name: at most 12 of
                                ; a 16-byte NUL-padded field. NAMED because
                                ; step 1b's three reads after the copy are
                                ; written as `- LD_NAMELEN`: rep movsb leaves
                                ; SI advanced by exactly this
LD_DE_CLUS  equ 18              ; word: first cluster of the file's chain
LD_DE_SIZE  equ 20              ; the LOW word of a DWORD size (SPEC.md 19.1
                                ; stages 32 bits at +20). The high half is at
                                ; LD_DE_SIZE+2 and ld_run_body step 1 tests it
                                ; BEFORE it trusts this one - see there

; --- .o88 v3 header field offsets (SPEC.md 20.2) ------------------------------
LD_H_MAGIC  equ 0               ; word: 0x384F ('O','8')
LD_H_VER    equ 2               ; byte: format version = 3
LD_H_FLAGS  equ 3               ; byte: bit 0 = embedded 16x16 icon
LD_H_LINK   equ 4               ; word: link base, must be 0 (org 0 - a v3
                                ; package is position-independent because it
                                ; owns its segment, SPEC.md 20.1)
LD_H_ENTRY  equ 6               ; word: entry offset
LD_H_IMG    equ 8               ; word: image size
LD_H_BSS    equ 10              ; word: bytes to zero after the image
LD_H_DISP   equ PKG_DISP        ; 12: the three-byte dispatcher, IN the
                                ; header - `call bp / retf`. Every kernel to
                                ; package call goes through it (SPEC.md 20.1)
LD_H_CLASS  equ 15              ; byte: THE WORKER'S STACK CLASS (SPEC.md 8.7),
                                ; an index into sch_clsbytes. It is the byte
                                ; that padded the dispatcher out to a word
                                ; boundary and was 0 on every package ever
                                ; built - and 0 is "no opinion", which gets the
                                ; largest class. So this field costs nothing to
                                ; adopt: an old package keeps exactly the stack
                                ; it always had, and the header version does
                                ; not move. ld_check_hdr does NOT validate it:
                                ; the one consumer bounds it itself and an
                                ; out-of-range index gets the largest class,
                                ; which is a safe answer rather than a refused
                                ; launch
LD_HDR_SIZE equ 0x20

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - but the DATA is
                                ; not, and stays in .text at every toggle here

; -----------------------------------------------------------------------------
; ld_slot - the instance slot a reserved record sits in, which is the owner
;           word its REGION claim carries (SPEC.md 50.2)
; in:  DI = instance record ptr
; out: BX = slot index 0..INST_MAX-1
; clobbers: nothing else (flags)
;
; The region is owned by the SLOT and a package's data claims by its SEGMENT,
; deliberately: mem_free_rec already releases both, so teardown needs no new
; code, and the Task Manager's HEAP column - which sums a package's claims by
; segment - does not then count the region twice against the SIZE column that
; already reports it.
; -----------------------------------------------------------------------------
section .cold                   ; a bare restate, and it stays: this file
                                ; toggles section three more times below and
                                ; the discipline is per-block, not per-file
ld_slot:
    push cx                     ; ...and no AX save: the arithmetic is done in
    mov bx, di                  ; BX, which is the answer's own register.
    sub bx, inst_tab            ; `sub bx, imm16` is a byte longer than the
    mov cl, 5                   ; accumulator form, and the AX bracket it
    shr bx, cl                  ; replaces is two - so it is still a win.
    pop cx                      ; CX is still preserved: the header says
    ret                         ; "clobbers: nothing else" and all three
                                ; callers are read on that - memory.inc's
                                ; mem_free_rec_x is one of them and leans on
                                ; AX being untouched as well, so this is NOT
                                ; loader-private and must not be deleted with
                                ; the loader.

; -----------------------------------------------------------------------------
; ld_unreserve - give back a region, and anything the entry proc claimed
; in:  nothing - the record is [ld_rec], which is its ONE source
; out: nothing; clobbers DI
;
; It loads DI itself. Both callers spelled `mov di, [ld_rec]` in front of the
; call and there is nowhere else the argument could come from - loader_run is
; UI-task-only and one load at a time (SPEC.md 21) - so the argument was a
; parameter in name only. DI is dead at both sites: `.ret2` follows with
; `ret` and `.abort` with `mov al, LD_EABORT / ret`, and loader_run_x banks
; it.
;
; The record is not published, so mem_free_rec cannot be used: it reads
; I_SPTR, which step 9 has not written yet. Both owner words are known here
; anyway - the slot for the region, [ld_base] for whatever the entry proc
; claimed before it failed.
;
; THE BLOCKS ABOVE 1MB ARE SWEPT HERE TOO, and they have to be: since step 8
; stamps the entry proc with this instance (SPEC.md 41.5.1), an entry that
; claims extended memory and then returns CF=1 leaves blocks owned by a slot
; that is about to be handed to somebody else - so the NEXT package to take
; this slot would inherit them, and the one after that would have them freed
; out from under it. That is a worse failure than the leak it replaces, and it
; is the price of the stamp being right; it costs a table scan on a path that
; only runs when a load has already failed.
; -----------------------------------------------------------------------------
ld_unreserve:
    push bx
    mov di, [ld_rec]
    call KERNEL_SEG:cw_xm_release_rec   ; DI = the record
    call ld_slot
    call mem_free_owner_x
    mov bx, [ld_base]
    or bx, bx
    jz .out
    call mem_free_owner_x
    mov word [ld_base], 0
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; ld_hdr_ok - the .o88 FAMILY's header prologue (SPEC.md 20.1/20.2/51.1)
; in:  ES:SI -> the header, AL = the format version this loader accepts
; out: CF = 0 well formed, CF = 1 malformed
; clobbers: flags
;
; ONE BODY FOR TWO LOADERS. A package header and a driver header agree on
; every field this tests - magic 0, version 2, link 4, the three dispatcher
; bytes at PKG_DISP - because SPEC.md 51.1 defines the driver header AS the
; package header with a different version byte, and drv_check's own banner
; has said "ld_check_hdr's tests, on a driver" since it was written. What
; differs is only the version each accepts, so that is the argument.
;
; A MODULE IS DELIBERATELY NOT A THIRD CALLER. mod_check reads the same first
; two fields and then diverges: offset 4 is MOD_H_BUILD rather than a link
; word, and a module carries no dispatcher at all (offset 12 is MOD_H_ENT).
; It fails two of these five by design, which is SPEC.md 51.1's two
; independent gates working, and merging it would delete them.
;
; Both callers are .cold and so is this (SPEC.md 2.6), so the call is near.
; -----------------------------------------------------------------------------
ld_hdr_ok:
    cmp word [es:si+LD_H_MAGIC], 0x384F      ; 'O','8'
    jne .no
    cmp [es:si+LD_H_VER], al                 ; 3 = package, 4 = driver: the
    jne .no                                  ; two gates SPEC.md 51.1 wants
    cmp word [es:si+LD_H_LINK], 0            ; org 0 or it is not v3
    jne .no
    cmp word [es:si+LD_H_DISP], 0D5FFh       ; call bp
    jne .no
    cmp byte [es:si+LD_H_DISP+2], 0CBh       ; retf - and without the pair the
    je .yes                                  ; kernel's first call in jumps
.no:                                         ; into the image's data
    stc
    ret
.yes:
    clc
    ret

; -----------------------------------------------------------------------------
; ld_check_hdr - validate a .o88 v3 header (module-internal)
; in:  ES:SI -> 32 readable header bytes; [ld_fsz] = the file's byte count
; out: CF = 0 and [ld_img]/[ld_bss]/[ld_ent] filled, or CF = 1 and
;      AL = LD_EBAD / LD_EBIG
; clobbers: AX, DX
;
; ES:SI AND NOT DS:SI, which is what drv_check and mod_check have always
; taken. It used to read through DS alone, so both callers STAGED 32 bytes
; into a kernel-segment buffer first - step 3 out of the LOW_SEG sector
; buffer, step 6 out of the package's own region with a hand-rolled
; push ds/pop ds and a rep movsb - and then validated the COPY. Nothing ever
; compared the two: each call just re-ran the same ten tests. The staging
; buffer and 34 bytes of copying went with the override, and the disk-swap
; guard step 6 exists for is untouched, because what catches a swap is
; re-running this routine on what the full read delivered, not the copy.
;
; v3 (SPEC.md 20.2) has no relocation table: a package is linked at org 0 and
; loaded at a paragraph boundary in its own segment, so the file IS the
; image. The three dispatcher bytes are checked here too - they are how the
; kernel calls in, and a package whose header does not carry them would jump
; the kernel into its data on the first paint.
; -----------------------------------------------------------------------------
ld_check_hdr:
    mov al, 3                   ; magic, version, link and the dispatcher, in
    call ld_hdr_ok              ; the body the driver loader shares. v1/v2
    jc .bad                     ; files fail on the version: "Bad package"
    mov ax, [es:si+LD_H_IMG]
    cmp ax, LD_HDR_SIZE
    jb .bad                                 ; image smaller than the header
    cmp ax, APP_MAX_SIZE
    ja .toobig
    ; THE SIZE IS 32 BITS (SPEC.md 19.1) and which test bounds it is the
    ; flags byte's to say. Decided on the HIGH WORD first - "is this file
    ; bigger than a segment?" - and only then on the comparison:
    ;
    ;   hi != 0, bit2 = 0   TOO LARGE   a file past 64KB with no bit 2 cannot
    ;                                   be an image, which is what SPEC.md
    ;                                   19.1 gave up saying when the mount's
    ;                                   rule moved out to PKG_FILE_HI
    ;   hi != 0, bit2 = 1   ok          the file is longer than the image ON
    ;                                   PURPOSE and what is past it is the
    ;                                   package's own (SPEC.md 20.12)
    ;   hi == 0, img > lo   BAD         the image runs past the file, always
    ;   hi == 0, img == lo  ok          exact, always
    ;   hi == 0, img < lo, bit2 = 0  BAD    a truncated file, not a match
    ;   hi == 0, img < lo, bit2 = 1  ok     parts
    ;
    ; ONE BIT IS THE WHOLE OF WHAT THE KERNEL LEARNS about a package's parts.
    mov dx, [ld_fsz+2]
    or dx, dx
    jz .lowword                             ; under 64KB: compare exactly
    test byte [es:si+LD_H_FLAGS], 4
    jz .toobig
    jmp short .imgok
.lowword:
    cmp ax, [ld_fsz]
    ja .bad                                 ; the image runs past the file
    je .imgok                               ; exact
    test byte [es:si+LD_H_FLAGS], 4         ; short of it: parts, or truncated
    jz .bad
.imgok:
    mov [ld_img], ax
    mov dx, [es:si+LD_H_BSS]
    mov [ld_bss], dx                        ; stored BEFORE the fence, which is
                                            ; free: both readers of [ld_bss]
                                            ; (step 4's `add ax` and step 7's
                                            ; `mov cx`) sit downstream of a
                                            ; `jc` on this routine, so a
                                            ; refused value is never read
    add dx, ax                              ; THE CARRY IS THE WHOLE FENCE.
    jc .toobig                              ; Each operand is separately <=
                                            ; APP_MAX_SIZE = 0xF000, so the sum
                                            ; reaches 0x1E000 and that is 17
                                            ; BITS: this line used to read
                                            ; `img+bss <= 0x1E000: no wrap`,
                                            ; which is the bug written down as
                                            ; its own proof. img = 0xF000 with
                                            ; bss = 0x1001 wraps to 1, passes
                                            ; the compare below, sizes a ONE
                                            ; KILOBYTE claim at step 4 and then
                                            ; reads 61,440 bytes into it at
                                            ; step 6 - 60,416 bytes through
                                            ; whatever mem_claim_hi placed
                                            ; under it, which is a resident
                                            ; package's CODE, top-down. Every
                                            ; byte off a disk is hostile
                                            ; (SPEC.md 19) and os88pkg.py is
                                            ; not a gate on a foreign .O88.
                                            ; SPEC.md 21 states this bound as
                                            ; an invariant twice ("never a
                                            ; neighbour's region") and the
                                            ; carry is what makes it true.
    cmp dx, APP_MAX_SIZE                    ; ...and the bss-alone test that
    ja .toobig                              ; used to sit above is implied by
                                            ; this one: image >= LD_HDR_SIZE,
                                            ; so bss < APP_MAX_SIZE follows
    mov ax, [es:si+LD_H_ENTRY]
    cmp ax, LD_HDR_SIZE
    jb .bad
    cmp ax, [ld_img]
    jae .bad
    mov [ld_ent], ax
    clc
    ret
.bad:
    mov al, LD_EBAD
    stc
    ret
.toobig:
    mov al, LD_EBIG
    stc
    ret

; -----------------------------------------------------------------------------
; ld_say_status - record a load's verdict and SAY it (SPEC.md 59, 22.9)
; in:  AL = the LD_* status
; out: [ld_status] = AL; a toast off fm_stattab
; clobbers: BX, CX (the table and its bound).  AX AND THE FLAGS SURVIVE,
;      because toast_say preserves every register and the flags - loader_run_x
;      leans on that with its `cmp al, LD_EABORT` two lines after the call,
;      and neither caller reads BX or CX between here and its own epilogue,
;      which is what the two inline copies already relied on
;
; It used to be stamped on the window that asked, because one global put
; 'Bad package' on every open Disk window at once - a question a toast cannot
; raise, since it belongs to no window at all and appears at the moment of
; the act (SPEC.md 22.9).  Written out twice until size pass 2: here and in
; assoc.inc's assoc_run_x, whose copy carried the same "...and SAY it".
;
; NO push bx/cx here either: the two sites this replaces did not bank them and
; do not need them.  toast_say is `.text` and this is `.cold`, so the call to
; it is far; both callers are `.cold`, so the calls to this are near.
; -----------------------------------------------------------------------------
ld_say_status:
    mov [ld_status], al
    cmp byte [ld_said], 0       ; SPEC.md 20.12.4: the entry proc's own toast
    je .say                     ; is up, and it names the reason better than
    mov byte [ld_said], 0       ; `Load failed` would - record, and say nothing
    ret
.say:
    mov bx, fm_stattab
    mov cx, LD_NSTAT
    call KERNEL_SEG:toast_say
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
loader_run_x:
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
    call fmv_sync_x             ; ...make the globals BE that listing. NO ax
                                ; bank: files.inc's own banner is "out:
                                ; nothing (all registers preserved)" and the
                                ; body opens push ax/bx/dx and closes
                                ; pop dx/bx/ax on EVERY exit, so the bank is
                                ; dead by construction and not only by
                                ; contract
.body:
    mov byte [ld_painted], 0
    call ld_run_body_x          ; out: AL = LD_* status
    call ld_say_status          ; record the verdict and SAY it (SPEC.md 59)
    cmp byte [ld_painted], 0
    jne .done                   ; the success path drew the new window itself
                                ; (SPEC.md 11.90), so nothing is owed here
    cmp al, LD_EABORT           ; ...and a FAILED load owes nothing either: it
    jne .done                   ; drew nothing, and the poster's window never
                                ; said a word about this load in the first
                                ; place (SPEC.md 22.17 - it used to hold
                                ; 'Loading...', and this path existed to take
                                ; that back). The verdict went out as a toast
                                ; above, which belongs to no window at all.
                                ; The ONE exception is a package the loader
                                ; REFUSED after running its entry proc, which
                                ; may have put pixels anywhere before it said
                                ; no - and that is a whole-screen repaint,
                                ; because there is no telling where
    call files_refresh_x
.done:

    jmp kretfc_es

; -----------------------------------------------------------------------------
; ld_run_body - the load pipeline (module-internal; loader_run saves regs)
; in:  AX = directory index
; out: AL = LD_* status
;
; Steps (SPEC.md 21): validate the dir entry; peek the header via a
; 1-sector read into dsk_secbuf; size the allocation
; (roundup512(max(img+bss, fsize)) - sector granularity means the full-file
; read can never touch a neighbouring region); reserve an instance record +
; pool region; read the whole file; re-check the header (disk-swap guard);
; apply the reloc table; zero the bss (killing the table); call the entry;
; register + publish the instance and wm_show its window. Every failure
; path leaves running instances untouched - the reserved record was never
; published, so it (and with it the region) is simply still free.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; ld_run_name - the load pipeline, resolved by NAME (SPEC.md 21.4)
; in:  SI -> a NUL-terminated 8.3 name, in the CURRENT directory
; out: AL = LD_* status
;
; ld_run_body's twin, and it exists so a caller does not need a LISTING. An
; index is only meaningful against disk_dir, and making disk_dir be a given
; folder is a FULL mount - the directory scan, the sort and one icon-harvest
; read per file (SPEC.md 18.9) - where dskw_stat walks the directory sectors
; and answers the same three questions step 1 asks. Every caller that had to
; mount only to turn a name it already had into an index can stop.
;
; It is also the SAFER resolution, which is worth saying because it looks
; like a pure optimisation. An index is resolved against a snapshot that can
; have shifted underneath it - a name sorting into place moves everything
; after it (SPEC.md 19.4) - and that is docs/FIELD-NOTES.md 4, "Bad package"
; about a file nobody clicked. A name cannot be shifted into somebody else's
; entry: it either exists in this directory now or it does not.
; -----------------------------------------------------------------------------
ld_run_name_x:
    call ld_res_name            ; ...which ends in ld_take, and ld_take leaves
                                ; the cluster IN AX as well as in [ld_clus].
                                ; `.peek` takes it from the register - it used
                                ; to be an undocumented contract at a jump
                                ; target with a `mov ax, [ld_clus]` at each of
                                ; the two entries honouring it, which is
                                ; exactly the kind of difference that
                                ; assembles cleanly and runs wrong: without it
                                ; AL was still dskw_stat's ATTRIBUTE byte and
                                ; dsk_clus2lba was handed 0x20 as a cluster.
                                ; One producer now, not two.
    jc .fail
    ; --- 1b, for THIS entry: the launching file's NAME, banked --------------
    ; The index path copies it out of dsk_ent in ld_run_body_x's own step 1b,
    ; which sits ABOVE .peek - so joining at .peek used to leave ld_lname
    ; holding whatever the last index launch wrote, and step 8 handed a
    ; package opened by association, or by the Task Manager, a stale name
    ; (SPEC.md 21 step 1b). SI is the caller's NUL-terminated 8.3 name and
    ; dskw_stat_x/ld_take left it alone. At most LD_NAMELEN characters, then
    ; the terminator: the same shape 1b writes.
    push ax                     ; the cluster .peek takes from AX
    push ds
    pop es
    mov di, ld_lname
    mov cx, LD_NAMELEN
    cld
.cp:
    lodsb
    stosb
    or al, al
    jz .named
    loop .cp
    mov byte [di], 0            ; ES == DS: no override needed
.named:
    pop ax
    jmp ld_run_body_x.peek      ; joins the pipeline exactly where the index
.fail:                          ; resolver leaves it - [ld_fsz] and [ld_clus]
    ret                         ; are the rest of what step 1 produces

; -----------------------------------------------------------------------------
; ld_res_name - fill [ld_fsz]/[ld_clus] from a name (module internal)
; in:  SI -> a NUL-terminated 8.3 name
; out: CF=0 and AX = the first cluster/handle; CF=1 with AL = LD_EBAD / LD_EBIG
; -----------------------------------------------------------------------------
ld_res_name:
    call dskw_stat_x            ; AL = raw attribute, BX = first cluster,
    jc ld_take.bad              ; DX:CX = size. A near call - loader.inc and
                                ; diskw.inc are both .cold (SPEC.md 2.6)
    test al, 0x1E               ; hidden | system | volume | directory: the
    jnz ld_take.bad             ; species disk_mount's own filter drops, so
                                ; this and "type 1" agree on what may be
                                ; loaded. READ-ONLY is deliberately allowed -
                                ; TASKMGR.O88 is read-only and is an app
                                ; (SPEC.md 19.6). BOTH REFUSALS ARE ld_take's
                                ; OWN, four bytes further down, and the same
                                ; AL = LD_EBAD with CF set - so this routine
                                ; carries no stub and does not even jump: it
                                ; FALLS THROUGH into the three questions
                                ; below, shared with step 1

; -----------------------------------------------------------------------------
; ld_take - "is this a file this loader will take?", asked ONCE (SPEC.md 21
;           step 1).  ld_res_name and ld_run_body_x's step 1 asked the same
;           three questions of the same three values and stored them in the
;           same three words - and THE TWO COPIES HAD DRIFTED.  Step 1 knows
;           that on a DVK_FILE volume the "first cluster" is an OPAQUE HANDLE
;           (SPEC.md 62.9.1) and that [dsk_maxclus] belongs to the last FAT
;           volume mounted; ld_res_name range-checked it anyway.  Sharing the
;           body gives the by-NAME resolver that exception, which is a
;           BEHAVIOUR CHANGE and the point rather than a side effect.
; in:  DX:CX = the file's byte size, BX = its first cluster or handle
;      (dskw_stat_x's own output registers, which is why they are these)
; out: CF=0, [ld_fsz]/[ld_clus] filled and AX = BX for `.peek`;
;      CF=1 with AL = LD_EBAD / LD_EBIG
; -----------------------------------------------------------------------------
ld_take:
    cmp dx, PKG_FILE_HI
    jae .big                    ; past what the mount will type at all: a file
                                ; up to 1MB is a package now (SPEC.md 19.1),
                                ; so a 70KB one arrives with a LOW word of
                                ; 4,608 - a plausible small package - and
                                ; reading it without this is the wrap SPEC.md
                                ; 21.4 warns about
    mov [ld_fsz+2], dx          ; the rest is ld_check_hdr's to bound, because
    mov [ld_fsz], cx            ; a package carrying parts is bigger than its
    or cx, cx                   ; image by construction (SPEC.md 20.12) and
    jnz .szok                   ; only the flags byte says so
    or dx, dx
    jz .bad                     ; empty file
.szok:
    cmp byte [dsk_vkind], DVK_FILE  ; ...unless the cluster word is AN OPAQUE
    je .clusok                      ; HANDLE (SPEC.md 62.9.1), in which case
                                    ; the kernel has no opinion about its range
                                    ; - and [dsk_maxclus] is the LAST FAT
                                    ; volume's, so the test is not merely wrong
                                    ; here, it is answered by a number
                                    ; belonging to another disk
    cmp bx, 2                   ; mount already validated (SPEC.md 21 step 1 -
    jb .bad                     ; this check stands alone if the mount code
    cmp bx, [dsk_maxclus]       ; ever changes)
    ja .bad
.clusok:
    mov [ld_clus], bx
    xchg ax, bx                 ; ...AND IN AX: `.peek` takes the cluster (or
    clc                         ; the handle) from the register, and one byte
                                ; does it - BX becomes the old AX, which is
                                ; the directory index on step 1's path and
                                ; dskw_stat_x's attribute byte on the name
                                ; one, and .peek writes BX on every arm
                                ; before anything reads it
    ret
.bad:
    mov al, LD_EBAD
    stc
    ret
.big:
    mov al, LD_EBIG
    stc
    ret

ld_run_body_x:
    ; --- 1. validate the directory entry --------------------------------------
    cmp ax, [disk_nfiles]
    jae ld_take.bad             ; index out of range - and ld_take's own
                                ; refusal, which is the same AL = LD_EBAD.
                                ; The only difference is that CF comes out
                                ; SET, and CF=1 on the way out is already
                                ; this routine's normal failure: every caller
                                ; reads AL alone (loader_run_x and assoc_run_x
                                ; both `call ld_say_status`, ui.inc does
                                ; `or al, al`), which is why step 1 can now
                                ; fall straight into .peek instead of jumping
                                ; over a three-byte stub
    call dsk_get_dir_x ; SI -> the entry, staged out of LOW_SEG into
                                ; the kernel segment (SPEC.md 2.1)
    cmp word [si+LD_DE_TYPE], 1
    jne ld_take.bad             ; not an application package

    ; --- 1b. the launching file's NAME, banked (SPEC.md 21 step 1b) --------
    ; HERE AND NOT AT STEP 9, because dsk_get_dir stages into dsk_ent, which
    ; is shared scratch that the icon harvest and every read below will spend.
    ; ONE BUFFER FOR THE MACHINE and not one per instance: the kernel does not
    ; remember where an instance came from - it says so once, at the only
    ; moment a package needs telling, and the package copies it (SPEC.md
    ; 20.2, 20.12.1). That is the whole of the third fact this design adds.
    push ds                     ; ...and NOT DI, CX or ES. DI's next write is
    pop es                      ; step 5's cw_inst_alloc, CX's is the
    mov di, ld_lname            ; `mov cx, [si+...]` three instructions down,
    mov cx, LD_NAMELEN          ; and ES is set by .peek before its next
    cld                         ; reader - so all three are provably dead
    rep movsb                   ; between here and there, and loader_run_x
    mov byte [di], 0            ; banks all eight anyway. ES == DS two lines
                                ; up, so the terminator needs no override
                                ; either. SI IS NOT BANKED: rep movsb with
                                ; CX = LD_NAMELEN and DF = 0 advances it by
                                ; exactly that, so the three reads below name
                                ; the same three fields with the copy's own
                                ; length subtracted - which is why it is a
                                ; NAMED constant and written as a subtraction
    mov dx, [si+LD_DE_SIZE+2-LD_NAMELEN]  ; the size and the cluster, in
    mov cx, [si+LD_DE_SIZE-LD_NAMELEN]    ; dskw_stat_x's own registers,
    mov bx, [si+LD_DE_CLUS-LD_NAMELEN]    ; because ld_take answers for both
                                ; entries and that is its input shape.
    call ld_take                ; BX, CX and DX are loader_run_x's to bank and
    jc .ret1                    ; it does. On the error arm ld_take answers
                                ; with CF=1 as well as AL, and CF=1 on the way
                                ; out is already this routine's normal
                                ; failure - ld_check_hdr's `jc .ret1` below is
                                ; the same shape; the caller reads AL only
                                ; (loader_run_x: `mov [ld_status], al`)

    ; --- 2. peek the header: first sector into dsk_secbuf ---------------------
    ; dsk_secbuf is disk.inc's mount scratch - safe to reuse here because
    ; both disk_mount and loader_run run only on the UI task. It lives in
    ; LOW_SEG, so the sector lands there and only the 32 header bytes are
    ; staged back into the kernel segment for ld_check_hdr to read.
.peek:
    push ss                     ; ES = LOW_SEG, ONCE for both arms and for
    pop es                      ; step 3 below (SPEC.md 2.1). The FAT arm
                                ; wants it for disk_read_x's ES:BX, the
                                ; DVK_FILE arm leaves it alone (drv_fs_call:
                                ; "DS and ES restored"), and ld_check_hdr
                                ; reads the header where it lands rather than
                                ; through a staged copy
    cmp byte [dsk_vkind], DVK_FILE
    jne .peekfat

    ; --- a REDIRECTED volume: the first 512 bytes, by handle (SPEC.md 62.9) --
    ; The FAT path below turns a cluster into an LBA and reads a SECTOR, and
    ; there are none here - dsk_xfer refuses such a volume outright, so
    ; without this the peek fails and every package on a redirected volume is
    ; "Bad package". FSV_READAT at offset 0 is exactly the same 512 bytes, and
    ; it lands in the same dsk_secbuf so step 3 below is untouched.
    push bp
    mov dx, ss                  ; DX:BX, not ES:BX (SPEC.md 51.8) - AX is
    mov bx, dsk_secbuf          ; still the handle and nothing here spends it.
                                ; SS names LOW_SEG for the life of the session
                                ; (SPEC.md 2.1) and this site wants the value
                                ; in a general register, so it is `mov dx, ss`
                                ; rather than the `push ss / pop es` form
    mov cx, 512
    xor si, si                  ; DI:SI = offset 0: the head of the file
    xor di, di
    mov bp, FSV_READAT
    call drv_fs_call
    pop bp
    jc .disk1                   ; a short file simply delivers less, and
    jmp short .hdr              ; ld_check_hdr rejects what is not a package
.peekfat:
    call dsk_clus2lba_x  ; AX: cluster -> its first LBA; the first
    jc ld_take.bad              ; sector of the first cluster IS the file's
                                ; first 512 bytes, so peek semantics are
                                ; exact (SPEC.md 21 step 2)
    call dsk_rd1_x                ; in: AX=LBA; ES set at .peek
    jc .disk1
.hdr:

    ; --- 3. validate the peeked header -----------------------------------------
    mov si, dsk_secbuf          ; ...WHERE IT LANDED. ES is LOW_SEG from
    call ld_check_hdr           ; .peek and ld_check_hdr reads through it, so
    jc .ret1                    ; there is no staged copy to make and none to
                                ; go stale. AL = LD_EBAD / LD_EBIG on failure

    call ld_alloc               ; --- 4 and 5, shared with OSAPI_PKG_RUN
    jc .ret1                    ; (SPEC.md 21.5): AL = LD_ENOMEM, and no
                                ; region was left claimed
    jmp short .read
.disk1:
    mov al, LD_EDISK
.ret1:
    ret
.ret2:                          ; step 6 failed with a region already claimed
    call ld_unreserve           ; (AL is the status): give it back, or a
    ret                         ; failed load would hold heap for the session.
                                ; The record itself is unpublished, so it
                                ; needs nothing - and AL is NOT banked:
                                ; ld_unreserve preserves AX (its banner, and
                                ; every callee down to mem_free_owner_x's
                                ; "all registers preserved")

    ; --- 6. read the whole file into the region --------------------------------
.read:
    mov ax, [ld_img]            ; THE IMAGE, NOT THE FILE. On a v3 package the
    call dsk_bytes_secs_x       ; two are equal and always were; on one
                                ; carrying parts the region holds the primary
                                ; alone and the rest is the package's to read
                                ; (SPEC.md 20.12). DX = ceil(image/512)
    mov ax, [ld_clus]
    mov es, [ld_base]           ; the package's OWN segment, offset 0
    xor bx, bx
    call dsk_read_chain_x ; in: AX=cluster, DX=sectors, ES:BX -> dest.
    jc .ret2                    ; on CF, AL is ALREADY the status: 1 =
                                ; LD_EDISK (disk error), 2 = LD_EBAD (chain
                                ; corrupt is bad data, not a disk error) -
                                ; SPEC.md 21 step 6

    xor si, si                  ; re-check against the in-region header - the
    call ld_check_hdr           ; disk could have been swapped between the
    jc .ret2                    ; peek and the read. ES IS STILL [ld_base]:
                                ; dsk_read_chain_x is "everything else
                                ; preserved", so the header is read where the
                                ; read just put it. This used to stage 32
                                ; bytes back into the kernel segment with a
                                ; push ds/pop ds and a rep movsb, purely
                                ; because ld_check_hdr read through DS

    jmp ld_start                ; --- 7, 8 and 9, shared with OSAPI_PKG_RUN
                                ; (SPEC.md 21.5). A TAIL jump: ld_start
                                ; answers in AL exactly as this routine does,
                                ; frees the region itself when the entry proc
                                ; aborts, and is near - both are .cold

; -----------------------------------------------------------------------------
; ld_alloc - steps 4 and 5: size the region and reserve it, together with the
;            instance record that OWNS it (SPEC.md 21, 21.5)
; in:  [ld_img] and [ld_bss], both already bounded by ld_check_hdr
; out: CF = 0 with [ld_rec], [ld_base] and [ld_need] filled;
;      CF = 1 with AL = LD_ENOMEM - no instance record, or no region
;
; SPLIT OUT FOR OSAPI_PKG_RUN (SPEC.md 21.5), which has no directory entry, no
; cluster and no disk, and needs every one of these three words before it can
; copy an image anywhere. Nothing about the arithmetic moved.
; -----------------------------------------------------------------------------
ld_alloc:
    mov ax, [ld_img]            ; need = roundup512(img + bss)
    add ax, [ld_bss]            ; <= APP_MAX_SIZE, and NOW that is enforced:
    add ax, 511                 ; ld_check_hdr's `jc` above is what makes both
                                ; of these adds overflow-free, so `<= 0xF000 +
                                ; 511: no 16-bit overflow` is a fact rather
                                ; than the assumption it was
    and ax, 0FE00h              ; ...and down to the 512 boundary. `>>9 <<9`
                                ; through CL is the same operation in six
                                ; bytes instead of three, and clobbers CL
                                ;
                                ; NO SECOND BOUND TEST, and no `.big2`: with
                                ; the fence repaired this value is at most
                                ; 0xF000 by construction, so `cmp ax,
                                ; APP_MAX_SIZE / ja .big2` was code that
                                ; could not run. It was also the one exit
                                ; here reached by a `ja` rather than a `jc`,
                                ; so it answered CF=0 with AL = LD_EBIG -
                                ; harmless only because loader_run reads AL
                                ; and never the flag
                                ;
                                ; ...AND NO `mov [ld_need], ax` HERE: step 5
                                ; below stores the CLAIM's size into the same
                                ; word and the one reader (step 9's I_SIZE) is
                                ; downstream of it. This was the size ASKED
                                ; FOR, never the size got

    ; --- 5. reserve an instance record + a pool region -------------------------
    call KERNEL_SEG:cw_inst_alloc ; DI = record (unpublished: still free on
    jc .nomem                   ; any later failure)
    mov [ld_rec], di            ; banked in .bss, not in BP: BP carries the
                                ; entry offset into the dispatcher below,
                                ; and loader_run is UI-task-only and one
                                ; load at a time (SPEC.md 21)
    call mem_bytes_kb_x         ; the region is a heap claim now (SPEC.md
                                ; 20.1/50.3), so it rounds to whole KB rather
                                ; than to the 512 bytes int 13h needs - which
                                ; mem_claim's own granularity then guarantees
    call ld_slot                ; BX = the owner word: this record's slot
    call mem_claim_hi_x ; from the TOP down, away from the data claims
    jc .nomem                   ; growing up from the bottom (SPEC.md 50.3)
    mov [ld_base], dx
    shl ax, cl                  ; I_SIZE is bytes, and it is what the region
                                ; occupies now, not what the file needed.
                                ; **CL IS STILL 10**, set by mem_bytes_kb_x
                                ; and documented as one of its outputs BECAUSE
                                ; of this line. That rests on TWO DOCUMENTED
                                ; CONTRACTS either side of it:
                                ; ld_slot preserves CX (it pushes it - and its
                                ; header says "clobbers: nothing else") and
                                ; mem_claim_hi_x "clobbers: DX (an output) and
                                ; the flags - every other register preserved",
                                ; which holds through mem_compact's far calls
                                ; into package relocation procs. The `jc`
                                ; between them jumps AWAY, not through.
                                ; WHAT A WRONG SHIFT COSTS: [ld_need] becomes
                                ; I_SIZE, and I_SIZE is the bound in two
                                ; OWNERSHIP FENCES - fsx.inc's "an entry inside
                                ; its image+bss" (SPEC.md 53.1) and
                                ; instance.inc's copy on the package-entry
                                ; path. Too large a value widens a security
                                ; fence, silently. Loosen either contract and
                                ; this line has to come back.
    mov [ld_need], ax
    clc                         ; `shl ax, cl` above WRITES CF, and this
    ret                         ; routine's answer is CF - it was the caller's
                                ; fall-through before the split, where the
                                ; flag went nowhere
.nomem:
    mov al, LD_ENOMEM
    stc
    ret

; -----------------------------------------------------------------------------
; ld_start - steps 7, 8 and 9: zero the package bss, call its entry proc
;            through the dispatcher in its own header, and register, publish
;            and show the instance (SPEC.md 21, 21.5)
; in:  ES = the region, with the image already in it; [ld_rec], [ld_base] and
;      [ld_need] as ld_alloc left them
; out: AL = LD_OK, or LD_EABORT with the region and everything the entry proc
;      claimed given back
;
; The other half of the split OSAPI_PKG_RUN needed: from here down the loader
; does not care whether the image arrived off a disk or out of a caller's
; claim, which is exactly why the slot is 100-odd bytes rather than a second
; pipeline.
; -----------------------------------------------------------------------------
ld_start:
    ; --- 7. zero the package bss ------------------------------------------------
    mov di, [ld_img]            ; ...and ES is STILL the region here too
    mov cx, [ld_bss]
    xor al, al
    cld
    rep stosb                   ; CX may be 0 (no-op)
    push ds
    pop es                      ; ES = KERNEL again (SPEC.md 1 rule 3)

    ; --- 8. launch ---------------------------------------------------------------
    ; The entry runs with CS = DS = its own segment, ES = KERNEL_SEG, IF = 1
    ; and the gfx lock NOT held (SPEC.md 20.2). It is reached the same way
    ; every other package callback is - through the dispatcher in its header
    ; (SPEC.md 20.1) - so an entry proc is an ordinary near proc ending in an
    ; ordinary `ret`, and returns BX = its window / CF set.
    ;
    ; AND IT IS STAMPED LIKE ONE (SPEC.md 34.3/41.5.1). Without this the entry
    ; proc has NO INSTANCE IDENTITY: inst_caller answers with the dispatched
    ; callback's stamp or else the RUNNING TASK's T_INST, and this runs on the
    ; UI task, whose T_INST is 0xFF - so everything an entry proc asked the
    ; kernel for came back attributed to the KERNEL. A sound grant was never
    ; released at teardown, a block above 1MB was stamped XM_OWN_KERN and
    ; stranded for the session, a toast outlived its app, and the file API
    ; answered with the machine's directory instead of the instance's. The
    ; entry proc is the FIRST thing a package runs and the natural place to
    ; size itself, so it was the one dispatch that most needed identifying and
    ; the only one that lacked it.
    ;
    ; The push/call/pop bracket is wm.inc's, exactly (SPEC.md 34.3): the outer
    ; stamp is restored whatever the entry does, and it nests inside the
    ; `push ds`/`pop ds` below rather than around it, so both unwind in order.
    ; `pop` leaves the flags alone, so the entry's CF still reaches .abort.
    ;
    ; The record is UNPUBLISHED here (step 9 writes I_STATE), and that is fine:
    ; the stamp is the slot INDEX, which is fixed the moment the record was
    ; reserved. What it does mean is that an aborting entry's claims have to be
    ; swept by ld_unreserve, which is where the XMS release joins the two heap
    ; ones - the slot is about to be handed to somebody else.
    push word [snd_inst]        ; the outer dispatch's stamp
    mov di, [ld_rec]
    call KERNEL_SEG:cw_snd_disp_set     ; DI = record: this instance is asking

    mov bp, [ld_ent]            ; ...and NOTHING assembles a far pointer here:
                                ; [ld_fp] is PKG_DISP for the life of the
                                ; machine and [ld_fp+2] IS [ld_base], the
                                ; word step 5 already wrote (see the
                                ; declarations - driver.inc's drv_fptr /
                                ; drv_fseg pair is the same idiom, one step
                                ; less far)
    push ds                     ; ...and ES IS ALREADY KERNEL_SEG, per the
                                ; callback contract: step 7's own
                                ; `push ds / pop es` set it ten lines up and
                                ; the only thing between is cw_snd_disp_set,
                                ; whose body is "clobbers: nothing (flags)".
                                ; This `push ds` banks DS alone, for the
                                ; `pop ds` after the far call
    mov si, ld_lname            ; SI = the file we came out of (SPEC.md 20.2's
                                ; entry contract). It is [es:si] - an offset
                                ; into the KERNEL's segment - and it is the
                                ; one thing in that contract a package may
                                ; ignore, which is what makes adding it
                                ; compatible with every package built before
    mov ds, [ld_base]           ; DS = the package's own segment, read through
    call far [es:ld_fp]         ; DS while DS still names the kernel and
    pop ds                      ; loaded in one instruction - the ordering
                                ; this step's contract wants. The far pointer
                                ; is then read through ES, the only register
                                ; still naming KERNEL_SEG here
    pop word [snd_inst]         ; ...and the stamp comes off, CF untouched
    jc .abort

    ; --- 9. register + publish the instance, show the window ---------------------
.reg:
    mov di, [ld_rec]
    mov word [di+I_KIND], (0xFF << 8) | KIND_PKG
%if I_TASK != I_KIND + 1
  %error "loader step 9 writes I_KIND and I_TASK as one word"
%endif
                                ; born task-less; a callback may claim one
                                ; worker later (SPEC.md 20.6). The entry
                                ; proc cannot: neither I_STATE nor
                                ; wm_owner is published until right here.
    mov ax, [ld_base]
    mov [di+I_SPTR], ax         ; the region's base SEGMENT (SPEC.md 20.1)
    mov ax, [ld_need]
    mov [di+I_SIZE], ax         ; allocated bytes: pool occupancy + RAM sum
    push es
    mov es, [ld_base]           ; the name field is in the package's image
    mov si, 16
    call KERNEL_SEG:cw_inst_set_name_x ; ES:SI -> the 16-byte NUL-padded name
                                ; ("out: nothing - all registers preserved")
    ; --- and the ICON, off the SAME ES -------------------------------------
    ; A SENTINEL, not a copy. The icon body is 64 bytes at a fixed offset in
    ; the package's own header, and the region holding it lives exactly as
    ; long as the instance does - so a per-slot copy in kernel .bss was 768
    ; bytes spent duplicating something that could not go away first.
    ; inst_icon_ptr stages one body back into the kernel segment when a tile
    ; is actually drawn, which is the dsk_get_icon idiom and costs nothing on
    ; any path that does not draw (SPEC.md 20.2/25). So there is nothing here
    ; but a flag test, and no reason for it to be a routine that sets up the
    ; ES the line above already has.
    mov word [di+I_ICON], 0
    test byte [es:LD_H_FLAGS], 1
    jz .noico
    mov word [di+I_ICON], I_ICO_PKG
.noico:
    pop es
    call KERNEL_SEG:cw_inst_bind_win ; DI = record, BX = window (from the entry)
    mov byte [di+I_STATE], 1    ; publish - the LAST record write
    ; --- the status BEFORE the repaint, not after ---------------------------
    ; A Disk window's status line draws [ld_status] (SPEC.md 22), so the
    ; value has to be published before the pass that draws it. Setting it in
    ; loader_run afterwards is what forced a SECOND full-screen repaint on
    ; every successful launch, purely to correct one line of text.
    mov word [ld_status], (1 << 8) | LD_OK   ; ...and the repaint flag with
                                ; it, in ONE store: the two are adjacent and
                                ; are only ever written together here. The
                                ; %if beside their declarations makes a
                                ; re-ordering a build error
    ; --- and the POSTER is owed NOTHING (SPEC.md 22.17) ---------------------
    ; There was a files_poster call here, immediately before the wm_show
    ; below. The window that posted this load used to have 'Loading...' on
    ; its status line, so somebody had to take the word back off - and doing
    ; it AFTER wm_show was one instruction too late, because by then the new
    ; window is on top, files_poster's wm_clip_set drops the raise cache
    ; wm_show has just taken (11.96) and there is nothing left on the glass
    ; to bank a new one from: that window then repainted in full on every
    ; interaction for the rest of the session (11.96.9.1's remaining row).
    ; The word is gone now - SPEC.md 12.8's menu-bar widget reports the read
    ; live, for every reader in the machine - so the poster's window is
    ; untouched by this load from beginning to end, and the ordering that
    ; had to be got right no longer exists to get wrong.
    call KERNEL_SEG:cw_gfx_lock
    call KERNEL_SEG:cw_wm_show  ; in: BX = window ptr; repaints all
    call KERNEL_SEG:cw_gfx_unlock
    mov al, LD_OK
    ret

.abort:
    ; SPEC.md 20.2: an aborting entry may have created windows before it
    ; declared itself broken - the one it returns in BX, and any others.
    ; Destroy them ALL by the W_SEG creator stamp (wm_destroy_seg) rather
    ; than by trusting BX: a package that just declared itself broken gets
    ; nothing believed, and the sweep only ever touches records wm_create
    ; stamped itself. BEFORE ld_unreserve, so no window outlives the region
    ; its procs live in - a survivor's repaint would far-call a freed claim.
    ; The reserved instance record was never published, so it needs nothing.
    call KERNEL_SEG:cw_gfx_lock
    mov ax, [ld_base]
    call KERNEL_SEG:cw_wm_destroy_seg
    call KERNEL_SEG:cw_gfx_unlock
    ; ...but NOT its toast (SPEC.md 20.12.4). A refusal the entry proc SAID -
    ; os88parts.inc's four, a C package's - carries the reserved slot as its
    ; raiser, because step 8's stamp is in force for the whole of the call
    ; (SPEC.md 19.2.1). Step 10 would toast `Load failed` over it and the
    ; reason would never reach the glass. So when the toast that is up is
    ; this instance's, re-home it to the kernel - the slot is about to be
    ; somebody else's, and their close must not take it down - and tell
    ; ld_say_status to record the verdict without saying it. A toast raised
    ; by anybody else during the launch is not ours and is overwritten as
    ; before.
    mov ax, [ld_rec]            ; AL = the reserved slot's index - inst_idx's
    sub ax, inst_tab            ; own arithmetic (32-byte records), inline
    mov cl, 5                   ; because that routine is .text and this is
    shr ax, cl                  ; .cold (SPEC.md 2.6): a shim costs 5 .text
    cmp al, [toast_inst]
    jne .unsaid
    cmp word [toast_on], 0      ; toast_kill's own test: on OR staged
    je .unsaid
    mov byte [toast_inst], 0xFF
    mov byte [ld_said], 1
.unsaid:
    call ld_unreserve           ; the region, and anything the entry proc
                                ; claimed before it declared itself broken
    mov al, LD_EABORT
    ret

; -----------------------------------------------------------------------------
; ld_pkg_run - OSAPI_PKG_RUN: run a package image that is ALREADY IN MEMORY
;              (SPEC.md 21.5), slot KERNEL_SEG:0x04F8
;
; in:  ES:SI = the image, byte for byte what the .O88 file holds, in a claim
;              of the caller's. ANY segment: it is COPIED, never adopted, so
;              the caller may free it the moment this returns
;      DX:CX = its length in bytes, DX the high word
;      DI    = a NUL-terminated 8.3 name, at most 12 characters, in the
;              CALLING INSTANCE's own segment (inst_caller, SPEC.md 19.2.1) -
;              what the new instance is told it was launched from, which is
;              step 1b's [ld_lname] and the SI of the entry contract (20.2)
; out: CF = 0, AX = 0: the instance is registered, published and its window
;              shown, exactly as a Disk-window double-click leaves one
;      CF = 1, AL = LD_* (SPEC.md 21.4)
;
; UI TASK ONLY, GFX LOCK NOT HELD - the same context loader_run runs in, and
; for its reasons: step 9 takes the lock itself around wm_show, and step 8
; runs the package's entry proc, which creates windows and may draw. ui.inc's
; ladder calls loader_run_x with the lock free (its `.chk_ld` step) and this
; is reachable only from a window callback or an OSAPI_WM_ONWAKE handler, both
; of which the UI task runs with the lock released.
;
; WHAT IT IS NOT: a second loader. Every check, the region claim, the entry
; call and the publish are ld_check_hdr, ld_alloc and ld_start - the same
; bodies the disk path enters. What is here is the three things a memory image
; needs that a file does not: a length that arrives in registers rather than
; out of a directory entry, a NAME out of the caller's segment, and a `rep
; movsb` where step 6 has an int 13h.
;
; A PACKAGE WITH PARTS IS REFUSED (header flags bit 2, SPEC.md 20.12): parts
; are read by the package from its OWN FILE, and there is no file here. That
; is LD_EBAD and it is asked BEFORE ld_check_hdr, which would allow the bit.
; -----------------------------------------------------------------------------
ld_pkg_run_x:
    mov [ld_fsz], cx            ; the size, in the two words ld_check_hdr
    mov [ld_fsz+2], dx          ; bounds the image against
    test byte [es:si+LD_H_FLAGS], 4
    jnz .bad                    ; parts, and no file to read them out of
    call ld_check_hdr           ; ES:SI IS where it reads - the same call the
    jc .fail                    ; disk path makes against dsk_secbuf in
                                ; LOW_SEG. AL = LD_EBAD / LD_EBIG

    mov [ld_msrc], si           ; the source, banked: ld_alloc's chain reaches
    mov ax, es                  ; mem_compact and a package relocation proc,
    mov [ld_msrc+2], ax         ; and ES is nobody's to promise across that

    ; --- the NAME, out of the caller's segment (SPEC.md 21 step 1b) ---------
    ; DI is an offset in the CALLING INSTANCE's segment and not in ES: the
    ; image is in a claim and the name is in the package's own data, which are
    ; two different segments by construction. inst_caller is the published way
    ; to ask which instance is calling (SPEC.md 19.2.1); a call from kernel
    ; context answers 0xFF and the name is then read through KERNEL_SEG, which
    ; is the only segment there is to read it through.
    push ds
    pop es                      ; ES = KERNEL for the copy's destination
    call KERNEL_SEG:cw_inst_caller ; DH = the slot, 0xFF = the kernel itself
    push ds                     ; DS is restored below whichever arm runs
    mov ax, ds                  ; the fallback: our own segment
    cmp dh, 0xFF
    je .nseg
    mov bl, dh
    xor bh, bh
    mov cl, 5                   ; inst_idx's arithmetic, 32-byte records -
    shl bx, cl                  ; inline because instance.inc is .text and
    mov ax, [bx+inst_tab+I_SPTR] ; this is .cold (SPEC.md 2.6)
.nseg:
    mov si, di                  ; DS:SI -> the caller's name, ES:DI -> ours
    mov di, ld_lname
    mov cx, LD_NAMELEN
    mov ds, ax
    cld
.ncp:
    lodsb
    stosb
    or al, al
    jz .named
    loop .ncp
    mov byte [es:di], 0         ; twelve characters and no terminator: end it
.named:
    pop ds                      ; ...and DS is the kernel's again

    call ld_alloc               ; steps 4 and 5: the record and the region
    jc .fail                    ; AL = LD_ENOMEM

    ; --- step 6, without a disk: the image, copied into the region ----------
    ; [ld_img] and not the file length: ld_check_hdr has just proved them
    ; equal, parts being refused above, and it is the value step 7 zeroes from.
    ; NO DISK-SWAP RE-CHECK - the bytes came from memory the caller owns and
    ; ld_check_hdr read those very bytes, so there is nothing that could have
    ; been swapped underneath them.
    ; **EVERY KERNEL WORD IS READ WHILE DS STILL NAMES THE KERNEL**, and the
    ; `mov si` below is the load-bearing one. It used to sit UNDER the
    ; `mov ds`, so it read [ld_msrc] out of the CALLER'S segment - a kernel
    ; .bss offset, in a package's image - and the copy then ran from
    ; caller:<whatever is there> instead of caller:<the image>. It assembles
    ; cleanly, it passes ld_check_hdr (which read the RIGHT bytes, before
    ; this), and what it produces is a region full of something else whose
    ; PKG_DISP is not a dispatcher: step 8's far call lands at CS=0000
    ; IP=0003, in the interrupt vector table.
    ;
    ; It was INTERMITTENT, which is what made it expensive. `ld_msrc`'s offset
    ; is past the end of a small package's image and bss, so the stray read
    ; lands in whatever the HEAP has next to the caller's region - and the
    ; same fixture wedged or worked according to whether a Disk window was
    ; open, because a listing claim is what sits there. Six runs differing
    ; only in that; the answer was two instructions in the wrong order.
    mov es, [ld_base]           ; ES:DI = the region at offset 0
    xor di, di
    mov cx, [ld_img]
    mov si, [ld_msrc]           ; ...the source OFFSET, first and through the
                                ; kernel's own DS
    push ds
    mov ds, [ld_msrc+2]         ; ...and only now the source SEGMENT, which is
    cld                         ; the last thing this block may read
    rep movsb
    pop ds
    call ld_start               ; 7, 8 and 9 - and ES is the region, which is
.bad_out:                       ; what that routine's step 7 wants. It answers
    ret                         ; in AL and frees the region itself when the
                                ; entry proc aborts
.bad:
    mov al, LD_EBAD
.fail:
    ret                         ; AL ALONE, and the CF is not this routine's
                                ; answer: ld_start's exits end in cw_gfx_unlock
                                ; and an `or`, so the flag arriving here means
                                ; nothing at all. The far entry below decides
                                ; on the CODE, which every arm sets

; -----------------------------------------------------------------------------
; ...and the answer, which ld_start cannot give: it is the DISK path's body
; too, and that one reports through [ld_status] rather than through a flag.
; -----------------------------------------------------------------------------
ldf_ld_pkg_run:
    call ld_pkg_run_x
    or al, al
    jnz .fail                   ; every LD_* but LD_OK, ld_start's own
    xor ax, ax                  ; LD_EABORT included: AX = 0 and CF = 0 on the
    retf                        ; way out, and `xor` is what clears the flag
.fail:
    stc
    retf                        ; retf touches no flags, so the answer stands

; -----------------------------------------------------------------------------
section .bss

ld_msrc:    resw 2              ; OSAPI_PKG_RUN's source image, offset then
                                ; segment (SPEC.md 21.5): banked because
                                ; ld_alloc's chain runs mem_compact and a
                                ; package's relocation procs, and no register
                                ; survives that by contract
ld_pending: resb 1              ; 0 = none, else directory index + 1
                                ; (set by files.inc, consumed by ui.inc).
                                ; A BYTE: the index is bounded by
                                ; [dsk_nmax], which is DSK_NENT for a FAT
                                ; volume and DSK_VENT for a driver-backed
                                ; one, so the stored value is 1..64. The
                                ; guard is on the larger of the two - and it
                                ; has to be, because dskwin.inc's DSK_VENT is
                                ; twice DSK_NENT and it is the one that binds
%if DSK_NENT > 254 || DSK_VENT > 254
  %error "[ld_pending] is a BYTE holding a directory index + 1, and \
[dsk_nmax] has outgrown it (kernel/dskwin.inc)"
%endif
ld_pwin:    resw 1              ; -> the state block of the file-manager
                                ; window that posted it, 0 = none. The index
                                ; above is meaningless anywhere else, and a
                                ; BACKGROUND window's listing is a real one it
                                ; is entitled to act on (SPEC.md 22.1)
ld_status:  resb 1              ; LD_* result of the last loader_run
ld_painted: resb 1              ; 1 = ld_run_body already did the full pass
                                ; (its wm_show), so loader_run must not.
                                ; ADJACENT TO ld_status ON PURPOSE - step 9
                                ; writes the pair as one word
%if (ld_painted - ld_status) != 1
  %error "ld_status and ld_painted are written as ONE WORD by ld_run_body_x \
step 9 - little-endian, status low"
%endif
ld_said:    resb 1              ; 1 = the aborting entry proc's own toast is
                                ; up and ld_say_status must not talk over it
                                ; (set at .abort, consumed by the next
                                ; ld_say_status - SPEC.md 20.12.4)
ld_clus:    resw 1              ; loader_run scratch: the file's first cluster
ld_fsz:     resw 2              ; file size, BYTES, in 32 bits: a package
                                ; that carries parts is bigger than its image
                                ; and may be bigger than a segment (SPEC.md
                                ; 20.12). ld_check_hdr is what bounds it, and
                                ; which test it applies is the flags byte's
                                ; to say
ld_img:     resw 1              ; header: image size
ld_bss:     resw 1              ; header: bss size
ld_ent:     resw 1              ; header: image-relative entry
ld_need:    resw 1              ; allocated region size (512-multiple)
ld_lname:   resb 13             ; the launching file's display name, NUL
                                ; terminated (SPEC.md 21 step 1b). Thirteen
                                ; bytes for the whole machine, because step 9
                                ; hands the package a pointer and the package
                                ; is the one that remembers
ld_rec:     resw 1              ; the reserved instance record

section .text
; The package's entry far pointer, DECLARED rather than assembled. Step 8
; used to write both halves on every launch, and both are already known:
; the offset is PKG_DISP for the life of the machine (SPEC.md 20.1), and the
; segment is the region base step 5 has just stored. So ld_base IS the
; segment half - driver.inc:721's drv_fptr/drv_fseg pair is the same idiom
; and this is one step further along it.
;
; .text and not .bss because the offset half has a non-zero resting value
; (SPEC.md 2.5); .text here carries mutable data on purpose, as kernel.asm
; 6345's own comment records for dsk_vtab, vid_w and three others. Both
; sections are KERNEL_SEG, so every reference is byte-identical.
ld_fp:      dw PKG_DISP         ; the offset half - a CONSTANT
ld_base:    dw 0                ; ...and the segment half IS the region base
%if (ld_base - ld_fp) != 2
  %error "ld_base must be the segment half of ld_fp: step 8's `call far \
[es:ld_fp]` reads FOUR bytes and the second word is this one"
%endif

section .cold                   ; far entries (SPEC.md 2.6): the resident
                                ; thunks in kernel.asm far-call these, and
                                ; these near-call the bodies above - so every
                                ; routine here keeps its ordinary near `ret`
ldf_ld_run_name:        call ld_run_name_x
                    retf

section .text
