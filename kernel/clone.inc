; =============================================================================
; os8088 - clone.inc
;
; The disk cloner (SPEC.md 18.99), prefix `clo_`. It copies LBA 0 to
; LBA N-1 from one floppy to another - or from one floppy to a second disk in
; the SAME drive - and it never looks at a directory, a FAT or a cluster. That
; is the whole difference from every other copy in this tree: a clone works on
; the system disk, on a disk whose BPB SPEC.md 18.2 would refuse, and on a
; disk holding a file system nobody here has heard of.
;
; **AN ON-DEMAND MODULE** (SPEC.md 2.8), shipped as `CLONE.DRV`. It qualifies
; on docs/plans/completed/ONDEMAND-PLAN.md 1's test for the formatter's reason with one more
; term in its favour: preparing a disk already has a trip to the disk box in
; it, and a clone has TWO.
;
; **THE RESIDENT HALF IS ONE WORD AND ONE THUNK**, and that is deliberate
; rather than lucky. A module's DATA is resident - cold code carries none and
; reads through DS = KERNEL_SEG (SPEC.md 2.6) - so a feature with thirty words
; of state pays for those thirty words for the life of the machine, on a
; kernel whose image rung had 148 bytes left in it. This one keeps its state,
; and the 512-byte keep of the source's boot sector, in the HEAD OF THE BUFFER
; IT WAS GOING TO CLAIM ANYWAY (SPEC.md 18.99.1). `[clo_seg]` names that
; claim; every CL_* offset below is read through ES; a machine that has never
; cloned a disk carries two bytes.
;
; The status line's own composer is in the image too (CLV_LINE), because a
; clone prompt can only ever be on screen while the image is loaded - the
; prompt is armed after `mod_need` succeeds and `fm_edit_end` drops the image
; when it ends. The strings it stages are resident, because they are data.
; =============================================================================

; --- the error codes, indexed into fm_clotab (files.inc) ---------------------
CERR_IO      equ 1              ; unrecoverable read or write
CERR_WPROT   equ 2              ; the target is write-protected
CERR_MEM     equ 3              ; no buffer to be had
CERR_NODISK  equ 4              ; not a floppy, or its geometry is unreadable
CERR_SMALL   equ 5              ; the target drive cannot hold the source
CERR_FULL    equ 6              ; the volume the image would go on is too full
CERR_NOTIMG  equ 7              ; that file is not a whole floppy (SPEC.md 18.99.8)
CERR_SAME    equ 8              ; ...and it may not live on the disk itself
CERR_BUSY    equ 9              ; a clone is ALREADY running (SPEC.md 22.21.2)
CERR_NOMOD   equ 10             ; CLONE.DRV itself could not be read: the
                                ; system disk is not in the boot drive
                                ; (SPEC.md 18.99.7). NOT CERR_NODISK, whose
                                ; 'No disk' read as "no disk to image" off
                                ; the 5150 with a disk in both drives
CLO_NERR     equ 11

; --- which prompt is up (SPEC.md 22.21.1) ------------------------------------
CLS_PICK     equ 0              ; 'Clone A: to B:, ERASING it'
CLS_SRC      equ 1              ; 'Insert the SOURCE disk, then Enter'
CLS_TGT      equ 2              ; 'Insert the TARGET disk, then Enter'
CLS_SAME     equ 3              ; 'Still the SOURCE disk'
CLS_WIMG     equ 4              ; 'ERASE A: and write DISK.IMG?' (SPEC.md 18.99.8)
CLS_NOSRC    equ 5              ; 'Not the SOURCE disk' - CLS_SAME's twin on
                                ; the other prompt (SPEC.md 18.99.3)

; --- what CLV_KEY tells the window it owes the screen ------------------------
; **0x80 and up, clear of the CERR_* block above.** clo_call answers in two
; alphabets on one register - AL is a CLA_* on success, AX a CERR_* with CF=1
; when the image cannot be had (SPEC.md 18.99.7) - and CF is the discriminator.
; It used to be the ONLY one: CERR_NODISK and CLA_SAVE were both 4, so a
; caller that forgot the `jc` read 'the cloner could not be loaded' as 'the
; user asked for an image target' and opened a Save box. Disjoint values make
; that miss a wrong answer rather than a plausible one.
CLA_LINE     equ 0x80           ; the prompt changed and nothing else did
CLA_DONE     equ 0x81           ; finished; BL/BH name the two volumes
CLA_STOP     equ 0x82           ; Esc - the user said it, so nothing is said
CLA_ERR      equ 0x83           ; AH = CERR_*
CLA_JDONE    equ 0x85           ; Uncompress To... ended: it has said its
                                ; verdict and freed its claim (SPEC.md 22.23.6)
CLS_JOIN     equ 0x80           ; CL_STEP of a claim that is the JOIN's, not a
                                ; clone's: clo_disp_x sends mode 7 to it
                                ; 0x84 was CLA_SAVE, "ask for a file name":
                                ; the image opens that Save box itself now,
                                ; through fdf_fdlg_open with the RESIDENT
                                ; fm_img_done_x as its completion proc - the
                                ; one thing fdlg_open needs that an image
                                ; cannot be (SPEC.md 18.99.8)

; --- the verbs (SPEC.md 18.99.7), in DL ---------------------------------------
CLV_START    equ 0
CLV_KEY      equ 1
CLV_LINE     equ 2
CLV_FREE     equ 3
CLV_SAVED    equ 4              ; the Save dialog answered: do the whole
                                ; disk -> file operation (SPEC.md 18.99.8) -
                                ; or, with NO claim live, start Uncompress
                                ; To...'s join there (SPEC.md 22.23.6)
CLV_WIMG     equ 5              ; the Open dialog answered: size it, claim,
                                ; and arm the confirmation

; --- the job block, at offset 0 of the claim (SPEC.md 18.99.1) ---------------
CL_SRC       equ 0              ; db: the source volume index
CL_TGT       equ 1              ; db: the target's - equal for a same-drive clone
                                ; (CL_SRC's next byte: clo_start and clo_wimg
                                ; store the pair as one word)
CL_STEP      equ 2              ; db: CLS_*
CL_XON       equ 3              ; db: 1 = the run is staged through XMS
CL_SPT       equ 4              ; dw: the SOURCE's sectors per track...
CL_HDS       equ 6              ; dw: ...heads...
CL_TOT       equ 8              ; dw: ...and total sectors. The target is
                                ;     written with all three: an EXACT clone
CL_NEXT      equ 10             ; dw: the next LBA to read
CL_HLBA      equ 12             ; dw: where the run in the buffer starts...
CL_HELD      equ 14             ; dw: ...and how many sectors it is
CL_CHUNK     equ 16             ; dw: sectors one trip moves
CL_BUFSG     equ 18             ; dw: the staging buffer's segment
CL_STAGE     equ 20             ; dw: sectors that buffer holds (XMS path)
CL_XLO       equ 22             ; dw \ the XMS block's 32-bit linear base
CL_XHI       equ 24             ; dw /
CL_XKB       equ 26             ; dw: KB claimed up there, 0 = none
CL_KB        equ 28             ; dw: KB of the conventional claim
CL_BSPT      equ 30             ; dw \ the caller's live geometry, banked
CL_BHDS      equ 32             ; dw  | across the probe and put back on
CL_BTOT      equ 34             ; dw /  every refusal (dskw_fmt_probe's .undo)
CL_MOK       equ 36             ; db: 1 = the source MOUNTED, so its BPB has
                                ;     already answered the geometry question
CL_END       equ 38             ; db: 0 = disk to disk; 1 = the TARGET is an
                                ;     image file; 2 = the SOURCE is one
CL_FDRV      equ 39             ; db: the image file's volume
CL_SPC       equ 40             ; dw: sectors per cluster there - dskw_append
                                ;     and dskw_read_at each want a whole
                                ;     number of clusters (SPEC.md 18.4.4)
CL_FOFF      equ 42             ; dd: the byte offset reached in the file
CL_FSPT      equ 46             ; dw \ the FILE volume's live geometry, banked
CL_FHDS      equ 48             ; dw  | so a raw transfer on the DISK end can
CL_FTOT      equ 50             ; dw /  borrow those words and give them back
CL_FTOK      equ 54             ; dw: the image file's WRITE_SEQ token (18.4.9)
CL_FCUR      equ 56             ; 16: ...and its READ_SEQ cursor (18.4.8.1)
CL_FCLUS     equ 52             ; dw: the FOLDER the image file is in - the
                                ; volume alone is not enough, because the
                                ; current directory is GLOBAL and the UI runs
                                ; between arming a prompt and answering it
CLE_IMG      equ 0xFE           ; ...and the volume index that means "not a
                                ; volume at all, a file" (SPEC.md 18.99.8). It
                                ; is outside DVOL_MAX by construction, so no
                                ; row lookup can be handed it by accident
CL_BOOT      equ 256            ; 512 bytes: the source's sector 0, kept to
                                ; the end (SPEC.md 18.99.2)
CL_HDRPARA   equ 64             ; the header is 1KB of the claim

; --- the planner's constants (SPEC.md 18.99.4) -------------------------------
CLO_SLACK    equ 16             ; KB of the largest free run left for the rest
                                ; of the machine. A clone freezes everything,
                                ; but a menu still takes a save-under on the
                                ; way out and a toast still stages a line
CLO_MINKB    equ 9              ; 1KB of header and 8KB of buffer. Below this
                                ; the swap count stops being a user interface
CLO_STAGEKB  equ 33             ; ...and what the XMS path stages through:
                                ; 1KB of header and 32KB, which is exactly
                                ; XM_MAX_COPY, so one buffer load is one copy
CLO_XSEC     equ 64             ; sectors per xm_copy: 64 x 512 is XM_MAX_COPY
                                ; - a THIRD hand-maintained mirror of 32,768,
                                ; which is defined only in
                                ; drivers/xmem/xmem.asm. The link is this
                                ; comment and nothing compares them; a copy
                                ; over the image's cap is refused with AX = 3,
                                ; so the failure is loud rather than silent
                                ; exactly (SPEC.md 41.12.2), and the length
                                ; has to be even as well as bounded
CLO_IMGMAX   equ 127            ; sectors per FILE transfer: dskw_append takes
                                ; a 16-bit CX, so 128 sectors is 65,536 and
                                ; reads as 0. filecp caps its own chunk at the
                                ; same number for the same reason (SPEC.md
                                ; 22.5), and clo_imgsize then rounds this DOWN
                                ; to a whole number of clusters, which is what
                                ; dskw_append and dskw_read_at each require
CLO_WINSEC   equ 128            ; sectors per int 13h window: dsk_xfer advances
                                ; BX and never ES (SPEC.md 18.4), so a run may
                                ; not cross 64KB of OFFSET. clo_span rounds it
                                ; DOWN to whole CYLINDERS before using it - 126
                                ; on both 9-sector geometries, 108 on 1.44MB -
                                ; because a window edge inside the unit
                                ; dsk_xfer's run stops at costs a revolution
                                ; (SPEC.md 18.99.9, and 18.91.1 for why the
                                ; unit is the cylinder and not the track)

; --- this image's ABI with the resident thunk --------------------------------
CLE_DISP     equ 0              ; ONE entry for the cloner: the image
                                ; dispatches on DL itself, so four verbs cost
                                ; one mod_fp slot...
CLO_NENT     equ 2              ; ...and a SECOND for the compressor, which
                                ; rides in this image since SPEC.md 20.15.3 -
                                ; compress.inc is `.modl` too, included right
                                ; after this file, and CMZFP is the slot

; The extended-memory pool is reached through its own API cells rather than
; through a new pair of cw_ shims (SPEC.md 18.99.4). An OSAPI_SLOT is
; `push ds / push cs / pop ds / call <body> / pop ds / retf` - a far-callable
; wrapper around a near proc, which is exactly what a module needs and what
; four new shims in `.text` would have been, at 6 resident bytes each.
; THE STORE IS kern_big's ALONE: kern_small's xm_caps answers 0 KB always
; (xmem.inc), so clo_xmsup never took a block there and CL_XON stayed 0 for
; every run - the staging path, clo_xms and the free were dead in its image.
%ifdef KERN_BIG
%define CLO_XMS
%endif
CLO_XCAPS    equ apic_xm_caps          ; SPEC.md 41: out AX = free KB
CLO_XALLOC   equ apic_xm_alloc         ; in DX:AX = bytes; out CF, DX:AX = the base
CLO_XFREE    equ apic_xm_free          ; in DX:AX = a base this caller owns
CLO_XCOPY    equ apic_xm_copy          ; in ES:SI / DX:AX / CX / DI = direction

; CLO_RSEQ seg, off - dwf_dskw_read_at, served by READ_SEQ from the 16-byte
; cursor at seg:off (SPEC.md 18.4.8.1): READ_AT's registers in and out, every
; other register kept. The image's three chunked readers - clo_fget, the
; join's cmz_jneed and the compressor's cmz_sfetch - each walked the chain
; from the front on every chunk. kern_small has no READ_SEQ and gets READ_AT.
%macro CLO_RSEQ 2
%ifdef KERN_BIG
    push bp
    push di
    mov bp, %1
    mov di, %2
    call clo_rseq
    pop di
    pop bp
%else
    call COLD_SEG:dwf_dskw_read_at
%endif
%endmacro

section .modl                   ; the on-demand image (SPEC.md 2.8)
modl_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER                  ; MOD_H_VER
    db MOD_CLONE                ; MOD_H_ID
    dw BUILD_NUM                ; MOD_H_BUILD
    dw MOD_STAMP                ; MOD_H_STAMP
    dw modl_end                 ; MOD_H_IMG
    dw CLO_NENT                 ; MOD_H_NENT
    dw modl_e_disp
    dw modl_e_cmz               ; ...the compressor's (compress.inc)

modl_e_disp:    call clo_disp_x
            retf
modl_e_cmz:     call cmz_verb   ; CF and AX are the answer, and `retf` leaves
            retf                ; both alone

; -----------------------------------------------------------------------------
; clo_disp_x - the image's own dispatcher (SPEC.md 18.99.7)
; in:  DL = CLV_*, everything else the verb's own arguments
; out: the verb's
;
; A ladder and not a table: four rows, and a table would be four words of
; image plus the bound check that a far-callable table needs anyway. DL is
; the verb register because no verb below wants DX - which is the property
; that has to be checked when a fifth one is added, not assumed.
; -----------------------------------------------------------------------------
clo_disp_x:
%ifdef KERN_BIG
    cmp word [clo_seg], 0       ; Uncompress To... (SPEC.md 22.23.6) rides the
    jne .claimed                ; cloner's claim word and its mode 7. With NO
    cmp dl, CLV_SAVED           ; claim, the Save box's answer is the join's:
    jne .ladder                 ; a clone's is asked with its claim live
    jmp cmz_jgo
.claimed:                       ; ...and a claim marked CLS_JOIN is the join's,
    cmp dl, CLV_KEY             ; so its keys, lines and teardown go to
    jb .ladder                  ; compress.inc. CLV_START is never the join's:
    cmp dl, CLV_FREE            ; clo_start refuses a busy [clo_seg] as it
    ja .ladder                  ; always has
    push es
    mov es, [clo_seg]
    cmp byte [es:CL_STEP], CLS_JOIN
    pop es
    jne .ladder
    jmp cmz_jdisp
.ladder:
%endif
    cmp dl, CLV_START
    je clo_start                ; adjacent, so a short Jcc reaches it; the
    cmp dl, CLV_KEY             ; three below are hundreds of bytes away and
    jne .n1                     ; an 8086 Jcc is short only (SPEC.md 1)
    jmp clo_key
.n1:
    cmp dl, CLV_LINE
    jne .n2
    jmp clo_line
.n2:
    cmp dl, CLV_SAVED
    jne .n3
    jmp clo_saved
.n3:
    cmp dl, CLV_WIMG
    jne .n4
    jmp clo_wimg
.n4:
    jmp clo_free

; -----------------------------------------------------------------------------
; clo_start - probe, plan, claim and publish (SPEC.md 18.99)
; in:  AL = the source volume, AH = 1 if it MOUNTED (so its BPB has already
;      answered the geometry question), gfx lock held, UI task
; out: CF=0 and [clo_seg] published with CL_STEP = CLS_PICK;
;      CF=1 with AX = CERR_* and nothing claimed, nothing published
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
clo_start:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ax

    cmp word [clo_seg], 0       ; a clone is already running: refuse rather
    jne .busy                   ; than leak the first one's claim. It CAN
                                ; happen - the File menu is not gated on
                                ; FS_EDIT, so a second Disk window reaches
                                ; here while the first prompt is up - so it
                                ; is refused as CERR_BUSY and not as
                                ; CERR_MEM (SPEC.md 22.21.2)
    ; --- the buffer FIRST, because the probe has nowhere to put its answers
    ;     until there is one, and because a machine that cannot fund the
    ;     operation should not have spun the drive to find out
    call clo_plan               ; CF=1 with AX = CERR_*; CF=0 with [clo_seg]
    jc .fail                    ; live and ES = it
    pop ax
    push ax
    mov es, [clo_seg]
    mov [es:CL_MOK], ah         ; the caller's answer to "did it mount?", in
                                ; the block rather than in a register: clo_geom
                                ; is four pushes deep by the time it wants it
    mov ah, al                  ; the source - and the target starts on the
    mov [es:CL_SRC], ax         ; source's own drive, which the first Space
                                ; below moves off (CL_TGT follows; AX is the
                                ; caller's again at the `pop ax` below)
    mov byte [es:CL_STEP], CLS_PICK
    call clo_geom               ; CF=1 with AX = CERR_*
    jc .freefail
    call clo_size               ; the buffer's capacity, now that CL_TOT is
                                ; known, and the XMS block if this is a
                                ; same-drive clone that can have one
    jc .freefail
    mov al, [es:CL_SRC]
    call clo_nextvol            ; AL = the volume AFTER the source, wrapping:
    mov [es:CL_TGT], al         ; the OTHER drive on a two-drive machine, and
                                ; the only answer there is on a one-drive one
    pop ax
    clc
    jmp short .out
.busy:
    mov ax, CERR_BUSY
    jmp short .fail
.freefail:
    call clo_free               ; the claim goes back; nothing was armed
.fail:
    pop bx                      ; the banked AX, discarded: AX is the code
    stc
.out:
    jmp clo_x6                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; clo_fits - can the TARGET DRIVE hold what the source is? (SPEC.md 18.99.6)
; in:  ES = the claim, CL_TOT and CL_TGT set
; out: CF=0 = yes, or the question cannot be answered; CF=1 with AX =
;      CERR_SMALL. Clobbers AX, flags.
;
; Nothing is READ off the target and no attempt is made to match its geometry
; to the source's: an exact clone writes the source's LBAs under the source's
; CHS mapping, and a target that cannot hold them answers int 13h with an
; error that is reported. This is the ONE early refusal, and it exists because
; 1.44MB -> 720KB is worth catching before a single sector moves rather than
; after seven hundred of them.
;
; **It asks about the DRIVE and not the medium**, which is the only thing
; AH=08h can answer and the only thing that can be asked of a target that may
; be blank. A ROM that will not answer skips the check, and the write error is
; the backstop it always was. A same-drive clone skips it too: the drive is by
; construction the one the source came out of.
; -----------------------------------------------------------------------------
clo_fits:
    push bx
    push cx
    push dx
    push di
    call clo_same
    je .ok
    mov bl, [es:CL_TGT]
    call COLD_SEG:dskf_dsk_vol_row
    jc .ok                      ; not a row we can ask about
    mov dl, [bx+DV_UNIT]
    call clo_ask08              ; AX = cylinders, BH = the ANSWER's DH
    jc .ok                      ; it will not say: the write error is the test
    mov di, ax
    mov al, cl
    and al, 0x3F                ; sectors per track live in CL bits 5:0
    jz .ok
    mov bl, al                  ; BL = spt (BH holds the head answer: nothing
                                ; below writes it before it is read)
    mov al, bh                  ; heads = the ANSWER's DH...
    xor ah, ah
    inc ax                      ; ...+ 1
    mul di                      ; ...and cylinders x heads
    or dx, dx
    jnz .ok                     ; over 65,535 tracks: it is not too small
    mov cl, bl
    xor ch, ch
    mul cx                      ; ...x spt = the drive's whole extent
    or dx, dx
    jnz .ok
    cmp ax, [es:CL_TOT]
    jb .small
.ok:
    clc
    jmp short .out
.small:
    mov ax, CERR_SMALL
    stc
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_plan - claim the buffer and publish [clo_seg] (SPEC.md 18.99.4)
; out: CF=0 with [clo_seg] live and its header ZEROED; CF=1 with AX = CERR_MEM
; clobbers: AX (the output), ES, flags
;
; The size the disk needs is not known yet - clo_geom has not run - so this
; takes the biggest run it can and clo_size decides afterwards how much of it
; a trip uses. Halving on refusal rather than trusting mem_avail outright:
; the figure counts purgeable claims as free (SPEC.md 50.6.3) and mem_claim
; sheds them, so the two agree in the ordinary case and this is what covers
; the one where they do not.
; -----------------------------------------------------------------------------
clo_plan:
    push bx
    push cx
    push dx
    push di
    call KERNEL_SEG:apic_osapi_mem_avail ; AX = the largest free run, in KB.
                                ; The CELL (0x0210), whose chain ends at the
                                ; same mem_avail_x our own shim called:
                                ; osapi_mem_avail_x is a bare `jmp` to it
    sub ax, CLO_SLACK
    jc .nomem
    cmp ax, CLO_MINKB
    jb .nomem
    cmp ax, 640                 ; mem_claim's own bound: KB, 1..640
    jbe .try
    mov ax, 640
.try:
    mov di, ax                  ; DI = what we are asking for
    mov bx, MEM_K_CLONE
    call COLD_SEG:mmf_mem_claim ; DX = the base segment
    jnc .got
    mov ax, di
    shr ax, 1                   ; ...and half of it, down to the floor
    cmp ax, CLO_MINKB
    jae .try
.nomem:
    mov ax, CERR_MEM
    stc
    jmp short .out
.got:
    mov [clo_seg], dx
    mov es, dx
    call clo_zerohdr            ; BLANK FIRST. CL_KB is inside the block this
    mov [es:CL_KB], di          ; zeroes, so writing it first writes it and
    clc                         ; then throws it away - and a claim of 0 KB is
                                ; a buffer of no sectors, which clo_size
                                ; reports as CERR_MEM on a machine with 400KB
                                ; free. Measured, on exactly that machine
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_zerohdr - blank the job block, CL_BOOT included (module internal)
; in:  ES = the claim
; out: nothing (all registers preserved)
;
; -f bin zeroes nothing and neither does mem_claim: the header arrives as
; whatever the last owner of that heap left, and CL_KB is written by the
; caller AFTER this for exactly that reason.
; -----------------------------------------------------------------------------
clo_zerohdr:
    push ax
    push cx
    push di
    xor ax, ax
    mov di, ax
    mov cx, (CL_BOOT + 512) / 2
    cld
    rep stosw
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clo_geom - how many sectors is the source, and in what shape? (18.99.5)
; in:  ES = the claim with CL_SRC set, the banked AX still on clo_start's
;      stack with AH = 1 if the volume MOUNTED
; out: CF=0 with CL_SPT/CL_HDS/CL_TOT filled and the live geometry set to
;      them; CF=1 with AX = CERR_*, and the caller's geometry put back
; clobbers: AX (the output), flags
;
; **A source that MOUNTED has already answered.** [dsk_totsec], [disk_spt]
; and [disk_heads] came off its BPB through SPEC.md 18.2's one hostile
; pipeline, so for every disk this OS can read there is no probe at all and
; no ambiguity to resolve.
;
; For one that did not - the case this feature exists for - the medium is
; asked for its sectors per track, the DRIVE is asked for its cylinders, and
; then THE LAST SECTOR UNDER THAT ANSWER IS READ. dskw_fmt_reach_x's idea,
; read-only: the formatter resolves 9-sector ambiguity downward because
; writing a 720KB layout onto 360KB media is unrecoverable, and here the same
; ambiguity is resolved by asking the disk, because a clone that copies half
; a disk produces a target that mounts and lies.
; -----------------------------------------------------------------------------
clo_geom:
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, CL_BSPT             ; bank the caller's live geometry before
    call clo_gget               ; anything is aimed anywhere: a refusal below
                                ; must leave the machine as it found it, and
                                ; a raw transfer between here and the next
                                ; mount would otherwise use a TRIAL value

    mov bl, [es:CL_SRC]         ; is it a floppy at all? DL = the int 13h
    call clo_flop               ; drive, for AH=08h below
    jc .nodisk
    mov al, [es:CL_SRC]
    mov [disk_drive], al

    cmp byte [es:CL_MOK], 0
    je .probe
    mov bx, CL_SPT              ; it mounted: the BPB has answered, through
    call clo_gget               ; SPEC.md 18.2's one hostile pipeline
    or ax, ax                   ; (AX = the total, clo_gget's last word)
    jz .jio                     ; a mounted volume of no sectors is not one
    jmp .ok
.nodisk:                        ; (here, in the floppy test's jcc reach)
    mov ax, CERR_NODISK
    jmp .bad

    ; --- sectors per track, off the MEDIUM ---------------------------------
    ; One disk_read of CHS (0,0,N) for N in 18, 15, 9, first success wins:
    ; disk_read derives CHS from [disk_spt]/[disk_heads] and those are ours to
    ; set, so LBA N-1 with spt = N is exactly CHS (0,0,N). A candidate above
    ; what the drive can do costs three attempts and two controller resets to
    ; refuse, which is the whole of what the doomed reads cost.
.probe:
    inc byte [dsk_rah_busy]     ; NOTHING here touches SPEC.md 18.95's cache:
                                ; a hit would answer a geometry question out
                                ; of the PREVIOUS disk's RAM, and a fill would
                                ; key sectors read under a TRIAL geometry as
                                ; if the real one had read them
    mov si, 18
.spt:
    mov [disk_spt], si
    mov word [disk_heads], 2
    mov word [dsk_totsec], 0    ; no clamp: the extent is the thing being
    mov ax, si                  ; established, so there is nothing to clamp to
    dec ax
    call clo_read1
    jnc .gotspt
    sub si, 3                   ; 18 -> 15, tried at once...
    cmp si, 12
    ja .spt
    mov si, 9                   ; ...15 -> 12 -> 9 (`mov` keeps the compare's
    je .spt                     ; ZF), and 9 -> 6 is the end of the list
.probeno:
    dec byte [dsk_rah_busy]
.jio:
    jmp .io
.gotspt:
    mov [es:CL_SPT], si

    ; --- cylinders and heads, off the DRIVE --------------------------------
    ; int 13h AH=08h, which is what DOS's own FORMAT asks and what SPEC.md
    ; 18.96 reasons about: a ROM that will not answer is a 40-cylinder
    ; machine, measured on both GLaBIOS and the 27 Oct 1982 IBM ROM.
    ;
    ; **AH=08h SPENDS ES**, and ES is the job block here - so it is banked
    ; over the call along with BP, which some BIOSes also spend.
    mov di, 40                  ; DI = cylinders, and the answer for a ROM
    call clo_ask08              ; that will not say (clo_ask08 keeps DI)...
    mov cx, 2                   ; ...and CX = heads, AFTER the call and not
    jc .havedrv                 ; before it: on a refusal CX is whatever the
                                ; ROM left there, and a ROM that zeroes it
                                ; made CL_HDS 0. `mov` keeps the call's CF.
                                ; AX = the 10-bit cylinder count (1..1,024),
    mov di, ax                  ; BH = the ANSWER's DH
    or bh, bh                   ; heads = DH + 1, held to two: a floppy is
    jnz .havedrv                ; one head or two, and a ROM answering more
    dec cx                      ; is answering about something else
.havedrv:
    mov [es:CL_HDS], cx

    ; --- ...and then READ the last sector that answer claims ---------------
    ; A positive test (SPEC.md 18.96.1's rule: every test is a read
    ; SUCCEEDING). A 40-track disk in an 80-track drive answers AH=08h with
    ; 80 and cannot reach cylinder 79, so this is what catches it - and the
    ; LAST sector is asked for rather than the 360KB boundary because plenty
    ; of 40-track mechanisms step to 41 or 42 and none reaches 79.
    call clo_extent
    jnc .probeok
    cmp di, 40
    jbe .probeno2
    mov di, 40                  ; the other thing 9-sector media can be
    call clo_extent
    jnc .probeok
.probeno2:
    jmp short .probeno
.probeok:
    dec byte [dsk_rah_busy]
.ok:
    call clo_live               ; the live geometry IS the clone's from here
    clc                         ; to the re-mount the caller owes
    jmp short .out
.io:
    mov ax, CERR_IO
.bad:
    mov bx, CL_BSPT             ; put the caller's geometry back - dskw_fmt_
    call clo_gput               ; probe_x's .undo, and for its reason: a
                                ; refusal must not leave a trial value where
                                ; a raw transfer would read it
    stc
.out:
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; clo_flop - is volume BL a BIOS FLOPPY? (SPEC.md 18.7: DV_KIND is the
;            TRANSPORT and a BIOS unit below 80h is the MEDIUM - disk_mount's
;            own test, and fm_fmt_ok's)
; out: CF=0 it is, with BX = its row and DL = its int 13h unit; CF=1 it is not
; clobbers: BX DL, flags
;
; The cloner's own question and Uncompress To's (cmz_jfloppy, "may a missing
; part be ASKED for?"), which were two copies of it.
; -----------------------------------------------------------------------------
clo_flop:
    call COLD_SEG:dskf_dsk_vol_row
    jc .r
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .no
    mov dl, [bx+DV_UNIT]
    test dl, 0x80
    jz .r                       ; (`test` leaves CF = 0)
.no:
    stc
.r:
    ret

; -----------------------------------------------------------------------------
; clo_ask08 - int 13h AH=08h, the DRIVE's geometry, for unit DL (SPEC.md 18.96)
; out: CF=1 the ROM will not say (CX as the ROM left it); CF=0 with AX = the
;      cylinders, CL = the answer's CL (sectors per track in bits 5:0) and
;      BH = the answer's DH (heads - 1)
; clobbers: AX BX CX, flags - DX, DI, BP and ES come back
;
; **AH=08h SPENDS BX, CX, DX, DI and ES** - and BP on some BIOSes. ES is the
; job block in both callers, and DX is theirs: the answer's DH is banked in BH
; BEFORE the pop that hands the caller's DX back, because read after it, DH
; was the CALLER's and a 720KB target sized at one head slipped clo_fits.
; -----------------------------------------------------------------------------
clo_ask08:
    push dx
    push di
    push es
    push bp
    mov ah, 0x08
    int 0x13
    pop bp
    pop es
    pop di
    mov bh, dh
    pop dx
    jc .r
    mov ah, cl
    and ah, 0xC0                ; cylinder bits 9:8 live in CL bits 7:6...
    mov al, ch                  ; ...and the low eight in CH, UNSHIFTED - a
    push cx                     ; shift of the whole of AX moved them too and
    mov cl, 6                   ; sized an 80-cylinder disk at 2
    shr ah, cl                  ; AH -> those two bits as a count of 256s
    pop cx
    inc ax                      ; AX = cylinders
    clc                         ; (the shift left CF as its last bit out)
.r:
    ret

; -----------------------------------------------------------------------------
; clo_extent - is spt x heads x DI cylinders really there? (module internal)
; in:  DI = cylinders, ES = the claim with CL_SPT/CL_HDS set
; out: CF=0 with CL_TOT set and the last sector proven readable; CF=1 = it is
;      not there. Clobbers AX, flags.
; -----------------------------------------------------------------------------
clo_extent:
    push dx
    mov ax, [es:CL_SPT]
    mul word [es:CL_HDS]
    mul di                      ; DX:AX = the extent. A floppy is well under
    or dx, dx                   ; 65,536 sectors, so a carry out is a ROM
    jnz .no                     ; answering about a hard disk
    or ax, ax
    jz .no
    mov [es:CL_TOT], ax
    call clo_live
    dec ax
    call clo_read1
    jc .no
    pop dx
    ret
.no:
    pop dx
    stc
    ret

; -----------------------------------------------------------------------------
; clo_live - make the machine's live geometry the clone's (module internal)
; in:  ES = the claim
; out: nothing (all registers preserved) - and clo_asrc / clo_atgt / clo_aim
;      point [disk_drive] at the source, the target or AL first, clobbering AL
;
; Both ends of the copy are addressed with the SOURCE's geometry, because an
; exact clone writes the source's LBAs under the source's CHS mapping. The
; caller owes a re-mount afterwards, which re-derives everything off whatever
; BPB is now on the platter (SPEC.md 22.21.3).
; -----------------------------------------------------------------------------
clo_asrc:                       ; ...aimed at the SOURCE's drive first,
    mov al, [es:CL_SRC]         ; or the TARGET's, or AL's (clobbers AL): the
    jmp short clo_aim           ; three shapes every transfer starts with
clo_atgt:
    mov al, [es:CL_TGT]
clo_aim:
    mov [disk_drive], al
clo_live:
    push bx
    mov bx, CL_SPT
    call clo_gput
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_gget - the live geometry - [disk_spt], [disk_heads], [dsk_totsec] - into
;            three claim words at ES:BX. out: AX = the total. clobbers: AX
; clo_gput - ...and back, from ES:BX. Every register preserved
;
; SIX copies of this triple, both ways, for the source, the caller's bank and
; the image file's volume: CL_SPT, CL_BSPT and CL_FSPT each lead three words
; in this order (asserted below).
; -----------------------------------------------------------------------------
clo_gget:
    mov ax, [disk_spt]
    mov [es:bx], ax
    mov ax, [disk_heads]
    mov [es:bx+2], ax
    mov ax, [dsk_totsec]
    mov [es:bx+4], ax
    ret
clo_gput:
    push ax
    mov ax, [es:bx]
    mov [disk_spt], ax
    mov ax, [es:bx+2]
    mov [disk_heads], ax
    mov ax, [es:bx+4]
    mov [dsk_totsec], ax
    pop ax
    ret
%if CL_HDS - CL_SPT != 2 || CL_TOT - CL_SPT != 4 || CL_BHDS - CL_BSPT != 2 || CL_BTOT - CL_BSPT != 4 || CL_FHDS - CL_FSPT != 2 || CL_FTOT - CL_FSPT != 4
    %error "CL_SPT, CL_BSPT and CL_FSPT must each lead spt, heads, total in three words: clo_gget/clo_gput copy them as a block"
%endif

; -----------------------------------------------------------------------------
; clo_read1 - read LBA AX into dsk_secbuf (module internal)
; in:  AX = LBA, [disk_drive] and the geometry aimed
; out: CF as disk_read; every register but the flags preserved
; -----------------------------------------------------------------------------
clo_read1:
    push ax
    push bx
    push cx
    push es
    push ss                     ; dsk_secbuf is .lowbss (SPEC.md 2.1), so it
    pop es                      ; is reached through ES and never through DS -
                                ; and SS IS LOW_SEG, on every task (SPEC.md 1)
    mov bx, dsk_secbuf
    mov cx, 1
    call COLD_SEG:dskf_disk_read
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clo_size - how much of the disk does one trip move? (SPEC.md 18.99.4)
; in:  ES = the claim, CL_TOT known, CL_SRC/CL_TGT set to the same drive
; out: CF=0 with CL_BUFSG, CL_STAGE, CL_CHUNK and (maybe) the XMS block set;
;      CF=1 with AX = CERR_MEM. Clobbers AX, flags.
;
; **The buffer's base is rounded up to 32 paragraphs**, because SPEC.md 2.4
; wants every disk-visible base 512-byte aligned and mem_claim only promises
; a paragraph. dsk_runcap's own comment records what an unaligned one costs:
; its cap falls to a single sector and says nothing.
;
; XMS is tried for a SAME-DRIVE clone alone. A cross-drive clone needs no
; trips at all - both disks are in the machine - so a bigger buffer there buys
; int 13h calls and never a trip to the disk box, and staging every byte
; through a second copy to buy nothing is the wrong trade.
; -----------------------------------------------------------------------------
clo_size:
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, es                  ; the buffer: the claim's base plus the
    add ax, CL_HDRPARA          ; 1KB header, rounded UP to 32 paragraphs
    add ax, 31
    and ax, 0xFFE0
    mov [es:CL_BUFSG], ax
    mov bx, ax                  ; BX = it

    mov ax, 64                  ; the claim's end, in paragraphs: KB * 64,
    mul word [es:CL_KB]         ; and KB <= 640 so this cannot carry out
    mov cx, es                  ; ...through CX, an 8086 having no `add r16,
    add ax, cx                  ; sreg` (SPEC.md 1)
    sub ax, bx                  ; ...less the buffer's base
    jbe .nomem
    mov cl, 5
    shr ax, cl                  ; paragraphs -> sectors, 32 paragraphs each
    jz .nomem
    mov [es:CL_STAGE], ax       ; what the conventional buffer holds
    mov si, ax

    mov ax, [es:CL_TOT]         ; ...and a trip is that, or the whole disk
    cmp ax, si                  ; when that is smaller. clo_xmsup may raise it
    jbe .setchunk               ; to the whole disk once the TARGET is known
    mov ax, si
.setchunk:
    mov [es:CL_CHUNK], ax
    clc
    jmp short .out
.nomem:
    mov ax, CERR_MEM
    stc
.out:
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

%ifdef CLO_XMS
; -----------------------------------------------------------------------------
; clo_xmsup - one trip instead of many, if the store can pay for it (18.99.4)
; in:  ES = the claim, CL_TOT known and CL_TGT finally chosen
; out: nothing (AX clobbered, every other register preserved - its one
;      caller reloads AL next). It either upgrades the plan or leaves it
;      exactly as clo_size left it - there is no failure to report, because
;      the conventional plan is a working plan.
;
; **Asked HERE and not in clo_size**, because until the user has pressed Enter
; on the pick prompt the target is still the source's own drive and every
; clone looks like a same-drive one. Reserving 1,440KB of somebody's extended
; memory to find out they meant B: is the kind of thing that is invisible
; until the machine has 1,440KB and something else wants it.
;
; A cross-drive clone is skipped outright: both disks are in the machine, so
; a bigger buffer buys int 13h calls and never a trip to the disk box, and
; staging every byte through a second copy to buy nothing is the wrong trade.
;
; A 1.44MB disk wants 1,440KB up there, which any 286 with 2MB has and no 8088
; has at all - so this is exactly the machine-class question SPEC.md 41 says
; to ask of xm_caps and never of the CPU tier.
; -----------------------------------------------------------------------------
clo_xmsup:
    push bx
    push cx
    push dx
    push si
    push di
    call clo_same
    jne .out
    cmp byte [es:CL_XON], 0
    jne .out                    ; already upgraded: Enter twice is one plan
    mov ax, [es:CL_TOT]
    inc ax
    shr ax, 1                   ; sectors -> KB, rounded up
    mov di, ax                  ; DI = the whole disk, in KB
    call KERNEL_SEG:CLO_XCAPS   ; AX = free KB up there (0 on every 8088)
    cmp ax, di
    jb .out
    mov ax, 1024
    mul di                      ; DX:AX = the bytes wanted
    call KERNEL_SEG:CLO_XALLOC  ; DX:AX = the block's 32-bit linear base
    jc .out
    mov [es:CL_XLO], ax
    mov [es:CL_XHI], dx
    mov [es:CL_XKB], di
    mov byte [es:CL_XON], 1
    cmp word [es:CL_STAGE], CLO_XSEC ; the staging load is capped at
    jbe .setstage               ; XM_MAX_COPY (SPEC.md 41.12.2), which is 64
    mov word [es:CL_STAGE], CLO_XSEC ; sectors exactly. A bigger conventional
.setstage:                      ; buffer buys nothing on this path and one
                                ; copy over the cap would be refused outright
    mov ax, [es:CL_TOT]
    mov [es:CL_CHUNK], ax       ; ONE trip: read it all, swap once, write it
.out:                           ; all - the buffer above merely stages it
    jmp clo_x5
%endif ; CLO_XMS

; =============================================================================
; THE IMAGE END (SPEC.md 18.99.8)
;
; A clone has two ends and until now both were disks. One of them may now be
; an ORDINARY FILE on an ordinary volume: `Clone Disk...` offers `IMG` in its
; target ring, and `Write Img...` runs the same machinery with the ends the
; other way round. Nothing about the middle changed - the buffer, the chunk
; loop, the progress bar and the geometry are the same code.
;
; **The two ends are addressed completely differently, and that is the whole
; of the difficulty.** The disk end is RAW SECTORS: it needs `[disk_drive]`
; and the three geometry words and nothing else, and it deliberately does not
; mount. The file end is the FILE API: it needs the volume mounted, its
; directory current, and those same three geometry words describing IT. So the
; two ends want one set of words to say two different things, and the loop
; alternates between them.
;
; It is a BANK, not a re-mount. `clo_fbank` saves the file volume's live
; geometry once and `clo_dend` / `clo_fend` swap the four values around each
; transfer; the mount, the current directory, the FAT window and the sector
; cache are all untouched, because the FAT window is per-volume (SPEC.md
; 18.8.1) and every other derived word - `[dsk_unit]`, `[dsk_base]`,
; `[dsk_cylmax]`, `[dsk_vkind]` - is re-derived from `[disk_drive]` inside
; `dsk_xfer` on every call. A re-mount per chunk would be correct and would
; cost a directory read each way.
;
; **The file may not live on the disk being written.** For `Write Img` that is
; the difference between a working feature and one that destroys the file it
; is reading halfway through; for `Clone Disk` it would be writing the image
; onto the disk it is imaging. One compare, refused as CERR_SMALL's sibling.
; =============================================================================

; -----------------------------------------------------------------------------
; clo_fbank - bank the FILE volume's geometry, once (module internal)
; in:  ES = the claim; the file's volume is mounted and current, because the
;      dialog that named the file left it that way
; out: CF=0 with CL_FDRV/CL_FSPT/CL_FHDS/CL_FTOT and CL_SPC filled;
;      CF=1 with AX = CERR_*. Clobbers AX, flags.
; -----------------------------------------------------------------------------
clo_fbank:
    push bx
    push dx
    mov al, [disk_drive]
    mov [es:CL_FDRV], al
    mov ax, [dsk_cwd]           ; ...AND THE FOLDER. [dsk_cwd] is one global
    mov [es:CL_FCLUS], ax       ; word for the whole machine, and `Write Img`
                                ; puts a KEYSTROKE between the dialog that
                                ; chose the file and the Enter that reads it -
                                ; the UI mounts what it likes in between, and
                                ; a name looked up in the wrong directory is
                                ; FERR_NOENT, which reaches the user as
                                ; 'Disk error' two seconds in. Measured,
                                ; exactly that way round
    mov bx, CL_FSPT
    call clo_gget
    call COLD_SEG:dwf_dskw_dfree    ; DX:AX = free bytes, BX = SECTORS per
    jc .bad                         ; cluster (SPEC.md 18.4)
    or bx, bx
    jz .bad
    mov [es:CL_SPC], bx
    jmp short .out
.bad:
    mov ax, CERR_NODISK
    stc
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_fend - point the shared geometry words at the FILE end (module internal)
; in:  ES = the claim
; out: nothing (all registers preserved)
;
; The disk end has no routine of its own: `clo_rd` and `clo_wr` already set
; `[disk_drive]` and call `clo_live` as their first act, because they did that
; before there was a file end at all. This is the OTHER half of that pair, and
; it is a BANK rather than a re-mount: the current directory, the FAT window
; and the sector cache are per-volume or untouched, and every other derived
; word is re-derived from `[disk_drive]` inside `dsk_xfer` on every call.
; Every chunk crosses the pair twice.
; -----------------------------------------------------------------------------
clo_fend:
    push ax
    mov al, [es:CL_FDRV]
    mov [disk_drive], al
    mov ax, [es:CL_FCLUS]       ; ...and the folder, which a raw transfer on
    mov [dsk_cwd], ax           ; the disk end does not touch but which one
                                ; word is cheaper to restore than to reason
                                ; about (clo_imgrun's own note)
    pop ax
    push bx
    mov bx, CL_FSPT
    call clo_gput
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_fput - put the run in hand into the image file (module internal)
; in:  ES = the claim, CL_HELD sectors at CL_BUFSG, CL_FOFF = where they go
; out: CF=0 with CL_FOFF advanced; CF=1 with AL = CERR_*
; clobbers: AX, flags
;
; The FIRST chunk is a `dskw_write` and every one after it a `dskw_append`,
; which is fcp_xfer's own shape (SPEC.md 22.5) and for its reason: creating
; the file empty and appending to it costs a directory write and a FAT flush
; for nothing. The OFFSET is what tells the two apart, so nothing has to
; remember which chunk this is.
;
; No sector 0 is held back here. SPEC.md 18.99.2's rule is about a disk that
; must not claim to be the source; a half-written file is a half-written file
; and its own length says so.
; -----------------------------------------------------------------------------
clo_fput:
    push bx
    push cx
    push dx
    push si
    push di
    call clo_fend
    mov ax, [es:CL_HELD]
    mov cl, 9
    shl ax, cl                  ; AX = the chunk in bytes, which fits 16 bits
    mov di, ax                  ; because clo_imgsize caps it at CLO_IMGMAX
    mov ah, 1                   ; AH = "this is an append"...
    mov cx, [es:CL_FOFF]
    or cx, [es:CL_FOFF+2]
    jnz .isapp
    xor ah, ah                  ; ...unless nothing has been written yet
.isapp:
    mov cx, di                  ; CX = the count, DX:CX for the write
    xor dx, dx
    mov si, clo_fnbuf
    xor bx, bx                  ; ES:BX = the buffer at offset 0
    push es
%ifdef KERN_BIG
    mov di, [es:CL_FTOK]
%endif
    mov es, [es:CL_BUFSG]
    or ah, ah
    jnz .append
    call COLD_SEG:dwf_dskw_write
    jmp short .did
.append:
%ifdef KERN_BIG
    mov al, WSQF_HELD           ; ONE HELD STREAM (SPEC.md 18.4.9, 18.99.8):
    call COLD_SEG:dwf_dskw_write_seq    ; no lookup and no walk a chunk, no
                                ; FAT or directory write until clo_imgrun's
                                ; close. Nothing between two chunks mounts
                                ; anything - the disk end is a bank - so the
                                ; FAT dirt simply stays in the window
%else
    call COLD_SEG:dwf_dskw_append
%endif
.did:
    pop es
%ifdef KERN_BIG
    mov [es:CL_FTOK], di        ; (`mov`: CF stays the answer)
%endif
    jc .bad
    call clo_fadv
    clc
    jmp short .out
.bad:
    call clo_ferr               ; AL = CERR_*, out of the FERR_* the file API
    stc                         ; answered
.out:
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; clo_fadv - CL_FOFF += CL_HELD sectors (module internal)
; in:  ES = the claim
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
clo_fadv:
    push ax
    push dx
    mov ax, 512
    mul word [es:CL_HELD]       ; DX:AX = the bytes
    add [es:CL_FOFF], ax
    adc [es:CL_FOFF+2], dx
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clo_fget - read the next chunk OUT of the image file (module internal)
; in:  ES = the claim, CL_FOFF = where to read from
; out: CF=0 with CL_HELD = the sectors delivered (0 = the file is finished);
;      CF=1 with AL = CERR_*
; clobbers: AX, flags
;
; The offset is still the whole argument (SPEC.md 18.4.4): the WRITE half
; between two reads walks directories and the FAT, so a resume token living in
; the kernel would be destroyed by the very work it is feeding. The cursor is
; the CALLER's, in the claim's header, and only caches the walk - READ_SEQ
; re-seeds it from the name whenever the media generation moved (SPEC.md
; 18.4.8.1).
; -----------------------------------------------------------------------------
clo_fget:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    call clo_fend
    mov ax, [es:CL_CHUNK]
    mov cl, 9
    shl ax, cl
    mov cx, ax                  ; CX = the capacity, in bytes
    mov ax, [es:CL_FOFF]
    mov dx, [es:CL_FOFF+2]
    mov di, [es:CL_BUFSG]
    mov si, clo_fnbuf
    push es
    mov bp, es                  ; BP = the claim, whose header holds the
    mov es, di                  ; cursor: clo_zerohdr blanks it per job
    xor bx, bx                  ; ES:BX = the buffer
    CLO_RSEQ bp, CL_FCUR        ; DX:AX = the bytes delivered
    pop es
    jc .bad
    add [es:CL_FOFF], ax
    adc [es:CL_FOFF+2], dx
    mov cl, 9
    shr ax, cl                  ; a chunk is under 64KB, so DX is 0 here and
    mov [es:CL_HELD], ax        ; the shift is the whole answer. A tail that
    clc                         ; is not a whole sector is DROPPED, which a
    jmp short .out              ; validated image cannot have (SPEC.md 18.99.8)
.bad:
    call clo_ferr
    stc
.out:
    pop bp                      ; the stream reader's cursor segment (18.4.8.1)
    jmp clo_x5                  ; ...then the shared epilogue, below clo_cat

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; clo_rseq - CLO_RSEQ's body (module internal; SPEC.md 18.4.8.1)
; in:  DS:SI -> the name, ES:BX -> the buffer, CX = the capacity, DX:AX = the
;      offset (dwf_dskw_read_at's), BP:DI -> the 16-byte cursor
; out: dwf_dskw_read_at's: CF=0 DX:AX = the bytes; CF=1 AX = FERR_*
; preserves: BX, CX, SI, DI, BP, DS, ES
;
; THE OFFSET STAYS THE CALLER'S. A cursor whose offset is not DX:AX is
; re-seeded there, so a seek, a cut-out's rewind, a new part at 0 and a retry
; need nothing from the caller. It names no file, so a caller moving to a new
; one zeroes it (cmz_jrst, the compressor's streamed start, clo_zerohdr).
; -----------------------------------------------------------------------------
clo_rseq:
    push es
    mov es, bp                  ; ES:DI = the cursor
    cmp ax, [es:di+FSEQ_OFF]
    jne .seed
    cmp dx, [es:di+FSEQ_OFF+2]
    je .go
.seed:
    mov word [es:di+RSQ_GEN], 0 ; not seeded: the next call stats the name
    mov [es:di+FSEQ_OFF], ax    ; and walks once, to here
    mov [es:di+FSEQ_OFF+2], dx
.go:
    pop dx                      ; DX:BX = the buffer (the caller's ES)
    push dx
    call COLD_SEG:dwf_dskw_read_seq
    pop es
    ret
%endif

; -----------------------------------------------------------------------------
; clo_ferr - a FERR_* from the file API, as a CERR_* (module internal)
; in:  AX = FERR_*
; out: AL = CERR_*
; clobbers: AX, flags
; -----------------------------------------------------------------------------
clo_ferr:
    cmp ax, FERR_FULL
    je .full
    cmp ax, FERR_WPROT
    je .wprot
    mov al, CERR_IO             ; everything else the file layer can answer -
    ret                         ; a bad name, a protected file, a short read -
.full:                          ; reaches the user as 'Disk error', which is
    mov al, CERR_FULL           ; what it is from here
    ret
.wprot:
    mov al, CERR_WPROT
    ret

; -----------------------------------------------------------------------------
; clo_nextvol - the floppy volume after AL, wrapping (module internal)
; in:  AL = a volume index
; out: AL = the next DVK_BIOS floppy volume, which is AL itself when it is the
;      only one. Clobbers AX (the output), flags.
; -----------------------------------------------------------------------------
clo_nextvol:
    push bx
    push cx
    push dx
    mov dl, al                  ; DL = where we started
    mov cx, DVOL_MAX + 1        ; ...one more stop than there are volumes: the
                                ; image file is on the ring (SPEC.md 18.99.8)
    cmp al, CLE_IMG
    jne .step
    mov al, 0xFF                ; from IMG, wrap to volume 0
.step:
    inc al
    cmp al, DVOL_MAX
    jb .test
    mov al, CLE_IMG             ; past the last volume comes the image file,
    jmp short .done             ; and it is always offerable - what it lands
                                ; on is the Save dialog's business
.test:
    cmp al, dl
    je .done                    ; all the way round: AL is the only floppy
    push ax
    mov bl, al
    call COLD_SEG:dskf_dsk_vol_row
    jc .next
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .next
    test byte [bx+DV_UNIT], 0x80
    jnz .next
    pop ax
    jmp short .done
.next:
    pop ax
    loop .step
.done:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_key - one keystroke, and the disk work it asks for (SPEC.md 22.21.2)
; in:  AL = ascii, AH = scan; ES free; gfx lock held, UI task
; out: AL = CLA_*, AH = CERR_* when AL is CLA_ERR, BL/BH = the two volumes
;      when it is CLA_DONE; CF = 0 ALWAYS (SPEC.md 18.99.7)
; clobbers: AX, BX, flags
;
; **The transfer happens HERE**, inside the keystroke, and that is not a
; layering slip: the disk is the UI task's (SPEC.md 18), a Disk window's
; callbacks have held the gfx lock since before the prompt went up (SPEC.md
; 22), and every other file operation in files.inc already runs its I/O under
; it. So Enter on CLS_SRC IS the read pass and Enter on CLS_TGT IS the write
; pass, the machine freezes for as long as the pass takes, and SPEC.md 12.8's
; widget is armed with the pass's byte count so the freeze has a bar on it.
;
; Esc is answered at every step and answered first - "cancel at any point
; input is required", which on a machine that cannot interrupt an int 13h is
; every point there is.
; -----------------------------------------------------------------------------
clo_key:
    push cx
    push dx
    push si
    push di
    push es
    mov es, [clo_seg]
    cmp al, 27
    je .stop                    ; Esc, at every step and answered first
    mov dl, [es:CL_STEP]
    cmp dl, CLS_PICK
    je .pick
    cmp dl, CLS_SRC
    je .src
    cmp dl, CLS_TGT
    je .jtgt
    cmp dl, CLS_WIMG
    je .wimg
    cmp dl, CLS_NOSRC
    je .nosrc
                                ; CLS_SAME: Enter re-checks, Y writes anyway,
                                ; and every other key does nothing at all -
                                ; SPEC.md 22's asymmetry where it matters most
    cmp al, 'y'
    je .force
    cmp al, 'Y'
    je .force
    cmp al, 13
    jne .linej2
    mov byte [es:CL_STEP], CLS_TGT
.jtgt:
    jmp .tgt                    ; NEAR: CLS_NOSRC's answers sit between here
                                ; and it now (SPEC.md 18.99.3)
.stop:                          ; Esc, HERE: the top's `je` is short
    mov al, CLA_STOP
    jmp .out

    ; `Write Img...`'s confirmation (SPEC.md 18.99.8): Enter runs the whole
    ; operation here and now - the file and the disk are both in the machine,
    ; so nothing has to be swapped and there is nothing to ask between the
    ; chunks. Esc was answered above and every OTHER key does nothing at all,
    ; which is what the status line promises and what SPEC.md 22.12 wants of
    ; the last line before an act with no undo: exactly two keys mean
    ; anything, and a mistyped third is not one of them.
.wimg:
    cmp al, 13
    jne .line
    call clo_imgrun
    jc .err
    jmp short .done

.src:
    cmp al, 13
    jne .line
    cmp word [es:CL_NEXT], 0    ; trip 1 has read nothing, so CL_BOOT is still
    je .srcgo                   ; zeroes and there is nothing to compare with
    call clo_issrc              ; ...but from trip 2 on the TARGET's LBA 0 is
    jc .srcwarn                 ; deliberately still unwritten (SPEC.md
                                ; 18.99.2), so the check answers here exactly
                                ; as well as it does at the target prompt.
                                ; Without it a user who forgets to swap reads
                                ; the target and writes it straight back, and
                                ; the operation reports success (SPEC.md
                                ; 18.99.3)
.srcgo:
    call clo_rd
    jc .err
    mov byte [es:CL_STEP], CLS_TGT
    jmp short .line
.srcwarn:
    mov byte [es:CL_STEP], CLS_NOSRC
.linej2:                        ; (a free trampoline for CLS_SAME's `jne`)
    jmp short .line

    ; CLS_NOSRC: CLS_SAME's three answers on the other prompt (SPEC.md
    ; 18.99.3) - Enter re-checks, Y reads anyway, Esc stops - so the habitual
    ; key is the safe one here too.
.nosrc:
    cmp al, 'y'
    je .srcgo
    cmp al, 'Y'
    je .srcgo
    cmp al, 13
    jne .line
    mov byte [es:CL_STEP], CLS_SRC
    jmp short .src

.tgt:
    cmp al, 13
    jne .line
    call clo_issrc              ; CF=0 = the disk in the drive IS the source
    jc .force                   ; (SPEC.md 18.99.3), so say so and write
    mov byte [es:CL_STEP], CLS_SAME  ; nothing
    jmp short .line
.force:
    call clo_wr
    jc .err
    mov ax, [es:CL_NEXT]
    cmp ax, [es:CL_TOT]
    jb .more
    call clo_fin                ; the last run is down, so sector 0 goes now
    jc .err
.done:
    mov bl, [es:CL_SRC]
    mov bh, [es:CL_TGT]
    mov al, CLA_DONE
    jmp short .out
.more:
    mov byte [es:CL_STEP], CLS_SRC
.line:
    mov al, CLA_LINE
    jmp short .out
.err:
    mov ah, al                  ; AL = CERR_* on the way out of every refusal
    mov al, CLA_ERR             ; and every pass
.out:
    clc                         ; **EVERY EXIT IS AN ANSWER, SO CF IS CLEAR**
                                ; (SPEC.md 18.99.7). The resident caller reads
                                ; CF=1 as the OTHER alphabet - CLONE.DRV could
                                ; not be re-read, AX = a bare CERR_* - and
                                ; repacks it. `.err` arrives from a `jc` and a
                                ; `mov` writes no flag, so CLA_ERR went back
                                ; with CF still set and was repacked as error
                                ; 83h, which toast_say refuses as out of its
                                ; table: every failed clone and Write Img
                                ; ended its mode in SILENCE (SPEC.md 18.99.10).
                                ; `.line` is reached through a `jne` after a
                                ; `cmp al, 13`, so a key below 13 leaked the
                                ; same carry out of an ordinary prompt
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

    ; CLS_PICK's answers, AFTER the exit: the ladder's other five targets
    ; are then in a short jcc's reach (size pass 8)
.pick:
    cmp al, ' '
    jne .pickgo
    mov al, [es:CL_TGT]
    call clo_nextvol            ; ...which walks THROUGH the image file
    mov [es:CL_TGT], al         ; (SPEC.md 18.99.8), so IMG is one more stop
.linej:                         ; ...and the two `jne` around it: NASM spelt each
    jmp .line                   ; as a five-byte inverted pair (an 8086 has no
                                ; near `jcc`) and from here they are two, with
                                ; nothing added at all - this jump was here
                                ; already.  -6 on CLONE.DRV
.pickgo:
    cmp al, 13
    jne .linej
    cmp byte [es:CL_TGT], CLE_IMG
    jne .picdisk
    ; The image target wants a NAME (SPEC.md 18.99.8): the Save box, opened
    ; from here through the far door, with the RESIDENT fm_img_done_x as its
    ; completion proc - fdlg_open dispatches a kernel window's proc as a near
    ; call into `.cold`, which an image cannot be. SI is the window: fm_onkey_x
    ; was handed it and nothing on the way here moves it. The mode stays armed
    ; across the box; a cancelled one calls nothing back (SPEC.md 38.2), so the
    ; user comes back to the pick prompt they left. CF=1 (refused - one is up
    ; already) cannot happen from a keystroke this window owns, and the prompt
    ; stands either way.
    mov bx, si
    mov si, clo_s_defimg        ; the default, staged where the box reads it
    mov di, fm_hdrbuf           ; (DS); fdlg_setname copies it before anything
    call cmz_cs13               ; else can stage into fm_hdrbuf
    mov si, fm_hdrbuf
    mov di, fm_img_done_x
    mov al, 1                   ; the SAVE form
    call COLD_SEG:fdf_fdlg_open
    jmp .line
.picdisk:
    call clo_fits               ; SPEC.md 18.99.6, asked HERE and not at
    jc .err                     ; CLV_START: the target is not chosen until
%ifdef CLO_XMS
    call clo_xmsup              ; this key, and neither is the trip count
%endif
    call clo_same
    je .samedrv
    call clo_both               ; two drives: the WHOLE operation, here, with
    jc .err                     ; no prompt between the halves - no disk has
    jmp short .done             ; to move, so there is nothing to ask
.samedrv:
    mov byte [es:CL_STEP], CLS_SRC
    jmp short .line


; -----------------------------------------------------------------------------
; clo_saved - the Save dialog answered: image this disk into that file
; in:  ES free; [clo_seg] live with CL_STEP = CLS_PICK and CL_TGT = CLE_IMG,
;      [fdlg_name] the box's answer, the FILE's volume mounted and current;
;      gfx lock held
; out: AL = CLA_*, AH = CERR_* when it is CLA_ERR (clo_key's answers exactly,
;      because the caller hands them to the same place); CF = 0 always
; clobbers: AX, BX, flags
;
; The whole operation, in one call, for the cross-drive clone's reason: both
; ends are in the machine, so nothing has to be swapped and there is nothing
; to ask between the chunks.
; -----------------------------------------------------------------------------
clo_saved:
    push cx
    push dx
    push si
    push di
    push es
    call clo_fnget              ; the box's answer, where the file API reads it
    mov es, [clo_seg]
    mov byte [es:CL_END], 1     ; the TARGET is a file. CL_FOFF is still
                                ; clo_plan's zero: only clo_imgrun moves it,
                                ; and every way out of that ends the claim
    call clo_fbank
    jc .err
    call clo_fsame              ; the image may not live on the disk being
    jc .err                     ; imaged (SPEC.md 18.99.8)
    call clo_froom
    jc .err
    call clo_imgsize
    jc .err
    call clo_imgrun
    jc .err
    mov al, CLA_DONE
    mov bl, [es:CL_SRC]
    mov bh, [es:CL_TGT]
    jmp short .out
.err:
    mov ah, al
    mov al, CLA_ERR
.out:
    clc                         ; an ANSWER, never the other alphabet - see
    pop es                      ; clo_key's own exit, and the resident .save
    pop di                      ; half that repacks a CF=1
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; clo_wimg - the Open dialog answered: size that file and ask (SPEC.md 18.99.8)
; in:  AL = the target volume (the acting window's), **BX:CX** = the file's
;      size as the dialog reported it, [fdlg_name] the box's answer, the
;      FILE's volume mounted and current; gfx lock held
;
; BX:CX and not DX:CX, which is what the dialog hands its callback and what
; every other 32-bit count in this tree uses (SPEC.md 18.4.1): **DL is the
; VERB**. `mov dl, CLV_WIMG` over the high half of a size turns 737,280 into
; 344,064, which is not one of the four layouts and so refuses a perfectly
; good 720KB image - a wrong answer that looks like a deliberate one.
; out: CF=0 with [clo_seg] published and CL_STEP = CLS_WIMG; CF=1 with
;      AX = CERR_* and nothing claimed
; clobbers: AX (the output), flags
;
; **The GEOMETRY comes from the SIZE, and that is the whole validation.** The
; disk in the drive may be blank, so there is nothing to probe; what a raw
; write needs is a sectors-per-track and a head count, and the only honest
; source for them is the image itself. clo_imgtab pins the four
; standard layouts (SPEC.md 18.96), so a file whose length is one of those
; four is a disk image and a file whose length is not is CERR_NOTIMG - which
; also refuses, for free, every ordinary document somebody picks by mistake.
; -----------------------------------------------------------------------------
clo_wimg:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call clo_fnget              ; the box's answer, where the file API reads
                                ; it - CX is the size and comes back intact
    mov dx, bx                  ; ...and DX:CX from here on, which is what
                                ; clo_geom_img reads and what the rest of the
                                ; tree means by a 32-bit count
    push ax                     ; the target volume, across the claim

    cmp word [clo_seg], 0
    jne .busy
    call clo_plan               ; CF=1 with AX = CERR_MEM; CF=0 with ES = the
    jc .fail                    ; claim and [clo_seg] live
    pop ax
    push ax
    mov es, [clo_seg]
    mov ah, al
    mov [es:CL_SRC], ax         ; ...both, so clo_free and the verdict have a
    mov byte [es:CL_END], 2     ; volume to name either way
    mov byte [es:CL_STEP], CLS_WIMG ; (CL_FOFF: clo_plan's zero)

    call clo_geom_img           ; DX:CX -> CL_SPT/CL_HDS/CL_TOT, or CERR_NOTIMG
                                ; (DX:CX survives clo_plan, which preserves
                                ; every register but DX - and DX is re-read
                                ; from the stack below, not from clo_plan)
    jc .freefail
    call clo_size               ; CL_BUFSG / CL_STAGE / CL_CHUNK, which the
    jc .freefail                ; clone side gets from CLV_START and this side
                                ; has to ask for itself: there was no probe
                                ; here, so nothing has sized the buffer yet
    call clo_fbank
    jc .freefail
    call clo_fsame
    jc .freefail
    call clo_imgsize
    jc .freefail
    pop ax
    jmp short .out
.busy:
    mov ax, CERR_BUSY           ; ...said as itself and not as 'Out of
    jmp short .fail             ; memory' (SPEC.md 22.21.2)
.freefail:
    call clo_free               ; (every register preserved)
.fail:
    pop bx                      ; the banked volume, discarded
    stc
.out:
    jmp clo_x6                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; clo_geom_img - which standard floppy is a file of DX:CX bytes? (18.99.8)
; in:  DX:CX = the file's size, ES = the claim
; out: CF=0 with CL_SPT/CL_HDS/CL_TOT filled - and the LIVE geometry left
;      exactly as it was, which is the file volume's; CF=1 with AX =
;      CERR_NOTIMG. Clobbers AX, flags.
;
; The four layouts are THIS IMAGE's own (size, spt) pairs, read through CS.
; They were dskw_fmt_tab's rows, read through DS from `.text` - 56 resident
; bytes kept there for this one reader, since two modules may not far-call
; each other (only one of them need be loaded). The formatter's table is in
; FORMAT.DRV now, and these sixteen bytes are the whole of what this needed.
; -----------------------------------------------------------------------------
clo_geom_img:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, 4
    mov si, clo_imgtab
.row:
    mov ax, [cs:si]             ; sectors -> bytes, as DX:AX
    push dx
    push cx
    mov bx, dx
    mov dx, 512
    mul dx                      ; DX:AX = this layout's whole size
    pop cx
    cmp ax, cx
    jne .next
    cmp dx, bx
    jne .next
    pop dx
    mov ax, [cs:si+2]
    mov [es:CL_SPT], ax
    mov word [es:CL_HDS], 2     ; every row in the table is double-sided, and
    mov ax, [cs:si]             ; dskw_format_x hard-codes the same 2
    mov [es:CL_TOT], ax
    clc                         ; ...and THE LIVE WORDS ARE NOT TOUCHED, which
                                ; cost a debugging session: this runs while the
                                ; FILE's volume is the current one, clo_fbank
                                ; banks those words two calls later, and an
                                ; image whose geometry is not the file volume's
                                ; then had the file read with the wrong CHS.
                                ; A 720KB image on a 1.44MB disk read 9
                                ; sectors of every 18 and wrote them
                                ; CONSECUTIVELY, so the target was plausible
                                ; rather than empty. The live words belong to
                                ; whichever END is being touched and both ends
                                ; set them at the moment of use - clo_live in
                                ; clo_rd/clo_wr, clo_fend on the file side -
                                ; so nothing here needs to
    jmp short .out
.next:
    pop dx
    add si, 4
    dec di
    jnz .row
    mov ax, CERR_NOTIMG
    stc
.out:
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

clo_imgtab:                     ; (sectors, sectors per track), SPEC.md 18.96
    dw 2880, 18                 ; 1.44MB
    dw 2400, 15                 ; 1.2MB
    dw 1440, 9                  ; 720KB
    dw 720, 9                   ; 360KB

; -----------------------------------------------------------------------------
; clo_fsame - is the image on the very disk it is about to be written to?
; in:  ES = the claim, CL_FDRV and the disk end both set
; out: CF=0 = they are different volumes; CF=1 with AX = CERR_SMALL... no:
;      AX = CERR_SAME
; clobbers: AX, flags
;
; **This is the one refusal the feature cannot do without.** Writing an image
; onto the volume the image FILE lives on destroys the file halfway through
; reading it; imaging a disk onto itself would be writing the picture into the
; thing being pictured. Either way the answer is the same and it is a REFUSAL
; rather than a warning, because unlike SPEC.md 18.99.3's identity check this
; one is not a guess: two volume indices are equal or they are not.
;
; It gets a message of its own - 'Pick another disk' - and that is eighteen
; resident bytes spent deliberately. It first reported CERR_NODISK, which the
; table already carried, and 'No disk' is what a user sees after saving into
; the folder the dialog OPENED IN: SPEC.md 38.10 remembers the application's
; last folder, and for a Disk window that is very often the disk being
; imaged. A wrong message on the commonest refusal is worth a string.
; -----------------------------------------------------------------------------
clo_fsame:
    push bx
    mov bl, [es:CL_SRC]
    cmp byte [es:CL_END], 2
    jne .have
    mov bl, [es:CL_TGT]
.have:
    cmp bl, [es:CL_FDRV]
    je .no
    pop bx
    clc
    ret
.no:
    pop bx
    mov ax, CERR_SAME
    stc
    ret

; -----------------------------------------------------------------------------
; clo_froom - will the image FIT on the volume it is going to? (18.99.8)
; in:  ES = the claim, the file volume current
; out: CF=0; CF=1 with AX = CERR_FULL. Clobbers AX, flags.
;
; fcp_room's argument (SPEC.md 22.5.2) at the only moment it can be made: the
; file volume is current, so the check costs no switch of its own, and it is
; before anything has been created. A 1.44MB image wants 1.44MB, and the disk
; it is going on is the user's problem rather than a surprise 90% of the way
; through a two-minute operation.
; -----------------------------------------------------------------------------
clo_froom:
    push bx
    push cx
    push dx
    push si
    call clo_fend
    call COLD_SEG:dwf_dskw_dfree    ; DX:AX = free bytes
    jc .bad
    mov si, dx
    mov cx, ax                  ; SI:CX = free
    mov ax, 512
    mul word [es:CL_TOT]        ; DX:AX = what the image needs
    cmp si, dx
    ja .ok
    jb .bad
    cmp cx, ax
    jb .bad
.ok:
    clc
    jmp short .out
.bad:
    mov ax, CERR_FULL
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_imgsize - the chunk, for an operation with a FILE at one end (18.99.8)
; in:  ES = the claim, CL_STAGE (the buffer's capacity) and CL_SPC known
; out: CF=0 with CL_CHUNK set; CF=1 with AX = CERR_MEM
; clobbers: AX, flags
;
; TWO caps that clo_size does not have. `dskw_append` takes a 16-bit byte
; count, so a chunk is at most CLO_IMGMAX sectors; and both it and
; `dskw_read_at` want a whole number of CLUSTERS, which is what makes the
; resume point exact (SPEC.md 18.4.4). So the buffer's capacity is trimmed to
; 127 sectors and then DOWN to a multiple of the volume's cluster - and a
; buffer that cannot hold one whole cluster cannot do this at all.
; -----------------------------------------------------------------------------
clo_imgsize:
    push bx
    push dx
%ifdef CLO_XMS
    mov byte [es:CL_XON], 0     ; the store is no use here: the file API reads
                                ; and writes conventional memory, and there is
                                ; no TRIP to save (SPEC.md 18.99.4) - both
                                ; ends are in the machine at once
%endif
    mov ax, [es:CL_STAGE]
    cmp ax, CLO_IMGMAX
    jbe .cap
    mov ax, CLO_IMGMAX
.cap:
    xor dx, dx
    mov bx, [es:CL_SPC]
    or bx, bx
    jz .nomem
    div bx                      ; AX = whole clusters that fit
    or ax, ax
    jz .nomem
    mul bx                      ; ...and back to sectors, now a multiple
    mov [es:CL_CHUNK], ax
    clc
    jmp short .out
.nomem:
    mov ax, CERR_MEM
    stc
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_imgrun - the whole operation, with a FILE at one end (SPEC.md 18.99.8)
; in:  ES = the claim, CL_END 1 or 2, everything planned
; out: CF=0; CF=1 with AL = CERR_*
; clobbers: AX, flags
;
; clo_both's twin, and the only difference is which end each half talks to.
; No prompt between the chunks, because nothing has to move; no sector 0 held
; back on the FILE side, because a file's length is what says it is finished.
; -----------------------------------------------------------------------------
clo_imgrun:
    push bx
    push cx
    mov al, [es:CL_FDRV]        ; STAND WHERE THE FILE IS, once, before
    mov bx, [es:CL_FCLUS]       ; anything reads or writes it. Free when the
    call COLD_SEG:fcpf_fcp_goto ; volume has not moved - fcp_goto re-aims
    jc .badgo                   ; [dsk_cwd] and returns - and a real remount
                                ; when it has, which is what makes the global
                                ; snapshot describe the file's volume again
    cmp byte [es:CL_END], 2
    je .write
.image:                         ; disk -> file
    call clo_rd
    jc .out
    call clo_fput
    jc .out
    mov cx, [es:CL_NEXT]
    cmp cx, [es:CL_TOT]
    jb .image
%ifdef KERN_BIG
    xor cx, cx                  ; THE CLOSE: the image's link, FAT and size,
    call COLD_SEG:dwf_dskw_write_seq    ; once - the last clo_fput left the
    jnc .out                    ; file end standing
    call clo_ferr
    stc
%endif
    jmp short .out
.write:                         ; file -> disk
    call clo_fget
    jc .out
    cmp word [es:CL_HELD], 0
    je .wdone                   ; the file ran out early: what is on the disk
                                ; is what the file had, and sector 0 is still
                                ; unwritten (SPEC.md 18.99.2)
    call clo_wpos               ; CL_HLBA/CL_NEXT, from the offset the read
    xor bl, bl                  ; ...and the KEEP of sector 0, which on this
    mov ax, [es:CL_HLBA]        ; side has to be asked for here: clo_rd never
    call clo_boot0              ; runs, so clo_move's own call never fires and
                                ; clo_fin would put 512 zero bytes down as the
                                ; boot sector - a disk that will not mount, at
                                ; the end of an operation that otherwise
                                ; worked (SPEC.md 18.99.2)
    call clo_wr
    jc .out
    mov cx, [es:CL_NEXT]
    cmp cx, [es:CL_TOT]
    jb .write
.wdone:
    mov ax, [es:CL_FOFF]        ; NOT ONE BYTE was read, so nothing was
    or ax, [es:CL_FOFF+2]       ; written and no boot sector is owed: CL_BOOT
    jz .empty                   ; is still clo_zerohdr's 512 zeroes, and
    call clo_fin                ; clo_fin would lay THOSE down as LBA 0 and
.out:                           ; the verdict would call it 'Wrote A:'
    pop cx                      ; (SPEC.md 18.99.8)
    pop bx
    ret
.badgo:
    mov al, CERR_NODISK         ; the file's volume will not mount any more -
    stc                         ; the disk was swapped, or it failed
    jmp short .out
.empty:
    mov al, CERR_NOTIMG         ; a file of no bytes is not a picture of a
    stc                         ; floppy either
    jmp short .out

; -----------------------------------------------------------------------------
; clo_wpos - where does the chunk just READ belong? (module internal)
; in:  ES = the claim, CL_HELD = the sectors in hand, CL_FOFF = the file
;      offset AFTER them
; out: CL_HLBA and CL_NEXT set
; clobbers: flags
;
; The file's byte offset IS the disk's LBA, times 512 - an image is the disk
; and nothing else - so this is that division, done backwards from the offset
; the read left behind.
; -----------------------------------------------------------------------------
clo_wpos:
    push ax
    push cx
    push dx
    mov ax, [es:CL_FOFF]
    mov dx, [es:CL_FOFF+2]
    mov cx, 512
    div cx                      ; AX = the LBA one past this chunk. DX:AX is
                                ; under 2,880 x 512, so the quotient fits
    mov [es:CL_NEXT], ax
    sub ax, [es:CL_HELD]
    mov [es:CL_HLBA], ax
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clo_both - the whole cross-drive clone, in one keystroke (module internal)
; in:  ES = the claim
; out: CF=0 done; CF=1 with AL = CERR_*
; clobbers: AX, flags
;
; No prompt between the halves because no disk has to move: both are in the
; machine, so the buffer size decides how many int 13h calls this is and
; never how many times the user gets up.
; -----------------------------------------------------------------------------
clo_both:
    push cx
.pass:
    call clo_rd
    jc .out
    call clo_wr
    jc .out
    mov cx, [es:CL_NEXT]
    cmp cx, [es:CL_TOT]
    jb .pass
    call clo_fin
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; clo_rd - read the next run off the SOURCE (module internal)
; in:  ES = the claim
; out: CF=0 with CL_HLBA/CL_HELD describing what is in hand and CL_NEXT moved
;      on; CF=1 with AL = CERR_*
; clobbers: AX, flags
;
; The first run carries LBA 0, and its 512 bytes are kept in CL_BOOT before
; anything else happens to them: they are what SPEC.md 18.99.2 writes last and
; what SPEC.md 18.99.3 compares the target against.
; -----------------------------------------------------------------------------
clo_rd:
    push bx
    push cx
    push dx
    push si
    push di
    call clo_asrc
    call clo_want               ; CX = sectors this run, SI = its first LBA
    mov [es:CL_HLBA], si
    mov [es:CL_HELD], cx
    mov ax, si
    add ax, cx
    mov [es:CL_NEXT], ax
    xor bl, bl                  ; read...
    xor dx, dx                  ; ...from the buffer's own origin. The keep of
    call clo_move               ; sector 0 happens INSIDE, per transfer: on
                                ; the XMS path the buffer holds the LAST
                                ; staging load by the time this returns, not
                                ; the first (SPEC.md 18.99.2). Its CF is ours
.out:
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; clo_wr - write the run in hand to the TARGET (module internal)
; in:  ES = the claim, CL_HLBA/CL_HELD live
; out: CF=0; CF=1 with AL = CERR_*
; clobbers: AX, flags
;
; LBA 0 IS SKIPPED (SPEC.md 18.99.2). The run that holds it is written from
; LBA 1 up, and one sector further into the buffer - which is 32 paragraphs,
; so the base stays 512-aligned. The XMS path needs no such adjustment and is
; given none: there the LBA walker IS the offset into the store.
; -----------------------------------------------------------------------------
clo_wr:
    push bx
    push cx
    push dx
    push si
    push di
    call clo_atgt
    mov si, [es:CL_HLBA]
    mov cx, [es:CL_HELD]
    xor dx, dx
    or si, si
    jnz .go
    inc si                      ; skip LBA 0...
    dec cx                      ; ...and the sector of buffer under it
    jz .none
    mov dx, 32
.go:
    mov bl, 1                   ; write
    call clo_move
    jmp short .out
.none:
    clc
.out:
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; clo_fin - put the source's sector 0 down, last (SPEC.md 18.99.2)
; in:  ES = the claim
; out: CF=0; CF=1 with AL = CERR_*
; clobbers: AX, flags
; -----------------------------------------------------------------------------
clo_fin:
    push bx
    push cx
    push si
    push di
    push es
    push ds                     ; ON TOP, so the copy below gets it back with
    call clo_atgt               ; a pop and a push

    ; **Out of the STAGING BUFFER and not out of CL_BOOT**, because SPEC.md
    ; 2.4 wants every disk-visible base 512-byte aligned: the buffer's is, by
    ; clo_size's rounding, and the job block sits at a paragraph inside the
    ; claim. dsk_runcap's own note records what an unaligned base costs - its
    ; cap falls to one sector and says nothing about it. The buffer's contents
    ; are spent by the time this runs.
    mov bx, [es:CL_BUFSG]
    push es
    pop ds                      ; DS = the claim, ES = the buffer, for one
    mov es, bx                  ; rep movsw and nothing else
    mov si, CL_BOOT
    xor di, di
    mov cx, 256
    cld
    rep movsw
    pop ds                      ; ...and back, before anything reads a global
    push ds

    xor bx, bx                  ; ES:BX = the buffer at offset 0
    xor ax, ax                  ; LBA 0, at last
    mov cx, 1
    call COLD_SEG:dskf_disk_write
    jnc .out
    call clo_ioerr              ; AL = CERR_IO / CERR_WPROT
    stc
.out:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_want - how many sectors does this run move? (module internal)
; in:  ES = the claim
; out: SI = the first LBA, CX = the count (never 0 - the caller has tested
;      CL_NEXT against CL_TOT). Clobbers CX, SI, flags.
; -----------------------------------------------------------------------------
clo_want:
    mov si, [es:CL_NEXT]
    mov cx, [es:CL_TOT]
    sub cx, si
    cmp cx, [es:CL_CHUNK]
    jbe .out
    mov cx, [es:CL_CHUNK]
.out:
    ret

; -----------------------------------------------------------------------------
; clo_move - CX sectors at LBA SI, between the drive and the buffer
; in:  SI = first LBA, CX = sectors, BL = 0 read / 1 write, ES = the claim
; out: CF=0; CF=1 with AL = CERR_*. Clobbers AX, flags.
;
; The one place the XMS split lives. Without a block up there the buffer IS
; the run and this is a single clo_span; with one, the run is walked through
; the conventional buffer CL_STAGE sectors at a time and each load is copied
; up or down - which is a memcpy and not a trip, so the swap count is
; unchanged at one (SPEC.md 18.99.4).
; -----------------------------------------------------------------------------
clo_move:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    push cx                     ; SPEC.md 12.8's bar, scaled to THIS pass:
    push dx                     ; DX:CX = its bytes (12.8.1). A 1.44MB run is
    mov ax, 512                 ; a minute of frozen machine on the field
    mul cx                      ; machine, and this is what is on it
    xchg cx, ax
    call KERNEL_SEG:fpg_begin
    pop dx
    pop cx

    mov di, [es:CL_BUFSG]
%ifdef CLO_XMS
    cmp byte [es:CL_XON], 0
    jne .staged
%endif

    add di, dx                  ; the caller's skip, in paragraphs
    push es
    mov es, di
    mov ax, si
    call clo_span
    pop es
    jc .done
    mov ax, si
    call clo_boot0
    jmp short .done

%ifdef CLO_XMS
    ; --- through the store: CL_STAGE sectors at a time, each load copied up
    ;     or down. A staging copy is a memcpy and not a TRIP, so the swap
    ;     count is unchanged at one (SPEC.md 18.99.4).
.staged:
    mov bp, si                  ; BP = the LBA walker, CX = sectors left
.sload:
    or cx, cx
    jz .sok
    mov ax, cx
    cmp ax, [es:CL_STAGE]
    jbe .sgo
    mov ax, [es:CL_STAGE]
.sgo:
    mov dx, ax                  ; DX = this load, in sectors
    or bl, bl
    jz .sdisk
    call clo_xms                ; a WRITE pass: bring the load down first
    jc .done
.sdisk:
    push cx
    push es
    mov es, di
    mov cx, dx
    mov ax, bp
    call clo_span
    pop es
    pop cx
    jc .done
    or bl, bl
    jnz .sdone
    mov ax, bp                  ; a READ pass: keep sector 0 if this is the
    call clo_boot0              ; load that carried it, THEN send the load up
    call clo_xms
    jc .done
.sdone:
    add bp, dx
    sub cx, dx
    jmp short .sload
.sok:
    clc
%endif
.done:
    call KERNEL_SEG:fpg_end     ; ...which writes no flag (a `mov` and a
                                ; `retf`): CF is still the answer
    pop es
    pop bp
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

; -----------------------------------------------------------------------------
; clo_span - CX sectors from LBA AX, between the drive and ES:0
; in:  AX = first LBA, CX = sectors, ES = the buffer's first segment,
;      BL = 0 read / 1 write
; out: CF=0; CF=1 with AL = CERR_*. Clobbers AX, flags.
;
; **64KB of OFFSET per int 13h window**, because dsk_xfer advances BX and
; never ES (SPEC.md 18.4) - and the window is CLO_WINSEC rounded DOWN to a
; whole number of CYLINDERS, which is worth 4 calls a pass on a 360KB disk and
; was measured that way (SPEC.md 18.99.9).
;
; A window boundary that lands mid-cylinder costs a REVOLUTION: dsk_xfer's run
; stops at the end of the cylinder (SPEC.md 18.91.1), so the remainder needs a
; second call, which on a 5150 waits for the platter to come round again
; (199 ms, docs/FIELD-MACHINES.md). 128 is not a multiple of 18, so five of a
; 360KB clone's six window edges fell inside one and each cost one; the sixth
; is the DMA page below. THE UNIT MOVED WITH 18.91.1 - it was the track when
; this was written, and rounding to tracks now leaves a 1.44MB window edge
; half way through a cylinder.
;
; The segment bump follows the window rather than being 0x1000, and only ever
; applies to a FULL one - a short window empties DI and ends the loop.
;
; dsk_runcap handles the other 64KB boundary - the DMA controller's, which is
; about the PHYSICAL address - inside, and THAT one cannot be aligned away
; from here: 65,536 is not a multiple of 4,608, so one track per 64KB of
; buffer straddles a page whatever this window does. Six calls on a 360KB
; disk, and they are the floor for a buffer that is not page-aligned - which
; the cloner's deliberately is not, because rounding its base up to a page
; costs up to 64KB of the very claim whose SIZE is the trip count (SPEC.md
; 18.99.4): a disk swap to save half a second.
; -----------------------------------------------------------------------------
clo_span:
    push bx
    push cx
    push dx
    push si
    push di
    push bp                     ; the LBA walker on clo_move's staged path,
    push es                     ; which calls this
    mov si, ax                  ; SI = LBA, DI = sectors left
    mov di, cx

    ; --- the window, IN WHOLE CYLINDERS ---
    ; A CYLINDER and not a track since SPEC.md 18.91.1: dsk_xfer's run now
    ; stops at the end of the cylinder rather than the end of the track, so
    ; the boundary a window edge must not land inside moved with it. On both
    ; 9-sector geometries the number is unchanged - 126 is 7 cylinders of 18
    ; as well as 14 tracks of 9 - and on 1.44MB it is 108 rather than 126,
    ; which is three whole cylinders instead of three and a half.
    ;
    ; `make TRACKRUN=1` puts BOTH back together, which is the point of that
    ; knob being one knob: a window rounded to cylinders against a dsk_xfer
    ; bounded by tracks would reinstate exactly the cost 18.99.9 removed.
    ;
    ; Banked rather than folded into a constant because the geometry is the
    ; MOUNTED volume's and this file serves three floppy geometries and a
    ; partition. The direction goes on the stack across it: `div` and `mul`
    ; both want DX.
    push bx
%ifdef TRACK_RUN
    mov bx, [disk_spt]
%else
    mov ax, [disk_spt]
    mul word [disk_heads]       ; DX = 0: 255*63 is all CHS can name
    mov bx, ax
%endif
    mov ax, CLO_WINSEC
    or bx, bx
    jz .whole                   ; no geometry to divide by
    xor dx, dx
    div bx                      ; AX = whole cylinders that fit the window
    mul bx                      ; ...and back to sectors
    or ax, ax
    jnz .have
.whole:
    mov ax, CLO_WINSEC          ; a cylinder LONGER than the window - possible
.have:                          ; on nothing this ships, and the old behaviour
    mov bp, ax                  ; BP = the window, in sectors
    pop bx
    mov dl, bl                  ; DL = the direction; BX is the buffer offset
.win:
    or di, di
    jz .ok
    mov cx, di
    cmp cx, bp
    jbe .go
    mov cx, bp
.go:
    mov ax, si
    xor bx, bx
    or dl, dl
    jnz .wr
    call COLD_SEG:dskf_disk_read
    jmp short .did
.wr:
    call COLD_SEG:dskf_disk_write
.did:
    jc .bad
    add si, cx
    sub di, cx
    mov ax, bp                  ; THE WINDOW'S OWN SIZE in paragraphs - a
    mov cl, 5                   ; sector is 512 bytes, so 32 of them. CL is
    shl ax, cl                  ; free: .win reloads CX from DI before the
                                ; next transfer reads it
    mov bx, es                  ; ...and an 8086 has no `add reg, sreg`. BX is
    add ax, bx                  ; reloaded at .go, so spending it here is free
    mov es, ax
    jmp short .win
.ok:
    clc
    jmp short .out
.bad:
    call clo_ioerr
    stc
.out:
    pop es
    pop bp
    jmp clo_x5                 ; (the shared epilogue, below clo_cat)

%ifdef CLO_XMS
; -----------------------------------------------------------------------------
; clo_xms - move one staging load between the buffer and the store
; in:  DX = sectors, BP = the LBA that load starts at, DI = the buffer's
;      segment, BL = 0 (a read: send it UP) / 1 (a write: bring it DOWN),
;      ES = the claim
; out: CF=0; CF=1 with AL = CERR_MEM. Clobbers AX, flags.
;
; The extended end is the LBA's own byte offset from the block's base, so the
; store holds the disk laid out exactly as the disk is - which is what makes
; the write pass able to walk it in any order the buffer wants.
; -----------------------------------------------------------------------------
clo_xms:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov si, dx                  ; SI = the load, in sectors - DX is about to
                                ; become the high half of a 32-bit product
    mov ax, 512
    mul bp                      ; DX:AX = LBA * 512, the offset into the block
    add ax, [es:CL_XLO]
    adc dx, [es:CL_XHI]         ; ...and DX:AX is the extended end. The store
                                ; holds the disk laid out exactly as the disk
                                ; is, which is what lets either pass walk it
                                ; in whatever order the buffer wants
    push ax
    push dx
    mov ax, 512
    mul si                      ; AX = the bytes; CLO_XSEC caps this at
    xchg cx, ax                 ; XM_MAX_COPY, so the high half is 0
    pop dx
    pop ax

    mov es, di                  ; ES:SI = the CONVENTIONAL end: the staging
    xor si, si                  ; buffer, at offset 0
    xor di, di                  ; DI = the direction, which is what the
    or bl, bl                   ; register means from here on
    jz .go
    mov di, 1                   ; a WRITE pass wants it brought down
.go:
    call KERNEL_SEG:CLO_XCOPY
    jc .bad
    jmp clo_x6                 ; (the shared epilogue, below clo_cat)
.bad:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    mov al, CERR_MEM            ; the store refused, which on this path is
    stc                         ; the same class of failure as never having
    ret                         ; had it (SPEC.md 41.5's codes are its own)
%endif ; CLO_XMS

; -----------------------------------------------------------------------------
; clo_boot0 - was that the transfer carrying sector 0? (module internal)
; in:  AX = the LBA the transfer started at, BL = 0 read / 1 write,
;      ES = the claim
; out: nothing (all registers preserved)
;
; Asked per TRANSFER rather than per run, because on the XMS path a run is
; many transfers and only the first carries LBA 0 - by the time clo_move
; returns the buffer holds the last staging load, and keeping sector 0 out of
; that would put a sector from the middle of the disk at LBA 0 of the target.
; -----------------------------------------------------------------------------
clo_boot0:
    or bl, bl
    jnz .out                    ; a write: the keep is a READ's business
    or ax, ax
    jnz .out
    jmp clo_keepboot
.out:
    ret

; -----------------------------------------------------------------------------
; clo_keepboot - stash the source's sector 0 (module internal)
; in:  ES = the claim, the first run in the buffer
; out: nothing (all registers preserved)
;
; From the CONVENTIONAL buffer either way, at offset 0: clo_boot0 is what
; establishes that the transfer just done started at LBA 0, so the sector is
; the first one in the buffer on both paths.
; -----------------------------------------------------------------------------
clo_keepboot:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov ax, es
    mov di, CL_BOOT
    mov ds, [es:CL_BUFSG]       ; DS is the BUFFER for this one copy...
    mov es, ax                  ; ...and ES the claim; put DS back below
    xor si, si
    mov cx, 256
    cld
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clo_issrc - is the disk in the TARGET drive the SOURCE disk? (18.99.3)
; in:  ES = the claim
; out: CF=0 = yes, it is (or it looks exactly like it); CF=1 = it is not, or
;      the question does not arise. Clobbers AX, flags.
;
; Only for a same-drive clone: one disk cannot be in two drives, so a
; cross-drive one has nothing to confuse.
;
; **[dsk_rah_busy] is raised for the read.** Without it SPEC.md 18.95's cache
; answers out of the SOURCE's sector 0, which it read minutes ago under this
; very volume index, and the check says "still the source disk" for ever.
;
; A read that FAILS is a disk this is not - an unformatted target is the
; ordinary case, and the source is by construction one we have just read.
; -----------------------------------------------------------------------------
clo_issrc:
    push bx
    push cx
    push si
    push di
    push ds
    push es
    call clo_same
    jne .no                     ; one disk cannot be in two drives
    call clo_aim
    inc byte [dsk_rah_busy]
    xor ax, ax
    call clo_read1              ; LBA 0 of whatever is in the drive
    dec byte [dsk_rah_busy]
    jc .no                      ; unreadable: an unformatted target is the
                                ; ordinary case, and the source is by
                                ; construction a disk we have just read

    push es
    pop ds                      ; DS = the claim, ES = LOW_SEG (which SS is):
    push ss                     ; one compare over two segments neither of
    pop es                      ; which is the kernel's, so cmpsw serves both
    mov si, CL_BOOT
    mov di, dsk_secbuf
    mov cx, 256
    cld
    repe cmpsw
    mov ax, KERNEL_SEG
    mov ds, ax                  ; DS back before the flags are read: `mov`
    jne .no                     ; leaves them alone, and this is the one
    clc                         ; every byte the same: it IS the source disk
    jmp short .out
.no:
    stc
.out:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clo_ioerr - dskw_fmt_ioerr's two compares, in this image
; out: AL = CERR_IO or CERR_WPROT
; clobbers: AL, flags
;
; Duplicated rather than far-entered, on docs/plans/completed/HDD-SPLIT-PLAN.md 3.2's rule: a
; boundary crossing for two compares costs more than the bytes it saves. It
; reads the same [dsk_ioerr] through the same DS.
; -----------------------------------------------------------------------------
clo_ioerr:
    mov al, CERR_IO
    cmp byte [dsk_ioerr], 0x03
    jne .out
    mov al, CERR_WPROT
.out:
    ret

; -----------------------------------------------------------------------------
; clo_line - stage one of the two prompt lines (SPEC.md 22.21.1)
; in:  AL = 0 the row-area line / 1 the status line, DI = the destination,
;      DS = KERNEL_SEG
; out: DI -> the NUL it wrote
; clobbers: DI (the output), flags
;
; It lives in the image and its strings do not, because strings are DATA
; (SPEC.md 2.6) - but the LADDER is code, and this is the half of the feature
; that would otherwise have cost files.inc a second forty-byte branch.
; -----------------------------------------------------------------------------
clo_line:
    push ax
    push bx
    push cx
    push si
    push es
    cmp al, 3
    je .swap                    ; the MODULE-SWAP line, which is the one
                                ; selector that runs before there is a claim:
                                ; mode 6 step 1 is up after the image loaded
                                ; and before CLV_START (SPEC.md 22.21.4)
    mov bx, [clo_seg]
    or bx, bx
    jz .none                    ; nothing is running, so there is no prompt to
    mov es, bx                  ; compose. It cannot happen - the prompt is
    mov bl, [es:CL_STEP]        ; armed after the claim and dropped with it -
    cmp al, 2                   ; and this is what makes that a fact rather
    je .verdict                 ; than a segment register pointed at 0000
    or al, al
    jz .line0                   ; line 0: the prompt itself, after the
                                ; exit - so all three tests here are
                                ; short jccs (size pass 8)
.keys:
    xor bh, bh                  ; the key line BY STEP (CLS_PICK..CLS_NOSRC):
    shl bx, 1                   ; 0 is the kernel's 'Enter=ready  Esc=no'
    mov si, [cs:bx+clo_ktab]
    or si, si
    jnz .tail
    mov si, fm_s_swapk          ; 'Enter=ready  Esc=no': the KERNEL's own line,
    call clo_dscat              ; which is resident for mode 6's step 0 anyway
    jmp short .none             ; (files.inc) - so no copy rides in the image
.tail:
    call clo_cat
.none:
    mov byte [di], 0
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

.swap:                          ; ...and step 1 of mode 6 is the SAME line
    mov bl, CLS_SRC             ; as CLS_SRC's, out of the same three strings:
    jmp short .ins              ; the disk it wants is this operation's source

    ; 'Cloned A: to B:' (SPEC.md 22.21.3) - not a prompt line at all, but the
    ; toast, composed here because the image is what knows the two volumes and
    ; because the alternative was thirty bytes of `.cold` in files.inc making
    ; the same five calls. The caller asks for it while the image is still
    ; loaded, which is before fm_edit_end and therefore before the drop.
.verdict:
    mov si, clo_s_ok            ; 'Cloned A: to B:'
    mov al, [es:CL_END]
    cmp al, 1
    jb .vdisk
                                ; ...and the image end's two, which name the
                                ; DISK and not the file: the file is the one
                                ; the user just chose in a dialog and the disk
                                ; is the thing that changed. Both are CL_SRC:
                                ; for 'Wrote' clo_wimg set the source to the
                                ; target as well
    mov si, clo_s_imgok         ; 'Imaged A:'
    je .vsay                    ; (`mov` leaves the compare's flags)
    mov si, clo_s_wrok          ; 'Wrote A:'
.vsay:
    call clo_cat
    call clo_drvs
    jmp short .colon
.vdisk:
    call clo_srcto
    call clo_drvt
.colon:
    mov si, clo_s_colon
    jmp .tail

.line0:

    cmp bl, CLS_PICK
    je .pick
    cmp bl, CLS_SAME
    je .same
    cmp bl, CLS_WIMG
    je .wimg
    cmp bl, CLS_NOSRC
    je .nosrc
.ins:
    mov si, clo_s_ins           ; 'Insert the ' + SOURCE/TARGET + ' disk, ...'
    call clo_cat
    mov si, clo_s_src
    cmp bl, CLS_SRC
    je .which
    mov si, clo_s_tgt
.which:
    call clo_cat
    mov si, clo_s_disk
    jmp .tail
.same:
    mov si, clo_s_same
    jmp .tail
.nosrc:
    mov si, clo_s_nosrc         ; ...and the same warning on the SOURCE prompt
    jmp .tail                   ; (SPEC.md 18.99.3)
.pick:
    mov si, clo_s_clone         ; 'Clone A: to B:, ERASING it'
    call clo_srcto
    cmp byte [es:CL_TGT], CLE_IMG
    je .pimg
    call clo_drvt
    mov si, clo_s_erase
    jmp .tail
.pimg:
    mov si, clo_s_img           ; 'Clone A: to an image file' - no erasure to
    jmp .tail                   ; name, because a file is named in the Save
                                ; box and replacing one there is what a Save
                                ; box means (SPEC.md 18.99.8)

    ; 'ERASE A: and write DISK.IMG?' - the WRITE side, which does erase a disk
    ; and says so in SPEC.md 22.12's own words.
.wimg:
    mov si, clo_s_wr1
    call clo_cat
    call clo_drvt
    mov si, clo_s_wr2
    call clo_cat
    mov si, clo_fnbuf           ; the name is RESIDENT (it has to be - the
    call clo_dscat              ; file API reads it through DS), so this one
    mov si, clo_s_q             ; string is lettered the kernel's way
    jmp .tail


; -----------------------------------------------------------------------------
; clo_cat - fm_scat, in the image, over the image's OWN strings (SPEC.md 2.8.6)
; in:  SI = a NUL string in THIS SEGMENT, DI = where it goes in KERNEL_SEG
; out: DI -> the terminator's place; SI past the NUL
; clobbers: AL, SI, DI, flags
;
; **`[cs:si]`, and that is the whole of how the prompts left the kernel.**
; SPEC.md 2.6 says cold code carries no data, and every module before this one
; obeyed it - so a feature's strings stayed resident while its code left, and
; for a feature that is mostly prompts the strings ARE the cost. The rule is a
; convention rather than a hardware fact: a module is loaded at a segment of
; its own and far-called, so CS names its own image and an override reads it.
; What the rule is really protecting is the ORDER - nothing may read an
; image's data before the image is there - and here that is guaranteed by
; construction (SPEC.md 2.8.6).
;
; `lodsb` is gone with it: that reads DS:SI, which is the kernel, and a
; segment override on a string instruction is legal but says the opposite of
; what is meant here.
; -----------------------------------------------------------------------------
; clo_x6 / clo_x5 - the epilogue thirteen of this image's routines share,
; fcp_x6's shape exactly: they bank bx, cx, dx, si, di (and es), and reach
; this by `jmp`, never `call`, so the `ret` finds the routine's own caller.
; Every site's flags are set above its jump, and a `pop` touches none.
; The order ENDS at bx, so it is no suffix of the kernel's kret ladders.
; clo_same - is the clone between ONE drive? AL = CL_SRC and ZF = 1 when
; CL_TGT is the same (four sites asked it in two instructions each)
clo_same:
    mov al, [es:CL_SRC]
    cmp al, [es:CL_TGT]
    ret

clo_ktab:   dw clo_s_key        ; CLS_PICK: 'Spc=drive  Enter=yes  Esc=no'
            dw 0, 0             ; CLS_SRC, CLS_TGT: 'Enter=ready  Esc=no'
            dw clo_s_samek      ; CLS_SAME: 'Enter=re-check  Y=go  Esc=no'
            dw clo_s_yn         ; CLS_WIMG: 'Enter=yes  Esc=no'
            dw clo_s_samek      ; CLS_NOSRC: CLS_SAME's three answers, on the
                                ; other prompt (SPEC.md 18.99.3)
%if CLS_PICK != 0 || CLS_SRC != 1 || CLS_TGT != 2 || CLS_SAME != 3 || CLS_WIMG != 4 || CLS_NOSRC != 5
    %error "clo_ktab is indexed by CL_STEP: CLS_PICK..CLS_NOSRC must be 0..5"
%endif

clo_x6:
    pop es
clo_x5:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

clo_cat:
    mov al, [cs:si]
    inc si
    mov [di], al
    or al, al
    jz .done
    inc di
    jmp short clo_cat
.done:
    ret

; -----------------------------------------------------------------------------
; The prompts (SPEC.md 22.21.1), IN THE IMAGE. Every one of them is read by
; clo_line above and by nothing else, which is what makes them movable: they
; go on screen only while a clone prompt is up, and a clone prompt is up only
; while this file is loaded.
;
; What stayed behind, and why, is the interesting half:
;
;   * the MENU ITEM ('Clone Disk...' and its MENU_DIS twin) - the bar is built
;     when the user presses it, which is before anything has been loaded and
;     is the gesture that leads to loading;
;   * the ERROR strings - four of the five are the FERR_* table's own, said
;     the same way (SPEC.md 22), and the fifth rides beside them;
;   * mode 6's step-0 line and its key line - that prompt exists BECAUSE the
;     image could not be read.
;
; 'Enter=ready  Esc=no' is NOT here: it is files.inc's fm_s_swapk, read
; through DS (clo_dscat). It was a copy, on the argument that pointing at the
; resident one would keep it resident - but step 0 above keeps it resident
; anyway, so the copy was twenty bytes of a claim buying nothing. Should step
; 0 ever lose that line, the reference here is what says it may not.
; -----------------------------------------------------------------------------
clo_s_clone: db 'Clone ', 0         ; 'Clone A: to B:, ERASING it' - Format's
clo_s_to:    db ': to ', 0          ; own confirmation shape (SPEC.md 22.12),
clo_s_erase: db ':, ERASING it', 0  ; and it says so because there is no undo
clo_s_key:   db 'Spc=drive  Enter=yes  Esc=no', 0
clo_s_ins:   db 'Insert the ', 0    ; ...and the two swap prompts, which name
clo_s_src:   db 'SOURCE', 0         ; NO DRIVE LETTER: they are only ever up
clo_s_tgt:   db 'TARGET', 0         ; for a same-drive clone, where there is
clo_s_disk:  db ' disk, then Enter', 0   ; one drive and naming it is noise
clo_s_same:  db 'Still the SOURCE disk', 0   ; SPEC.md 18.99.3: a WARNING and
clo_s_samek: db 'Enter=re-check  Y=go  Esc=no', 0   ; not a refusal, because
                                    ; two disks can carry identical boot
                                    ; sectors and re-cloning onto last week's
                                    ; copy is exactly that case
clo_s_nosrc: db 'Not the SOURCE disk', 0    ; ...and the SAME check on the
                                    ; source prompt, which is the direction
                                    ; that used to go unguarded and silent
                                    ; (SPEC.md 18.99.3). Its key line is
                                    ; clo_s_samek's
clo_s_ok:    db 'Cloned ', 0        ; the verdict (SPEC.md 22.21.3)
clo_s_colon: db ':', 0              ; ...and its tail: 'Cloned A: to B:'
clo_s_img:   db 'an image file', 0  ; ...and the IMAGE end (SPEC.md 18.99.8)
clo_s_wr1:   db 'ERASE ', 0         ; 'ERASE A: and write DISK.IMG?'
clo_s_wr2:   db ': and write ', 0
clo_s_q:     db '?', 0
clo_s_defimg: db 'DISK.IMG', 0     ; the Save box's default (SPEC.md 18.99.8)
clo_s_yn     equ clo_s_key + 11 ; 'Enter=yes  Esc=no', which is clo_s_key's
                                ; own tail (asserted below)
clo_s_wrok:  db 'Wrote ', 0         ; ...and the two verdicts the image end
clo_s_imgok: db 'Imaged ', 0        ; earns
%if clo_s_ins - clo_s_key != 29
    %error "clo_s_yn is the last 18 bytes of clo_s_key: 'Spc=drive  ' must stay 11 long"
%endif

; -----------------------------------------------------------------------------
; clo_fnget - the file dialog's answer into clo_fnbuf (SPEC.md 18.99.8)
; clo_ds13  - ...and the copy under it: 13 bytes DS:SI -> DS:DI
; clobbers: SI DI, flags - CX and ES preserved, because clo_wimg's size is in CX
;
; [fdlg_name] is the dialog's own buffer and the next dialog reuses it, while a
; Write Img waits for its Enter with the name still wanted - so it is copied,
; and HERE rather than in the resident completion proc, which used to spend
; eighteen bytes doing it for all three of its answers.
; -----------------------------------------------------------------------------
clo_fnget:
    mov si, fdlg_name
    mov di, clo_fnbuf
clo_ds13:
    push cx
    push es
    push ds
    pop es
    mov cx, 13
    rep movsb                   ; DF=0 (SPEC.md 1), no segment override
    pop es
    pop cx
    ret

; -----------------------------------------------------------------------------
; clo_dscat - clo_cat over a KERNEL string (module internal)
; in:  SI = a NUL string in KERNEL_SEG, DI = where it goes
; out: DI -> the terminator's place
; clobbers: AL, SI, DI, flags
;
; The ONE string this image letters that is not its own: the file name, which
; has to be resident because the file API reads a name through DS (see
; clo_fnbuf). Two routines rather than a flag, because which segment a string
; is in is a property of the string and not of the moment.
; -----------------------------------------------------------------------------
clo_dscat:
    mov al, [si]
    inc si
    mov [di], al
    or al, al
    jz .done
    inc di
    jmp short clo_dscat
.done:
    ret

; -----------------------------------------------------------------------------
; clo_drv - stamp volume AL's LETTER, and not its colon (module internal)
; in:  AL = a volume index, DI = where it goes
; out: DI past the letter
; clobbers: AL, DI, flags
;
; The colon belongs to the string that follows - ': to ' and ':, ERASING it' -
; because putting it here would make every one of those strings begin with a
; character the composer has already written.
;
; clo_drvs / clo_drvt - ...of CL_SRC / CL_TGT, ES = the claim. clo_srcto -
; SI's string, the SOURCE's letter and ': to ', which 'Clone A: to ...' and
; 'Cloned A: to ...' both begin with. Clobbers AL, SI, DI, flags
; -----------------------------------------------------------------------------
clo_srcto:
    call clo_cat
    call clo_drvs
    mov si, clo_s_to
    jmp clo_cat
clo_drvs:
    mov al, [es:CL_SRC]
    jmp short clo_drv
clo_drvt:
    mov al, [es:CL_TGT]
clo_drv:
    add al, 'A'
    mov [di], al
    inc di
    ret

; -----------------------------------------------------------------------------
; clo_free - give the claim and the XMS block back (SPEC.md 22.21.2)
; out: nothing; [clo_seg] is 0 afterwards
; clobbers: flags only - every register preserved, because fm_edit_end calls
;           this from inside a walk over the window pool
;
; Idempotent, and it has to be: fm_edit_end runs on every mode change and
; reaches this once per armed block.
; -----------------------------------------------------------------------------
clo_free:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [clo_seg]
    or ax, ax
    jz .out
    mov es, ax
%ifdef CLO_XMS
    cmp word [es:CL_XKB], 0
    je .noxms
    mov ax, [es:CL_XLO]
    mov dx, [es:CL_XHI]
    call KERNEL_SEG:CLO_XFREE
    mov word [es:CL_XKB], 0
.noxms:
%endif
    mov dx, [clo_seg]
    mov word [clo_seg], 0       ; cleared FIRST: the free below can compact
    mov bx, MEM_K_CLONE         ; (SPEC.md 66.4), and a word still naming a
    call COLD_SEG:mmf_mem_free  ; claim that is going away is the one state
.out:                           ; nothing downstream could survive
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

section .text
; The one resident datum (SPEC.md 18.99.1). `.text` and not `.bss`, because
; -f bin zeroes nothing and this word decides whether clo_free dereferences
; a segment - so it is initialised here rather than by an init routine that
; something could run before.
clo_seg:    dw 0                ; the running operation's claim, 0 = none
clo_fnbuf:  times 13 db 0       ; the image file's 8.3 name (SPEC.md 18.99.8).
                                ; THIRTEEN RESIDENT BYTES and they cannot go
                                ; in the claim with the rest of the state: the
                                ; file API reads a name through DS, and DS is
                                ; KERNEL_SEG. Staged by the dialog's
                                ; completion proc, read by the module.

section .cold
; -----------------------------------------------------------------------------
; clo_need - make CLONE.DRV's image resident (SPEC.md 2.8.1)
; out: CF = 1 it is not.  AX and every other register preserved - `pop ax` and
;      `ret` write no flag, so mod_need's CF reaches the caller intact
;
; dskw_fmt_load's shape for the other on-demand module, and here for the same
; reason: three sites in two files open-coded the bank-set-call-unbank, which
; is seven bytes against this call's three.  `.cold` because ITS CALLERS ARE -
; clo_call below and files.inc's two - and so is mod_need, so every edge is a
; near call.  It is NOT in the `.modl` block above: that is the image, and a
; routine whose whole job is to fetch the image cannot live inside it.
; -----------------------------------------------------------------------------
clo_need:
    push ax
    mov al, MOD_CLONE
    call mod_need
    pop ax
    ret

; -----------------------------------------------------------------------------
; clo_call - the whole resident dispatch (SPEC.md 18.99.7)
; in:  DL = CLV_*, everything else the verb's own
; out: the verb's; CF=1 with AX = CERR_NOMOD if the image cannot be had
;
; ONE thunk, because the image dispatches on DL itself - four of the
; formatter's shape would have been eighty bytes of `.cold` for four verbs.
; NEAR to mod_need, this file's stub being cold like mod.inc (SPEC.md 2.6:
; a cw_ shim out and a thunk back would be six segment transitions where a
; near call does).
; -----------------------------------------------------------------------------
clo_call:
    call clo_need
    jc .no
    call far [CLFP + CLE_DISP*4]
    ret
.no:
    mov ax, CERR_NOMOD          ; the cloner is on the system disk and the
    ret                         ; system disk is not in the boot drive - said
                                ; as 'No system disk in A:', the Task
                                ; Manager's own string. It was CERR_NODISK
                                ; ('No disk'), and on a two-drive machine with
                                ; a disk in each that reads as "no disk to
                                ; image" (docs/FIELD-NOTES.md 32). CF IS
                                ; ALREADY SET: `.no` is reachable only through
                                ; the `jc` above, and `mov r16, imm16` writes
                                ; no flag

; -----------------------------------------------------------------------------
; clo_drop - the cloner is finished with (SPEC.md 2.8.3)
;
; fm_edit_end's, and only ever after CLV_FREE: dropping the image first would
; free the code that is about to run.
; -----------------------------------------------------------------------------
clo_drop:
    mov al, MOD_CLONE           ; A TAIL JUMP, and it is a CONTRACT CHANGE:
    jmp mod_drop                ; this routine now clobbers AL where it used to
                                ; preserve AX. mod_drop preserves every
                                ; register and takes AL as its input, so the
                                ; bank only ever protected our own `mov al`.
                                ; Both callers checked: files.inc's first is
                                ; followed by `.armed: ret` and reads nothing,
                                ; and its second has its own push/pop ax round
                                ; the pair. A THIRD CALLER WILL NOT BE TOLD.
                                ; The near `jmp` is legal because clone.inc and
                                ; mod.inc are both `.cold` and mod_drop's `ret`
                                ; is near - it lands on our caller's return
                                ; address. It does NOT generalise to clo_call
                                ; or to kernel.asm's far thunks.

section .text
