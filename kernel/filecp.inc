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
; TWO: the recursion has NO call stack. A task stack is 384 bytes
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

; FF - a frame field, at BX, wherever fcp_stack lives on this build
; (docs/plans/MODULE-SELFCONTAIN-PLAN.md 3). On kern_small the descent stack is
; in FILECP.DRV's OWN bss - the tail of the claim mod_need already rounded up
; to whole KB - so it is reached through CS like any other image data; on
; kern_big this file is resident `.cold` and there is no image, so the stack
; stays in `.bss` and BX is DS-relative as it always was.
;
; ONE %define rather than six %ifdefs, and it is the whole of the difference:
; `fcp_faddr` computes the same offset either way, because a label's value is
; the same in both sections.
; There are two, because the frame is reached through BX at one set of sites
; and DI at another - fcp_faddr answers in BX, fcp_scan walks with DI.
%ifdef FCP_MOD
  %define FF(x) [cs:bx+x]
  %define FD(x) [cs:di+x]
%else
  %define FF(x) [bx+x]
  %define FD(x) [di+x]
%endif

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

%ifdef FCP_MOD
FCP_NENT    equ 4               ; entries FILECP.DRV publishes (SPEC.md 2.8):
                                ; arm, paste, answer and the door. NOT
                                ; fcp_ncopy, which is `equ dsk_ncopy` and
                                ; resident, and NOT fcp_goto, which CLONE.DRV
                                ; needs mid-clone and which therefore stays
                                ; resident too
FCPE_ARM    equ 0               ; ...and their slots, in header order
FCPE_PASTE  equ 1
FCPE_ANSWER equ 2
FCPE_DOOR   equ 3               ; OSAPI_FILE_COPY's, both verbs (SPEC.md 22.24)
%endif

; The clipboard and the OPERATION are two records of one shape, and the
; second is a COPY of the first taken as a paste starts (fcp_paste) - or
; filled from registers by the published door (fcp_door, SPEC.md 22.24), which
; is what lets a package copy a file without disturbing what the user has on
; the clipboard. dsk_ncopy moves the whole record in one call, so the layout
; is pinned here and asserted below the two definitions.
FCP_CBSZ    equ 19              ; op (1), drive (1), folder (2), type (2),
                                ; name (13)

; =============================================================================
; TWO SHAPES, ONE SOURCE (docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md 9.2 wave 1)
; =============================================================================
; kern_big keeps every body below in `.cold`, resident, near-called from
; files.inc's own cold code - unchanged, to the byte. kern_small emits the same
; bodies into `.modp`, which tools/os88mod.py cuts out into FILECP.DRV, and
; mod_need reads back into a heap claim when the user copies something.
;
; Five macros carry the whole difference, so no body below is written twice and
; no %ifdef appears inside one:
;
;   FCPBODY   the label a body carries. `fcp_paste` resident; `fcp_paste_x` in
;             the image, with a resident stub of the public name in front of it
;             that loads the module and far-calls in.
;   FCPX      a call OUT of the image. Near on big. On small the image sits at
;             a heap address and everything it calls is at COLD_SEG, so it is
;             `call fcpx_go` with the target in the word after it - EXTD.DRV's
;             EXK device (extmod.inc's exk_go), one segment along: fcpx_go
;             builds the far frame into COLD_SEG:<name>, an assembly-time
;             constant, and the body's near `ret` lands on cold_kretf. FIVE
;             BYTES A SITE, the same as the `call COLD_SEG:xf_<name>` it
;             replaced, and NO resident thunk per routine: that was 35 of them,
;             140 bytes of kern_small's `.cold`, for routines only the image
;             ever called this way.
;   (a TAIL jump out is written as an explicit %ifdef at each of its four
;    sites, NOT as a fifth macro. It cannot stay a `jmp` on small - the
;    routine it lands in ends in a NEAR `ret` and the image was entered by a
;    FAR call, so the return would pop two bytes of a four-byte frame - but
;    tools/stkbalance.py reads SOURCE and does not expand macros, so a macro
;    that ENDS a path is a path it walks straight through into the next
;    routine's pops. The same goes for the shared epilogues below.)
;   (FCPXF, the resident `xf_<name>: call <name> / retf` entry, is gone: the
;    last one, xf_fm_stage_name, was FDLG.DRV's, and fdlg.inc reaches that
;    body through its own fdx_go now.)
;   ...and the shared register epilogues, same shape and same reason: a module
;   may not `jmp kretc_cx`, so the image carries its own (fcp_x5).
; =============================================================================
%ifdef FCP_MOD
  %macro FCPBODY 1
%1 %+ _x:
  %endmacro
  %macro FCPX 1
    call fcpx_go
    dw %1
  %endmacro
%else
  %macro FCPBODY 1
%1:
  %endmacro
  %macro FCPX 1
    call %1
  %endmacro
%endif

; THE ARMS ARE THIS WAY ROUND ON PURPOSE, and it is tools/os88ovlchk.py's
; requirement rather than taste: that gate reads SOURCE and cannot evaluate a
; %ifdef, so it files everything after the LAST `section` directive it sees.
; With `.cold` written second it filed this file's bodies as cold while the
; image header and the epilogues stayed `.modp`, and reported every jump between
; them as a segment crossing. `.modp` last makes its model agree with the
; kern_small build, which is the one the check exists to protect.
%ifndef FCP_MOD
section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - but the DATA is
                                ; not, and stays in .text at every toggle here
%else
section .modp                   ; THE ON-DEMAND IMAGE (SPEC.md 2.8). The header
modp_hdr:                       ; is at offset 0 because vstart is 0 and
    dw 0x384F                   ; mod_check reads it through ES
    db MOD_VER
    db MOD_FCP
    dw BUILD_NUM                ; the commit...
    dw MOD_STAMP                ; ...and this build's LAYOUT
    dw modp_end                 ; MOD_H_IMG - vstart 0, so the end label IS it
    dw FCP_NENT
    dw modp_e_arm               ; the FAR entries, never the bodies: an entry
    dw modp_e_paste             ; is reached by `call far`, so it has to end in
    dw modp_e_answer            ; a retf, and every body below keeps the near
    dw modp_e_door              ; `ret` cold code has always had
                                ; FOUR, AND `fcp_ncopy` IS NOT ONE OF THEM.
                                ; files.inc's other public name here is not a
                                ; body in this file at all - the `fcp_ncopy equ
                                ; dsk_ncopy` below makes it disk.inc's routine
                                ; under a second name, and that stays RESIDENT.
                                ; So it needs no entry, no stub and no load: a
                                ; caller of it reads no disk.
                                ; The last one IS here: OSAPI_FILE_COPY's door
                                ; (SPEC.md 22.24), the one entry in this image
                                ; reached from outside the file manager

modp_e_arm:     call fcp_arm_x
            retf
modp_e_paste:   call fcp_paste_x
            retf
modp_e_answer:  call fcp_answer_x
            retf
modp_e_door:    call fcp_door_x
            retf

; -----------------------------------------------------------------------------
; fcpx_go - the far call FCPX names, built by hand (extmod.inc's exk_go, with
; COLD_SEG for KERNEL_SEG): the call left the address of `dw target` on the
; stack, and this turns that one word into a far return to just past it with
; cold_kretf's near return on top, then RETFs into COLD_SEG:target - whose
; near `ret` lands on cold_kretf and its `retf` back here. Every register and
; the flags are preserved both ways (no ADD and no INC: LEA), so a body sees
; exactly what the site handed it - BP included, which drv_fs_call takes as
; its verb.
;
; It is the cost of a COLD path: ~300 cycles a hop more than the four-byte
; resident thunk it replaced, ~60 us on a 4.77MHz 8088. The busiest sites are
; the copy loop's, a handful per CHUNK (fcp_rdnext: dsk_bytes_secs_x,
; dsk_read_chain_x, dsk_next_clus_x), each chunk being whole `int 13h` calls
; at ~400 ms apiece. The frame at the body's entry is the six bytes the
; thunk's was; the peak inside this routine is 14 above the call.
; -----------------------------------------------------------------------------
fcpx_go:                        ; ovlchk: builds its own far frame
    push ax                     ; STKBALANCE-OK: a far frame built by hand, its
    push ax                     ; four slots spent by the RETF below and by the
    push ax                     ; target's own return through cold_kretf
    push ax
    push bp
    mov bp, sp                  ; [bp+2] offset, [bp+4] segment, [bp+6] the
    push bx                     ; near return, [bp+8] the far return's offset
    mov bx, [bp+10]             ; and [bp+10] the call's own return, which
    mov [bp+10], cs             ; becomes the far return's CS
    mov word [bp+6], cold_kretf
    mov word [bp+4], COLD_SEG
    lea bx, [bx+2]              ; past the `dw`
    mov [bp+8], bx
    mov bx, [cs:bx-2]           ; ...which names the target
    mov [bp+2], bx
    pop bx
    pop bp
    retf
%endif

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
FCPBODY fcp_arm

    push ax
    push bx
    push cx
    push si
    push di
    mov [fcp_cbop], al
    mov di, [fm_vp]
    mov ax, bx                  ; the INDEX is an argument: Cut and Copy pass
    cmp ax, [di+FS_N]           ; FS_SEL, a drag passes the row it began on
    jae .none                   ; (0xFFFF fails this too)
    FCPX fm_stage_name          ; fm_name + [fm_ftype], out of THIS window's
    mov ax, [fm_ftype]          ; cache (SPEC.md 22.1)
    cmp ax, 3
    je .none
    mov [fcp_cbtype], ax
    mov al, [di+FS_DRV]
    mov [fcp_cbdrv], al
    mov ax, [di+FS_CWD]
    mov [fcp_cbcwd], ax
    mov si, fm_name             ; COPIED, not pointed at: fm_name is rewritten
    mov di, fcp_cbname          ; for every row the painter draws
    call fcp_n13
    clc
    jmp short .out
.none:
    mov byte [fcp_cbop], FCP_NONE
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; fcp_ncopy - CX bytes from DS:SI to DS:DI. disk.inc's dsk_ncopy is this
; body to the instruction, and both are `.cold` = COLD_SEG.
fcp_ncopy   equ dsk_ncopy

; fcp_unbatch - end the write batch and pay SPEC.md 18.9's listing debt, in
; that order: dskw_batch gates the remount dskw_sync would otherwise defer.
; clobbers: nothing but flags (dskw_sync_x preserves every register) - which
; fcp_stop and fcp_step's .ask both rely on.
;
; dskw_sync_x AND NOT dsk_relist_x, and the difference is who pays. dsk_relist
; is an UNCONDITIONAL remount whenever a quiet switch left [dsk_lstale] set -
; real disk I/O, the scan, the sort and an icon read per file - whose only
; product is the GLOBAL listing, which no Disk window is drawn from
; (SPEC.md 22.8's account: a Disk window paints from its VIEW cache, fmv_mark
; has just marked it, and fm_paste_res's fmv_reload_all is the coherence pass
; every paste already pays). dskw_sync_x settles the same debt honestly: it
; marks every window showing this folder and publishes the
; empty-listing-plus-debt that dsk_relist collects on
; the next read. So the file manager sees exactly what it always saw, and a
; PACKAGE calling the door (SPEC.md 22.24) - which has no Disk window at all -
; no longer buys a remount per call for a listing nothing reads.
fcp_unbatch:
    mov byte [dskw_batch], 0
    FCPX dskw_sync_x
    ret

; fcp_n13 - ...and the length THREE of its callers pass: an 8.3 display name
; (SPEC.md 19.1).  Leaves CX = 13 on return, exactly as `mov cx, 13` +
; call did - dsk_ncopy restores CX to its OWN entry value - and no caller
; reads CX after the copy.
fcp_n13:
    mov cx, 13
    FCPX dsk_ncopy
    ret

; fcp_x5 - the epilogue of the three bodies that bank BX..DI: fcp_paste,
; fcp_answer and fcp_door. Below them nothing banks those five at all - that
; is fcp_run's free set - so this is the module's one save ladder.
;
; Reached by `jmp`, never `call`, so SP here is exactly what it was at the
; routine's own epilogue and the `ret` finds that routine's caller.  Each
; site's flag is already set above its jump.
;
; The order ENDS at bx, so it is not a suffix of any other pop ladder in the
; kernel and the two cannot be mistaken for one another.
fcp_x5:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
; Copies the clipboard into the operation record, spends a Cut, and runs
; (fcp_run). Everything after the first question re-enters through
; fcp_answer.
;
; THE CLIPBOARD IS COPIED, NOT USED IN PLACE, and the copy is what makes
; OSAPI_FILE_COPY (SPEC.md 22.24) a door rather than a second engine: the
; published body fills the same record from registers and enters fcp_run
; below, so a package's copy and the user's paste are one path - and a
; package copying a file while the user has something on the clipboard
; disturbs nothing, because the operation never reads the clipboard at all.
; -----------------------------------------------------------------------------
FCPBODY fcp_paste
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, [fcp_cbop]          ; (AH: AL is the target drive, fcp_run's)
    test ah, ah                 ; FCP_NONE
    je .empty
    mov si, fcp_cbop            ; the clipboard record, whole, into the
    mov di, fcp_op              ; operation's (FCP_CBSZ, asserted below)
    mov cx, FCP_CBSZ            ; ...and dsk_ncopy hands CX back, so CH = 0
    FCPX dsk_ncopy              ; below: FCP_NONE, and [fcp_all]'s 'ask'
    cmp ah, FCP_CUT
    jne .keep                   ; a Cut is spent by its paste - however that
    mov [fcp_cbop], ch          ; paste ends - and a Copy stays, so it can
.keep:                          ; be pasted again. It USED to be spent in
                                ; fcp_stop, at the end; here, at the start, is
                                ; the same moment as far as anything that
                                ; reads the clipboard can tell, and it keeps
                                ; fcp_stop - which the door's operations end
                                ; through too - from spending a clipboard
                                ; that was not this operation's
    mov [fcp_all], ch
    call fcp_run
    jmp short .out
%if FCP_NONE != 0 || FCP_CBSZ > 255
  %error "fcp_paste writes FCP_NONE and a zero [fcp_all] out of CH = FCP_CBSZ >> 8"
%endif
.empty:
    mov al, FCPS_DONE           ; nothing on the clipboard is not an error,
.out:                           ; it is a menu item that does nothing
    jmp fcp_x5

; -----------------------------------------------------------------------------
; fcp_run - run the operation the record describes (module-internal)
; in:  AL = the destination drive, BX = its folder; fcp_op / [fcp_drv] /
;      [fcp_cwd] / [fcp_type] / fcp_name set; [fcp_all] set
;      fcp_run2: the same with [fcp_ddrv] / [fcp_dcwd] already stored
; out: AL = FCPS_*, and [fcp_err] = FERR_OK on FCPS_DONE, FERR_EXIST when
;      the destination was the folder the entry is already in (nothing to do,
;      nothing written - the paste treats that as done and the door reports
;      it), or the FERR_* behind FCPS_ERR
; clobbers: AX, BX, CX, DX, SI, DI, flags - FCP_RUN'S FREE SET
;
; **EVERYTHING REACHED FROM HERE MAY CLOBBER BX, CX, DX, SI AND DI.** fcp_run
; and fcp_step are entered from exactly three bodies - fcp_paste, fcp_answer
; and fcp_door - and all three bank BX..DI on entry and leave through
; fcp_x5, so nothing below them has a caller that reads one of the five
; across a call. The routines under here therefore save only what a SIBLING
; needs from them: fcp_scan keeps BX (the frame, which fcp_step hands on to
; fcp_mksub), fcp_rdnext keeps DI (fcp_xfer's WRITE_SEQ token on kern_big)
; and the rest of its set, and whatever writes ES puts it back, because the
; three bodies do not bank ES. A routine that says "fcp_run's free set" in its
; clobbers line is leaning on this paragraph; one that may be reached from
; anywhere else (fcp_here, fcp_n13, fcp_unbatch) keeps its own contract.
;
; The copy buffer is NOT claimed here. It used to be, ahead of the first step,
; and a same-volume Cut - which fcp_relink finishes with three directory
; writes and no data I/O - paid two root switches and a heap claim for a
; buffer it never touched. fcp_step claims it at .copyit, the first moment
; the copy engine is actually going to run; what that buys the published door
; is that a MOVE on one volume needs no free heap at all, which is the case
; the DOS shell reaches it from inside an fsx bracket (SPEC.md 96.30.6).
; -----------------------------------------------------------------------------
fcp_run:
    mov [fcp_ddrv], al
    mov [fcp_dcwd], bx
fcp_run2:
    cmp byte [dsk_mntok], 1
    jne .nodisk

    call fcp_here               ; pasting into the folder it came from is
    jc .none                    ; nothing to do, and DOING it destroys the
                                ; entry (see the proc below)

    call fcp_selfchk            ; a folder may not be pasted into itself
    jc .loop_err

%if FERR_OK != 0
  %error "fcp_run2 stores FERR_OK out of a zeroed AX"
%endif
    xor ax, ax                  ; (AL is fcp_step's to set on the way back)
    mov [fcp_depth], ax
    mov [fcp_err], al           ; FERR_OK
    mov [fcp_started], al
    inc ax
    mov [fcp_busy], al
    jmp fcp_step                ; a tail call: AL = FCPS_* is its answer
.none:
    mov byte [fcp_err], FERR_EXIST  ; the truthful name for "the destination
    mov al, FCPS_DONE           ; already holds this": the door reports it and
    ret                         ; the paste, which reads AL alone, is done
.nodisk:
    mov al, FERR_NODISK         ; AL is the courier: it is dead here, and .err
    jmp short .seterr           ; reloads it with FCPS_ERR one line down
.loop_err:
    mov al, FERR_NAME
.seterr:
    mov [fcp_err], al
    mov al, FCPS_ERR
    jmp fcp_stop                ; not a bare busy=0: fcp_selfchk quiet-switches
                                ; volumes before this point, so an exit here
                                ; can owe the listing debt of SPEC.md 18.9 -
                                ; fcp_stop pays it and is a no-op for the
                                ; exits that owe nothing. Today's callers
                                ; full-remount afterwards anyway; this is
                                ; what keeps "every path back to the event
                                ; loop pays" true of the NEXT caller too

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
    push ax                     ; `xor` CLEARS CF, which is the "not here"
    mov ax, [fcp_cwd]           ; answer - so the two arms share one tail and
    xor ax, [fcp_dcwd]          ; the folder is tested first only because it
    jnz .no                     ; is the word.  The predicate is an AND, so
    mov al, [fcp_drv]           ; testing it first is the same question.
    xor al, [fcp_ddrv]          ; AH is left dirty by the second xor and is
    jnz .no                     ; never read - the `pop` restores AX whole
    stc
.no:
    pop ax
    ret

; -----------------------------------------------------------------------------
; fcp_selfchk - refuse a folder pasted into itself or into its own subtree
; out: CF=1 = refuse
; clobbers: AX, BX, CX, DX, flags (fcp_run's free set - see fcp_run)
;
; Without it the walk creates a copy inside the directory it is reading and
; then finds that copy, forever - a loop bounded only by the disk filling up.
; The test is a walk UP from the destination looking for the source folder's
; cluster, which is the cheapest way to ask "is the source an ancestor of the
; destination" on a file system with no paths. Same drive only; across drives
; the two trees cannot touch.
; -----------------------------------------------------------------------------
fcp_selfchk:
    cmp word [fcp_type], 2      ; only a FOLDER can contain anything
    jb .ok
    mov al, [fcp_drv]
    cmp al, [fcp_ddrv]
    jne .ok
    call fcp_srcclus            ; BX = the source folder's own cluster
    jc .ok                      ; unreadable: the copy will fail honestly
    mov dx, bx
    FCPX fcp_godst
    jc .ok
    mov cx, FCP_MAXD + 2        ; the walk up is bounded like every other
.up:
    mov ax, [dsk_cwd]
    cmp ax, dx
    je .bad                     ; the destination IS the source, or is under
    or ax, ax                   ; it: either way the copy would eat itself
    jz .ok                      ; reached the root without meeting it
    FCPX dsk_dotdot_x
    jc .ok
    mov [dsk_cwd], ax
    loop .up
.bad:
    stc
    ret
.ok:
    clc
    ret

; -----------------------------------------------------------------------------
; fcp_srcclus - the clipboard entry's own first cluster
; out: CF=0 with BX = it; CF=1 = it could not be looked up
; clobbers: BX (the output), AX, CX, DX, SI, flags (fcp_run's free set)
; -----------------------------------------------------------------------------
fcp_srcclus:
    FCPX fcp_gosrc
    jc .bad
    mov si, fcp_name
    FCPX dskw_stat_x            ; CF=0 with BX = first cluster, and that flag
.bad:                           ; IS this routine's answer - no second test
    ret

; -----------------------------------------------------------------------------
; fcp_claim - the copy buffer, for fcp_step's .copyit (SPEC.md 22.5, 50)
; in:  [fcp_drv] and [fcp_ddrv], both set; [fcp_seg] = 0 (no buffer held)
; out: CF=0 with [fcp_seg] the buffer and [fcp_chunk] the bytes one pass
;      moves; CF=1 with AL = FERR_NODISK (a volume would not mount) or
;      FERR_FULL (no run of heap big enough for one cluster) - in AL, because
;      fcp_step's .fail is where it lands
; clobbers: AX, BX, CX, DX, SI, flags (fcp_run's free set - see fcp_run)
;
; Four steps, and they were four routines (fcp_clspan, fcp_floor, fcp_bufget,
; fcp_chunkset) with one caller each; one body is what lets the cluster span
; ride in SI from the first step to the last instead of in .bss.
;
; 1. THE CLUSTER SPAN, from BOTH volumes and not the current one. A chunk has
;    to satisfy two separate multiples and they belong to different disks:
;
;    - the SOURCE's, because fcp_rdnext resumes the chain by stepping past the
;      last cluster dsk_read_chain touched. A take that ends mid-cluster skips
;      the rest of it, and the copy silently loses those bytes.
;    - the DESTINATION's, because dskw_append's precondition is that the file
;      it extends is already a whole number of clusters (SPEC.md 22.5).
;
;    Both are powers of two, so a multiple of the larger is a multiple of
;    both. This used to be `dskw_clbytes` of whichever volume happened to be
;    current when the buffer was claimed, which is right only when the two
;    agree - and they always did, because the formatter gave every partition
;    512-byte clusters. It gives them 1KB and 2KB now (SPEC.md 52.3), so a
;    31MB volume and a 6MB one disagree and the bug came out: a 116KB file
;    copied from the first to the second arrived 64,512 bytes long. The root
;    is used as the folder because a cluster size belongs to the VOLUME: two
;    quiet switches, once per operation.
;
; 2. THE FLOOR: FCP_MINKB, or ONE CLUSTER if that is bigger - because step 4
;    floors the chunk to a cluster multiple and a chunk of zero clusters reads
;    nothing at all. A 32MB FAT16 volume formatted with 32KB clusters is the
;    case: three kilobytes of buffer would divide to a zero chunk and the copy
;    would report success having written an empty file.
;
; 3. THE CLAIM, one for the whole operation, released by fcp_stop - including
;    the end that a question is still waiting on, which is why every exit
;    from this module goes through it. ITS BASE IS THE BUFFER: every heap
;    claim is 512-byte aligned (kernel.asm guards 6 and 6b - HEAP_SEG % 32,
;    every claim a whole number of KB, a DMA bump onto a 64KB page floor or a
;    whole number of KB below one), which is the same fact the loader stands
;    on when it reads a package image straight into its region. This used to
;    round the base up to 32 paragraphs and keep a second word for what
;    mem_free must be handed, behind a fallback for an unaligned base that
;    nothing could produce.
;
; 4. THE CHUNK: the buffer, rounded DOWN to whole clusters - dskw_append's
;    precondition (SPEC.md 22.5), so every append but the last lands on a
;    cluster boundary. NEVER zero: the floor is a whole cluster, so the
;    divide yields at least one. A zero chunk would read no bytes, which
;    fcp_xfer cannot tell from "finished", and the copy would truncate in
;    silence.
; -----------------------------------------------------------------------------
fcp_claim:
    mov al, [fcp_drv]           ; 1. the span
    xor bx, bx
    FCPX fcp_goto
    jc .nodisk
    FCPX dskw_clbytes
    xchg ax, dx                 ; DX = the source's cluster bytes - in DX
    mov al, [fcp_ddrv]          ; because fcp_goto keeps it on EVERY arm,
    xor bx, bx                  ; where CX, SI and DI are a redirected
    FCPX fcp_goto               ; volume's driver's (SPEC.md 19.2.2's clobber
    jc .nodisk                  ; note)
    FCPX dskw_clbytes
    cmp dx, ax
    jae .have
    xchg ax, dx                 ; DX = the larger
.have:
    mov ax, dx                  ; 2. the floor, in KB, rounded up
    mov si, dx                  ; ...and SI carries the span to step 4
    FCPX mem_bytes_kb_x
    cmp ax, FCP_MINKB
    jae .floor
    mov ax, FCP_MINKB
.floor:
    xchg ax, cx                 ; CX = the smallest buffer that can work here
    FCPX mem_avail_x            ; AX = largest free run in KB
    cmp ax, cx
    jb .full                    ; (CF = 1, the `jb`'s)
    cmp ax, FCP_MAXKB
    jbe .size
    mov ax, FCP_MAXKB
.size:                          ; 3. the claim
    mov bx, MEM_K_COPY
    mov cx, ax                  ; ...THE WHOLE BUFFER INSIDE ONE 64KB DMA PAGE
                                ; (SPEC.md 22.5.1). dsk_runcap splits a run at
                                ; the page (SPEC.md 18.91), so where this
                                ; buffer lands decides how often a chunk costs
                                ; two int 13h instead of one - and that made
                                ; the copy engine's cost depend on what else
                                ; the heap happened to be holding. Aligned, a
                                ; chunk smaller than the buffer never reaches
                                ; the boundary at all
    FCPX mem_claim_dma_x
    jnc .claimed
    FCPX mem_claim_x            ; ...and a heap with no page-aligned run
    jc .full                    ; still copies, exactly as it did before
.claimed:                       ; the segment comes back in DX, and AX still
    mov [fcp_seg], dx           ; holds the KB asked for
    mov cl, 10                  ; 4. the chunk: KB -> bytes. 64KB exactly
    shl ax, cl                  ; wraps to 0 - the one count that does,
    jnz .round                  ; FCP_MAXKB being the cap - and takes the
    mov ax, FCP_MAXCH           ; largest whole-sector run below it instead
.round:
    xor dx, dx
    div si
    mul si                      ; floor(bytes / clusterbytes) * clusterbytes,
    mov [fcp_chunk], ax         ; and CF = 0: the product fits in AX
    ret
.full:
    mov al, FERR_FULL           ; (`mov` writes no flag: CF is still 1)
    ret
.nodisk:
    mov al, FERR_NODISK         ; ...and here, from the `jc`
    ret

; -----------------------------------------------------------------------------
; fcp_stop - end the operation, however it ended
; in:  AL = the FCPS_* to report
; out: AL unchanged
; clobbers: BX, DX, flags (fcp_run's free set - see fcp_run; fcp_answer, its
;      one caller outside it, banks the same five)
;
; The copy buffer goes back first. The word is cleared BEFORE mem_free is
; asked, so it never names a freed block whatever mem_free answers, and a
; zero is what fcp_claim's contract and this routine both read as "none".
; -----------------------------------------------------------------------------
fcp_stop:
    xor dx, dx
    xchg dx, [fcp_seg]          ; the buffer, and the word left 0
    or dx, dx
    jz .nobuf
    mov bx, MEM_K_COPY          ; mem_free takes the owner word too, and
    FCPX mem_free_x             ; refuses a base that is not this owner's.
.nobuf:                         ; It preserves AX, so AL survives it
    mov byte [fcp_busy], 0
    call fcp_unbatch            ; the deferred remount is spent by the caller;
                                ; the volume switches were quiet (SPEC.md
                                ; 18.9), so the global snapshot may be this
                                ; operation's debt - and the world is about to
                                ; repaint. AFTER dskw_batch, which gates the
                                ; sync it tail-calls; a no-op when nothing
                                ; is owed. The Cut used to be spent HERE; it is
                                ; spent where the clipboard is copied
                                ; (fcp_paste), because this routine ends the
                                ; door's operations too and those own no
                                ; clipboard to spend
    ret

; -----------------------------------------------------------------------------
; fcp_answer - resume a suspended paste (SPEC.md 22.3)
; in:  AL = 0 stop / 1 replace this one / 2 replace all
; out: AL = FCPS_*
; clobbers: AX, flags
; -----------------------------------------------------------------------------
FCPBODY fcp_answer
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [fcp_busy], 0
    je .idle
    or al, al
    jz .stop                    ; (AL = 0 is FCPS_DONE already)
    cmp al, 2
    jne .yes
    mov [fcp_all], al           ; both flags are read as zero / NOT zero, so
.yes:                           ; the answer itself is their 'yes'
    mov [fcp_ovw], al           ; the pending file may be replaced
    call fcp_step
    jmp short .out
.idle:
    mov al, FCPS_DONE
    jmp short .out
.stop:                          ; a stop is not a failure: what was already
    call fcp_stop               ; copied stays, and the line says so
.out:
    jmp fcp_x5
%if FCPS_DONE != 0
  %error "fcp_answer's stop reports FCPS_DONE out of the AL = 0 it tested"
%endif

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
    mov al, 1
    mov [dskw_batch], al        ; one remount for the whole operation, spent
                                ; by the caller (SPEC.md 18.4 rule 3)
    FCPX dsk_batch_begin_x  ; ...and one BPB validation per volume
                                ; for as long as the interface stays frozen
                                ; (SPEC.md 18.9.3). One slice is one batch: a
                                ; question or a finish unlocks, which ends it,
                                ; and the next slice opens another. It
                                ; preserves AX, so AL is still 1 here
    xchg al, [fcp_started]      ; test AND set: AL = what it was
    test al, al
    jnz .walk

    ; --- the first step: the clipboard entry itself ---
    cmp byte [fcp_op], FCP_CUT  ; a same-volume move needs no data I/O at
    jne .copyit                 ; all (SPEC.md 22.6) - try that first, and
    mov al, [fcp_drv]           ; fall through to the copy engine when it
    cmp al, [fcp_ddrv]          ; declines
    jne .copyit
    call fcp_relink             ; CF=0 done; CF=1 with AX=0 not attempted;
    jnc .finish2                ; CF=1 with AX = FERR_* past the point of
    or ax, ax                   ; no return - which lands on the `jc .fail`
    stc                         ; below with CF set: .fail is one byte past a
    jnz .jcfail                 ; short jnz's reach from here (size pass 8)
.copyit:
    call fcp_claim              ; THE COPY BUFFER, CLAIMED ONLY NOW - after the
.jcfail:
    jc .fail                    ; re-link has had its chance, and before the
                                ; first thing the copy engine creates
                                ; (fcp_mkroot below), so a heap that cannot
                                ; fund it refuses having written nothing
    cmp word [fcp_type], 2      ; a plain file is the WALK's .plain with
    jb .plain                   ; [fcp_depth] still 0: .walk finds nothing
                                ; pending, .next finds depth 0 and goes to
                                ; .finish - the same destination this arm
                                ; jumped to, by the same route the suspend
                                ; case already took
.tree:
    call fcp_mkroot             ; create (or adopt) the folder in the target
    jc .fail                    ; ([fcp_depth] is 0: fcp_run2 zeroed it)
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
    jnz .fail
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
    jmp fcp_stop                ; a tail call: fcp_step has nothing of its own
                                ; on the stack here, and fcp_stop leaves AL
.ask:
    call fcp_unbatch            ; the pause is a trip through the event loop,
                                ; where anything else that writes must behave
                                ; normally (fcp_step raises the batch again on
                                ; the way back in) and where the listing must
                                ; be this volume's again (SPEC.md 18.9) - the
                                ; same pair, for the same two reasons, as the
                                ; one in fcp_stop
    mov al, FCPS_ASK            ; the buffer and the frames stay claimed:
    ret                         ; this is a pause, not an end
.fail:
    mov [fcp_err], al
    mov al, FCPS_ERR
    jmp fcp_stop                ; ...and the same here

; -----------------------------------------------------------------------------
; fcp_relink - move an entry inside ONE volume by moving its directory entry
; out: CF=0 = done; CF=1 with AX = 0 = not attempted (the caller falls back
;      to the copy engine); CF=1 with AX = FERR_* = failed past the point of
;      no return
; clobbers: AX, BX, CX, DX, SI, DI, flags (fcp_run's free set - see fcp_run)
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
; dskw_find, which is the only thing that writes them - so the entry that
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
    FCPX fcp_gosrc              ; the source entry, raw
    jc .no
    cmp byte [dsk_vkind], DVK_FILE
    je .no                      ; A REDIRECTED VOLUME DECLINES THIS WHOLE
                                ; PATH (SPEC.md 62.9.7). Everything below is
                                ; raw directory SLOTS - dskw_ent_store into
                                ; [dskw_fsec]/[dskw_foff], the 0x00
                                ; terminator, fcp_rlup rewriting the moved
                                ; folder's own on-disk '..' - and there are
                                ; none. It costs nothing to refuse: this is
                                ; a fast path with a fallback, so AX = 0
                                ; "not attempted" sends the caller into the
                                ; copy engine, which is entirely built here.
                                ; The '..' is moot too - FSV_CHDIR answers
                                ; the parent handle, so there is no on-disk
                                ; link to go stale
    mov si, fcp_name            ; ...for ALL THREE lookups below: dskw_stat,
                                ; dskw_take_slot, dskw_ent_store and fcp_goto's
                                ; quiet and dsk_chdir_q arms all keep SI, and
                                ; its FSV arm is out of reach - a redirected
                                ; volume declined above, and both ends are on
                                ; it (fcp_step asked)
    FCPX dskw_stat_x            ; dskw_raw = its 32 bytes, BX = first cluster
    jc .no
    mov [fcp_rlclus], bx

    FCPX fcp_godst              ; the destination must not hold that name
    jc .no
    FCPX dskw_stat_x
    jnc .no                     ; it does: the copy path knows how to ask
    cmp ax, FERR_NOENT
    jne .no
    cmp word [DSKW_FNONE], 0xFFFF   ; (the HIGH word on kern_big, 18.7.5)
    je .no                      ; no reusable slot: FERR_DIRFULL is the copy
                                ; engine's to report, with its own message

    FCPX dskw_take_slot         ; adopt the slot dskw_find spotted - diskw.inc's,
                                ; `.cold` like this, so the call is near
    FCPX dskw_ent_store
    jc .no                      ; nothing written yet: still declinable

    FCPX fcp_gosrc              ; ...and ONLY now unname the source -
    jc .half                    ; WITHOUT freeing its chain, the one delete
    FCPX dskw_stat_x            ; in this system that leaves the clusters
    jc .half                    ; alone: the entry just written in the
    mov byte [dskw_raw], 0xE5   ; destination owns them now, and dskw_delete
    mov byte [dskw_zapnext], 0  ; would take the moved file with it. The
    FCPX dskw_ent_store         ; stat locates it (dskw_dsec/doff and
    jc .half                    ; dskw_raw); 0xE5 is SPEC.md 19's deleted
                                ; marker

    cmp word [fcp_type], 2      ; a folder's own '..' has to follow it
    jb .done
    call fcp_rlup               ; best effort: the move has happened either
.done:                          ; way, and a stale '..' is a wrong parent
    clc                         ; link, not a lost file
    ret
.half:
    mov ax, FERR_IO             ; past the point of no return: the entry is
    stc                         ; in the destination and the source still
    ret                         ; names it. Reported, never retried.
.no:
    xor ax, ax                  ; not attempted, nothing written
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_rlup - point a moved folder's own '..' at its new parent
; out: nothing; a failure is left alone
; clobbers: AX, BX, CX, DX, flags (fcp_run's free set); ES is kept
;
; Entry 1 of a subdirectory is '..' by spec, which is the same fact
; dsk_dotdot rests on (SPEC.md 19.2) - and it is checked here rather than
; assumed, because everything read off the disk is hostile. Without this the
; moved folder still lists and still opens, but going UP out of it lands in
; the folder it used to live in.
; -----------------------------------------------------------------------------
fcp_rlup:
    push es
    push ss                     ; ES = LOW_SEG (SPEC.md 2.1), which is what SS names
    pop es
    mov ax, [fcp_rlclus]
%ifdef OS88_BIGVOL
    FCPX dsk_c2lba_arm_x        ; AX = the folder's first sector, its high
%else                           ; word armed (SPEC.md 18.7.5)
    FCPX dsk_clus2lba_x ; AX = the folder's first sector
%endif
    jc .out
    push ax
    FCPX dsk_rd1_x
    pop ax
    jc .out
    cmp word [es:dsk_secbuf+32], '..'   ; the first TWO bytes of the name,
    jne .out                    ; in one compare
    mov bx, [fcp_dcwd]
    mov [es:dsk_secbuf+32+DSK_R_CLUS], bx
%ifdef OS88_BIGVOL
    FCPX dsk_c2arm_x            ; [dsk_c2hi] is still the folder's: the read
%endif                          ; between spent [dsk_lbahi], not it
    FCPX dsk_wr1_x
.out:
    pop es
    ret

; -----------------------------------------------------------------------------
; fcp_frame / fcp_push - the frame stack (module-internal)
; fcp_frame: out BX -> the top frame (depth >= 1 assumed)
; fcp_push:  in BX = the new destination folder's cluster, [fcp_csrc] its
;            source; out CF=0, or CF=1 = FCP_MAXD exceeded (AX = FERR_DIRFULL -
;            the truthful shape of "this tree is deeper than we walk")
; clobbers: AX, BX, DX, flags - both. Every caller is fcp_step, whose own
;      callers bank BX..DI around it, or fcp_fnames, which banks them itself;
;      none of them reads AX or DX across the call.
;
; fcp_frame FALLS INTO fcp_faddr, the address of frame AX: in AX = the index;
; out BX -> the frame, AX = whatever BX held on the way in, DX = 0 (the high
; half of a product under 64K). The `xchg` is load-bearing and must not be
; tidied into a `mov`: fcp_push arrives with the new folder's cluster in BX
; and needs it back in AX.
; -----------------------------------------------------------------------------
fcp_frame:
    mov ax, [fcp_depth]
    dec ax                      ; the TOP frame is fcp_push's at depth-1
fcp_faddr:
    mov dx, FCP_FSIZE
    mul dx                      ; DX:AX, and DX = 0: the product is tiny
    add ax, fcp_stack           ; (no carry: CF = 0 out of here)
    xchg ax, bx
    ret

fcp_push:
    mov ax, [fcp_depth]
    cmp ax, FCP_MAXD
    jae .deep
    inc word [fcp_depth]
    call fcp_faddr              ; BX -> the new frame, AX = its destination
    mov FF(FCP_FDST), ax
    mov ax, [fcp_csrc]
    mov FF(FCP_FSRC), ax
    mov FF(FCP_FPOS), dx        ; 0, out of the `mul` - and CF = 0 out of
    ret                         ; fcp_faddr's `add`: no `mov` writes a flag
.deep:
    mov ax, FERR_DIRFULL
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_scan - the next usable entry of the top frame's SOURCE directory
; in:  BX -> the frame
; out: CF=0 with fcp_cname / [fcp_ctype] / [fcp_csrc] set and
;      the frame's ordinal advanced past it; CF=1 with AX = 0 (exhausted) or
;      AX = FERR_IO (unreadable)
; clobbers: AX, CX, DX, SI, DI, flags (fcp_run's free set); BX and ES are kept
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
    push bx                     ; ...the frame, which fcp_step hands on to
    push es                     ; fcp_mksub - and ES, for the module's callers
    mov di, bx                  ; DI -> the frame, held throughout
    push ss
    pop es
    mov al, [fcp_drv]
    mov bx, FD(FCP_FSRC)
    FCPX fcp_goto
    jc .fsio
    cmp byte [dsk_vkind], DVK_FILE
    jne .fatwalk

    ; --- a REDIRECTED volume: one FSV_ENUM, BY ORDINAL (SPEC.md 62.9.7) -----
    ; There are no directory sectors to walk, no raw 32 bytes to filter and no
    ; dsk_synth to run - so the driver is asked for the Nth entry of this
    ; folder and answers with the same staged entry dsk_synth would have
    ; built. That is why this joins the FAT path at .stage instead of
    ; repeating its tail: one copy of "what does the engine take from an
    ; entry", not two that can drift.
    mov ax, FD(FCP_FSRC)       ; the folder's handle...
    mov cx, FD(FCP_FPOS)       ; ...and how far into it we already are
    mov dx, ds                  ; DX:BX, not ES:BX (SPEC.md 51.8) - and DS
    mov bx, dsk_ent             ; because dsk_ent is .bss, which is where the
    push bp                     ; shared tail reads it from (ES is LOW_SEG
    mov bp, FSV_ENUM            ; here, for the raw sectors this path has not
    FCPX drv_fs_call            ; got)
    pop bp
    jc .fsdone
    inc word FD(FCP_FPOS)      ; consumed. THIS ordinal is dense where the
    jmp .stage                  ; FAT one counts raw slots, and both are
.fsdone:                        ; opaque to every caller above
    or ax, ax
    jnz .fsio                   ; (near jumps: .end and .ioerr are both out
    jmp .end                    ; of a conditional's short range from here)
.fsio:
    jmp .ioerr

.fatwalk:
    mov ax, FD(FCP_FSRC)
    FCPX dsk_dirw_start_x
    mov ax, FD(FCP_FPOS)       ; the ordinal splits into a sector and a slot,
    mov bx, ax                  ; and both ride in REGISTERS to the end of the
    and bx, 15                  ; walk: BX = the slot to resume at, DX = the
    mov cl, 4                   ; whole sectors to skip. The dsk_dirw_* trio
    shr ax, cl                  ; preserves both, and nothing else in this
    xchg ax, dx                 ; arm writes either until .stage exits
.skipsec:
    dec dx                      ; (at most 4,095, so the sign is the end)
    js .readsec                 ; dsk_dirw_next only computes LBAs - skipping
    FCPX dsk_dirw_next_x        ; whole sectors reads nothing
    jc .end
    jmp short .skipsec
.readsec:
    FCPX dsk_dirw_get_x ; dsk_secbuf = the sector holding the
    jnc .secok                  ; ordinal (SPEC.md 19.2.3)
    jz .end                     ; ZF=1: the chain ended (the thunk's retf
                                ; keeps the flags dsk_dirw_get_x answered)
    jmp .ioerr
.secok:
    mov si, bx
    mov cl, 5
    shl si, cl                  ; slot * 32
    add si, dsk_secbuf
    mov cx, 16
    sub cx, bx                  ; slots left in this sector, always >= 1
.entry:
    mov al, [es:si]
    test al, al
    jz .end                     ; 0x00: end of directory, per spec
    inc word FD(FCP_FPOS)      ; consumed, whatever it turns out to be
    cmp al, 0xE5
    je .next
    cmp al, '.'
    je .next                    ; the on-disk dot links (SPEC.md 19)
    cmp al, 0x20
    je .next
    mov ah, [es:si+DSK_R_ATTR]
    test ah, 0x0E               ; the volume label, or hidden or system,
    jnz .next                   ; exactly as the listing - and a long-name
                                ; fragment, whose attribute 0x0F carries all
                                ; three, which is the listing's reason too
    FCPX dsk_synth_x              ; ES:SI -> dsk_ent (SPEC.md 19): the display
                                ; name, the type word, the cluster, the size
.stage:                         ; ...and FSV_ENUM's answer arrives here too
    mov si, dsk_ent             ; SI and DI are dead from here to the `ret`
    mov di, fcp_cname           ; below - every path out of .stage is this
    call fcp_n13                ; routine's own exit.
                                ; (dsk_ent is scratch every caller rewrites)
    mov ax, [dsk_ent+16]
    mov [fcp_ctype], ax
    mov ax, [dsk_ent+18]
    mov [fcp_csrc], ax
    clc
    jmp short .out
.next:
    add si, 32
    loop .entry
    xor bx, bx                  ; this sector is spent; the ordinal has kept
    jmp .readsec                ; up with it, so the next one starts at 0
.end:
    xor ax, ax
    jmp short .bad
.ioerr:
    mov ax, FERR_IO
.bad:
    stc
.out:
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; fcp_mkroot - create (or adopt) the pasted folder in the destination
; out: CF=0 with BX = its first cluster and [fcp_csrc] = the source's;
;      CF=1 with AX = FERR_*
; clobbers: AX, BX, CX, DX, SI, flags (fcp_run's free set - see fcp_run)
;
; "A folder that already exists is not an overwrite" (SPEC.md 22.3): if the
; name is there and IS a folder, it is adopted and the recursion continues
; into it. If it is there and is a FILE, that is a genuine collision a mkdir
; cannot resolve, and it is reported rather than guessed at.
; -----------------------------------------------------------------------------
fcp_mkroot:
    call fcp_srcclus            ; BX = the source folder's own cluster
    jc .out
    mov [fcp_csrc], bx

    ; --- CAN this source be walked at all? (SPEC.md 62.9.7) ---------------
    ; fcp_srcclus left the SOURCE volume standing, which is the only place
    ; this question can be asked: a redirected source whose driver publishes
    ; no FSV_ENUM answers every fcp_scan with CF=1 and AX=0, and that is
    ; letter for letter what the END of a folder answers - so the walk would
    ; build the top folder, find it empty and report FCPS_DONE over a copy
    ; that moved nothing. Asked HERE and not in fcp_scan because it is one
    ; probe per paste instead of one per entry, and because it comes before
    ; the destination folder is made rather than after.
    cmp byte [dsk_vkind], DVK_FILE
    jne .dest
    push bp
    mov bp, FSV_ENUM
    FCPX drv_fs_has
    pop bp
    jc .noenum
.dest:
    FCPX fcp_godst
    jc .out
    mov si, fcp_name
    jmp short fcp_ensure        ; ...whose CF is this routine's answer
.noenum:
    mov ax, FERR_PROT           ; the same honest refusal dskw_rmbody gives
.out:                           ; for the same absence (SPEC.md 62.10.4.5).
    ret                         ; CF is still set by the `jc` that got here

; -----------------------------------------------------------------------------
; fcp_mksub - the same, for the subdirectory fcp_scan just named
; in:  BX -> the top frame
; out: CF=0 with BX = the mirror's first cluster; CF=1 with AX = FERR_*
; clobbers: AX, BX, CX, DX, SI, flags (fcp_run's free set - see fcp_run)
;
; FALLS INTO fcp_ensure, whose CF is this routine's answer.
; -----------------------------------------------------------------------------
fcp_mksub:
    mov al, [fcp_ddrv]
    mov bx, FF(FCP_FDST)        ; (BX is the output: the frame is not needed
    FCPX fcp_goto               ; again until fcp_step asks for it)
    jnc .go
    ret
.go:
    mov si, fcp_cname

; -----------------------------------------------------------------------------
; fcp_ensure - make SI a folder of the current directory, or adopt it
; in:  SI -> a NUL 8.3 name; the destination directory is current
; out: CF=0 with BX = its first cluster; CF=1 with AX = FERR_*
; clobbers: AX, BX, CX, DX, flags - dskw_stat answers a size in DX:CX
; -----------------------------------------------------------------------------
fcp_ensure:
    FCPX dskw_stat_x            ; CF=0: it is already there
    jc .make
    test al, DSKW_DIR           ; ...and `test` has cleared CF, which is this
    jz .clash                   ; arm's whole answer: a FILE of that name is
    ret                         ; a clash mkdir cannot fix
.make:
    cmp ax, FERR_NOENT
    jne .fail                   ; a lookup that failed for another reason
    FCPX dskw_mkdir_x
    jc .fail
    FCPX dskw_stat_x            ; the mount's own answer for where it landed,
    ret                         ; and its CF is ours. A near call inside
                                ; COLD_SEG on kern_big and a far one on
                                ; kern_small, and either way the `ret` is what
                                ; this routine's near caller unwinds. Legal
                                ; because this routine pushes nothing of its own
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
; clobbers: AX, BX, CX, DX, SI, DI, flags (fcp_run's free set - see fcp_run)
; -----------------------------------------------------------------------------
fcp_file1:
    xor ax, ax
    mov [fcp_ovw], al
    mov [fcp_pend], al          ; no question owed until one is found
    mov [fcp_ovwsz], ax         ; ...and it replaces nothing until proven
    mov [fcp_ovwsz+2], ax       ; otherwise (SPEC.md 22.5.2)
    call fcp_fnames             ; publish this file's source and destination
    FCPX fcp_gofd               ; does the destination already hold it?
    jc .err
    mov si, fcp_fname
    FCPX dskw_stat_x
    jc .free                    ; FERR_NOENT is the ordinary case
    test al, DSKW_DIR
    jnz .clash                  ; a FOLDER of that name: not a replaceable
    mov [fcp_ovwsz], cx         ; a FILE of that name: its clusters come back
    mov [fcp_ovwsz+2], dx       ; when dskw_write replaces it, so fcp_room may
                                ; count them as room. Banked HERE because this
                                ; is the one stat the engine already does -
                                ; asking again at the check would be a second
                                ; directory walk for a number we have held all
                                ; along
    cmp byte [fcp_all], 0       ; file, and not something to silently remove
    jne .go                     ; "replace all" was answered earlier
    inc byte [fcp_pend]         ; suspend: fcp_cname is what to ask about.
    ret                         ; 0 -> 1, and `inc` leaves CF as the `cmp`
                                ; above left it: 0, a compare against 0 never
                                ; borrows
.free:
    cmp ax, FERR_NOENT
    jne .err
.go:
    jmp fcp_xfer                ; ...whose CF and AX are this routine's
.clash:
    mov ax, FERR_EXIST
.err:
    stc
    ret

; -----------------------------------------------------------------------------
; fcp_file2 - the answered half of fcp_file1: the transfer, no question
; out: as fcp_file1, and [fcp_pend] = 0 on EVERY path - a transfer that
;      fails must not leave the question owed, or the next folder paste would
;      find it at .walk and answer it with this operation's stale names
; clobbers: as fcp_file1
; -----------------------------------------------------------------------------
fcp_file2:
    mov byte [fcp_pend], 0
    cmp byte [fcp_ovw], 0       ; answered 'no': leave the target alone
    je .out                     ; (CF = 0: the compare was EQUAL)
    jmp fcp_xfer                ; ...whose CF and AX are this routine's
.out:
    ret

; -----------------------------------------------------------------------------
; fcp_fnames - publish the pending file's source and destination
; out: fcp_fname = its name, [fcp_fsclus] the source directory and
;      [fcp_fdclus] the destination - on the OPERATION's two drives, [fcp_drv]
;      and [fcp_ddrv], at every depth: a tree does not change volume on the
;      way down, so fcp_gofs and fcp_gofd read those directly
; clobbers: AX, BX, CX, DX, SI, DI, flags (fcp_run's free set - see fcp_run)
;
; The first step and the walk name a file differently - the clipboard entry
; itself versus whatever fcp_scan just found - and resolving that here means
; fcp_file1, fcp_file2 and the question all read one set of words.
; -----------------------------------------------------------------------------
fcp_fnames:
    cmp word [fcp_depth], 0
    jne .intree
    mov si, fcp_name            ; the operation's entry itself
    mov ax, [fcp_cwd]
    mov [fcp_fsclus], ax
    mov ax, [fcp_dcwd]
    mov [fcp_fdclus], ax
    jmp short .name
.intree:
    mov si, fcp_cname
    call fcp_frame              ; BX -> the top frame
    mov ax, FF(FCP_FSRC)
    mov [fcp_fsclus], ax
    mov ax, FF(FCP_FDST)
    mov [fcp_fdclus], ax
.name:
    mov di, fcp_fname           ; ...and it lands under the same name at the
    jmp fcp_n13                 ; other end, which is what a copy means here:
                                ; fcp_wprep and fcp_undo read this one word
                                ; for the destination too

; -----------------------------------------------------------------------------
; fcp_rdnext - read the next chunk of that file into the copy buffer
; in:  the SOURCE directory is current
; out: CF=0 with AX = bytes read, 0 = the file is finished; CF=1 = FERR_IO
; clobbers: AX (the output), BX, CX, DX, SI, ES, flags (fcp_run's free set); DI kept
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
    push di                     ; fcp_xfer's WRITE_SEQ token (kern_big), which
                                ; the FSV path below spends on the offset. The
                                ; rest is fcp_run's free set, and ES is
                                ; fcp_xfer's to put back
    mov es, [fcp_seg]
    mov ax, [fcp_rsz]           ; take = min(what is left, one chunk)
    cmp word [fcp_rszh], 0
    jne .full                   ; more than 64KB left: a whole chunk
    cmp ax, [fcp_chunk]
    jb .have
.full:
    mov ax, [fcp_chunk]
.have:
    mov [fcp_rgot], ax          ; stored BEFORE the test, so a finished file
    or ax, ax                   ; leaves the 0 that .done reports
    jz .done                    ; the file is finished: 0 bytes says so
    cmp byte [dsk_vkind], DVK_FILE
    jne .fatread

    ; --- a REDIRECTED volume: one FSV_READAT at the running offset ----------
    ; No chain to step and no [dsk_maxclus] to range-check against - that word
    ; belongs to the last FAT volume mounted. The offset walks instead, which
    ; is also what makes this resumable without [dsk_chain_end].
    push bp
    mov cx, ax                  ; the take is also the capacity
    mov dx, es                  ; DX:BX = the destination (SPEC.md 51.8)
    xor bx, bx
    mov si, [fcp_roff]
    mov di, [fcp_roff+2]
    mov ax, [fcp_rclus]         ; the handle
    mov bp, FSV_READAT
    FCPX drv_fs_call
    pop bp
    jc .jio
    call fcp_took               ; ...which is what leaves CX, so the offset
    add [fcp_roff], cx          ; update goes BELOW it.  The two are
    adc word [fcp_roff+2], 0    ; independent, so the swap is free
    jmp short .done
.jio:
    jmp .io

.fatread:
    mov ax, [fcp_rgot]
    FCPX dsk_bytes_secs_x       ; sectors = ceil(take / 512). take is at most
                                ; FCP_MAXCH, so the +511 cannot carry out
    mov ax, [fcp_rclus]
    cmp ax, 2
    jb .io
    cmp ax, [dsk_maxclus]
    ja .io
    xor bx, bx
    FCPX dsk_read_chain_x ; AX = first cluster, DX = sectors, ES:BX
    jc .io
    call fcp_took
    mov ax, [dsk_chain_end]     ; resume past the last cluster it touched
    FCPX dsk_next_clus_x
    jnc .store
    xor ax, ax                  ; the chain ended exactly here, which for a
.store:                         ; final chunk is what should happen
    mov [fcp_rclus], ax
.done:
    mov ax, [fcp_rgot]
    clc
    jmp short .out
.io:
    mov ax, FERR_IO
    stc
.out:
    pop di
    ret

; fcp_took - the chunk just read is taken off what is left (SPEC.md 22.5)
; out: CX = the take; everything else preserved - and CX is fcp_rdnext's to
;      clobber, which is where both callers are
fcp_took:
    mov cx, [fcp_rgot]
    sub [fcp_rsz], cx
    sbb word [fcp_rszh], 0
    ret

; -----------------------------------------------------------------------------
; fcp_room - will this file fit the destination? (SPEC.md 22.5.2)
; in:  [fcp_need] = the whole file's byte count, [fcp_ovwsz] = the size of the
;      entry it replaces (0 = none); THE DESTINATION VOLUME IS CURRENT
; out: CF = 0 there is room, or we could not find out; CF = 1 it will not fit
; clobbers: flags
;
; **ASKED BEFORE ANYTHING IS CREATED**, which is the whole of it. Without this
; a copy that does not fit runs to the end of the volume and stops, leaving a
; TRUNCATED file whose directory entry states the truncated length as though it
; were the file's own - measured, a 304,552-byte module onto a 264K RAM disk
; lists as `BANANA.MOD 260096` and is not a module any more. Doing the work and
; throwing it away would merely be wasteful; doing the work and leaving
; something that looks like the file is the part that costs the user data.
;
; It costs no volume switch: the destination is current here anyway, because
; the create is the next thing that happens. It is NOT free on a hard disk,
; whose FAT is paged through a nine-sector window - which is why it sets
; [dsk_fcgoal] to the file's sectors round the count, and a partition with
; room stops at the first window (SPEC.md 22.5.2.1). On a floppy the FAT is resident and the count is CPU alone.
;
; **"I DO NOT KNOW" ANSWERS YES.** A volume whose dfree fails - a redirector
; that publishes no FSV_DFREE (SPEC.md 62.9.1) is the real case - falls through
; to the behaviour that stood before this existed. Turning an unknown into a
; refusal would break copies that work today, which is a worse trade than the
; one this is fixing.
;
; It compares BYTES and not clusters, so it is optimistic by up to one cluster
; per file: a file that fits by less than a cluster's rounding still passes here
; and can still fail at the end. That is deliberate - a FALSE REFUSAL is worse
; than a late failure, and the late failure is what fcp_undo below is for. The
; case this catches is the one that matters, a file the volume could not hold
; under any rounding.
; -----------------------------------------------------------------------------
fcp_room:                       ; clobbers AX, BX, DX - its ONE caller reloads
%ifdef OS88_BIGVOL              ; all three between here and the create, so
    mov ax, [fcp_need+1]        ; saving them would be four bytes spent on
    mov dl, [fcp_need+3]        ; nothing. THE WANT GOES IN (22.5.2.1): a hard
    shr dl, 1                   ; disk's count may stop once it has MORE than
    rcr ax, 1                   ; this many CLUSTERS - the file's whole
    mov [dsk_fcgoal], ax        ; sectors, which is at least its clusters
                                ; however big they are. Past 32MB the goal
                                ; wraps and may stop the count early, which
                                ; answers "no room" - and no FAT16 volume
                                ; here has room for such a file
%endif
    FCPX dskw_dfree_x
%ifdef OS88_BIGVOL
    mov word [dsk_fcgoal], 0xFFFF   ; ...and back to "all" (`mov`: CF stays)
%endif
    jc .yes
    add ax, [fcp_ovwsz]         ; CX is untouched: [fcp_flen] is
    adc dx, [fcp_ovwsz+2]       ; already in it and the create needs it
    jc .yes                     ; the sum wrapped: bigger than any file
    cmp dx, [fcp_need+2]
    ja .yes
    jb .no
    cmp ax, [fcp_need]
    jb .no
.yes:
    clc
.no:                            ; (CF = 1: both arrive by `jb`)
    ret

; -----------------------------------------------------------------------------
; fcp_undo - a transfer failed after creating its destination (SPEC.md 22.5.2)
; in:  [fcp_made] = 1 if there is anything to undo; AX = the error being
;      returned
; out: AX and every other register unchanged, CF untouched
;
; **A FAILED COPY MUST LEAVE NOTHING, NOT SOMETHING THAT LOOKS LIKE THE FILE.**
; fcp_room catches the case anybody can predict; this covers the ones nobody
; can - a rounding that fits by bytes and not by clusters, a directory that
; fills, a device that errors halfway, another writer taking the space between
; the check and the write. In all of them the destination is a partial file
; with a plausible name and a plausible size, and the next thing the user does
; with it is the thing that goes wrong.
;
; A REPLACE that fails is deleted too, and that is the right answer rather than
; a hard one: dskw_write has already released the old chain by the time a chunk
; can fail, so the original is gone whichever way this goes. What is left is a
; choice between a missing file and a corrupt one wearing its name.
;
; It preserves AX because the caller is on its way out with an FERR_* in it,
; and the flags because .err's `stc` is above this. A delete that fails is
; ignored: we are already failing, and there is nothing better to report.
; -----------------------------------------------------------------------------
fcp_undo:
    cmp byte [fcp_made], 0
    je .out
    push ax                     ; **AX AND NOTHING ELSE.** Its one caller is
    mov byte [fcp_made], 0      ; fcp_xfer's `.err`, whose very next
    FCPX fcp_gofd               ; instructions pop BX, CX, DX, SI, DI and ES
                                ; off the stack and whose `stc` sets the flags
                                ; after this returns - so those are free and
    jc .done                    ; saving them is bytes spent to restore values
    mov si, fcp_fname           ; that are about to be overwritten. AX is not:
    FCPX dskw_delete_x          ; it carries the FERR_* on its way out
.done:
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; fcp_xfer - move one file's bytes, source directory to destination
; out: CF=0, or CF=1 with AX = FERR_* (FERR_BIG when it does not fit the
;      buffer - see the header note about dskw_write's 16-bit length)
; clobbers: AX, BX, CX, DX, SI, DI, flags (fcp_run's free set); ES is kept
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
    push es                     ; (the rest is fcp_run's free set)
    FCPX fcp_gofs               ; open the source stream AND take its first
                                ; chunk: we are here, and the alternative is
    jc .jerr                    ; coming back for it

    ; --- BOTH ENDS ON ONE REDIRECTED VOLUME: let the far side do it -------
    ; A remote-to-remote copy streamed every byte across the cable and back
    ; again, which at PERFORMANCE.md Set 39's 3,741 B/s is visible on a large
    ; file and is entirely wasted: the far side has both the source and the
    ; destination. FSV_COPY hands it the pair and it copies with its own
    ; int 21h, at its own disk's speed.
    ;
    ; The test is after fcp_goto, not before, because [dsk_vkind] describes
    ; the volume we are STANDING ON - and standing on the source is what the
    ; line above just arranged. A driver that publishes no FSV_COPY is
    ; refused by drv_fs_call exactly as it is for any absent verb, so the
    ; fall-through below is the whole error path and this can never be worse
    ; than not trying (SPEC.md 62.9.8).
    cmp byte [dsk_vkind], DVK_FILE
    jne .stream
    mov al, [fcp_drv]
    cmp al, [fcp_ddrv]
    jne .stream
    push bp
    mov ax, [fcp_fsclus]        ; the two FOLDERS, and one name: fcp_xfer
    mov dx, [fcp_fdclus]        ; copies under the same name at both ends
    mov si, fcp_fname
    mov bp, FSV_COPY
    FCPX drv_fs_call
    pop bp
    jnc .fin
.stream:
    mov si, fcp_fname           ; open the streaming read, in the CURRENT
    FCPX dskw_stat_x            ; directory: BX = first cluster, DX:CX = size
    jc .err
    mov [fcp_rclus], bx         ; ...or, on a redirected volume, THE HANDLE
    mov [fcp_rsz], cx           ; dskw_stat answered (SPEC.md 62.9.1)
    mov [fcp_rszh], dx
    mov [fcp_need], cx          ; THE WHOLE FILE, banked before the first chunk
    mov [fcp_need+2], dx        ; comes off it: fcp_rsz is what REMAINS, so
                                ; after fcp_rdnext it is no longer the size
    xor ax, ax                  ; such a read walks an OFFSET, not a chain
    mov [fcp_roff], ax
    mov [fcp_roff+2], ax
%ifdef KERN_BIG
    xor di, di                  ; no WRITE_SEQ token yet: the first is COLD.
%endif                          ; DI carries it from here: nothing in the loop
                                ; below touches it but WRITE_SEQ itself, and
                                ; fcp_rdnext banks it

    ; --- ONE LOOP, and [fcp_made] says which write a chunk is -------------
    ; The first chunk CREATES the destination (dskw_write, a truncating
    ; create carrying its first bytes) and every later one EXTENDS it; they
    ; used to be two copies of the read-switch-write sequence. A take of 0
    ; bytes comes back only for an EMPTY file's first chunk - fcp_rdnext's
    ; take is min(what is left, the chunk) and the loop goes round only while
    ; something is left - and that creates a real empty file.
.chunk:
    call fcp_rdnext             ; AX = bytes; a chain that ended early says so
    jc .err
    mov [fcp_flen], ax
    FCPX fcp_gofd               ; the destination, with the chunk in hand
    jc .err
    cmp byte [fcp_made], 0
    jne .more
    call fcp_room               ; ...but ASK FIRST (SPEC.md 22.5.2). Here and
    mov ax, FERR_FULL           ; not earlier, because this is the first moment
    jc .err                     ; the destination volume is current - so the
                                ; check costs no switch of its own - and it is
                                ; still before anything has been created
                                ; (`mov` writes no flag: CF is fcp_room's)
    call fcp_wprep
    xor dx, dx                  ; DX:CX = the chunk (SPEC.md 18.4.1) - the one
    FCPX dskw_write_x           ; argument the two sites do not share
    jc .err
    mov byte [fcp_made], 1      ; ...and from HERE a failure owes a delete
    jmp short .next
.more:
    call fcp_wprep
%ifdef KERN_BIG
    mov al, WSQF_HELD           ; ONE HELD STREAM per file (SPEC.md 22.5.3):
    call COLD_SEG:dwf_dskw_write_seq    ; no lookup and no walk a chunk, and
                                ; no FAT or directory write until the close
                                ; below - a partial copy is deleted anyway,
                                ; so HELD's one loss is nothing
%else
    FCPX dskw_append_x
%endif
    jc .err
.next:
    mov ax, [fcp_rsz]           ; the file itself says whether there is more,
    or ax, [fcp_rszh]           ; so finishing costs no I/O at all
    jz .fin
    FCPX fcp_gofs
    jnc .chunk
.jerr:
    jmp short .err
.fin:
%ifdef KERN_BIG
    xor cx, cx                  ; THE CLOSE: the stream's link, FAT and size.
    call COLD_SEG:dwf_dskw_write_seq    ; Nothing held (one chunk, a
    jc .err                     ; redirected copy) answers AX = 0 at once; a
%endif                          ; failure is the disk's, and the partial copy
                                ; goes with it
    mov byte [fcp_made], 0      ; whole and committed: nothing to undo
    clc
    jmp short .out
.err:
    call fcp_undo               ; ...and a partial destination goes with it
    stc                         ; (SPEC.md 22.5.2). It preserves AX and the
.out:                           ; flags, the FERR_* being on its way out
    pop es
    ret

; fcp_wprep - the four arguments dskw_write and dskw_append share
; out: ES:BX -> the chunk, CX = its length, SI -> the destination name.
;      All four are fcp_xfer's to clobber (fcp_run's free set, and ES it
;      banks itself), which is where both callers are; the create site's
;      `xor dx, dx` stays at the site
fcp_wprep:
    mov es, [fcp_seg]
    xor bx, bx
    mov cx, [fcp_flen]
    mov si, fcp_fname           ; the name at BOTH ends: a copy never renames
    ret

; -----------------------------------------------------------------------------
; fcp_door - OSAPI_FILE_COPY's body, both verbs (SPEC.md 22.24)
; in:  SI -> the 8.3 name, staged in kernel scratch by the N cell (api_n);
;      AL = the verb, OSAPI_FCP_COPY (0) or OSAPI_FCP_MOVE (1); BL = the
;      source drive and DX its folder's first cluster, BH = the destination
;      drive and CX its folder's - the pair OSAPI_FILE_HERE answers and
;      OSAPI_FILE_GOTO takes
; out: CF=0 with AX = 0; CF=1 with AX = FERR_*
; clobbers: AX, flags
;
; A FILLER FOR THE ENGINE, and nothing else: it puts the verb and the two
; places into the operation record the way fcp_paste copies them off the
; clipboard, and enters fcp_run2 - so a package's copy and the user's paste
; are ONE path, the walk, the overwrite, the partial-destination undo and the
; listing debt included. What is here and is not in fcp_paste is only what a
; package cannot be assumed to have done: the busy test (the user's own
; operation may be suspended on its question, and its record must not be
; touched), the verb check, and the stat that says whether the entry is a
; FOLDER - which the paste has for free from the listing, and which decides
; the tree walk, the self-paste refusal and the re-link's '..' fix-up.
;
; A MOVE IS A CUT: re-linked on one volume, and copied then deleted where the
; re-link declines - the far side of a volume boundary, a destination that
; holds the name, a folder with no reusable slot. There is no "not attempted"
; answer any more, because the caller had nothing to do with one but copy
; and delete, which is what this does. The one thing a move never does is
; ask: [fcp_all] = 1 is the "replace all" a paste's user would have given.
;
; The answer is read back out of [fcp_err], which fcp_run leaves as FERR_OK,
; FERR_EXIST (the destination is the folder the entry is already in - nothing
; to do, nothing written, and for a door the honest answer is a refusal rather
; than a paste's silent no-op) or the FERR_* behind an FCPS_ERR. FCPS_ASK
; cannot come back with [fcp_all] set, so AL is not consulted at all.
;
; THE HEADER OF THIS FILE SAYS "gfx lock held by the caller" AND THAT IS A
; DESCRIPTION OF ITS CALLERS, NOT A REQUIREMENT. Nothing on the copy path
; draws: the exclusion the disk layer needs is [sch_lock], which dsk_xfer
; raises itself, and the bodies fcp_xfer streams through - dskw_write_x and
; dskw_append_x - are the same two OSAPI_FILE_WRITE and OSAPI_FILE_APPEND have
; always published to any package.
; -----------------------------------------------------------------------------
FCPBODY fcp_door
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [fcp_busy], 0      ; ONE OPERATION RECORD in this module, so a
    jne .busy                   ; Paste that is running - or suspended on its
                                ; overwrite question - owns it. Tested BEFORE
                                ; a byte of it is written
    cmp al, 1                   ; a verb the SDK does not name is refused, not
    ja .bad                     ; guessed at: a caller that left AL alone must
    inc ax                      ; not find its file moved. 0/1 -> FCP_COPY /
    mov [fcp_op], al            ; FCP_CUT
    mov [fcp_drv], bl
    mov [fcp_cwd], dx
    mov [fcp_ddrv], bh
    mov [fcp_dcwd], cx
    mov di, fcp_name            ; the staged name into the record; SI is
    call fcp_n13                ; api_name and dsk_ncopy preserves it
    FCPX fcp_gosrc
    jc .ans                     ; AX = FERR_NODISK: the source volume would
                                ; not mount, and that is the whole answer
    mov si, fcp_name
    FCPX dskw_stat_x            ; dskw_raw = its 32 bytes...
    jc .ans                     ; ...or AX = FERR_NOENT et al., which is this
                                ; door's answer too - the engine would find
                                ; the same thing a directory walk later
    and ax, DSKW_DIR            ; AL = the raw attribute byte: the folder
    mov cl, 3                   ; bit, 0x10, becomes SPEC.md 19's folder type
    shr ax, cl                  ; 2 and anything else a FILE's 0 - the tree
    mov [fcp_type], ax          ; walk, fcp_selfchk and fcp_rlup all key off it
    mov [fcp_all], cl           ; replace without asking (see above): any
                                ; non-zero is 'all', and CL is 3
%if DSKW_DIR != 0x10
  %error "fcp_door shifts DSKW_DIR down to the folder type 2"
%endif
    call fcp_run2
    mov al, [fcp_err]
.ans:
    cbw                         ; every FERR_* is under 128
    cmp ax, 1                   ; CF = (AX == 0)...
    cmc                         ; ...so CF = (AX != 0): 0 is FERR_OK
    jmp fcp_x5
.bad:
    mov al, FERR_NAME           ; "bad argument", which is the SDK's own
    jmp short .ans              ; reading of that code
.busy:
    mov al, FERR_NODISK         ; the nearest true thing: the machinery this
    jmp short .ans              ; needs is not available to you right now,
                                ; which is fcp_pfail's own precedent
; -----------------------------------------------------------------------------
; fcp_srcdel - a Cut's second half: remove the source, once the copy is whole
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX, BX, CX, DX, SI, DI, flags (fcp_run's free set - see fcp_run)
; -----------------------------------------------------------------------------
fcp_srcdel:
    FCPX fcp_gosrc
    jc .out
    mov si, fcp_name
    cmp word [fcp_type], 2
    jae .tree
    FCPX dskw_delete_x          ; ...and the callee's CF is this routine's
.out:                           ; answer on both arms
    ret
.tree:
    FCPX dskw_rmtree            ; the folder AND everything under it (18.6)
    ret

; -----------------------------------------------------------------------------
section .bss

; THE CLIPBOARD - what fcp_arm recorded, resident on both builds (SPEC.md
; 22.3.0: an arm is safe to drop the image on because this is `.bss`). It is
; READ by exactly one routine, fcp_paste, which copies it whole into the
; operation record below; the engine never looks at it.
fcp_cbop:   resb 1              ; FCP_NONE / FCP_COPY / FCP_CUT
fcp_cbdrv:  resb 1              ; the clipboard entry's drive...
fcp_cbcwd:  resw 1              ; ...folder...
fcp_cbtype: resw 1              ; ...and SPEC.md 19 type word
fcp_cbname: resb 13             ; ...and 8.3 display name, COPIED
%if $ - fcp_cbop != FCP_CBSZ
  %error "the clipboard record is not FCP_CBSZ bytes - fcp_paste copies it as one"
%endif

fcp_busy:   resb 1              ; 1 = an operation is running or suspended
fcp_started: resb 1             ; 1 = fcp_step is past its first step
fcp_all:    resb 1              ; 1 = 'replace all' was answered
fcp_pend:   resb 1              ; 1 = a file is waiting on a verdict
fcp_ovw:    resb 1              ; that verdict: 1 = replace it
fcp_err:    resb 1              ; FERR_* of whatever stopped us
fcp_ddrv:   resb 1              ; the operation's target drive...
fcp_dcwd:   resw 1              ; ...and folder

fcp_seg:    resw 1              ; the copy buffer, 0 = none (fcp_claim)...
fcp_chunk:  resw 1              ; ...and the bytes one pass moves

fcp_rlclus: resw 1              ; fcp_relink: the moved entry's first cluster

fcp_rclus:  resw 1              ; the streaming read: the next cluster, or on
                                ; a redirected volume THE HANDLE, which does
                                ; not move...
fcp_roff:   resd 1              ; ...and where that read has got to, which is
                                ; what replaces stepping a chain there...
fcp_rsz:    resw 1              ; ...the bytes of the file still to come,
fcp_rszh:   resw 1              ; low and high...
fcp_rgot:   resw 1              ; ...and what this chunk has taken so far

fcp_depth:  resw 1              ; frames in use, 0 = not inside a tree
; --- and the descent stack goes in the IMAGE's own bss on kern_small ------
; (docs/plans/MODULE-SELFCONTAIN-PLAN.md 3). It is SCRATCH by 3.4's test: its
; whole life is inside one copy operation, fcp_fin drops the image at the end
; of every one, and a suspended paste still holds the image while the user
; reads the overwrite question - so there is no moment when the stack is
; wanted and FILECP.DRV is out. mod_need zeroes the claim's tail, so it starts
; zeroed exactly as `.bss` did.
;
; THE MODULE ARM IS LAST on purpose: tools/os88ovlchk.py reads source and
; cannot evaluate an %ifdef, so the section it ends up in is the one it models
; - and kern_small is the build its module-data check exists to protect. Same
; reason filecp.inc's one other switch is written this way round.
; THE OPERATION RECORD - the clipboard's shape, in the clipboard's order.
; fcp_paste fills it as a paste starts and fcp_door as a door call starts,
; and nothing reads it once the operation has ended - which is the stack's
; own scratch test, and it is NOT in the image's bss anyway: fcp_name is
; handed to resident routines (dskw_stat_x, dskw_delete_x) as a DS pointer,
; and on kern_small a `.modpb` label is an image offset reached through CS.
; fcp_paste copies FCP_CBSZ bytes from fcp_cbop to here in one dsk_ncopy,
; which is why the two records are pinned to one layout and both asserted.
fcp_op:     resb 1              ; FCP_COPY / FCP_CUT (a move IS a cut)
fcp_drv:    resb 1              ; the entry's drive - the source drive of the
                                ; whole tree, so the walk reads this one word
                                ; wherever it is
fcp_cwd:    resw 1              ; ...its folder...
fcp_type:   resw 1              ; ...its SPEC.md 19 type word...
fcp_name:   resb 13             ; ...and its 8.3 name
%if $ - fcp_op != FCP_CBSZ
  %error "the operation record is not FCP_CBSZ bytes - fcp_paste copies it as one"
%endif

%ifndef FCP_MOD
section .bss
%else
section .modpb
%endif
fcp_stack:  resb FCP_MAXD * FCP_FSIZE
%ifndef FCP_MOD
section .bss
%else
section .bss                    ; ...and back, for the words that STAY: the
%endif                          ; clipboard is resident by 3.4

fcp_cname:  resb 13             ; fcp_scan's answer: name... (its only writer
                                ; is fcp_n13, `mov cx, 13` + dsk_ncopy, a BYTE
                                ; loop that ends exactly here - which is what
                                ; made the alignment pad that used to follow
                                ; safe to drop. An 8088's bus is 8 bits and
                                ; charges nothing for the odd word)
fcp_ctype:  resw 1              ; ...SPEC.md 19 type word...
fcp_csrc:   resw 1              ; ...and first cluster. NOT the size: it was
                                ; stored in two more words here and read by
                                ; nothing - fcp_xfer sizes the copy off the
                                ; DESTINATION handle, not off the scan

fcp_fname:  resb 13             ; the pending file: name, at BOTH ends -
                                ; files.inc reads it for the overwrite
                                ; question, so it is resident on both builds
fcp_fsclus: resw 1              ; ...source folder (the drives are the
                                ; operation's own, fcp_drv and fcp_ddrv)...
fcp_fdclus: resw 1              ; ...destination folder...
fcp_made:   resb 1              ; 1 = fcp_xfer has CREATED the destination, so
                                ; a failure from here owes a delete (SPEC.md
                                ; 22.5.2)
fcp_ovwsz:  resw 2              ; the size of the entry this file will REPLACE,
                                ; 0 = it replaces nothing. A replace hands its
                                ; clusters back, so it is room the check may
                                ; count (22.5.2)
fcp_need:   resw 2              ; the whole file, banked across dskw_dfree -
                                ; which answers in DX:AX and so spends the
                                ; register the count arrives in
fcp_flen:   resw 1              ; ...and the byte count dskw_read reported

section .cold
; =============================================================================
; fcp_goto AND ITS FOUR DOORS - RESIDENT ON BOTH BUILDS
; =============================================================================
; They were in the middle of this file and are down here so that the file has
; exactly ONE conditional `section` in it.
;
; WHY THEY DO NOT MOVE INTO THE IMAGE. CLONE.DRV far-calls fcp_goto through
; fcpf_fcp_goto BETWEEN TWO RAW TRANSFERS of a same-drive clone (SPEC.md
; 18.99.8) - which is exactly the moment the system disk is NOT in the drive.
; A mod_need there would go looking for FILECP.DRV, fail on drv_mounted, and
; hand the cloner a CF=1 it can only stop on. So the image far-calls IN to
; these like any other resident routine, and one module never loads another.
; =============================================================================
; -----------------------------------------------------------------------------
; fcp_gosrc / fcp_godst / fcp_gofs / fcp_gofd - the four fcp_goto arguments
; this module ever passes, written once each (SPEC.md 22.3)
; out: as fcp_goto
; clobbers: AX, BX, flags - BX is the argument, so every one of the thirteen
;      sites was already loading it with this exact value on the line before
;
; The clipboard entry's folder, the paste target, and the pending file's
; source and destination.  Thirteen sites spelled out the same three
; instructions; the last door falls into fcp_goto rather than jumping to it.
; -----------------------------------------------------------------------------
; Two pairs that share a DRIVE - the pending file's source is on the
; clipboard entry's drive and its destination on the paste target's, at every
; depth - so each pair loads its folder and then shares the drive load.
fcp_gosrc:
    mov bx, [fcp_cwd]
    jmp short fcp_gos
fcp_gofs:
    mov bx, [fcp_fsclus]
fcp_gos:
    mov al, [fcp_drv]
    jmp short fcp_goto
fcp_godst:
    mov bx, [fcp_dcwd]
    jmp short fcp_god
fcp_gofd:
    mov bx, [fcp_fdclus]
fcp_god:
    mov al, [fcp_ddrv]

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
; The debt is [dsk_lstale], and fcp_step settles it with dskw_sync on every
; path that can return to the event loop.
;
; The cluster is range-checked here rather than trusted, because the quiet
; path skips disk_mount's own .cwd_lost validation.
;
; Its three callees are reached with a PLAIN `call` on both builds, not FCPX:
; this body is resident `.cold` on kern_small too, so they are near - FCPX
; there was a far call through a resident `xf_` thunk into its own segment,
; two bytes a site and a four-byte thunk apiece for nothing.
; -----------------------------------------------------------------------------
fcp_goto:
    push dx
    cmp byte [dsk_mntok], 0     ; IS ANYTHING MOUNTED? dsk_here_ok asks this
    je .full                    ; first and for the same reason (SPEC.md
                                ; 18.9.1): [disk_drive] is where the machine
                                ; BELIEVES it stands, and dsk_vol_del sets it
                                ; to 0 when the volume under it is unmounted -
                                ; without mounting A:, which is what the
                                ; cleared [dsk_mntok] beside it says. The
                                ; compare below then reads "already on A:" off
                                ; a machine whose BPB, FAT window and geometry
                                ; still describe the dead volume, and this
                                ; answers CF = 0 having done nothing at all.
                                ; Every read after it fails, which on the
                                ; hard-disk installer's path is a copy that
                                ; stops on its first file (SPEC.md 52.10.8.1).
                                ; It came from osapi_file_goto_q, which had it
                                ; and was otherwise this body twice over
    cmp al, [disk_drive]
    jne .full
    mov dl, al                  ; **...AND IS IT STILL THE SAME DISK?** (SPEC.md
    call dsk_media_ok           ; 18.9.1.1) The compare above says only that the
    jc .full                    ; VOLUME has not changed, and a floppy is the
                                ; thing that can be swapped under it. Without
                                ; this the quiet arm wrote [dsk_cwd] and
                                ; returned, so no boot sector was read, so
                                ; [dsk_sigcur] never moved - and 18.95's cache
                                ; is keyed on exactly that, so it went on
                                ; serving the OLD disk's directory until the
                                ; package was closed. MEASURED: three DIRs four
                                ; seconds apart cost reads=0 and listed a disk
                                ; that was no longer in the drive.
                                ;
                                ; The MEDIA half alone - dsk_here_ok also asks
                                ; whether the DIRECTORY has moved, and moving
                                ; between folders of one mounted volume with no
                                ; I/O is what this routine is FOR. DX is banked
                                ; at the top and .full re-loads DL anyway
    cmp byte [dsk_vkind], DVK_FILE
    je .fsgo                    ; a REDIRECTED volume: below
    or bx, bx
    jz .quiet                   ; 0 is the root, always legal
    cmp bx, 2
    jb .bad
    cmp bx, [dsk_maxclus]
    ja .bad
.quiet:
    mov [dsk_cwd], bx
.ok:
    pop dx
    clc
    ret
.bad:
    mov ax, FERR_IO
    jmp short .eout

    ; --- a REDIRECTED volume: TELL THE DRIVER, do not just store the word ---
    ; The quiet path above exists precisely to avoid a mount for a move inside
    ; one volume - and a mount is the only thing that would have called
    ; FSV_CHDIR. A DRVC_FILE driver keeps its own idea of where it is standing
    ; (it has to: FSV_STAT resolves a name in the CURRENT folder), so writing
    ; [dsk_cwd] behind its back leaves the two disagreeing and every name the
    ; copy then asks about is looked up in the folder it came FROM. It
    ; presented as FERR_NOENT for a file that is plainly listed.
    ;
    ; The range checks go with it: that word is an OPAQUE HANDLE (SPEC.md
    ; 62.9.1), `>= 2` rejects the first folder such a driver ever hands out,
    ; and [dsk_maxclus] belongs to the last FAT volume mounted.
.fsgo:
    push bp
    mov ax, bx                  ; the handle, or 0 for the root
    mov bp, FSV_CHDIR
    call drv_fs_call
    pop bp
    jc .bad
    jmp short .quiet
.full:
    mov dl, al
    mov ax, bx
    call dsk_chdir_q_x
    jnc .ok
.ferr:
    mov ax, FERR_NODISK
.eout:
    pop dx
    stc
    ret

; -----------------------------------------------------------------------------
; The one far entry this module publishes (SPEC.md 2.6), and it is for an
; ON-DEMAND MODULE rather than for the kernel: CLONE.DRV has a FILE at one end
; of a sector copy (SPEC.md 18.99.8), so between two raw transfers it has to
; put the machine back where the file lives - which is exactly what fcp_goto
; is, including the free case where the volume has not moved and only
; [dsk_cwd] has to be re-aimed.
; -----------------------------------------------------------------------------
fcpf_fcp_goto:          call fcp_goto
                    retf

; ...and OSAPI_FILE_COPY's (SPEC.md 22.24). kernel.asm's `.text` thunk reaches
; the engine through here, which on kern_big is the body and on kern_small is
; the loading stub below - one name, both shapes, and the slot never learns
; which build it is in.
fcpf_fcp_door:          call fcp_door
                    retf

%ifdef FCP_MOD
; =============================================================================
; THE RESIDENT HALF (SPEC.md 2.8.1) - kern_small only
; =============================================================================
; What stays behind when the bodies leave: one loading stub per public name,
; and one four-byte far entry per routine the image calls out to.
;
; fcpf_fcp_goto above now reaches the STUB, so CLONE.DRV far-calling it is one
; module asking a resident entry to load another. That is bounded rather than
; clever - the two images are separate heap claims, neither is dropped while
; the other runs, and a refusal comes back as the CF=1 that entry documents.
; -----------------------------------------------------------------------------

; fcp_load - is the image in RAM?
; out: CF=1 it is not. AX and every other register preserved: `pop ax` and
;      `ret` write no flag, so mod_need's CF reaches the caller intact.
fcp_load:
    push ax
    mov al, MOD_FCP
    call mod_need
    pop ax
    ret

; --- the four public names, unchanged, so no call site in files.inc moves ---
; Each REFUSES the way its body would have. fcp_arm's CF=1 is "nothing
; selected", a no-op its caller already handles; the two paste doors answer
; FCPS_ERR with [fcp_err] set, which is a path fcp_step already takes and
; files.inc already letters.
; fcp_fin - the operation is over, so give the image back (SPEC.md 2.8.3)
; Preserves AL and the FLAGS, because between them they are the caller's whole
; answer; mod_drop preserves every register itself and clobbers only flags.
;
; SPEC.md 2.8.3 says the caller decides when, and there is exactly one right
; moment per feature. Here it is "the operation finished", which is every
; return that is not FCPS_ASK - a suspended paste still owns the image while
; the user reads the overwrite question. What makes an arm safe to drop on is
; that the CLIPBOARD IS `.bss`: fcp_arm records a selection in resident data
; and the image is not needed again until the paste.
fcp_fin:
    pushf
    push ax
    mov al, MOD_FCP
    call mod_drop
    pop ax
    popf
    ret

fcp_arm:                        ; files.inc reaches this one with `jmp`, and a
    call fcp_load               ; stub is as jumpable as the body it replaces
    jc fcp_pfin.keep            ; the refusal is a bare `ret` (CF = 1, the
                                ; `jc`'s), and fcp_pfin has one
    call far [FCPFP + FCPE_ARM*4]
    jmp short fcp_fin           ; CF is fcp_arm's answer and fcp_fin keeps it

fcp_paste:
    call fcp_load
    jc fcp_pfail
    call far [FCPFP + FCPE_PASTE*4]
    jmp short fcp_pfin

fcp_answer:
    call fcp_load
    jc fcp_pfail                ; a suspended paste whose image has gone cannot
    call far [FCPFP + FCPE_ANSWER*4]   ; be resumed either, and says so the
                                       ; same way
fcp_pfin:
    cmp al, FCPS_ASK            ; suspended? then the image is still the
    je .keep                    ; operation's - fcp_answer will come back for
    jmp short fcp_fin           ; it. Both doors document `clobbers: AX, flags`,
.keep:                          ; so the compare costs nothing they promised
    ret

; OSAPI_FILE_COPY's stub, both verbs. Its refusal is the body's own
; FERR_NODISK with CF - "the machinery is not available to you right now" -
; which is exactly what a system disk that is not in the drive means here
; too, so the slot's caller needs no way to tell the two apart.
fcp_door:
    call fcp_load
    jc .no
    call far [FCPFP + FCPE_DOOR*4]
    jmp short fcp_fin           ; AX and CF are the answer; fcp_fin keeps both
.no:
    mov ax, FERR_NODISK         ; CF = 1 already: the `jc` is the one way in,
    ret                         ; and `mov` writes no flag

fcp_pfail:
    mov byte [fcp_err], FERR_NODISK   ; the system disk was not there to read
    mov al, FCPS_ERR                  ; the image from, which is the literal
    stc                               ; truth and dskw_fmt_probe's precedent -
    ret                               ; "the nearest true thing" (SPEC.md 2.8.1)


%endif

section .text
