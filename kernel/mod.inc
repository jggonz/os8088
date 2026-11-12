; =============================================================================
; os8088 - mod.inc
;
; On-demand kernel modules (SPEC.md 2.8, docs/plans/completed/ONDEMAND-PLAN.md): kernel code
; that lives in a file rather than in KERNEL.SYS, is read into a heap claim
; when the feature is asked for, and is freed when the feature is done.
; Prefix `mod_`.
;
; **This is `.cold` with the address changed, and nothing else.** A module is
; assembled as part of THIS kernel, in a section of its own at vstart=0, and
; tools/os88mod.py cuts it out of the binary afterwards. So it keeps every one
; of cold code's properties (SPEC.md 2.6) - CS of its own, DS still
; KERNEL_SEG, out through the cw_* shims, near `ret`s throughout - and the
; only thing that changes is that its inbound thunk names a segment out of
; `mod_fp` instead of the constant COLD_SEG.
;
; Being part of the same assembly is the load-bearing property. Every kernel
; address a module names - `cp_sel`, `dsk_secbuf`, a string in .text - is the
; one the kernel itself uses, resolved by the same pass, so a module cannot
; hold a stale or second opinion about where anything is. The build stamp in
; its header (SPEC.md 2.8.2) refuses the one case that would break that: a
; module file left over beside a kernel it was not built with.
;
; WHAT MAY BE A MODULE is not a question about code (docs/plans/completed/ONDEMAND-PLAN.md 1):
; a feature qualifies only if the system disk is already required to use it,
; or can be required without interrupting what the user was doing - because on
; a one-floppy machine every load is a DISK SWAP. The Control Panel qualifies
; because it writes SYSTEM.CFG to that disk when it closes; the floppy
; formatter qualifies because preparing a disk is an operation with a trip to
; the disk box already in it; the disk CLONER (SPEC.md 18.99) qualifies for
; the formatter's reason with one more term in its favour, since a same-drive
; clone has TWO trips in it already. The Standard File dialog and
; Cut/Copy/Paste do not, and no amount of seam width changes that.
;
; NOTHING HERE CAN STOP ANYTHING. Every failure - no system disk, no heap, a
; short read, a stamp mismatch - is CF = 1 out of mod_need, and every caller
; owes a refusal the user can act on (SPEC.md 47 rule 3, and 59's toast is
; where all four of the current ones say it).
; =============================================================================

%ifdef FCP_MOD
MOD_SER     equ 5
MOD_NIMG    equ 6               ; SMALL: file operations, no hibernate, and
                                ; no Dock module (SPEC.md 30.5)
%else
MOD_DOCK    equ 4
MOD_EXT     equ 5               ; ...and the extended desktop (SPEC.md 39.19.6)
MOD_WIN     equ 6               ; ...and the Windows theme's chrome (76.14)
MOD_DESK    equ 7               ; ...and the desk layer's verbs (11.103.10)
MOD_STIKM   equ 8               ; ...and the Sticky Apps menu (SPEC.md 110.12)
MOD_SER     equ 9
MOD_NIMG    equ 10              ; BIG: hibernate, resident file operations,
                                ; the optional Dock, EXTD.DRV, WIN95.DRV,
                                ; DESK.DRV and STIKMENU.DRV
%endif
; FSCHECK.DRV (SPEC.md 118.14.1) is an image os88mod.py cuts and NO row: the
; package that uses it loads it, so it takes the id after every image's.
%ifdef OS88_FSCKIMG
MOD_FSCK    equ MOD_NIMG
MOD_NFSCK   equ 1
%else
MOD_NFSCK   equ 0
%endif
; MOD_NIMG is the IMAGES os88mod.py cuts out; MOD_MAX the ROWS. They differ by
; one row that is not an image of its own: MOD_SETS, CTRL.DRV's first
; MODS_SIZE bytes - the settings core a desktop gesture loads instead of the
; whole panel (SPEC.md 2.8.7). LAST, so every image keeps its id.
%ifdef OS88_SHORTCUTS
MOD_SETS    equ MOD_NIMG
MOD_KBM     equ MOD_NIMG + 1
%else
MOD_KBM     equ MOD_NIMG
%endif
; ...and MOD_KBM, the second row that is no image os88mod.py cuts: the
; keyboard mouse's pointer half, `.boot3m`, ADOPTED FROM MEMORY at the boot's
; end on a machine with no pointer and dropped when one first speaks
; (SPEC.md 2.8.8, 9.6.7). No file on any disk names it, so mod_need is never
; asked for it - kbm_adopt (mouse.inc) claims and arms it itself. LAST, for
; MOD_SETS's reason
MOD_MAX     equ MOD_KBM + 1
MOD_CTRL    equ 0               ; the Control Panel (SPEC.md 31)
MOD_FMT     equ 1               ; the floppy formatter (SPEC.md 18.96)
MOD_DISKOPS equ 2               ; DISKOPS.DRV: the cloner, Write Img and
                                ; the compressor (SPEC.md 18.99)
%ifdef KERN_BIG
MOD_HIBER   equ 3               ; hibernate and resume (SPEC.md 87) - kern_big
%endif                          ; alone: the 128KB machine has no hard disk to
                                ; hibernate to, so it carries no ROW either
%ifdef FCP_MOD
MOD_FCP     equ 3               ; Cut/Copy/Paste (SPEC.md 22.3-22.5),
                                ; kern_small's alone - kern_big keeps the
                                ; bodies resident in `.cold`
MOD_FDLG    equ 4               ; ...and the Standard File dialog (SPEC.md 38,
                                ; 38.0), on the same terms
%endif
                                ; The LZB compressor (SPEC.md 20.15) has NO
                                ; row: it rides in the cloner's image as its
                                ; second entry (20.15.3), on both builds, so a
                                ; row, a name and a mod_fp block are what it
                                ; does not cost

; THERE IS NO MOD_NENT (SPEC.md 2.8.1). Each module declares its own X_NENT
; beside its header, and its slot block in mod_fp is exactly that long - so an
; entry costs 4 bytes of .bss and a module pays for no slot it does not arm,
; and there is no ceiling. mod_fpt maps a row to its block, and the NEXT
; block's start bounds it, so the count is a subtraction and no row carries it.

; --- the module image header, at offset 0 of the file ------------------------
; The one datum in a module image, and it is read through ES by the LOADER,
; never through DS by the module - so SPEC.md 2.6's "cold code carries no
; data" survives intact.
MOD_H_MAGIC equ 0               ; word: 0x384F ('O','8')
MOD_H_VER   equ 2               ; byte: 5 = kernel module (a package is 8, a
                                ; driver 9), so ld_check_hdr and drv_check
                                ; both refuse it - two independent gates, the
                                ; discipline SPEC.md 51.1 already uses
MOD_H_ID    equ 3               ; byte: which MOD_* row this image is
MOD_H_BUILD equ 4               ; word: the build number of the kernel this
                                ; was assembled with (SPEC.md 2.8.2)
MOD_H_STAMP equ 6               ; word: this kernel's LAYOUT, which the build
                                ; number cannot answer for - `make small`,
                                ; `make VIDEO=cga` and a FONT= build are all
                                ; the same commit and put the kernel's data at
                                ; different addresses, so a module built for
                                ; one would far-call correct offsets into the
                                ; wrong variables. It is the section sizes
                                ; summed, which no two configurations of one
                                ; commit have ever shared
MOD_H_IMG   equ 8               ; word: image size = the file's whole size,
                                ; the bss's zeros included (kernel.asm's
                                ; MODB_TAIL) - and so the claim's
MOD_H_NENT  equ 10              ; word: entries that follow
MOD_H_SCR   equ 12              ; word: how many of the image's LAST bytes are
                                ; SCRATCH (SPEC.md 2.8.3) - the only bytes of
                                ; it the module ever writes, each written
                                ; before it is read or expected as a zero. 0
                                ; = the image is freed at its drop; non-zero =
                                ; it is KEPT, as a purgeable claim, with those
                                ; bytes zeroed again, so a re-adopted image is
                                ; the file's own. A module opts in when its
                                ; tail is zeros in its file and every byte it
                                ; writes outside it is written before it is
                                ; read (every image with a file does now);
                                ; the header says so, no kernel table does.
                                ; ONE result
                                ; lives just below it on purpose: STIKMENU.
                                ; DRV's kept scan, valid only on a claim a
                                ; scan filled (SPEC.md 2.8.3, 110.12.2)
MOD_H_ENT   equ 14              ; word[]: entry offsets, in this image
MOD_VER     equ 5
MOD_MAX_KB  equ 16              ; the biggest module image we will claim,
                                ; image + bss: mod_need bounds the BYTES

; The layout stamp (SPEC.md 2.8.2). Forward references to the section sizes,
; which is legal in a `dw` and is how MODC_SIZE already reaches its own end
; label - and it has to be, because every module is assembled before the sizes
; are known. Summed rather than hashed: the point is that two builds of one
; commit differ, and a section size differing by ONE byte moves every address
; above it.
MOD_BNUM    equ BUILD_NUM       ; ...and the build number as an ASSEMBLER number,
                                ; for a header met before apps.inc %includes
                                ; buildnum.inc: mouse.inc's `.boot3m` (SPEC.md
                                ; 2.8.8). BUILD_NUM is a %define, which cannot
                                ; be named ahead of its line; an equ can
%ifdef KERN_MEGA                ; ...and `.mega`'s, or code moving from `.text`
MOD_STAMP   equ (KTEXT_SIZE + KBSS_SIZE + COLD_SIZE + MEGA_SIZE + KLOW_SIZE) & 0xFFFF
%else                           ; to `.mega` would keep the sum and move every
MOD_STAMP   equ (KTEXT_SIZE + KBSS_SIZE + COLD_SIZE + KLOW_SIZE) & 0xFFFF
%endif                          ; offset (KERN-MEGA-SEGMENTS-PLAN 3.5)

; --- the table row, stride MODR_SIZE -----------------------------------------
MODR_SEG    equ 0               ; dw: the segment it is loaded at, 0 = not
MODR_FILE   equ 2               ; dw: -> its NUL 8.3 file name (.text)
MODR_SIZE   equ 4               ; ...and that is the whole row. It was 8: two
                                ; live words, a `dw: KB its claim took` that
                                ; three sites STORED and nothing ever read,
                                ; and two bytes of padding to keep the stride
                                ; a power of two. The dead word went first and
                                ; the padding with it - the live part had been
                                ; the first four bytes for a while, and MOD_MAX
                                ; was THREE then, so the pad was wasting six.
MODR_SHIFT  equ 2               ; ...still a power of two, so the id -> row
                                ; conversion is shifts and never a multiply.
                                ; ONE SITE reads it now - mod_row, which
                                ; unrolls it as two `shl bx,1`. The three
                                ; `mov cl, MODR_SHIFT / shr ax, cl` that took
                                ; the id back OUT of a row pointer are gone:
                                ; the only caller that wanted it (mod_check)
                                ; is handed the id in DI instead, and the
                                ; conversion is now one-way.
%if (1 << MODR_SHIFT) != MODR_SIZE
  %error "MODR_SHIFT and MODR_SIZE disagree: mod_row SHIFTS a module id up into a row pointer while every other site SHIFTS one back down, and a stride that is not 1<<MODR_SHIFT makes those two operations different functions - which reads one row and writes another, silently"
%endif
%ifdef MOD_CACHE
%if MODR_SEG != 0 || MODR_SIZE != 4 || MOD_MAX * 2 > MEM_P_MOD_N
  %error "MEM_P_MOD + 2 x the id must land on the row's MODR_SEG through mem_pg_own's `base + 2d` (memory.inc), inside the tag's range"
%endif
%endif
section .text
; DATA stays in the kernel segment, reached through DS, exactly as a cold
; module's own data does (SPEC.md 2.6). The CODE below is .cold, and that is
; not a tidiness choice: in .text it would have been ~430 bytes against the
; 438 the kernel had left under KERN_CODE_MAX when this was written, and that
; guard cannot be raised by anybody. Being cold also means the module thunks
; that call mod_need are in the same segment and reach it with a NEAR call,
; so the pair of cw_* shims the first draft needed does not exist.

%ifdef DOCK_OPT
mod_f_dock: db 'DOCK.DRV', 0
%endif
%ifdef KERN_BIG
mod_f_ext:  db 'EXTD.DRV', 0
mod_f_win:  db 'WIN95.DRV', 0
mod_f_desk: db 'DESK.DRV', 0    ; SPEC.md 11.103.10: QUERY loads it
mod_f_stikm: db 'STIKMENU.DRV', 0 ; SPEC.md 110.12: the Sticky Apps drop loads it
%endif
mod_f_ctrl: db 'CTRL.DRV', 0    ; .DRV for the attributes and nothing else
mod_f_fmt:  db 'FORMAT.DRV', 0  ; (SPEC.md 19.6): os88disk.py stamps anything
mod_f_dops: db 'DISKOPS.DRV', 0 ; ...and a third (SPEC.md 18.99; CLONE.DRV
                                ; until it held more than the cloner)
%ifdef KERN_BIG
mod_f_hib:  db 'HIBER.DRV', 0   ; ...and a fourth: hibernate (SPEC.md 87),
                                ; which qualifies twice - the user is leaving
                                ; the machine, or has just booted it
%endif
%ifdef FCP_MOD
mod_f_fcp:  db 'FILECP.DRV', 0  ; ...and kern_small's fifth (SPEC.md 22.3)
mod_f_fdlg: db 'FDLG.DRV', 0    ; ...and its sixth (SPEC.md 38.0)
%endif
                                ; (the compressor has no file: SPEC.md 20.15.3)
                                ; ending DRV read-only + hidden + system by
                                ; EXTENSION, the installer's `*.DRV` copy rule
                                ; picks it up, and neither of these is in
                                ; drv_tab, so nothing lists them as drivers or
                                ; tries to attach one

; THE HEADER mod_check EXPECTS, as the image carries it (kernel size pass 9):
; the magic, the version, an id byte mod_check stamps per call, this build's
; number and this kernel's layout stamp - four words one `repe cmpsw` asks in
; place of four compares with an immediate each. It is .text because it is
; WRITTEN (the id), and data so MOD_STAMP's self-reference cannot reach it:
; a `dw` is two bytes whatever value it holds.
%if MOD_H_MAGIC != 0 || MOD_H_VER != 2 || MOD_H_ID != 3 || MOD_H_BUILD != 4 || MOD_H_STAMP != 6
  %error "mod_hdrt is the image header's first four words, in MOD_H_* order"
%endif
mod_hdrt:   dw 0x384F           ; MOD_H_MAGIC
            db MOD_VER, 0       ; MOD_H_VER, MOD_H_ID (mod_check's to stamp)
            dw BUILD_NUM        ; MOD_H_BUILD
            dw MOD_STAMP        ; MOD_H_STAMP

mod_f_serial: db 'SERIAL.DRV',0 ; SPEC.md 115: explicit Connect loads it
mod_f_kbm:  db 'KBMOUSE', 0     ; SPEC.md 2.8.8: no file has this name - it is
                                ; what the RAM view calls the claim
mod_tab:
    dw 0, mod_f_ctrl            ; MOD_CTRL
    dw 0, mod_f_fmt             ; MOD_FMT
    dw 0, mod_f_dops            ; MOD_DISKOPS
%ifdef KERN_BIG
    dw 0, mod_f_hib             ; MOD_HIBER
%endif
%ifdef FCP_MOD
    dw 0, mod_f_fcp             ; MOD_FCP
mod_r_fdlg:                     ; ...and this one is NAMED, because SPEC.md
    dw 0, mod_f_fdlg            ; 38.0.1's resident guard asks "is the image
                                ; held?" and that question is this word. A
                                ; label rather than mod_tab + MOD_FDLG *
                                ; MODR_SIZE at the call site: ui.inc is
                                ; %included ABOVE this file, so the two `equ`s
                                ; are not defined there and a forward label is
                                ; the only spelling that works in both.
                                ; Under FCP_MOD and not FDLG_MOD because that
                                ; is where the ROW is; the two are defined
                                ; together by KERN_SMALL, and decoupling them
                                ; is an undefined symbol rather than a silent
%endif                          ; read of the wrong word

%ifdef DOCK_OPT
mod_r_dock:
    dw 0, mod_f_dock            ; MOD_DOCK, kern_big's alone (SPEC.md 30.5)
%endif
%ifdef KERN_BIG
    dw 0, mod_f_ext             ; MOD_EXT, kern_big's alone (SPEC.md 39.19.6)
    dw 0, mod_f_win             ; MOD_WIN, kern_big's alone (SPEC.md 76.14)
    dw 0, mod_f_desk            ; MOD_DESK, kern_big's alone (SPEC.md 11.103.10)
    dw 0, mod_f_stikm           ; MOD_STIKM, kern_big's alone (SPEC.md 110.12)
%endif
    dw 0, mod_f_serial          ; MOD_SER, both builds (SPEC.md 115)
%ifdef OS88_SHORTCUTS
    dw 0, mod_f_ctrl            ; MOD_SETS: the SAME file, cut short (2.8.7)
%endif
mod_r_kbm:                      ; MOD_KBM, NAMED for mod_r_fdlg's reason:
    dw 0, mod_f_kbm             ; kbm_adopt (mouse.inc) stores its claim here
mod_tab_end:                    ; (a label and no byte: tools/os88rom.py
                                ; counts the ROWS by it, MOD_SETS included,
                                ; because the ROM answers per row)

; mod_fpt - each module's slot block by id, and one word more: where the LAST
; block ends. Emitted by kernel.asm's MODFP macro beside the blocks themselves
; (below every module's %include), so the two lists are one list.
MOD_TAB_OFF equ mod_tab - $$    ; mod_tab AS A NUMBER. .text has vstart=0
                                ; (kernel.asm), so this IS the address every
                                ; `mov bx, mod_tab` loads - and a number can
                                ; be multiplied where a label cannot: nasm
                                ; 3.02 refuses `mod_tab*7` in mod_fpr below
                                ; as an invalid operand, in every spelling

section .cold                   ; ...and the loader itself

; -----------------------------------------------------------------------------
; mod_gone - what every unloaded entry slot points at
; out: CF = 1
;
; A `retf`, because the slots it fills are far-call targets. Nothing should
; ever reach it - every thunk calls mod_need first - so this is the answer to
; "what if one does" being a refusal rather than a jump into whatever claimed
; that memory next. Zeroing the slot instead would far-call 0000:0000, which
; is the divide-by-zero vector.
; -----------------------------------------------------------------------------
mod_gone:
    stc
    retf

; -----------------------------------------------------------------------------
; mod_row - AL = a module id -> BX = its row
; clobbers: BX, AX, flags
;
; AH, because `cbw` is a byte where `mov bh, 0` is two. It SIGN-extends, so an
; AL past 0x7F would give a negative row offset where `mov bh,0` gave a large
; positive one - both out-of-table garbage, and every caller passes a MOD_*
; constant or the [fm_swapwho]-selected one, so no reachable input differs.
; All three callers bank AX round the call - mod_need and mod_drop on the KENT
; ladder, mod_live's two by push/pop - so the row is built in AX, where the
; add of the table's base is the accumulator's 3-byte form, and handed over
; with a 1-byte xchg (kernel size pass 17, -2).
; -----------------------------------------------------------------------------
mod_row:
    cbw
    add ax, ax
    add ax, ax                  ; MODR_SHIFT, unrolled rather than borrowing CL
    add ax, mod_tab
    xchg bx, ax
    ret

; -----------------------------------------------------------------------------
; mod_live - is module AL resident RIGHT NOW? (SPEC.md 2.8, 13.8.3)
; in:  AL = a module id
; out: CF = 0 resident, CF = 1 not; BX = its row either way, and AX spent
;      (mod_row's, kernel size pass 17 - both callers bank it); nothing else
;      touched - which is still true, and the only thing that changed is
;      WHICH flags carry the answer. `cmp word [bx+MODR_SEG], 1` sets CF iff
;      the word is below 1 unsigned, and for a 16-bit word that is exactly
;      "is zero" - the question this routine asks. ZF/SF/OF now differ on the
;      resident arm; its callers (hiber.inc's hbf_onup and desksc.inc's
;      sc_modcall, the complete set - ctrl.inc's two edges ask the slot's own
;      mod_gone now, kernel size pass 14) read CF alone, and the contract
;      never named the rest.
;
; mod_need without the loading. The callers are the ones that can only ever
; follow a gesture the module itself took - a W_ONMOUSEUP (or, once, a
; W_ONDRAG) after
; a W_ONCLICK that loaded it (SPEC.md 13.8.3) - where "not resident" means the
; arm is stale and there is nothing to fire. Loading the Control Panel off a
; FLOPPY to answer a mouse movement would be absurd, and on the drag path
; mod_need would try it eighteen times a second.
;
; A HELD MODR_SEG IS NOT AN ARMED ROW on a kernel that KEEPS dropped images
; (MOD_CACHE, SPEC.md 2.8.3): a kept one is held and disarmed. So there the
; answer is mod_need's own LIVE test - the row's first slot names its
; segment - which a KEPT row (its slots on mod_gone's, `.cold`'s) fails.
; Both rows asked here, MOD_CTRL and MOD_HIBER, are kept since every image
; opted in; without the test sc_modcall would run a desktop gesture through
; a kept panel's disarmed slot and do nothing. DI and CX are banked round
; mod_fpr, which spends them
; -----------------------------------------------------------------------------
mod_live:
    call mod_row
    cmp word [bx+MODR_SEG], 1   ; CF = 1 iff the word is 0 - see the banner
%ifdef MOD_CACHE
    jc .ret                     ; nothing held
    push di
    push cx
    call mod_fpr
%ifdef KERN_MEGA
    COLDSEG_TO ax               ; kern_mega: a LIVE slot names the image's
%else                           ; code half - MODR_SEG above 1MB, MODR_SEG - 8
    mov ax, [bx+MODR_SEG]       ; below it - so the test is the KEPT one: a
%endif                          ; disarmed slot names mod_gone's segment,
    cmp [di+2], ax              ; `.cold`'s, which is this CS (COLDSEG_TO)
    pop cx
    pop di
%ifdef KERN_MEGA
    clc
    jne .ret                    ; kern_mega LIVE: not mod_gone's, CF = 0
%else
    je .ret                     ; ZF = 1 and CF = 0: LIVE
%endif
    stc                         ; KEPT, disarmed: not resident for a caller
.ret:
%endif
    ret

; -----------------------------------------------------------------------------
; mod_fpr - BX = a ROW -> DI = its slot block, CX = its slot count
; clobbers: DI, CX, flags.  BX SURVIVES, which is what deletes the two round
;           trips its callers used to make through the stack.
;
; Blocks are per module and exactly X_NENT long (kernel.asm's MODFP), so the
; row cannot be multiplied into a block any more: mod_fpt holds each block's
; start and the next one's start bounds it. mod_check compares CX against the
; header's own count, mod_disarm loops on it.
; -----------------------------------------------------------------------------
mod_fpr:                        ; BX = a ROW -> DI = its slot block, CX = its
    mov di, bx                  ; slot count. Row i is mod_tab + 4i, so BX>>1 is
    shr di, 1                   ; (mod_tab>>1) + 2i EXACTLY whatever mod_tab's
    mov cx, [di + mod_fpt + 2 - (MOD_TAB_OFF >> 1)]   ; parity (4i is even)
    mov di, [di + mod_fpt - (MOD_TAB_OFF >> 1)]
    sub cx, di
    shr cx, 1
    shr cx, 1
    ret

; -----------------------------------------------------------------------------
; mod_need - make sure a module is loaded (SPEC.md 2.8.1)
; in:  AL = MOD_*
; out: CF = 0 it is loaded and its mod_fp slots are live; CF = 1 it is not,
;      and nothing was left half-done
; clobbers: FLAGS ONLY - every register is preserved, including AX
;
; **Every register, because this sits under a thunk whose caller's arguments
; are already in them.** cp_onclick takes CX/BX, dskw_format takes AL; the
; thunk pushes AX to make room for the id and pops it back, and this routine
; owes it everything else. That is drv_svc_call's lesson (SPEC.md 51.2) in the
; one place where the argument set is not even known here.
;
; The order is drv_load's, minus everything that is about being a driver: no
; table row to publish, no class, no DRVV_ATTACH, no tier. drv_vol_bank /
; drv_vol_back bracket it for SPEC.md 51.5.2's reason - the user may be
; anywhere, and going to the system volume is a file operation and not a
; navigation - and the compare inside them makes the common case free.
;
; **Every call below is NEAR**, because driver.inc, memory.inc and diskw.inc
; are all in the cold set and so is this file. Routing them out through a cw_
; shim and back in through a resident thunk is SPEC.md 2.6's double crossing -
; six segment transitions where a near call does - and the sweep that found
; 139 of those deleted the shims this file was written against.
; -----------------------------------------------------------------------------
mod_need:
    call kentc_bp               ; KENT: push ax..bp






    push es

%ifdef OS88_SHORTCUTS
    cbw                         ; AH is the CALLER'S (sc_modcall's SC_OP_*),
%endif                          ; and DI is compared whole against MOD_SETS
    mov di, ax                  ; THE ID, FOR THE WHOLE ROUTINE, and in a
                                ; register rather than on the stack: it used
                                ; to be pushed here and popped by both exit
                                ; arms, and nothing between read it -
                                ; mod_check's own banner said the pushed copy
                                ; was NOT read. DI survives every callee
                                ; below (drv_vol_bank/back and drv_mounted
                                ; preserve everything, drv_find clobbers AX
                                ; alone, mem_claim_hi_x "every other register
                                ; preserved" - including through mem_compact,
                                ; whose mem_reloc_call is "all registers
                                ; preserved" - and dskw_read_x banks BP/DI
                                ; itself), and mod_check reads it instead of
                                ; deriving the id back out of the row pointer
    call mod_row
%ifdef MOD_CACHE
    call drv_vol_bank           ; FIRST here, so .held's re-adopt leaves
                                ; through .back like a load (it only
                                ; records: no volume moves until a read)
.reread:                        ; (.held's: a KEPT image the UI task shed or
    mov dx, [bx+MODR_SEG]       ; moved between this read and its IF = 0)
    or dx, dx                   ; CF = 0, which a LIVE row answers with
    jnz .held                   ; ...held: LIVE, or KEPT by a drop (2.8.3)
%else
    cmp word [bx+MODR_SEG], 0
    jne .out                    ; already in: `cmp x, 0` cannot borrow, so CF
                                ; = 0 is the answer (kernel size pass 17, -3:
                                ; an `.already` arm that said `clc` and
                                ; jumped here)
%endif

%ifndef MOD_CACHE
    call drv_vol_bank                      ; this goes to the system volume to find the
%endif                          ; file, and the user may have been anywhere
%ifdef ROM_ADOPT
    ; THE ROM FIRST (docs/plans/ROM-PLAN.md 4.3, SPEC.md 2.10.5): a ROM that
    ; adopted this kernel may carry the image too, cut from the same build, so
    ; the load is a copy out of F4000-FDFFF and needs no system disk at all -
    ; which on a one-drive machine is the difference between a Save As onto a
    ; data floppy and a refusal. NO TEST OF [api_coldseg] FIRST: a kernel
    ; whose `.cold` is in RAM has rom_mfp at rom_none, which refuses SIZE, so
    ; the disk arm is reached the same way a ROM that holds no copy sends it
    ; there (kernel size pass 12, -10 with the COPY below)
    mov al, ROMF_SIZE
    call far [rom_mfp]          ; CF = 0: AX = the image's bytes in the ROM
    jnc .sized
%endif
    call drv_mounted           
    jc .fail

    mov si, [bx+MODR_FILE]
    call drv_find                             ; AX = the file's size, refused over 64KB
    jc .fail
.sized:
    cmp ax, MOD_H_ENT + 2       ; a header with at least one entry
    jb .fail
    mov bp, ax                  ; THE FILE'S BYTE SIZE, in the register this
                                ; routine already banks and never spends -
                                ; it was a .bss word read five times, four of
                                ; them through a disp16 operand. mod_check
                                ; takes it as an input
%ifdef OS88_SHORTCUTS
    cmp di, MOD_SETS            ; ...or the SETTINGS CORE's, which is only the
    jne .whole                  ; file's first MODS_SIZE bytes (SPEC.md 2.8.7):
    mov ax, MODC_SIZE           ; the read stops there, but the CLAIM is the
    mov bp, MODS_SIZE           ; WHOLE image's, so the module's bss tail
    mov byte [dskw_trunc], 1    ; (cpc_buf) is inside it on either load. Both
.whole:                         ; are this assembly's constants, like every
%endif                          ; offset the module is called at - never the
                                ; file's size, which a short file would shrink

    cmp ax, MOD_MAX_KB * 1024   ; the bound FIRST, on the bytes: the round-up
    ja .fail                    ; below carries for a size near 64KB
    call mem_bytes_gp_x         ; -> paragraphs, whole granules (SPEC.md
                                ; 50.2.1): the FILE, whose last bytes are the
                                ; module's bss as zeros (kernel.asm's
                                ; MODB_TAIL), so this claim holds image and bss
                                ; with no figure of either in the kernel
    push bx
    mov bx, MEM_K_MOD
    call mem_claimp_hi_x        ; a module's base IS its CS, so it can never
    pop bx                      ; move: top down, like a package's region
    jc .fail
    mov [bx+MODR_SEG], dx

    push bx
    mov es, dx                  ; ...and NO second `mov si, [bx+MODR_FILE]`:
    mov cx, bp                  ; SI is still the one loaded for drv_find, and
%ifdef ROM_ADOPT                 ; mem_claim_x banks SI/DI around everything
    mov al, ROMF_COPY           ; including the compaction (memory.inc, mem_claim_x).
    call far [rom_mfp]          ; ...or the ROM has the bytes: the first CX
    jnc .rdone                  ; of the image to ES:0, and the read's own
                                ; answer, AX = CX and DX = 0. A ROM that holds
                                ; none - and rom_none - refuse with every
                                ; register kept, so the disk read below gets
                                ; the SI, CX and ES it always had. SIZE said
                                ; yes iff COPY will, so no mark is carried
%endif
    xor bx, bx
    xor dx, dx                  ; DX:CX = the capacity, DX:AX the answer
    call dskw_read_x                         ; the whole file, by name, on the system disk
.rdone:
%ifdef OS88_SHORTCUTS
    mov byte [dskw_trunc], 0    ; ...ONE read's worth, and spent whatever it
%endif                          ; answered
    pop bx
    jc .freefail
    cmp ax, bp                  ; DX is 0 or the read would have refused
    jne .freefail
    jmp short .check            ; the check is written in BELOW the answers
                                ; and not here, where it would push every
                                ; early refusal's `.fail` past a short jump
.badarm:
    call mod_disarm             ; a half-armed block is the one state nothing
                                ; downstream could survive - and it falls
                                ; into the refusal every other check takes
.freefail:
    call mod_free_row           ; the claim goes back; nothing was armed
.fail:
%ifdef OS88_SHORTCUTS
    mov byte [dskw_trunc], 0    ; a claim that failed never reached the read
%endif                          ; that clears it, and every read after would cut
    stc
.back:                          ; ...and the SUCCESS path joins here, CF = 0:
    call drv_vol_back           ; it keeps the flags (pushf .. popf), so one
                                ; call answers both ways (kernel size pass 16)
.out:
    jmp kretc_es

%ifdef MOD_CACHE
; --- A HELD ROW (SPEC.md 2.8.3): LIVE - MEM_K_MOD, armed, and the answer is
; CF = 0 as it always was - or KEPT by its drop, MEM_P_MOD + 2 x its id, and
; then RE-ADOPTED here with no disk read: the claim pinned again, and its
; slots armed by the same loop a load arms them with. Nothing else a load
; does is owed: the scratch was zeroed at the drop, the stamps were checked
; on the read the image came from, and mod_cfix (or the ROM's fix) re-pointed
; its `.cold` words then and is not run twice - `.cold` never moves after
; the boot, which slides it before the heap exists. DX = the segment, BX =
; the row
.held:
    call mod_fpr                ; DI = its slot block. LIVE: the slots name
%ifdef KERN_MEGA                ; its segment, and that is the whole test -
    COLDSEG_TO ax               ; no walk on a thunk's hot path. KEPT: they
    cmp [di+2], ax              ; read mod_gone's, `.cold`'s. kern_mega asks
    clc                         ; the KEPT question (mod_live's note): a
    jne .out                    ; live slot there names a code half, which is
%else                           ; MODR_SEG or MODR_SEG - 8
    cmp [di+2], dx
    je .out
%endif
    pushf                       ; AN IF = 0 WINDOW from the check to the re-tag
    cli                         ; (SPEC.md 2.8.3): a mod_need on ANY task may
    cmp dx, [bx+MODR_SEG]       ; re-adopt - a package worker's SERIAL.DRV
    jne .race                   ; included - and only the UI task sheds or
                                ; compacts (66.3.1), so a shed (MODR_SEG 0)
                                ; or a move (another base) since the read
                                ; above is seen here and read again, and
                                ; none can land between this and the tag
%ifdef KERN_MEGA
    call mem_ishidx             ; A POOL PAIR (SPEC.md 2.8.3.3), or the
    jc .hlow                    ; conventional claim every other kernel keeps
    push bx                     ; THE POOL PAIR: its block's
    xchg ax, dx                 ; purgeable mark comes off - BX = 0 - so no
    xor bx, bx                  ; shed can take it from here; still under
    call MEGA_SEL_CODE:mega_modkeepf ; the IF = 0 the shed's whole scan runs
    xchg ax, dx                 ; under (mx_shedf). DX = the code selector
    pop bx
    add dx, 8                   ; ...and ES its DATA half: .arming stores
    jmp short .hset             ; ES - 8, the code half, in every slot
.hlow:                          ; BELOW 1MB in protected mode: MODR_SEG is
                                ; already the record's DATA selector, and the
                                ; re-adopt is every kernel's, below
%endif
    push bx
    mov bx, dx                  ; mem_find_own's own-segment arm: the record
    call mem_find_own           ; AT that base - a held MODR_SEG names one
    pop bx
    mov word [ss:si+MC_OWN], MEM_K_MOD ; PINNED again before a slot names it
                                ; - by the TAG: mem_can_move refuses
                                ; MEM_K_MOD whatever MC_RLOC says, so the
                                ; drop's declaration (mem_region_reloc) is
                                ; left in the record, inert until the next
                                ; drop re-tags it. Withdrawing it was 5 bytes
                                ; here and 4 more of relaxed branches below
%ifdef KERN_MEGA
.hset:
%endif
    popf
    mov es, dx
%ifdef KERN_MEGA
    mov bp, [es:MOD_H_IMG]      ; the image's bytes, as its read had them:
                                ; mod_hi below COPIES a low one above 1MB now
                                ; that the pool may have room (mod_hiboot's
                                ; BP), and -1 would copy nothing
%else
    or bp, -1                   ; no bound on the entries: this image's were
%endif                          ; bounded by the read it came from
    jmp .arming
.race:
    popf
    jmp .reread
%endif

.check:

; -----------------------------------------------------------------------------
; mod_check - validate a loaded image and arm its entry slots: WRITTEN IN HERE,
; its one caller (kernel size pass 16, -9: the call, the CF answer and the test
; of it, both rets; a refusal jumps to .freefail and a half-armed block
; disarms on the way there, through .badarm)
; in:  BX = the row with MODR_SEG live, BP = the bytes that arrived,
;      DI = the module id mod_need was called with - which is checked against
;      the header's own MOD_H_ID rather than derived back out of the row
;      pointer, because mod_need already has it
; out: mod_fp armed and on below; malformed - nothing armed - to .freefail
; clobbers: AX, CX, DX, SI, DI, ES, flags
;
; ld_check_hdr's tests on a module, plus the one a package and a driver do not
; need: **the build stamp**. A module carries no dispatcher, because the
; kernel knows its entry offsets - it built them - and that is cheaper than
; `call bp / retf` and also the hazard, since a module file from another
; build would be far-called at offsets that have since moved. buildnum.py's
; number changes on every commit (SPEC.md 14.2), so agreement here is by
; construction and disagreement is refused rather than executed.
; -----------------------------------------------------------------------------
                                ; ES = the image ALREADY: the read went to
                                ; ES:0 and dskw_read_x keeps ES, as does the
                                ; ROM door's COPY (boot/osrom.asm) - so the
                                ; `mov es, [bx+MODR_SEG]` that reloaded it is
                                ; gone (kernel size pass 17, -3)
    mov ax, di                  ; the id mod_need was CALLED with - nine
%ifdef OS88_SHORTCUTS
    cmp al, MOD_SETS            ; ...whose image is CTRL.DRV's, cut short
    jne .id
    mov al, MOD_CTRL
.id:
%endif
    mov [mod_hdrt + MOD_H_ID], al ; bytes of arithmetic recovered a number the
%ifdef OS88_SHORTCUTS           ; caller was holding. If DI were ever
    push di                     ; clobbered on the way here the failure is a
%endif                          ; REFUSAL and not a wrong dispatch, which is
    mov si, mod_hdrt            ; the safe direction. The magic, the version,
    xor di, di                  ; the id, the BUILD and the layout STAMP
    mov cx, 4                   ; (SPEC.md 2.8.2) in one compare against
    cld                         ; mod_hdrt; SI and CX are this routine's to
    repe cmpsw                  ; spend (the arming below reloads both).
%ifdef OS88_SHORTCUTS           ; The id in DI is read again only by the
    pop di                      ; settings core's two tests below, so a
%endif                          ; kernel with no shortcuts banks nothing
    jne .freefail               ; (kernel size pass 17: kern_small -2)
    mov ax, [es:MOD_H_IMG]
    cmp ax, bp                  ; the image IS the file
%ifdef OS88_SHORTCUTS
    je .img
    jb .freefail                     ; ...or, for the settings core alone, the
    cmp di, MOD_SETS            ; file is LONGER than what was read
    jne .freefail
.img:
%else
    jne .freefail
%endif

    ; THE `.cold` WORDS, BEFORE THE ARM AND NOT AFTER IT (SPEC.md 2.8.3): a
    ; KEPT image re-adopted by .held joins at .arming below, past this, so
    ; neither fix runs twice on one image - mod_cfix's single pass is what
    ; tools/os88rom.py proves, and the ROM's refuses a word that no longer
    ; names COLD_RAM. Neither reads a slot, so the order is free
%ifdef COLD_SLIDE
    ; A MODULE WHOSE `.cold` HAS MOVED (MOVABLE-COLD-PLAN 4) - into a ROM, the
    ; HMA, or down over a dropped VGA rung - far-calls `.cold` at COLD_SEG,
    ; the segment it was assembled against. mod_cfix re-points those words
    ; itself, from the bytes alone: the ROM and the HMA image used to carry
    ; every module's list and a door to apply it (ROMF_FIX), and a kernel
    ; that moved `.cold` by itself would have needed both resident. BX is the
    ; ROW, BP the bytes the claim holds (the settings core reads a PART, and
    ; a word past BP is not in this claim). AX, CX, DX, DI and ES are dead
    ; here (mod_check clobbers them, kentc_bp/kretc_es banked and restore
    ; them; ES comes back the image's, the scan's own). kern_small keeps the
    ; ROM's door below instead: it has no slide, and SPEC.md 39.27.4 spends
    ; nothing there
    call mod_cfix               ; (it answers in no flag: the arm's `add`
                                ; leaves CF = 0 for the success path)
%elifdef ROM_ADOPT
    ; A MODULE ON A ROM MACHINE (docs/plans/ROM-PLAN.md 3.4.4) far-calls
    ; `.cold` at COLD_RAM, the segment it was assembled against, and `.cold`
    ; runs at the ROM's. The ROM carries each module's list of those words and
    ; re-points them; it checks every one names COLD_RAM first, so a module
    ; it was not cut against is refused here rather than run. BX is the ROW,
    ; BP the bytes the read brought in (the settings core reads a PART). On a
    ; kernel whose `.cold` is in RAM rom_none answers FIX with CF = 0 and
    ; nothing touched. ES and AX are dead here
    mov al, ROMF_FIX
    call far [rom_mfp]
    jc .freefail                ; nothing armed yet: the claim goes back
%endif

    ; --- arm the slots. Every entry offset is checked against the image the
    ; --- way ld_check_hdr checks a package's: inside the bytes that arrived,
    ; --- and past the header, so a malformed table cannot aim a far call at
    ; --- the header's own bytes or off the end of the claim.
.arming:                        ; (.held's way in: BX = the row, ES = the
                                ; image, BP = its bytes)
    call mod_fpr                ; DI = the block, CX = the KERNEL's count
    cmp cx, [es:MOD_H_NENT]     ; ...which the header must equal exactly
%ifdef OS88_SHORTCUTS
    je .nent
    ja .freefail                     ; ...or, for the settings core alone, be its
    cmp di, SETFP               ; FIRST PART: CPE_SC is entry 0, so the one
    jne .freefail                    ; slot arms from the header's first word
.nent:
%else
    jne .freefail
%endif
    mov si, MOD_H_ENT           ; ...and CX is the count both sides agreed
    mov dx, cx
    shl dx, 1
    add dx, si                  ; DX = the header's own end (SI, a byte under
                                ; the immediate - kernel size pass 19)
                                ; DI = &mod_fp[id], and BX is untouched - so
                                ; the `pop bx / push bx` that used to put the
                                ; row back goes with the id derivation
.arm:
    es lodsw                    ; the entry table, one word a pass
    cmp ax, dx
    jb .badarm
    cmp ax, bp
    jae .badarm
    mov [di], ax                ; offset...
%ifdef KERN_MEGA
    push ax                     ; kern_mega: the slot is a FAR CALL's, so it
    mov ax, es                  ; holds the image's CODE half - its pair's
    cmp byte [ss:mega_pm], 0    ; data selector less 8 (KERN-MEGA-MEMORY-PLAN
    je .rmseg                   ; MC7.4) - safe to store rather than derive at
    sub ax, 8                   ; every call, because a module image never
.rmseg:                         ; moves (memory.inc pins MEM_K_MOD). In REAL
                                ; MODE the segment, and mega_enter converts
                                ; what the boot armed (mega_mods_fix)
    mov [di+2], ax
    pop ax
%else
    mov [di+2], es              ; ...and segment, which ES IS: it was loaded
%endif
                                ; from [bx+MODR_SEG] at the top of this
                                ; routine and no segment register is written
                                ; in between
    add di, 4
    loop .arm                   ; ...and the success path goes on below
%ifdef KERN_MEGA
    call mod_hi                 ; ...and above 1MB, if it may go (13.5)
    clc                         ; (mod_hi answers in no flag)
%endif                          ; elsewhere CF = 0 is the `add di, 4`'s
    ; NO ZEROING HERE: a module's bss is zeros in its own file (kernel.asm's
    ; MODB_TAIL), so the read above - or the ROM's copy, the same bytes -
    ; delivered them on this load. It was 22 bytes of kern_small's `.cold`
    ; that zeroed from the image's end to the claim's KB-rounded one
    ; (docs/plans/MODULE-SELFCONTAIN-PLAN.md 3.5). A KEPT image re-adopted
    ; through .arming is not a read, and its scratch tail (MOD_H_SCR) was
    ; zeroed by the drop that kept it (SPEC.md 2.8.3)

    jmp .back                   ; CF = 0: the volume back, and the success
                                ; answer (short on every kernel but kern_mega,
                                ; whose check carries the selector arm and
                                ; mod_hi)

;
%ifdef COLD_SLIDE
; -----------------------------------------------------------------------------
; mod_cfix - re-point a just-loaded module's far calls into `.cold`
; (MOVABLE-COLD-PLAN 4; module internal)
; in:  BX = the ROW ([bx+MODR_SEG] is the claim), BP = the bytes it holds
; out: nothing. clobbers: AX, CX, DX, DI, ES and the flags
;
; NO TABLE. A module names `.cold` only as the SEGMENT HALF OF A FAR CALL OR
; JMP IMMEDIATE (9A/EA ofs seg) - tools/os88rom.py proves that for every image
; on every build, running this very scan against the two-assembly diff for
; each segment `.cold` can be moved to and failing on any word it would miss
; or invent. So the low byte of COLD_HOME is searched for, the high byte and
; the opcode three bytes back confirm it, and the word gets [api_coldseg].
; A hit's high byte is stepped over, so a segment just written can never be
; read back as the start of another. Home, it costs a compare.
; COLD_HOME, never COLD_SEG: this is `.cold`, which names no segment of its
; own (SPEC.md 2.10.2), and COLD_HOME is the built value under the name
; tools/os88romfix.py never moves.
; -----------------------------------------------------------------------------
mod_cfix:
    mov dx, [api_coldseg]
    cmp dx, COLD_HOME
    je .ret                     ; `.cold` is where the module was built for
    mov es, [bx+MODR_SEG]
    mov di, 3                   ; a site's opcode is 3 bytes before it...
    mov cx, bp
    sub cx, 4                   ; ...and its word ends inside the claim: the
    jbe .ret                    ; positions 3..BP-2
    cld
.scan:
    mov al, COLD_HOME & 0xFF
    repne scasb
    jne .ret                    ; (ZF from the last compare: no more)
    cmp byte [es:di], COLD_HOME >> 8
    jne .next
    mov al, [es:di-4]           ; the opcode: 9A (call far) or EA (jmp far),
    xor al, 0x9A                ; EXACTLY those two - 9A ^ EA is 70h. It was
    je .hit                     ; `or al, 70h / cmp al, FAh`, which reads EIGHT
    cmp al, 0x9A ^ 0xEA         ; opcodes as a far one, and two of the other
    jne .next                   ; six are 8A (mov r8, r/m8) and BA (mov dx,
.hit:                           ; imm16): every module is full of both, so a
                                ; `.cold` home whose two bytes followed one
                                ; was re-pointed - the build's proof (os88rom
                                ; cfix, which models this predicate byte for
                                ; byte) refused CTRL.DRV's `8A 0E ..` the day
                                ; a +69 `.vga` moved COLD_HOME to 08B3h
    mov [es:di-1], dx
    jcxz .ret
    inc di                      ; ...past the high byte just written
    dec cx
.next:
    inc cx                      ; LOOP's test is CX != 0 AFTER its decrement,
    loop .scan                  ; so this is "CX was not 0" in three bytes
.ret:
    ret
%endif


%ifdef KERN_MEGA
; -----------------------------------------------------------------------------
; mod_hi - kern_mega in protected mode: a module the census CLEARED goes above
; 1MB the moment it is loaded and checked (KERN-MEGA-PLAN 13.5). BX = its row,
; BP = the bytes read; the image is armed with its conventional alias. The
; whole claim - image and the bss past it - is copied into a code/data pair
; (mega_modhif), every slot naming the old alias is re-armed with the pair's
; code selector, the conventional claim is freed and MODR_SEG holds the code
; selector, whose TI bit says "above 1MB" to mod_free_row. Any refusal leaves
; it where it is, loaded and working. Clobbers AX, CX, DX, DI
;
; MOD_HIOK is the census, and since KERN-MEGA-PLAN 14.47 it is EVERY module.
; What kept four of them low was a buffer of the image's own handed to the
; file layer, which normalised ES and converted it with mega_s2segc - FFFF
; above 1MB. Item 77 taught the file layer exactly that case (dskw_hiseg: the
; selector is kept and the OFFSET moves, a sector at a time through the
; scratch), and the disk layer bounces any LDT selector through dsk_secbuf
; (dsk_runcap), so a module's buffer above 1MB reaches the drive the way a
; package's does. The rest of the census holds: no module hands a reflected
; BIOS call a buffer of its own (FORMAT.DRV's address fields and every raw
; sector DISKOPS.DRV moves are LOW_SEG's dsk_secbuf, the cloner's windows
; conventional claims), HIBER.DRV's real-mode code is COPIED out of the image
; to the text page before it runs (kds_stub, the park stub via mega_dstubup),
; and every place a module stores its own CS it does so after this move -
; the wake address (mega_hbwk, KDL_WAKE, HBP_WAKE), DOCK.DRV's dkv and
; WIN95.DRV's thv. DESK.DRV's one segment sum is its LIB verb, which asks
; mega_dklibf above 1MB. A module loaded in REAL mode, at the boot, is moved
; at the switch by mod_hiboot below.
%ifdef OS88_SHORTCUTS
MOD_HIOK_SETS equ 1 << MOD_SETS ; CTRL.DRV's settings core, the same file
%else
MOD_HIOK_SETS equ 0
%endif
%ifdef KERN_BIG
MOD_HIOK equ (1 << MOD_CTRL) | (1 << MOD_FMT) | (1 << MOD_DISKOPS) | (1 << MOD_HIBER) | (1 << MOD_DOCK) | (1 << MOD_EXT) | (1 << MOD_WIN) | (1 << MOD_DESK) | (1 << MOD_STIKM) | MOD_HIOK_SETS | (1 << MOD_KBM)
; (MOD_KBM, the keyboard mouse's adopted image, SPEC.md 2.8.8: armed at the
; boot in real mode like any boot-loaded module, so mod_hiboot moves it; its
; code half is readable, access byte 9Ah, so its table reads through CS hold)
%else
MOD_HIOK equ 0
%endif
%if MOD_MAX_KB > 63
  %error "mod_hi copies a whole claim through one 64KB descriptor"
%endif
mod_hi:
%ifdef MOD_CACHE
    mov ax, [bx+MODR_SEG]       ; ALREADY A POOL PAIR: a kept image .held
    call mem_ishiax             ; re-adopted (SPEC.md 2.8.3.3) - armed with
    jnc .out                    ; the pair's code half, nothing to copy. (CF =
%endif                          ; 1 in real mode, so the test below still asks)
    cmp byte [ss:mega_pm], 0
    je .out
    mov cx, bx
    sub cx, mod_tab
    shr cx, 1
    shr cx, 1                   ; CL = the id
    mov ax, 1
    shl ax, cl
    test ax, MOD_HIOK
    jz .out
    mov ax, bp
%ifdef OS88_SHORTCUTS
    cmp bx, mod_tab + MOD_SETS * MODR_SIZE
    jne .whole                  ; the settings core READ its first MODS_SIZE
    mov ax, MODC_SIZE           ; bytes into a claim of the WHOLE image's
.whole:                         ; (mod_need's .whole): its bss tail, cpc_buf,
%endif                          ; is past what was read and inside the pair
    inc ax                      ; the IMAGE, its bss's zeros included
    and al, 0FEh                ; (kernel.asm's MODB_TAIL), to the even count
    xchg ax, cx                 ; mega_modhif copies: CX = its bytes. Not the
                                ; claim's granule: past the file is nothing
                                ; the module owns
    mov ax, [bx+MODR_SEG]
    push ax
    call MEGA_SEL_CODE:mega_modhif ; AX = the pair's code selector
    pop dx                      ; DX = the conventional pair's data half
    jc .out
    push dx
    push ax
    sub dx, 8                   ; DX = the code half mod_check armed with
    call mod_fpr                ; DI = the slot block, CX = its count
    pop ax
    jcxz .free
.slot:
    cmp [di+2], dx
    jne .next
    mov [di+2], ax              ; ...re-armed with the pair's code selector
.next:
    add di, 4
    loop .slot
.free:
    pop dx                      ; the conventional claim's selector
    mov [bx+MODR_SEG], ax       ; MODR_SEG = the selector
%ifdef MEGA_E9
    push si
    mov si, [bx+MODR_FILE]      ; ...and where it went, to the debug port
    call MEGA_SEL_CODE:mega_modsayf
    pop si
%endif
    push bx
    mov bx, MEM_K_MOD
    call mem_free_x
    pop bx
.out:
    ret

; -----------------------------------------------------------------------------
; mod_hiboot - once, right after mega_enter, from the foot of drv_hiboot_f:
; every module the BOOT loaded in real mode goes above 1MB as mod_need would
; have sent it there in protected mode (KERN-MEGA-PLAN 14.47).
; mega_mods_fix has armed its slots with its conventional CODE ALIAS, which is
; exactly what mod_hi re-arms; what mod_hi cannot know is the far pointers a
; module stored of ITSELF while it was hooked in real mode, and those are the
; two tables mega_mods_fix already converts (dkv, thv) - so the alias they
; hold now becomes the pair (mega_modvfixf). Nothing else can hold the alias:
; nothing below ui_task has run a task, so no module frame is on any stack.
; BP is the image's own size off its header, which is what mod_need's read
; would have been (the settings core's MODS_SIZE is mod_hi's own case). A
; module mod_hi refuses stays where it is, as it always did. Clobbers nothing
; -----------------------------------------------------------------------------
mod_hiboot:
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    push es
    mov bx, mod_tab
.l:
    mov ax, [bx+MODR_SEG]
    or ax, ax
    jz .n
    push ax
    call mem_ishiax
    pop ax
    jnc .n                      ; already above 1MB
    mov es, ax                  ; (its conventional pair's data half)
    mov bp, [es:MOD_H_IMG]      ; ...the image's bytes, as the read had them
    push ax
    call mod_hi
    pop ax
    mov dx, [bx+MODR_SEG]
    cmp dx, ax
    je .n                       ; refused: it stays low, hooks and all
    sub ax, 8                   ; AX = the code half the tables hold
    call MEGA_SEL_CODE:mega_modvfixf
.n:
    add bx, MODR_SIZE
    cmp bx, mod_tab_end
    jb .l
    pop es
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; mod_disarm - point one module's slots back at mod_gone (module internal)
; in:  BX = the row
; clobbers: CX, DI, flags       <- NOT AX any more: mod_fpr answers from the
;           row, so the id is never formed. mod_init_x's loop counter depends
;           on that (see there).
; -----------------------------------------------------------------------------
mod_disarm:
    call mod_fpr                ; DI = the block, CX = its count
    pushf                       ; NO TORN SLOT: a drop ran on a WORKER
    cli                         ; (inst_task_die -> desk_drop_x, until BR-4
                                ; posted that teardown to ui_task) while the UI
    call .slot                  ; task far-calls a slot with no lock held
    popf                        ; (cur_shape_pass's DKD_HOVER), and mod_gone's
    ret                         ; offset in the IMAGE's segment is garbage
                                ; (its parity pad cancelled memory.inc's at
                                ; mem_bytes_kb_x, kernel size pass 14)
.slot:                          ; a CALLED loop, not a fall-through:
    mov word [di], mod_gone     ; (kmain_o's `.boot3` loop is this, written out)
    COLDSEG_TO word [di+2]      ; mod_gone is .cold's own (ROM-PLAN 3.2)
    add di, 4
    loop .slot
    ret

; -----------------------------------------------------------------------------
; mod_free_row - give a module's claim back (module internal)
; in:  BX = the row
; clobbers: AX, BX, CX, DX, DI, flags   <- BX now: it becomes the MEM_K_MOD
;           owner word for mem_free and is not put back. Both callers are in
;           this file, both named below, and neither reads it after the call.
;           A THIRD CALLER WILL NOT BE TOLD - the same hazard clo_drop
;           accepted, and the same mitigation: it is written here.
;
; DISARM FIRST. The claim is freed here, and a slot still naming it is a far
; call into whatever claims that memory next - which is the failure this whole
; mechanism has to be unable to produce, because it would not fault.
; -----------------------------------------------------------------------------
mod_free_row:
    cmp word [bx+MODR_SEG], 0   ; ...and no outer bank: nothing between here
    je .out                     ; and `.out` writes BX except the inner
    call mod_disarm             ; mod_disarm preserves BX since it stopped
    xor dx, dx                  ; forming the id, and READ-AND-CLEAR is one
    xchg dx, [bx+MODR_SEG]      ; instruction on the 8086 - DX comes out the
%ifdef KERN_MEGA
    call mem_ishidx             ; ABOVE 1MB (mod_hi): a selector, whose
    jc .conv                    ; block and pair go back to the pool - and
    xchg ax, dx                 ; DESK.DRV's library selector with them
    call MEGA_SEL_CODE:mega_hifreef
%ifdef KERN_BIG
    cmp bx, mod_tab + MOD_DESK * MODR_SIZE
    jne .hiok
    call KERNEL_SEG:mega_dklibfree
.hiok:
%endif
    clc                         ; CF = 0, mem_free_x's answer on the other
    ret                         ; path: mod_drop's flags ARE its callers'
                                ; (fm_cldrop's success CF, which Disk >
                                ; Unmount Image reads) - and this path left
                                ; the `cmp` above in them, CF = 1 for every
                                ; module whose row is under DESK.DRV's
                                ; (KERN-MEGA-PLAN 14.82)
.conv:
%ifdef KERN_BIG
    cmp bx, mod_tab + MOD_DESK * MODR_SIZE
    jne .cvl                    ; ...and below 1MB too, where the library is a
    call KERNEL_SEG:mega_dklibfree ; descriptor of its own as well (MC7.4)
.cvl:
%endif
%endif
    mov bx, MEM_K_MOD           ; old segment and the word is left 0. NO bank
    call mem_free_x             ; round BX either: neither caller reads it
.out:                           ; after the call - mod_need's .freefail falls
    ret                         ; into .fail and out through kretc_es, and
                                ; mod_drop goes straight to its own pop run

; -----------------------------------------------------------------------------
; mod_drop - the feature is finished with its module (SPEC.md 2.8.3)
; in:  AL = MOD_*
; clobbers: FLAGS ONLY - every register preserved, for mod_need's reason
;
; **The caller decides when, and there is exactly one right moment per
; feature**, because nothing here can tell whether the module is on the stack:
; the Control Panel's is `cp_flush_close` returning (the panel's own teardown,
; SPEC.md 31.8), and the formatter's is the confirmation ending, either way.
; A drop is the END OF THE PIN, and the need/drop bracket is the whole of it
; (ONDEMAND-PLAN 7.1's pin, which the bracket t_moddrop gates already is):
; from mod_need to mod_drop the claim is MEM_K_MOD, which nothing sheds and
; nothing moves, so the heap can never take an image a frame stands in.
;
; KEPT, NOT FREED, under MOD_CACHE (kern_big, kern_emu, kern_mega; SPEC.md 2.8.3): an
; image whose header opts in (MOD_H_SCR, its scratch tail) has the tail
; zeroed, every slot disarmed, and the claim re-tagged MEM_P_MOD + 2 x its id
; - a purgeable cache at MEM_PG_LOW that the next mod_need re-adopts with no
; disk read, and that a claim needing the room sheds (mem_pg_own zeroes
; MODR_SEG; the slots already read mod_gone). The drop still ENDS the
; feature's hold - the heap may take the bytes from here on, which is the
; thing a drop is for. EVERY image with a file opts in now (SPEC.md 2.8.3's
; table: each one's tail is zeros in its file, and what it writes outside it
; is written before it is read); KBMOUSE alone, with no file to come back
; from, carries 0 and is freed. mod_live tells a kept row from a live one by
; its slot, so the panel's kept row is not mistaken for an open panel.
; ON kern_mega IN PROTECTED MODE (SPEC.md 2.8.3.3) RAM IS RAM: the image is
; kept wherever it is. Above 1MB it is the POOL PAIR mod_hi put it in, so the
; pool block, not a mem_tab record, is marked purgeable (mega_modkeepf),
; mx_alloc's shed takes it through mem_pg_forget's .mod arm (the word zeroed,
; the pair and DESK.DRV's library freed), and every pool move keeps its
; selectors. Below 1MB (a pool that refused the copy) it is THIS kernel's
; arm exactly: a mem_tab claim re-tagged and declared MOVABLE - its pair is
; the record's own (mega_mpnewf), the compactor rebases both halves with the
; block (mega_mpmovef) and tells nobody, and mem_shed_one frees both
; (mega_mpfreef). In REAL MODE (no switch) the image is freed, as it was.
; -----------------------------------------------------------------------------
mod_drop:
%ifdef MOD_CACHE
    call kentc_bp               ; KENT: push ax..bp
    push es                     ; ...and ES, mod_need's own ladder
    call mod_row
    mov dx, [bx+MODR_SEG]
    or dx, dx
    jz .free                    ; nothing held: mod_free_row's own no-op
%ifdef KERN_MEGA
    cmp byte [ss:mega_pm], 0    ; kern_mega in REAL MODE frees (SPEC.md
    je .free                    ; 2.8.3.3): it would be kept across the switch
%endif
    mov es, dx                  ; (kern_mega: a pool pair's CODE half,
    mov cx, [es:MOD_H_SCR]      ; readable, or a low record's DATA half)
    jcxz .free                  ; an image that does not opt in: freed
    mov di, [es:MOD_H_IMG]
    sub di, cx                  ; ES:DI = its scratch tail, CX bytes
%ifdef KERN_MEGA
    call mem_ishidx             ; CF = 1 below 1MB, CARRIED to the tag below
    pushf
    jc .wr
    add dx, 8                   ; ...a pool pair's stored through its DATA
    mov es, dx                  ; half, which mega_modkeepf takes as well as
.wr:                            ; the code one (the pair shares one base)
%endif
    xor al, al
    cld
    rep stosb                   ; zeroed NOW, so a re-adopted image is the
                                ; file's own (the zeros its read delivers)
    call mod_disarm             ; every slot to mod_gone BEFORE the tag says
                                ; the heap may take the block
    mov ax, bx
    shr ax, 1                   ; AX = its kept tag, MEM_P_MOD + 2 x the id
    add ax, MEM_P_MOD - (MOD_TAB_OFF >> 1) ; (.held's sum)
%ifdef KERN_MEGA
    popf                        ; mem_ishidx's answer: CF = 0 above 1MB
    jnc .pool
                                ; BELOW 1MB IN PROTECTED MODE the arm is the
                                ; one every kernel takes: the record's pair
                                ; moves with it (mega_mpmovef rebases both
                                ; halves) and no holder is told, so no
                                ; mem_rr_tab row is owed there - the
                                ; declaration is what lets the compactor move
                                ; it rather than dissolve it
%endif
    mov bx, dx                  ; mem_find_own's own-segment arm: the
    call mem_find_own           ; record AT that base, whoever owns it - a
                                ; held MODR_SEG always names one
%ifdef OS88_COMPACT
    mov word [ss:si+MC_RLOC], mem_region_reloc ; MOVABLE (SPEC.md
                                ; 2.8.3, 66.2): a KEPT image is a disarmed CS
                                ; that writes nothing outside its zeroed
                                ; scratch, and the ONE word naming it is its
                                ; MODR_SEG - a mem_rr_tab row, so the kernel's
                                ; half of every move re-points it and the proc
                                ; is the driver image's (driver.inc). PINNED,
                                ; it was a BARRIER the compactor DISSOLVES for
                                ; any claim that outranks LOW (mem_cp_walk's
                                ; .pinned) - so the next press read the whole
                                ; file again although nothing had needed its
                                ; bytes (the Task Manager report, tests/
                                ; modcache.py). STORED FIRST, while the tag
                                ; still pins it, so no task the drop is not
                                ; on can see a purgeable record without it
%endif                          ; (kern_small: nothing moves, so the KEPT
                                ; image is purgeable only - SPEC.md 2.8.3.4)
    mov [ss:si+MC_OWN], ax      ; KEPT: CF = 0, mem_find_own's (a second
                                ; drop re-writes the same tag)
    jmp short .out
%ifdef KERN_MEGA
.pool:
    push bx                     ; THE POOL BLOCK IS MARKED A PURGEABLE KERNEL
    xchg ax, bx                 ; CACHE (MX_FPURGE, its tag in MX_AUX): the
    xchg ax, dx                 ; next mx_alloc that needs the room sheds it
    call MEGA_SEL_CODE:mega_modkeepf ; through mem_pg_forget's .mod arm. Not
    pop bx                      ; MOVABLE to declare: every pool block moves
    jnc .out                    ; with its selectors (mx_compactf). CF = 1:
%endif                          ; no block found - freed, never left held
.free:
    call mod_free_row           ; (mod_free_row clobbers AX, BX, CX, DX, DI)
.out:
    jmp kretc_es
%else
    call kentc_di               ; KENT: push ax..di



                                ; SI is pushed so the run is a SUFFIX of the
                                ; shared ladder's es/bp/di/si/dx/cx/bx/ax -
    call mod_row                ; which mod_drop already preserved in fact
    call mod_free_row           ; (mod_free_row clobbers AX, BX, CX, DX, DI),
    jmp kretc_di                ; so no contract weakens. .cold's copy of the
                                ; ladder, and mod_drop is .cold
%endif

; -----------------------------------------------------------------------------
; mod_init - point every slot at mod_gone before anything can call one
; clobbers: CX, DI, flags
;
; WHY IT EXISTS - AND NOT FOR THE REASON THIS COMMENT USED TO GIVE. It said
; ".bss arrives as whatever was in memory - `-f bin` zeroes nothing". That is
; false here and clip.inc's declaration block says so from the other end:
; `.bss` is `nobits vfollows=.text` and the next section's file position
; (`.ovl`'s at FAT_SEG; `.cold`'s under NO_ROM_COLD) is padded past it, so NASM
; emits at least KBSS_SIZE zero bytes and the load lands them (kz_all under
; KZIP, stage 2's read on NOKZIP). `.bss` IS zero at boot, which is precisely why clip.inc
; needs no init entry point.
;
; What this routine is for is the OTHER thing: a slot's resting value is not
; zero. Every mod_fp slot must read mod_gone before the first thunk runs, or a
; thunk that reaches its far call ahead of its own mod_need jumps to segment
; 0. mod_need cannot be trusted to be the only door, because a thunk with the
; test written the wrong way round still assembles.
; -----------------------------------------------------------------------------
; (mod_init_x is gone: kmain_o writes its loop out on every kernel - kernel
; size pass 13 on kern_small, pass 14 on kern_big - in the blob then, in
; `.boot3` since BOOT3-PLAN wave 4)

section .cold                   ; the far entry, for kmain

section .bss

; **A far pointer per entry, and that is the whole dispatch mechanism.**
; A thunk does `call far [mod_fp + K]` with K an assembly-time constant, which
; takes no register, needs no scratch word two callers could race for, and is
; re-entrant across modules by construction. It is drv_fptr/drv_fseg's idiom
; (SPEC.md 51.2.1) with a slot per entry rather than a slot per class - and
; the reason it is not one shared `mod_target` word set by mod_need is that a
; word set here and consumed one instruction later is a race with anything
; that can run in between, which on a pre-emptive kernel is everything.
section .text
