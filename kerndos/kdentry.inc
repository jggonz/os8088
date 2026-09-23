; =============================================================================
; kerndos/kdentry.inc - kern_dos's REAL entry (docs/plans/KERN-DOS-PLAN.md §7 step 8)
;
; The machine has already been torn down and restarted into this image: the
; kernel is gone, the drivers are gone, the ROM's vectors are back, and what
; is left is a BIOS, a disk and a block of parameters the stub copied in.
; This hooks INT 21h and its neighbours, builds the PSP and the environment,
; loads the program and runs it - and when the program exits there is nothing
; to go back to, so it restarts the machine (§9).
;
; **IT IS `dos_run` PLUS `dos_fsx_main` WITH THE OS TAKEN OUT**, and that is
; the shape to keep: every step below is one of theirs in their order, minus
; the ones that name a kernel this machine has not got. What is left is the
; steps that are pure 8086 work on the IVT, the BDA and the DOS core's own
; bss - which W4 measured as 21 procs reaching no `OSAPI_*` and no indirect
; call at all (SPEC.md 96.38), and is why they port by being included.
;
; The W4 gate (`kdosgate.inc`) stages a block and comes in here too, so the
; ABI has a test that needs no kernel, no handoff and no hard disk.
; =============================================================================
section .text

; -----------------------------------------------------------------------------
; kd_entry - in: nothing; the stub has put a launch block at [kd_lbp]
; -----------------------------------------------------------------------------
kd_entry:
    cli
    mov ax, KD_SEG              ; CS is already ours; the rest follows
    mov ds, ax
    mov es, ax
    mov ax, LOW_SEG             ; SS is NOT DS (SPEC.md 2.1): the disk layer's
    mov ss, ax                  ; buffers are reached through it
    mov sp, KD_STACK
    sti
    cld

    ; --- THE BSS, WHICH ARRIVES AS WHATEVER WAS ALREADY THERE ---------------
    ; `.bss` and `.lowbss` are `nobits`: they cost the part no disk, and
    ; NOTHING THAT PUTS THIS IMAGE IN MEMORY WRITES THEM. On the W4 gate that
    ; was invisible - a machine four seconds out of POST has zeros above the
    ; image - and on the handoff it is the outgoing KERNEL's data, at its own
    ; offsets, which is a volume table, a FAT window and a handle table that
    ; all look plausible and are another operating system's. It surfaced as
    ; `the disk could not be mounted` about a floppy whose BPB int 13h had
    ; just read perfectly.
    ;
    ; Here rather than in each loader, for `mod_need`'s MOD_BSS reason one
    ; layer out: one place cannot be forgotten by the next thing that learns
    ; to start this image.
    ;
    ; **THE LAUNCH BLOCK IS THE ONE THING ALREADY WRITTEN**, so it is skipped
    ; at RUN time rather than at assembly time - the handoff's lands in
    ; `kd_lblock` and the gate's in `kd_glb`, and `[kd_lbp]` is the only thing
    ; that knows which. It is in `.text` and survives this.
    mov si, [kd_lbp]
    push ds
    pop es
    xor al, al
    mov di, kd_b_start
    mov cx, si
    sub cx, di
    rep stosb                   ; ...everything under the block
    mov di, si
    add di, KDL_SIZE
    mov cx, kd_e_bss
    sub cx, di
    rep stosb                   ; ...and everything over it
    push ss
    pop es                      ; ...and the disk layer's buffers, which are
    xor di, di                  ; SS's and whose section is `vstart=0`, so the
    mov cx, kd_e_lowbss         ; end label IS the length
    rep stosb
%ifdef KDSTKDIAG
    ; --- HOW DEEP DOES THIS STACK ACTUALLY GO? (SPEC.md 96.43.1) -------------
    ; `tools/kdstkwater.py` is the reader.  The buffers above are zeroed
    ; exactly as they ship - the machine under measurement has to be the
    ; machine - and what is filled is only the GAP between them and the stack
    ; top, which nothing has written yet: DI is already `kd_e_lowbss` and SP
    ; is still KD_STACK, so `rep stosb` stops one byte under the first thing
    ; the CPU will push.  Zero when the knob is off.
    mov al, 0xAA
    mov cx, KD_STACK
    sub cx, di
    rep stosb
%endif
    push ds
    pop es

%ifndef KD_GATE
    ; --- the glass, because the STAGING AREA IS THE TEXT FRAMEBUFFER ---------
    ; SPEC.md 87.5 stages the handoff in the one RAM a conventional-memory
    ; image does not cover, which is the text page - so what is on the screen
    ; when this starts is the stub, its extent list and the launch block, drawn
    ; as characters. Setting the mode the machine is ALREADY in is the BIOS's
    ; own clear and costs six bytes; a hard-coded 3 would be wrong on an MDA.
    ; It is `%ifndef KD_GATE` because the gate arrives through a loader that
    ; left the screen clean and has already printed on it (kdosgate.inc).
    mov ah, 0x0F
    int 0x10                    ; AL = the mode vid_reboot left
    xor ah, ah
    int 0x10                    ; ...set again, which is how a BIOS clears
%endif

    ; --- the block, checked before a byte of it is believed ------------------
    mov si, [kd_lbp]
    cmp word [si+KDL_MAGIC], KDL_SIG & 0xFFFF
    jne .nolb
    cmp word [si+KDL_MAGIC+2], KDL_SIG >> 16
    jne .nolb
    cmp word [si+KDL_VER], KDL_VER_NOW
    jne .nolb
    cmp word [si+KDL_TLEN], KDL_MINE  ; **AND THE LENGTH, WHICH IS THE HALF
    jne .nolb                         ; THAT ROTS** - the magic survives a
                                      ; field being added on one side only and
                                      ; this does not, because both sides sum
                                      ; their own copy of the same list
                                      ; (kdlaunch.inc)

    ; --- the fields, back into the DOS core's own bss -------------------------
    call kd_scatter

    ; --- and the DISKETTE PARAMETER TABLE, which is not one of them ----------
    ; SPEC.md 18.92: `int 1Eh` is a POINTER to eleven bytes the BIOS re-reads
    ; on every floppy operation, and os8088 owns it - so the vector we inherit
    ; names KERNEL_SEG:dsk_dpt, which is THIS segment at an offset that is not
    ; our table. The handoff carries the bytes (kerndos/kdlaunch.inc) and this
    ; puts them in ours and names it; a block with none - the W4 gate, which
    ; arrives through a loader that never touched the vector - leaves the ROM's
    ; alone, and a table whose first byte is zero is not one.
    cmp byte [si+KDL_DPT], 0
    je .nodpt
    push si
    push di
    add si, KDL_DPT
    mov di, dsk_dpt             ; ES is ours
    mov cx, DPT_LEN
    rep movsb
    xor ax, ax
    mov es, ax
    pushf
    cli                         ; the vector is two words and an interrupt
    mov word [es:0x0078], dsk_dpt   ; between them would hand the BIOS half an
    mov word [es:0x007A], KD_SEG    ; address (dsk_dpt_init_x's reason)
    popf
    push ds
    pop es
    pop di
    pop si
.nodpt:

    ; --- THE VOLUME TABLE, BEFORE ANY MOUNT (SPEC.md 96.50) -----------------
    ; It has to be in place before `disk_mount_x` below, and before the back
    ; end: `dos_vol` is an INDEX into it, and until this runs the table is the
    ; static A:/B: initialiser in `kernel/disk.inc` - which is an assumption
    ; about a table os8088 builds at boot and does not match one on any
    ; machine whose floppy count is not two.
    call kd_vtab
    call kdb_bevec              ; **THE BACK END, BEFORE ANY DOOR** (SPEC.md
                                ; 96.44.1): the core stores a DBE_* ordinal and
                                ; dos_be_go resolves it through `dos_bevec`, so
                                ; this comes before the mount and before
                                ; dos_fh_setup - both of which are doors

    ; --- the arena ------------------------------------------------------------
    ; A real DOS puts the program straight above itself and gives it
    ; everything to the top of memory. So does this: no heap, no claim, no
    ; region - [kd_floor] IS the first paragraph the program owns.
    mov ax, LOW_SEG + (KD_LOW_KB * 64)
    mov [kd_floor], ax
    mov [dos_arena], ax
    mov bx, [si+KDL_CAP]        ; KB, the BDA's own figure banked by the box
    cmp bx, KD_CAP_MAX
    jbe .cap
    mov bx, KD_CAP_MAX          ; a cap over the 640KB an 8086 can address at
.cap:                           ; all is a block nobody staged
    mov cl, 6
    shl bx, cl                  ; KB -> paragraphs; KD_CAP_MAX keeps it in 16
    mov [kd_top], bx
    sub bx, ax
    jbe .noroom
    mov [dos_apara], bx
    mov ax, bx
    shr ax, cl
    mov [dos_akb], ax

    ; --- the volume the program came off --------------------------------------
    mov dl, [dos_vol]           ; SPEC.md 96.6's map is the identity, so the
    call disk_mount_x           ; DOS drive IS the volume index
    jc .nomount

    ; --- AND THE ARENA AGAIN, BECAUSE THE MOUNT CLAIMED (SPEC.md 96.44.11) ----
    ; The block above sized the arena from `[kd_top]` and the mount then LOWERED
    ; it: `dsk_rah_want` solves the read-ahead's width from `mem_avail_lvl_x`,
    ; which here answers the whole arena, so it takes the `DSK_RAH_RUNS` ceiling
    ; - 32 KB off the top, every launch. Nothing re-read `[kd_top]` afterwards,
    ; so the program was told it owned memory the cache was sitting in.
    ;
    ; **RE-READING IS NOT COSMETIC AND IT IS NOT THE HANDOVER'S JOB**: this is
    ; before `dos_fh_setup`, which carves 96.11's window off the ceiling, and
    ; before `dos_ldpara` bounds `dos_load`'s READ. Left overstated, a program
    ; image big enough to reach the cache is read straight over it and the next
    ; `dsk_rah_serve` hit serves the program's own bytes as disk sectors - on
    ; exactly the large programs this arm exists for.
    call kd_arena
    jc .noroom

    ; --- the load geometry, which is dos_run's and is NOT the arena's ---------
    ; [dos_ldpsp] is the PSP and not the arena base: dos_build_psp lays the
    ; environment's MCB at paragraph 0 and the PSP at 10, so a base handed to
    ; dos_load loads the image on top of the environment (SPEC.md 96.38.2).
    mov ax, [dos_arena]
    add ax, DOS_PSPP
    mov [dos_ldpsp], ax
    mov word [dos_ldname], dos_name
    call dos_fh_setup           ; ...and the file window comes OFF the top
    jc .nofh                    ; before the program is told how much memory
    mov ax, [dos_apara]         ; it has (SPEC.md 96.11)
    sub ax, DOS_PSPP
    mov [dos_ldpara], ax

    ; --- where the launch stood -----------------------------------------------
    mov ax, [dos_dir]           ; NOT a root: a program launched from a
    or ax, ax                   ; subdirectory can walk out of it, like it
    jz .stood                   ; would under DOS (SPEC.md 96.6.1)
    mov dx, ax                  ; DX = the cluster, BL = its volume
    mov bl, [dos_vol]
    call dos_be_goto
.stood:

    ; --- load it ---------------------------------------------------------------
    ; **AND THE LADDER IS UNDER IT** (SPEC.md 96.44.11). The arena is honest
    ; now, so an image between `[kd_top]` and the machine's own ceiling no
    ; longer overruns the cache - `dskw_read_x` answers `FERR_BIG` before any
    ; data I/O and leaves the destination untouched. A refusal is better than
    ; the corruption it replaces and is still not the right answer: the memory
    ; EXISTS, a cache is sitting in it, and the program has just asked. So a
    ; failed load sheds a rung and reads again, and only a ladder with nothing
    ; left on it lets the refusal stand. Retrying is safe precisely because
    ; FERR_BIG writes nothing.
.loadtry:
    call dos_load               ; ...through dos_be_read, which is kdback's
    jnc .loaded
    call kd_shed                ; 32 KB -> 18 -> 9 -> gone, a rung a try
    jc .noload                  ; nothing left: the program really is too big
    call kd_arena               ; ...and the room it gave back is the arena's
    jc .noload
    jmp short .loadtry
.loaded:
    call dos_is_exe             ; the answer is in the FILE and not in the
    jnc .isCOM                  ; name (SPEC.md 96.8)
    call dos_exe_setup
    jc .noload
    mov byte [dos_isexe], 1
    jmp short .ready
.isCOM:
    mov byte [dos_isexe], 0
.ready:

    ; --- THE CACHE, BACK TO THE PROGRAM (SPEC.md 96.44.11) --------------------
    ; The load is done, and it is what the read-ahead was for: the mount, the
    ; directory walk and `dos_load`'s own read of the image - TD3 is 137,845
    ; bytes of `.EXE`. What comes next is `dos_build_psp`, which hands the
    ; program EVERYTHING (96.38.2's two blocks, and PSP:0002 is how it finds
    ; out), so from here the cache is memory the program owns and cannot see.
    ;
    ; **THIS IS WHERE THE LADDER ENDS AT ZERO, AND THAT IS A PROPERTY OF THE
    ; HANDOVER RATHER THAN OF THE LADDER.** `dos_build_psp` gives a program the
    ; whole arena because that is what DOS does with a .COM, and `dos_exe_setup`
    ; reads MINALLOC and not MAXALLOC - so nothing here ever wants less than
    ; all of it and the loop below runs to the bottom. `kd_shed` is a rung at a
    ; time anyway: the day a caller wants a FIGURE rather than everything, the
    ; policy is the `jnc` below and nothing else changes.
    call kd_giveback

    ; --- the machine, banked, and the program built on top of it ---------------
    ; [dos_inbr] = 1 FOR EVER here and it is a finding rather than a setting
    ; (SPEC.md 96.38): the program owns the machine from the moment it starts,
    ; so dos_tty's console arm is unreachable and there is no window to come
    ; back to.
    mov byte [dos_inbr], 1
    mov word [dos_vw], 640      ; INT 33h's scale (SPEC.md 96.10). The package
    mov word [dos_vh], 200      ; asks OSAPI_VIDEO; there is no kernel here to
                                ; ask, and a text-mode machine's is the 80x25
                                ; cell grid's
    ; --- THE MOUSE, AND IT GOES IN BEFORE THE BANK (SPEC.md 96.45) ----------
    ; `dos_restore_machine` puts the WHOLE IVT back out of `dos_save_machine`'s
    ; copy, so a vector taken after the bank is one the program's own exit
    ; silently undoes. Taken before it, the pointer is part of the machine the
    ; program is handed - which is what a mouse driver is under DOS - and
    ; `DHK_MOUSE` is how the core reaches it (96.44.3). SI still points at the
    ; launch block, which is where the port and the line came from.
    call kd_mou_start
    mov word [dos_hkv + DHK_MOUSE], kd_mou_read
    mov word [dos_hkv + DHK_TXT],  kd_mou_txt
                                ; **THE CURSOR IS THIS HOST'S AND ONLY THIS
                                ; HOST'S** (SPEC.md 96.10.5.1). A DOS mouse
                                ; driver draws its own pointer and nothing
                                ; else will; here the program owns every
                                ; pixel, so it can be drawn. The windowed box
                                ; leaves this cell at zero and the core
                                ; refuses at its second instruction, because
                                ; there B800 is the kernel's
%ifndef KD_NO_KBGUARD
    call kd_kbd_start           ; ...and the key buffer's guard, BEFORE the
                                ; bank for the same reason (SPEC.md 96.50)
%endif

    call dos_save_machine       ; **BEFORE dos_hook_vectors**: the whole IVT is
                                ; what dos_restore_machine puts back, so a hook
                                ; taken before the bank is never taken off
    call dos_build_psp
    call dos_hook_vectors

    mov ax, [dos_arena]         ; the DTA starts at PSP:0080 (SPEC.md 96.3)
    add ax, DOS_PSPP
    mov [dos_dtaseg], ax
    mov word [dos_dta], 0x80
    mov ax, [dsk_cwd]           ; ...and we stand where the mount and the goto
    mov [dos_curdir], ax        ; left us, read off the machine rather than
    mov al, [dos_vol]           ; asserted
    call dos_drv_bank
    call dos_date_init          ; the RTC once, or the fallback

    ; --- into the program, and back out of it ----------------------------------
    ; **THE BANKED SP MUST SIT ON A RETURN ADDRESS** (SPEC.md 96.38.2):
    ; dos_terminate restores SS:SP from these two words and JUMPS to
    ; dos_prog_done, which ends in the `ret` that ends dos_fsx_main at its own
    ; entry depth. Banking the plain SP of an entry nothing called points it at
    ; the top of an empty stack.
    mov ax, .back
    push ax
    mov ax, ss
    mov [dos_sv_ss], ax
    mov [dos_sv_sp], sp
    call dos_prog_enter
.back:
    mov al, [dos_exit]
    mov [kd_code], al
    mov byte [kd_code+1], 0         ; IT RAN, so the high byte says nothing -
    jmp kd_leave                    ; and it is cleared HERE rather than at
                                    ; entry, because a refusal may have set it
                                    ; on a previous pass through this image

.nolb:
    mov si, kd_s_nolb               ; **AND EACH ONE NAMES ITSELF** (SPEC.md
    mov al, KDC_FAIL | DER_HAND     ; 96.40.7). They all reported 0xFF, so a
    jmp short .say                  ; program that was never there came home as
.noroom:                            ; `ended, exit code 255` - a sentence about
    mov si, kd_s_noroom             ; a run that did not happen. The reason is
    mov al, KDC_FAIL | DER_MEM      ; the BOX's own DER_*, which this build can
    jmp short .say                  ; see because dos.asm defines them ungated
.nomount:
    ; --- **AND IT SAYS WHICH ONE** (SPEC.md 96.40.4) ------------------------
    ; A refusal that names no drive is a refusal nobody can act on: the field
    ; report it earned was "it cannot mount the disk, regardless of what disk
    ; I tried", which is four facts short of a diagnosis. The volume index IS
    ; the DOS drive (96.6), the unit is what `int 13h` was asked about, and
    ; the two disagreeing is itself the answer on a machine with an unusual
    ; drive table.
    push ax
    mov byte [kd_code+1], KDC_FAIL | DER_GOTO   ; the drive did not mount, so
    mov si, kd_s_nomount                        ; nothing on it could be opened
    call kd_puts
    mov al, [dos_vol]
    add al, 'A'
    call kd_putc
    mov si, kd_s_nomu
    call kd_puts
    push bx
    ; **`dsk_vol_row_x`, NEAR, AND THE INDEX IN BL.** Both halves of that are a
    ; correction: this first went out calling `dskf_dsk_vol_row`, which is the
    ; FORMAT module's far shim and ends in `retf`, so a near call to it popped
    ; the return offset into IP and a stack word into CS. `KERNEL_SEG` is this
    ; segment here (kdos.asm), so the near entry is the one to use - and the
    ; routine takes BL, not AX, which is what made the first attempt print
    ; whatever row the index happened to land on.
    mov bl, [dos_vol]
    call dsk_vol_row_x           ; BX = the row, or CF=1 = no such volume
    jc .nmnorowb
    mov al, [bx+DV_UNIT]         ; THE ROW'S UNIT, not the volume index
    xor ah, ah                   ; (SPEC.md 52.10.3)
    pop bx
    call kd_dec
    jmp short .nmend
.nmnorowb:
    pop bx
    mov si, kd_s_nmnone
    call kd_puts
.nmend:
    mov si, kd_s_nmcr
    call kd_puts
    pop ax
    jmp short .sayd
.nofh:
    mov si, kd_s_nofh
    mov al, KDC_FAIL | DER_MEM      ; no file window IS no room, one carve along
    jmp short .say
.noload:
    mov si, kd_s_noload
    mov al, KDC_FAIL | DER_READ     ; ...and THE ONE THE FIELD REPORTED: a name
                                    ; that resolves and does not open. The box
                                    ; already says `It could not be read.`
.say:
    mov [kd_code+1], al             ; BEFORE kd_puts, which spends AL on the
    call kd_puts                    ; characters it writes
.sayd:
    mov byte [kd_code], 0xFF        ; ...and the low byte is meaningless now,
    jmp kd_leave                    ; which is what KDC_FAIL says out loud

; -----------------------------------------------------------------------------
; kd_giveback - the read-ahead's rungs, into the DOS program's block
; in:  nothing; out: nothing; every register preserved
;
; `kd_shed` a rung at a time until there is none left, then put what came back
; into the three figures `dos_build_psp` is about to read. The arena's top IS
; `[kd_top]` - that is the whole of what 96.44.11 fixes - so this recomputes
; from it rather than adding a delta, which cannot drift.
;
; THE WINDOW MOVES WITH THE TOP. §96.11's file window is carved off the arena's
; ceiling by `dos_fh_setup`, which ran before the load; leaving it where it was
; would put a hole in the middle of the program's block. Nothing owns it yet -
; `dos_load` reads through `dos_be_read` and not through a handle - so moving
; it is a store rather than a flush.
; -----------------------------------------------------------------------------
; **AND THE TWO SIDES OF `Auto` ARE MIRRORED**, which is checked HERE because
; this is the one file that sees both: `apps/dos/dos.asm` prints what Auto is
; worth in KB and `kerndos/kdshim.inc` decides what it really is, and the box's
; own build cannot see the second name at all (`doscore.asm` is KD_BACKEND
; without kdshim.inc). A drift is a figure on the glass the launch disagrees
; with, which is exactly the class of defect no gate catches.
%if DOS_CA_AUTORUN != KD_RAH_KEEP
  %error "DOS_CA_AUTORUN (apps/dos/dos.asm) must equal KD_RAH_KEEP"
%endif
kd_giveback:
    push ax
    push cx
    mov cx, KD_RAH_KEEP         ; **AND THE CALLER DOES WANT A FIGURE NOW**
    push si
    mov si, [kd_lbp]            ; (SPEC.md 96.36.6): the box carries the width
    mov ax, [si+KDL_FLAGS]      ; the user picked, and `[kd_lbp]` is the one
    pop si                      ; thing that knows WHICH block is live
    and ax, KDLF_RAHM           ; as runs + 1 in the flags word, where 0 is
    jz .rung                    ; "your own default" - which is
    mov cl, KDLF_RAHSH          ; KDL_NVOL's rule, so an old block that never
    shr ax, cl                  ; heard of the field still gets KD_RAH_KEEP
    dec ax
    mov cx, ax
.rung:
    mov ax, [dsk_rah_runs]
    cmp ax, cx
    jbe .kept                   ; at a width of 0 the loop runs to the bottom
    call kd_shed                ; exactly as it always did
    jnc .rung
.kept:
    pop cx
    call kd_arena
    mov byte [kd_spent], 1      ; **AND THE ALLOCATOR IS CLOSED** (SPEC.md
                                ; 96.44.11.3). Everything below `[kd_top]` is
                                ; the program's block and its file window from
                                ; here; a mount later in the session would
                                ; otherwise call `dsk_rah_want`, find
                                ; `[dsk_rah_seg]` zero because THIS loop zeroed
                                ; it, and re-claim 32 KB straight through both
    pop ax
    ret

; -----------------------------------------------------------------------------
; kd_arena - the arena, re-read off [kd_top]
; in:  nothing; out: CF=1 = there is no room at all; every register preserved
;
; THE ARENA IS `[kd_floor]` TO `[kd_top]` AND ALWAYS WAS - what 96.44.11 fixes
; is that it was computed once, before the mount claimed, and never re-read.
; Every caller here has just MOVED `[kd_top]`, so this recomputes from it
; rather than adding a delta, which cannot drift.
;
; THE WINDOW MOVES WITH THE CEILING. 96.11's file window is carved off the
; arena's top by `dos_fh_setup`; leaving it where it was would put a hole in
; the middle of the program's block. `[dos_wbytes]` is 0 until that routine has
; run, so the same three stores are right before it and after it. Nothing owns
; the window on any of these paths - `dos_load` reads through `dos_be_read`
; and not through a handle - so moving it is a store rather than a flush.
; -----------------------------------------------------------------------------
kd_arena:
    push ax
    push cx
    push dx
    mov dx, [kd_top]
    sub dx, [dos_arena]
    jbe .no                     ; DX = the arena BEFORE the window, which is
                                ; what `dos_akb` is in the windowed box too
    mov ax, [dos_wbytes]        ; 0 before dos_fh_setup has placed one
    mov cl, 4
    shr ax, cl                  ; bytes -> paragraphs; a window is a cluster
    mov cx, dx                  ; multiple and so paragraph-round (96.11)
    sub cx, ax                  ; CX = the arena the program is TOLD about
    jc .no                      ; `dos_fh_setup`'s OWN floor, repeated so that
    cmp cx, DOS_PSPP + 0x100    ; this routine is total rather than correct only
    jb .no                      ; because every caller happens to RAISE the top

    mov [dos_apara], cx         ; ...and only here does anything land, so a
    mov ax, cx                  ; refusal leaves all four figures as it found
    add ax, [dos_arena]         ; them
    mov [dos_wseg], ax
    mov ax, cx
    sub ax, DOS_PSPP            ; ...and the LOAD geometry's copy of it, which
    mov [dos_ldpara], ax        ; `dos_psp_make` sizes the block and the .COM
    mov ax, dx                  ; stack from
    mov cl, 6
    shr ax, cl
    mov [dos_akb], ax
    pop dx
    pop cx
    pop ax
    clc
    ret
.no:
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; kd_vtab - the KERNEL's volume table, into ours (SPEC.md 96.50)
; in:  SI -> the launch block; DS = ES = KD_SEG
; out: nothing; every register preserved
;
; `kernel/disk.inc`'s `dsk_vtab` is a STATIC initialiser - A: and B: as BIOS
; units 0 and 1, the rest free - which is right for the kernel, where it is
; the starting point a boot probe then edits, and wrong here, where nothing
; ever edits it. The divergence is not exotic: SPEC.md 18.7.1 pins the boot
; partition at ROW 2 and the initialiser has row 2 FREE, so a fixed disk is
; not reachable here at all; and `dsk_fdd_retire` frees row 1 in the kernel
; on a machine with one floppy while nothing frees it here. `dos_vol` is an
; index into whichever table the reader has.
;
; **A ZERO COUNT MEANS KEEP WHAT WE HAVE**, which is KDL_DPT's zero one field
; along and for its reason: the W4 gate stages its own block and knows no
; volumes, so it must be able to say "I cannot fill this" distinctly from
; filling it with zeros - which here would read as eight copies of BIOS unit
; 0, every drive letter pointing at A:.
;
; ONLY `DV_KIND` AND `DV_UNIT` ARE WRITTEN, plus the side table's base. The
; rest of the row is left exactly as the initialiser has it: `DV_SECS` and
; `DV_SEG` are a mount's to fill and writing them here would be staler than
; not, and `DV_FLAGS`/`DV_LBL` are a desktop's.
; -----------------------------------------------------------------------------
kd_vtab:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, [si+KDL_NVOL]
    xor ch, ch
    jcxz .out                   ; nothing staged: the built-in table stands
    cmp cl, DVOL_MAX
    jbe .n
    mov cl, DVOL_MAX            ; a sender with MORE rows than this build has
.n:                             ; is capped rather than refused - the extra
                                ; ones are volumes we could not reach anyway
    lea di, [si+KDL_VTAB]
    xor si, si                  ; SI = the row number again, for kd_vgather's
.row:                           ; reason on the other side
    mov bx, si
    mov ax, cx
    mov cl, 4
    shl bx, cl
    mov cx, ax
    add bx, dsk_vtab
    mov al, [di]
    mov [bx+DV_KIND], al
    mov al, [di+1]
    mov [bx+DV_UNIT], al
    mov bx, si
    shl bx, 1
    shl bx, 1
    add bx, dsk_vbase
    mov ax, [di+2]
    mov [bx], ax
    mov ax, [di+4]
    mov [bx+2], ax
    add di, KDL_VROW
    inc si
    cmp si, cx
    jb .row
    ; ...and which one the kernel booted from, which `dskw_*` reads when a
    ; name carries no drive and nothing else here would set.
    mov si, [kd_lbp]            ; re-read rather than unwind: SI has been the
    mov ax, [si+KDL_HDSPT]      ; row number since .row and the block's own
    mov [dsk_bootspt], ax       ; pointer is a word in the image.
    mov ax, [si+KDL_HDHDS]      ; The GEOMETRY goes with the table: a row for
    mov [dsk_boothds], ax       ; the fixed disk with no spt/heads behind it
                                ; mounts on the floppy fallback and fails
    mov al, [si+KDL_BOOTV]
    cmp al, DVOL_MAX
    jae .cyl
    mov [dsk_bootvol], al
.cyl:
    ; ...and the KERNEL's finding about this machine's FDC (SPEC.md 96.44.14).
    ; It is a whole WORD over here for `dsk_geom_check`'s `cmp word`, and the
    ; block carries one byte, so the high half is cleared rather than left as
    ; whatever the bss clear did NOT reach - which is the defect this field
    ; exists because of.
    xor ah, ah
    mov al, [si+KDL_CYLRUN]
    mov [boot_cylrun], ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; kd_leave - the program is over (docs/plans/KERN-DOS-PLAN.md §9)
;
; **THERE IS NOTHING TO GO BACK TO ON THIS ARM**, which is the first
; deliberately destructive thing this OS does and reads like one: the user is
; to be asked in its own window, at the moment of launch, with Cancel as the
; default - docs/plans/KERN-DOS-PLAN.md §9, which is W7 and is not built. So
; the machine restarts - and it says so first, because a
; screen that goes black on its own is a crash and a screen that says why is a
; machine doing what it was told.
;
; W6's arm hangs off the same byte: with KDLF_REBOOT clear there is a
; hibernation image to put back, and this is where the return stub goes.
; -----------------------------------------------------------------------------
kd_leave:
    ; **INTERRUPTS BACK ON, BECAUSE THE PROGRAM'S EXIT CAME THROUGH A GATE.**
    ; AH=4Ch arrives on an `int 21h`, which clears IF, and `dos_terminate`
    ; restores SS:SP and JUMPS rather than `iret`-ing - so the flags the gate
    ; pushed are never popped and we are back here with IF = 0. In the box
    ; that is invisible; here the very next thing is the ROM, and a BIOS disk
    ; read waits on IRQ6. `int 19h` ran, its read timed out with AH=01, and it
    ; RETURNED - so the machine printed "Press any key to restart" and then sat
    ; in `.hold`'s `hlt` for ever, which reads exactly like a refusal to
    ; restart rather than like a masked interrupt.
    sti
    ; **THE MOUSE GOES QUIET HERE AND NOT LATER** (SPEC.md 96.45.1). Both
    ; exits below hand the machine to somebody else - `int 19h` to the ROM, or
    ; the live restore to os8088 - and our ISR sits at a KD_SEG offset that is
    ; the next owner's code the instant it loads. An unmasked line with a
    ; latched byte behind it is then a wild jump with no way to tell afterwards
    ; where it went, which is exactly what step 6's `mouse_unhook` protects the
    ; handover in the other direction from.
    call kd_mou_stop
%ifndef KD_NO_KBGUARD
    call kd_kbd_stop            ; ...and ours is at a KD_SEG offset too
%endif
    mov si, [kd_lbp]

    ; --- THE SESSION, PUT BACK HERE (SPEC.md 96.49) -------------------------
    ; Before the exit code is printed and before anything is parked: this does
    ; not return when it works, and what it returns to when it does not is the
    ; route W6 shipped - `int 19h`, a whole boot, and os8088's own `hb_probe`
    ; finding the pointer. So the fast path is an optimisation over a route
    ; that still works, never a new way to lose the session.
    call kd_resume              ; ...or CF=1, and on down
    mov si, [kd_lbp]            ; (it spends SI on the block's own fields)

    test word [si+KDL_FLAGS], KDLF_REBOOT
    jz .hold                    ; W6: the return. Until it exists, a block
                                ; that does not ask for the reboot has nowhere
                                ; to go and says so rather than restarting a
                                ; machine the user expected back
    call kd_bda                 ; the exit code, where a boot can find it
    ; --- ...AND WHO IT IS FOR (SPEC.md 96.41.2) -----------------------------
    ; With `KDLF_HIBER` set there is a session on the disk and the code has
    ; just been posted to it, so the number will be on the glass in the box's
    ; own console a few seconds from now - and a *"Press any key"* in front of
    ; it is a machine that stops, in the middle of coming back, for an answer
    ; it does not use. **The line still says why the screen is about to go**,
    ; which is this routine's own manner two exits along, and the wait goes.
    ;
    ; Reaching here at all means `kd_resume` refused, so this arm is the
    ; fallback now rather than the route: SPEC.md 96.49 is what runs when the
    ; image can be read from here, and its own line says so.
    test word [si+KDL_FLAGS], KDLF_HIBER
    jz .noreturn
    mov si, kd_s_ret
    call kd_puts
    jmp short .park0
.noreturn:
    mov si, kd_s_over
    call kd_puts
    mov al, [kd_code]               ; the CODE alone here: this is the sentence
    xor ah, ah                      ; on kern_dos's OWN screen, where the
    call kd_dec                     ; reason has already been printed in words
    mov si, kd_s_boot
    call kd_puts
    xor ah, ah                  ; the ROM's own wait: a program that printed
    int 0x16                    ; its last line deserves to have it read
.park0:

    ; --- the park, AND the DL the ROM boots from (SPEC.md 18.100, 96.40.2) ---
    ; **`int 19h` READS THE BOOT DRIVE OUT OF DL AS IT FINDS IT.** The
    ; bootstrap takes no documented input, so a routine that just calls it
    ; hands the ROM whatever the DOS program left - here 0xA1, which is
    ; `int 13h` answering AH=01 "bad command" twice and the bootstrap RETURNING
    ; rather than booting. It reads as a machine that refuses to restart, and
    ; it sat in `.hold`'s `hlt` for ever with its own message underneath.
    ; kernel/disk.inc's `dsk_fdd_park_x` leaves DL = 0 by falling out of its
    ; own loop, which is why the desktop's Restart has never shown this.
    ;
    ; The resets are that routine's other half and are owed anyway: the FDC has
    ; just run a whole DOS program's worth of I/O, and SPEC.md 18.97's probe on
    ; the next boot decides on drive B's own TRACK 0 line.
    mov dl, 2
.park:
    dec dl                      ; ...1, then 0
    xor ah, ah                  ; AH=00 RESET DISK SYSTEM = recalibrate to
    int 0x13                    ; track 0. No result is read: the int 19h
    or dl, dl                   ; below resets the FDC whatever this leaves
    jnz .park                   ; behind (SPEC.md 18.100)
    mov si, [kd_lbp]            ; **THE BLOCK AGAIN, because the two lines that
                                ; print above took SI for their strings.** It
                                ; was read once at the top and the drive below
                                ; then came out of `kd_s_boot` - a letter,
                                ; which `int 13h` answers AH=01 to and the
                                ; bootstrap returns on
    mov dl, [si+KDL_UNIT]       ; ...and the drive the KERNEL booted from, not
    int 0x19                    ; the 0 the loop left: on a machine with a hard
                                ; disk A: is an empty drive, and the bootstrap
                                ; stops at the ROM's boot menu waiting for a
                                ; key that is never coming
.hold:
    mov si, kd_s_held
    call kd_puts
.h:
    hlt
    jmp .h

; -----------------------------------------------------------------------------
; kd_bda - the exit code, into the BDA's intra-application area (SPEC.md 96.41)
; in:  [kd_code]; the block at [kd_lbp]
; out: nothing; every register preserved
;
; **IT ONLY HAS TO SURVIVE `int 19h` AND A BOOT**, which is what makes this the
; cheapest answer to the plan's own open question 3
; (docs/plans/KERN-DOS-PLAN.md 8.2). `0040:00F0` is sixteen bytes the BIOS sets
; up at POST and never touches again, `int 19h` is the BOOTSTRAP and not POST,
; and os8088's own boot writes nothing below 0x0600 - measured on the machine,
; byte for byte, at a settled desktop.
;
; Only when the kernel said it wrote an image (`KDLF_HIBER`). A record left by
; a run nobody is coming back from would otherwise sit there until something
; read it, and the magic is only half an answer: `hbm_ask` clears it after
; reading, and this writes it only when somebody is going to.
; -----------------------------------------------------------------------------
kd_bda:
    test word [si+KDL_FLAGS], KDLF_HIBER
    jz .out
    push ax
    push es
    push di
    mov ax, KDB_SEG
    mov es, ax
    mov di, KDB_OFF
    mov word [es:di], KDB_SIG & 0xFFFF
    mov word [es:di+2], KDB_SIG >> 16
    mov ax, [kd_code]               ; THE WHOLE WORD: the `xor ah, ah` that was
    mov [es:di+KDB_CODE], ax        ; here threw the reason away, which is the
                                    ; half of 96.40.7 that would have made the
                                    ; flag arrive always zero
    mov ax, [dos_akb]           ; ...and the ARENA it was given (SPEC.md
    mov [es:di+KDB_AKB], ax     ; 96.41.1), which only this host can know
    pop di
    pop es
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; kd_scatter - the block's fields into the DOS core's bss
; in:  SI = the block; DS = ES = KD_SEG
; out: everything clobbered but SI
;
; The table is kdlaunch.inc's and the box emits the identical one, so this is
; the same walk in the other direction. One segment throughout: the stub put
; the block in OUR image (§7 step 7), which is what makes it a plain `movsb`
; and not a segment argument.
; -----------------------------------------------------------------------------
kd_scatter:
    push si
    mov bx, kd_lbtab
    lea si, [si+KDL_BODY]
.row:
    mov cx, [bx+2]
    jcxz .out
    mov di, [bx]
    add di, DOS_CBASE           ; **THE CORE'S BASE AND NOT THIS HOST'S IMAGE
                                ; END** (SPEC.md 96.44.5). Every KDL_FIELDS row
                                ; is a CORE cell, and since the core became a
                                ; part its cells sit at DOS_CBASE - the same
                                ; address in both hosts, which is the whole
                                ; point of the launch block being a wire format
    rep movsb
    add bx, 4
    jmp short .row
.out:
    pop si
    ret

; --- the table this walk reads, and our own idea of the payload's length ----
%macro KDL_F 2
    dw (%1 - DOS_CBASE), %2     ; ...and the offsets are measured from it
%endmacro
kd_lbtab:
    KDL_FIELDS
    dw 0, 0
%unmacro KDL_F 2

%assign KDL_ACC 0
%macro KDL_F 2
    %assign KDL_ACC KDL_ACC + %2
%endmacro
    KDL_FIELDS
%unmacro KDL_F 2
KDL_MINE equ KDL_ACC
%if KDL_ACC + KDL_BODY > KDL_SIZE
  %error "kern_dos: the launch block's payload outgrew KDL_SIZE"
%endif

; --- the ROM's teletype, which is the only screen this machine has ----------
; --- kd_putc - one character, through the ROM's teletype --------------------
kd_putc:
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    pop bx
    pop ax
    ret

kd_puts:
    push ax
    push bx
    push si
.c:
    mov al, [si]
    or al, al
    jz .out
    inc si
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp short .c
.out:
    pop si
    pop bx
    pop ax
    ret

kd_dec:                         ; AX, in decimal, smallest thing that works
    push ax
    push bx
    push cx
    push dx
    mov cx, 0
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop ax
    add al, '0'
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    loop .e
    pop dx
    pop cx
    pop bx
    pop ax
    ret

kd_s_nolb:    db 13, 10, 'kern_dos: no launch block', 13, 10, 0
kd_s_noroom:  db 13, 10, 'kern_dos: no room for a program', 13, 10, 0
kd_s_nomount: db 13, 10, 'kern_dos: could not mount drive ', 0
kd_s_nomu:    db ' (unit ', 0
kd_s_nmnone:  db 'none - no such volume', 0
kd_s_nmcr:    db ')', 13, 10, 0
kd_s_nofh:    db 13, 10, 'kern_dos: no file window', 13, 10, 0
kd_s_noload:  db 13, 10, 'kern_dos: the program could not be loaded', 13, 10, 0
kd_s_over:    db 13, 10, 13, 10, 'os8088: the program has exited, code ', 0
kd_s_boot:    db '.', 13, 10, 'Press any key to restart.', 13, 10, 0
kd_s_held:    db 13, 10, 'kern_dos: nothing to return to.', 13, 10, 0
kd_s_ret:     db 13, 10, 13, 10, 'os8088: restarting to put the session back...', 13, 10, 0

section .bss
kd_code:    resb 2              ; low: the program's exit code. HIGH: zero if
                                ; it really ran, or KDC_FAIL | DER_* if it
                                ; never started (SPEC.md 96.40.7). A WORD so
                                ; the pair moves into KDH_CODE in one store
            resb 1              ; docs/plans/KERN-DOS-PLAN.md §12 question 3

; **THE BLOCK LIVES IN `.bss`, WHICH IS NOBITS**, and the image's fixed header
; is a POINTER to it rather than the block itself (kdlayout.inc's KD_H_LBP).
; The stub has no symbol table, so something at a known offset has to say
; where to write - and a word says it for four bytes where the block itself
; would cost 512 of the part on every disk that ships one.
kd_lblock:
    resb KDL_SIZE

section .text

; --- what the kernel's own kernel.asm would have given the disk layer -------
; `dsk_vol_fixed` is a two-instruction thunk in kernel.asm:7030 and it is
; COPIED here rather than shortened. The obvious shortening - `jmp
; dkf_dsk_vol_fixed`, on the reasoning that `.cold` is not another segment
; here so the call need not be far - is WRONG, because near or far is a
; property of the BODY and not of the distance: `dkf_dsk_vol_fixed` ends in
; `retf` whatever segment it is in. kdback.inc's header has the rest of it.
section .text
dsk_vol_fixed:
    call COLD_SEG:dkf_dsk_vol_fixed
    ret

section .bss
kd_glb:
    resb KDL_SIZE

; --- and the DOS core's own bss ---------------------------------------------
; **IT GOES IN `.bss` AND NOT AT THE END OF AN IMAGE.** In the package,
; `os88_image_end` is where the loader zeroes from (SPEC.md 21 step 5) and the
; DBSS table is offsets from it. Here there is no loader and no image end:
; the same table lands in the root's own `.bss`, addressed through DS exactly
; as before, and the name is all that has to be kept.
section .bss
os88_image_end:
    resb DOS_BSS_SIZE + CON_BSS

section .text
