; =============================================================================
; os8088 - RAMDISK.DRV
;
; A DRVC_FILE driver (SPEC.md 62.9): a volume whose contents come from
; somebody ANSWERING QUESTIONS ABOUT FILES rather than about sectors. It has
; no BPB, no FAT, no directory to walk and no int 13h behind it at all -
; dsk_xfer refuses it outright - and the Disk window, the desktop zone, the
; sort, the `..` synthesis, the loader, the Standard File dialog and the copy
; engine all work on it unchanged.
;
; IT WAS THE REDIRECTOR'S HARNESS AND IT STILL IS. SPEC.md 62.9.3 is explicit:
; the harness for the redirector is a RAM DISK and not NET.DRV, because a RAM
; disk needs no hardware and so every branch site the redirector added to the
; kernel runs on a cycle-accurate 8088 in a container - which block mode never
; had, and which is why two of ITS bugs reached the field. Six more have been
; found here since.
;
; WHAT SPEC.md 62.9.9 ADDS IS THE OTHER HALF: a RAM disk worth having on the
; machine. That is a different set of questions - how big, where the store
; lives on a machine with memory above 1MB, and how anything put on it
; survives the power going off - and it is why this file is now five:
;
;   rdabi.inc    what this image and RAMPAGE.DRV both have to agree about
;   rdstore.inc  the store: chained extents, conventional or extended
;   rdfsv.inc    the directory and the FSV_* verbs
;   rdimg.inc    the settings, the mount, and the volume as one file
;   rdpage.inc   loading the page image, and the calls each way through it
;
; TICKING THE ROW MOUNTS NOTHING (SPEC.md 62.9.13). Attach claims no memory
; and adds no volume; the Control Panel's Ram Disk page is what does both, and
; DRVV_READY only restores a preserved image if one was asked for.
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1). Every string
; instruction in this tree therefore loads its own segments and puts them
; back; a bare `rep stosw` here writes into the kernel's own image, which is
; the trap apps/modplug shipped with until it was caught.
; =============================================================================

%include "os88drv.inc"
%include "rdpkg.inc"            ; the package ABI - the same file the PACKAGE
                                ; includes, so the two ends cannot drift

    OS88_DRIVER 'Ram Disk', DRVC_FILE, rd_entry

%include "rdabi.inc"

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2). DSV_BLK is 0 and that is the point: this
; volume has no sectors, so there is nothing for the kernel to ask for.
;
; THE PAGE CELLS ARE TRAMPOLINES (SPEC.md 62.9.9): the page's body is a second
; image read off the system volume when it is first painted. DSV_CPNAME is the
; one thing that has to stay resident, because cp_list draws it with font_str
; and the kernel stages it at attach.
; -----------------------------------------------------------------------------
rd_svc:
    dw 0                        ; DSV_CAPS    - sound's, not ours
    dw 0                        ; DSV_FM
    dw 0                        ; DSV_STREAM
    dw 0                        ; DSV_TICK
    dw 0                        ; DSV_RELINST
    dw rd_name                  ; DSV_NAME
    dw 0                        ; DSV_TONE
    dw 0                        ; DSV_TIERS
    dw 0                        ; DSV_BLK     - NO SECTORS (SPEC.md 62.9)
    dw rd_name                  ; DSV_CPNAME  - the Control Panel row's label,
    dw rd_page_paint            ; DSV_CPPAINT   and the three cells behind it,
    dw rd_page_click            ; DSV_CPCLICK   each of which is a trampoline
    dw rd_page_closed           ; DSV_CPCLOSE   into RAMPAGE.DRV
    dw rd_fsv                   ; DSV_FS      - ...and the verbs
    dw rd_page_key              ; DSV_CPKEY   - ...and the one page in the
                                ; machine that can be typed into (SPEC.md
                                ; 31.9.2), which is a fourth trampoline
    dw rd_page_up               ; DSV_CPUP    - the page acts on the RELEASE
    dw rd_page_drag             ; DSV_CPDRAG  - ...and its pressed control
                                ; follows the pointer (SPEC.md 13.8.4)
    dw rd_pkg                   ; DSV_PKGCALL - and a PACKAGE may call us
                                ; (SPEC.md 20.11). This driver is the package
                                ; door's HARNESS as much as it is software -
                                ; tests/drvcall drives it with no card and no
                                ; cable, which is the only way the fence and
                                ; the ES contract get exercised in a container
    times DSV_SIZE - ($ - rd_svc) db 0
                                ; ...AND THE PAD, which is not decoration:
                                ; drv_publish copies DSV_SIZE bytes whatever
                                ; the table's length, so a table that stops
                                ; short hands the kernel whatever FOLLOWS it
                                ; as a published cell. This one stopped at
                                ; DSV_CPKEY and rd_fsv is the next label, so
                                ; when SPEC.md 13.8.4's two cells landed the
                                ; kernel read `rd_list` as DSV_CPUP and a
                                ; release on this page would have far-called
                                ; the FILE LISTER as a mouse handler.
                                ; sound.asm and debug.asm had the idiom
                                ; already; it is the one that self-heals when
                                ; DSV_SIZE grows again

; --- the FSV_* table DSV_FS points at, in OUR segment (SPEC.md 62.9.1) -------
rd_fsv:
    dw rd_list                  ; FSV_LIST
    dw rd_chdir                 ; FSV_CHDIR
    dw rd_stat                  ; FSV_STAT
    dw rd_read                  ; FSV_READ
    dw rd_write                 ; FSV_WRITE
    dw rd_append                ; FSV_APPEND
    dw rd_readat                ; FSV_READAT
    dw rd_delete                ; FSV_DELETE
    dw rd_rename                ; FSV_RENAME
    dw rd_mkdir                 ; FSV_MKDIR
    dw rd_rmdir                 ; FSV_RMDIR
    dw rd_dfree                 ; FSV_DFREE
    dw rd_enum                  ; FSV_ENUM
    dw 0                        ; FSV_COPY - DELIBERATELY NOT IMPLEMENTED.
                                ; 62.9.8's whole safety property is that a
                                ; driver may decline and fcp_xfer streams as
                                ; before, and the only way to TEST that is a
                                ; DRVC_FILE volume which declines. A RAM disk
                                ; copy is a memcpy either way, so there is no
                                ; win here to give up - and this is the
                                ; harness for the branch rather than a user
                                ; of it. The row still exists because the
                                ; table is indexed by verb and dispatching
                                ; past its end is a wild far call
    dw 0                        ; FSV_RMTREE - DECLINED, on the same reasoning
                                ; and for the same job (SPEC.md 62.10.7). The
                                ; kernel's fallback for an unpublished verb is
                                ; the empty-only FSV_RMDIR, which is what every
                                ; redirected volume did before the verb
                                ; existed, and this is the only DRVC_FILE
                                ; driver that can exercise it
rd_fsv_end:
%if rd_fsv_end - rd_fsv != FSV_SIZE
  %error "ramdisk: the FSV_* table is not FSV_SIZE bytes - a swallowed row?"
%endif

rd_name:    db 'Ram Disk', 0
rd_label:   db 'RAM', 0         ; DV_LBL is 7 bytes inline (SPEC.md 18.7.3)
rd_s_nopage: db 'Ram Disk needs the system disk', 0

; =============================================================================
; ENTRY
; =============================================================================
rd_entry:
    cmp al, DRVV_ATTACH
    je rd_attach
    cmp al, DRVV_DETACH
    je rd_detach
    cmp al, DRVV_READY
    je rd_ready
    clc                         ; a verb we do not implement is not an error
    ret

; -----------------------------------------------------------------------------
; DRVV_ATTACH - there is no hardware, so this cannot fail
; out: CF=0 and SI = the service table
;
; ALL-OR-NOTHING (SPEC.md 51.6 rule 1) is trivially honoured: nothing is
; probed, nothing is hooked, no vector is taken AND NO MEMORY IS CLAIMED. The
; store used to be claimed at DRVV_READY and the volume mounted there with it;
; both are the page's now (SPEC.md 62.9.13), because the size and the location
; are questions that can only be answered before the store is claimed.
; -----------------------------------------------------------------------------
rd_attach:
    mov byte [rd_cwd], 0
    call rd_dir_clear
    call rd_ext_clear
    mov si, rd_svc
    clc
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - the earliest point a fence keyed on our publication answers
;              (SPEC.md 51.2.2)
;
; Three things, and only the last of them touches a disk. The system volume is
; banked for RAMPAGE.DRV while drv_load's bracket still has us standing on it
; (SPEC.md 62.9.9); the settings come out of SYSTEM.CFG's blob; and if they
; ask for it, a preserved image is restored and mounted.
;
; A FAILED RESTORE IS QUIET (SPEC.md 62.9.12) - the disk the image was on may
; simply not be in the drive, and a machine that boots to a notice because a
; RAM disk could not be restored is worse than one that boots without it. The
; code is banked and the page reports it when it is next opened.
; -----------------------------------------------------------------------------
rd_ready:
    call rd_page_home
    call rd_cfg_load
    call rd_boot_restore
    clc                         ; a refused restore is the page's to report,
    ret                         ; not a reason to fail the attach

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1)
;
; The kernel drops our volume itself before it frees the image, and since
; DV_CLASS it drops OURS rather than every driver-backed row (SPEC.md 51.8) -
; but the store, the bounce, the extended block and the page image are all
; given back explicitly here, because a claim owned by a segment that is about
; to be freed is exactly the thing SPEC.md 51.6 says to hand over before
; returning rather than after.
; -----------------------------------------------------------------------------
rd_detach:
    call rd_page_drop
    call rd_unmount
    clc
    ret

%include "rdstore.inc"
%include "rdfsv.inc"
%include "rdimg.inc"
%include "rdpage.inc"

; =============================================================================
; THE SEED TREE - `make ramseed` only (SPEC.md 62.9.5.1)
;
; Two levels of directory, three text files and a copy of MINES.O88, laid into
; the store at MOUNT rather than at attach, because that is where a store now
; exists to lay them into. It is OFF by default: the files cost the shipped
; floppy 1.8KB of driver image to carry a package it already has.
;
; MINES.O88 is the real package, embedded from build/ (the Makefile passes
; -I build). It is what makes "a package launches off a redirected volume" a
; thing that can be clicked rather than argued - and it proves 62.9.2's other
; half by accident: MINES has an EMBEDDED ICON and draws with 25's generic one
; here, because a redirected volume has no first sector to harvest from.
;
; NEVER END A LINE IN THE TABLE BELOW WITH A BACKSLASH: NASM treats a trailing
; `\` as a LINE CONTINUATION even inside a comment, so an ASCII-art brace down
; the right-hand side silently swallows the row below it. That shipped here
; once and presented as an unrelated window refusing to open (SPEC.md 62.9.4).
; =============================================================================
%ifdef RDSEED
rd_d_readme:
    db 'This volume has no sectors.', 13, 10
    db 'Its contents come from RAMDISK.DRV answering', 13, 10
    db 'questions about FILES - no BPB, no FAT, and', 13, 10
    db 'no int 13h anywhere behind it (SPEC.md 62.9).', 13, 10
rd_d_readme_end:

rd_d_notes:
    db 'Read by OSAPI_FILE_READ, which reaches', 13, 10
    db 'dskw_rbody, which asks FSV_STAT for a handle', 13, 10
    db 'and then FSV_READ for the bytes.', 13, 10
rd_d_notes_end:

rd_d_hello:
    db 'One level down, in DOCS.', 13, 10
rd_d_hello_end:

rd_d_bottom:
    db 'Two levels down, in DEEP. Going up from here', 13, 10
    db 'has to land on DOCS, which is what FSV_CHDIR', 13, 10
    db 'answering the parent handle buys.', 13, 10
rd_d_bottom_end:

rd_d_mines:
    incbin "mines.o88"
rd_d_mines_end:

; A SEED ROW IS NOT A DIRECTORY ROW, and it has its own offsets for exactly
; that reason: where a live row carries an extent byte and a 32-bit size, a
; seed carries a POINTER into this image and a 16-bit length. They share a
; stride and a name field and nothing else, and the first version of this read
; the seed's data pointer through RDE_SIZE - which is a length of 0x0123 for a
; file whose content happens to start there.
RDS_DATA    equ 4               ; dw: the content, an offset in our image
RDS_SIZE    equ 6               ; dw: how much of it

%macro RD_SEED 5                ; parent handle, type, data, size, name
%%row:
    db %1, %2, 0, 0
    dw %3
    dw %4
    db %5, 0
    times RDE_SZ - ($ - %%row) db 0
%endmacro

; The parent column is a HANDLE, and a handle is `row index + 1`, so DOCS is
; 1 and DEEP is 5 - which is why these are laid out parents-first.
rd_seed:
    RD_SEED 0, 2, 0, 0, 'DOCS'                                      ; -> 1
    RD_SEED 0, 0, rd_d_readme, rd_d_readme_end - rd_d_readme, 'README.TXT'
    RD_SEED 0, 0, rd_d_notes,  rd_d_notes_end  - rd_d_notes,  'NOTES.TXT'
    RD_SEED 0, 1, rd_d_mines,  rd_d_mines_end  - rd_d_mines,  'MINES.O88'
    RD_SEED 1, 2, 0, 0, 'DEEP'                                      ; -> 5
    RD_SEED 1, 0, rd_d_hello,  rd_d_hello_end  - rd_d_hello,  'HELLO.TXT'
    RD_SEED 5, 0, rd_d_bottom, rd_d_bottom_end - rd_d_bottom, 'BOTTOM.TXT'
rd_seed_end:
RD_NSEED    equ (rd_seed_end - rd_seed) / RDE_SZ
%if RD_NSEED > RD_MAXENT
  %error "ramdisk: more seeds than directory rows"
%endif
%if rd_d_mines_end - rd_d_mines > RD_MINKB * 1024
  %error "ramdisk: the seeded package does not fit the smallest store"
%endif

; Every row starts free (rd_dir_clear ran first), then the seeds are laid in
; ORDER so that row i is seed i and the handles the seed table's parent column
; names come out right.
rd_seed_all:
    push si
    push di
    push cx
    mov si, rd_seed
    mov di, rd_dir
    mov cx, RD_NSEED
.one:
    push cx
    call rd_seed_one
    pop cx
    add si, RDE_SZ
    add di, RDE_SZ
    loop .one
    pop cx
    pop di
    pop si
    ret

; rd_seed_one - SI = a seed row, DI = its directory row
rd_seed_one:
    push ax
    push bx
    push cx
    push dx
    mov al, [si+RDE_PAR]
    mov [di+RDE_PAR], al
    mov al, [si+RDE_TYPE]
    mov [di+RDE_TYPE], al
    mov word [di+RDE_EXT], RD_NOEXT     ; a WORD now, and RD_NOEXT is 0xFFFF
    mov word [di+RDE_SIZE], 0
    mov word [di+RDE_SIZE+2], 0
    push si
    push di
    add si, RDE_NAME
    add di, RDE_NAME
    call rd_ncopy               ; DS:SI -> DS:DI, NUL and all
    pop di
    pop si
    mov cx, [si+RDS_SIZE]       ; the SEED's offsets, not a live row's
    or cx, cx
    jz .out                     ; a folder: no extent, no content
    push si
    mov si, di                  ; rd_chain_fit takes the ROW in SI and DX:CX
    xor dx, dx                  ; as the count - a seed is well under 64KB, so
    call rd_chain_fit           ; the high half is genuinely zero
    pop si
    jc .out                     ; a seed that will not fit simply is not
                                ; there, which is better than half there
    mov [di+RDE_SIZE], cx
    mov byte [rd_io_dir], 1     ; caller -> store, and rd_io takes its nine
    mov ax, [di+RDE_EXT]        ; words in MEMORY (SPEC.md 62.9.10) - the
    mov [rd_io_ext], ax         ; extent read back AFTER rd_chain_fit set it
    mov word [rd_io_off], 0
    mov word [rd_io_off+2], 0
    mov [rd_io_len], cx
    mov word [rd_io_len+2], 0
    push cs
    pop ax
    mov [rd_io_seg], ax         ; the content, in this image
    mov ax, [si+RDS_DATA]
    mov [rd_io_ofs], ax
    mov byte [rd_io_prog], 0
    call rd_io
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; =============================================================================
; rd_pkg - DSV_PKGCALL: a PACKAGE calling us (SPEC.md 20.11, rdpkg.inc)
; in:  BL = an RDPV_* verb, BH = DRVC_FILE (the class the kernel matched);
;      ES = THE CALLING PACKAGE'S SEGMENT, which no other entry point here
;      gets - everything else in this file is handed KERNEL_SEG in ES
; out: per verb. CF=1 with AX=0 for a verb we do not have, which is the same
;      answer the kernel gives for a driver that is not there: a package
;      testing CF alone cannot tell those apart and does not need to
;
; A NEAR PROC WITH A NEAR ret, like every other callback in a driver - the
; kernel reaches it through the header's dispatcher (SPEC.md 51.2), so there
; is no retf anywhere in this file and there must not be one here.
; =============================================================================
rd_pkg:
    cmp bl, RDPV_MAX
    ja .none                    ; an unchecked verb into a jump table is a
                                ; wild near call. The table is two entries and
                                ; this is a compare, so it is a ladder instead
    or bl, bl
    jnz .upcase

    mov ax, RD_SIG              ; RDPV_IDENT. NASM puts the FIRST character in
    clc                         ; the LOW byte, so a caller storing AX stores
    ret                         ; 'D' then 'R' and the string reads DR

.upcase:                        ; RDPV_UPCASE: ES:SI, CX -> AX, in place
    push si
    push cx
    xor ax, ax
    jcxz .done
.walk:
    mov dl, [es:si]             ; ...THROUGH ES, and that is the whole test:
    or dl, dl                   ; this is the package's memory, not ours
    jz .done
    cmp dl, 'a'
    jb .keep
    cmp dl, 'z'
    ja .keep
    sub dl, 32
    mov [es:si], dl
.keep:
    inc si
    inc ax
    loop .walk
.done:
    pop cx
    pop si
    clc
    ret
.none:
    xor ax, ax
    stc
    ret

; STATE
;
; A driver has no bss (os88drv.inc): its zeroed data is written as `db 0` in
; the image and ships on the floppy, which buys a load path with exactly one
; heap claim in it.
; =============================================================================
; --- the settings, and the blob they ride in ---------------------------------
rd_kb:      dw RD_DEFKB         ; the store's size. The DEFAULT lives here and
                                ; not in rd_cfg_load, so a machine with no
                                ; SYSTEM.CFG at all gets it
rd_loc:     db RDL_CONV
rd_flags:   db 0                ; RDCF_*
rd_iname:   times 13 db 0       ; the preserved image: its name...
rd_idrv:    db 0                ; ...the volume it is on...
rd_icwd:    dw 0                ; ...and its folder's first cluster
rd_blob:    times DRV_RBLOB_SZ db 0

; --- the store ---------------------------------------------------------------
rd_have:    db 0                ; 1 = there is a store
rd_arena:   dw 0                ; conventional: its segment
rd_bounce:  dw 0                ; extended: the one-extent conventional bounce
rd_xbase:   dw 0, 0             ; ...and the block's 32-bit linear base
rd_ctab:    dw 0                ; the CHAIN TABLE's claim (SPEC.md 62.9.10) -
                                ; a word per extent, and a claim rather than an
                                ; array in this image because an array is what
                                ; capped the whole store at 224KB
rd_sext:    dw RD_NOEXT         ; which extent the bounce is holding
rd_xerr:    db 0                ; ONE operation's verdict: the extended store
                                ; refused a stage, so what is in the bounce is
                                ; not what the caller asked about. Cleared at
                                ; the top of rd_io and tested by every verb
                                ; that calls it (SPEC.md 62.9.10.2) - a
                                ; swallowed refusal is a copy that silently
                                ; never happened
rd_extkb:   dw RD_EXTMINKB      ; the geometry rd_geom derives from [rd_kb]:
rd_extb:    dw RD_EXTMINKB * 1024   ; the extent in KB and in bytes...
rd_extpara: dw RD_EXTMINKB * 64     ; ...in paragraphs, for a conventional
rd_next:    dw 0                ; store...and how many there are
rd_ctabkb:  dw 0                ; ...and what the table costs

; --- the volume --------------------------------------------------------------
rd_vol:     db 0xFF             ; the volume index we registered, FF = none
rd_cwd:     db 0                ; the folder we are standing in, 0 = the root
rd_err:     db 0                ; the last RDERR_*, for the page to report

; --- scratch: an operation's own words ---------------------------------------
rd_n:       dw 0, 0             ; bytes this operation is about to move,
                                ; banked across the copy that spends CX. A
                                ; DWORD now: a file is bounded by the volume
rd_off:     dw 0, 0             ; FSV_READAT's 32-bit offset, and APPEND's
rd_cap:     dw 0                ; ...and READAT's capacity, which stays a word
rd_end:     dw 0, 0             ; an append's new length
rd_fitlo:   dw 0                ; rd_fits' low half, CL being spent on shifts
rd_bufo:    dw 0                ; the kernel's buffer, banked across a lookup
rd_bufs:    dw 0
rd_nameo:   dw 0                ; a name in KERNEL_SEG, likewise
rd_want:    dw 0                ; rd_chain_fit's arithmetic, and rd_load's
rd_hasn:    dw 0                ; banked image size
rd_need:    dw 0
rd_head:    dw 0                ; the private list a grow builds before it
                                ; links any of it on
rd_io_dir:  db 0                ; rd_io's walk, which takes its arguments in
rd_io_prog: db 0                ; MEMORY: two 32-bit quantities, a far pointer,
rd_io_ext:  dw 0                ; an extent and two flags is nine words, and
rd_io_off:  dw 0, 0             ; threading that through an 8086's registers
rd_io_len:  dw 0, 0             ; costs every caller more than it saves
rd_io_seg:  dw 0
rd_io_ofs:  dw 0
rd_io_o:    dw 0                ; the offset inside the current extent
rd_io_n:    dw 0                ; ...and how much of it this piece moves
rd_pos:     dw 0, 0             ; how far a preserve or a load has got
rd_ilen:    dw 0                ; ...and one chunk's length, banked across a
                                ; file slot whose CX is an argument
rd_wseg:    dw 0                ; the metadata block's buffer...
rd_wown:    db 0                ; ...and whether it is ours to free
rd_badcode: db 0                ; rd_meta_check's verdict
rd_bdrv:    db 0                ; where the caller was standing, banked across
rd_bcwd:    dw 0                ; a visit to the image's folder
rd_dlgmode: db 0                ; FDLG_OPEN / FDLG_SAVE, across the dialog
rd_dlgname: times 14 db 0       ; ...and its default name, staged out of the
                                ; page's segment into ours

; --- the volume's own tables -------------------------------------------------
rd_ent:     times 32 db 0       ; the staged SPEC.md 19.1 entry, ours to fill
rd_dir:     times RD_MAXENT * RDE_SZ db 0   ; ...and the directory, which STAYS
                                    ; in the image: it is bounded by a row
                                    ; count rather than by the store's size,
                                    ; so it does not scale and does not need to
                                    ; be a claim

    OS88_DRV_END
