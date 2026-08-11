; =============================================================================
; os8088 - RAMDISK.DRV
;
; A DRVC_FILE driver (SPEC.md 62.9): a volume whose contents come from
; somebody ANSWERING QUESTIONS ABOUT FILES rather than about sectors. It has
; no BPB, no FAT, no directory to walk and no int 13h behind it at all -
; dsk_xfer refuses it outright - and the Disk window, the desktop zone, the
; sort, the `..` synthesis and the file dialog work on it unchanged.
;
; WHY THIS EXISTS, AND WHY IT IS NOT THE CABLE. SPEC.md 62.9.3 is explicit:
; the harness for the redirector is a RAM DISK and not NET.DRV. A RAM disk
; needs no hardware, so every branch site the redirector added to the kernel
; runs on a cycle-accurate 8088 in a container - which is the one thing block
; mode never had, and the reason two of its bugs reached the field instead of
; the build (docs/NET-PLAN.md 1.2.0). The cable's file client is then a SECOND
; DRVC_FILE driver against a kernel that is already proven.
;
; MILESTONE 1 IS READ-ONLY AND THE TREE IS STATIC. Of the thirteen FSV_*
; verbs this publishes three - LIST, CHDIR and DFREE - which is exactly what
; "a volume that mounts and lists" needs. Every other cell is 0, and
; drv_fs_call answers CF=1 for an unpublished verb, which is an ordinary
; refusal and not a fault: the kernel greys or declines and nothing hangs.
; Milestones 2 (STAT/READ/READAT) and 3 (the write verbs) fill the same table
; in, and the tree stops being a fixture when they do.
;
; THE TREE IS DELIBERATELY TWO LEVELS DEEP AND DELIBERATELY OUT OF ORDER.
; Two levels, because a one-level tree cannot tell a working parent handle
; from a hard-coded zero - SPEC.md 62.9.1's whole point is that FSV_CHDIR
; ANSWERS the parent, and going up from DEEP has to land on DOCS and not on
; the root. Out of order, because the kernel sorts (SPEC.md 19.4) and a
; driver that handed entries over pre-sorted would hide it if it did not.
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1), so there is not
; one string instruction in this file: `rep stosw` over a staging buffer here
; would write into the kernel's own segment, which is the trap apps/modplug
; shipped with until it was caught. The zeroing loop below is DS-relative on
; purpose, exactly as dsk_synth's is.
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'Ram Disk', DRVC_FILE, rd_entry

; --- the files' CONTENT, ahead of the table that names it --------------------
; Declared first so every reference from the table below is BACKWARDS: the
; row macro pads with `times RDE_SZ - ($ - %%row)`, and a forward reference
; inside a `times` count is the one thing NASM cannot resolve in one pass.
;
; MINES.O88 IS THE REAL PACKAGE, embedded from build/ (the Makefile passes
; -I build). It is what makes "a package launches off a redirected volume"
; a thing that can be clicked rather than argued: the loader reads it through
; dsk_read_chain, which on this volume is one FSV_READ by handle, and the
; header it validates in the region is the one os88pkg.py stamped.
;
; It also proves 62.9.2's other half by accident and worth naming: MINES has
; an EMBEDDED ICON, and on this volume it draws with 25's generic one -
; because a redirected volume has no first sector to peek at and disk_icons
; is left blank. That is the feature, not a defect.
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

; --- one row of the static tree ----------------------------------------------
; RDE_HAND is the OPAQUE HANDLE the kernel carries in the staged entry's
; first-cluster word at offset 18 (SPEC.md 62.9.1). It means nothing to the
; kernel and everything to us, and 0 is reserved for the root.
;
; EVERY ROW HAS ONE NOW, FILES INCLUDED, and that is milestone 2's doing:
; reads are BY HANDLE and only FSV_STAT takes a name, so a file that answered
; handle 0 could be listed and never read.
RDE_PAR     equ 0               ; db: the parent folder's handle
RDE_TYPE    equ 1               ; db: 0 file / 1 package / 2 folder
RDE_HAND    equ 2               ; db: this row's own handle, unique and non-0
RDE_DATA    equ 4               ; dw: its content, an offset in OUR segment
RDE_SIZE    equ 6               ; dd: bytes
RDE_NAME    equ 10              ; db: NUL-terminated 8.3 display name
RDE_SZ      equ 28

%macro RD_ENT 6                 ; parent, type, handle, data, size, name
%%row:
    db %1, %2, %3, 0
    dw %4
    dd %5
    db %6, 0
    times RDE_SZ - ($ - %%row) db 0
%endmacro

rd_tab:
    RD_ENT 0, 2, 1, 0, 0, 'DOCS'
    RD_ENT 0, 0, 2, rd_d_readme, rd_d_readme_end - rd_d_readme, 'README.TXT'
    RD_ENT 0, 0, 3, rd_d_notes,  rd_d_notes_end  - rd_d_notes,  'NOTES.TXT'
    RD_ENT 0, 1, 4, rd_d_mines,  rd_d_mines_end  - rd_d_mines,  'MINES.O88'
    RD_ENT 1, 2, 5, 0, 0, 'DEEP'
    RD_ENT 1, 0, 6, rd_d_hello,  rd_d_hello_end  - rd_d_hello,  'HELLO.TXT'
    RD_ENT 5, 0, 7, rd_d_bottom, rd_d_bottom_end - rd_d_bottom, 'BOTTOM.TXT'
rd_tab_end:
RD_NENT     equ (rd_tab_end - rd_tab) / RDE_SZ

RD_FREE_LO  equ 0x0000          ; what FSV_DFREE reports: 64KB, round enough
RD_FREE_HI  equ 0x0001          ; to be recognised in the status line

; -----------------------------------------------------------------------------
; The service table (SPEC.md 51.2). DSV_BLK is 0 and that is the point: this
; volume has no sectors, so there is nothing for the kernel to ask for.
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
    dw 0                        ; DSV_CPNAME  - no page: there is nothing to
    dw 0                        ; DSV_CPPAINT   configure and nothing to
    dw 0                        ; DSV_CPCLICK   report that the Drivers row
    dw 0                        ; DSV_CPCLOSE   does not already say
    dw rd_fsv                   ; DSV_FS      - ...and the verbs instead

; --- the FSV_* table DSV_FS points at, in OUR segment (SPEC.md 62.9.1) -------
; The zeroed cells are milestones 2 and 3: drv_fs_call answers CF=1 for an
; unpublished verb, which is a refusal and not a fault.
;
; NEVER END A LINE IN THIS TABLE WITH A BACKSLASH. NASM treats a trailing `\`
; as a LINE CONTINUATION even inside a comment, so an ASCII-art brace drawn
; down the right-hand side (`\` on the first row, `|` on the middle ones, `/`
; on the last) SILENTLY SWALLOWS THE NEXT `dw 0`. This table shipped one word
; short exactly that way: rd_dfree landed at index 10 instead of 11, FSV_DFREE
; read whatever followed the table, and `call bp` in the driver's own
; dispatcher jumped to 0x6152 - a runaway inside the driver's image that
; presented as "the Disk window will not open", four call layers from the
; cause. It assembles clean under -w+error and nothing points at the table.
; The build-time guard below is what makes it impossible to repeat.
rd_fsv:
    dw rd_list                  ; FSV_LIST
    dw rd_chdir                 ; FSV_CHDIR
    dw rd_stat                  ; FSV_STAT
    dw rd_read                  ; FSV_READ
    dw 0                        ; FSV_WRITE
    dw 0                        ; FSV_APPEND
    dw rd_readat                ; FSV_READAT
    dw 0                        ; FSV_DELETE
    dw 0                        ; FSV_RENAME
    dw 0                        ; FSV_MKDIR
    dw 0                        ; FSV_RMDIR
    dw rd_dfree                 ; FSV_DFREE
rd_fsv_end:
%if rd_fsv_end - rd_fsv != FSV_SIZE
  %error "ramdisk: the FSV_* table is not FSV_SIZE bytes - a swallowed row?"
%endif

rd_name:    db 'Ram Disk', 0
rd_label:   db 'RAM', 0         ; DV_LBL is 7 bytes inline (SPEC.md 18.7.3)

; =============================================================================
; ENTRY
; =============================================================================
rd_entry:
    cmp al, DRVV_ATTACH
    je  rd_attach
    cmp al, DRVV_DETACH
    je  rd_detach
    cmp al, DRVV_READY
    je  rd_ready
    clc                         ; a verb we do not implement is not an error
    ret

; -----------------------------------------------------------------------------
; DRVV_ATTACH - there is no hardware, so this cannot fail
; out: CF=0 and SI = the service table
;
; ALL-OR-NOTHING (SPEC.md 51.6 rule 1) is trivially honoured: nothing is
; probed, nothing is hooked, no vector is taken and no heap is claimed. That
; is the whole reason this is the harness - every other driver's attach can
; refuse for a reason a container cannot arrange.
; -----------------------------------------------------------------------------
rd_attach:
    mov byte [rd_cwd], 0        ; a re-attach starts at the root: the volume
    mov si, rd_svc              ; is added fresh and the kernel's [dsk_cwd]
    clc                         ; went with the row that was dropped
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - the earliest point a fence keyed on our publication answers
;              (SPEC.md 51.2.2), so this is where the volume may be added
;
; DVK_FILE is NOT passed and must not be: dsk_vol_add derives the kind from
; the CLASS the fence matched (SPEC.md 62.9), so a DRVC_FILE driver asking for
; a volume gets a redirected one by construction and cannot ask for the wrong
; sort.
; -----------------------------------------------------------------------------
rd_ready:
    cmp byte [rd_vol], 0xFF     ; DRVV_READY is sent once, but a driver that
    jne .out                    ; re-attached must not add a second volume
    xor al, al                  ; our own volume handle: there is one RAM disk
    xor cx, cx                  ; no sector count - there are no sectors
    xor dx, dx                  ; no donated listing claim: the .lowbss floor
    mov si, rd_label            ; and its 32-entry cap is plenty for a fixture
    call OSAPI_VOL_ADD
    jc  .out
    mov [rd_vol], al
    call OSAPI_VOL_MOUNT        ; AL survives: it is the volume index
.out:
    clc                         ; a refused mount is the Drivers page's to
    ret                         ; report, not a reason to fail the attach

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1), and has nothing to undo
;
; The kernel drops our volume itself before it frees the image, and since
; DV_CLASS it drops OURS rather than every driver-backed row (SPEC.md 51.8) -
; which is the bug that made unticking Sound unmount every hard-disk
; partition. There is no port to restore, no vector to put back and no worker
; to wait for.
; -----------------------------------------------------------------------------
rd_detach:
    mov byte [rd_vol], 0xFF
    clc
    ret

; =============================================================================
; THE FSV_* VERBS
; =============================================================================

; -----------------------------------------------------------------------------
; FSV_CHDIR - stand in a folder, and say what is above it (SPEC.md 62.9.1)
; in:  AX = a handle out of an entry, 0 = the root
; out: CF=0 and DX = THE PARENT'S HANDLE; CF=1 = no such folder
;
; The parent is answered HERE rather than by a verb of its own because this is
; the only call that ever moves the current folder - the driver is being told
; where to go at the exact moment it can say what is above it, and the kernel
; has no directory sector to read a '..' out of.
;
; A handle that names no folder of ours is REFUSED, and the mount fails with
; it. That is not defensiveness for its own sake: [dsk_cwd] is carried in the
; window's state block and in inst_fcwd, so a stale one can outlive a detach
; and arrive here after a re-attach.
; -----------------------------------------------------------------------------
rd_chdir:
    push bx
    push cx
    push si
    or ax, ax
    jz .root
    mov bl, al                  ; the handle as a byte: ours are 1..255
    mov si, rd_tab
    mov cx, RD_NENT
.scan:
    cmp byte [si+RDE_TYPE], 2   ; folders only - nothing chdirs into a file
    jne .next
    cmp [si+RDE_HAND], bl
    je .found
.next:
    add si, RDE_SZ
    loop .scan
    pop si
    pop cx
    pop bx
    stc
    ret
.found:
    mov [rd_cwd], bl
    mov bl, [si+RDE_PAR]        ; ...and what is above it
    xor bh, bh
    mov dx, bx
    jmp short .ok
.root:
    mov byte [rd_cwd], 0
    xor dx, dx                  ; the root's parent is the root, and [dsk_cwd]
                                ; = 0 means disk_mount synthesizes no '..' at
                                ; all - so this value is never read there
.ok:
    pop si
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_LIST - append this folder's entries (SPEC.md 62.9.1)
; out: CF=0
;
; THE DRIVER APPENDS AND THE KERNEL COUNTS. dsk_put_dir is module-internal, it
; knows whether this volume's listing is the .lowbss floor or a donated claim,
; and the count is disk_nfiles' - so what crosses the boundary is one entry at
; a time through OSAPI_FS_ENT, which is legal only from inside this call.
;
; NO SORT AND NO '..' HERE. The kernel does both (SPEC.md 19.4/19.5) and two
; places deciding a listing's order is how a display index stops meaning what
; the hit-test thinks it means.
;
; A FULL LISTING IS NOT AN ERROR: it is SPEC.md 19's cap, the extras are
; invisible exactly as they are on a FAT volume whose folder overruns it, and
; stopping cleanly is what the kernel's own scan does.
; -----------------------------------------------------------------------------
rd_list:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, rd_tab
    mov cx, RD_NENT
.next:
    mov al, [si+RDE_PAR]
    cmp al, [rd_cwd]
    jne .skip
    call rd_stage               ; SI's row -> rd_ent
    push si
    mov si, rd_ent              ; ES is set by the X stub from our own DS, so
    call OSAPI_FS_ENT           ; there is nothing to load here
    pop si
    jc .full
.skip:
    add si, RDE_SZ
    loop .next
.full:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; rd_stage - one tree row -> the 32-byte staged SPEC.md 19.1 entry (internal)
; in:  SI = the row
; out: rd_ent filled; AX/BX/DI clobbered, CX PRESERVED
;
; Zeroed DS-RELATIVE and not with `rep stosw`, because ES IS KERNEL_SEG on
; entry to every driver callback (SPEC.md 20.1) and a string store here would
; land 32 bytes in the middle of the kernel's own image. apps/modplug shipped
; that bug; this file has no string instruction in it at all.
;
; CX IS SAVED BECAUSE THE CALLER'S LOOP COUNTER IS IN IT, and the first
; version of this did not save it: the zeroing loop below spent CX, rd_list's
; `loop .next` then ran ~65,000 times walking off the end of the table, and
; the mount filled the listing to DSK_NENT with whatever followed it in the
; image. It presented as `disk_nfiles` = 32 for a three-entry root, which is
; also what it looks like when a driver appends too much on purpose - so the
; kernel's cap held and refused the rest, exactly as designed, and the bug
; was invisible until the count was read out of the guest.
; -----------------------------------------------------------------------------
rd_stage:
    push cx
    mov di, rd_ent
    mov cx, DSK_DE_SIZE/2
.zero:
    mov word [di], 0            ; the name's NUL padding and bytes 24..31 both
    inc di                      ; fall out of this
    inc di
    loop .zero

    mov bx, si                  ; @0: the display name, up to its own NUL
    add bx, RDE_NAME
    mov di, rd_ent
.name:
    mov al, [bx]
    mov [di], al
    or al, al
    jz .named
    inc bx
    inc di
    jmp short .name
.named:

    mov al, [si+RDE_TYPE]       ; @16: 0 file / 1 package / 2 folder. NEVER 3 -
    xor ah, ah                  ; the parent link is the kernel's to synthesize
    mov [rd_ent+16], ax
    mov al, [si+RDE_HAND]       ; @18: OUR OPAQUE HANDLE, which only FSV_CHDIR
    xor ah, ah                  ; and (at milestone 2) the loader interpret
    mov [rd_ent+18], ax
    mov ax, [si+RDE_SIZE]       ; @20: the size dword, which fm_measure sums
    mov [rd_ent+20], ax         ; for the status line's `Size`
    mov ax, [si+RDE_SIZE+2]
    mov [rd_ent+22], ax
    pop cx
    ret

; -----------------------------------------------------------------------------
; FSV_DFREE - free space (SPEC.md 62.9.1)
; out: CF=0, DX:AX = free BYTES, BX = the granule
;
; A fixed figure, because the tree is static: what this exercises is the
; kernel's path, where dsk_free_clus turns bytes into the SECTORS its two
; consumers expect (the redirected mount sets [dsk_spc] = 1 for exactly that).
; It becomes a real number when milestone 3 makes the tree writable.
; -----------------------------------------------------------------------------
rd_dfree:
    mov ax, RD_FREE_LO
    mov dx, RD_FREE_HI
    mov bx, 1
    clc
    ret

; -----------------------------------------------------------------------------
; rd_upper / rd_ncmp - case-insensitive name matching (internal)
; rd_ncmp: in SI = a row, ES:DI = the caller's NUL name; out CF=0 equal
; clobbers: nothing (SI and DI preserved)
;
; The caller's string is in KERNEL_SEG and ours is in our own image, which is
; the whole reason ES has to be read explicitly here: ES is KERNEL_SEG on
; entry to everything the kernel far-calls into a driver (SPEC.md 20.1), and
; that is exactly what makes it the right segment for the name.
; -----------------------------------------------------------------------------
rd_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

rd_ncmp:
    push ax
    push bx
    push di
    mov bx, si
    add bx, RDE_NAME
.next:
    mov al, [bx]
    call rd_upper
    mov ah, al
    mov al, [es:di]
    call rd_upper
    cmp al, ah
    jne .no
    or al, al
    jz .yes                     ; both reached the NUL together
    inc bx
    inc di
    jmp short .next
.yes:
    pop di
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; rd_byhand - a handle -> its row (internal)
; in:  AL = the handle
; out: CF=0 and SI = the row; CF=1 = no such handle
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
rd_byhand:
    mov si, rd_tab
.scan:
    cmp si, rd_tab_end
    jae .no
    cmp [si+RDE_HAND], al
    je .yes
    add si, RDE_SZ
    jmp short .scan
.no:
    stc
    ret
.yes:
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_STAT - a name in the current folder -> a handle (SPEC.md 62.9.1)
; in:  SI -> a NUL 8.3 name, in KERNEL_SEG
; out: CF=0, AX = the handle, DX:CX = the size, BL = a FAT-style attribute
;      byte; CF=1 and AX = FERR_*
; clobbers: AX, BX, CX, DX, SI, DI
;
; This is the ONLY verb that takes a name, and every read composes on it -
; which is what keeps `the thing in AX, unless it is 0, in which case the
; string in SI` out of the ABI.
;
; BL IS A FAT ATTRIBUTE BYTE and not the 19.1 type word: 0x10 for a folder,
; so dskw_rbody's existing `test 0x18` refuses one with FERR_PROT and
; dskw_stat's published answer means what it has always meant.
; -----------------------------------------------------------------------------
rd_stat:
    mov di, si                  ; ES:DI = the caller's name
    mov si, rd_tab
.row:
    cmp si, rd_tab_end
    jae .none
    mov al, [si+RDE_PAR]
    cmp al, [rd_cwd]            ; this folder only: a name resolves where the
    jne .nextrow                ; volume is standing (SPEC.md 19.2)
    call rd_ncmp
    jnc .found
.nextrow:
    add si, RDE_SZ
    jmp short .row
.none:
    mov ax, FERR_NOENT
    stc
    ret
.found:
    mov cx, [si+RDE_SIZE]
    mov dx, [si+RDE_SIZE+2]
    xor bx, bx
    cmp byte [si+RDE_TYPE], 2
    jne .attr
    mov bl, 0x10                ; a directory, in the kernel's own vocabulary
.attr:
    mov al, [si+RDE_HAND]
    xor ah, ah
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_READ - the whole file, by handle (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, DI:CX = its 32-bit capacity
; out: CF=0 and DX:AX = the bytes read; CF=1 and AX = FERR_*
;
; THE CAPACITY IS CHECKED AGAIN HERE and that is not redundant. dskw_rbody
; checks it before it calls, but this cell is reached from dsk_read_chain
; too - the loader's path - and a driver that trusts its caller's arithmetic
; is a driver that writes past the end of somebody else's claim. It costs
; four compares against a `rep movsb`.
; -----------------------------------------------------------------------------
rd_read:
    call rd_byhand
    jc .bad
    cmp word [si+RDE_SIZE+2], 0
    jne .big                    ; nothing here is 64KB, and one rep movsb
                                ; could not carry it across a segment anyway
    or di, di
    jnz .fits                   ; the capacity's high word alone clears it
    mov ax, [si+RDE_SIZE]
    cmp ax, cx
    ja .big
.fits:
    mov cx, [si+RDE_SIZE]
    mov [rd_n], cx
    mov ax, [si+RDE_DATA]
    push es
    mov es, dx                  ; DX:BX is the buffer - DX and NOT ES, because
    mov di, bx                  ; ES arrived as KERNEL_SEG (SPEC.md 51.8)
    mov si, ax
    cld
    rep movsb
    pop es
    mov ax, [rd_n]
    xor dx, dx
    clc
    ret
.big:
    mov ax, FERR_BIG
    stc
    ret
.bad:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_READAT - a chunk of a file, by handle and byte offset (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, CX = its capacity, DI:SI = the
;      32-bit offset
; out: CF=0 and DX:AX = the bytes delivered, 0 = at or past the end
;
; No alignment precondition at all, where the FAT path needs whole clusters
; at both ends: there are no clusters here, so dskw_read_at's two refusals
; simply do not arise on this volume.
; -----------------------------------------------------------------------------
rd_readat:
    mov [rd_off], si
    mov [rd_offhi], di
    mov [rd_cap], cx
    call rd_byhand
    jc .bad
    cmp word [rd_offhi], 0
    jne .eof                    ; past the end of anything we serve
    cmp word [si+RDE_SIZE+2], 0
    jne .bad
    mov ax, [si+RDE_SIZE]
    cmp ax, [rd_off]
    jbe .eof                    ; at or past the end: 0 bytes, and NOT an error
    sub ax, [rd_off]            ; AX = what is left of the file
    cmp ax, [rd_cap]
    jbe .take
    mov ax, [rd_cap]            ; ...clamped to what was asked for
.take:
    mov [rd_n], ax
    mov cx, ax
    mov ax, [si+RDE_DATA]
    add ax, [rd_off]
    push es
    mov es, dx
    mov di, bx
    mov si, ax
    cld
    rep movsb
    pop es
    mov ax, [rd_n]
    xor dx, dx
    clc
    ret
.eof:
    xor ax, ax
    xor dx, dx
    clc
    ret
.bad:
    mov ax, FERR_NOENT
    stc
    ret

; --- state -------------------------------------------------------------------
rd_n:       dw 0                ; bytes this read is about to deliver, banked
                                ; across the `rep movsb` that spends CX
rd_off:     dw 0                ; FSV_READAT's offset...
rd_offhi:   dw 0
rd_cap:     dw 0                ; ...and its capacity
rd_cwd:     db 0                ; the folder we are standing in, 0 = the root
rd_vol:     db 0xFF             ; the volume index we registered, FF = none
rd_ent:     times 32 db 0       ; the staged SPEC.md 19.1 entry, ours to fill

    OS88_DRV_END
