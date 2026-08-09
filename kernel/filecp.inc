; =============================================================================
; filecp.inc - the file manager's clipboard: Cut, Copy, Paste (SPEC.md 22.3)
;
; Prefix fcp_. UI-task context only, gfx lock held by the caller - every
; entry point here is reached from a menu body, a context-menu body, a key
; handler or a drop, and all four already hold it (SPEC.md 22). It is disk
; I/O under the lock, which is the trade SPEC.md 22 already accepted for
; Refresh and for navigation.
;
; Three things about this module are load-bearing.
;
; ONE: the clipboard is (drive, folder, name, type) and NOT an index. An
; index means nothing outside the listing it came from, and the whole point
; of the feature is that the listing changes between the Cut and the Paste -
; a different window, a different folder, possibly a different drive. A name
; is a legal dskw_name83 input (SPEC.md 19) and cannot go stale the way an
; index can, which is the same argument fm_onam already won for Rename.
;
; TWO: the recursion has NO call stack. A task stack is 512 bytes
; (SPEC.md 2.1) and a directory tree is attacker-supplied, so the walk keeps
; its state in an explicit FCP_MAXD-deep array of frames and iterates. That
; is dskw_rmtree's discipline; what is different here is that a copy does
; not consume the source as it goes, so each frame has to remember WHERE IT
; WAS - fcp_scan takes an entry ordinal and re-walks the directory to it,
; which costs FAT lookups out of the RAM snapshot plus one sector read.
;
; THREE: the operation SUSPENDS on a question. "Replace this file?" cannot
; be answered from inside a loop that holds the gfx lock, so fcp_step runs
; until it is done, fails, or needs an answer - and in the last case every
; byte of its state is already in .bss, so returning to the event loop and
; coming back through fcp_answer costs nothing. That is the only reason the
; frame array exists in a module that could otherwise have recursed.
;
; What this deliberately does NOT do:
;
; - A same-volume move re-links the directory entry rather than copying the
;   data (fcp_relink, SPEC.md 22.6). Across drives it is still a copy
;   followed by a delete, which is the only thing it can be when the two
;   volumes share no FAT.
; - Nothing about the SIZE of a file: the buffer bounds the CHUNK, not the
;   file (SPEC.md 22.5). dskw_write lost its 64KB ceiling in SPEC.md 18.4.1,
;   but the copy buffer is a heap claim of whatever the machine could spare
;   and a file may still be bigger than it, so a copy stays a truncating
;   create followed by a run of dskw_append calls, each one a whole number
;   of clusters.
;
; **This module is `.cold` (SPEC.md 2.6).** It is the one of the four with no
; resident thunks and no `_x` bodies at all: every caller of an fcp_ routine
; is files.inc, which is cold too, so those calls stayed near. Only the calls
; that leave the cold set go out, through `KERNEL_SEG:cw_*`. Two things
; are on the author rather than the build: CS is NOT KERNEL_SEG here, so
; nothing may take a kernel segment from it, and DATA stays in `.text` - the
; section toggles below are that, and a `db`/`dw` added on the wrong side of
; one is read at the wrong segment with no diagnostic at all.
; =============================================================================

FCP_NONE    equ 0               ; [fcp_op]
FCP_COPY    equ 1
FCP_CUT     equ 2

FCP_MAXD    equ 6               ; nesting depth of one paste. Six levels of
                                ; folder is far past anything a floppy holds
                                ; and the array is 36 bytes; deeper than this
                                ; is reported, never walked.
FCP_FSRC    equ 0               ; frame: word, the source folder's cluster
FCP_FDST    equ 2               ;        word, its mirror in the destination
FCP_FPOS    equ 4               ;        word, the next raw entry ordinal to
FCP_FSIZE   equ 6               ;        examine in the source

FCP_MAXKB   equ 64              ; the copy buffer. It bounds the CHUNK, not
FCP_MINKB   equ 3               ; the file (SPEC.md 22.5), so the minimum is
FCP_MAXCH   equ 65024           ; small - but big is still what we ASK for,
                                ; because every chunk is a seek and a real
                                ; floppy is where that is felt. The chunk
                                ; caps at 127 sectors: 64KB exactly would
                                ; wrap a 16-bit byte count, and this is the
                                ; largest whole-sector run below it.

; The step machine's answers, returned in AL by fcp_step:
FCPS_DONE   equ 0               ; the operation finished
FCPS_ASK    equ 1               ; suspended: fcp_cname needs a verdict
FCPS_ERR    equ 2               ; stopped, [fcp_err] = FERR_*

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - but the DATA is
                                ; not, and stays in .text at every toggle here

; -----------------------------------------------------------------------------
; fcp_arm - put one of the acting window's entries on the clipboard
; in:  AL = FCP_COPY or FCP_CUT, BX = its directory index; [fm_vp] published
;      (fm_layout or fm_vp_set has run); gfx lock held
; out: CF=0 armed, CF=1 refused (nothing selected, or the parent link)
; clobbers: flags
;
; The parent link is refused for fm_arm_sel's reason (SPEC.md 19.5): '..' is
; a way out of the folder, not a thing in it. Everything else - a file, a
; package, a folder - is fair game.
; -----------------------------------------------------------------------------
fcp_arm:

    push ax
    push bx
    push cx
    push si
    push di
    mov [fcp_op], al
    mov di, [fm_vp]
    mov ax, bx                  ; the INDEX is an argument: Cut and Copy pass
    cmp ax, [di+FS_N]           ; FS_SEL, a drag passes the row it began on
    jae .none                   ; (0xFFFF fails this too)
    call fm_stage_name          ; fm_name + [fm_ftype], out of THIS window's
    cmp word [fm_ftype], 3      ; cache (SPEC.md 22.1)
    je .none
    mov ax, [fm_ftype]
    mov [fcp_type], ax
    mov al, [di+FS_DRV]
    mov [fcp_drv], al
    mov ax, [di+FS_CWD]
    mov [fcp_cwd], ax
    mov si, fm_name             ; COPIED, not pointed at: fm_name is rewritten
    mov di, fcp_name            ; for every row the painter draws
    mov cx, 13
    call fcp_ncopy
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.none:
    mov byte [fcp_op], FCP_NONE
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_ncopy - CX bytes from DS:SI to DS:DI (module-internal)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fcp_ncopy:
    push ax
    push cx
    push si
    push di
.b:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .b
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fcp_goto - make (AL = drive, BX = folder cluster) the current directory
; out: CF=1 = the move failed (AX = FERR_*)
; clobbers: AX, flags
;
; TWO quiet paths, and between them they are why a copy is not a sequence of
; mounts. Nothing in this module reads the LISTING - dskw_* resolve names by
; walking [dsk_cwd]'s raw directory sectors and so does fcp_scan - while the
; FAT window and every derived geometry belong to the VOLUME, not the folder.
;
; So a move inside one volume is a word. And crossing to the other drive is
; a dsk_chdir_q (SPEC.md 18.9): the BPB, the FAT window and the cwd, and NOT
; the directory scan, the sort or the per-file icon harvest. That last one is
; what made a copy quadratic - every switch back to the destination
; re-harvested an icon for each file already copied, and the harvest was 23%
; of all sectors read.
;
; The debt is [dsk_lstale], and fcp_step pays it with dsk_relist on every
; path that can return to the event loop.
;
; The cluster is range-checked here rather than trusted, because the quiet
; path skips disk_mount's own .cwd_lost validation.
; -----------------------------------------------------------------------------
fcp_goto:
    push dx
    cmp al, [disk_drive]
    jne .full
    or bx, bx
    jz .quiet                   ; 0 is the root, always legal
    cmp bx, 2
    jb .bad
    cmp bx, [dsk_maxclus]
    ja .bad
.quiet:
    mov [dsk_cwd], bx
    pop dx
    clc
    ret
.bad:
    mov ax, FERR_IO
    pop dx
    stc
    ret
.full:
    mov dl, al
    mov ax, bx
    call KERNEL_SEG:cw_dsk_chdir_q
    jc .ferr
    pop dx
    clc
    ret
.ferr:
    mov ax, FERR_NODISK
    pop dx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_paste - start a paste into (AL = drive, BX = folder) (SPEC.md 22.3)
; in:  the target folder in AL/BX; gfx lock held
; out: AL = FCPS_*, and on FCPS_ASK the caller owes the user a question
; clobbers: AX, flags
;
; The target is an argument, not the acting window's own folder, because
; three surfaces name it differently: Paste means "this window's folder",
; Paste Into means "the folder under the pointer", and a drop means "the
; folder I was released over", possibly in a DIFFERENT window (SPEC.md 22.4).
; One engine, three callers that each know their own answer.
;
; Claims the copy buffer, seeds the frame stack and runs. Everything after
; the first question re-enters through fcp_answer.
; -----------------------------------------------------------------------------
fcp_paste:
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [fcp_op], FCP_NONE
    je .empty
    cmp byte [dsk_mntok], 1
    jne .nodisk

    mov [fcp_ddrv], al
    mov [fcp_dcwd], bx
    mov al, [fcp_drv]           ; the walk reads one word wherever it is
    mov [fcp_sdrv], al

    call fcp_here               ; pasting into the folder it came from is
    jc .empty                   ; nothing to do, and DOING it destroys the
                                ; entry (see the proc below)

    call fcp_selfchk            ; a folder may not be pasted into itself
    jc .loop_err

    call fcp_clspan             ; what a chunk must be a multiple of, from
    jc .nodisk                  ; BOTH volumes - fcp_bufget sizes off it

    call fcp_bufget             ; the one heap claim (SPEC.md 50)
    jc .nomem

    mov byte [fcp_all], 0
    mov word [fcp_depth], 0
    mov byte [fcp_busy], 1
    mov byte [fcp_err], FERR_OK
    mov byte [fcp_started], 0
    call fcp_step
    jmp short .out
.empty:
    mov al, FCPS_DONE           ; nothing on the clipboard is not an error,
    jmp short .out              ; it is a menu item that does nothing
.nodisk:
    mov byte [fcp_err], FERR_NODISK
    jmp short .err
.nomem:
    mov byte [fcp_err], FERR_FULL
    jmp short .err
.loop_err:
    mov byte [fcp_err], FERR_NAME
.err:
    mov al, FCPS_ERR
    call fcp_stop               ; not a bare busy=0: fcp_selfchk and
                                ; fcp_clspan quiet-switch volumes before this
                                ; point, so an exit here can owe the listing
                                ; debt of SPEC.md 18.9 - fcp_stop pays it
                                ; (dsk_relist) and is a no-op for the exits
                                ; that owe nothing. Today's two callers
                                ; full-remount afterwards anyway; this is
                                ; what keeps "every path back to the event
                                ; loop pays" true of the NEXT caller too
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fcp_here - is the paste target the folder the clipboard entry already lives
;            in? (module-internal)
; out: CF=1 = yes; all registers preserved
;
; The one case where doing the work DESTROYS the thing being copied, and it is
; the easiest one in the system to reach: Copy (or Cut) and then Paste without
; navigating anywhere. Source and destination are then the same directory, so
; fcp_xfer opens a read stream on an entry and immediately hands that entry's
; own name to dskw_write as a truncating create - which frees the chain the
; stream is walking. A one-cluster file survives by luck; anything longer comes
; back 0 bytes with FERR_IO, and a Cut then deletes what is left. A folder is
; worse: fcp_mkroot ADOPTS a folder that is already there (SPEC.md 22.3), so
; the walk mirrors a directory into itself and a Cut finishes with dskw_rmtree
; on the destination.
;
; This is deliberately NOT fcp_selfchk's job. That one asks a question about
; the TREE - is the destination inside the source - and only a folder can fail
; it. This asks about one directory and applies to every species.
;
; A no-op rather than an error, which is what fm_dgdrop already decided for the
; same fact arriving as a drag ("the drag went nowhere and there is nothing to
; do"): the clipboard is left armed, so the user can navigate somewhere it does
; mean something and paste there.
; -----------------------------------------------------------------------------
fcp_here:
    push ax
    mov al, [fcp_drv]
    cmp al, [fcp_ddrv]
    jne .no
    mov ax, [fcp_cwd]
    cmp ax, [fcp_dcwd]
    jne .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; fcp_selfchk - refuse a folder pasted into itself or into its own subtree
; out: CF=1 = refuse
; clobbers: flags
;
; Without it the walk creates a copy inside the directory it is reading and
; then finds that copy, forever - a loop bounded only by the disk filling up.
; The test is a walk UP from the destination looking for the source folder's
; cluster, which is the cheapest way to ask "is the source an ancestor of the
; destination" on a file system with no paths. Same drive only; across drives
; the two trees cannot touch.
; -----------------------------------------------------------------------------
fcp_selfchk:
    push ax
    push bx
    push cx
    push dx
    cmp word [fcp_type], 2      ; only a FOLDER can contain anything
    jb .ok
    mov al, [fcp_drv]
    cmp al, [fcp_ddrv]
    jne .ok
    call fcp_srcclus            ; BX = the source folder's own cluster
    jc .ok                      ; unreadable: the copy will fail honestly
    mov dx, bx
    mov al, [fcp_ddrv]
    mov bx, [fcp_dcwd]
    call fcp_goto
    jc .ok
    mov cx, FCP_MAXD + 2        ; the walk up is bounded like every other
.up:
    mov ax, [dsk_cwd]
    cmp ax, dx
    je .bad                     ; the destination IS the source, or is under
    or ax, ax                   ; it: either way the copy would eat itself
    jz .ok                      ; reached the root without meeting it
    call KERNEL_SEG:cw_dsk_dotdot
    jc .ok
    mov [dsk_cwd], ax
    loop .up
.bad:
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.ok:
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; fcp_srcclus - the clipboard entry's own first cluster
; out: CF=0 with BX = it; CF=1 = it could not be looked up
; clobbers: BX (the output), AX, flags
; -----------------------------------------------------------------------------
fcp_srcclus:
    push si
    mov al, [fcp_drv]
    mov bx, [fcp_cwd]
    call fcp_goto
    jc .bad
    mov si, fcp_name
    call dskw_stat_x            ; CF=0 with BX = first cluster
    jc .bad
    pop si
    clc
    ret
.bad:
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_clspan - the cluster size a chunk must be a multiple of (SPEC.md 22.5)
; in:  [fcp_sdrv] and [fcp_ddrv], both set
; out: [fcp_lclus]; CF=1 = a volume would not mount
; clobbers: flags
;
; **BOTH volumes, not the current one.** A chunk has to satisfy two separate
; multiples and they belong to different disks:
;
;   - the SOURCE's, because fcp_rdnext resumes the chain by stepping past the
;     last cluster dsk_read_chain touched. A take that ends mid-cluster skips
;     the rest of it, and the copy silently loses those bytes.
;   - the DESTINATION's, because dskw_append's precondition is that the file
;     it extends is already a whole number of clusters (SPEC.md 22.5).
;
; Both are powers of two, so a multiple of the larger is a multiple of both.
; This used to be `dskw_clbytes` of whichever volume happened to be current
; when the buffer was claimed, which is right only when the two agree - and
; they always did, because the formatter gave every partition 512-byte
; clusters. It gives them 1KB and 2KB now (SPEC.md 52.3), so a 31MB volume
; and a 6MB one disagree and the bug came out: a 116KB file copied from the
; first to the second arrived 64,512 bytes long.
;
; The root is used as the folder because a cluster size belongs to the VOLUME.
; Two quiet switches, once per operation.
; -----------------------------------------------------------------------------
fcp_clspan:
    push ax
    push bx
    push cx
    mov al, [fcp_sdrv]
    xor bx, bx
    call fcp_goto
    jc .err
    call dskw_clbytes
    mov cx, ax
    mov al, [fcp_ddrv]
    xor bx, bx
    call fcp_goto
    jc .err
    call dskw_clbytes
    cmp ax, cx
    jae .have
    mov ax, cx
.have:
    mov [fcp_lclus], ax
    pop cx
    pop bx
    pop ax
    clc
    ret
.err:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_floor - the smallest copy buffer this pair of volumes can be copied with
; out: CX = KB
; clobbers: CX (the output), flags
;
; FCP_MINKB, or ONE CLUSTER if that is bigger - because fcp_chunkset floors
; the chunk to a cluster multiple and a chunk of zero clusters reads nothing
; at all. A 32MB FAT16 volume formatted with 32KB clusters is the case: three
; kilobytes of buffer would divide to a zero chunk and the copy would report
; success having written an empty file. fcp_bufget must hand fcp_chunkset a
; buffer of AT LEAST this many whole KB - which is why its alignment fallback
; refuses rather than shrinking below it (it once shrank, and mem_avail
; landing exactly here turned the shrink into that empty file).
; -----------------------------------------------------------------------------
fcp_floor:
    push ax
    push dx
    mov ax, [fcp_lclus]         ; bytes per cluster, the LARGER of the two
    add ax, 1023                ; volumes (fcp_clspan) - as KB, rounded up
    mov cl, 10
    shr ax, cl
    mov cx, ax
    cmp cx, FCP_MINKB
    jae .out
    mov cx, FCP_MINKB
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fcp_bufget / fcp_bufput - the copy buffer (SPEC.md 50)
; out: CF=1 = the heap cannot fund even fcp_floor's minimum
; clobbers: AX, flags
;
; One claim for the whole operation, taken at the start and released at the
; end - including the end that a question is still waiting on, which is why
; fcp_stop exists and why every exit from this module goes through it.
; -----------------------------------------------------------------------------
fcp_bufget:
    push bx
    push cx
    push dx
    call fcp_floor              ; CX = the smallest buffer that can work here
    call KERNEL_SEG:cw_mem_avail              ; AX = largest free run in KB
    cmp ax, cx
    jb .no
    cmp ax, FCP_MAXKB
    jbe .size
    mov ax, FCP_MAXKB
.size:
    mov [fcp_kb], ax
    mov bx, MEM_K_COPY
    call KERNEL_SEG:cw_mem_claim              ; CF=1 refused; the segment comes back in DX,
    jc .no                      ; NOT in AX - AX still holds the KB asked for
    mov [fcp_raw], dx           ; what mem_free must be handed back...
    mov ax, dx
    add ax, 31                  ; ...and the 512-BYTE aligned base every
    and ax, 0xFFE0              ; int 13h target has to have (SPEC.md 2)
    mov [fcp_seg], ax
    cmp ax, dx                  ; belt: guard 6b (kernel.asm) makes every
    je .whole                   ; claim base KB-aligned, so the rounding
                                ; never moves it and the whole claim is the
                                ; buffer. It used to pay an unconditional KB
                                ; here, which with mem_avail EXACTLY at
                                ; fcp_floor took the buffer below one cluster
                                ; and fcp_chunkset divided to a ZERO chunk -
                                ; fcp_rdnext reads that as "file finished",
                                ; so the copy wrote an empty destination,
                                ; reported success, and a Cut then deleted
                                ; the source
    dec word [fcp_kb]           ; braces: if a future allocator ever hands
    mov ax, [fcp_kb]            ; back an unaligned base, the KB the
    cmp ax, cx                  ; alignment eats must not take the buffer
    jae .whole                  ; below fcp_floor's one-cluster minimum (CX
    call fcp_bufput             ; still holds it) - refusing outright is the
    jmp short .no               ; honest answer, not a silent truncation
.whole:
    call fcp_chunkset
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    mov word [fcp_seg], 0
    mov word [fcp_raw], 0
    pop dx
    pop cx
    pop bx
    stc
    ret

fcp_bufput:
    push bx
    push dx
    mov dx, [fcp_raw]           ; the claim, not the aligned view of it
    or dx, dx
    jz .out
    mov bx, MEM_K_COPY          ; mem_free takes the owner word too, and
    call KERNEL_SEG:cw_mem_free ; refuses a base that is not this owner's
    mov word [fcp_raw], 0
    mov word [fcp_seg], 0
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fcp_stop - end the operation, however it ended
; in:  AL = the FCPS_* to report
; out: AL unchanged
; clobbers: flags
; -----------------------------------------------------------------------------
fcp_stop:
    push ax
    call fcp_bufput
    mov byte [fcp_busy], 0
    mov byte [dskw_batch], 0    ; the deferred remount is spent by the caller
    call KERNEL_SEG:cw_dsk_relist ; the volume switches were quiet (SPEC.md
                                ; 18.9), so the global snapshot may be this
                                ; operation's debt - and the world is about to
                                ; repaint. AFTER dskw_batch, which gates the
                                ; remount it tail-calls; a no-op when nothing
                                ; is owed
    cmp byte [fcp_op], FCP_CUT  ; a Cut is spent by its paste; a Copy stays
    jne .keep                   ; on the clipboard, so it can be pasted again
    mov byte [fcp_op], FCP_NONE
.keep:
    pop ax
    ret

; -----------------------------------------------------------------------------
; fcp_answer - resume a suspended paste (SPEC.md 22.3)
; in:  AL = 0 stop / 1 replace this one / 2 replace all
; out: AL = FCPS_*
; clobbers: AX, flags
; -----------------------------------------------------------------------------
fcp_answer:
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [fcp_busy], 0
    je .idle
    or al, al
    jz .stop
    cmp al, 2
    jne .yes
    mov byte [fcp_all], 1
.yes:
    mov byte [fcp_ovw], 1       ; the pending file may be replaced
    call fcp_step
    jmp short .out
.stop:
    mov al, FCPS_DONE           ; a stop is not a failure: what was already
    call fcp_stop               ; copied stays, and the line says so
    jmp short .out
.idle:
    mov al, FCPS_DONE
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fcp_step - run the operation until it finishes, fails or must ask
; out: AL = FCPS_*
; clobbers: AX, flags (called with everything else already saved)
;
; Re-entrant across a question by construction: it reads its whole state out
; of .bss and writes it back before returning, so the answer path calls it
; again and nothing needs to be threaded through the event loop.
; -----------------------------------------------------------------------------
fcp_step:
    mov byte [dskw_batch], 1    ; one remount for the whole operation, spent
                                ; by the caller (SPEC.md 18.4 rule 3)
    cmp byte [fcp_started], 0
    jne .walk

    ; --- the first step: the clipboard entry itself ---
    mov byte [fcp_started], 1
    cmp byte [fcp_op], FCP_CUT  ; a same-volume move needs no data I/O at
    jne .copyit                 ; all (SPEC.md 22.6) - try that first, and
    mov al, [fcp_drv]           ; fall through to the copy engine when it
    cmp al, [fcp_ddrv]          ; declines
    jne .copyit
    call fcp_relink             ; CF=0 done; CF=1 with AX=0 not attempted;
    jnc .finish2                ; CF=1 with AX = FERR_* past the point of
    or ax, ax                   ; no return
    jnz .failax
.copyit:
    cmp word [fcp_type], 2
    jae .tree

    call fcp_file1              ; a plain file: one copy and we are done
    jc .fail
    cmp byte [fcp_pend], 0
    jne .ask
    jmp .finish

.tree:
    call fcp_mkroot             ; create (or adopt) the folder in the target
    jc .fail
    mov word [fcp_depth], 0
    call fcp_push               ; BX = the new folder's cluster
    jc .fail

.walk:
    cmp byte [fcp_pend], 0      ; an answered question: finish that file
    je .next
    call fcp_file2
    jc .fail
    cmp byte [fcp_pend], 0
    jne .ask
.next:
    cmp word [fcp_depth], 0
    je .finish
    call fcp_frame              ; BX -> the top frame
    call fcp_scan               ; CF=1 = this directory is exhausted
    jc .updir
    cmp word [fcp_ctype], 2     ; a subdirectory: mirror it and descend
    jne .plain
    call fcp_mksub
    jc .fail
    call fcp_push
    jc .fail
    jmp short .walk
.plain:
    call fcp_file1
    jc .fail
    cmp byte [fcp_pend], 0
    jne .ask
    jmp short .walk
.updir:
    or ax, ax                   ; fcp_scan reports a read error in AX
    jnz .failax
    dec word [fcp_depth]
    jmp short .walk

.finish:
    cmp byte [fcp_op], FCP_CUT  ; a Cut removes the source, and only once
    jne .ok                     ; every byte of it has arrived
    call fcp_srcdel
    jc .fail
.ok:
.finish2:
    mov al, FCPS_DONE
    call fcp_stop
    ret
.ask:
    mov byte [dskw_batch], 0    ; the pause is a trip through the event loop,
                                ; where anything else that writes must behave
                                ; normally; fcp_step raises it again on the
                                ; way back in
    call KERNEL_SEG:cw_dsk_relist ; ...and where the listing must be this
                                ; volume's again (SPEC.md 18.9), for the same
                                ; reason and at the same moment
    mov al, FCPS_ASK            ; the buffer and the frames stay claimed:
    ret                         ; this is a pause, not an end
.failax:
    mov [fcp_err], al
    jmp short .failed
.fail:
    mov [fcp_err], al
.failed:
    mov al, FCPS_ERR
    call fcp_stop
    ret

; -----------------------------------------------------------------------------
; fcp_relink - move an entry inside ONE volume by moving its directory entry
; out: CF=0 = done; CF=1 with AX = 0 = not attempted (the caller falls back
;      to the copy engine); CF=1 with AX = FERR_* = failed past the point of
;      no return. Preserves everything else.
;
; A same-volume move needs no data I/O at all (SPEC.md 22.6): the chain is
; already where it belongs and only the name of it changes hands. Three
; sector writes, against a read and a write per cluster - a folder of
; packages moves in less time than the copy path took to read one of them.
;
; It DECLINES rather than competes. Another drive, a name the destination
; already holds, a full destination directory, a source it cannot read: AX =
; 0 and the engine that knows how to ask about overwrites takes it. That is
; what keeps this an optimisation rather than a second set of semantics.
;
; Two things make it cheap. dskw_raw still holds the SOURCE's thirty-two
; bytes when the destination's lookup fails - dskw_stat bails inside
; dskw_find, before dskw_ent_load could overwrite them - so the entry that
; lands in the destination carries the real 32-bit size AND the original
; timestamps, which building a fresh one through dskw_commit would not.
; And [dskw_zapnext] is consumed by dskw_ent_store, not by dskw_commit, so
; taking the slot that IS the end-of-directory marker is handled by the same
; routine that writes the entry.
;
; The order is binding: the destination entry is written BEFORE the source is
; unnamed. A crash between the two leaves the chain named from two
; directories - visible, and the user can delete one. The other order loses
; the file.
; -----------------------------------------------------------------------------
fcp_relink:
    push bx
    push cx
    push dx
    push si
    push di

    mov al, [fcp_drv]           ; the source entry, raw
    mov bx, [fcp_cwd]
    call fcp_goto
    jc .no
    mov si, fcp_name
    call dskw_stat_x            ; dskw_raw = its 32 bytes, BX = first cluster
    jc .no
    mov [fcp_rlclus], bx

    mov al, [fcp_ddrv]          ; the destination must not hold that name
    mov bx, [fcp_dcwd]
    call fcp_goto
    jc .no
    mov si, fcp_name
    call dskw_stat_x
    jnc .no                     ; it does: the copy path knows how to ask
    cmp ax, FERR_NOENT
    jne .no
    cmp word [dskw_fsec], 0xFFFF
    je .no                      ; no reusable slot: FERR_DIRFULL is the copy
                                ; engine's to report, with its own message

    mov ax, [dskw_fsec]         ; the slot dskw_find spotted on its scan
    mov [dskw_dsec], ax
    mov ax, [dskw_foff]
    mov [dskw_doff], ax
    mov al, [dskw_fend]         ; taking the 0x00 terminator means the next
    mov [dskw_zapnext], al      ; entry must be zeroed to remain one
    call dskw_ent_store
    jc .no                      ; nothing written yet: still declinable

    mov al, [fcp_drv]           ; ...and ONLY now unname the source
    mov bx, [fcp_cwd]
    call fcp_goto
    jc .half
    mov si, fcp_name
    call fcp_rlzap
    jc .half

    cmp word [fcp_type], 2      ; a folder's own '..' has to follow it
    jb .done
    call fcp_rlup               ; best effort: the move has happened either
.done:                          ; way, and a stale '..' is a wrong parent
    pop di                      ; link, not a lost file
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.half:
    mov ax, FERR_IO             ; past the point of no return: the entry is
    jmp short .out              ; in the destination and the source still
.no:                            ; names it. Reported, never retried.
    xor ax, ax                  ; not attempted, nothing written
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_rlzap - unname an entry WITHOUT freeing its chain (module-internal)
; in:  SI = its NUL 8.3 name, its directory current
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX, flags
;
; The one delete in this system that leaves the clusters alone, because they
; are not free: the entry fcp_relink just wrote in the destination owns them
; now. dskw_delete would free them and take the moved file with it.
; -----------------------------------------------------------------------------
fcp_rlzap:
    call dskw_stat_x            ; locates it: dskw_dsec/doff, and dskw_raw
    jc .out
    mov byte [dskw_raw], 0xE5   ; SPEC.md 19's deleted marker
    mov byte [dskw_zapnext], 0
    call dskw_ent_store
.out:
    ret

; -----------------------------------------------------------------------------
; fcp_rlup - point a moved folder's own '..' at its new parent
; out: nothing (all registers preserved); a failure is left alone
;
; Entry 1 of a subdirectory is '..' by spec, which is the same fact
; dsk_dotdot rests on (SPEC.md 19.2) - and it is checked here rather than
; assumed, because everything read off the disk is hostile. Without this the
; moved folder still lists and still opens, but going UP out of it lands in
; the folder it used to live in.
; -----------------------------------------------------------------------------
fcp_rlup:
    push ax
    push bx
    push cx
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [fcp_rlclus]
    call KERNEL_SEG:cw_dsk_clus2lba ; AX = the folder's first sector
    jc .out
    push ax
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_read
    pop ax
    jc .out
    cmp byte [es:dsk_secbuf+32], '.'
    jne .out
    cmp byte [es:dsk_secbuf+33], '.'
    jne .out
    mov bx, [fcp_dcwd]
    mov [es:dsk_secbuf+32+DSK_R_CLUS], bx
    push ax
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_write
    pop ax
.out:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fcp_frame / fcp_push - the frame stack (module-internal)
; fcp_frame: out BX -> the top frame (depth >= 1 assumed)
; fcp_push:  in BX = the new destination folder's cluster, [fcp_cclus] its
;            source; out CF=1 = FCP_MAXD exceeded (AX = FERR_DIRFULL - the
;            truthful shape of "this tree is deeper than we walk")
; -----------------------------------------------------------------------------
fcp_frame:
    push ax
    push dx
    mov ax, [fcp_depth]
    dec ax
    mov bx, FCP_FSIZE
    mul bx                      ; DX:AX, so DX is saved above
    add ax, fcp_stack
    mov bx, ax
    pop dx
    pop ax
    ret

fcp_push:
    push ax
    push bx
    push dx
    cmp word [fcp_depth], FCP_MAXD
    jae .deep
    mov ax, [fcp_depth]
    mov dx, FCP_FSIZE
    mul dx
    add ax, fcp_stack
    xchg ax, bx                 ; BX -> the new frame, AX = its destination
    mov [bx+FCP_FDST], ax
    mov ax, [fcp_csrc]
    mov [bx+FCP_FSRC], ax
    mov word [bx+FCP_FPOS], 0
    inc word [fcp_depth]
    pop dx
    pop bx
    pop ax
    clc
    ret
.deep:
    pop dx
    pop bx
    pop ax
    mov ax, FERR_DIRFULL
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_scan - the next usable entry of the top frame's SOURCE directory
; in:  BX -> the frame
; out: CF=0 with fcp_cname / [fcp_ctype] / [fcp_csrc] / [fcp_clen] set and
;      the frame's ordinal advanced past it; CF=1 with AX = 0 (exhausted) or
;      AX = FERR_IO (unreadable)
; clobbers: AX, flags
;
; The ordinal is a RAW slot number - sector*16 + slot - and re-walking to it
; is what buys the frame array its small size. A copy does not modify the
; source directory, so the ordinal stays valid across everything that happens
; between two calls: the scan of another directory, a write into the
; destination, and a trip through the event loop while a question is
; answered.
;
; The species filter is the LISTING's (SPEC.md 19), not the disk's, so what
; you see in the window is what gets copied - a hidden or system file is not
; silently carried along, and neither is a long-name fragment.
; -----------------------------------------------------------------------------
fcp_scan:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, bx                  ; DI -> the frame, held throughout
    mov ax, LOW_SEG
    mov es, ax
    mov al, [fcp_sdrv]
    mov bx, [di+FCP_FSRC]
    call fcp_goto
    jc .ioerr
    mov ax, [di+FCP_FSRC]
    call KERNEL_SEG:cw_dsk_dirw_start
    mov ax, [di+FCP_FPOS]       ; the ordinal splits into a sector and a slot
    mov cl, 4
    shr ax, cl
    mov [fcp_ssec], ax
    mov ax, [di+FCP_FPOS]
    and ax, 15
    mov [fcp_sslot], ax
.skipsec:
    cmp word [fcp_ssec], 0      ; dsk_dirw_next only computes LBAs - skipping
    je .readsec                 ; whole sectors reads nothing
    call KERNEL_SEG:cw_dsk_dirw_next
    jc .end
    dec word [fcp_ssec]
    jmp short .skipsec
.readsec:
    call KERNEL_SEG:cw_dsk_dirw_next ; AX = the sector holding the ordinal
    jc .end
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_read
    jc .ioerr
    mov si, dsk_secbuf
    mov ax, [fcp_sslot]
    mov cl, 5
    shl ax, cl                  ; slot * 32
    add si, ax
    mov cx, 16
    sub cx, [fcp_sslot]         ; slots left in this sector, always >= 1
.entry:
    mov al, [es:si]
    test al, al
    jz .end                     ; 0x00: end of directory, per spec
    inc word [di+FCP_FPOS]      ; consumed, whatever it turns out to be
    cmp al, 0xE5
    je .next
    cmp al, '.'
    je .next                    ; the on-disk dot links (SPEC.md 19)
    cmp al, 0x20
    je .next
    mov ah, [es:si+DSK_R_ATTR]
    mov al, ah
    and al, 0x3F
    cmp al, 0x0F
    je .next                    ; a long-name fragment
    test ah, 0x08
    jnz .next                   ; the volume label
    test ah, 0x06
    jnz .next                   ; hidden or system, exactly as the listing
    call KERNEL_SEG:cw_dsk_synth              ; ES:SI -> dsk_ent (SPEC.md 19): the display
                                ; name, the type word, the cluster, the size
    push si
    push di
    mov si, dsk_ent
    mov di, fcp_cname           ; copied out: dsk_ent is scratch every caller
    mov cx, 13                  ; below rewrites
    call fcp_ncopy
    pop di
    pop si
    mov ax, [dsk_ent+16]
    mov [fcp_ctype], ax
    mov ax, [dsk_ent+18]
    mov [fcp_csrc], ax
    mov ax, [dsk_ent+20]
    mov [fcp_clen], ax
    mov ax, [dsk_ent+22]
    mov [fcp_clenh], ax
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.next:
    add si, 32
    loop .entry
    mov word [fcp_sslot], 0     ; this sector is spent; the ordinal has kept
    jmp .readsec                ; up with it, so the next one starts at 0
.end:
    xor ax, ax
    jmp short .bad
.ioerr:
    mov ax, FERR_IO
.bad:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_mkroot - create (or adopt) the pasted folder in the destination
; out: CF=0 with BX = its first cluster and [fcp_csrc] = the source's;
;      CF=1 with AX = FERR_*
; clobbers: AX, BX, flags
;
; "A folder that already exists is not an overwrite" (SPEC.md 22.3): if the
; name is there and IS a folder, it is adopted and the recursion continues
; into it. If it is there and is a FILE, that is a genuine collision a mkdir
; cannot resolve, and it is reported rather than guessed at.
; -----------------------------------------------------------------------------
fcp_mkroot:
    push cx
    push si
    call fcp_srcclus            ; BX = the source folder's own cluster
    jc .err
    mov [fcp_csrc], bx
    mov al, [fcp_ddrv]
    mov bx, [fcp_dcwd]
    call fcp_goto
    jc .err
    mov si, fcp_name
    call fcp_ensure
    jc .err
    pop si
    pop cx
    clc
    ret
.err:
    pop si
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_mksub - the same, for the subdirectory fcp_scan just named
; in:  BX -> the top frame
; out: CF=0 with BX = the mirror's first cluster; CF=1 with AX = FERR_*
; clobbers: AX, BX, flags
; -----------------------------------------------------------------------------
fcp_mksub:
    push si
    mov al, [fcp_ddrv]
    push bx
    mov bx, [bx+FCP_FDST]
    call fcp_goto
    pop bx
    jc .err
    mov si, fcp_cname
    call fcp_ensure
    jc .err
    pop si
    clc
    ret
.err:
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_ensure - make SI a folder of the current directory, or adopt it
; in:  SI -> a NUL 8.3 name; the destination directory is current
; out: CF=0 with BX = its first cluster; CF=1 with AX = FERR_*
; clobbers: AX, BX, flags
; -----------------------------------------------------------------------------
fcp_ensure:
    call dskw_stat_x            ; CF=0: it is already there
    jc .make
    test al, DSKW_DIR
    jz .clash                   ; a FILE of that name: mkdir cannot fix this
    clc
    ret
.make:
    cmp ax, FERR_NOENT
    jne .fail                   ; a lookup that failed for another reason
    call dskw_mkdir_x
    jc .fail
    call dskw_stat_x            ; the mount's own answer for where it landed
    jc .fail
    clc
    ret
.clash:
    mov ax, FERR_EXIST
.fail:
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_file1 - copy the pending file, asking first if it would replace one
; in:  for the first step, fcp_name / [fcp_cwd] / [fcp_drv] name the source
;      and [fcp_dcwd] the target; inside the walk, fcp_cname and the top
;      frame do. [fcp_intree] says which.
; out: CF=0 and [fcp_pend] = 1 when a question is owed, 0 when the file is
;      copied; CF=1 with AX = FERR_*
; clobbers: AX, flags
; -----------------------------------------------------------------------------
fcp_file1:
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [fcp_ovw], 0
    call fcp_fnames             ; publish this file's source and destination
    mov al, [fcp_fddrv]         ; does the destination already hold it?
    mov bx, [fcp_fdclus]
    call fcp_goto
    jc .err
    mov si, fcp_fname
    call dskw_stat_x
    jc .free                    ; FERR_NOENT is the ordinary case
    test al, DSKW_DIR
    jnz .clash                  ; a FOLDER of that name: not a replaceable
    cmp byte [fcp_all], 0       ; file, and not something to silently remove
    jne .go                     ; "replace all" was answered earlier
    mov byte [fcp_pend], 1      ; suspend: fcp_cname is what to ask about
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.free:
    cmp ax, FERR_NOENT
    jne .err
.go:
    call fcp_xfer
    jc .err
    mov byte [fcp_pend], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.clash:
    mov ax, FERR_EXIST
.err:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_file2 - the answered half of fcp_file1: the transfer, no question
; out: as fcp_file1
; clobbers: AX, flags
; -----------------------------------------------------------------------------
fcp_file2:
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [fcp_ovw], 0
    je .skip                    ; answered 'no': leave the target alone
    call fcp_xfer
    jc .err
.skip:
    mov byte [fcp_pend], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.err:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_fnames - publish the pending file's source and destination
; out: fcp_fname = its name, [fcp_fsdrv]/[fcp_fsclus] the source directory,
;      [fcp_fddrv]/[fcp_fdclus] the destination. All registers preserved.
;
; The first step and the walk name a file differently - the clipboard entry
; itself versus whatever fcp_scan just found - and resolving that here means
; fcp_file1, fcp_file2 and the question all read one set of words.
; -----------------------------------------------------------------------------
fcp_fnames:
    push ax
    push bx
    push cx
    push si
    push di
    mov al, [fcp_ddrv]
    mov [fcp_fddrv], al
    cmp word [fcp_depth], 0
    jne .intree
    mov si, fcp_name            ; the clipboard entry itself
    mov al, [fcp_drv]
    mov [fcp_fsdrv], al
    mov ax, [fcp_cwd]
    mov [fcp_fsclus], ax
    mov ax, [fcp_dcwd]
    mov [fcp_fdclus], ax
    jmp short .name
.intree:
    mov si, fcp_cname
    mov al, [fcp_sdrv]
    mov [fcp_fsdrv], al
    call fcp_frame              ; BX -> the top frame
    mov ax, [bx+FCP_FSRC]
    mov [fcp_fsclus], ax
    mov ax, [bx+FCP_FDST]
    mov [fcp_fdclus], ax
.name:
    mov di, fcp_fname
    mov cx, 13
    call fcp_ncopy
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fcp_rdopen - begin a streaming read of SI in the CURRENT directory
; out: CF=0 with the chain and the size latched; CF=1 with AX = FERR_*
; clobbers: AX, flags
; -----------------------------------------------------------------------------
fcp_rdopen:
    push bx
    push cx
    push dx
    call dskw_stat_x            ; BX = first cluster, DX:CX = size
    jc .out
    mov [fcp_rclus], bx
    mov [fcp_rsz], cx
    mov [fcp_rszh], dx
    clc
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fcp_rdnext - read the next chunk of that file into the copy buffer
; in:  the SOURCE directory is current
; out: CF=0 with AX = bytes read, 0 = the file is finished; CF=1 = FERR_IO
; clobbers: AX (the output), flags
;
; ONE dsk_read_chain call per chunk, not one disk_read per cluster. That
; walker already has the run coalescer (SPEC.md 18) - contiguous clusters
; accumulate into a single (lba, count) transfer - and a freshly written file
; is contiguous, so a 63KB chunk off a 512-byte-cluster volume went from 126
; transfers to one. It cost nothing but making the walker resumable:
; [dsk_chain_end] names the last cluster it touched and one dsk_next_clus
; steps past it.
;
; The chunk is a cluster multiple (fcp_chunkset), so every take but the last
; ends exactly on a cluster boundary and the resume point is exact. The last
; take is the file's tail and there is nothing to resume to.
;
; Reading STOPS at the file's byte count now rather than at the end of the
; cluster holding it, because a sector-driven walker can express that and a
; cluster-driven one could not. The trailing slack was harmless to read and
; is simply not read any more.
;
; **A chain that ends before the size says it should is now an error**, where
; the cluster-at-a-time loop returned short and let the caller read that as
; "finished" - which silently truncated the copy. dsk_read_chain calls it
; corruption, the loader has always treated it that way (SPEC.md 21), and a
; copy that cannot read its source should say so.
;
; A read walk rather than a dskw_ entry point because nothing here writes:
; the FAT window is already resident, so stepping the chain is a lookup and
; the only I/O is the data. The WRITE side is where the new primitive had to
; go (dskw_append), and that is where the commit discipline lives.
; -----------------------------------------------------------------------------
fcp_rdnext:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [fcp_rgot], 0
    mov es, [fcp_seg]
    mov ax, [fcp_rsz]           ; take = min(what is left, one chunk)
    cmp word [fcp_rszh], 0
    jne .full                   ; more than 64KB left: a whole chunk
    cmp ax, [fcp_chunk]
    jb .have
.full:
    mov ax, [fcp_chunk]
.have:
    or ax, ax
    jz .done                    ; the file is finished: 0 bytes says so
    mov [fcp_rgot], ax
    add ax, 511                 ; sectors = ceil(take / 512). take is at most
    mov cl, 9                   ; FCP_MAXCH, so the +511 cannot carry out
    shr ax, cl
    mov dx, ax
    mov ax, [fcp_rclus]
    cmp ax, 2
    jb .io
    cmp ax, [dsk_maxclus]
    ja .io
    xor bx, bx
    call KERNEL_SEG:cw_dsk_read_chain ; AX = first cluster, DX = sectors, ES:BX
    jc .io
    mov cx, [fcp_rgot]
    sub [fcp_rsz], cx
    sbb word [fcp_rszh], 0
    mov ax, [dsk_chain_end]     ; resume past the last cluster it touched
    call KERNEL_SEG:cw_dsk_next_clus
    jc .stop
    mov [fcp_rclus], ax
    jmp short .done
.stop:
    mov word [fcp_rclus], 0     ; the chain ended exactly here, which for a
.done:                          ; final chunk is what should happen
    mov ax, [fcp_rgot]
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.io:
    mov ax, FERR_IO
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_chunkset - the chunk size: the buffer, rounded DOWN to whole clusters
; out: [fcp_chunk]; all registers preserved
;
; A cluster multiple is dskw_append's precondition (SPEC.md 22.5): every
; append but the last therefore lands on a cluster boundary, so none of them
; has to fill a partial sector inside an existing chain.
; -----------------------------------------------------------------------------
fcp_chunkset:
    push ax
    push cx
    push dx
    mov ax, [fcp_kb]
    cmp ax, FCP_MAXKB
    jb .kb
    mov ax, FCP_MAXCH
    jmp short .round
.kb:
    mov cl, 10
    shl ax, cl
.round:
    mov cx, [fcp_lclus]         ; the span BOTH volumes need (fcp_clspan),
                                ; never just the current one's
    xor dx, dx
    div cx
    mul cx                      ; floor(bytes / clusterbytes) * clusterbytes
    mov [fcp_chunk], ax         ; NEVER zero: fcp_bufget's floor is a whole
                                ; cluster, so the divide above yields at least
                                ; one. A zero chunk would read no bytes, which
                                ; fcp_xfer cannot tell from "finished", and the
                                ; copy would truncate in silence
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fcp_xfer - move one file's bytes, source directory to destination
; out: CF=0, or CF=1 with AX = FERR_* (FERR_BIG when it does not fit the
;      buffer - see the header note about dskw_write's 16-bit length)
; clobbers: AX, flags
;
; TWO VOLUME SWITCHES for a file that fits the buffer (SPEC.md 22.5), which is
; every app on the disk. It used to be five, and the two that went were both
; bookkeeping rather than work:
;
;   - the destination was CREATED EMPTY and then appended to, so the first
;     chunk cost a switch back to the source to fetch what we could have
;     fetched before leaving it. Reading chunk one while the source is still
;     current, and creating the destination WITH it, removes that switch and
;     an empty file's directory write and FAT flush with it.
;   - the loop discovered the file was finished by switching to the source and
;     asking for a chunk that was not there. [fcp_rsz] already knew, so the
;     test moved to the top of the loop where it costs nothing.
;
; The replace, the slot hunt and the old chain's release still all happen in
; dskw_write exactly as before - it is the same call, with a length.
; -----------------------------------------------------------------------------
fcp_xfer:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [fcp_fsdrv]         ; open the source stream AND take its first
    mov bx, [fcp_fsclus]        ; chunk: we are here, and the alternative is
    call fcp_goto               ; coming back for it
    jc .err
    mov si, fcp_fname
    call fcp_rdopen
    jc .err
    call fcp_rdnext             ; AX = bytes; 0 = the file is empty, which is
    jc .err                     ; a real file and creates a real empty one
    mov [fcp_flen], ax

    mov al, [fcp_fddrv]         ; create (or truncate) the destination, with
    mov bx, [fcp_fdclus]        ; its first chunk already in hand
    call fcp_goto
    jc .err
    mov es, [fcp_seg]
    xor bx, bx
    mov cx, [fcp_flen]
    xor dx, dx                  ; DX:CX = the chunk (SPEC.md 18.4.1)
    mov si, fcp_fname
    call dskw_write_x
    jc .err

.chunk:
    mov ax, [fcp_rsz]           ; the file itself says whether there is more,
    or ax, [fcp_rszh]           ; so finishing costs no I/O at all
    jz .fin
    mov al, [fcp_fsdrv]
    mov bx, [fcp_fsclus]
    call fcp_goto
    jc .err
    call fcp_rdnext             ; AX = bytes; 0 cannot happen here, but a
    jc .err                     ; chain that ended early would say it
    or ax, ax
    jz .fin
    mov [fcp_flen], ax
    mov al, [fcp_fddrv]
    mov bx, [fcp_fdclus]
    call fcp_goto
    jc .err
    mov es, [fcp_seg]
    xor bx, bx
    mov cx, [fcp_flen]
    mov si, fcp_fname
    call dskw_append_x
    jc .err
    jmp .chunk
.fin:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.err:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_srcdel - a Cut's second half: remove the source, once the copy is whole
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX, flags
; -----------------------------------------------------------------------------
fcp_srcdel:
    push bx
    push si
    mov al, [fcp_drv]
    mov bx, [fcp_cwd]
    call fcp_goto
    jc .err
    mov si, fcp_name
    cmp word [fcp_type], 2
    jae .tree
    call dskw_delete_x
    jc .err
    jmp short .ok
.tree:
    call dskw_rmtree            ; the folder AND everything under it (18.6)
    jc .err
.ok:
    pop si
    pop bx
    clc
    ret
.err:
    pop si
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
section .bss

fcp_op:     resb 1              ; FCP_NONE / FCP_COPY / FCP_CUT
fcp_drv:    resb 1              ; the clipboard entry's drive...
fcp_cwd:    resw 1              ; ...folder...
fcp_type:   resw 1              ; ...and SPEC.md 19 type word
fcp_name:   resb 13             ; ...and 8.3 display name, COPIED

fcp_busy:   resb 1              ; 1 = an operation is running or suspended
fcp_started: resb 1             ; 1 = fcp_step is past its first step
fcp_all:    resb 1              ; 1 = 'replace all' was answered
fcp_pend:   resb 1              ; 1 = a file is waiting on a verdict
fcp_ovw:    resb 1              ; that verdict: 1 = replace it
fcp_err:    resb 1              ; FERR_* of whatever stopped us
fcp_ddrv:   resb 1              ; the paste target's drive...
fcp_sdrv:   resb 1              ; ...and the source tree's (they are the
                                ; clipboard's, held here so the walk reads
                                ; one word wherever it is)
fcp_dcwd:   resw 1              ; the paste target's folder

fcp_raw:    resw 1              ; the copy buffer as mem_claim gave it...
fcp_seg:    resw 1              ; ...and rounded up to a 512-byte boundary
fcp_kb:     resw 1              ; its size in KB...
fcp_lclus:  resw 1              ; bytes per cluster, the larger of the two
                                ; volumes in this operation (fcp_clspan)
fcp_chunk:  resw 1              ; ...and the bytes one pass moves

fcp_rlclus: resw 1              ; fcp_relink: the moved entry's first cluster

fcp_rclus:  resw 1              ; the streaming read: the next cluster...
fcp_rsz:    resw 1              ; ...the bytes of the file still to come,
fcp_rszh:   resw 1              ; low and high...
fcp_rgot:   resw 1              ; ...and what this chunk has taken so far

fcp_depth:  resw 1              ; frames in use, 0 = not inside a tree
fcp_stack:  resb FCP_MAXD * FCP_FSIZE

fcp_ssec:   resw 1              ; fcp_scan: sectors still to skip...
fcp_sslot:  resw 1              ; ...and the slot to resume at

fcp_cname:  resb 13             ; fcp_scan's answer: name...
fcp_cpad:   resb 1              ; (keeps the words below even-aligned)
fcp_ctype:  resw 1              ; ...SPEC.md 19 type word...
fcp_csrc:   resw 1              ; ...first cluster...
fcp_clen:   resw 1              ; ...and size, low word...
fcp_clenh:  resw 1              ; ...and high

fcp_fname:  resb 13             ; the pending file: name...
fcp_fsdrv:  resb 1              ; ...source drive...
fcp_fddrv:  resb 1              ; ...destination drive...
fcp_fpad:   resb 1
fcp_fsclus: resw 1              ; ...source folder...
fcp_fdclus: resw 1              ; ...destination folder...
fcp_flen:   resw 1              ; ...and the byte count dskw_read reported

section .text
