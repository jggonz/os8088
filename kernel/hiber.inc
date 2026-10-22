; =============================================================================
; os8088 - hiber.inc
;
; Hibernate the machine to a file on the hard disk, and resume it at the next
; boot (SPEC.md 87). Prefix `hb_` for the resident half, `hbf_` for its .cold
; thunks, `hbk_` for the far entries the module reaches cold code through,
; `hbm_` for the on-demand module and `hbs_` for the resume stub.
;
; WHAT IS RESIDENT is the least that has to be: the System menu's three
; strings live in menu.inc beside their siblings; here are the window
; template and title, the state block the image carries, the .text thunks a
; kernel window's callbacks need (a W_SEG-0 window is dispatched NEAR in
; KERNEL_SEG), and in .cold the boot probe, the greying predicate, the far
; entries, and the thunks that load the module before calling into it -
; cpf_cp_paint's shape (SPEC.md 2.8).
;
; EVERYTHING ELSE IS HIBER.DRV, MOD_HIBER: the window, the write, the
; boot-time question, the extent walk, the stub
; that puts memory back, and the routine that wakes the restored kernel. It
; is .cold code with the address changed (SPEC.md 2.8): CS = its own claim,
; DS = KERNEL_SEG, every kernel address the one this assembly gave it, and
; its own strings read through CS (SPEC.md 2.8.6) - nothing can draw them
; while the image is not loaded, because every path in is a thunk below that
; runs mod_need first.
;
; kern_small has none of it (SPEC.md 62.9.15's reason: the 128KB machine is
; not the one with a hard disk to hibernate to), and ships a one-entry stub
; module so that os88mod.py cuts the same four files out of every kernel.
; =============================================================================

HB_M_ASK    equ 0               ; the window offers Hibernate... / Cancel
HB_M_BUSY   equ 1               ; the image is being written (or read)
HB_M_RESUME equ 2               ; the boot-time question: Resume / Discard
HB_M_GONE   equ 3               ; the window closed: the thunk drops the image

; --- the module's entry slots (SPEC.md 2.8.1) - its ABI with the thunks -----
HBE_PAINT   equ 0
HBE_ONCLICK equ 1
HBE_ONUP    equ 2
HBE_ONKEY   equ 3
HBE_KINIT   equ 4
HBE_PERFORM equ 5
HBE_OPEN    equ 6
HB_NENT     equ 7
; --- the two verbs OSAPI_DRV_SUSPEND adds to hbm_perform (SPEC.md 51.11) ----
; The slot's AL is 0 resume / 1 suspend / 2 handoff. drv_suspend_x adds
; HB_P_RESUME to the first two so the module's perform entry tells them from
; the UI_RBQ_* it already dispatches, and the module gains no eighth entry.
; The handoff never enters the module at all.
HB_P_RESUME  equ UI_RBQ_DOSRUN + 1
HB_P_SUSPEND equ UI_RBQ_DOSRUN + 2

; --- API cells the module calls (SPEC.md 20.3); the kernel has no copy of
; --- apps/os88api.inc, so the four it needs are named here ------------------
HB_API_FILL   equ apic_gfx_fill        ; AX=x1, BX=y1, CX=x2, DX=y2 in [gfx_color]
HB_API_FONTX  equ apic_font_str        ; CX=x, DX=y, the string at the CALLER's DS:SI
HB_API_COLOR  equ apic_osapi_set_color        ; AL -> [gfx_color]
HB_API_XCAPS  equ apic_xm_caps        ; out BL = free block-table entries (SPEC.md 41)

; --- the pointer file, HIBERNAT.PTR (SPEC.md 87.3) ---------------------------
HBP_MAGIC   equ 0               ; 'HIB1'
HBP_BUILD   equ 4               ; dw BUILD_NUM
HBP_STAMP   equ 6               ; dw MOD_STAMP
HBP_MEMTOP  equ 8               ; dw [mem_top]
HBP_VID     equ 10              ; db [vid_kind]
HBP_WAKE    equ 12              ; dd hbm_wake, off:seg - and the 12 bytes in
HBP_DOS     equ 16              ; db 1 = THIS WAS NOT THE USER LEAVING THE
                                ; MACHINE (docs/plans/KERN-DOS-PLAN.md 8): the
                                ; image was written for a DOS handoff, so the
                                ; boot resumes it rather than asking. It is
                                ; BEHIND the fixed head on purpose - hbm_ask
                                ; compares the first twelve bytes and this is
                                ; not one of them, because it describes the
                                ; OCCASION and not the machine
HBP_PAD     equ 17              ; db 0
%ifndef KERN_MEGA
HBP_SIZE    equ 18              ; front of it are hbm_ptrfix, one description
%else
; kern_mega's pointer is TEN BYTES LONGER, which is also what keeps the two
; kernels apart: each reads exactly HBP_SIZE bytes and refuses any other size,
; and both have one BUILD_NUM at a commit (KERN-MEGA-DOS-PLAN 8.2 item 4)
HBP_XFLG    equ 18              ; db 1 = written in PROTECTED MODE: the image
                                ; is whole 32KB chunks and an EXTENDED SECTION
                                ; follows it (hbm_xwrite)
HBP_XSUM    equ 20              ; dd that section's Fletcher sums, s1 then s2
HBP_XLEN    equ 24              ; dd ...and its bytes on the disk
HBP_SIZE    equ 28
HBX_CHUNK   equ 32768           ; the bounce and the file's chunk: a multiple
HBX_KB      equ HBX_CHUNK / 1024    ; of every cluster (<= 32KB, mount rule 8)
HBX_HMAX    equ (512 - 12) / 8  ; the runs the section's head can list: 12
                                ; bytes, then 8 a run, in ONE sector, the
                                ; runs' data starting at 512 (hbm_xwrite) -
                                ; 62, where the pool's table holds 96 blocks
%if 12 + HBX_HMAX * 8 > 512
  %error "hbm_xwrite: the section's head and its run list are one sector"
%endif
%endif

%include "hbstage.inc"        ; SPEC.md 87.5's staging map, shared as SOURCE
                              ; with kerndos/kdresume.inc (SPEC.md 96.49)
%ifdef DOSR_MARK
  %include "hbmark.inc"       ; ...and the trace, which hbm_wake stamps 500
%endif                        ; lines before hbstub.inc is included
; HBM_HMACHK - a kernel whose `.cold` can be ADOPTED into the HMA (COLDHMA.SYS,
; docs/plans/HMA-PLAN.md), so the DOS handoff sums what runs up there for the
; live route (SPEC.md 96.49.9). kern_big alone: kern_small's `.cold` has no
; HMA image and kern_mega's HMA is its own
%ifdef KERN_BIG
  %ifdef ROM_ADOPT
    %define HBM_HMACHK 1
  %endif
%endif

; --- the DOS handoff's own fields (SPEC.md 96.40.1) -------------------------
; They sit ABOVE HS_SLAVE and BELOW HS_IVT, which the DOS stub never uses: it
; reads to KD_SEG, which is above the IVT and the BDA both, so it has no
; late-copy problem to solve and 1,024 bytes of the staging area are free. The
; launch block takes 512 of them.
KDS_PLEN    equ 0x0430          ; dd the part's PACKED bytes
KDS_ULEN    equ 0x0434          ; dd ...and what they expand to
KDS_TSEG    equ 0x0438          ; dw where the packed stream is read
; --- AND THE CORE PART'S, because the stub expands TWICE (SPEC.md 96.44.5.4)
; ONE extent list covers both: os88pkg.py lays parts out in table order and
; 512-aligned, so the core's bytes are immediately followed by kern_dos's and
; a single run reads the pair. KDS_CDELTA is how far into that run kern_dos's
; stream begins, in PARAGRAPHS.
KDS_CPLEN   equ 0x043A          ; dd the CORE part's packed bytes
KDS_CDELTA  equ 0x043E          ; dw paragraphs from the run's base to
                                ; kern_dos's own stream
KDS_CORG    equ 0x0440          ; dw where the core lands in KD_SEG
%ifdef KERN_MEGA
KDS_KSEG    equ 0x044A          ; dw kern_mega: the stub's own pick of
KDS_KORG    equ 0x044C          ; dw kern_dos's segment and origin, HMA or LOW
                                ; (96.40.9.2) - above HS_TICK0's dword
%endif
KDS_LB      equ 0x0500          ; db[512] the launch block, on HS_IVT's ground
KDS_TMPSEG  equ 0x1000          ; 64KB up: above kern_dos's image, which starts
                                ; at KD_SEG and is 46KB, so the stream and the
                                ; thing it expands to never meet and SPEC.md
                                ; 20.13.7's in-place rules need no thought

; --- kern_dos's own three numbers, MIRRORED (kerndos/kdlayout.inc) -----------
; tests/unit/t_mirror.py is what keeps them honest: a name defined in more than
; one place has to agree everywhere, and these three are the whole of what the
; kernel needs to know about a machine it is handing over to.
%ifndef KERN_MEGA
KD_SEG      equ 0x0060          ; linear 0x00600, above the IVT and the BDA
KD_ORG      equ 0               ; ...and the image at its offset 0
%else
KD_SEG      equ 0xFFFF          ; kern_mega's kern_dos is in the HMA (KD_HMA,
KD_ORG      equ 0x10            ; kerndos/kdlayout.inc): FFFF:0010, 100000h
%endif
KD_H_LBP    equ 4               ; dw: where the launch block goes
KD_H_LBSZ   equ 6               ; dw: ...and how many bytes of it there are
KDL_SIZE    equ 512             ; ...which is one sector
KDL_FLAGS   equ 6               ; dw in the block: KDLF_*
KDL_UNIT    equ 12              ; dw ...and the unit `int 19h` is to boot from,
                                ; which is also the kernel's to fill
KDL_MOUBASE equ 14              ; dw the serial mouse's UART base, 0 = none
KDL_NVOL    equ 28              ; db rows of volume table, 0 = leave the
                                ; built-in one alone (SPEC.md 96.46)
KDL_BOOTV   equ 29              ; db ...and which index the kernel booted from
KDL_HDSPT   equ 30              ; dw the fixed disk's sectors per track...
KDL_HDHDS   equ 32              ; dw ...and its heads, which the boot overlay
                                ; probes once and kern_dos cannot ask for
KDL_VTAB    equ 34              ; db kind, db unit, dd base - per volume
KDL_WAKE    equ 82              ; dd where a live resume re-enters the kernel
KDL_KSEG    equ 86              ; dw ...and the DS it does so with (96.49)
KDL_HBVOL   equ 88              ; db the volume HIBERNAT.IMG is on
KDL_CYLRUN  equ 89              ; db may a run cross a HEAD over there (96.44.14)
KDL_CLK     equ 90              ; db[10] the kernel's own clock block
KDL_TICK    equ 100             ; dd ...and the BIOS tick 0040:006C it was read
                                ; at, which is what lets the other host say how
                                ; long the DOS program ran (SPEC.md 96.49.5)
KDL_STAGE   equ 104             ; dw WHERE THE KERNEL WILL LOOK on the way back
                                ; - `hbm_stageseg`'s own answer, because the
                                ; other host asks the BDA's mode byte and that
                                ; byte is the DOS PROGRAM's (SPEC.md 96.49.6).
                                ; 0 = a kernel too old to say, and `kd_resume`
                                ; falls back to asking
KDL_HOFF    equ 488             ; dw the HMA span the live resume re-sums -
KDL_HWDS    equ 490             ; dw its words - and what this side read
KDL_HSUM    equ 492             ; dd (SPEC.md 96.49.9). In the kern_mega
                                ; tail's XMS bytes, which no kern_big build
                                ; writes. MIRRORED in kerndos/kdlaunch.inc
KDL_VROW    equ 6
KDL_VMAX    equ 8
KDL_MOULINE equ 27              ; db ...and its bit in the master 8259's mask.
                                ; The kernel's fields for SPEC.md 96.45's
                                ; reason: `mouse_init` settled the port and
                                ; the line at boot and nothing a package can
                                ; see records which one won
KDLF_HIBER  equ 4               ; ...bit 2, which is the ONE bit the kernel
                                ; sets rather than the box - whether there is a
                                ; fixed disk to come back to is hb_pick's
                                ; question (docs/plans/KERN-DOS-PLAN.md 8)
KDLF_NOMOUSE equ 8              ; ...and bit 3, which is the box's and is read
                                ; HERE: KDL_MOUBASE is a kernel field patched
                                ; into the staged block after the box has
                                ; gathered it, so a package cannot say "no
                                ; pointer" by writing a zero there (SPEC.md
                                ; 96.36.8)
%ifdef KERN_MEGA
KDLF_LOWDOS equ 0x20            ; ...and bit 5, the box's and read by THIS
                                ; module's stub: kern_dos is part 3, the LOW
                                ; image, expanded at 0060:0000 rather than in
                                ; the HMA (SPEC.md 96.40.9.2)
KDLF_SAVE   equ 0x40            ; ...and bit 6, the box's and read by
                                ; hbm_dosrun: HIBERNAT.IMG before the park
                                ; (SPEC.md 96.40.9.3). MIRRORED
KD_LSEG     equ 0x0060          ; ...where that image goes, with KD_ORG 0
KDL_O88CL   equ 508             ; dw DOS.O88's first cluster and...
KDL_O88V    equ 510             ; db ...its volume, for the HMA image to read
                                ; part 3 itself (SPEC.md 96.40.9.1): step 2
                                ; finds the file and is the one place both
                                ; are known. MIRRORED in kerndos/kdlaunch.inc
%endif
KDL_DPT     equ 16              ; db[11] in the block: the DISKETTE PARAMETER
KDL_DPT_LEN equ 11              ; TABLE, the one field the box cannot fill.
                                ; `hbm_dosrun` patches it into the STAGED copy
                                ; out of `dsk_dpt` - see kerndos/kdlaunch.inc
%if KDL_DPT_LEN != DPT_LEN
  %error "hiber: the launch block's DPT field is not a diskette parameter table"
%endif

; --- the record a package posts (SPEC.md 96.40) -----------------------------
; MIRRORED in apps/os88api.inc, and t_mirror is the guard.
KDH_MAGIC   equ 0               ; dd 'KDH1'
KDH_VOL     equ 4               ; db the volume DOS.O88 is on
KDH_PAD     equ 5               ; db 0
KDH_DIR     equ 6               ; dw its folder's first cluster, 0 = the root
KDH_NAME    equ 8               ; db[13] its 8.3 name, NUL-terminated
KDH_POFF    equ 21              ; dw the part's first file SECTOR
KDH_PLEN    equ 23              ; dd its packed bytes
KDH_ULEN    equ 27              ; dd ...and its unpacked bytes
KDH_LB      equ 31              ; db[512] the launch block, verbatim
KDH_WIN     equ 543             ; dw the window to wake on the way back, which
                                ; is the box's and only the box knows it
KDH_CODE    equ 545             ; dw the exit code the RESTORED kernel pokes in,
                                ; 0xFFFF = the program has not run. Both sides
                                ; of this one are the ordinary wake the box
                                ; already services (SPEC.md 74.1), so the
                                ; return needs no new slot
; --- AND THE CORE PART'S THREE, because the stub reads TWO (96.44.5.4) ------
; They go on the END rather than beside KDH_POFF, so KDH_LB's offset does not
; move: the launch block is 512 of the 547 and every field before it is one a
; wave already published.
KDH_COFF    equ 547             ; dw the CORE part's first file SECTOR
KDH_CPLEN   equ 549             ; dd its packed bytes
KDH_CULEN   equ 553             ; dd ...and its unpacked bytes
KDH_CORG    equ 557             ; dw where the core sits in kern_dos's
                                ; segment - CORE_ORG, which only the box knows
KDH_AKB     equ 559             ; dw the ARENA the program was given, in KB,
                                ; poked in beside KDH_CODE (SPEC.md 96.41.1)
KDH_SIZE    equ 561
KDH_SIG     equ 0x3148444B      ; 'KDH1', little-endian
KDH_NOCODE  equ 0xFFFF          ; ...and what KDH_CODE says until it has one

; --- the exit code's mailbox (SPEC.md 96.41) --------------------------------
; MIRRORED in kerndos/kdlayout.inc, and tests/unit/t_mirror.py is the guard.
KDB_SEG     equ 0x0040          ; the BDA...
KDB_OFF     equ 0x00F0          ; ...and its intra-application area
KDB_SIG     equ 0x3158444B      ; 'KDX1', little-endian
KDB_CODE    equ 4               ; dw the program's exit code, behind the magic
KDB_AKB     equ 6               ; dw ...and the arena it was given, in KB
%if HS_EXT + HS_XMAX*HS_XSTRIDE > HS_STACK - 512
  %error "hiber: the extent list would run into the stub's stack"
%endif

%ifdef KERN_BIG

section .text
; --- resident data: the template and the title (read through DS by wm) -----
hb_tpl:     dw 160, 150, 320, 100, hb_ttl, hbf_paint, hbf_onkey, hbf_onclick
hb_ttl:     db 'Hibernate', 0
hb_s_ptr:   db 'HIBERNAT.PTR', 0    ; THE ONE name (SPEC.md 87.3). The two
HB_S_EXT    equ hb_s_ptr + 9        ; files differ in three bytes, so the
                                    ; module STAMPS the extension it wants.
                                    ; Every use sets it, so there is no state
                                    ; to leave wrong; the resident probe is the
                                    ; one reader that does not, and it runs at
                                    ; boot before any module can have stamped

; --- the .text thunk: KD_INIT is called near in KERNEL_SEG by inst_launch ---
; ...and ONLY that one. The window's own callbacks are the cold hbf_ entries,
; which hb_tpl and hbm_kinit name directly: wm_pkgcall dispatches a kernel
; window's callbacks near in `.cold` (SPEC.md 2.6.3).
hb_kinit:   call COLD_SEG:hbf_kinit     ; KD_INIT: preserves every register
            ret

section .bss
; The state block. Part of the image, which is the point: what hb_perform
; banks before the write is what hbm_wake reads after the restore.
hb_mode:    resb 1              ; HB_M_*
hb_vol:     resb 1              ; the volume the image is (or will be) on
hb_down:    resb 1              ; the button the press armed, 1/2, 0 = none
hb_drvmask: resw 1              ; drv_tab rows detached before the image
hb_sp:      resw 1              ; hbm_perform's entry SP (SPEC.md 87.4)
hb_resumes: resw 1              ; how many times this machine has woken
hb_unit:    resw 1              ; the transport facts (SPEC.md 87.5 step 1)
hb_spt:     resw 1              ; - these FOUR are HS_UNIT..HS_NX's own order
hb_heads:   resw 1              ;   and are staged as one block copy
hb_nx:      resw 1              ; ...how many extents the walk found
hb_base:    resd 1
hb_xseg:    resw 1              ; the extent list's claim, 0 = none
; --- THE DOS HANDOFF's POSTED RECORD (SPEC.md 96.40) ------------------------
; A FAR POINTER and not a copy, which is osapi_pkg_rehome's economy: the
; package's region is alive until drv_shutdown, so the four bytes that say
; where its KDH_* record lies are all the kernel has to keep resident. A copy
; would be 543 bytes of every machine's RAM for a feature most never use.
hb_dosoff:  resw 1              ; the record, in the POSTER's segment...
                                ; ...and `hb_dosseg`, that segment (0 =
                                ; nothing posted), is DECLARED IN driver.inc
                                ; beside drv_dlg_seg (SPEC.md 96.40.8). It is
                                ; held from the post to the picture and read
                                ; again on the wake, and three claims between
                                ; can compact the poster's region away - the
                                ; module's own load, step 3's extent list, and
                                ; the wake's driver reload. So it is a holder
                                ; mem_rr_tab patches, and there it rides
                                ; drv_dlg_seg's row for no bytes at all
hb_dosakb:  resw 1              ; ...and the ARENA it was given, in KB, which
                                ; travels beside it and for its reason
                                ; (SPEC.md 96.41.1)
hb_doscode: resw 1              ; **THE ONLY RESIDENT BYTES THE RETURN COSTS**
                                ; (docs/plans/KERN-DOS-PLAN.md 8): the code a
                                ; DOS handoff left in the BDA, between hbm_ask
                                ; reading it at the boot and hbm_res staging it
                                ; for the wake. It cannot be module data -
                                ; those two are separate loads of the image
                                ; with a posted restart between them
hb_susp:    resw 1              ; the rows OSAPI_DRV_SUSPEND took out, to put
                                ; back (SPEC.md 51.11): the suspend's OWN word
                                ; and not [hb_drvmask], because a hibernate can
                                ; be taken while a suspend stands and the two
                                ; sets come back at different moments
                                ; ...and NO staging cells: the kernel already
                                ; has both singletons this module wanted, and
                                ; hbm_btn satisfies the invariant that makes
                                ; them singletons (UI task, gfx lock held,
                                ; stage-then-draw inside one call) -
                                ; os88ui_krect for the rect (SPEC.md 13) and
                                ; cp_sbuf for the label (ctrl.inc)

section .cold
; =============================================================================
; The resident half's cold code: the probe, the predicate, the far entries
; and the thunks.
; =============================================================================

; -----------------------------------------------------------------------------
; hb_probe - is there a hibernation to offer? (SPEC.md 87.5; kmain, after
;            drv_boot and before the first paint)
; in:  nothing
; out: nothing (all registers preserved). A hit records the volume in [hb_vol]
;      and posts UI_RBQ_ASK, which ui_task's first pass spends.
;
; The ONE root a pointer can be in - hb_pick's volume, the one a hibernate
; writes to - in a drv_vol_bank/back bracket (SPEC.md 51.5.2) so the machine
; is left standing where drv_boot left it, and reached the way drv_load
; reaches a driver: a QUIET navigation (18.9: no listing, no icon harvest)
; and a walk of the directory sectors, not the snapshot. A floppy-only
; machine pays eight compares and no I/O; a machine with a hard disk pays
; one quiet mount of its root.
; -----------------------------------------------------------------------------
section .boot2                    ; IN THE BLOB (SPEC.md 2.5.3.3): kmain_o is its
                                ; one caller, after drv_boot and before its own
                                ; BLOBCALL spl_finish
hb_probe_x:
    push ax
    push dx
    push si
    xor ax, ax                  ; .bss is whatever the LAST kernel left
    mov [hb_resumes], ax        ; (a warm boot clears nothing), and three of
    mov [hb_xseg], ax           ; these are read before anything writes them.
    mov [hb_dosakb], ax         ; FIVE STORES THROUGH THE ACCUMULATOR, which
    dec ax                      ; this routine has already pushed: 20 bytes
    mov [hb_doscode], ax        ; against 29, and `mov [m], ax` is 14 clocks
                                ; where `mov word [m], imm` is 20. KDH_NOCODE
                                ; is 0xFFFF and means "no DOS program ran",
                                ; which is every resume but one - SO THE `dec`
                                ; GOES HERE, off the zero, and not after the
                                ; mode below: from HB_M_GONE it makes 2, which
                                ; is a DOS exit code the machine never had and
                                ; which tests/hibernate.py reads back as one
    mov al, HB_M_GONE           ; ...and AH is left 0xFF, which nothing reads:
    mov [hb_mode], al           ; the mode is a byte
%if KDH_NOCODE != 0xFFFF
  %error "hb_probe spells KDH_NOCODE as a decremented zero"
%endif
    call COLD_SEG:hb_pick_x     ; AL = the volume
    jc .out
    mov [hb_vol], al
    call COLD_SEG:drvf_drv_vol_bank
    mov dl, al
    xor ax, ax                  ; the root
    call COLD_SEG:dkf_dsk_chdir_q
    jc .back
    mov si, hb_s_ptr
    mov bp, drv_find            ; (wm_cbd: kmain_o holds nothing in BP)
    call COLD_SEG:wm_cbd        ; the directory sectors themselves
    jc .back
    mov byte [ui_rebootq], UI_RBQ_ASK
.back:
    call COLD_SEG:drvf_drv_vol_back
.out:
    pop si
    pop dx
    pop ax
    retf
section .cold

; -----------------------------------------------------------------------------
; drv_suspend_x - slot 0x0419's three verbs (SPEC.md 51.11): THE MACHINE, OUT
;                 OF MY WAY. Entered from kernel.asm's thunk under api_x, so
;                 ES = the caller's DS and DS = KERNEL_SEG
; in:  AL = 1 suspend the hardware drivers, ES:DI = a DQ_SIZE-record buffer
;      each is asked DRVV_HWINFO into on the way past, or DI = 0; BL = extra
;      DRVC_* classes the skip list is to let go of, bit 1<<class, or 0 for
;      today's set (SPEC.md 51.11.4)
;      AL = 0 put back the rows a suspend took out
;      AL = 2 hand the whole machine to kern_dos, ES:SI = a KDH_* record in the
;      caller's own image or bss (96.40)
; out: CF = 0 done, CX = records written (0 and 1). CF = 1: the hibernate
;      module could not be read - the system disk is out - or, for 2, a post
;      already stands
; clobbers: AX, BX, CX, DX, SI, DI
;
; The suspend and the resume are HIBER.DRV's (hbm_drvsusp), because they ARE
; the hibernate's own detach and reload with a third skip and a question, and
; a module's bytes are resident only while it is asked for. The thunk loads
; it, calls the perform entry with the slot's AL moved up past the UI_RBQ_*
; that entry already takes, and DROPS it again on the way out - hbf_dropck,
; which leaves the image alone while the Hibernate window has it - because
; the one caller in the tree is about to claim the heap the module is sitting
; in (96.35). The whole resident cost is this routine and the six-byte thunk.
;
; A refusal here is the DISK's and nothing else's: the sweep cannot refuse. A
; caller that is refused a suspend has nothing to put back, and one refused a
; resume has its rows still noted in [hb_susp] for the next ask.
; -----------------------------------------------------------------------------
drv_suspend_x:
    cmp al, 2
    je osapi_dos_handoff_x
    add al, HB_P_RESUME         ; 0 / 1 -> the module's own two verbs.
                                ; **BL IS NOT BANKED** (SPEC.md 51.11.4): it
                                ; RIDES, because mod_need clobbers flags only
                                ; and modh_e_perform is `call/retf`, so the
                                ; slot's own register reaches hbm_drvsusp
                                ; untouched. The byte it used to be stored in
                                ; was kernel .bss and the store four bytes of
                                ; .cold, for a decision that is the module's
    call hbf_need
    jc osapi_dos_handoff_x.no
    call far [HBFP + HBE_PERFORM*4]
    call hbf_dropck             ; ...and the image goes back: the caller wants
    clc                         ; the memory. dropck's compare is not an answer
    retf

; -----------------------------------------------------------------------------
; osapi_dos_handoff_x - the DOS box asks for the machine (SPEC.md 96.40): verb
;                       2 of the slot above
; in:  ES:SI = a KDH_* record in the CALLER's segment
; out: CF=0 posted, CF=1 refused
;
; **IT ONLY RECORDS**, which is osapi_pkg_rehome's shape and
; osapi_mem_compact_wake's: what it asks for detaches every driver and takes
; the screen, and a package's callback is the one place that may not happen -
; the caller is executing in a region the teardown is about to stop caring
; about, with the gfx lock held. ui_task's step 0 spends it with nothing held.
;
; The record is read WHERE IT LIES rather than copied, so it must be in the
; package's image or bss and not on a stack: the four bytes here are the whole
; resident cost of the feature. ES is stored as a segment register: it is the
; caller's DS by api_x's contract and a package's DS is never 0, so there is
; nothing to validate and no register to bank.
osapi_dos_handoff_x:
    cmp word [hb_dosseg], 0     ; one at a time: a second post before the first
    jne .no                     ; is spent would be two machines' worth of
    mov [hb_dosseg], es         ; parameters and one teardown
    mov [hb_dosoff], si
    mov byte [ui_rebootq], UI_RBQ_DOSRUN
    mov byte [sch_uiwake], 1    ; ui_task blocks (SPEC.md 8.1.2), so the post
                                ; has to wake it or the handoff waits for
                                ; whatever else happens to. CF = 0 still:
    retf                        ; the cmp found nothing posted
.no:                            ; ...shared with the verbs above: a `stc`/`retf`
    stc                         ; is a `stc`/`retf`
    retf

; -----------------------------------------------------------------------------
; hb_pick - the disk a hibernation goes to, or is looked for on (SPEC.md 87.2):
;           the one the machine BOOTED from when that is a fixed disk, else
;           the first fixed volume there is - and never a DVK_FILE row, which
;           dsk_vol_fixed calls fixed and which has no sectors the stub could
;           read. THE predicate: the menu item, the probe and the window all
;           ask it, so what greys, what is probed and what is written to
;           cannot disagree (SPEC.md 47 rule 5)
; in:  nothing
; out: CF = 0 and AL = its index; CF = 1 no such volume
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
hb_pick_x:
    push bx
    push cx
    mov cl, [dsk_bootvol]
    call hb_pick1
    jnc .out
    xor cx, cx
.vol:
    call hb_pick1
    jnc .out
    inc cx
    cmp cx, DVOL_MAX
    jb .vol
    stc
.out:
    pop cx
    pop bx
    retf

hb_pick1:                       ; is volume CL one? out: CF = 0 and AL = CL
    mov bl, cl
    call dsk_vol_fixed_x        ; AL = 1 fixed, AH = the kind; CF = no row
    jc .no
    or al, al
    jz .no
    cmp ah, DVK_FILE
    je .no
    mov al, cl
    CLC_OR_STC .no              ; CF = 0 AL = CL, or (.no) CF = 1
    ret

; -----------------------------------------------------------------------------
; hb_ok - may the machine hibernate, AND WHICH STRING SAYS SO (SPEC.md 87.2,
;         47 rule 5)
; in:  nothing
; out: [menu_items_logo + 8] = menu_s_hiber / menu_s_hibernh / menu_s_hiberxm.
;      IT ANSWERS IN MEMORY AND NOT IN CF: ui_loc_gate is the only reader this
;      predicate has ever had, and it turned the CF/AL pair straight back into
;      one of three pointers - so the choice is here, where it is .cold bytes,
;      and .text is 19 lighter for it. A second caller wanting the reason back
;      re-adds the `clc`/`stc`; nothing wants it today
; clobbers: AX (AH is XCAPS's), flags. Everything else is
;           preserved, CX AND DX INCLUDED - see below
;
; Called on the press that opens the menu bar, so it is at most eight row
; tests and one far call into a table - no I/O, no probe.
;
; **THE FAR CALL ANSWERS IN THREE REGISTERS, NOT ONE** (SPEC.md 87.2).
; OSAPI_XM_CAPS is AX = free KB, DX:CX = the pool's linear base and BL = free
; block-table entries (SPEC.md 41.8), and this routine reads BL alone - so it
; saves CX and DX across it as well as BX, or it hands its caller three
; clobbers where its contract promises one.
;
; That is not a defensive push. ui_loc_gate promises ITS caller that nothing
; moves, and that caller is ui_task's mouse-down arm holding the press's x in
; CX and its y in DX; CX goes to menu_track three instructions later. And the
; call is reached ONLY when hb_pick found a fixed volume, so a floppy-booted
; machine takes the `jc .no` above it and never sees the damage: on an
; INSTALLED machine CX came back 0, every press in the bar hit-tested at x = 0,
; and cell 0 is the System menu in every application - so every menu title on
; the bar dropped the System menu.
; -----------------------------------------------------------------------------
%ifdef KERN_MEGA
HB_XMIN_KB equ (MPK_HDR + 512 + MEGA_IDT_LIN + 256 * 8 - MEGA_GDT_LIN + 1023) / 1024
%endif                          ; the header, the section's head, the tables
hb_ok_x:
    push bx
    push cx                     ; ...and these two, for the reason above
    push dx
    COLDCALL hb_pick_x          ; the volume a hibernate would go to
    mov bx, menu_s_hibernh      ; ...none: '(no disk)'. A `mov` writes no
    jc .set                     ; flag, so the string can be chosen first
%ifdef VID_EXT
    mov bx, menu_s_hibervx      ; a VESA primary (SPEC.md 120.4): an image
    cmp byte [vid_kind], VID_VESA   ; carrying [vid_kind] = 4 into a machine
    je .set                     ; whose driver has not attached would arm
%endif                          ; nothing and draw nothing - greyed, with why
%ifdef KERN_MEGA
    test byte [cpu_feat], CPU_F_PM  ; protected mode: the image carries the
    jz .rm                      ; pool (hbm_xwrite), so a block the store has
    mov ax, [mem_top]           ; given out is not a reason (KERN-MEGA-DOS-
    mov cl, 6                   ; PLAN 8, wave W5) - but a resume stages the
    shr ax, cl                  ; WHOLE picture in the pool, so a pool smaller
    add ax, HB_XMIN_KB          ; than conventional memory and the tables is
    mov bx, menu_s_hibernr      ; a refusal hbm_xplan is certain to make: a
    cmp ax, [xm_kb]             ; 1MB AT (10.5 lesser). A FACT, never a
    ja .set                     ; guess: it greys only what must refuse
    mov bx, menu_s_hiber
    jmp short .set
.rm:
%endif
    call KERNEL_SEG:HB_API_XCAPS    ; BL = free block-table entries; on a
    cmp bl, XM_MAX_BLKS         ; machine with no store, all of them
    mov bx, menu_s_hiber        ; ...and the STRING, because ui_loc_gate is
    jae .set                    ; the only reader this answer has ever had
    mov bx, menu_s_hiberxm      ; (size pass 10: the reason no longer goes
.set:                           ; through AL and back, -10)
    mov [menu_items_logo + 8], bx   ; item 4 of the System menu
    pop dx
    pop cx
    pop bx
    retf

; --- far entries for the module (SPEC.md 2.6): cold bodies that end in a near
; --- ret, reached from a segment of their own. Four bytes each.
; The nine bodies the module calls with BP free go through wm.inc's `wm_cbd`,
; which is `call bp / retf` - cw_mem_disp's shape one segment along, and the
; entry wm_pkgcall dispatches a kernel window's cold callbacks through
; (SPEC.md 2.6.3). This file had its own copy until then.
hbk_drv_blk_call:   call drv_blk_call_x ; ...and the tenth, which cannot use
                    retf                ; it: BP IS drv_blk_call's verb cell

; hbf_need - make the image resident (cpf_need's shape, ctrl.inc): the id in
; AL and the caller's AX put back, so a thunk below is three bytes and not
; seven. A NEAR call, and so is the mod_need inside it.
hbf_need:
    push ax
    mov al, MOD_HIBER
    call mod_need
    pop ax
    ret

; --- the thunks that load the module and call in (SPEC.md 2.8, cpf_'s shape)
; The four window callbacks are NEAR: hb_tpl and hbm_kinit name them, and
; wm_pkgcall calls a kernel window's callbacks near in `.cold` (SPEC.md 2.6.3).
hbf_paint:
    call hbf_need
    jc .gone
    call far [HBFP + HBE_PAINT*4]
.gone:
    ret

hbf_onclick:
    call hbf_need
    jc .gone
    call far [HBFP + HBE_ONCLICK*4]
.gone:
    ret

hbf_onup:                       ; a release can only follow a press this
    push ax                     ; window took, so mod_live, not mod_need
    mov al, MOD_HIBER           ; (SPEC.md 13.8.3)
    call mod_live
    pop ax
    jc hbf_onclick.gone         ; the refusal is a bare `ret`, and that one is
    call far [HBFP + HBE_ONUP*4]    ; onclick's - one byte, not a copy
    jmp short hbf_dropck        ; a tail call: its ret is ours

hbf_onkey:
    call hbf_need
    jc hbf_onclick.gone         ; ...and so is this one
    call far [HBFP + HBE_ONKEY*4]
    jmp short hbf_dropck        ; a tail call: its ret is ours

hbf_kinit:
    call hbf_need
    jc .gone
    call far [HBFP + HBE_KINIT*4]
.gone:
    retf

; hbf_open - the System menu's Hibernate... (ui_cmd, no lock held)
; A refusal here is about the DISK - the module could not be read - and the
; toast names it the way the Control Panel's does, out of the same table.
hbf_open:
    call hbf_need
    jc .no
    call far [HBFP + HBE_OPEN*4]
    retf
.no:
    jmp short hbf_perform.toast ; the toast and the retf, hbf_perform's

hbf_nodisk:
    push ax
    push bx
    push cx
    mov al, 1
    mov bx, cp_opentab
    mov cx, CP_NOPEN
    call ct_toast_say
    jmp kretc_cx                ; pop cx, bx, ax and ret

; hbf_perform - a posted UI_RBQ_HIBER / UI_RBQ_RESUME / UI_RBQ_ASK, spent by
;               ui_task with nothing held (SPEC.md 87.4, 87.5)
; in:  AL = which
; out: nothing. Returns only on a refusal or a discard: a hibernate ends in
;      int 19h and a resume ends in the stub.
hbf_perform:
    call hbf_need
    jc .no
    call far [HBFP + HBE_PERFORM*4]
    call hbf_dropck
    retf
.no:
    ; **A POST THAT CANNOT BE SPENT IS WITHDRAWN** (SPEC.md 96.40.6). The
    ; module could not be read, so nothing below `hbm_dosrun` runs - and that
    ; one clears the latch on every arm it has. Unconditional because
    ; `[hb_dosseg]` is zero unless a DOS handoff is what was posted, and
    ; `ui_rebootq` holds one post at a time.
    mov word [hb_dosseg], 0
    cmp al, UI_RBQ_ASK          ; a question that cannot be asked is not
    je .quiet                   ; worth a toast: the pointer stays for a
.toast:                         ; boot that has the system disk in (and
    call hbf_nodisk             ; hbf_open's refusal is this toast too)
.quiet:
    retf

; hbf_dropck - the window closed inside the call that just returned, so the
;              image goes back now, from OUTSIDE it (SPEC.md 87.1)
hbf_dropck:
    cmp byte [hb_mode], HB_M_GONE
    jne .out
    push ax
    mov al, MOD_HIBER
    call mod_drop
    pop ax
.out:
    ret

; hbf_closed - app_close_win's KIND_HIBER hook (SPEC.md 87.1): the window went
;              by a route the module did not take - the close box, Close
;              Window, the Task Manager, the restart sweep - so nothing of the
;              module is on the stack and the image can go now. The module's
;              own closes set HB_M_GONE FIRST, and that is the test: a GONE
;              window is the module closing itself, and hbf_dropck drops the
;              image once it has returned
hbf_closed:
    cmp byte [hb_mode], HB_M_GONE
    je .out
    mov byte [hb_mode], HB_M_GONE
    call hbf_dropck             ; ...which now finds it GONE and drops
.out:
    retf

; =============================================================================
; THE MODULE - HIBER.DRV (SPEC.md 2.8, 87)
;
; CS = the image's own claim, DS = KERNEL_SEG. Kernel data by name through
; DS; this file's own strings through CS; kernel code through KERNEL_SEG:cw_*
; shims (.text), COLD_SEG:*_x / hbk_* far entries (.cold) and the API cells.
; =============================================================================
section .modh
modh_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER                  ; MOD_H_VER
    db MOD_HIBER                ; MOD_H_ID
    dw BUILD_NUM                ; MOD_H_BUILD  (SPEC.md 2.8.2)
    dw MOD_STAMP                ; MOD_H_STAMP
    dw modh_end                 ; MOD_H_IMG
    dw HB_NENT                  ; MOD_H_NENT
    dw modh_e_paint             ; HBE_PAINT
    dw modh_e_onclick           ; HBE_ONCLICK
    dw modh_e_onup              ; HBE_ONUP
    dw modh_e_onkey             ; HBE_ONKEY
    dw modh_e_kinit             ; HBE_KINIT
    dw modh_e_perform           ; HBE_PERFORM
    dw modh_e_open              ; HBE_OPEN

modh_e_paint:   call hbm_paint
                retf
modh_e_onclick: call hbm_onclick
                retf
modh_e_onup:    call hbm_onup
                retf
modh_e_onkey:   call hbm_onkey
                retf
modh_e_kinit:   call hbm_kinit
                retf
modh_e_perform: call hbm_perform
                retf
modh_e_open:    call hbm_open
                retf

; hbm_p2rd - one byte from the 8042's output buffer, BOUNDED (a device that
; never answers costs a 65,536-poll wait, not a hang); AL = the byte, or the
; status if none came. CX preserved
hbm_p2rd:
    push cx
    xor cx, cx
.w:
    in  al, 0x64
    test al, 0x01
    jnz .got
    loop .w
    pop cx
    ret
.got:
    in  al, 0x60
    pop cx
    ret

%ifdef KERN_BIG
; hbm_kbak - the KEYBOARD's answer to a command, BOUNDED like hbm_p2rd; a byte
; the AUX device sent (status bit 5) is read and dropped rather than taken for
; it. AL = the byte, or the status if none came. CX preserved
hbm_kbak:
    push cx
    xor cx, cx
.w:
    in  al, 0x64
    test al, 0x01
    jz .n
    test al, 0x20               ; `in` writes no flag, so ZF is still bit 5's
    in  al, 0x60
    jz .x
.n:
    loop .w
.x:
    pop cx
    ret

; hbm_kstrand - a byte the 8042 holds that NO INTERRUPT WILL EVER TAKE, read
; and dropped (KERN-MEGA-PLAN 14.62). The controller has one output buffer for
; both devices and holds IRQ1 or IRQ12 high while a byte waits in it, so a
; byte whose edge was lost - an 8259 initialised under it, its line's
; interrupt off in the command byte when it arrived, the line masked when it
; came and the latch reset since - keeps both devices silent for good. A byte
; whose request IS latched at the 8259 (IRR; the controller is given a few
; polls to raise it) is left for its ISR. The desktop's own state at the end
; of the return, whatever the program or a package left. AX clobbered
hbm_kstrand:
    pushf
    cli
    push cx
    mov cx, 4                   ; four strands at most: then it is live traffic
.l:
    in  al, 0x64
    test al, 0x01
    jz .x
    mov ah, al
    push cx
    mov cx, 64                  ; the controller's own lag from OBF to its pin
.s:
    in  al, 0x64
    loop .s
    pop cx
    mov al, 0x0A                ; OCW3: read the IRR
    test ah, 0x20
    jnz .aux
    out 0x20, al
    in  al, 0x20
    test al, 0x02               ; IRQ1 requested: int 09h will take it
    jmp short .q
.aux:
    out 0xA0, al
    in  al, 0xA0
    test al, 0x10               ; IRQ12 requested: int 74h will take it
.q:
    jnz .x
    in  al, 0x60
    loop .l
.x:
    pop cx
    popf
    ret
%endif

hbm_knk     db 0xF3, 200, 0xF3, 100, 0xF3, 80, 0xF2 ; mou_p2knk's (SPEC.md 9.9.8)
hbm_knk_n   equ $ - hbm_knk

; --- near doors to the far calls this image makes most (SPEC.md 2.6): a far
; --- call is five bytes and a near one three, so each door pays for itself by
; --- its third site. Every one is `call far` / `ret` and preserves exactly
; --- what the far entry does.
hbm_kd:     call KERNEL_SEG:cw_mem_disp     ; BP = a .text body
            ret
hbm_cb:     call COLD_SEG:wm_cbd            ; BP = a .cold body
            ret
hbm_vbn:    call COLD_SEG:drvf_drv_vol_bank
            ret
hbm_vbk:    call COLD_SEG:drvf_drv_vol_back
            ret
hbm_lock:   call KERNEL_SEG:cw_gfx_lock
            ret
hbm_unlock: call KERNEL_SEG:cw_gfx_unlock
            ret

; hbm_post - post AL for the top of ui_task's pass, unless a restart already
;            posted wins (SPEC.md 20.10). Clobbers flags only
hbm_post:
    cmp byte [ui_rebootq], 0
    jne .out
    mov [ui_rebootq], al
.out:
    ret

; --- the two names, stamped (SPEC.md 87.3): the kernel carries ONE and the
; --- module writes the three bytes that tell them apart. Out: SI = the name.
; --- Clobbers AX and DL, which no caller holds across one of these
hbm_nimg:
    mov ax, 'IM'
    mov dl, 'G'
    jmp short hbm_nset
hbm_nptr:
    mov ax, 'PT'
    mov dl, 'R'
hbm_nset:
    mov [HB_S_EXT], ax
    mov [HB_S_EXT+2], dl
    mov si, hb_s_ptr
    ret

; --- geometry of the window's content (SPEC.md 87.1) -------------------------
HBW_LX      equ 8               ; text left, content-relative
HBW_LY      equ 6               ; first line's top
HBW_LH      equ 12              ; line pitch
HBW_BY      equ 54              ; the button band's top
HBW_BH      equ 16              ; ...and height
HBW_B1X     equ 8               ; the first button
HBW_B1W     equ 120
HBW_B2X     equ 140             ; the second
HBW_B2W     equ 80
HBW_CW      equ 320 - 2         ; content width: the template's W_W less the
HBW_CH      equ 100 - TITLE_H - 1   ; borders; height less the title bar

; -----------------------------------------------------------------------------
; hbm_str - draw one of THIS image's strings (SPEC.md 2.8.6)
; in:  CX = x, DX = y, SI = the string's offset in this image; the pen is set
; out: nothing (all registers preserved)
;
; The transparent string cell is an X stub: ES becomes the CALLER's DS and DS
; the kernel's, so with DS = CS for the call the string is read from here and
; [gfx_color] from where it lives.
; -----------------------------------------------------------------------------
hbm_str:
    push ds
    push cs
    pop ds
    call KERNEL_SEG:HB_API_FONTX
    pop ds
    ret

; -----------------------------------------------------------------------------
; hbm_stage - copy one of this image's NUL strings to DS:DI (kernel memory)
; in:  SI = the string's offset here, DI = the destination, CX = its size
; out: nothing (all registers preserved). Always NUL-terminated.
; -----------------------------------------------------------------------------
hbm_stage:
    push ax
    push cx
    push si
    push di
    dec cx                      ; room for the NUL
.copy:
    mov al, [cs:si]
    mov [di], al
    or al, al
    jz .done
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_toast - say one of this image's strings in the menu bar (SPEC.md 59)
; in:  SI = the string's offset here
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_toast:
    push cx
    push es
    push cs
    pop es                      ; toast_show takes ES:SI
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    pop es
    pop cx
    ret

%ifdef MEGA_DIAG
; -----------------------------------------------------------------------------
; hbm_d1why - "Not our handoff record" as a field reading (KERN-MEGA-PLAN 10.3
; item 54.2): `H ssss:oooo bbbbbbbb rrr`, the pointer DOS.O88 posted, the
; first four bytes found there read DIRECTLY (no string op, nothing that can
; fault - a good record reads 4B444831, 'KDH1'), and mega_strcal's REP MOVS
; figures for SI, DI and CX. Built in hbm_dosrec, whose copy has just failed.
; TOAST_MAX is 24 and this is 24. DS = KERNEL. out: SI = the string. Clobbers
; AX, CX, DI, ES
; -----------------------------------------------------------------------------
hbm_d1why:
    PUSH_CSDATA
    pop es
    mov di, hbm_dosrec
    mov ax, 'H '
    stosw
    mov ax, [hb_dosseg]
    call hbm_dhx4
    mov al, ':'
    stosb
    mov ax, [hb_dosoff]
    call hbm_dhx4
    mov al, ' '
    stosb
    push si
    push ds
    mov si, [hb_dosoff]
    mov ds, [hb_dosseg]
    mov cx, 4
.b:
    mov al, [si]                ; DS = THE POSTER'S segment, on purpose
    inc si
    call hbm_dhx2
    loop .b
    pop ds
    mov al, ' '
    stosb
    mov si, mega_sdr
    mov cx, 3
.r:
    mov al, [si]                ; DS = the kernel's: mega_sdr is its data
    inc si
    add al, '0'
    stosb
    loop .r
    xor al, al
    stosb
    pop si
    mov si, hbm_dosrec
    ret
hbm_dhx4:                       ; AX as four hex digits at ES:DI
    xchg al, ah
    call hbm_dhx2
    xchg al, ah
hbm_dhx2:                       ; AL as two
    push ax
    push cx
    mov cl, 4
    shr al, cl
    call hbm_dhx1
    pop cx
    pop ax
    and al, 0x0F
hbm_dhx1:
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 7
.d:
    stosb
    ret
%endif

; -----------------------------------------------------------------------------
; hbm_origin - the window's content origin
; in:  SI = window ptr
; out: AX = content left, DX = content top
; -----------------------------------------------------------------------------
hbm_origin:
    push bx
    mov bx, si
    call KERNEL_SEG:cw_wm_content
    pop bx
    ret

; -----------------------------------------------------------------------------
; hbm_btnrect - one button's rect, staged in os88ui_krect
; in:  AL = 1 or 2, SI = window ptr
; out: BX = os88ui_krect, filled; SI = the label's offset here (this mode)
; clobbers: AX, BX, CX, DX, SI, flags
; -----------------------------------------------------------------------------
hbm_btnrect:
    push ax
    call hbm_origin             ; AX = left, DX = top
    mov bx, os88ui_krect
    add dx, HBW_BY
    mov [bx+2], dx
    add dx, HBW_BH - 1
    mov [bx+6], dx
    xchg cx, ax                 ; CX = left
    pop ax
    cmp al, 1
    mov si, hbm_s_bhib          ; button 1...
    mov ax, HBW_B1X
    mov dx, HBW_B1W - 1
    je .one
    mov si, hbm_s_bcan          ; ...or button 2
    mov ax, HBW_B2X
    mov dx, HBW_B2W - 1
.one:
    add ax, cx
    mov [bx], ax
    add ax, dx
    mov [bx+4], ax
    cmp byte [hb_mode], HB_M_RESUME
    jne .out
    add si, hbm_s_bres - hbm_s_bhib ; the RESUME face's pair, the same
.out:                               ; distance on from each (asserted there)
    ret

; -----------------------------------------------------------------------------
; hbm_btn - draw one button, upright or pressed as [hb_down] says
; in:  AL = 1 or 2, SI = window ptr; the gfx lock is held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, OS88UI_FILL         ; the interior is erased first, so a redraw
    cmp al, 1                   ; between the two states never leaves a mark
    jne .flags
    or di, OS88UI_DEF           ; Enter is the first button
.flags:
    cmp al, [hb_down]
    jne .up
    or di, OS88UI_DOWN
.up:
    call hbm_btnrect            ; BX = the rect, SI = the label here
    push di
    mov di, cp_sbuf
    mov cx, CP_SBUF
    call hbm_stage              ; ...staged where the button drawer reads it
    pop di
    mov si, cp_sbuf
    call COLD_SEG:os88ui_btn_f
    jmp hbm_ret6

; -----------------------------------------------------------------------------
; hbm_hit - which button is a point in?
; in:  CX = x, DX = y (screen), SI = window ptr
; out: AL = 1, 2 or 0
; clobbers: AL (the output), flags
; -----------------------------------------------------------------------------
hbm_hit:
    push bx
    push cx
    push dx
    push si
    push di
    mov di, cx                  ; DI = x, BX = y, across hbm_btnrect
    mov bx, dx
    mov al, 1
    call hbm_hitone
    jc .yes
    mov al, 2
    call hbm_hitone
    jc .yes
    xor al, al
.yes:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; hbm_hitone - is the point DI,BX inside button AL? (out CF = 1 yes)
hbm_hitone:
    push ax                     ; AL is hbm_hit's OWN output on a hit, BX the
    push bx                     ; y it carries across both calls and SI the
    push si                     ; window ptr - hbm_btnrect eats the last two,
    push bx                     ; y, across hbm_btnrect - which clobbers CX,
    call hbm_btnrect            ; BX = the krect. (It was `mov cx, bx` BEFORE
    pop cx                      ; the call, and no click ever landed)
    cmp di, [bx]
    jb .no
    cmp di, [bx+4]
    ja .no
    cmp cx, [bx+2]
    jb .no
    cmp cx, [bx+6]
    ja .no
    stc
    jmp short .out
.no:
    clc
.out:
    pop si                      ; ...and hbm_hit banks CX, DX and DI itself
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_paint - W_PAINT (SPEC.md 87.1)
; in:  SI = window ptr; content already white; the gfx lock is held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call KERNEL_SEG:HB_API_COLOR
    call hbm_origin             ; AX = left, DX = top
    mov cx, ax
    add cx, HBW_LX
    add dx, HBW_LY
    mov bx, hbm_lines_ask
    mov al, [hb_mode]
    cmp al, HB_M_BUSY
    jne .notbusy
    mov bx, hbm_lines_busy
.notbusy:
    push ax
    mov al, [hb_vol]            ; the letter, patched into both faces' lines
    add al, 'A'
    mov [cs:hbm_s_r1 + HBM_R1_LTR], al
    mov [cs:hbm_s_a2 + HBM_A2_LTR], al
    pop ax
    cmp al, HB_M_RESUME
    jne .lines
    mov bx, hbm_lines_res
.lines:
    push si
.line:
    mov si, [cs:bx]
    or si, si
    jz .lined
    call hbm_str
    add dx, HBW_LH
    add bx, 2
    jmp short .line
.lined:
    pop si
    cmp byte [hb_mode], HB_M_BUSY
    je .out                     ; no buttons while it is happening
    mov al, 1
    call hbm_btn
    mov al, 2
    call hbm_btn
.out:
; --- THE SHARED EPILOGUE: hbm_paint's own, and the tail every routine here
; --- that banks the same registers in the same order jumps to (a near `jmp`,
; --- so it costs no stack). hbm_geom pops BP first and joins it
hbm_ret6:
    pop di
    pop si
hbm_ret4:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; hbm_repaint - erase the content and paint it again, for a mode change
; in:  SI = window ptr; the gfx lock is held
hbm_repaint:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call KERNEL_SEG:HB_API_COLOR
    call hbm_origin             ; AX = left, DX = top
    mov bx, dx
    mov cx, ax
    add cx, HBW_CW - 1
    add dx, HBW_CH - 1
    call KERNEL_SEG:HB_API_FILL ; AX=x1, BX=y1, CX=x2, DX=y2
    call hbm_paint
    jmp hbm_ret4

; hbm_win - this kind's window, if it is up
; out: CF = 0 and SI = window ptr; CF = 1 none
hbm_win:
    push ax
    push di
    mov al, KIND_HIBER
    call KERNEL_SEG:cw_inst_find_kind   ; DI = the record
    jc .no
    mov si, [di+I_WIN]
    or si, si
    jz .no
    pop di
    pop ax                      ; (CF = 0: the `or`'s, which `jz` left)
    ret
.no:
    pop di
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hbm_onclick - W_ONCLICK: arm the button the press landed on (SPEC.md 13.8)
; in:  CX = x, DX = y, SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
hbm_onclick:
    push ax
    cmp byte [hb_mode], HB_M_BUSY
    je .out
    call hbm_hit
    mov [hb_down], al
    or al, al
    jz .out
    call hbm_btn                ; ...drawn pressed
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_onup - W_ONMOUSEUP: fire if the release is on the armed button
; in:  CX = x, DX = y, SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
hbm_onup:
    push ax
    push bx
    mov bl, [hb_down]
    mov byte [hb_down], 0
    or bl, bl
    jz .out
    cmp byte [hb_mode], HB_M_BUSY
    je .out
    mov al, bl
    call hbm_btn                ; upright again, whatever happens next
    call hbm_hit
    cmp al, bl
    jne .out
    call hbm_act
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_onkey - W_ONKEY: Enter is the first button, Esc the second
; in:  AL = ascii, AH = scan, SI = window ptr; the gfx lock is held
; -----------------------------------------------------------------------------
hbm_onkey:
    push ax
    cmp byte [hb_mode], HB_M_BUSY
    je .out
    cmp al, 13
    jne .notcr
    mov al, 1
    jmp short .act
.notcr:
    cmp al, 27
    jne .out
    mov al, 2
.act:
    call hbm_act
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_act - one of the two buttons, in this mode (SPEC.md 87.1)
; in:  AL = 1 or 2, SI = window ptr; the gfx lock is held, UI task
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_act:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [hb_mode], HB_M_RESUME
    je .resume
    cmp al, 2
    je .close
    call hbm_arm                ; Hibernate: check the disk, then post
    jmp short .out
.resume:
    cmp al, 2
    je .discard
    ; --- Resume: say so, and post the restore for the top of the pass -------
    mov byte [hb_mode], HB_M_BUSY
    call hbm_repaint
    mov al, UI_RBQ_RESUME
    call hbm_post               ; ...unless a restart already stands
    jmp short .out
.discard:
    call hbm_vbn                   
    call hbm_forget             ; the pointer and the image go
    call hbm_vbk                   
.close:
    mov byte [hb_mode], HB_M_GONE       ; BEFORE the close: app_close_win's
    mov bx, si                          ; KIND_HIBER hook drops the image on
    mov bp, app_close_win
    call hbm_kd                    ; every OTHER route (SPEC.md 87.1), and
                                        ; GONE is how it knows this is not one.
                                        ; Synchronous: task-less kind; the
                                        ; thunk drops the image on the way back
.out:
    jmp hbm_ret6

; -----------------------------------------------------------------------------
; hbm_enter / hbm_root - stand in the root of [hb_vol], where BOTH files are
;              (SPEC.md 87.3). hbm_enter banks the user's volume first; the
;              callers that have already banked enter at hbm_root.
; out: CF = the chdir's; DX preserved
; -----------------------------------------------------------------------------
hbm_enter:
    call hbm_vbn                   
hbm_root:
    push dx
    mov dl, [hb_vol]
    xor ax, ax                  ; the root: both files live in it (87.3)
    mov bp, dsk_chdir_x         ; ...through the one far entry (SPEC.md 2.6)
    call hbm_cb         
    pop dx                      ; ...and a pop does not touch the chdir's CF
    ret

; -----------------------------------------------------------------------------
; hbm_findimg - find HIBERNAT.IMG in the volume standing now
; out: CF = 0 with BX = its first cluster and DX:CX = its size;
;      CF = 1 = there is no such file
; clobbers: AX, BX, CX, DX (the outputs)
;
; **BY NAME OFF THE DIRECTORY, not out of the global listing** (SPEC.md 18.4).
; It was `dsk_find_name_x` then `dsk_get_dir_x` - two thunked far calls to
; reach a staged entry from which its three callers took exactly two things,
; the first cluster at +18 and the size at +20. `dskw_stat_x` answers both in
; REGISTERS off a directory walk, so this is one far call instead of two and
; the entry is never staged at all.
;
; It is also what takes hibernate out of the global snapshot's consumer list
; (docs/plans/LISTING-HOME-PLAN.md): a resume runs with no Disk window in
; existence, so it was the reader that no window's cache could ever serve.
; The walk it pays for instead is noise beside writing a whole memory image.
; -----------------------------------------------------------------------------
hbm_findimg:
    call hbm_nimg               ; SI = the name - this build has ONE, and the
    mov bp, dskw_stat_x         ; extension is stamped on (SPEC.md 87.3)
    call hbm_cb         
    ret                         ; CF, BX and DX:CX are all its answer

; -----------------------------------------------------------------------------
; hbm_forget - delete the pointer and the image (SPEC.md 87.6 step 5, and
;              87.4 step 3, before a new pair is written)
; in:  [hb_vol] = the volume; both files are in its root
; out: nothing (all registers preserved); failures are ignored - there is
;      nothing further to do about a file that is already gone. The volume is
;      left at the root of [hb_vol]: a caller standing elsewhere brackets
;      this with drv_vol_bank/back.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; hbm_psum - the sparse sum over what the PROGRAMS hold (SPEC.md 87.4.1)
; in:  DS = KERNEL_SEG
; out: AX = s1, DX = s2: one word in every 256 bytes of every conventional
;      heap record a live PACKAGE instance owns, its place in the line
;      stepping 34 bytes (mod 256) a sample, Fletcher's two running sums
;      mod 65536. ~2,500 reads for 640KB, a few ms on a 4.77 MHz 8088
; clobbers: nothing else
;
; THE PROGRAMS AND NOT THE MACHINE, and that is the measurement and not a
; shortcut: the image is written LIVE, through the file layer, so the
; kernel's own memory - the FAT and directory buffers the write updates,
; the stack it runs on, the BDA, whatever an interrupt counts - is not the
; same at any one instant as the bytes the disk got. A whole-memory sum
; refuses good images. What no code touches between this read and the
; transfer is a package's: [sch_lock] stops its workers and the UI task is
; the writer, so no callback runs. A record above 1MB (kern_mega's
; selectors) is not in this image and the park's own sum covers it
; -----------------------------------------------------------------------------
hbm_psum:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov ax, 1                   ; s1 from 1, so zeroed memory is not a match
    xor dx, dx                  ; for a sum over memory that was not zero
    xor bp, bp
    mov si, mem_tab             ; .lowbss: LOW_SEG, which is SS here (the UI
.rec:                           ; task's stack, and hbm_wake's)
    mov cx, [ss:si+MC_SEG]
    jcxz .next
    cmp cx, [mem_top]
    jae .next
%ifdef KERN_MEGA
    test cl, 4                  ; a selector: above 1MB
    jnz .next
%endif
    mov di, [ss:si+MC_OWN]
    cmp di, INST_MAX
    jae .next                   ; the kernel's, a driver's
%if I_RECSZ != 32
  %error "hbm_psum: inst_tab's stride is a shift by 5"
%endif
    push cx
    mov cl, 5
    shl di, cl
    pop cx
    add di, inst_tab
    cmp byte [di+I_STATE], 0
    je .next
    test byte [di+I_KIND], KIND_PKG
    jz .next                    ; a built-in's: the kernel writes it
    mov bx, [ss:si+MC_PARA]
.line:
    cmp bx, 16
    jb .next                    ; a part line at the end is not sampled
    mov es, cx
    add ax, [es:bp]
    add dx, ax
    add bp, 34
    and bp, 0xFE
    add cx, 16
    sub bx, 16
    jmp short .line
.next:
    add si, MC_SIZE
    cmp si, mem_tab + MEM_MAX * MC_SIZE
    jb .rec
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

hbm_forget:
    push ax
    push dx
    push si
    call hbm_root               ; both live in the root (SPEC.md 87.3)
    jc .out
    call hbm_nptr               ; the pointer FIRST: with it gone there is
    call COLD_SEG:dwf_dskw_delete   ; nothing to resume whatever becomes of
    call hbm_nimg               ; the image (SPEC.md 87.3)
    call COLD_SEG:dwf_dskw_delete
.out:
    pop si
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_kinit - KD_INIT (SPEC.md 29.3): the release hook and the snap
; in:  BX = window ptr, DI = 0, SI = the record; no lock; not yet visible
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_kinit:
    push ax
    mov byte [hb_down], 0
    mov ax, hbf_onup            ; a COLD offset (SPEC.md 2.6.3)
    call KERNEL_SEG:cw_wm_onmouseup
    mov al, 1
    call KERNEL_SEG:cw_wm_snap  ; text on cell boundaries (SPEC.md 11.94)
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_open - the System menu's Hibernate... (SPEC.md 87.1): open the window
; in:  nothing; UI task, no lock
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
hbm_open:
    push ax
    push si
    call hbm_win                ; already up, in whatever mode: app_launch
    jnc .launch                 ; fronts it and the mode stays what it is
    call COLD_SEG:hb_pick_x     ; the disk (SPEC.md 87.2): the item was
    jc .out                     ; live, so there is one
    mov [hb_vol], al            ; ...and the window names it
    mov byte [hb_mode], HB_M_ASK
    mov byte [hb_down], 0
.launch:
    mov al, KIND_HIBER
    call KERNEL_SEG:cw_app_launch
    jnc .out
    call KERNEL_SEG:cw_snd_beep
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_geom - the transport facts for a volume (SPEC.md 87.5 step 1)
; in:  AL = the volume index
; out: CF = 0 and [hb_unit], [hb_spt], [hb_heads], [hb_base] filled;
;      CF = 1: not a fixed disk the ROM's int 13h reaches
; clobbers: flags
; -----------------------------------------------------------------------------
hbm_geom:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bl, al
    call COLD_SEG:dskf_dsk_vol_row  ; BX = the row
    jc .no
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .drv
    mov dl, [bx+DV_UNIT]        ; the boot partition (SPEC.md 52.10.3): the
    test dl, 0x80               ; kernel holds every fact itself
    jz .no
    xor dh, dh
    mov [hb_unit], dx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov dx, [dsk_vbase + bx]
    mov [hb_base], dx
    mov dx, [dsk_vbase + bx + 2]
    mov [hb_base + 2], dx
    mov dx, [dsk_bootspt]
    mov [hb_spt], dx
    mov dx, [dsk_boothds]
    mov [hb_heads], dx
    jmp short .check
.drv:
    cmp byte [bx+DV_KIND], DVK_DRV
    jne .no
    mov ah, [bx+DV_UNIT]        ; the driver's handle
    mov al, [bx+DV_CLASS]
    mov [drv_blkcls], al
    call COLD_SEG:drvf_drv_cls_svc  ; DI = the class's service table
    jc .no
    mov bp, [di+DSV_GEOM]       ; == DSV_BLK, and AL = 2 is the sub-function
    mov al, 2
    call COLD_SEG:hbk_drv_blk_call  ; DL = unit, CX:BX = base, SI = spt,
    jc .no                      ; DI = heads; CF = 1 not int 13h
    xor dh, dh
    mov [hb_unit], dx
    mov [hb_base], bx
    mov [hb_base + 2], cx
    mov [hb_spt], si
    mov [hb_heads], di
.check:
    mov ax, [hb_spt]            ; 1..63 and 1..255 as ONE unsigned window
    dec ax                      ; each: 0 wraps to 0xFFFF and is refused by
    cmp ax, 62                  ; the same `ja` the upper bound needs
    ja .no
    mov ax, [hb_heads]
    dec ax
    cmp ax, 254
    ja .no
    CLC_OR_STC .no              ; CF = 0, or (.no) CF = 1
    pop bp
    jmp hbm_ret6                ; pops neither touch CF

; -----------------------------------------------------------------------------
; hbm_arm - the Hibernate button (SPEC.md 87.2, 87.4): the disk in [hb_vol]
;           has to be one the ROM reaches and have the room, and then the
;           write is POSTED for the top of the pass
; in:  SI = the window; the gfx lock is held
; out: nothing (all registers preserved); a refusal is a toast
; -----------------------------------------------------------------------------
hbm_arm:
    push ax
    push bx
    push cx
    push dx
    push si
    call hbm_vbn                    ; the user may be standing anywhere
    mov al, [hb_vol]
    call hbm_geom
    jc .nobios
    call hbm_root               ; the root: where the image goes, and where
    jc .failed                  ; dfree answers for
    call COLD_SEG:dwf_dskw_dfree    ; DX:AX = free bytes
    jc .failed
    push ax
    push dx
    push si
    call hbm_findimg            ; ...plus what replacing an old image frees
    jc .noold                   ; DX:CX = the old image's size
    pop bx                      ; BX = the window, banked
    mov si, dx                  ; ...and SI is free the instant it is: the
    pop dx                      ; size's high half has to outlive the two
    pop ax                      ; pops that are about to land in DX:AX
    add ax, cx
    adc dx, si
    push ax
    push dx
    push bx
.noold:
    pop si
    pop dx
    pop ax
    push ax
    push dx
    call hbm_imgsize            ; DX:AX = the image's bytes
    add ax, 0x1000              ; ...and the pointer, with room to spare
    adc dx, 0
    mov cx, ax
    mov bx, dx                  ; BX:CX = the need
    pop dx
    pop ax                      ; DX:AX = the room
    cmp dx, bx
    jb .noroom
    ja .room
    cmp ax, cx
    jb .noroom
.room:
    call hbm_vbk                   
    mov byte [hb_mode], HB_M_BUSY
    call hbm_repaint
    mov al, UI_RBQ_HIBER
    call hbm_post               ; ...unless a restart already stands
    jmp short .out
.nobios:
    mov si, hbm_s_nobios
    jmp short .refuse
.failed:
    mov si, hbm_s_failed        ; the disk could not be read, which is not
    jmp short .refuse           ; "no room" (LESSONS: the right refusal with
.noroom:                        ; the wrong reason sends the user deleting
    mov si, hbm_s_noroom        ; files that were never the problem)
.refuse:
    call hbm_toast
    call hbm_vbk                   
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_perform - a posted request, spent with nothing held (SPEC.md 87.4/87.5)
; in:  AL = UI_RBQ_HIBER / UI_RBQ_RESUME / UI_RBQ_ASK; UI task, no lock
; out: nothing; returns only from a refusal, a discard, or the question
;
; [hb_sp] is this routine's entry SP - the word under it is the return into
; modh_e_perform - and it is the frame hbm_wake comes back to after a
; resume, and the one every exit below returns through.
; -----------------------------------------------------------------------------
hbm_perform:
    ; STKBALANCE-OK: enters the resume stub by pushing HS_CODE:0 and `retf`
    ; into it (SPEC.md 87.5) - a far jump spelled as a return, on purpose, so
    ; the static walk sees a ret two words deep. Nothing returns here.
    mov [hb_sp], sp
    cld
    cmp al, UI_RBQ_HIBER
    je hbm_hib
    cmp al, UI_RBQ_RESUME
    je hbm_res
    cmp al, UI_RBQ_DOSRUN
    je hbm_dosrun
    cmp al, HB_P_RESUME         ; ...and OSAPI_DRV_SUSPEND's two (SPEC.md
    jae hbm_drvsusp             ; 51.11), carried by this entry so the module
    jmp hbm_ask                 ; gains no eighth

; --- hbm_hib: write the image and stop (SPEC.md 87.4) ------------------------
; -----------------------------------------------------------------------------
; hbm_wrimg - THE IMAGE AND ITS POINTER, and nothing after them (87.4 steps 1-6)
; in:  AL = what HBP_DOS is to say: 0 the user is leaving, 1 a DOS handoff
; out: CF = 0 written. CF = 1 not, SI = the refusal, and the caller owns the
;      unwind - the lock, [sch_lock] and the detached drivers are STILL TAKEN
;      either way, because both callers go on holding them
;
; Factored because docs/plans/KERN-DOS-PLAN.md 8 has a second customer: the DOS
; handoff writes an image so that the machine has somewhere to come back to,
; and then does NOT stop - where hibernate's own step 7 asks the user to switch
; the machine off.
; -----------------------------------------------------------------------------
hbm_wrimg:
    push ax
    mov [cs:hbm_ptrfix + HBP_DOS], al   ; ...WHY this image exists, the one
                                ; thing in the pointer about the occasion
                                ; rather than the machine
    mov byte [cs:hbm_dosheld], 1    ; ...both taken below, and given back by
                                    ; neither arm
%ifdef HMA_DRV
    mov word [cs:hbm_hseg], 0   ; no pool copy yet: .no frees only a real one
%endif
    call hbm_vbn                    ; where the user stands, for a refusal
    mov si, hb_drvmask          ; 1. the hardware drivers go, and are noted:
    mov byte [cs:hbm_spm], 0    ; THE HIBERNATE'S OWN SWEEP IS TODAY'S SKIP
                                ; LIST, whatever the last suspend asked for
                                ; (SPEC.md 51.11.4): the mask is a slot
                                ; argument, and a hibernate is not that slot
    xor dx, dx                  ; no third skip...
    xor di, di                  ; ...and nobody asking where the hardware is.
    call hbm_sweep              ; Every register but AX is this routine's to
                                ; spend - both callers reload what they use
    call hbm_lock               ; 2. held from here: the machine is one
    inc byte [sch_lock]         ;    instant from here on - and only NOW
    cmp word [ser_row], 0xffff
    je .serialclear
    mov si, hbm_s_serial
    jmp .nos                   ; unwind the same locks and suspended drivers
.serialclear:
    call hbm_root               ; 3. the root of the disk the boot picked:
    jc .no                      ;    the detach yields, and a task that took
                                ;    the turn could have moved the current
                                ;    directory under the write
    call hbm_forget             ;    ...and the OLD pointer and image go
                                ;    before a byte is written: a pointer that
                                ;    outlived its question would otherwise
                                ;    name the NEW image with the old session's
                                ;    wake address (SPEC.md 87.3)
%ifdef HMA_DRV
    call hbm_hsave              ; 3b. the HMA's pool, into a claim the image
    mov si, hbm_s_nomem         ;    carries - last, so nothing but the write
    jc .nos                     ;    runs between the copy and the picture
%endif
                                ; 4. (SPEC.md 87.4) - [hb_sp] was banked at
                                ;    the top, and where the restored kernel
                                ;    enters goes straight into the pointer at
                                ;    step 6 (HBP_WAKE): the .bss copy that
                                ;    used to be taken here was never read
%ifdef KERN_MEGA
    test byte [cpu_feat], CPU_F_PM  ; kern_mega in protected mode: a volume
    jz .rimg                    ; of 512-byte clusters is refused first -
    mov si, hbm_s_clus          ; hbm_xwconv puts chunk 0 right from 40h
    cmp byte [dsk_spc], 1       ; past its first CLUSTER, and 40h cannot
    je .nos                     ; reach linear 200h. Then the list of what
    call hbm_xplan              ; is above 1MB, and whether a resume could
    jc .no                      ; place it, BEFORE the picture - which then
.rimg:                          ; includes the masks and the wake address
%endif
    call hbm_psum               ; 5. THE PROGRAMS' SUM, taken here, the last
    CSDO mov [CSEG hbm_psv], ax ;    instant before the write: nothing that
    CSDO mov [CSEG hbm_psv + 2], dx ; runs from here touches a program's
                                ;    memory, so the image holds what this
                                ;    read (SPEC.md 87.4.1). Into this image,
                                ;    so it is written with it
    call hbm_nimg               ;    ...and the image: all of memory, one write
    xor bx, bx
    mov es, bx
    call hbm_imgsize            ; DX:AX = bytes
%ifdef KERN_MEGA
    test byte [cpu_feat], CPU_F_PM
    jz .rsz
    call hbm_xconv              ; ...in whole chunks, so the section appends,
    call hbm_xwconv             ; and a chunk a call (below)
    jmp short .rwrote
.rsz:
%endif
    mov cx, ax
    call COLD_SEG:dwf_dskw_write
.rwrote:
    jc .no
%ifdef KERN_MEGA
    test byte [cpu_feat], CPU_F_PM
    jz .rptr
    call hbm_xwrite             ; 5b. the extended section, after it
    jc .no
.rptr:
%endif
    call hbm_ptrfix_set         ; 6. the pointer, beside it: the fixed
    mov word [cs:hbm_ptrfix + HBP_WAKE], hbm_wake   ; head's live fields, and
    mov [cs:hbm_ptrfix + HBP_WAKE + 2], cs          ; the wake address. The
    call hbm_nptr               ; file IS hbm_ptrfix - one object, written
    push cs                     ; from where it is described rather than
    pop es                      ; staged through a buffer (SPEC.md 87.3)
    mov bx, hbm_ptrfix
    xor dx, dx
    mov cx, HBP_SIZE
    call COLD_SEG:dwf_dskw_write
    jc .no
    pop ax                      ; (CF = 0 from the `jc`)
    ret
.no:
    mov si, hbm_s_failed        ; SI = the refusal, which hbm_hib toasts
.nos:
%ifdef HMA_DRV
    call hbm_hfree              ; ...and the pool's copy goes with it
%endif
    pop ax
    stc
    ret

hbm_hib:
    xor al, al                  ; HBP_DOS = 0: this IS the user leaving
    call hbm_wrimg
    jc hbm_hibfail
    mov si, hbm_s_off
; hbm_bye - SI = the sentence: text mode, the sentence, a key, and the reboot
;           path's tail. Never returns. hbm_hib's step 7, and hbm_wake's
;           refusal of an image whose programs did not read back (87.4.1)
hbm_bye:
    call hbm_kbdrain            ; 7. a key typed during the write is not the
    mov bp, vid_reboot
    call hbm_kd                   ;    answer; text mode, the sentence, a key
    call hbm_tty
    xor ah, ah
    int 0x16
    call COLD_SEG:drv_shutdown_x    ; ...and the reboot path's tail (20.10)
%ifndef NO_FDDPARK
    jmp KERNEL_SEG:sched_unhook     ; ...and the WHOLE of that tail, shared and
                                    ; not copied: unhook, SPEC.md 18.100's park
                                    ; and the int 19h at the end of it. The user
                                    ; is ASKED to switch off here, and one who
                                    ; does not gets an ordinary boot off a
                                    ; machine that has read floppies all
                                    ; session. NEVER RETURNS, which is why this
                                    ; is a jmp and there is nothing after it
%else
    mov bp, sched_unhook            ; ...and on this arm sched_unhook DOES
    call hbm_kd                     ; return, so it is a call through the
    int 0x19                        ; kernel's generic BP trampoline and the
    jmp $                           ; int 19h is ours to issue
%endif
hbm_hibfail:                        ; hbm_hib's: the write did not happen
    push si                     ; hbm_wrimg's refusal
    mov byte [cs:hbm_dosheld], 0
    dec byte [sch_lock]
    call hbm_unlock              
    call hbm_reload             ; the drivers come back
    call hbm_vbk                   
    mov byte [hb_mode], HB_M_ASK
    pop si
    call hbm_toast
    call hbm_winback
    mov sp, [hb_sp]
    ret

; hbm_winback - the window, if it is up, painted for the mode it is in now
; in:  nothing; no lock held
hbm_winback:
    push si
    call hbm_win
    jc .out
    call hbm_lock              
    call hbm_repaint
    call hbm_unlock              
.out:
    pop si
    ret

; hbm_kbdrain - empty the ROM's keyboard buffer (SPEC.md 87.4 step 7, 87.5)
; in:  nothing; out: nothing (all registers preserved)
hbm_kbdrain:
    push ax
.more:
    mov ah, 0x01
    int 0x16
    jz .out
    xor ah, ah
    int 0x16
    jmp short .more
.out:
    pop ax
    ret

; hbm_tty - print one of this image's strings through the ROM's teletype
; in:  SI = the string's offset here; text mode
hbm_tty:
    push ax
    push bx
    push si
.ch:
    mov al, [cs:si]
    or al, al
    jz .out
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    inc si
    jmp short .ch
.out:
    pop si
    pop bx
    pop ax
    ret

%ifdef HMA_DRV
; -----------------------------------------------------------------------------
; hbm_hsave - THE HMA'S POOL, INTO THE IMAGE (SPEC.md 87.4 step 3b)
; in:  the sweep done, the lock and [sch_lock] held; [cs:hbm_hseg] = 0
; out: CF = 0 and [cs:hbm_hseg] = the copy's MEM_K_HIB claim, or 0 when
;      there is nothing to carry; CF = 1 no room for the copy. HBP_PAD is the
;      table's bytes or 0. Clobbers AX, BX, CX, DX, SI, DI, BP, ES
;
; The image is conventional memory and nothing above 1MB is in it, so a
; driver the HMA holds - a kept class, HDD.DRV or NET.DRV - would come back
; as a row naming an empty segment. One claim, the table and then every
; live block in record order, and the image carries it at no cost: it is
; written with the rest of memory. hbm_wake's step 0b copies it back.
; -----------------------------------------------------------------------------
hbm_hsave:
    mov byte [cs:hbm_ptrfix + HBP_PAD], 0
    cmp word [api_coldseg], HMA_SEG
    jne .none                   ; `.cold` is not in the HMA: there is no pool
    mov al, ROMF_HTAB
    call far [rom_mfp]          ; DX:AX = the table, CX = its bytes
    jc .none
    push dx
    push ax
    mov es, dx
    mov si, ax
    mov di, cx                  ; DI = the copy's bytes: the table...
    push cx
.sum:
    cmp word [es:si], 0
    je .snext
    add di, [es:si+2]           ; ...and every live block (paragraphs, so
.snext:                         ; even, and the pool is under 64KB)
    add si, 6
    sub cx, 6
    ja .sum
    pop cx
    cmp di, cx
    je .empty                   ; no live block: the wake resets the pool
    lea ax, [di + 1023]
    push cx
    mov cl, 10
    shr ax, cl                  ; AX = KB
    pop cx
    mov bx, MEM_K_HIB
    mov bp, mem_claim_x
    call hbm_cb                 ; DX = its segment
    mov bx, dx
    pop ax
    pop dx
    jc .out                     ; CF = 1: no room for the copy
    mov [cs:hbm_hseg], bx
    mov [cs:hbm_ptrfix + HBP_PAD], cl
    xor bp, bp                  ; ...into the claim
    call hbm_hcopy
    jmp short .none
.empty:
    pop ax
    pop dx
.none:
    clc
.out:
    ret

; hbm_hcopy - the pool's table and its live blocks, between the tail and the
;             claim at BX (SPEC.md 87.4 step 3b, 87.6 step 0b)
; in:  DX:AX = the tail's table (ROMF_HTAB), CX = its bytes, BX = the claim;
;      BP = 0 into the claim, 1 back out of it
; out: nothing; clobbers AX, CX, DX, SI, DI, ES
hbm_hcopy:
    push ds
    mov si, ax                  ; DX:SI = the table, BX:0 its copy - first,
    xor di, di                  ; so the walk below reads the claim's records
    push cx                     ; either way round
    call hbm_hmove
    pop cx                      ; CX = where the copy's records stop
    mov dx, HMA_SEG             ; a block's bytes are FFFF:base
    xor ax, ax
.rec:
    push cx
    mov es, bx
    mov si, ax
    mov cx, [es:si+2]
    mov si, [es:si]
    or si, si                   ; base 0 = a free record
    jz .skip
    call hbm_hmove
.skip:
    pop cx
    add ax, 6
    cmp ax, cx
    jb .rec
    pop ds
    ret

; hbm_hmove - CX bytes (even) between DX:SI and BX:DI, BP saying which way;
;             DI is past them after. One block whole between two interrupts.
;             Clobbers CX, SI, DS, ES
hbm_hmove:
    pushf
    cli
    shr cx, 1
    or bp, bp
    jnz .back
    mov ds, dx
    mov es, bx
    rep movsw
    popf
    ret
.back:
    mov ds, bx
    mov es, dx
    xchg si, di
    rep movsw
    xchg si, di
    popf
    ret

; hbm_hfree - the copy's claim back, if there is one. Clobbers BX, DX, BP
hbm_hfree:
    xor dx, dx
    xchg dx, [cs:hbm_hseg]
    or dx, dx
    jz .out
    mov bx, MEM_K_HIB
    mov bp, mem_free_x
    call hbm_cb
.out:
    ret
%endif

; -----------------------------------------------------------------------------
; hbm_sweep / hbm_reload - the drivers that own hardware (SPEC.md 87.4 step 2)
; in:  nothing; UI task, NO lock held (an unload waits on its worker)
; out: nothing (all registers preserved); [hb_drvmask] says which rows
;
; ...and hbm_drvsusp below them is OSAPI_DRV_SUSPEND (SPEC.md 51.11): the same
; sweep with a third skip and a question asked on the way past, noting its
; rows in [hb_susp], and the same reload. ONE loop each, shared - the slot's
; first body was a third copy of this pair, resident.
; -----------------------------------------------------------------------------
; hbm_sweep - unload every driver that owns a chip, and note the rows
; in:  SI = the word to note them in (cleared first); DL = one more DRVC_* to
;      leave standing, or 0; [cs:hbm_spm] = classes to let go of ANYWAY, bit
;      1<<class; ES:DI = a DQ_SIZE-record buffer each driver is asked
;      DRVV_HWINFO into on the way past, or DI = 0 for no question
; out: CX = records written
; clobbers: AX, BX, DX, DI, BP
;
; THE SKIP LIST: DRVC_DISK is memory and the ROM's int 13h, DRVC_FILE is the
; RAM disk, and both are things a DOS program WANTS rather than fights -
; neither owns a chip anyone is about to take. The caller's third is 96.23.6.
; DRVC_EMS is a board's memory with packages holding pages of it (107.6).
; DRVC_CD is a read-only volume a DOS program may be reading (119.1).
;
; ...AND THE CALLER MAY HAVE ASKED FOR ONE OF THEM ANYWAY (SPEC.md 51.11.4).
; [cs:hbm_spm] is the slot's BL, bit 1<<class: a set bit does not ADD a class
; to the sweep, it takes one OFF the skip list, so the mask can only reach the
; three rows this loop was already deciding about and a 0 is exactly today.
; The test hangs off the skip itself, so a row that was going to be taken pays
; nothing for it. It is MODULE data, so the whole feature is resident-free.
hbm_sweep:
    mov word [si], 0
    CSDO mov byte [CSEG hbm_swn], 0
    xor cx, cx
.row:
    mov al, cl
    call COLD_SEG:drvf_drv_row  ; BX = the row
    cmp word [bx+DRVR_SEG], 0
    je .next
%ifdef VID_EXT
    cmp bx, vesa_row            ; THE DISPLAY'S RENDERER STAYS (SPEC.md 120.4):
    je .next                    ; a suspend that took it would leave every
                                ; drawing body calling nothing. A hibernate
                                ; never reaches here with it - hb_ok refuses
                                ; while VID_VESA is the kind
%endif
    mov al, [bx+DRVR_CLASS]
    cmp al, DRVC_DISK
    je .asked                   ; memory and the ROM's int 13h: it stays...
    cmp al, DRVC_FILE
    je .asked                   ; the cable: asked, and it stays...
    cmp al, DRVC_FILE2
    je .asked                   ; the RAM disk: memory, and it stays - and
                                ; 1 << 8 is 0 in CH, so no caller can name it
    cmp al, DRVC_CD
    je .asked                   ; the CD-ROM: a volume DOS may be reading, and
                                ; no IRQ or DMA a program could want (119.1)
    cmp al, DRVC_EMS
    je .asked                   ; the EMS board: memory with holders, and a
                                ; fresh attach would sign every page and remap
                                ; the quarters under them (SPEC.md 107.6)
    cmp al, dl
    jne .take                   ; ...and the caller's own
.asked:                         ; ...unless this call named it (51.11.4).
                                ; A driver in the HMA follows its class like
                                ; a heap one: hbm_hsave carries the pool in
                                ; the image (SPEC.md 87.4 step 3b)
    push cx                     ; CL is the ROW and the shift wants the CLASS,
    mov cl, al                  ; so the index is banked for four instructions
    mov ch, 1
    shl ch, cl
    test [cs:hbm_spm], ch
    pop cx                      ; ...which does not touch the ZF `test` set
    jz .next
.take:
    mov ax, 1
    shl ax, cl
    or [si], ax
    or di, di
    jz .unload
    mov al, [bx+DRVR_CLASS]     ; ASK IT WHERE ITS HARDWARE IS, on the way
    mov [es:di+DQ_CLASS], al    ; past: it is about to stop existing, and it
    mov bp, [bx+DRVR_ENT]       ; is the last thing that knows. The class is
    push cx                     ; staged before the ask so a CF=1 costs nothing
    push dx                     ; to undo - the record is not counted and DI
    push di                     ; does not move - and the row, the skip and the
    mov al, DRVV_HWINFO         ; buffer are banked because the answer is the
    call COLD_SEG:drvf_drv_call ; DRIVER's code: AX/BX/CX = its three words
    pop di
    pop dx
    jc .noinfo
    mov [es:di+DQ_A], ax
    mov [es:di+DQ_B], bx
    mov [es:di+DQ_C], cx
    mov byte [es:di+DQ_FLAGS], 1
    add di, DQ_SIZE
    CSDO inc byte [CSEG hbm_swn]
.noinfo:
    pop cx
.unload:
    mov al, cl
    call COLD_SEG:drvf_drv_unload
.next:
    inc cx
    cmp cx, DRV_MAX
    jb .row
    mov cl, [cs:hbm_swn]
    xor ch, ch
    ret

hbm_reload:
    push dx
    mov dx, [hb_drvmask]
    call hbm_reload_m
    pop dx
    ret

; hbm_reload_m - load the rows in DX again, in row order
; in:  DX = the rows; out: nothing; clobbers DX
hbm_reload_m:
    push ax
    push bx
    push cx
    xor cx, cx
.row:
    shr dx, 1
    jnc .next
    mov al, cl
    call COLD_SEG:drvf_drv_row  ; BX = the row hdl_load takes
    call hdl_load               ; failures show on the Drivers page, as at boot
.next:
    inc cx
    cmp cx, DRV_MAX
    jb .row
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_drvsusp - OSAPI_DRV_SUSPEND's suspend and resume (SPEC.md 51.11), reached
;               through hbm_perform with the slot's AL + HB_P_RESUME
; in:  AL = HB_P_SUSPEND, ES:DI = the caller's DQ_SIZE-record buffer or DI = 0,
;      BL = the slot's own class mask (SPEC.md 51.11.4); or AL = HB_P_RESUME
; out: CX = records written (0 on a resume); CF = 0 - the sweep cannot refuse
; -----------------------------------------------------------------------------
hbm_drvsusp:
    CSDO mov [CSEG hbm_spm], bl        ; THE SLOT'S OWN BL, still in the register the
                                ; caller put it in (SPEC.md 51.11.4) - banked
                                ; HERE, in the module, on both verbs. Writing
                                ; it on the resume too is what stops one
                                ; call's mask outliving it, which is what the
                                ; kernel's byte used to be for
    cmp al, HB_P_SUSPEND
    je .suspend
    mov dx, [hb_susp]           ; RESUME: the rows a suspend noted and only
    mov word [hb_susp], 0       ; those, so a caller with nothing suspended
    call hbm_reload_m           ; reloads nothing
    xor cx, cx                  ; ...and CF is clear
    ret
.suspend:
    mov si, hb_susp
    mov dl, DRVC_NET            ; AND THE WIRE STAYS (SPEC.md 96.23.6): the
                                ; DOS box's packet driver is the only route a
                                ; DOS program here has to the card, and it is
                                ; built on the driver staying mounted - so it
                                ; is the sweep's third skip and not a fourth
                                ; rule, which is what puts it under the same
                                ; [cs:hbm_spm] bit as the other two: a caller
                                ; that does not want the wire either says so
                                ; (SPEC.md 51.11.4)
    call hbm_sweep              ; CX = the records
    clc
    ret

; -----------------------------------------------------------------------------
; hbm_ask - the boot-time question (SPEC.md 87.5): validate the pointer,
;           find the image, and open the window in HB_M_RESUME
; in:  [hb_vol] = the volume the probe found the pointer on; no lock
; -----------------------------------------------------------------------------
hbm_ask:
    call hbm_enter
    jc .gone
    call hbm_nptr
    push cs
    pop es
    mov bx, hbm_ptrbuf
    xor dx, dx
    mov cx, HBP_SIZE
    call COLD_SEG:dwf_dskw_read ; DX:AX = bytes
    jc .gone
    cmp ax, HBP_SIZE
    jne .stale
    call hbm_ptrfix_set         ; ONE description of the fixed head, written
    push ds                     ; by hbm_wrimg and compared here (87.3)
    push cs
    pop ds
    push cs
    pop es
    mov si, hbm_ptrfix
    mov di, hbm_ptrbuf
    mov cx, HBP_WAKE / 2
    repe cmpsw
    pop ds                      ; ...and a pop does not touch cmpsw's ZF
    jne .stale
%ifdef HMA_DRV
    mov cl, [cs:hbm_ptrbuf + HBP_PAD]   ; THE HMA POOL THE IMAGE CARRIES
    or cl, cl                   ; (SPEC.md 87.3): its table goes back over
    jz .htab                    ; this boot's tail at the wake, where nothing
    mov al, ROMF_HTAB           ; can refuse - so the tail is asked HERE,
    call far [rom_mfp]          ; before the teardown. The fixed head has
    jc .stale                   ; already said `.cold` is in the HMA (FF)
    cmp cl, [cs:hbm_ptrbuf + HBP_PAD]
    jne .stale
.htab:
%endif
%ifdef KERN_MEGA
    mov al, [cs:hbm_ptrbuf + HBP_XFLG]  ; THE MODE IS THE IMAGE'S: a picture
    test byte [cpu_feat], CPU_F_PM  ; of protected mode needs this boot to
    jz .rmode                   ; have switched, and one of real mode needs
    xor al, 1                   ; it not to have (KERN-MEGA-DOS-PLAN 8.3
.rmode:                         ; step 1)
    or al, al
    jz .mode
    mov si, hbm_s_mode
    jmp .drop
.mode:
%endif
    call hbm_findimg            ; ...which is mounted: the pointer came from it
    jc .lost                    ; DX:CX = the image's size on the disk
    mov bx, dx                  ; ...banked, because hbm_imgsize answers in
%ifndef KERN_MEGA
    call hbm_imgsize            ; DX:AX and is documented to preserve CX
%else
    call hbm_xisz               ; (kern_mega: the image the pointer describes)
%endif
    cmp cx, ax
    jne .lost
    cmp bx, dx
    jne .lost
    mov al, [hb_vol]
    call hbm_geom
    jc .nobios
    call hbm_vbk                   
    mov byte [hb_mode], HB_M_RESUME
    ; --- ...AND WHETHER TO ASK AT ALL (docs/plans/KERN-DOS-PLAN.md 8) -------
    ; An image written for a DOS handoff is not the user leaving the machine,
    ; so there is no question to put: the session is coming back and the only
    ; thing the user did was run a program. Everything above still runs - the
    ; fixed head, the image's size, the geometry - because a stale or
    ; unreadable pointer is exactly as fatal either way.
    cmp byte [cs:hbm_ptrbuf + HBP_DOS], 0
    je .askit
    call hbm_dosbda             ; the exit code, out of the BDA
    mov al, UI_RBQ_RESUME
    call hbm_post               ; ...unless a restart already stands
    jmp short .out
.askit:
    mov byte [hb_down], 0
    call hbm_kbdrain            ; a key held at the hibernate's own prompt
                                ; repeats through int 19h into this boot, and
                                ; ui_task's next step hands it to the front
                                ; window - where Esc is Discard
    mov al, KIND_HIBER
    call KERNEL_SEG:cw_app_launch
    jmp short .out
.stale:
    mov si, hbm_s_stale
    jmp short .drop
.lost:
    mov si, hbm_s_lost
    jmp short .drop
.nobios:
    mov si, hbm_s_nobios
.drop:
    call hbm_toast
    call hbm_forget             ; a pointer nothing can act on
.gone:
    call hbm_vbk                   
    mov byte [hb_mode], HB_M_GONE   ; ...and the image goes back too
.out:
    mov sp, [hb_sp]
    ret

; -----------------------------------------------------------------------------
; hbm_dosbda - the DOS program's exit code, out of the BDA (SPEC.md 96.41)
; out: [hb_doscode] and [hb_dosakb]; the mailbox is CLEARED. Clobbers AX,
;      DI and ES, which its one caller (hbm_ask's DOS arm) holds none of
;
; `kd_leave` wrote it at `0040:00F0` before its `int 19h` and it survived the
; ROM's bootstrap and our own boot (docs/plans/KERN-DOS-PLAN.md 8.2). Read
; ONCE and cleared, because the next thing that reads it would otherwise find
; a code from a session two boots ago and attach it to the wrong program.
;
; READ BY hbm_ask AT THE BOOT, into two resident cells hbm_res stages for the
; wake. The size pass had hbm_res read the BDA itself and keep no cells, which
; is a word cheaper - and it does not survive SPEC.md 96.41.1, where the ARENA
; travels beside the code and the live resume and the reboot route have to
; converge on the same pair. Two cells, one reader, one writer.
; -----------------------------------------------------------------------------
hbm_dosbda:
    xor ax, ax
    mov [hb_dosakb], ax
%ifdef KERN_MEGA
    mov ax, MPK_RESETCODE       ; kern_mega: a DOS image with no code in the
                                ; BDA is 14.43's, from a machine RESET during
                                ; the program (kern_dos goes home by the park
                                ; and writes no mailbox) - W7's code, so the
                                ; box says so rather than nothing at all
%else
    dec ax
%endif
    mov [hb_doscode], ax        ; KDH_NOCODE, asserted at hb_probe_x
    mov ax, KDB_SEG
    mov es, ax
    mov di, KDB_OFF
    cmp word [es:di], KDB_SIG & 0xFFFF
    jne .out
    cmp word [es:di+2], KDB_SIG >> 16
    jne .out
    mov ax, [es:di+KDB_CODE]
    mov [hb_doscode], ax
    mov ax, [es:di+KDB_AKB]     ; ...and the arena beside it (SPEC.md 96.41.1)
    mov [hb_dosakb], ax
    xor ax, ax
    mov [es:di], ax             ; spent: the magic goes, not the code, so a
    mov [es:di+2], ax           ; second reader gets nothing rather than this
.out:
    ret

; hbm_ptrfix_set - fill the two fields of the fixed head that are the running
;                  machine's, so hbm_wrimg and hbm_ask read ONE object.
;                  Clobbers AX; the pad byte after [vid_kind] is the image's
;                  own 0 and nothing writes it
hbm_ptrfix_set:
    mov ax, [mem_top]
    mov [cs:hbm_ptrfix + HBP_MEMTOP], ax
    mov al, [vid_kind]
    mov [cs:hbm_ptrfix + HBP_VID], al
%ifdef ROM_ADOPT
    ; ...and WHERE `.cold` RUNS, in the pad byte the head always compared as
    ; 0 (docs/plans/ROM-PLAN.md 3.4.5): the HIGH BYTE of [api_coldseg], which
    ; is the RAM rung's on a machine that kept it, F4 when a ROM adopted the
    ; kernel and FF when XMEM.DRV put it in the HMA (docs/plans/HMA-PLAN.md).
    ; An image hibernated with `.cold` in either has every far reference
    ; pointing there and its cold rung is heap, so resumed where `.cold` is
    ; anywhere else it would run into nothing; three places, so a byte that
    ; only said "not in RAM" could not refuse the pair that matters. Same
    ; build is already the head's BUILD_NUM, and a ROM or an HMA image this
    ; boot installed is one that proved itself this kernel's - so an HMA
    ; image resumes onto the HMA its own build filled, byte for byte the same.
    mov al, [api_coldseg + 1]
    mov [cs:hbm_ptrfix + HBP_VID + 1], al
%endif
    ret

%ifdef KERN_MEGA
; hbm_xisz - out: DX:AX = the image's bytes as the pointer in hbm_ptrbuf
;            describes it: hbm_imgsize's, or whole chunks and the section
hbm_xisz:
    cmp byte [cs:hbm_ptrbuf + HBP_XFLG], 0
    je hbm_imgsize
    call hbm_xconv
    add ax, [cs:hbm_ptrbuf + HBP_XLEN]
    adc dx, [cs:hbm_ptrbuf + HBP_XLEN + 2]
    ret
%endif
; hbm_imgsize - out: DX:AX = [mem_top] * 16, the bytes an image must have
hbm_imgsize:
    push cx
    mov ax, [mem_top]
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    pop cx
    ret

hbm_res_f:                      ; hbm_res's two first refusals, whose .fail
    jmp hbm_res.fail            ; is past short reach: one trampoline behind
                                ; the `ret` above for two 5-byte jccs

; -----------------------------------------------------------------------------
; hbm_res - put the image back (SPEC.md 87.5): the extents, then the stub
; in:  the question was answered Resume; no lock held
; -----------------------------------------------------------------------------
hbm_res:
    call hbm_enter              ; where the user stands, for a refusal
    jc hbm_res_f
    call hbm_findimg            ; BX = its first cluster
    jc hbm_res_f
%ifdef KERN_MEGA
    test byte [cpu_feat], CPU_F_PM
    jz .kbres
    jmp hbm_xres                ; protected mode: through a block above 1MB
.kbres:
%endif
    push bx
    call hbm_xcap               ; 2. sized for THIS volume, not the worst one
    mov bx, MEM_K_HIB
    mov bp, mem_claim_x
    call hbm_cb          ; DX = its segment
    pop ax
    jc .fail
    mov [hb_xseg], dx
    mov bx, [mem_top]           ; THE IMAGE'S CHAIN AS SECTOR RUNS (SPEC.md
    mov cl, 5                   ; 87.5 step 2) is hbm_extat with nothing to
    shr bx, cl                  ; skip and the whole image to take: paragraphs
    xor cx, cx                  ; / 32 sectors. It had a copy of its own, the
    call hbm_extat              ; same walk less the skip. AX = first cluster
    jc .failx
    ; 3. the panel's settings, the fresh boot's drivers go, the lock is taken,
    ;    text mode - ui_cmd_reboot's order (SPEC.md 20.10, 31.8) - and 4. the
    ;    staging area: the stub, the transport words and the extents, which
    ;    is the half hbm_dosrun's handoff shares (hbm_down)
    mov byte [cs:hbm_dosheld], 0    ; nothing of the lock is ours yet
    mov si, hbs_stub
    mov cx, hbs_stub_end - hbs_stub
    call hbm_down               ; ES = the text framebuffer, DI = HS_WAKE
%if HS_WAKE != HS_UNIT + 8
  %error "the resume staging: hbm_down leaves DI at HS_UNIT + 8, which is to be HS_WAKE"
%endif
    mov ax, [cs:hbm_ptrbuf + HBP_WAKE]  ; ...and the wake address out of the
    stosw                               ; pointer
    mov ax, [cs:hbm_ptrbuf + HBP_WAKE + 2]
    stosw
    mov si, clk_sec             ; the fresh boot's clock (SPEC.md 87.6): ten
    mov di, HS_CLK              ; bytes, clk_sec..clk_year, and NOT the two
    mov cx, 5                   ; display toggles after them, which are the
    rep movsw                   ; image's to keep
    mov ax, [hb_doscode]        ; the DOS program's exit code, if this
    mov [es:HS_DOSCODE], ax     ; resume is a handoff coming home. NO STUB CODE
                                ; AT ALL: hbm_wake reads this segment anyway
%if HS_DOSAKB != HS_KSEG + 2 || HS_TICK0 != HS_DOSAKB + 2
  %error "the resume staging stores HS_KSEG..HS_TICK0 as one run of four words"
%endif
    mov di, HS_KSEG             ; ...and four words in a row from here:
    mov ax, ds                  ; the segment the stub used to spell as a
    stosw                       ; constant (kernel/hbstage.inc) - it means
                                ; something else in the other host, so it is
                                ; staged by whoever stages, and DS is it here;
    mov ax, [hb_dosakb]         ; the ARENA beside the code, so the reboot
    stosw                       ; route and the live one converge where the
                                ; CODE does (SPEC.md 96.41.1);
    mov ax, 0xFFFF              ; and HS_TICK0 = -1: **THE CLOCK ABOVE IS
    stosw                       ; ALREADY CURRENT** (SPEC.md 96.49.5) - a
    stosw                       ; fresh boot has just run the ladder, so there
                                ; is no elapsed time to add and adding one
                                ; would count the DOS session twice. The live
                                ; route stages the handover's own tick instead
    ; 5. the ROM's int 08h back, the kernel's IRQs masked, and go ----------
    jmp hbm_handover            ; **A JUMP AND NOT A CALL**: what ends that
                                ; routine is a `retf` that is a FAR JUMP INTO
                                ; THE STUB spelled as a return (SPEC.md 87.5),
                                ; so os88ovlchk's 2.6.1 rule reads a near call
                                ; to it as a far entry called wrongly - and it
                                ; is right that nothing returns here

.failx:
    mov dx, [hb_xseg]
    mov bx, MEM_K_HIB
    mov bp, mem_free_x
    call hbm_cb         
    mov word [hb_xseg], 0
.fail:
    call hbm_vbk                   
    mov byte [hb_mode], HB_M_RESUME
    mov si, hbm_s_lost
    call hbm_toast
    call hbm_winback
    mov sp, [hb_sp]
    ret

; =============================================================================
; THE DOS HANDOFF (SPEC.md 96.40, docs/plans/KERN-DOS-PLAN.md §7)
; =============================================================================
; The DOS box has asked for the WHOLE machine. What follows is §87.5's resume
; with a different payload and a different stub - the same extent walk, the
; same staging area, the same teardown in the same order - and it is here
; rather than in a module of its own for exactly that reason: one walker, one
; staging layout, one place where the kernel stops being the kernel.
;
; WHAT IS DIFFERENT is the thing being read. A resume reads an IMAGE that is
; the whole of memory; this reads a PART of a package - a byte range of
; DOS.O88 - and the range is compressed, so the stub expands it. The walk
; therefore takes a SKIP and a COUNT where the resume's takes neither.
;
; It does not come back. The machine it hands over has no kernel in it.
; -----------------------------------------------------------------------------
hbm_dosrun:
    CSDO mov byte [CSEG hbm_dosheld], 0    ; module BSS is not zeroed: only hbm_wrimg
                                    ; below may say the locks are held
    ; --- 1. the record, where the poster left it (SPEC.md 96.40) -----------
    ; It is copied into the MODULE's own data and every read of it carries a
    ; `cs:`, because DS here is KERNEL_SEG - the module runs with the kernel's
    ; data segment and its own code segment, which is what the rest of this
    ; file assumes and what makes [hb_dosseg] readable without an override.
    push ds
    mov si, [hb_dosoff]         ; **THE OFFSET BEFORE THE SEGMENT.** Both words
                                ; are KERNEL_SEG's, and the line below is what
                                ; stops DS being it - so loading DS first makes
                                ; the second read come out of the POSTER's own
                                ; image at that offset. It assembles, it runs,
                                ; and it copies 543 bytes of somebody else's
                                ; bss over the record
    mov ds, [hb_dosseg]
    PUSH_CSDATA                 ; ES = OUR DATA: `push cs` everywhere but
    pop es                      ; kern_mega, whose CS is a read-only alias -
                                ; there the copy went through mega_gpw's
                                ; fault-and-restart, which a real 286 runs
                                ; differently from the 386 every emulator
                                ; here is (KERN-MEGA-PLAN 10.3 item 54.2)
    mov di, hbm_dosrec
    mov cx, KDH_SIZE
    rep movsb
    pop ds
    cmp word [cs:hbm_dosrec + KDH_MAGIC], KDH_SIG & 0xFFFF
    jne .b1
    cmp word [cs:hbm_dosrec + KDH_MAGIC + 2], KDH_SIG >> 16
    jne .b1
%ifdef OS88_IMG
    ; --- ...AND NO DISK IMAGE MOUNTED (SPEC.md 18.101.3) -------------------
    ; A mounted image's file is read-only only VIRTUALLY - dskw_imgro, in a
    ; FAT layer kern_dos does not assemble - so a DOS program given the whole
    ; machine could delete or replace it, and the session coming home would
    ; write the drive into clusters another file now owns. Refused here,
    ; with nothing taken yet, by the row walk dsk_img_next makes
    mov bx, dsk_vtab
.imgrow:
    cmp byte [bx+DV_KIND], DVK_DRV
    jne .imgnx
    cmp byte [bx+DV_CLASS], DRVC_IMG
    je .bimg
.imgnx:
    add bx, DV_SIZE
    cmp bx, dsk_vtab + DVOL_MAX*DV_SIZE
    jb .imgrow
%endif

    ; --- 3c. THE VOLUME TABLE, WHICH WAS HARD-CODED OVER THERE (96.46) ------
    ; `kern_dos` assumed A: and B: as BIOS units 0 and 1 and every other row
    ; free, and os8088 BUILDS its table at boot. SPEC.md 18.7.1 pins the boot
    ; partition at ROW 2, which that initialiser has FREE - so a machine with
    ; a fixed disk hands over a volume index kern_dos reads as no volume at
    ; all; and row 1 stays live there on a machine whose second floppy the
    ; kernel retired. The box hands `dos_vol` over as an INDEX into that
    ; table, so an index resolved against a different one is a program
    ; reading a drive that is not its own.
    ;
    ; **BEFORE STEP 4, AND NOT AT STEP 5 WHERE IT USED TO BE**: step 4 calls
    ; `drv_shutdown_x`, and `drv_release` drops a driver's volumes on the way
    ; out - so a gather after it sees a table with every DVK_DRV row already
    ; DVK_FREE and cannot tell that from a machine that never had one. It
    ; writes the RECORD's copy of the launch block and step 5's `rep movsw`
    ; carries the lot, which is `KDLF_HIBER`'s own arrangement one field over.
    ;
    ; THE TWO TABLES HAVE DIFFERENT STRIDES: `dsk_vtab` is DV_SIZE = 16 and
    ; `dsk_vbase` is a 4-byte side table (SPEC.md 52.10.3). Keeping ONE
    ; running pointer for both is the arithmetic that gets written wrong, so
    ; each has its own (below), and SI is the row number the limits test.
    ;
    ; **AND BEFORE STEP 2, WHICH IS WHAT FREES hbm_geom**: the loop asks
    ; `hbm_geom` of every driver row and that writes `hb_*`, which step 3 is
    ; about to fill with DOS.O88's OWN volume for step 5 to stage. Gathering
    ; first lets step 3 have the last word, where gathering after it had to
    ; bank and restore all five words around the loop. Nothing in between
    ; touches the table or the launch block: step 2 reads the record's name
    ; and folder and step 3 its offsets, all outside KDH_LB.
    ; ...AND THE FIXED DISK'S GEOMETRY FIRST, because the loop may better it.
    ; It is the half a row alone does not buy: `dsk_boot_from_x` asks
    ; `int 13h AH=08h` once at boot and banks it, and that routine is in the
    ; BOOT OVERLAY - so kern_dos cannot ask the question, and the mount that
    ; needs the answer is the read that FETCHES the BPB, so it cannot come
    ; from there either. Left at zero the mount keeps the floppy fallback of 9
    ; sectors and 2 heads and fails honestly, which is what a hard disk did
    ; over there before this.
    ;
    ; **AND IT IS ZERO ON A MACHINE THAT BOOTED OFF A FLOPPY**, which is the
    ; half this pair was written blind to: `dsk_boot_from_x` fills those two on
    ; its `.hard` arm ALONE, so a machine with a floppy in A: and a fixed disk
    ; on a driver banks nothing at all and every read of that disk over there
    ; is aimed with 9 sectors and 2 heads.
    mov ax, [dsk_bootspt]
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_HDSPT], ax
    mov ax, [dsk_boothds]
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_HDHDS], ax
    ; --- AND WHETHER A RUN MAY CROSS A HEAD (SPEC.md 96.44.14) ------------
    ; The LOADER settled that with a canary over its own transfer and nothing
    ; over there can ask it again - kern_dos has no stage 2 and no canary - so
    ; without this the cautious answer is the only honest one it has, and
    ; 18.91.1's cylinder run is lost on every machine that earned it. Carried
    ; as a BOOLEAN and not as the kernel's word: what the far side needs is
    ; the verdict, and a word whose meaning is "non-zero" invites the next
    ; reader to put arithmetic on it.
    xor ax, ax
    cmp [boot_cylrun], ax
    je .nocyl
    inc ax
.nocyl:
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_CYLRUN], al
    ; A ROW IS FOUR REGISTERS and every arm below only says which: AL the
    ; kind, AH the unit, DX:CX the partition base - stored by one tail. BX
    ; walks `dsk_vtab` and BP `dsk_vbase`, each at its own stride.
    xor si, si
    mov bx, dsk_vtab
    mov bp, dsk_vbase
    mov di, hbm_dosrec + KDH_LB + KDL_VTAB
.vrow:
    xor cx, cx
    xor dx, dx
    mov ax, DVK_FREE            ; AL = DVK_FREE, AH = unit 0, base 0
    cmp si, DVOL_MAX            ; this kernel may have FEWER rows than the
    jae .vput                   ; block has room for - kern_small's four -
                                ; and the tail is FREE rather than absent
    cmp byte [bx+DV_KIND], DVK_BIOS
    je .vbios
    cmp byte [bx+DV_KIND], DVK_DRV
    jne .vput                   ; DVK_FILE has no sectors at all (SPEC.md 62.9)
    ; --- A DRIVER VOLUME THE ROM CAN REACH (SPEC.md 96.46.1) ---------------
    ; The rule USED to be *"a DVK_DRV row cannot come at all - there is no
    ; driver layer on the other side"*, and that is true of the DRIVER and not
    ; of the DISK. `DSV_GEOM` sub-function 2 answers CF=1 for a volume
    ; `int 13h` cannot reach, and a unit, a base and a geometry for one it
    ; can - so a driver volume that ANSWERS is a BIOS volume as far as
    ; kern_dos is concerned, and it is carried as one. SPEC.md 87.5 step 1's
    ; stub has rested on exactly that fact since the day it was written; this
    ; is the same question asked of the same routine.
    ;
    ; WHAT IT FIXES is the live resume on a machine that booted off a FLOPPY.
    ; HIBERNAT.IMG goes to the first fixed volume there is (`hb_pick`), which
    ; on such a machine is driver-backed - and `kd_resume` mounts it BY INDEX
    ; against this table, whose row said DVK_FREE. It refused, `kd_leave` fell
    ; back to `int 19h`, and the session came back the long way: a whole POST,
    ; a whole boot and a restore at the desktop.
    mov ax, si                  ; AL = the volume index
    call hbm_geom               ; hb_unit / hb_spt / hb_heads / hb_base; it
    mov ax, DVK_FREE            ; preserves every register, and a `mov` keeps
    jc .vput                    ; its CF: not reachable by int 13h is FREE -
                                ; KIND 0xFF, where it used to store the ROW
                                ; NUMBER, which kern_dos copies into DV_KIND
                                ; verbatim (row 2 reads there as DVK_FILE)
    mov ah, [hb_unit]
    mov cx, [hb_base]
    mov dx, [hb_base + 2]
    cmp word [cs:hbm_dosrec + KDH_LB + KDL_HDSPT], 0
    jne .vkind                  ; a booted-from fixed disk's own stands: there
    push ax                     ; is ONE pair for the whole table, and the
    mov ax, [hb_spt]            ; volume the machine booted from is the one it
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_HDSPT], ax    ; is most about
    mov ax, [hb_heads]
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_HDHDS], ax
    pop ax
    jmp short .vkind
.vbios:
    mov ah, [bx+DV_UNIT]
    mov cx, [ds:bp]             ; ...and the partition base, 0 on a floppy
    mov dx, [ds:bp+2]
.vkind:
    mov al, DVK_BIOS
.vput:
    CSDO mov [CSEG di], ax             ; kind, unit
    CSDO mov [CSEG di+2], cx           ; ...base
    CSDO mov [CSEG di+4], dx
    add bx, DV_SIZE
    add bp, 4
    add di, KDL_VROW
    inc si
    cmp si, KDL_VMAX
    jb .vrow
    CSDO mov byte [CSEG hbm_dosrec + KDH_LB + KDL_NVOL], KDL_VMAX
    mov al, [dsk_bootvol]
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_BOOTV], al

    ; --- 2. stand where DOS.O88 is, and find it -----------------------------
    call hbm_vbn                        ; hbm_enter's first half: bank where
    mov ax, [cs:hbm_dosrec + KDH_DIR]   ; the user stands, then stand where
    mov dl, [cs:hbm_dosrec + KDH_VOL]   ; DOS.O88 is - a FOLDER cluster, not
    mov bp, dsk_chdir_x                 ; hbm_root's zero, because a package
    call hbm_cb                         ; lives in APPS/ and not in the root
    jc .b2
    ; **THE NAME HAS TO BE IN KERNEL_SEG**, which is not what ES says: the
    ; compare underneath is `dsk_streq`, and that reads DS:SI against DS:DI
    ; (kernel/disk.inc) - so a name in the module's own image is read at that
    ; offset in the KERNEL's segment and matches nothing. `api_name` is the
    ; kernel's own 13-byte stage for exactly this question, it is exactly
    ; KDH_NAME's length, and it is free here: it is what the N cells copy a
    ; package's name into, and nothing is inside a package call at ui_task's
    ; step 0. Zero resident bytes for the buffer, thirteen for the copy.
    mov si, hbm_dosrec + KDH_NAME
    mov di, api_name
    mov cx, 13
.nm:
    mov al, [cs:si]             ; ...and BYTE BY BYTE, not `movsb`: a module's
    mov [di], al                ; own data is CS-relative (SPEC.md 2.8.6) and
    inc si                      ; a repeated string op with a segment override
    inc di                      ; loses the prefix to an interrupt on an 8086
    loop .nm
    mov si, api_name
    mov bp, dskw_stat_x         ; ...and BY NAME off the directory (SPEC.md
    call hbm_cb                 ; 18.4), not out of the global listing: one
    jc .b3                      ; far call where find-then-stage was two, and
%ifdef KERN_MEGA
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_O88CL], bx    ; ...and WHERE DOS.O88 IS,
    mov al, [cs:hbm_dosrec + KDH_VOL]               ; for the HMA kern_dos to
    CSDO mov [CSEG hbm_dosrec + KDH_LB + KDL_O88V], al     ; read part 3 off it on a
                                        ; refused EXEPACK switch (96.40.9.1).
                                        ; CSDO, as step 3c's own writes are:
                                        ; each was a mega_gpw fixup until
                                        ; KERN-MEGA-PLAN 14.46
%endif
    push bx                     ; BX is the entry's first cluster - NOT the
                                ; FAT record's own 26 (SPEC.md 96.37.1 item 4).
                                ; ON THE STACK until step 3's walk: every
                                ; refusal between resets SP from [hb_sp]

    ; --- 3. the transport facts, and the extents ----------------------------
    mov al, [cs:hbm_dosrec + KDH_VOL]
    call hbm_geomd              ; hb_unit / hb_spt / hb_heads / hb_base
    jc .b4
    call hbm_xcap               ; a claim big enough for the worst case
    mov bx, MEM_K_HIB
    mov bp, mem_claim_x
    call hbm_cb                 ; DX = its segment
    jc .b5
    mov [hb_xseg], dx
    ; **ONE RUN COVERS BOTH PARTS** (SPEC.md 96.44.5.4). The core is part 1 and
    ; `kern_dos` is part 2; `os88pkg.py` lays parts out in TABLE ORDER, each
    ; 512-aligned, so the core's packed bytes are immediately followed by
    ; `kern_dos`'s and the extent list wanted is one range from KDH_COFF to the
    ; end of KDH_PLEN. Nothing here knows that adjacency as a fact - it takes
    ; the difference of the two offsets the box posted, so a layout that ever
    ; put something between them would read that something too and still hand
    ; the stub the right two streams.
    mov ax, [cs:hbm_dosrec + KDH_PLEN]  ; the PACKED bytes, as sectors: what
    add ax, 511                         ; the stub reads is the stream and not
    mov cl, 9                           ; what it expands to
    shr ax, cl
    mov cx, [cs:hbm_dosrec + KDH_COFF]  ; CX = sectors to skip: the CORE's
    mov bx, [cs:hbm_dosrec + KDH_POFF]  ; start, which is the lower of the two
    sub bx, cx                          ; BX = the gap to kern_dos's stream...
    add bx, ax                          ; ...plus kern_dos's own sectors
    pop ax                      ; DOS.O88's first cluster, from step 2
    call hbm_extat
    jc .b6

    ; --- 3b. SOMEWHERE TO COME BACK TO (docs/plans/KERN-DOS-PLAN.md 8) ------
    ; Here and not earlier: everything above can still refuse, and an image
    ; written for a handoff that then refuses is a hibernation the user did not
    ; ask for. Here and not later: step 4 takes the screen and shuts the
    ; drivers down, and the write wants the machine it is a picture OF.
    ;
    ; **THE POST IS STILL STANDING WHEN THE PICTURE IS TAKEN**, deliberately:
    ; [hb_dosseg]/[hb_dosoff] are in the image, so the restored kernel knows
    ; whose record to put the exit code in. `hbm_wake` is what spends it over
    ; there; the zero below spends it here.
    ;
    ; A FAILURE IS NOT FATAL. What the user asked for is to run the program
    ; with the whole machine, and that still happens - it is the coming back
    ; that is lost, and `KDLF_HIBER` staying clear is what says so all the way
    ; down to `kd_leave`.
%ifdef KERN_MEGA
    ; --- kern_mega: THE PARK in place of the image (docs/plans/
    ; KERN-MEGA-DOS-PLAN.md 2.3, kernel/megados.inc). hbm_wrimg's own first
    ; half - the hardware drivers swept and noted, the lock, [sch_lock] - and
    ; then the snapshot, which returns twice: AL = 0 now, on to the drop; AL =
    ; 1 when the DOS program has exited and the session is back, which is
    ; where the image's wake address would have brought a kern_big machine.
    ; THE CLAIM COMES FIRST, so a machine with no room above 1MB refuses with
    ; nothing torn down.
    test byte [cpu_feat], CPU_F_PM
    jz .kbimg
    ; --- ...AND, WHEN THE BOX ASKS, THE IMAGE AS WELL (KERN-MEGA-PLAN 14.43,
    ; KERN-MEGA-DOS-PLAN 12.1 answer 2): `Save the session to disk first`.
    ; The park lives in RAM, so a program that hangs the machine, or a power
    ; cycle, loses it; HIBERNAT.IMG does not. It is kern_big's arm-3 image
    ; (hbm_wrimg, AL = 1), written through W5's protected-mode writer, and it
    ; comes back three ways: a warm reset still recovers the PARK first (W7,
    ; faster and the newer of the two), a cold one finds the pointer and
    ; resumes the image with no question (hbm_ask's HBP_DOS arm), and a
    ; normal return spends it at hbm_wake's step 5 - `hbm_forget` there
    ; already deletes both files on every wake, the park's included, so no
    ; stale image survives a trip that came home.
    ;
    ; BEFORE THE CLAIM, because the claim takes the XMS arena - everything
    ; the pool has left - and the image carries every pool block in use: the
    ; arena in the picture is a section the size of the pool, which no resume
    ; could place beside itself. So a claim refused AFTER the write is an
    ; unwind (.b9), where the park alone refuses with nothing torn down.
    ; hb_pick's answer is the predicate the box greys on (dos_hasfixed); a
    ; tick carried by a shortcut onto a machine with no fixed disk parks.
    test word [cs:hbm_dosrec + KDH_LB + KDL_FLAGS], KDLF_SAVE
    jz .nosave
    call COLD_SEG:hb_pick_x
    jc .nosave
    mov [hb_vol], al
    mov al, 1                   ; HBP_DOS = 1: no question at the resume
    call hbm_wrimg              ; the sweep, the lock, [sch_lock], the image
    jc .b8
    call MEGA_SEL_CODE:mega_dosclaimf
    jc .b9
    jmp short .held
.nosave:
    call MEGA_SEL_CODE:mega_dosclaimf
    jc .b7
    CSDO mov byte [CSEG hbm_dosheld], 1
    call hbm_vbn
    mov si, hb_drvmask
    CSDO mov byte [CSEG hbm_spm], 0
    xor dx, dx
    xor di, di
    call hbm_sweep
    call hbm_lock
    inc byte [sch_lock]
.held:
    call hbm_psum               ; THE PROGRAMS' SUM, as hbm_wrimg's step 5
    CSDO mov [CSEG hbm_psv], ax ; takes it for an image: hbm_wake 3a checks
    CSDO mov [CSEG hbm_psv + 2], dx ; it on this way home too, and without
                                ; it every trip compared against a stale
                                ; figure and refused (KERN-MEGA-PLAN 14.22).
                                ; Before the snapshot, so the park holds it
    call MEGA_SEL_CODE:mega_dosparkf   ; AL = 1: back from DOS
    or al, al
    jnz .parkback
    mov byte [mega_dparked], 1  ; ...AND NOTHING IS TORN DOWN after it (the
    jmp .noimg                  ; byte is kernel data, which the restore puts
.parkback:                      ; back to 0: hbm_down)
    call MEGA_SEL_CODE:mega_dosfreef
    jmp hbm_wake
.kbimg:
%endif
    call COLD_SEG:hb_pick_x     ; is there a fixed disk at all? THE predicate
    jc .noimg                   ; (SPEC.md 87.2), so what greys, what is probed
    mov [hb_vol], al            ; and what is written cannot disagree
    mov al, 1                   ; HBP_DOS = 1: not the user leaving
    call hbm_wrimg
    jc .noimg
    mov bx, hbm_dosrec + KDH_LB ; BX = the record's launch block, below
    ; **THE RECORD'S COPY AND NOT THE STAGE'S**: the stage is filled at step 5,
    ; below, out of this very record - so a bit set in the staging area here is
    ; a bit `rep movsw` overwrites forty lines later. It read as the whole
    ; return working and `kd_leave` posting nothing, which is a machine that
    ; comes back with no exit code in it.
    or word [cs:bx+KDL_FLAGS], KDLF_HIBER
    ; --- ...AND WHAT A LIVE RESUME NEEDS (SPEC.md 96.49) --------------------
    ; Here and nowhere else: `hb_vol` is `hb_pick_x`'s answer four lines up
    ; and the wake address is what `hbm_wrimg` has just written into the
    ; pointer, so this is the one point in the handoff where all four are
    ; settled. They go
    ; into the RECORD's copy and not the stage's, for `KDLF_HIBER`'s own
    ; reason immediately below - step 5 copies the record over the stage.
    ; WITH `.cold` IN THE HMA TOO (SPEC.md 96.49.9): step 5 sums what runs up
    ; there, and kd_resume sums it again before it jumps.
    mov word [cs:bx+KDL_WAKE], hbm_wake ; ...exactly what hbm_wrimg put in
    mov [cs:bx+KDL_WAKE + 2], cs        ; the pointer, and from the same place
    mov [cs:bx+KDL_KSEG], ds    ; DS IS KERNEL_SEG in this module (SPEC.md 2.8)
    mov al, [hb_vol]
    mov [cs:bx+KDL_HBVOL], al
    mov si, clk_sec             ; the ten `hbm_wake` puts back, ours because
    lea di, [bx+KDL_CLK]        ; two of them are clk_rtc and clk_dirty and
    mov cx, 10                  ; nothing over there can know them
.hbclk:                         ; (kernel/clock.inc)
    mov al, [si]
    mov [cs:di], al
    inc si
    inc di
    loop .hbclk
    ; --- ...AND THE TICK THOSE TEN BYTES WERE TRUE AT (SPEC.md 96.49.5) -----
    ; A clock reading is worth nothing over there without the instant it was
    ; taken: the program may run for an hour and `kern_dos` has no way to
    ; measure one - its `cw_clk_snapshot` is a bare `retf`, and the six stores
    ; that trusted it brought the resumed machine home at 00:00:00 00/00/0000.
    ; 0040:006C is the only counter that runs on BOTH sides: SPEC.md 8.1's PIT
    ; change is a MODE change so IRQ0 stays 18.2065 Hz, `sch_isr` chains the
    ; ROM's handler every tick, and the stub restores the IVT alone and never
    ; file sector 2 - so it is still counting when we come back. A PAIR with
    ; the ten bytes above, and banked in the same breath for that reason.
    xor ax, ax
    mov es, ax
    mov ax, [es:0x046C]
    mov [cs:bx+KDL_TICK], ax
    mov ax, [es:0x046E]
    mov [cs:bx+KDL_TICK + 2], ax
    ; ...AND WHERE WE WILL LOOK FOR ALL OF IT (SPEC.md 96.49.6). `kd_stageseg`
    ; answers out of the BDA's mode byte, which belongs to the DOS PROGRAM;
    ; `hbm_wake` asks `[vid_kind]`. They agree when `kern_dos` STARTS - step 4
    ; below is `vid_reboot`, which sets mode 7 on a Hercules primary and 3
    ; elsewhere, the same split - and they stop agreeing the moment the program
    ; calls int 10h. So the side that will BELIEVE the answer states it.
    call hbm_stageseg           ; ES = B000 or B800
    mov [cs:bx+KDL_STAGE], es
.noimg:                         ; ...and the post is spent HERE on both arms,
    mov word [hb_dosseg], 0     ; and the image (if any) has it

    ; --- 4. the panel, the drivers, the lock, text mode - 87.5 step 3's order,
    ; and the stub, the transport words and the extents into the stage: the
    ; half the resume shares, hbm_down. The lock may already be hbm_wrimg's,
    ; and [cs:hbm_dosheld] - this routine's own byte - is how it knows.
    mov si, kds_stub
    mov cx, kds_stub_end - kds_stub
    call hbm_down               ; ES = the stage: the stub, HS_UNIT, HS_EXT

    ; --- 5. the rest of the staging area -------------------------------------
    push ds
    push cs
    pop ds                      ; ...and DS the module, for the three copies
    mov si, hbm_dosrec + KDH_PLEN   ; the two lengths, as they lie
    mov di, KDS_PLEN
    mov cx, 4
    rep movsw
    mov si, hbm_dosrec + KDH_CPLEN  ; ...and the CORE's packed length beside
    mov di, KDS_CPLEN               ; them (96.44.5.4). Its UNPACKED length is
    mov cx, 2                       ; not staged: `kds_expand` is unbounded, so
    rep movsw                       ; nothing over there reads one
    mov ax, [cs:hbm_dosrec + KDH_CORG]  ; **`cs:` THOUGH DS IS CS HERE**: the
    mov [es:KDS_CORG], ax               ; module's own data is CS-relative
    mov ax, [cs:hbm_dosrec + KDH_POFF]  ; (SPEC.md 2.8.6) and os88ovlchk reads
    sub ax, [cs:hbm_dosrec + KDH_COFF]  ; the instruction rather than the two
    mov cl, 5                       ; lines of context that make it safe. The
    shl ax, cl                      ; delta is exact: the parts are
                                    ; 512-aligned, so a sector is 32 paragraphs
    mov [es:KDS_CDELTA], ax
    mov si, hbm_dosrec + KDH_LB     ; ...and the launch block, verbatim
    mov di, KDS_LB
    mov cx, KDL_SIZE / 2
    rep movsw
    pop ds
    ; **AND THE DRIVE THE ROM IS TO BOOT FROM**,
    mov bl, [dsk_bootvol]       ; which is this machine's and not the
    call COLD_SEG:dskf_dsk_vol_row  ; program's: `int 19h` reads DL as it finds
    jc .nobootu                 ; it (SPEC.md 96.40.2), and A: is an empty
    mov al, [bx+DV_UNIT]        ; drive on every machine that has a hard disk
    xor ah, ah
    mov [es:KDS_LB + KDL_UNIT], ax
.nobootu:
    ; --- ...AND THE MOUSE, for the same reason (SPEC.md 96.45) --------------
    ; `mouse_init` spent 596 ms at boot settling which port has a mouse and
    ; which LINE it fires on, against a modem that answers in bytes whose bit
    ; 5 is the left button (SPEC.md 9.5). Nothing a package can see records
    ; that answer, and step 6 below UNHOOKS the mouse - so without these three
    ; lines `kern_dos` has INT 33h and no pointer to put in it.
    ;
    ; A PS/2 mouse leaves the base ZERO and that is the whole refusal: 9.9's
    ; transport is the 8042's aux port and `[mou_port]` is `MOU_P2ROW` there,
    ; which is not an index into `mou_bases` at all - so the test is the ROW
    ; and not the base, and a machine that fails it keeps 96.10's still
    ; pointer instead of being handed rubbish to drive a UART with.
    test word [es:KDS_LB + KDL_FLAGS], KDLF_NOMOUSE
    jnz .nomouse                ; **THE USER SAID NOT TO** (SPEC.md 96.36.8),
                                ; which is a decision and not a capability, so
                                ; it is asked before the contest's result: the
                                ; base stays 0 and kd_mou_start reads that as
                                ; the machine having none
    cmp byte [mou_seen], 0      ; did anything ever win the contest?
    je .nomouse
    mov bl, [mou_port]
    cmp bl, MOU_P2ROW
    jae .nomouse                ; PS/2, or no row at all
    xor bh, bh                  ; **[mou_port] IS ALREADY A BYTE OFFSET** and
                                ; not a row number: `mou_isr` walks the table
                                ; with `inc bx`/`inc bx` against MOU_PEND and
                                ; `mou_claim` banks that same BX, which is why
                                ; MOU_P2ROW is 4 on a two-port machine rather
                                ; than 2. Scaling it again reads past the end
                                ; of `mou_bases` and hands kern_dos whatever
                                ; follows it as a UART base
    mov ax, [bx+mou_bases]
    mov [es:KDS_LB + KDL_MOUBASE], ax
    mov al, [mou_line]
    mov [es:KDS_LB + KDL_MOULINE], al
.nomouse:
    ; **THE VOLUME TABLE IS NOT GATHERED HERE ANY MORE** - step 3c is, before
    ; the teardown, and this step's own `rep movsw` of KDH_LB has already
    ; carried it. A driver-backed volume stops existing when `drv_shutdown_x`
    ; runs (`drv_release` drops its rows), so a gather at THIS point can only
    ; ever have seen the BIOS ones - which is the whole of the defect.
    mov si, dsk_dpt             ; **THE DISKETTE PARAMETER TABLE IS OURS AND NOT
    mov di, KDS_LB + KDL_DPT    ; THE BOX'S**, so the block is patched here
    mov cx, DPT_LEN             ; rather than gathered there: `int 1Eh` names
    rep movsb                   ; KERNEL_SEG:dsk_dpt and kern_dos lands on that
                                ; same segment with its own table at a
                                ; different offset, so without these eleven
                                ; bytes the ROM reads kern_dos's code as an
                                ; EOT and a gap length (SPEC.md 18.92, 96.40.1)
    mov word [es:KDS_TSEG], KDS_TMPSEG  ; where the PACKED stream lands: above
                                        ; the image it expands to, so the two
                                        ; never meet
%ifdef HBM_HMACHK
    ; --- ...AND WHAT RUNS ABOVE 1MB, for the live route (SPEC.md 96.49.9) ---
    ; The image is conventional memory. The drivers in the HMA's pool come
    ; back out of it (hbm_hsave's copy, hbm_wake's step 0b), but `.cold` and
    ; the HMA image's own code are trusted to be where they were - and a DOS
    ; program may have written there, or shut A20. So the span is summed HERE,
    ; after hbm_down and the last thing that writes the tail's block table,
    ; and kd_resume takes int 19h on a difference: XMEM.DRV then installs
    ; `.cold` again, which is the route that never needed this. The span is
    ; COLD_VSTART up to the tail's block table, which is the first byte above
    ; it that changes; a door that will not say where that is (CF = 1) gets
    ; `.cold` alone summed. A ROM's `.cold` is not in the HMA: KDL_HOFF is 0.
    xor bx, bx                  ; KDL_HOFF 0: nothing above 1MB to check
    cmp word [api_coldseg], HMA_SEG
    jne .hmaput
    mov al, ROMF_HTAB
    call far [rom_mfp]          ; AX = the table's offset in FFFF
    jnc .hmaend
    mov ax, cold_end
.hmaend:
    sub ax, COLD_VSTART
    shr ax, 1
    mov [es:KDS_LB + KDL_HWDS], ax
    xchg ax, cx                 ; CX = words
    push ds
    mov si, HMA_SEG
    mov ds, si
    mov si, COLD_VSTART
    cld
    HS_HMASUM                   ; BX = s1, DX = s2
    pop ds
    mov [es:KDS_LB + KDL_HSUM], bx
    mov [es:KDS_LB + KDL_HSUM + 2], dx
    mov bx, COLD_VSTART
.hmaput:
    mov [es:KDS_LB + KDL_HOFF], bx
%endif

    ; --- 6. the VECTORS the ROM is about to need (SPEC.md 96.40.2) ----------
    ; `sched_unhook` is exactly this and cannot be called: it falls into
    ; SPEC.md 18.100's park and the `int 19h` at its tail, so what is wanted is
    ; its BODY and there is no entry for one. Two shim calls and five
    ; instructions is cheaper than factoring it, and every byte here is the
    ; module's rather than the kernel's.
    ;
    ; **INT 09h IS THE ONE THAT MATTERS.** `mouse_init` hooks it for keypad 5
    ; and `hbm_handover` below UNMASKS IRQ1 on the way out, so a key pressed
    ; any time after the handover vectors into kern_dos's image at `kbm_isr`'s
    ; KERNEL offset - which is SPEC.md 9.6.5's hard freeze one segment along,
    ; and `kd_leave` ends on `int 16h`.
%ifdef KERN_BIG
    ; **THE PS/2 MOUSE IS QUIETED FIRST, OR THE PROGRAM'S KEYBOARD IS DEAD**
    ; (KERN-MEGA-PLAN 14.62). `mouse_unhook` masks IRQ12 and then sends 0xA7,
    ; and the device is left REPORTING - only its clock is held. A byte it
    ; sent in between, or one it had in flight, lands in the 8042's one output
    ; buffer with IRQ12 masked for the whole program (the handover masks it),
    ; so nobody ever reads it and the controller sends NOTHING from the
    ; keyboard either: the program runs on with no keys at all. Bochs, with
    ; the pointer moving across a launch: `KBTRIP entry 64=31` (OBF, AUX) on
    ; 6 trips of 12, and not one key. So the device is told to stop (0xF5,
    ; what POST leaves it as; hbm_wake's 0xF4 undoes it on the way home), its
    ; 0xFA found behind any packet already queued, with IRQ12 masked so the
    ; poll is the only reader...
    cmp byte [mou_p2], 0
    je .p2q
    pushf
    cli
    in  al, 0xA1
    or  al, MOU_P2SL
    out 0xA1, al
    popf
    mov al, 0xD4
    mov bp, mou_p2cw
    call hbm_kd
    mov al, 0xF5
    mov bp, mou_p2dw
    call hbm_kd
    mov cx, 256                 ; bounded: a real device has one packet in
.p2f5:                          ; flight; Bochs's mouse queues more, and 64
                                ; let a long move's tail reach the program as a
                                ; key (megakbtrip trip 1: `key 4300`)
    call hbm_p2rd
    cmp al, 0xFA
    loopne .p2f5
.p2q:
%endif
    mov bp, mouse_unhook
    call hbm_kd                 ; int 09h, int 0Ch/0Bh, int 74h, the UARTs
%ifdef KERN_BIG
    ; ...and after the 0xA7, the buffer emptied of any AUX byte still in it
    ; (a keyboard byte is left for int 09h: IRQ1 is open)
    cmp byte [cpu_tier], CPU_8086   ; an XT's 64h is no 8042 (9.9.2)
    je .p2d
    mov cx, 0x1000
.p2dr:
    in  al, 0x64
    and al, 0x21
    cmp al, 0x21
    jne .p2dn
    in  al, 0x60
.p2dn:
    loop .p2dr
.p2d:
%endif
    mov bp, snd_unhook
    call hbm_kd                 ; 61h bits 0-1 back to their boot state

    ; ...and the PIT, whose divisor is the kernel's fast one (SPEC.md 8.1).
    ; The ROM counts a tick per IRQ0 whatever the rate, so a machine handed
    ; over fast keeps a clock that gains. IF stays 0 from here: `hbm_handover`
    ; opens with `cli` anyway, and closing the window costs nothing.
    cli
    mov al, 0x36
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al

    ; --- 7. the ROM's int 08h back, the kernel's IRQs masked, and go --------
%ifdef KERN_MEGA
    test byte [cpu_feat], CPU_F_PM
    jz .kbhand
    call hbm_stageseg
    jmp MEGA_SEL_CODE:mega_dosgo   ; out of protected mode (kernel/megados.inc)
.kbhand:
%endif
    jmp hbm_handover            ; 87.5 step 5, factored. A JUMP for the reason
                                ; hbm_res's is one: nothing returns from it
; **A REFUSAL SAYS WHICH STEP, and that is not only for the bring-up.** Seven
; things can go wrong before the machine is torn down and they are seven
; different repairs: a stale record, a disk image still mounted, a folder that
; has moved, a DOS.O88 that is not there, a volume int 13h cannot reach, no
; heap for the extent list, and a part too fragmented to hand over. `kern_dos
; could not be reached` is true of all seven and useful for none, and these
; strings are MODULE bytes - the machine pays nothing for them until it asks.
%ifdef KERN_MEGA
 %define HBM_BADV .badv         ; kern_mega: a refusal after hbm_vbn gives
%else                           ; back what was taken (KERN-MEGA-DOS-PLAN
 %define HBM_BADV .bad          ; 10.4 item 3)
%endif
%ifdef OS88_IMG
.bimg:                          ; before hbm_vbn: nothing to give back
    mov si, hbm_s_dimg
    jmp short .bad
%endif
.b1:
%ifdef MEGA_DIAG
    call hbm_d1why              ; SI = `H ssss:oooo bbbbbbbb rrr`: what was
    jmp short .bad              ; posted, what is there, and the 286 restart
%else                           ; figures (KERN-MEGA-PLAN 10.3 item 54.2) -
    mov si, hbm_s_d1            ; MEGADIAG=1's field reading; the shipped
    jmp short .bad              ; kern_mega says kern_big's sentence (14.65)
%endif
.b2:
    mov si, hbm_s_d2
    jmp short HBM_BADV
.b3:
    mov si, hbm_s_d3
    jmp short HBM_BADV
.b4:
    mov si, hbm_s_d4
    jmp short HBM_BADV
.b5:
    mov si, hbm_s_d5
    jmp short HBM_BADV
.b6:
    mov si, hbm_s_d6
%ifdef KERN_MEGA
    jmp short .badv
.b9:                            ; the image is written and the park refused:
    call hbm_forget             ; nothing goes to DOS, so neither file stays
    mov si, hbm_s_d7
    jmp short .b8s
.b8:                            ; the image could not be written
    mov si, hbm_s_d8
.b8s:                           ; ...and hbm_wrimg's lock, [sch_lock] and
    push si                     ; detached drivers are given back, as
    xor dx, dx                  ; hbm_hibfail gives them - less its mode
    xchg dx, [hb_xseg]          ; byte, since no Hibernate window asked
    or dx, dx                   ; (hbf_dropck keeps the module while it is
    jz .b8x                     ; not GONE)
    mov bx, MEM_K_HIB
    mov bp, mem_free_x
    call hbm_cb
.b8x:
    CSDO mov byte [CSEG hbm_dosheld], 0
    dec byte [sch_lock]
    call hbm_unlock
    call hbm_reload
    call hbm_vbk
    pop si
    jmp short .bad
.b7:
    mov si, hbm_s_d7
.badv:                          ; the extent list, if it was claimed, and the
    xor dx, dx                  ; user's own volume back: every retry of a
    xchg dx, [hb_xseg]          ; refused handoff leaked a claim and left the
    or dx, dx                   ; user in DOS.O88's folder
    jz .nox
    mov bx, MEM_K_HIB
    mov bp, mem_free_x
    call hbm_cb
.nox:
    call hbm_vbk
%endif
.bad:
    mov word [hb_dosseg], 0
    call hbm_toast
    mov sp, [hb_sp]
    ret

; -----------------------------------------------------------------------------
; hbm_geomd - the transport facts for a volume kern_dos will be read OFF
; in:  AL = the volume index; that volume is the one standing
; out: as hbm_geom
;
; **A FLOPPY IS THE CASE hbm_geom REFUSES, AND IT IS THIS ONE'S ORDINARY CASE.**
; Hibernate writes an image and can only write it to a fixed disk, so its
; `test dl, 0x80 / jz .no` is right for it; the DOS handoff READS a part of a
; package, and on the machine docs/plans/KERN-DOS-PLAN.md §9 is about - no hard
; disk, so the program runs and the machine reboots - that package is on a
; floppy by definition.
;
; The facts for one are simpler rather than different: the unit is the row's,
; there is no partition so the base is zero, and the geometry is the MOUNTED
; volume's, which is this one because the caller has just stood on it.
; -----------------------------------------------------------------------------
hbm_geomd:
    call hbm_geom
    jnc .out                    ; a fixed disk: hibernate's own answer
    push ax
    push bx
    push dx
    mov bl, al
    call COLD_SEG:dskf_dsk_vol_row  ; BX = the row
    jc .no
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .no                     ; a DRIVER volume that refused really is one
    mov dl, [bx+DV_UNIT]        ; this machine cannot read without its driver
    test dl, 0x80
    jnz .no                     ; ...and a fixed disk that got here failed
    xor dh, dh                  ; hbm_geom's own range check, not its class one
    mov [hb_unit], dx
    mov word [hb_base], 0       ; a floppy has no partition to be offset into
    mov word [hb_base + 2], 0
    mov dx, [disk_spt]          ; the MOUNTED volume's, which is this one
    mov [hb_spt], dx
    mov dx, [disk_heads]
    mov [hb_heads], dx
    mov ax, [hb_spt]            ; hbm_geom's range check, repeated rather than
    dec ax                      ; shared: 1..63 and 1..255 as one unsigned
    cmp ax, 62                  ; window each, so a zero wraps and is refused
    ja .no
    mov ax, [hb_heads]
    dec ax
    cmp ax, 254
    ja .no
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop bx
    pop ax
    stc
.out:
    ret

; -----------------------------------------------------------------------------
; hbm_extat - a chain's sectors as extents, with a SKIP and a COUNT
; in:  AX = the first cluster, CX = sectors to skip, BX = sectors to take;
;      [hb_xseg] = where to build
; out: CF=0 and [hb_nx] runs of {dd lba, dw count}; CF=1 the chain ran out
; clobbers: AX, BX, CX, DX, SI, DI, BP, ES - its two callers (hbm_res and
;      hbm_dosrun) hold nothing across it, so nothing is banked
;
; hbm_res calls it with CX = 0 and BX from [mem_top]. THE SKIP IS THE WHOLE
; DIFFERENCE and it is why a part can be handed over at all: the part is a byte
; range of a file (docs/plans/KERN-DOS-PLAN.md §4.1.1), so the walk has to
; arrive at file sector N without reading anything, and N is not a cluster
; boundary. Sector N is the (N mod spc)'th of the (N div spc)'th cluster, which
; is the one place an off-by-one lands in the MIDDLE of the image rather than
; at its edge - tests/kdpart.py does the same arithmetic on the host.
; -----------------------------------------------------------------------------
hbm_extat:
    mov es, [hb_xseg]
    xor di, di
    mov word [hb_nx], 0
    mov si, ax                  ; SI = the cluster in hand
    CSDO mov [CSEG hbm_xskip], cx
    CSDO mov [CSEG hbm_xtake], bx
.clus:
    mov al, [dsk_spc]
    xor ah, ah
    mov dx, ax                  ; DX = this cluster's sectors
    cmp [cs:hbm_xskip], dx
    jb .here                    ; the part starts inside this cluster
    CSDO sub [CSEG hbm_xskip], dx      ; ...or past it: step over, reading nothing
    jmp .next                   ; (near: SPEC.md 18.7.5's high word took the
.here:                          ; short one out of reach)
    mov ax, si
    mov bp, dsk_clus2lba_x
    call COLD_SEG:wm_cbd        ; AX = its first LBA, volume-relative
    jc .bad
    add ax, [cs:hbm_xskip]      ; ...plus however far in the part starts
    mov bx, [dsk_c2hi]          ; ...in 32 bits: the volume-relative LBA's own
    adc bx, 0                   ; high word (SPEC.md 18.7.5)
    sub dx, [cs:hbm_xskip]      ; DX = sectors of this cluster we want
    CSDO mov word [CSEG hbm_xskip], 0
    add ax, [hb_base]           ; DX:AX would be the absolute LBA, and the
    adc bx, [hb_base + 2]       ; high half is BX because DX is in use
    cmp dx, [cs:hbm_xtake]
    jbe .whole
    mov dx, [cs:hbm_xtake]      ; ...capped at what is left to take
.whole:
    cmp word [hb_nx], 0
    je .new
    cmp ax, [cs:hbm_nextlo]     ; does this cluster follow the run in hand?
    jne .new
    cmp bx, [cs:hbm_nexthi]
    jne .new
    add [es:di-2], dx           ; contiguous: the run grows
    jmp short .grown
.new:
    cmp di, [cs:hbm_xlim]
    jae .bad                    ; more runs than the staging area holds
    mov [es:di], ax
    mov [es:di+2], bx
    mov [es:di+4], dx
    add di, HS_XSTRIDE
    inc word [hb_nx]
.grown:
    add ax, dx
    adc bx, 0
    CSDO mov [CSEG hbm_nextlo], ax
    CSDO mov [CSEG hbm_nexthi], bx
    CSDO sub [CSEG hbm_xtake], dx
    jz .done
.next:
    mov ax, si
    mov bp, dsk_next_clus_x
    call COLD_SEG:wm_cbd        ; AX = the next cluster; CF = outside the
    jc .bad                     ; volume's [2, dsk_maxclus]
    mov si, ax
    jmp .clus                   ; near: wm_cbd made the walk longer
.done:
    CLC_OR_STC .bad             ; CF = 0 done, or (.bad) CF = 1
    ret

; -----------------------------------------------------------------------------
; hbm_down - the machine stops being the kernel's: SPEC.md 87.5 steps 3-4 for
;            a resume and 96.40 steps 4-5 for a DOS handoff, which are the same
;            teardown in the same order and the same three copies
; in:  SI = the stub to stage (in this image), CX = its bytes;
;      [cs:hbm_dosheld] = 1 when hbm_wrimg already holds the gfx lock
; out: ES = the staging segment; the stub at HS_CODE, the four transport
;      words (hb_unit..hb_nx) at HS_UNIT, and [hb_nx] extents from [hb_xseg]
;      at HS_EXT; DI = HS_UNIT + 8 = HS_WAKE. DS = KERNEL_SEG.
;      Clobbers AX, BX, CX, DX, SI, BP
; -----------------------------------------------------------------------------
hbm_down:
    push si
    push cx
%ifdef KERN_MEGA
    ; **A PARK IS TORN DOWN BEFORE ITS SNAPSHOT AND NOT AFTER IT.** The park
    ; checksums the kernel's state above 1MB IN PLACE - the tables, the
    ; drivers' and modules' images - and restores only conventional memory, so
    ; whatever changes up there after the snapshot is a changed session: the
    ; drivers the sweep leaves loaded (disk, cable, RAM disk, EMS: none owns a
    ; chip kern_dos runs into) were unloaded HERE, freeing their selectors and
    ; images above 1MB under a restored kernel that still believes in them, and
    ; the stub refused every trip ("wrote over the parked session") once
    ; packages-high wave P4 put driver images up there. They come back as they
    ; were instead, and the panel with them
    cmp byte [mega_dparked], 0
    jne .nodown
%endif
    call COLD_SEG:cpf_cp_flush_close    ; the panel's settings
    call COLD_SEG:drv_shutdown_x        ; the drivers
.nodown:
    ; The lock and [sch_lock] may already be ours - `hbm_wrimg` takes both and
    ; gives neither back - so this cannot simply take them again. **AND
    ; `[sch_lock]` IS NOT THE WAY TO ASK**: `dsk_xfer` raises it around every
    ; `int 13h` and `ui_task` holds it for its own reasons, so a non-zero there
    ; means nothing about whether the image write took it. Testing it skipped
    ; `cw_gfx_lock` on the floppy arm - where nothing had been taken at all -
    ; and the teardown then ran unlocked. A byte the callers set is the only
    ; thing that knows.
    cmp byte [cs:hbm_dosheld], 0
    jne .locked                 ; hbm_wrimg's, still held
    call hbm_lock
.locked:
    mov bp, vid_reboot          ; text mode
    call hbm_kd
    call hbm_stageseg           ; ES = the text framebuffer
    pop cx
    pop si
    push ds
    push cs
    pop ds
    mov di, HS_CODE             ; the stub, to offset 0
    rep movsb
    pop ds
    mov cx, [hb_nx]             ; the extents: 3 words each
    mov ax, cx
    shl cx, 1
    add cx, ax
    push ds
    mov ds, [hb_xseg]
    xor si, si
    mov di, HS_EXT
    rep movsw
    pop ds
    mov si, hb_unit             ; unit, spt, heads, extents - the stub's order,
    mov di, HS_UNIT             ; and these four are the KERNEL's. LAST, so DI
    mov cx, 4                   ; comes back at HS_WAKE for hbm_res
    rep movsw
    ret

; -----------------------------------------------------------------------------
; hbm_handover - SPEC.md 87.5 step 5, and it does not come back
; in:  ES = the staging segment, the stub already copied to HS_CODE
;
; FACTORED because the DOS handoff (96.40) ends exactly here too: the same
; vector, the same three masks, the same far jump spelled as a return. What
; differs between a resume and a handoff is the payload, not the way out.
;
; **IT IS JUMPED TO AND NEVER CALLED.** The `retf` at the foot of it is the
; jump into the stub, not a return, so a near `call` here would read to
; os88ovlchk as 2.6.1's defect - and the reading would be fair: there is
; nothing for a return address to return to.
; -----------------------------------------------------------------------------
hbm_handover:
    cli
    push es
    xor ax, ax
    mov es, ax
    mov ax, [sch_old08]
    mov [es:0x20], ax
    mov ax, [sch_old08 + 2]
    mov [es:0x22], ax
    pop es
    in al, 0x21
    mov [es:HS_MASKM], al
    or al, 0x1A                 ; IRQ 1, 3, 4: the keyboard and the mice
    out 0x21, al
    mov byte [es:HS_SLAVE], 0
    cmp byte [cpu_tier], CPU_8086   ; an XT has no second 8259, and its port
    je .noslave                 ; 0xA0 is the NMI mask register with 0xA1 an
    mov byte [es:HS_SLAVE], 1   ; alias of it - mou_p2_init's refusal (9.9.2)
    in al, 0xA1
    mov [es:HS_MASKS], al
    or al, 0x10                 ; IRQ 12: the auxiliary port
    out 0xA1, al
.noslave:
    push es
    mov ax, HS_CODE
    push ax
    retf                        ; into the stub, IF = 0

; ...and still to place

; hbm_stageseg - out: ES = the text framebuffer the stub will run in
hbm_stageseg:
    push ax
    mov ax, 0xB800
    cmp byte [vid_kind], VID_HERC
    jne .set
    mov ax, 0xB000
.set:
    mov es, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_xcap - the extent list's bound for THIS volume (SPEC.md 87.5 step 2).
;            A run spans at least one cluster, so the image's CLUSTERS bound
;            the runs, and [dsk_spc] - which the walk beside it already reads
;            - is what turns its sectors into them. HS_XMAX stays the ceiling:
;            the staging region is fixed at assembly time and cannot grow.
; in:  the volume mounted
; out: AX = KB to claim; [cs:hbm_xlim] = the walk's byte bound
; -----------------------------------------------------------------------------
hbm_xcap:
    push bx
    push cx
    push dx
    mov bl, [dsk_spc]
    xor bh, bh
    or bx, bx                   ; a BPB that says 0 is not one this can divide
    jz .worst                   ; by: take the assembly-time bound instead
    mov ax, [mem_top]
    mov cl, 5
    shr ax, cl                  ; sectors the image has: paragraphs / 32
    add ax, bx
    dec ax
    xor dx, dx
    div bx                      ; AX = clusters, and so the runs' bound
    cmp ax, HS_XMAX
    jbe .cap
.worst:
    mov ax, HS_XMAX
.cap:
    mov bx, HS_XSTRIDE          ; ...as bytes, for the walk's own stop test
    mul bx
    CSDO mov [CSEG hbm_xlim], ax
    add ax, 1023                ; ...and as whole KB, for the claim
    mov cl, 10
    shr ax, cl
    pop dx
    pop cx
    pop bx
    ret

%ifdef KERN_MEGA
; =============================================================================
; HIBERNATE IN PROTECTED MODE (kern_mega; docs/plans/KERN-MEGA-DOS-PLAN.md 8,
; wave W5). The conventional image is written as on every other kernel - one
; dskw_write from linear 0, through the reflected int 13h - but rounded up to
; whole HBX_CHUNKs, and an EXTENDED SECTION is appended after it: a 512-byte
; head ('HIBX', the run count, the section's bytes, each run's linear base and
; length) and then the runs - the GDT, LDT and IDT as one, and every block the
; XMEM pool has given out - copied down through a 32KB bounce. Its Fletcher
; sums go in the pointer, which is written last.
;
; The resume reads the WHOLE FILE into one staging block above 1MB, placed
; clear of every block the image had and every block this boot holds, and
; enters the park stub over it (kernel/megapark.inc): the stub copies the runs
; to where the image had them, then conventional memory, then jumps to
; mega_hbwake through the image's own tables. Nothing of it leaves protected
; mode, on either CPU.
; =============================================================================

; hbm_xwconv - the conventional picture in protected mode: DX:AX bytes from
; linear 0, a whole number of HBX_CHUNKs, into the image file - the same
; bytes in the same file as one write from segment 0, which is what it was.
; But segment 0 is the NULL selector, so every word HDD.DRV's PIO loop moved
; out of the first 64KB was a mega_null0 fixup, two exceptions each: 32,512
; lines of MEGATRAP's census a hibernate (KERN-MEGA-PLAN 14.46). The identity
; map starts at segment 40h (GDT entry 8) and only the IVT's 1KB has no other
; way in, so: chunk 0 is WRITTEN for its size from 40h (its first cluster
; wrong, for a moment), every later chunk APPENDED from its own identity
; segment n x 800h, and then chunk 0 is put right in place with two
; write-ats - the first CLUSTER from segment 0 (an offset must be a cluster
; multiple: 1,024 fixups at 2KB), the rest of the chunk from 40h. An append
; needs a whole number of clusters already in the file, which is why chunk 0
; is not simply split. out: CF. Clobbers AX, BX, CX, DX, SI, ES
hbm_xwconv:
    shl dx, 1                   ; CX = the chunks: (DX:AX) >> 15
    rol ax, 1
    and ax, 1
    or ax, dx
    push ax
    call hbm_nimg               ; SI = the name (AX and DL spent)
    mov ax, 0x40
    mov es, ax
    xor bx, bx
    xor dx, dx
    mov cx, HBX_CHUNK
    call COLD_SEG:dwf_dskw_write    ; chunk 0's SIZE
    pop cx
    jc .out
    mov ax, HBX_CHUNK / 16      ; chunk 1's segment
.ap:
    dec cx
    jz .fix
    push cx
    push ax
    mov es, ax
    call hbm_nimg
    xor bx, bx
    mov cx, HBX_CHUNK
    call COLD_SEG:dwf_dskw_append
    pop ax
    pop cx
    jc .out
    add ax, HBX_CHUNK / 16
    jmp short .ap
.fix:
    mov al, [dsk_spc]           ; AX = the volume's cluster in bytes
    xor ah, ah
    mov cl, 9
    shl ax, cl
    push ax
    dec ax                      ; linear 0's selector over that cluster: the
    call hbm_xlow               ; fixup runs each instruction through LOW0,
    call hbm_nimg               ; which stops at 4FFh on purpose (mega.inc)
    xor bx, bx
    mov es, bx                  ; the first cluster, from linear 0
    pop cx
    push cx
    xor ax, ax
    xor dx, dx                  ; at offset 0
    call COLD_SEG:dwf_dskw_write_at
    pushf
    mov ax, 0x04FF              ; ...and back
    call hbm_xlow
    popf
    pop ax                      ; AX = the cluster = the offset of the rest
    jc .out
    cmp ax, HBX_CHUNK
    jae .ok                     ; a cluster as big as the chunk: all done
    push ax
    call hbm_nimg
    pop ax
    mov bx, ax
    sub bx, 0x400               ; 40h:BX = linear AX
    mov cx, HBX_CHUNK
    sub cx, ax
    mov dx, 0x40
    mov es, dx
    xor dx, dx                  ; DX:AX = the offset
    call COLD_SEG:dwf_dskw_write_at
    jc .out
.ok:
    clc
.out:
    ret

; hbm_xlow - MEGA_SEL_LOW0's limit = AX. Every register kept
hbm_xlow:
    push di
    push es
    push ax
    mov di, MEGA_SEL_WIN            ; the GDT, as data
    mov es, di
    mov [es:MEGA_SEL_LOW0], ax
    pop ax
    pop es
    pop di
    ret

; hbm_xconv - out: DX:AX = the conventional image's bytes on the disk, which
;             is [mem_top] x 16 up to a whole HBX_CHUNK. Preserves every other
;             register
hbm_xconv:
    push cx
    mov ax, [mem_top]
    add ax, HBX_CHUNK / 16 - 1
    and ax, -(HBX_CHUNK / 16)
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    pop cx
    ret

; hbm_xneed - out: AX = KB a resume's staging block needs for the image the
;             list describes ([mega_hbxl] its section's bytes): the header,
;             the conventional image and the section, both in whole chunks.
;             Clobbers CX, DX
hbm_xneed:
    mov ax, [mega_hbxl]
    mov dx, [mega_hbxl + 2]
    add ax, HBX_CHUNK - 1
    adc dx, 0
    mov cl, 15                  ; DX:AX >> 15: whole chunks
    shr ax, cl
    shl dx, 1
    or ax, dx
    mov cl, 5                   ; ...as KB
    shl ax, cl
    push ax
    call hbm_xconv
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    pop cx
    add ax, cx
    add ax, MPK_HDR / 1024
    ret

; hbm_pses - ES = the PARK'S SCRATCH (MEGA_SEL_PSCR, megados.inc's MPS_*),
; where the list of runs is built: not in `.text` since KERN-MEGA-PLAN 14.45.
; Every register but ES kept
hbm_pses:
    push ax
    mov ax, MEGA_SEL_PSCR
    mov es, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; hbm_xplan - BEFORE THE PICTURE: the masks and the wake address the image is
;             to wake with, the list of what is above 1MB, and whether a
;             resume could place all of it in one block beside it
; out: CF = 0 with [mega_dncpy] runs in the park's scratch's list (MPS_REG:
;      {dd -, dd to, dd bytes}),
;      run 0 the tables, and [mega_hbxl] = the section's bytes; CF = 1 a
;      resume would have no room, or the runs are more than HBX_HMAX.
;      Clobbers AX BX CX DX SI DI
; -----------------------------------------------------------------------------
hbm_xplan:
    in al, 0x21                 ; the masks the image wakes with: the
    mov [mega_dimr1], al        ; hardware drivers' lines are already
    in al, 0xA1                 ; masked by the sweep
    mov [mega_dimr2], al
    mov word [mega_hbwk], hbm_wake  ; ...and where (mega_hbwake's far jump)
    mov [mega_hbwk + 2], cs
    push es
    call hbm_pses               ; the list is the park's scratch's
    mov di, MPS_REG             ; run 0: the GDT, the LDT and the IDT, which
    mov word [es:di+4], MEGA_GDT_LIN & 0xFFFF  ; are one range. `.cold`'s copy
    mov word [es:di+6], MEGA_GDT_LIN >> 16     ; is not in it: a code selector,
    mov word [es:di+8], (MEGA_IDT_LIN + 256 * 8 - MEGA_GDT_LIN) & 0xFFFF
    mov word [es:di+10], (MEGA_IDT_LIN + 256 * 8 - MEGA_GDT_LIN) >> 16
    mov word [mega_hbxl], (512 + MEGA_IDT_LIN + 256 * 8 - MEGA_GDT_LIN) & 0xFFFF
    mov word [mega_hbxl + 2], (512 + MEGA_IDT_LIN + 256 * 8 - MEGA_GDT_LIN) >> 16
    add di, MPK_CPYSZ           ; so KERNEL.SYS's bytes, as a boot makes them
    mov si, mx_tab
.mx:
    mov ax, [ss:si+MX_KB]          ; every block the pool has given out, the
    or ax, ax                   ; kernel's own among them (track 1's modules
    jz .next                    ; and caches: hbm_wake flushes the caches)
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    mov [es:di+8], ax
    mov [es:di+10], dx
    add [mega_hbxl], ax
    adc [mega_hbxl + 2], dx
    mov ax, [ss:si+MX_OFF]
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    add dx, MEGA_XPOOL >> 16
    mov [es:di+4], ax
    mov [es:di+6], dx
    add di, MPK_CPYSZ
.next:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .mx
    mov ax, di
    sub ax, MPS_REG
    mov cl, MPK_CPYSZ
    div cl
    xor ah, ah
    mov [mega_dncpy], ax
    pop es
    cmp ax, HBX_HMAX + 1        ; more runs than the head has room to list:
    jae .full                   ; run 0's bytes would land on the list's tail
    call hbm_xneed              ; AX = KB
    jmp hbm_xfit                ; CF
.full:
    stc                         ; ...refused BEFORE the picture, not an
    ret                         ; image that is written and cannot resume

; -----------------------------------------------------------------------------
; hbm_xfit - a place in the pool for AX KB clear of every run the list names
;            (but run 0, which is below the pool) and every block this kernel
;            holds now - first fit, from the bottom
; out: CF = 0 with AX = its offset in KB from the pool's base; CF = 1 none.
;      Every other register kept
; -----------------------------------------------------------------------------
hbm_xfit:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call hbm_pses               ; (the list: the scratch's)
    mov bx, ax                  ; BX = KB wanted
    xor ax, ax                  ; AX = the candidate
.again:
    mov si, MPS_REG + MPK_CPYSZ
    mov cx, [mega_dncpy]
    dec cx
.img:
    jcxz .live
    push cx
    mov dx, [es:si+6]              ; DX = the run's offset in the pool, in KB
    sub dx, MEGA_XPOOL >> 16
    mov cl, 6
    shl dx, cl
    mov di, [es:si+4]
    mov cl, 10
    shr di, cl
    or dx, di
    mov di, [es:si+8]              ; DI = ...and its end
    shr di, cl
    push dx
    mov dx, [es:si+10]
    mov cl, 6
    shl dx, cl
    or di, dx
    pop dx
    pop cx
    add di, dx
    call .ov
    jc .again
    add si, MPK_CPYSZ
    dec cx
    jmp short .img
.live:
    mov si, mx_tab
.lv:
    mov di, [ss:si+MX_KB]
    or di, di
    jz .lvn
    mov dx, [ss:si+MX_OFF]
    add di, dx
    call .ov
    jc .again
.lvn:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .lv
    mov dx, ax
    add dx, bx
    jc .no
    cmp dx, [xm_kb]
    ja .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.ov:                            ; [DX, DI) against [AX, AX + BX): CF = 1
    cmp ax, di                  ; and the candidate moved past it when they
    jae .ovn                    ; meet
    push ax
    add ax, bx
    cmp dx, ax
    pop ax
    jae .ovn
    mov ax, di
    stc
    ret
.ovn:
    clc
    ret

; hbm_xsum - [mega_dsum] += Fletcher's sums over the bounce, ES:0 for
;            HBX_CHUNK bytes, mod 65535. Clobbers AX CX DX SI
hbm_xsum:
    mov ax, [mega_dsum]
    mov dx, [mega_dsum + 2]
    push ds
    push es
    pop ds
    xor si, si
    mov cx, HBX_CHUNK / 2
.l:
    add ax, [si]
    adc ax, 0
    add dx, ax
    adc dx, 0
    inc si
    inc si
    loop .l
    pop ds
    mov [mega_dsum], ax
    mov [mega_dsum + 2], dx
    ret

; hbm_xbounce - the 32KB bounce, conventional and page-safe like every claim:
;               out CF = 0 with ES = DX = its segment and [mega_hbbl] its
;               linear base; CF = 1 none. Clobbers AX BX CX BP
hbm_xbounce:
    mov ax, HBX_KB
    mov bx, MEM_K_HIB
    mov bp, mem_claim_x
    call hbm_cb                 ; DX = its segment
    jc .out
    mov es, dx
    mov ax, dx
    mov cl, 4
    shl ax, cl
    mov [mega_hbbl], ax
    mov ax, dx
    mov cl, 12
    shr ax, cl
    mov [mega_hbbl + 2], ax
.out:
    ret

; hbm_xfree - the bounce at DX back. Clobbers BX BP
hbm_xfree:
    mov bx, MEM_K_HIB
    mov bp, mem_free_x
    jmp hbm_cb

; hbm_xup - CX bytes (even) from linear [mega_xsrc] to linear [mega_xdst],
;           under the kernel's copy. Clobbers AX BX CX DX SI DI BP
hbm_xup:
    mov bp, mega_lincopy
    jmp hbm_kd

; -----------------------------------------------------------------------------
; hbm_xwrite - THE EXTENDED SECTION, after the conventional image (5b)
; in:  hbm_xplan's list; the lock and [sch_lock] held, as hbm_wrimg has them
; out: CF = 0 appended and the pointer's three fields set; CF = 1 not.
;      Clobbers AX BX CX DX SI DI BP ES
; -----------------------------------------------------------------------------
hbm_xwrite:
    call hbm_xbounce            ; AFTER the picture, so the image does not
    jnc .got                    ; hold it
    ret
.got:
    push dx                     ; for the free
    xor ax, ax
    mov [mega_dsum], ax
    mov [mega_dsum + 2], ax
    mov [mega_hbxd], ax
    mov [mega_hbxd + 2], ax
    xor di, di                  ; --- the head, at the front of chunk 0 -------
    mov cx, 256
    rep stosw
    mov word [es:0], 'HI'
    mov word [es:2], 'BX'
    mov cx, [mega_dncpy]
    mov [es:4], cx
    mov ax, [xm_kb]             ; the pool the runs were cut from, which a
    mov [es:6], ax              ; resume holds its own against
    mov ax, [mega_hbxl]
    mov [es:8], ax
    mov ax, [mega_hbxl + 2]
    mov [es:10], ax
    push ds
    mov ax, MEGA_SEL_PSCR       ; (the list: the park's scratch's)
    mov ds, ax
    mov si, MPS_REG + 4
    mov di, 12
.hd:
    movsw                       ; to, then bytes
    movsw
    movsw
    movsw
    add si, MPK_CPYSZ - 8
    loop .hd
    pop ds
    mov word [mega_hbf], 512
    mov si, MPS_REG             ; --- the runs, through the bounce ------------
    mov cx, [mega_dncpy]
.run:
    push cx
    push es                     ; (the bounce, around the scratch's list)
    call hbm_pses
    mov ax, [es:si+4]
    mov [mega_hbs], ax
    mov ax, [es:si+6]
    mov [mega_hbs + 2], ax
    mov ax, [es:si+8]
    mov [mega_hbr], ax
    mov ax, [es:si+10]
    mov [mega_hbr + 2], ax
    pop es
.chunk:
    mov cx, HBX_CHUNK
    sub cx, [mega_hbf]          ; the room left in the bounce...
    mov ax, [mega_hbr]
    cmp word [mega_hbr + 2], 0
    jne .take
    or ax, ax
    jz .rnext                   ; ...and this run is done
    cmp ax, cx
    jae .take
    mov cx, ax
.take:
    push si
    push cx
    mov ax, [mega_hbs]
    mov [mega_xsrc], ax
    mov ax, [mega_hbs + 2]
    mov [mega_xsrc + 2], ax
    mov ax, [mega_hbbl]
    mov dx, [mega_hbbl + 2]
    add ax, [mega_hbf]
    adc dx, 0
    mov [mega_xdst], ax
    mov [mega_xdst + 2], dx
    call hbm_xup
    pop cx
    pop si
    add [mega_hbs], cx
    adc word [mega_hbs + 2], 0
    sub [mega_hbr], cx
    sbb word [mega_hbr + 2], 0
    add [mega_hbf], cx
    cmp word [mega_hbf], HBX_CHUNK
    jb .chunk
    call .flush
    jnc .chunk
    pop cx
    jmp short .fail
.rnext:
    pop cx
    add si, MPK_CPYSZ
    dec cx
    jz .tail
    jmp .run
.tail:
    mov di, [mega_hbf]          ; the last chunk, zeros to its end
    or di, di
    jz .done
    mov cx, HBX_CHUNK
    sub cx, di
    shr cx, 1
    xor ax, ax
    rep stosw
    call .flush
    jc .fail
.done:
    CSDO mov byte [CSEG hbm_ptrfix + HBP_XFLG], 1
    mov ax, [mega_dsum]
    CSDO mov [CSEG hbm_ptrfix + HBP_XSUM], ax
    mov ax, [mega_dsum + 2]
    CSDO mov [CSEG hbm_ptrfix + HBP_XSUM + 2], ax
    mov ax, [mega_hbxd]
    CSDO mov [CSEG hbm_ptrfix + HBP_XLEN], ax
    mov ax, [mega_hbxd + 2]
    CSDO mov [CSEG hbm_ptrfix + HBP_XLEN + 2], ax
    pop dx
    call hbm_xfree
    clc
    ret
.fail:
    pop dx
    call hbm_xfree
    stc
    ret
.flush:                         ; the bounce, summed and appended
    push si
    call hbm_xsum
    call hbm_nimg               ; SI = the name
    xor bx, bx
    mov cx, HBX_CHUNK
    call COLD_SEG:dwf_dskw_append
    pop si
    jc .fret
    add word [mega_hbxd], HBX_CHUNK
    adc word [mega_hbxd + 2], 0
    mov word [mega_hbf], 0
    clc
.fret:
    ret

; -----------------------------------------------------------------------------
; hbm_xruns - the image's runs, as hbm_xres read them into MPS_REG, against
;             THIS boot (KERN-MEGA-DOS-PLAN 10.5 defect 5): run 0 is exactly
;             the tables' range, and every other run is whole KB, not empty,
;             and inside the pool this kernel found - [xm_kb] KB from
;             MEGA_XPOOL - so a damaged or foreign section refuses before the
;             stub copies a byte. hbm_xfit's KB arithmetic rests on this too
; out: CF = 1 refuse. Clobbers AX BX CX DX SI
; -----------------------------------------------------------------------------
%if MEGA_XPOOL & 0xFFFF
 %error "hbm_xruns takes the pool's base to be 64KB-aligned"
%endif
hbm_xruns:
    push es
    call hbm_pses               ; (the list: the scratch's)
    call .go                    ; CF, kept by the pop
    pop es
    ret
.go:
    mov si, MPS_REG
    cmp word [es:si+4], MEGA_GDT_LIN & 0xFFFF
    jne .no
    cmp word [es:si+6], MEGA_GDT_LIN >> 16
    jne .no
    cmp word [es:si+8], (MEGA_IDT_LIN + 256 * 8 - MEGA_GDT_LIN) & 0xFFFF
    jne .no
    cmp word [es:si+10], (MEGA_IDT_LIN + 256 * 8 - MEGA_GDT_LIN) >> 16
    jne .no
    mov cx, [mega_dncpy]
.r:
    dec cx
    jz .ok
    add si, MPK_CPYSZ
    mov ax, [es:si+4]
    mov dx, [es:si+6]
    sub dx, MEGA_XPOOL >> 16
    jb .no                      ; below the pool
    call .kb
    jc .no
    mov bx, ax                  ; BX = where it starts, in KB
    mov ax, [es:si+8]
    mov dx, [es:si+10]
    call .kb
    jc .no
    or ax, ax
    jz .no
    add ax, bx
    jc .no
    cmp ax, [xm_kb]
    jbe .r
.no:
    stc
    ret
.ok:
    clc
    ret
.kb:                            ; DX:AX bytes -> AX KB; CF = 1 not whole KB
    test ax, 0x3FF              ; or 64MB or more: DX counts 64KB, so
    jnz .kbno                   ; 3FFh is the last that shifts into a word
    cmp dx, 0x3FF
    ja .kbno
    push cx
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    pop cx
    clc
    ret
.kbno:
    stc
    ret

; -----------------------------------------------------------------------------
; hbm_xclaim - the resume's staging block into mx_tab as the kernel's own, at
;              the place hbm_xfit found, so nothing the file read does can be
;              handed it. Nothing frees it on the way in: the stub puts back
;              the image's mx_tab over this one
; in:  AX = its offset in KB from the pool's base
; out: CF = 1 the table is full. AX kept; clobbers CX DX
; -----------------------------------------------------------------------------
hbm_xclaim:
    push bx
    push di
    mov di, mx_tab
.f:
    cmp word [ss:di+MX_KB], 0
    je .t
    add di, MX_BLKSZ
    cmp di, mx_tab_end
    jb .f
    stc
    jmp short .o
.t:
    push ax
    mov bx, ax
    call hbm_xneed              ; AX = its KB
    pushf
    cli
    mov [ss:di+MX_OFF], bx
    mov byte [ss:di+MX_OWN], MX_KOWN
    mov word [ss:di+MX_SEL], 0
    mov [ss:di+MX_KB], ax          ; ...last: a non-zero size is the claim
    popf
    pop ax
    clc
.o:
    pop di
    pop bx
    ret

; hbm_xunclm - a refused resume's staging claim back, found by [mega_dpbase]
; (0 = none was made). Clobbers AX CX DX SI
hbm_xunclm:
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase + 2]
    mov cx, ax
    or cx, dx
    jz .o
    sub dx, MEGA_XPOOL >> 16
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx                   ; AX = its KB offset
    mov si, mx_tab
.s:
    cmp word [ss:si+MX_KB], 0
    je .n
    cmp byte [ss:si+MX_OWN], MX_KOWN
    jne .n
    cmp [ss:si+MX_OFF], ax
    jne .n
    mov word [ss:si+MX_KB], 0
    ret
.n:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .s
.o:
    ret

; -----------------------------------------------------------------------------
; hbm_xres - THE RESUME, kern_mega in protected mode (jumped to from hbm_res,
;            the image found and the pointer in hbm_ptrbuf)
; -----------------------------------------------------------------------------
hbm_xres:
    call hbm_xbounce
    jnc .got
    jmp hbm_res.fail
.got:
    mov [hb_xseg], dx           ; ...where .refuse finds it
    xor ax, ax                  ; no staging claim yet: .refuse frees the one
    mov [mega_dpbase], ax       ; mega_dpbase names, and a stale base could
    mov [mega_dpbase + 2], ax   ; name a block somebody else has since
    inc byte [sch_lock]         ; the kernel's copy has two descriptors
    call hbm_xconv              ; --- the section's head, first --------------
    call .rd
    jc .bad1
    cmp word [es:0], 'HI'
    jne .bad1
    cmp word [es:2], 'BX'
    jne .bad1
    mov cx, [es:4]
    jcxz .bad1
    cmp cx, MPK_CMAX
    ja .bad1
    mov ax, [es:6]              ; THE POOL IT WAS CUT FROM, against this
    or ax, ax                   ; boot's (KERN-MEGA-DOS-PLAN 10.5 defect 5):
    jz .bad1                    ; the restored kernel believes the image's
    cmp ax, [xm_kb]             ; [xm_kb] and its runs live in it, so less
    jbe .nok                    ; memory now - a SIMM out, a BIOS setting -
    dec byte [sch_lock]         ; is a refusal, not a copy into memory that
    mov si, hbm_s_d7            ; is not there ('No room above 1MB')
    jmp .refuse
.bad1:
    jmp .bad
.nok:
    mov [mega_dncpy], cx
    mov word [mega_dnreg], 0    ; the stub sums nothing: hbm_xsum has
    mov si, 12
    mov di, MPS_REG + 4
    push ds
    push es
    push es
    pop ds                      ; DS = the bounce, ES = the park's scratch
    call hbm_pses
.hd:
    movsw
    movsw
    movsw
    movsw
    add di, MPK_CPYSZ - 8
    loop .hd
    pop es
    pop ds
    call hbm_xruns              ; ...each run, against the tables and the pool
    jc .bad
    mov ax, [cs:hbm_ptrbuf + HBP_XLEN]  ; --- the block -----------------------
    mov [mega_hbxl], ax
    mov ax, [cs:hbm_ptrbuf + HBP_XLEN + 2]
    mov [mega_hbxl + 2], ax
    call hbm_xneed
    call hbm_xfit               ; AX = its KB offset in the pool
    jnc .placed
    dec byte [sch_lock]
    mov si, hbm_s_d7            ; 'No room above 1MB'
    jmp .refuse
.placed:
    call hbm_xclaim             ; CLAIMED for the read below: a cache that
    jnc .claimed                ; takes a pool block while the file comes in
    dec byte [sch_lock]         ; must not take this one (10.5 lesser)
    mov si, hbm_s_d7
    jmp .refuse
.claimed:
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    add dx, MEGA_XPOOL >> 16
    mov [mega_dpbase], ax
    mov [mega_dpbase + 2], dx
    call hbm_xconv              ; each run's FROM: the block, the header, the
    add ax, MPK_HDR + 512       ; image and the section's head, and the runs
    adc dx, 0                   ; before it
    add ax, [mega_dpbase]
    adc dx, [mega_dpbase + 2]
    push es
    call hbm_pses               ; (the list: the scratch's)
    mov si, MPS_REG
    mov cx, [mega_dncpy]
.fr:
    mov [es:si], ax
    mov [es:si+2], dx
    add ax, [es:si+8]
    adc dx, [es:si+10]
    add si, MPK_CPYSZ
    loop .fr
    pop es
    xor ax, ax                  ; --- the whole file, chunk by chunk, into it
    mov [mega_dsum], ax
    mov [mega_dsum + 2], ax
    mov [mega_hbs], ax          ; the file offset
    mov [mega_hbs + 2], ax
.chunk:
    call hbm_xconv              ; DX:AX = the image's end, where the section
    mov bx, ax                  ; and its sum begin
    mov cx, dx
    add ax, [mega_hbxl]
    adc dx, [mega_hbxl + 2]
    cmp ax, [mega_hbs]
    jne .more
    cmp dx, [mega_hbs + 2]
    jne .more
    jmp .read
.more:
    push bx
    push cx
    mov ax, [mega_hbs]
    mov dx, [mega_hbs + 2]
    call .rd
    pop cx
    pop bx
    jc .bad
    mov ax, [mega_hbs + 2]      ; ...summed from the image's end on
    cmp ax, cx
    jb .nosum
    ja .sum
    mov ax, [mega_hbs]
    cmp ax, bx
    jb .nosum
.sum:
    call hbm_xsum
.nosum:
    mov ax, [mega_hbbl]
    mov [mega_xsrc], ax
    mov ax, [mega_hbbl + 2]
    mov [mega_xsrc + 2], ax
    mov ax, [mega_hbs]
    mov dx, [mega_hbs + 2]
    add ax, MPK_HDR
    adc dx, 0
    add ax, [mega_dpbase]
    adc dx, [mega_dpbase + 2]
    mov [mega_xdst], ax
    mov [mega_xdst + 2], dx
    mov cx, HBX_CHUNK
    call hbm_xup
    add word [mega_hbs], HBX_CHUNK
    adc word [mega_hbs + 2], 0
    jmp .chunk
.bad:
    dec byte [sch_lock]
    mov si, hbm_s_lost
.refuse:
    push si
    call hbm_xunclm             ; the staging block, if it was claimed
    mov dx, [hb_xseg]
    call hbm_xfree
    mov word [hb_xseg], 0
    call hbm_vbk
    mov byte [hb_mode], HB_M_RESUME
    pop si
    call hbm_toast
    call hbm_winback
    mov sp, [hb_sp]
    ret
.read:
    mov ax, [mega_dsum]         ; THE SUM, against the pointer's: a byte of
    cmp ax, [cs:hbm_ptrbuf + HBP_XSUM]  ; the section changed on the disk is
    jne .bad                    ; a refusal, not a machine that wakes wrong
    mov ax, [mega_dsum + 2]
    cmp ax, [cs:hbm_ptrbuf + HBP_XSUM + 2]
    jne .bad
    mov ax, [mem_top]           ; the KB the stub puts back
    mov cl, 6
    shr ax, cl
    mov [mega_dpkb], ax
    ; --- the fresh kernel goes: hbm_down's order, with no stub to stage ------
    call COLD_SEG:cpf_cp_flush_close
    call COLD_SEG:drv_shutdown_x
    call hbm_lock
    mov bp, vid_reboot          ; text mode: hbm_wake's cells are in its page
    call hbm_kd
    call hbm_stageseg
    mov si, clk_sec             ; the fresh boot's clock, as hbm_res stages it
    mov di, HS_CLK
    mov cx, 5
    rep movsw
    mov ax, [hb_doscode]
    mov [es:HS_DOSCODE], ax
    mov di, HS_KSEG
    mov ax, ds
    stosw
    mov ax, [hb_dosakb]
    stosw
    mov ax, 0xFFFF              ; HS_TICK0: the clock above is current
    stosw
    stosw
    call MEGA_SEL_CODE:mega_hbgof  ; ...and does not come back
.rd:                            ; HBX_CHUNK at file offset DX:AX into ES:0.
    push ax                     ; CF = 1 unless all of it came
    push dx
    call hbm_nimg
    pop dx
    pop ax
    xor bx, bx
    mov cx, HBX_CHUNK
    call COLD_SEG:dwf_dskw_read_at
    jc .rdo
    cmp ax, HBX_CHUNK
    je .rdo                     ; (CF = 0 from the equal compare)
    stc
.rdo:
    ret
%endif

; -----------------------------------------------------------------------------
; hbm_wake - the restored kernel wakes up here (SPEC.md 87.6)
; in:  CS = this image's segment IN THE RESTORED MEMORY, DS = KERNEL_SEG,
;      SS:SP = hbm_perform's entry frame, IF = 0, the gfx lock held and
;      [sch_lock] up - all of it as the write left it
; out: does not return: it RETURNS THROUGH THE FRAME, into modh_e_perform
; -----------------------------------------------------------------------------
hbm_wake:
    mov ax, LOW_SEG             ; 0. every task stack is in LOW_SEG (SPEC.md
    mov ss, ax                  ;    2.1) and [hb_sp] is the frame
    mov sp, [hb_sp]             ;    hbm_perform banked - both just restored
%ifdef DOSR_MARK
    mov ax, 0xB000              ; 'W': the restored kernel has control, which
    mov es, ax                  ; is the one fact the stub cannot report. To
    HBMARKAT HM_WCOL, 'W'       ; BOTH adapters' pages, because WHICH one the
    mov ax, 0xB800              ; staging segment was is the question being
    mov es, ax                  ; asked - and the page that is not there is a
    HBMARKAT HM_WCOL, 'W'       ; black hole rather than a fault
%endif
    cld
%ifdef HMA_DRV
    ; --- 0b. THE HMA'S POOL, BEFORE ANYTHING CAN RUN IN IT (SPEC.md 87.6) ---
    ; The image names its HMA drivers' segments - their drv_tab rows, the
    ; class pairs, a window's W_DISP - and the boot that resumed us has emptied
    ; the pool (drv_shutdown_x at hbm_down). hbm_hsave copied the table and
    ; every live block into [hbm_hseg], which the image carried, so they go
    ; back to the bases they were at, with IF = 0 still: `.cold` and the tail
    ; are this build's again (the fixed head's pad byte) and A20 is as the
    ; fresh XMEM.DRV left it, open. No copy is the old HRESET.
    cmp word [api_coldseg], HMA_SEG
    jne .nohma
    mov bx, [cs:hbm_hseg]
    or bx, bx
    jz .hreset
    mov al, ROMF_HTAB
    call far [rom_mfp]          ; DX:AX = the tail's table, CX = its bytes
    jc .hreset                  ; (hbm_ask asked it already)
    mov bp, 1                   ; ...out of the claim
    call hbm_hcopy
    jmp short .nohma
.hreset:
    mov al, ROMF_HRESET
    call far [rom_mfp]
.nohma:
%endif
    ; --- 1. THE PIT, WHICH THE HANDOVER PUT BACK IN THE ROM'S MODE ----------
    ; SPEC.md 8.1: the kernel runs channel 0 in MODE 2 with divisor 0, so the
    ; IRQ rate stays 18.2065 Hz and the counter decrements by ONE per input
    ; clock - which is what makes `sch_pit_now` and `sch_account`'s
    ; `65536 - count` an elapsed time at all. The ROM's default is mode 3,
    ; where it steps by two and wraps twice a period.
    ;
    ; **SPEC.md 87.6 SAID THIS WAS ALREADY TRUE AND IT WAS TRUE OF ONE ROUTE.**
    ; *"the fresh boot's `sched_init` set it and nothing on the resume path
    ; unset it"* is exactly right after `int 19h`; on the LIVE route there is
    ; no fresh boot and no `sched_init`, and the last thing to touch channel 0
    ; was `hbm_dosrun`'s own step 6, which put the ROM's mode 3 back so that
    ; `kern_dos` would keep BIOS time. Nothing on the way home undid it.
    ;
    ; AND THE DIVISOR IS THE HALF THAT CAN BITE HARD: a DOS game that
    ; reprograms channel 0 for its own timer and exits without restoring it
    ; hands the resumed kernel an IRQ0 rate of its choosing, and everything
    ; the machine measures in ticks is then wrong together. Three instructions
    ; here - `sched_init`'s own - answer both, and on the reboot route they
    ; are idempotent.
    ;
    ; BEFORE THE `sti`, so no IRQ0 lands between the reprogram and the re-seed
    ; below: `sch_account` bills the delta against `[sch_pit_last]`, and a
    ; stamp taken in one mode and spent in another charges a whole tick of
    ; garbage to whichever task was running - `sch_fast_off`'s own reason.
    mov al, 0x34                ; ch0, lo/hi, mode 2, divisor 0 = 65536
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al
    mov bp, sch_pit_now
    call hbm_kd                 ; AX = phase, DX = [ticks]
    mov [sch_pit_last], ax
    mov [sch_pit_last + 2], dx
    ; ...AND THE SPEAKER, WHICH IS THE OTHER THING A DOS PROGRAM CAN LEAVE ON.
    ; `hbm_dosrun` calls `snd_unhook` on the way out - 61h bits 0-1 back to
    ; their boot state - and there is no counterpart on the way in, so a
    ; program that exits mid-tone comes home as a machine that will not stop
    ; buzzing. The kernel's own sound state is the image's and is silent by
    ; construction, so the gate simply goes off.
    in al, 0x61
    and al, 0xFC
    out 0x61, al
    sti                         ; the IVT is the image's; nobody switches
    call hbm_stageseg           ; 2. the clock the boot just read
    mov si, HS_CLK
    mov di, clk_sec
    mov cx, 10
.clk:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .clk
    mov ax, [ticks]
    mov [clk_last], ax
    mov word [clk_acc], 0
    ; --- 2b. ...AND THE TIME SOMEBODY ELSE HAD THE MACHINE (SPEC.md 96.49.5)
    ; The ten bytes above are the FRESH BOOT's on the reboot route and the
    ; HANDOVER's on the live one, where there was no boot to read a clock. The
    ; difference is at 0040:006C, which the stub never restores - file sector 2
    ; is the ROM's - and which has been counting at 18.2065 Hz throughout,
    ; because SPEC.md 8.1's PIT change is a MODE change and `sch_isr` chains
    ; the ROM's handler every tick.
    ;
    ; **`clk_tick` IS THE CATCH-UP AND IT IS ALREADY WRITTEN.** It takes
    ; `[ticks] - [clk_last]`, turns it into whole seconds and carries them
    ; through `clk_inc_sec` one at a time - month lengths, leap years and all -
    ; and its own comment names *"an hour of held-open menu"* as the case it
    ; exists for. So the delta is applied by moving `[clk_last]` BACK under it,
    ; 32,768 ticks at a time: half an hour a pass, which keeps that routine's
    ; `mul`/`div` inside the range it already assumes and adds no arithmetic
    ; here at all. A day is 48 passes.
    mov ax, [es:HS_TICK0]
    mov dx, [es:HS_TICK0 + 2]
    mov bx, ax
    and bx, dx
    inc bx
    jz .clkdone                 ; 0xFFFFFFFF: the clock above is current
    push es
    xor bx, bx
%ifdef KERN_MEGA
    MEGA_SEG0 es                ; (segment 0 is the NULL selector there)
%else
    mov es, bx
%endif
    mov bx, [es:0x046C]         ; the ROM's own tick, now
    mov cx, [es:0x046E]
    pop es
    sub bx, ax
    sbb cx, dx                  ; CX:BX = ticks the kernel was not running for
    jns .clkcat
    add bx, 0x00B0              ; NEGATIVE is midnight: the ROM zeroes the
    adc cx, 0x0018              ; count at 24 hours, so one day - 1,573,040
    js .clkdone                 ; ticks - goes back. A SECOND crossing keeps
                                ; the handover's clock rather than guessing:
                                ; late is a clock, invalid is not
.clkcat:
    mov ax, cx
    or ax, bx
    jz .clkdone
    mov ax, 0x8000              ; ...a pass at a time
    or cx, cx
    jnz .clkpass
    cmp bx, ax
    jae .clkpass
    mov ax, bx
.clkpass:
    sub bx, ax
    sbb cx, 0
    sub [clk_last], ax          ; ...which is exactly what clk_tick reads
    push bx
    push cx
    mov bp, clk_tick
    call hbm_kd                
    pop cx
    pop bx
    jmp short .clkcat
.clkdone:
    mov byte [clk_barq], 1      ; AFTER the catch-up and not before: clk_tick
                                ; TESTS AND CLEARS this byte, so a request
                                ; posted in front of the loop is one the loop
                                ; spends and the bar never repaints
    ; 3. the disk caches are the write's, not the disk's (SPEC.md 87.4) -----
    xor al, al
.drop:
    push ax
    mov bp, dsk_fatw_drop
    call hbm_cb         
    pop ax
    inc al
    cmp al, DVOL_MAX
    jb .drop
    xor ax, ax                  ; (AL is DVOL_MAX here, and spent)
    mov [dsk_mntok], al
    mov [disk_nfiles], ax
    mov [dsk_batch], al
    mov [dskw_batch], al
    mov [dskw_syswr], al
    mov [dskw_isdir], al
    dec ax
    mov [dsk_fatd0], ax         ; 0xFFFF: no FAT sector in either cache
    mov [dsk_fatw0], ax
    mov bp, dsk_rah_flush
    call hbm_cb
    xor ax, ax
    mov [mem_pinseg], ax        ; dsk_xfer's pin, mid-transfer in the image
%ifdef OS88_BIGVOL
    mov [dsk_lbahi], ax         ; ...and its LBA's high word, which only its
%endif                          ; .out spends (SPEC.md 18.7.5)
    ; 3a. THE PROGRAMS' MEMORY IS WHAT WAS WRITTEN (SPEC.md 87.4.1) --------
    ; The same sum over the same records hbm_wrimg took it over, on the
    ; memory the stub has just read back. Here and not in the stub, because
    ; the records are the image's own and so is the figure, and because a
    ; refusal needs the file layer to discard the image: the caches are
    ; dropped above, so it reads the disk as it is. A wrong, truncated or
    ; cross-linked image comes back as somebody else's bytes under a program,
    ; and a session that resumed on them would be a crash at best
    call hbm_psum
    cmp ax, [cs:hbm_psv]
    jne .psbad
    cmp dx, [cs:hbm_psv + 2]
    je .psok
.psbad:
    call hbm_forget             ; the pointer and the image go, so the next
    mov si, hbm_s_psbad         ; boot is an ordinary one rather than the same
    jmp hbm_bye                 ; refusal again
.psok:
    ; ...AND THE EXTENT LIST THE PICTURE WAS TAKEN OVER (SPEC.md 87.6.1).
    ; `mem_pinseg` one level up: a word that was true of the machine that
    ; wrote the image and is meaningless in the machine that reads it - except
    ; that this one is a CLAIM, so leaving it costs 4KB of the arena for the
    ; rest of the session rather than nothing.
    ;
    ; It is the DOS handoff that puts it here. That path claims the list at
    ; step 3, writes the image at step 3b and only copies the list into the
    ; staging area at step 5 (SPEC.md 96.40) - so the picture is taken with
    ; the claim live, and what comes back is a 4KB HELD block over an extent
    ; list nothing will ever read again. The user's route in is ordinary: run
    ; one program with the whole machine, and every DOS arena afterwards is
    ; short by a block sitting in the middle of the heap.
    ;
    ; The guard is not decoration - `hbm_perform`'s own save claims no list at
    ; all, so a plain hibernate's image has [hb_xseg] = 0 and this is the path
    ; every resume but one takes.
    mov dx, [hb_xseg]
    or dx, dx
    jz .noxs
    mov bx, MEM_K_HIB
    mov bp, mem_free_x
    call hbm_cb         
    mov word [hb_xseg], 0
.noxs:
%ifdef HMA_DRV
    call hbm_hfree              ; ...and the HMA pool's copy, spent at 0b
%endif
    call KERNEL_SEG:fpg_end     ; ...and the progress widget it had up
    ; 3c. THE MAILBOX, WHILE THERE STILL IS ONE (SPEC.md 96.41.3) -----------
    ; **THE STAGING AREA IS THE DESKTOP'S FRAMEBUFFER ON TWO ADAPTERS OF
    ; THREE.** It lives in the TEXT page - 0xB800, or 0xB000 on Hercules - and
    ; that is exactly where a CGA's 640x200 and a Hercules' 720x348 desktop
    ; are drawn. Step 4 below sets the mode and paints the whole screen, so by
    ; the time step 5b used to read these two cells they were PIXELS. On VGA
    ; the desktop is at 0xA000 and they survived, which is the whole of why
    ; this was invisible: the field machine is a VGA and the exit code came
    ; home on it every time.
    ;
    ; Read here and POKED at 5b, which is where the wake belongs - the box's
    ; window has to exist and the drivers have to be back. Both routes home
    ; converge on these cells (96.41.1), so one read serves both.
    call hbm_stageseg
    mov ax, [es:HS_DOSCODE]
    mov [hb_doscode], ax
    mov ax, [es:HS_DOSAKB]
    mov [hb_dosakb], ax
    ; 3d. THE MOUSE'S UART, WHICH NOTHING ELSE PUTS BACK (SPEC.md 96.45.2) --
    ; `kd_mou_stop` gives the port back quiet on the way out - IER = 0 and the
    ; line masked - and justifies restoring nothing on the ground that *"the
    ; live restore['s] os8088 runs `mouse_init` again on the way up"*. **IT
    ; DOES NOT.** `mouse_init` is in `.ovl`, which `mem_unblob` handed back at
    ; the end of `kmain`, and a live resume re-enters HERE rather than at a
    ; boot - so the one place in this tree that ever writes IER = 1 cannot run.
    ;
    ; Everything else about the mouse comes home and that is exactly why it
    ; presents as a dead pointer on a working machine: the 8259 mask is
    ; restored (sampled BEFORE kd_mou_stop masked the line), and the IVT is the
    ; image's so `mou_isr` is vectored correctly. The UART simply never asserts
    ; its line again, which is also why the KEYBOARD is unaffected.
    ;
    ; The reboot route runs `mouse_init` and needs none of this, so the three
    ; writes are IDEMPOTENT there and the wake stays one body for both ways
    ; home. ORDER IS THE POINT: the drain is armed first, so that if a DOS
    ; program left MCR low the rising edge below provokes an identify burst
    ; into a window that is already discarding it (SPEC.md 9.4.8) rather than
    ; into the packet decoder; and IER goes on LAST, because until it does the
    ; ISR cannot fire at all.
    mov ax, [ticks]             ; arm the drain exactly as mou_hotplug's raise
    mov [mou_dstamp], ax        ; does - [mou_hpt] is the ceiling's reference
    mov [mou_hpt], ax           ; and [mou_dstamp] the quiet test's
    mov byte [mou_drain], 1
    mov ax, 0x040B              ; MCR: DTR|RTS|OUT2. Normally already this -
    mov bp, mou_pall            ; kern_dos set it and kd_mou_stop leaves it -
    call hbm_kd                 ; so no edge and no burst; a DOS program that
                                ; mangled it is what the drain above is for
    mov ax, 0x0101              ; IER: RX data available only
    mov bp, mou_pall
    call hbm_kd                
    ; ...AND THE LINE, WHICH IS THE OTHER HALF AND WAS MEASURED (96.45.2.1).
    ; `kd_mou_stop` masks the mouse's IRQ as well as clearing IER, and
    ; `kdentry.inc` calls it BEFORE `kd_resume` samples `HS_MASKM` - so the
    ; mask the stub faithfully restores is one that already has our line
    ; masked. That ordering is deliberate and right (an unmasked line with a
    ; latched byte behind it is a wild jump into the next owner's code), so
    ; the unmask belongs HERE, on the way back in, and not over there.
    ;
    ; **THE LINE AND NOT THE BASE** (SPEC.md 9.5.2.1): `[mou_line]` is the bit
    ; the WINNING packets actually came in on, which a card at 2F8 jumpered to
    ; IRQ4 makes a different question from the port's own row. It is 0 on a
    ; machine that never settled, and `not 0` is 0xFF, so the `and` below is
    ; then a no-op and needs no branch to be safe.
    ;
    ; **BUT IT IS 0xFF ON EVERY MACHINE WHOSE POINTER IS ON NO SERIAL LINE**
    ; (KERN-MEGA-PLAN 14.57): MOU_P2LINE, which a PS/2 mouse and a pointing
    ; driver both settle with, is 0xFF precisely so that 9.5.2.1's guard masks
    ; both serial lines - and `not 0xFF` is 0, so this `and` UNMASKED THE
    ; WHOLE MASTER 8259 after every resume on such a machine: IRQ 3, 4, 5 and
    ; 7 open on lines the session had masked (read in Bochs: B8h before the
    ; hibernate, 00h after it). A PS/2 or driver-fed pointer has no line here
    ; to give back, so it skips the block.
    mov cl, [mou_line]
%ifdef KERN_BIG
    cmp cl, MOU_P2LINE
    je .noline
%endif
    not cl
    pushf
    cli
    in al, 0x21
    and al, cl
    out 0x21, al
    popf
%ifdef KERN_BIG
.noline:
%endif
%ifdef KERN_BIG
    ; --- ...AND THE OTHER SOCKET, WHICH IS THE SAME DEFECT WITH THREE
    ; TEARDOWNS INSTEAD OF ONE (SPEC.md 96.45.2.2). Step 6's `mouse_unhook`
    ; calls `mou_p2_off`, which masks IRQ12 at the slave, sends 0xA7 so the
    ; auxiliary interface is not even CLOCKED, and puts the BIOS's command
    ; byte back - IRQ12 bit and all. `mou_p2_init` is the only thing that
    ; undoes any of it and is in `.ovl` beside `mouse_init`, so a PS/2
    ; pointer came home exactly as dead and for exactly the same reason.
    ;
    ; **`[mou_p2]` IS THE RIGHT GUARD AND IT SURVIVES**: `hbm_wrimg` takes the
    ; picture at step 3b and `mouse_unhook` runs at step 6, so the byte the
    ; image carries is the one from BEFORE the teardown - 1 only where a PS/2
    ; mouse was the live pointer. A serial machine runs none of this, and
    ; neither does one whose PS/2 mouse had already LOST the contest, which
    ; `mou_lockon` retires through the same `mou_p2_off`.
    ;
    ; --- ...BUT FIRST THE KEYBOARD, WHICH IS THE 8042'S OTHER HALF AND HAD NO
    ; GUARD AT ALL (SPEC.md 96.45.2.3). The 0xAE at the foot of the block
    ; below only runs where a PS/2 MOUSE was the live pointer, and a DOS
    ; program owns the whole 8042 while it runs: one that leaves the keyboard
    ; interface disabled (0xAD, or bit 4 of the command byte), IRQ1 off in the
    ; command byte (bit 0), or the KEYBOARD told to stop scanning (0xF5) brings
    ; a machine with a PS/2 keyboard and a serial mouse home with a dead
    ; keyboard and nothing to say why. The bank already exists, taken by
    ; `mou_p2_init` at boot, so this puts back EXACTLY what the boot left the
    ; controller holding: `[mou_p2cmdr]` (the BIOS's own byte) where the probe
    ; gave up after reading it, and `[mou_p2cmd0]` where a PS/2 mouse went
    ; live and later lost the contest (`mou_p2_off`'s byte), each behind the
    ; 0xA7 that road sent too. Then the interface on and the keyboard
    ; scanning, its 0xFA collected with IRQ1 masked so `int 09h` never sees
    ; it. Where a PS/2 mouse IS live the block below writes its own command
    ; byte and this skips to the enables. Idempotent on the reboot route,
    ; which POST and `mou_p2_init` left in this state already
    cmp byte [cpu_tier], CPU_8086   ; an XT's keyboard is an 8255 (9.9.2)
    je .kbunm
    pushf
    cli
    in  al, 0x21                ; IRQ1 masked: the 0xFA below is OURS
    or  al, MOU_P2KIRQ
    out 0x21, al
    popf
    cmp byte [mou_p2], 0
    jne .kbon                   ; the block below writes the byte
    cmp byte [mou_p2st], 2
    jb .kbon                    ; the boot never read one: nothing to put back
    mov al, 0xA7                ; the aux port as the boot left it - off
    mov bp, mou_p2cw
    call hbm_kd
    mov al, [mou_p2cmdr]
    cmp byte [mou_p2st], 9
    jb .kbcb
    mov al, [mou_p2cmd0]
.kbcb:
    mov bp, mou_p2wcmd
    call hbm_kd
.kbon:
    mov al, 0xAE                ; the keyboard interface on...
    mov bp, mou_p2cw
    call hbm_kd
    mov al, 0xF4                ; ...and the keyboard scanning (0xF5 undone)
    mov bp, mou_p2dw
    call hbm_kd
    call hbm_kbak               ; its 0xFA, off the buffer before IRQ1 opens
.kbunm:
    pushf                       ; ...AND IRQ1 AT THE 8259 ON EVERY MACHINE: the
    cli                         ; live route's mask is the one kd_resume
    in  al, 0x21                ; sampled AS THE PROGRAM LEFT IT, and a
    and al, 0xFF & ~MOU_P2KIRQ  ; desktop never runs with its keyboard masked
    out 0x21, al
    popf
    cmp byte [mou_p2], 0
    je .nop2
    ; **IRQ12 MASKED AT THE SLAVE FOR THE WHOLE RE-ARM** (KERN-MEGA-PLAN
    ; 14.57). Everything below reads the device's replies off 60h by POLLING
    ; and says so - *"IRQ12 is still masked at the slave"* - which is true of
    ; the live route (mou_p2_off masked it before kd_resume sampled the mask)
    ; and FALSE of the reboot route: hbm_handover samples the FRESH boot's
    ; mask, whose PS/2 mouse is live, and kern_mega's mega_hbwake puts back
    ; the image's, whose mouse was live too. So with IF on since step 1,
    ; `mou_p2_isr` RACED the poll for every ACK of the wheel's knock. On a real
    ; 8042 the IRQ pin follows OBF by a few of the controller's own
    ; instructions, so either side can win any one byte: the poll takes F5's
    ; ACK, the ISR takes F3's, the knock gives up with the DEVICE still
    ; waiting for its sample-rate argument - and the F4 below is spent as
    ; that argument. Reporting stays off: the PS/2 mouse is dead after the
    ; resume and the keyboard is fine (the Victory 286 on kern_mega, 14.56).
    ; QEMU raises IRQ12 with OBF, so its ISR wins every byte: the knock fails
    ; at its first ACK and the wheel is lost (wz 0 on a device in four-byte
    ; mode) rather than the pointer; Bochs raises it a timer tick later, so
    ; its poll wins every byte and nothing shows at all. The unmask at the foot of the block is the one that was always
    ; meant, and mou_p2_init's own probe holds the same mask for the same
    ; reason (its "IRQ12 MASKED BEFORE ANYTHING IS ASKED")
    pushf
    cli
    in  al, 0xA1
    or  al, MOU_P2SL
    out 0xA1, al
    popf
    mov al, 0xA8                ; the auxiliary interface back on - 0xA7 undone
    mov bp, mou_p2cw
    call hbm_kd                
    mov al, [mou_p2cmd0]        ; ...and the command byte mou_p2_init computes,
    and al, 0xDF                ; off the same banked BIOS byte: the aux clock
    or  al, 0x02 | MOU_P2KOFF   ; on, and IRQ12 armed AT THE CONTROLLER
    mov bp, mou_p2wcmd
    call hbm_kd
    ; ...AND THE WHEEL'S KNOCK AGAIN (SPEC.md 9.9.8). A power-off hibernate
    ; brings the device back from POST in three-byte mode, and a DOS mouse
    ; driver may have reset it - and a decoder still counting to four on a
    ; three-byte stream is a pointer gone mad, not one that lost its wheel.
    ; The replies ARE collected here, unlike 0xF4's: IRQ12 is still masked at
    ; the slave and the keyboard is off, so 60h is ours, and the last one is
    ; the ID. Not 3 any more: the decoder goes back to three bytes. The device
    ; is NOT silent here, as it is after the boot's reset: 0xF4 is still in
    ; force (or the DOS driver's), so 0xF5 stops it first, its 0xFA is found
    ; behind any packet already in flight, and 60h is drained before the
    ; replies are read by position - each of which must be 0xFA, as
    ; mou_p2dev insists at boot
    cmp byte [mou_p2wz], 0
    je .nowz
    push si
    push cx
    mov al, 0xD4
    mov bp, mou_p2cw
    call hbm_kd
    mov al, 0xF5                ; reporting off...
    mov bp, mou_p2dw
    call hbm_kd
    mov cx, 8                   ; ...its ACK, BOUNDED: a packet's three or
.f5:                            ; four bytes may come first
    call hbm_p2rd
    cmp al, 0xFA
    loopne .f5                  ; `loop` writes no flag: ZF is the compare's
    jne .wzno
    mov cx, 16                  ; mou_p2flush's drain (that one is the
.dr:                            ; overlay's): 60h empty before the knock
    in  al, 0x64
    test al, 0x01
    jz .drx
    in  al, 0x60
    loop .dr
.drx:
    mov si, hbm_knk
    mov cx, hbm_knk_n
.knk:
    mov al, 0xD4
    mov bp, mou_p2cw
    call hbm_kd
    mov al, [cs:si]             ; THIS IMAGE'S copy: the kernel's, mou_p2knk,
    inc si                      ; went with the boot overlay
    mov bp, mou_p2dw
    call hbm_kd
    call hbm_p2rd               ; its ACK, and nothing else will do
    cmp al, 0xFA
    jne .wzno
    loop .knk
    call hbm_p2rd               ; ...and 0xF2's ID
    cmp al, 3
    je .wzok
.wzno:
    mov byte [mou_p2wz], 0
.wzok:
    pop cx
    pop si
.nowz:
    mov al, 0xD4                ; ...and 0xF4 to the DEVICE, because 0xA7 left
    mov bp, mou_p2cw            ; it unclocked and a device that is not
    call hbm_kd                 ; reporting is a live line with nothing on it.
    mov al, 0xF4
    mov bp, mou_p2dw
    call hbm_kd
    ; **ITS ACK IS COLLECTED, here, with IRQ12 still masked** (KERN-MEGA-PLAN
    ; 14.62). It was left for the ISR to discard - and the next thing this
    ; routine does is step 4's int 10h, which on a 286 is a CPU RESET whose
    ; BIOS can re-initialise the 8259 (mega_r286): an 0xFA landing in that
    ; window has its edge erased, and a byte nobody reads leaves the keyboard
    ; and the mouse dead together. Bounded: hbm_p2rd gives up on its own
    push cx
    mov cx, 4
.f4:
    call hbm_p2rd
    cmp al, 0xFA
    loopne .f4
    pop cx
    mov al, 0xAE                ; ...AND THE KEYBOARD BACK, which is
    mov bp, mou_p2cw            ; mou_p2_init's own last step: the command
    call hbm_kd                 ; byte above is its PROBE-WINDOW value, bit 4
                                ; (MOU_P2KOFF) set, so without this the PS/2
                                ; mouse came home working and the keyboard
                                ; dead - found on a 286 after a DOS run and
                                ; reproduced in Bochs, whose mouse is PS/2 too
                                ; (the Victory 286, KERN-MEGA-PLAN 10.3 item
                                ; 54.3)
    pushf
    cli
    in  al, 0xA1                ; IRQ12 at the SLAVE...
    and al, 0xFF & ~MOU_P2SL
    out 0xA1, al
    in  al, 0x21                ; ...and IRQ2 at the master, the CASCADE, whose
    and al, 0xFF & ~MOU_P2CASC  ; absence is a mouse that passed every step and
    out 0x21, al                ; is then silent for the session (SPEC.md 9.9)
    popf
    mov byte [mou_p2], 1
.nop2:
    ; ...AND THE 8042 LEFT WITH NOTHING STRANDED IN IT, now that the vectors
    ; and the masks are back (KERN-MEGA-PLAN 14.62): the keyboard's half and
    ; the mouse's above leave the controller as the session needs it, and a
    ; byte with no interrupt behind it would still keep both devices dead
    cmp byte [cpu_tier], CPU_8086   ; an XT's 64h is no 8042 (9.9.2)
    je .nokst
    call hbm_kstrand
.nokst:
%endif
    ; 4. the desktop, the way fsx_restore gets it back (SPEC.md 53) - before
    ;    any disk work, so the progress widget has a graphics mode to draw in
    mov bp, vid_setmode
    call hbm_kd                
%ifndef BAKED_FONT
    ; 4a. ...AND THE GLYPH TABLE, which the image names BY ADDRESS (SPEC.md
    ;     6.0.1). The renderers read [font_seg]:[font_base] in a ROM, and the
    ;     image carries the pointer the WRITING machine's boot chose - so
    ;     this asks the reading machine the same question, with the same
    ;     body (FONT_PICK). A heap copy is refilled in place, since its claim
    ;     came back with the image; a ROM pointer is simply replaced. No
    ;     claim is made here: an option ROM found slow only at a resume is
    ;     read in place, which is slower and never wrong. The same TIMED
    ;     pick, so the fastest candidate is still the one taken. Nothing is live across
    ;     it - every register below is reloaded before it is read.
    FONT_PICK hbf               ; DX:BP = this machine's fastest table
    mov ax, [font_seg]
    cmp ax, 0xA000              ; a ROM (C000 up) or the image's heap copy
    jae .fptr
    push ds
    mov es, ax
    mov ds, dx
    mov si, bp
    xor di, di
    mov cx, FONT_BYTES / 2
    rep movsw
    pop ds
    jmp short .fdone
.fptr:
    mov [font_seg], dx
    mov [font_base], bp
.fdone:
%endif
    mov byte [hb_mode], HB_M_GONE   ; before the close: app_close_win's hook
    call hbm_win                ; leaves a GONE window's image to the thunk
    jc .nowin                   ; (SPEC.md 87.1), and this code IS the image
    mov bx, si
    mov bp, app_close_win
    call hbm_kd                
.nowin:
    mov bp, menu_force
    call hbm_kd                
    mov bp, dock_force
    call hbm_kd                
    call KERNEL_SEG:cw_wm_paint_all
    ; 5. the two files are spent, and the drivers come back -----------------
    call hbm_forget
    ; **AND A SUSPEND'S ROWS COME BACK THROUGH THIS DOOR TOO** (SPEC.md
    ; 51.11.5). `[hb_drvmask]` is what `hbm_wrimg`'s sweep found still MOUNTED when it
    ; swept, so a row `OSAPI_DRV_SUSPEND` had ALREADY unmounted is not in it -
    ; and the whole of arm 3 is exactly that shape: `dos_lbfill` suspends the
    ; sound card to build the program's BLASTER= (SPEC.md 96.44.13) long before
    ; the handoff writes the image, so the picture is of a machine that is
    ; missing it and the reload below was told nothing about it.
    ;
    ; The LIVE return is the suspender's to pay and it pays it - the box gets a
    ; `wm_wake` carrying the exit code and answers it with its own resume
    ; (SPEC.md 96.35.5). **THIS DOOR IT CANNOT.** A DOS program that crashes,
    ; or a machine switched off with one running, comes home by a COLD BOOT and
    ; Resume: `[hb_doscode]` is `KDH_NOCODE` there, so the arm below never
    ; reaches `wm_wake` and the box is restored onto the desktop with no wake
    ; ever delivered. Nothing in any package runs, so nothing in any package
    ; can put the card back - the user's machine is simply silent from then on.
    ;
    ; So the two sets are UNIONED here and only here: at the one moment the
    ; whole machine is coming back, which is the moment every set is due.
    ; `hb_susp` stays a word of its own everywhere else for the reason its
    ; declaration gives - a hibernate can be taken while a suspend stands and
    ; the two come back at different moments - and this IS the moment for both.
    ; It is spent as it is read, so the live return's own resume then reloads
    ; nothing rather than `drv_load`-ing a row that is already mounted.
    ;
    ; `hbm_reload_m` and not `hbm_reload`, so `[hb_drvmask]` is not written:
    ; `hbm_hib`'s failure unwind calls `hbm_reload` to undo a detach that never
    ; became an image, and a suspender standing behind THAT one still holds its
    ; rows and is still going to ask for them.
    ;
    ; (The HMA's pool is already the image's: step 0b, before the `sti`.)
    mov dx, [hb_drvmask]
    or dx, [hb_susp]
    mov word [hb_susp], 0
    call hbm_reload_m
    ; 5b. ...and the DOS program's exit code, to the box that asked for it ---
    ; **THIS IS THE ONLY THING THE RETURN ADDS TO THE WAKE**
    ; (docs/plans/KERN-DOS-PLAN.md 8). `[hb_dosseg]`/`[hb_dosoff]` are the
    ; IMAGE's, standing because `hbm_dosrun` took the picture before it spent
    ; the post - so the record the box posted is exactly where it was, and it
    ; gets a number in it. The number itself was banked at 3c: the staging
    ; area is gone by here on two adapters of three (96.41.3).
    ;
    ; The wake is the ordinary one (SPEC.md 74.1): the box already registers a
    ; handler and already services `DST_RAN`, so the way home needed no slot
    ; and no second mechanism.
    mov bx, [hb_dosseg]
    mov word [hb_dosseg], 0     ; **SPENT ON EVERY WAKE, AND THAT IS THE FIX**
                                ; (SPEC.md 96.40.6). It used to be spent only
                                ; on the arm below - the one where a DOS
                                ; program really did come back with a code -
                                ; and `hbm_dosrun` takes the picture with the
                                ; post STANDING, on purpose, so the image
                                ; carries a live latch. Resume that image any
                                ; other way (a cold boot and Resume, where
                                ; `HS_DOSCODE` is `KDH_NOCODE`) and the latch
                                ; came back with it and nothing ever spent it:
                                ; `osapi_dos_handoff_x` refuses a second post
                                ; while one stands, so arm 3 was dead for the
                                ; whole of the restored session. It read as
                                ; "Not enough memory" on a machine with 425K
                                ; free, because the box mislabelled the
                                ; refusal (96.40.6 again)
    mov ax, [hb_doscode]        ; BANKED AT 3c, before the mode change: on a
                                ; 1bpp adapter the staging area is the
                                ; desktop's own framebuffer and step 4 has
                                ; painted over it (SPEC.md 96.41.3) - which is
                                ; also why there is no `hbm_stageseg` here any
                                ; more: nothing in this block reads it
    cmp ax, KDH_NOCODE
    je .nodos
    or bx, bx
    jz .nodos
    mov es, bx
    mov di, [hb_dosoff]
    mov [es:di+KDH_CODE], ax
    mov ax, [hb_dosakb]         ; ...and the arena beside it (SPEC.md 96.41.1)
    mov [es:di+KDH_AKB], ax
    mov bx, [es:di+KDH_WIN]
    or bx, bx
    jz .nodos
    mov bp, wm_wake
    call hbm_kd                
.nodos:
    ; 6. and the machine is the user's again --------------------------------
    mov bp, blk_wake
    call hbm_kd                
    inc word [hb_resumes]
    call hbm_unlock              
    mov byte [sch_lock], 0      ; NOT a dec: the image holds the count as the
                                ; write left it, and dsk_xfer raises it around
                                ; every int 13h - so it reads 2, and one down
                                ; from that is a machine that never switches
    mov sp, [hb_sp]
    ret
%ifndef BAKED_FONT
    FONT_TIMERS hbf             ; step 4a's clock (kernel/font.inc)
%endif

; =============================================================================
; THE STUB (SPEC.md 87.5). Copied to HS_CODE in the text framebuffer and
; entered by a far jump with IF = 0. Position-independent: every jump is
; relative and every datum is at a numeric HS_* offset in its own segment.
; =============================================================================
; =============================================================================
; kds_stub - THE DOS HANDOFF'S STUB (SPEC.md 96.40.1)
; =============================================================================
; It runs with CS, DS and SS in the text framebuffer, on a machine with no
; kernel in it, and does three things: read the part's sectors, expand them
; into `kern_dos`'s segment, and jump.
;
; **IT IS hbs_stub WITH THE HARD PART TAKEN OUT.** A resume reads an image to
; its own linear address and has to put the IVT back LAST, under `cli`, after
; the final `int 13h` - which is the whole of that stub's awkwardness. This
; reads to KD_SEG, which is 0x0060: above the IVT and above the BDA, so
; nothing it writes is anything the ROM is still using and there is no
; special case at all.
;
; What it has that the other does not is a DECODER. The part is `OP_COMP`
; because the 360KB system disk cannot afford it raw
; (docs/reports/KERN-DOS-PART-COST-2026-09-14.md), and the packed stream is
; read to KDS_TMPSEG - a segment ABOVE the image it expands to, so the two
; never meet and the in-place rules of SPEC.md 20.13.7 do not have to be
; reasoned about at all.
; -----------------------------------------------------------------------------
kds_stub:
    mov ax, cs
    mov ds, ax
    mov ss, ax
    mov sp, HS_STACK
    cld
    ; --- 1. the packed stream, one run per int 13h --------------------------
    mov word [HS_K], 0
    mov si, HS_EXT
    mov cx, [HS_NX]
.extent:
    or cx, cx
    jz .expand
    push cx
    mov ax, [si]
    mov [HS_LBA], ax
    mov ax, [si+2]
    mov [HS_LBA+2], ax
    mov ax, [si+4]
    mov [HS_LEFT], ax
.run:
    cmp word [HS_LEFT], 0
    je .nextext
    ; --- CHS of [HS_LBA], hd_chs's one div pair, and the track's remainder --
    mov ax, [HS_LBA+2]
    xor dx, dx
    div word [HS_SPT]
    mov bx, ax
    mov ax, [HS_LBA]
    div word [HS_SPT]           ; DX = sector - 1
    mov cx, [HS_SPT]
    sub cx, dx                  ; CX = sectors to the end of the track
    mov [HS_N], cx
    inc dx
    push dx
    mov dx, bx
    div word [HS_HEADS]         ; AX = cylinder, DX = head
    pop bx                      ; BL = the sector
    mov dh, dl
    mov dl, [HS_UNIT]
    mov ch, al
    mov cl, ah
    ror cl, 1
    ror cl, 1
    and cl, 0xC0
    or cl, bl
    mov [HS_CHS], cx
    mov [HS_CHS+2], dx
    ; --- the run: to the track's end, the extent's end, the DMA page --------
    mov cx, [HS_N]
    cmp cx, [HS_LEFT]
    jbe .cap1
    mov cx, [HS_LEFT]
.cap1:
    mov bx, [HS_K]
    push cx
    mov cl, 5
    shl bx, cl                  ; the destination, k * 32 paragraphs...
    add bx, [KDS_TSEG]          ; ...above the packed stream's base
    mov ax, bx
    and ax, 0x0FFF
    neg ax
    add ax, 0x1000              ; paragraphs to the 64KB page's end...
    shr ax, cl                  ; ...in sectors
    pop cx
    cmp cx, ax
    jbe .go
    mov cx, ax
.go:
    mov [HS_N], cx
    mov es, bx
    xor bx, bx
    mov di, 3                   ; tries
.try:
    mov ax, [HS_N]
    mov ah, 0x02
    mov cx, [HS_CHS]
    mov dx, [HS_CHS+2]
    sti                         ; the ROM wants its interrupts on an AT
    int 0x13
    jnc .ok
    xor ah, ah
    mov dl, [HS_UNIT]
    int 0x13                    ; reset, and again
    dec di
    jnz .try
    jmp .fail
.ok:
    mov ax, [HS_N]
    add [HS_K], ax
    add [HS_LBA], ax
    adc word [HS_LBA+2], 0
    sub [HS_LEFT], ax
    jmp .run
.nextext:
    add si, HS_XSTRIDE
    pop cx
    dec cx
    jmp .extent

    ; --- 2. expand it into kern_dos's own segment ---------------------------
.expand:
    cli
    mov al, [HS_MASKM]          ; the PIC masks go back BEFORE the expansion:
    out 0x21, al                ; it is the longest thing this stub does and a
    cmp byte [HS_SLAVE], 0      ; machine with the timer masted through it
    je .noslave                 ; would lose ticks the ROM counts
    mov al, [HS_MASKS]
    out 0xA1, al
.noslave:
    sti
    ; **KERN_DOS FIRST, AND THE ORDER IS THE WHOLE OF IT** (SPEC.md 96.44.5.4).
    ; `kern_dos`'s image carries a HOLE where the core goes - `times CORE_MAX +
    ; CORE_BSS_SIZE db 0`, which is what reserves the core's address space in
    ; the host that does not contain it - so expanding kern_dos SECOND writes
    ; those zeros straight over the core that had just arrived, and the machine
    ; jumps into a table of `jmp near 0`. The other order costs nothing at all:
    ; neither stream is read twice and neither lands anywhere different.
%ifndef KERN_MEGA
    mov ax, KD_SEG
%else
    ; **WHICH kern_dos, AND SO WHERE** (SPEC.md 96.40.9.2): the box posted
    ; the HMA image (FFFF:0010) or the LOW one (0060:0000) and says which in
    ; the launch block it staged. Picked once and kept in two cells, because
    ; the expansion, the launch block's copy and the jump all want it
    mov ax, KD_SEG
    mov dx, KD_ORG
    test word [KDS_LB + KDL_FLAGS], KDLF_LOWDOS
    jz .kdhma
    mov ax, KD_LSEG
    xor dx, dx
.kdhma:
    mov [KDS_KSEG], ax
    mov [KDS_KORG], dx
%endif
    mov es, ax
    mov cx, [KDS_PLEN]          ; ONE BLOCK EACH, because os88pkg.py caps a
    mov ax, [KDS_TSEG]          ; part at 64KB (SPEC.md 20.12)
    add ax, [KDS_CDELTA]        ; ...and kern_dos's stream begins that many
    push ds                     ; paragraphs into the run the extents brought
    mov ds, ax                  ; in, the core's occupying the front of it
    xor si, si
%ifndef KERN_MEGA
    call kds_expand             ; DS:SI -> ES:0, CX packed bytes
%else
    mov di, [cs:KDS_KORG]       ; ...to FFFF:0010 on kern_mega: the HMA, and
    call kds_expand_at          ; A20 is open (the kernel leaves it so) - or
%endif                          ; to 0060:0000, the low image
    pop ds
    ; --- ...and then the core, into the hole kern_dos just left -------------
%ifndef KERN_MEGA
    mov ax, [KDS_CORG]          ; CORE_ORG, which is 512-aligned, so the shift
    mov cl, 4                   ; is exact and there is no intra-paragraph
    shr ax, cl                  ; remainder for kds_expand's DI to carry
    add ax, KD_SEG
    mov es, ax
%endif
    mov cx, [KDS_CPLEN]
    push ds
    mov ds, [cs:KDS_TSEG]       ; the core's stream IS the run's own base
    xor si, si
%ifndef KERN_MEGA
    call kds_expand
%else
    mov di, [cs:KDS_CORG]       ; FFFF:CORE_ORG: the segment cannot move up
    call kds_expand_at          ; past FFFF, so the OFFSET carries it - and
%endif                          ; 0060:CORE_ORG is the same place as kern_big's
    pop ds

    ; --- 3. the launch block, into the image that just arrived --------------
    ; kern_dos's first eight bytes are a fixed header (kerndos/kdlayout.inc):
    ; a near jump and a word saying where the block goes. The stub has no
    ; symbol table, which is why that word is there.
%ifndef KERN_MEGA
    mov ax, KD_SEG
    mov es, ax
    mov di, [es:KD_ORG + KD_H_LBP]
    mov cx, [es:KD_ORG + KD_H_LBSZ]
%else
    mov es, [cs:KDS_KSEG]
    mov bx, [cs:KDS_KORG]
    mov di, [es:bx + KD_H_LBP]
    mov cx, [es:bx + KD_H_LBSZ]
%endif
    shr cx, 1
    mov si, KDS_LB
    push ds
    push cs
    pop ds
    rep movsw
    pop ds
%ifndef KERN_MEGA
    jmp KD_SEG:KD_ORG           ; ...and away. Its header jumps to the entry
%else
    push word [cs:KDS_KSEG]     ; ...and away, to whichever image it is
    push word [cs:KDS_KORG]
    retf
%endif

.fail:
    mov ah, 0x02                ; the cursor to row 8 first, hbs_stub's reason:
    xor bh, bh                  ; rows 0-2 of this page ARE the stub
    mov dx, 0x0800
    int 0x10
    mov si, kds_s_fail - kds_stub
.ch:
    mov al, [si]
    or al, al
    jz .key
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    inc si
    jmp short .ch
.key:
    xor ah, ah
    int 0x16
    int 0x19
    jmp $

; --- kds_expand: one LZ4 stream, kernel/lz.inc's lz_lz4 with no bounds -------
; in:  DS:SI = the stream, T WORD FIRST; ES = where it expands to, DI free;
;      CX = the stream's PACKED bytes
;
; **THE STREAM IS NOT A CLASSIC LZ4 BLOCK AND THIS IS THE THIRD READER OF THE
; ONE THAT SHIPS** (SPEC.md 20.13.7): a T word, then the symbols cut at the
; first peak of (produced - consumed), then T bytes of RAW TAIL that go across
; as they are. `os88lz.py` writes it, `kernel/lz.inc` reads it, and this reads
; it again because the kernel is gone by the time this runs. A classic decoder
; on one of these streams does not fail - it reads the T word AS A TOKEN, and
; `05 00` is "copy nine bytes from 0xFC00 back", so the image lands NINE BYTES
; along with the header still holding whatever was there before. That is what
; it did, and the machine jumped into it.
;
; UNBOUNDED, for kz_expand's reason one file along: what it produces is the
; thing that is about to be jumped into, and a bound cannot make a wrong
; expansion safe. What can is that tests/kdpart.py has already checked, on the
; host, that these exact sectors expand to these exact bytes.
;
; The two bounds live in the STAGE's own scratch rather than in registers -
; `HS_LBA` and `HS_LEFT` are the disk walk's and it has finished - and they are
; read through `cs:` because the match copy below owns DS.
kds_expand:
    xor di, di
kds_expand_at:                  ; ...or from DI (kern_mega's HMA image)
    add cx, si                  ; the stream's end...
    mov ax, [si]
    add si, 2                   ; ...past the T word
    sub cx, ax                  ; ...and so where the raw tail begins: the ONLY
    mov [cs:HS_LBA], cx         ; source bound the loop tests
    mov [cs:HS_LEFT], ax        ; ...and how much of it there is
.seq:
    cmp si, [cs:HS_LBA]
    jae .tail
    mov al, [si]                ; the token: literal length in the high nibble,
    inc si                      ; match length - 4 in the low one.
    mov bl, al                  ; **THE STRING FORM IS REFUSED IN A MODULE
                                ; IMAGE** (SPEC.md 2.8.6): a module's own data
                                ; is CS-relative and os88ovlchk cannot see that
                                ; this body is COPIED OUT of the module before
                                ; it runs. Two bytes a site, and hbs_stub reads
                                ; its own string the same way
    mov cl, 4
    shr al, cl
    xor ah, ah
    mov cx, ax
    cmp al, 15
    jne .lits
.lx:
    mov al, [si]
    inc si
    xor ah, ah
    add cx, ax
    cmp al, 255
    je .lx
.lits:
    rep movsb                   ; CX = 0 is legal and moves nothing
    cmp si, [cs:HS_LBA]
    jae .tail                   ; the symbols never end on a literal run in
                                ; this format - the tail is what ends it
    mov ax, [si]                ; the match offset
    add si, 2
    mov bp, ax
    mov al, bl
    and al, 0x0F
    xor ah, ah
    mov cx, ax
    cmp al, 15
    jne .mat
.mx:
    mov al, [si]
    inc si
    xor ah, ah
    add cx, ax
    cmp al, 255
    je .mx
.mat:
    add cx, 4                   ; the minimum match, which the token omits
    push ds
    push si
    mov si, di
    sub si, bp
    push es
    pop ds                      ; a match reads what we have already written
    rep movsb
    pop si
    pop ds
    jmp short .seq
.tail:
    mov cx, [cs:HS_LEFT]        ; SI is standing on it
    rep movsb
    ret

kds_s_fail: db 13, 10, 'kern_dos could not be read off the disk.', 13, 10
            db 'Press any key to restart.', 13, 10, 0
kds_stub_end:
%if kds_stub_end - kds_stub > HS_CODE_MAX
  %error "hiber: the DOS handoff stub is bigger than its staging slot"
%endif

%include "hbstub.inc"         ; ...and the stub's body, in THIS host's
                              ; `.modh` - the section is the includer's
%ifdef KERN_MEGA
%include "megapark.inc"       ; ...and kern_mega's PARK stub, which the kernel
                              ; copies out of this image (kernel/megados.inc)
%endif

; =============================================================================
; The image's own data (SPEC.md 2.8.6): strings read through CS, and the
; pointer's buffer, which dskw_write and dskw_read reach as ES:BX.
; =============================================================================
; --- the DOS handoff's record, copied out of the poster's segment ------------
; MODULE DATA and not kernel bss: 543 bytes that only exist while the module
; is resident, which is the whole economy of osapi_dos_handoff_x keeping a far
; POINTER (SPEC.md 96.40).
hbm_s_d1:       db 'Not our handoff record', 0
hbm_s_d2:       db 'That folder has moved', 0
hbm_s_d3:       db 'DOS.O88 is not there now', 0
hbm_s_d4:       db 'The ROM cannot read it', 0
hbm_s_d5:       db 'No room for the extents', 0
hbm_s_d6:       db 'kern_dos too scattered', 0
%ifdef OS88_IMG
hbm_s_dimg:     db 'Unmount the image first', 0
%endif
%ifdef KERN_MEGA
hbm_s_d7:       db 'No room above 1MB', 0
hbm_s_d8:       db 'Could not save to disk', 0   ; 14.43's image
hbm_s_mode:     db 'Hibernation: other mode', 0
hbm_s_clus:     db 'Disk clusters too small', 0  ; 512 bytes, protected mode
%endif

hbm_ptrfix:     db 'HIB1'      ; THE POINTER FILE as hbm_wrimg writes it,
                dw BUILD_NUM    ; and its fixed head as hbm_ask compares it -
                dw MOD_STAMP    ; described ONCE (SPEC.md 87.3)
                dw 0            ; [mem_top], filled by hbm_ptrfix_set
                db 0, 0         ; ...[vid_kind], and the pad
                dd 0            ; HBP_WAKE, hbm_wrimg's
                db 0, 0         ; HBP_DOS, hbm_wrimg's; and HBP_PAD, the
                                ; HMA pool's table bytes the image carries
                                ; (hbm_hsave), 0 = none - which hbm_ask asks
                                ; the tail about before the teardown
%ifdef KERN_MEGA
                db 0, 0         ; HBP_XFLG and a pad, hbm_wrimg's
                dd 0, 0         ; HBP_XSUM and HBP_XLEN, hbm_xwrite's
%endif
%if $ - hbm_ptrfix != HBP_SIZE
  %error "hbm_ptrfix is the pointer file, HBP_SIZE bytes"
%endif

hbm_lines_ask:  dw hbm_s_a1, hbm_s_a2, hbm_s_a3, 0
hbm_lines_busy: dw hbm_s_b1, 0
hbm_lines_res:  dw hbm_s_r1, hbm_s_r2, hbm_s_r3, 0

hbm_s_a1:   db 'Save everything in memory to', 0
hbm_s_a2:   db 'C:\HIBERNAT.IMG and stop the machine?'
HBM_A2_LTR  equ 0
            db 0
hbm_s_a3:   db 'Restarting offers to resume it.', 0
hbm_s_b1:   db 'Hibernating...', 0
hbm_s_r1:   db 'A hibernation file is on drive '
HBM_R1_LTR  equ $ - hbm_s_r1
            db 'C:.', 0        ; 34 cells: the content is 39 wide
hbm_s_r2:   db 'Resume where you left off, or', 0
hbm_s_r3:   db 'discard it and start normally?', 0

hbm_s_bhib: db 'Hibernate', 0      ; the ASK face's two labels, then the
hbm_s_bcan: db 'Cancel', 0, 0, 0, 0 ; RESUME face's two at one distance on
hbm_s_bres: db 'Resume', 0, 0, 0, 0 ; from each - hbm_btnrect adds it rather
hbm_s_bdis: db 'Discard', 0         ; than choosing between four (NULs pad)
%if hbm_s_bdis - hbm_s_bcan != hbm_s_bres - hbm_s_bhib
  %error "hbm_btnrect: each RESUME label is one fixed distance past its ASK label"
%endif

hbm_s_nobios: db 'Needs a BIOS hard disk', 0
hbm_s_noroom: db 'Not enough room on disk', 0
hbm_s_failed: db 'Hibernate failed', 0
hbm_s_serial: db 'Disconnect serial before hibernating',0
%ifdef HMA_DRV
hbm_s_nomem:  db 'Not enough memory', 0  ; the HMA pool's copy (87.4 step 3b)
%endif
hbm_s_stale:  db 'Hibernation: other build', 0
hbm_s_lost:   db 'Hibernation: unreadable', 0

hbm_psv:    dw 0, 0             ; the programs' sum, s1 and s2 (hbm_psum):
                                ; written into this image by hbm_wrimg and
                                ; read back out of the restored one
hbm_s_psbad: db 13, 10, 'Resume failed: the programs', 39, ' memory did not read', 13, 10
            db 'back as it was saved. The hibernated session has been', 13, 10
            db 'deleted.', 13, 10, 13, 10
            db 'Press any key to restart.', 13, 10, 0
hbm_s_off:  db 13, 10, 'os8088 has hibernated.', 13, 10, 13, 10
            db 'It is now safe to switch the machine off.', 13, 10
            db 'Press any key to restart it.', 13, 10, 0

; --- THE IMAGE'S OWN BSS (docs/plans/MODULE-SELFCONTAIN-PLAN.md 3.1) --------
; `nobits`, in the tail of the module's own claim: mod_need rounds the claim up
; to whole KB and kernel.asm asserts this fits that rounding, so none of it is
; on the floppy and none of it costs heap. **NOT ZEROED** - MOD_BSS is
; kern_small's alone - so every cell here is written before it is read, and
; the one flag that was relying on the image's zero (hbm_dosheld) is cleared
; at hbm_dosrun's head. Read and written through `cs:` like the image's data:
; the section follows `.modh` in the same claim. SCRATCH only, never state
; that has to outlive a mod_drop (MODULE-SELFCONTAIN-PLAN 3.4) - the ptrbuf
; carries the wake address from hbm_ask to hbm_res, and the module is never
; dropped between those two (hb_mode stays RESUME, not GONE).
section .modhb
hbm_dosrec:     resb KDH_SIZE   ; the DOS handoff's record, copied out of the
                                ; poster's segment: 561 bytes that exist only
                                ; while the module is resident (SPEC.md 96.40)
hbm_ptrbuf:     resb HBP_SIZE   ; the pointer file, as read or as written
hbm_xskip:      resw 1          ; hbm_extat: sectors still to step over...
hbm_xtake:      resw 1          ; ...and still to take
hbm_nextlo:     resw 1          ; the LBA the run in hand ends at
hbm_nexthi:     resw 1
hbm_xlim:       resw 1          ; ...and the byte the extent list stops at
hbm_dosheld:    resb 1          ; 1 = hbm_wrimg took the gfx lock and [sch_lock]
                                ; and still holds them, so step 4 must not
hbm_swn:        resb 1          ; hbm_sweep's record count, across the ask
hbm_spm:        resb 1          ; OSAPI_DRV_SUSPEND's class mask (SPEC.md
                                ; 51.11.4), banked by hbm_drvsusp off the
                                ; slot's own BL - the kernel keeps nothing
%ifdef HMA_DRV
hbm_hseg:       resw 1          ; the HMA pool's copy (hbm_hsave), 0 = none:
                                ; written before the picture, so the restored
                                ; hbm_wake reads what the hibernate wrote -
                                ; hbm_ptrbuf's reasoning, one boot further
%endif

%endif                          ; --- kern_small has NO hibernate at all,
                                ; and so no module and no mod_tab row

section .text                   ; every module ends where the next include
                                ; expects to begin (SPEC.md 1)
