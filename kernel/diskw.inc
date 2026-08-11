; =============================================================================
; os8088 - diskw.inc
;
; The FAT write path (SPEC.md 18.4). disk.inc reads a data floppy; this
; module is the only one that modifies one, and the only caller of
; disk_write anywhere in the kernel. Five public routines - dskw_write,
; dskw_read, dskw_delete, dskw_rename, dskw_dfree - are the whole surface,
; and read and write are ONE routine each: both take a far pointer and a
; 32-bit count, so there is no separate big-file entry point any more
; (SPEC.md 18.4.1 - the retired slot is dskw_gone below),
; for the OS and for packages alike (they ARE the API cells 0x0120..0x0140,
; SPEC.md 20.3): whole files, by name, in the volume's CURRENT directory
; (SPEC.md 19.2 - [dsk_cwd], which dsk_chdir moves). No handles, no seek,
; no partial rewrite, no paths: a name is resolved in one directory, the
; one the volume is currently sitting in. dskw_mkdir joins them as the
; sixth (SPEC.md 18.5) and dskw_rmdir as the seventh (SPEC.md 18.6) - the
; two that act on a directory rather than on a file, and the two the API
; deliberately does not expose, because a package cannot navigate.
;
; Everything here is UI-task-only, exactly like the read path: int 13h runs
; with [sch_lock] raised, dsk_secbuf and the FAT snapshot are shared with
; disk_mount, and nothing serializes the two but the single-task rule
; (SPEC.md 18/18.4). A window callback is a legal caller and holds the gfx
; lock, so a save stalls painters for its duration - by design, on the same
; terms as a Refresh remount (SPEC.md 22).
;
; That now includes a HARD PROHIBITION on package worker tasks (SPEC.md
; 20.6): a package may own one, and nothing here can tell. A second
; concurrent caller cross-links the FAT through the shared dsk_secbuf and
; FAT snapshot - the commit order below defends against a crash, not
; against a second writer.
;
; The disk stays hostile. No LBA is ever computed from a raw on-disk field:
; clusters go through dsk_clus2lba (which bounds them to [2, dsk_maxclus]),
; FAT offsets rest on SPEC.md 18.2 rule 16, and directory LBAs come from
; dsk_dirw_next (SPEC.md 19.2), which bounds the root by dsk_rootsecs and
; a subdirectory's chain by both dsk_clus2lba and a sector-count guard. A corrupt volume can make a write fail;
; it cannot make one land outside itself.
;
; The commit order is binding (SPEC.md 18.4): allocate + write the data,
; flush the FAT, THEN write the directory entry, THEN free the old chain
; and flush again. A power failure at the worst moment leaks lost clusters,
; which any host fsck reclaims - it never cross-links and never loses the
; bytes the user already had. Anything that fails before the commit rolls
; the snapshot back by re-reading the FAT off the disk.
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

; --- error codes, pinned (SPEC.md 18.4; mirrored as FERR_* in os88api.inc) ---
FERR_OK      equ 0             ; success
FERR_NODISK  equ 1             ; no mounted volume (or the mount failed)
FERR_IO      equ 2             ; unrecoverable read/write error
FERR_NAME    equ 3             ; malformed 8.3 name / bad argument
FERR_NOENT   equ 4             ; no such file
FERR_EXIST   equ 5             ; the target name already exists
FERR_FULL    equ 6             ; no free clusters
FERR_DIRFULL equ 7             ; the current directory has no free slot
FERR_PROT    equ 8             ; read-only/hidden/system/label/subdirectory
FERR_WPROT   equ 9             ; the media is write-protected
FERR_BIG     equ 10            ; larger than the caller's buffer - or, on a
                               ; write, than the 32MB sector count a 16-bit
                               ; count can hold (SPEC.md 18.4.1)

; --- directory attribute bits + the raw fields disk.inc does not name -------
DSKW_PROT    equ 0x1F          ; read-only|hidden|system|volume|directory:
                               ; any of them and the entry is untouchable
DSKW_NPROT   equ 0x0F          ; the RENAME mask (SPEC.md 18.4) - the same
                               ; four bits WITHOUT the directory one. A
                               ; rename moves the eleven name bytes and
                               ; nothing else, and nothing on the volume
                               ; records a directory by NAME: a child's '..'
                               ; holds the parent's first CLUSTER and its own
                               ; '.' holds its own, so a folder may be renamed
                               ; without breaking the walk back up. Replace
                               ; and delete keep DSKW_PROT, because both of
                               ; those would strand the subtree.
DSKW_ARCH    equ 0x20          ; archive: what a fresh entry carries
DSKW_DIR     equ 0x10          ; directory: what dskw_mkdir stamps (18.5)
DSKW_HID     equ 0x02          ; hidden
DSKW_SYS     equ 0x04          ; system
DSKW_SYSAT   equ DSKW_HID | DSKW_SYS
                               ; what a KERNEL file wears (SPEC.md 19.6):
                               ; hidden + system, so neither DOS's DIR nor
                               ; our own file manager lists it
DSKW_SPROT   equ DSKW_PROT & ~DSKW_SYSAT
                               ; the replace mask for dskw_write_sys - the
                               ; same refusals WITHOUT hidden and system,
                               ; because those two are what the kernel just
                               ; stamped on its own file and it must be able
                               ; to rewrite it. Read-only, the volume label
                               ; and a subdirectory still refuse.
DSK_R_CTIME  equ 14            ; create time (word)
DSK_R_CDATE  equ 16            ; create date (word)
DSK_R_ADATE  equ 18            ; last-access date (word)
DSK_R_WTIME  equ 22            ; last-write time (word)
DSK_R_WDATE  equ 24            ; last-write date (word)

DSKW_EOC12   equ 0x0FFF        ; end-of-chain marks: >= the spec's EOC floor
DSKW_EOC16   equ 0xFFFF        ; (0FF8h / 0FFF8h), so a host fsck walks the
                               ; chains exactly the way this kernel does

; the legal short-name symbols, beyond A-Z and 0-9 (SPEC.md 18.4). Spaces,
; path separators, wildcards and "+,;=[]:" are absent on purpose.
dskw_syms:  db '$', '%', 0x27, '-', '_', '@', '~', 0x60
            db '!', '(', ')', '{', '}', '^', '#', '&', 0

; dskw_commit stamps ATTR_DIRECTORY instead of ARCHIVE while this is set,
; and dskw_mkbody is the only thing that ever sets it (SPEC.md 18.5).
;
; It lives HERE, in .text, and not in .bss - the same rule, and the same
; hard-won reason, as [dsk_keepcwd] in disk.inc. -f bin gives .bss no image
; bytes and nothing zeroes it, so an uninitialised .bss byte reads back as
; whatever the machine left there - reliably NON-ZERO on the hardware this
; was found on. In .bss this byte made dskw_commit stamp attribute 0x10
; on every file the OS ever created - which every reader then refused as
; FERR_PROT (a directory is protected), so nothing could be read back or
; deleted, and every chain those writes allocated leaked. It is exactly the
; kind of flag that is only ever cleared on a path that has never run.
dskw_isdir: db 0                ; 1 = dskw_commit is stamping a folder
dskw_batch: db 0                ; 1 = a MULTI-FILE operation is running and
                                ; owns the coherence remount (SPEC.md 22.3).
                                ; A .text `db`, not .bss, for dskw_isdir's
                                ; reason: -f bin zeroes nothing, and the very
                                ; first write must not see a garbage flag
dskw_syswr: db 0                ; 1 = dskw_write_sys, not dskw_write: this
                                ; file is the KERNEL's (SPEC.md 19.6). Here
                                ; for the same reason again, and with the
                                ; sharper edge - garbage in this byte would
                                ; make every package's write create a hidden
                                ; system file and overwrite one

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - but the DATA is
                                ; not, and stays in .text at every toggle here

; -----------------------------------------------------------------------------
; dskw_write - create or replace a whole file (SPEC.md 18.4, slot 0x0120)
; in:  SI -> NUL-terminated 8.3 name, ES:BX -> the bytes, DX:CX = byte count
;      (32-bit, DX high; 0 = an empty file)
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
;
; ES:BX is a plain far pointer and DX:CX a 32-bit count, so there is ONE
; write and it has no 64KB ceiling (SPEC.md 18.4.1): the source is normalised
; to an offset of 0..15 and the transfer loop then walks it by SEGMENT. A
; buffer inside the caller's own segment and a multi-segment heap claim are
; the same call.
; -----------------------------------------------------------------------------
dskw_write_x:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [dskw_seg], es          ; the caller's buffer segment, for the data
    mov [dskw_src], bx          ; loop (which re-loads ES per transfer)
    mov [dskw_len], cx
    mov [dskw_lenhi], dx
    call dskw_wbody             ; out: CF + AX (pops do not touch flags)
    call KERNEL_SEG:cw_fpg_end  ; the ONE place the widget is given back,
                                ; however many ways the body returned - and it
                                ; preserves CF and AX too (SPEC.md 12.8)
    mov byte [dskw_syswr], 0    ; one shot, cleared here so it can never
    pop es                      ; leak into the next caller's write
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_write_sys - dskw_write, for a file that belongs to the KERNEL
; in/out: dskw_write's, exactly
;
; KERNEL-ONLY - there is no API slot and there must never be one, because
; the whole point of SPEC.md 19.6 is that a package cannot make a file the
; user can neither see nor delete. Two differences from the ordinary write,
; both keyed off [dskw_syswr] and both inside dskw_commit / dskw_wbody:
;
;   1. a file it CREATES is stamped hidden + system, and
;   2. a file it REPLACES may already be hidden + system.
;
; The second is not a courtesy: SYSTEM.CFG is rewritten every time the
; Control Panel closes, and without it the first save would make the file
; and every save after that would be refused FERR_PROT by the very bits the
; first one set. Nothing else relaxes - a read-only SYSTEM.CFG, or a folder
; by that name, is still refused.
; -----------------------------------------------------------------------------
dskw_write_sys_x:
    mov byte [dskw_syswr], 1
    jmp short dskw_write_x      ; ...which clears it again on the way out

; -----------------------------------------------------------------------------
; dskw_read - read a whole file by name (SPEC.md 18.4, slot 0x0128)
; in:  SI -> name, ES:BX -> destination, DX:CX = its capacity in bytes
;      (32-bit, DX high)
; out: CF=0, DX:AX = bytes read (the file's 32-bit size); CF=1, AX = FERR_* -
;      and on FERR_BIG, decided from the directory entry's 32-bit size BEFORE
;      any data I/O, the destination is untouched
; clobbers: AX/DX (the outputs), CF
;
; dskw_write's mirror, and the same argument: the destination is normalised
; to an offset of 0..15 and then advances BY SEGMENT (ES += 32 paragraphs per
; 512-byte sector, the offset held), so the 16-bit offset limit never binds
; and dsk_xfer's per-sector int 13h granularity is unchanged. A package reads
; a 116KB module into a heap claim and the kernel reads a 32-byte settings
; file into a .bss buffer through this one entry point.
; -----------------------------------------------------------------------------
dskw_read_x:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    mov [dskw_seg], es
    mov [dskw_src], bx
    mov [dskw_cap], cx
    mov [dskw_caphi], dx
    call dskw_rbody             ; out: CF + DX:AX (pops touch neither)
    call KERNEL_SEG:cw_fpg_end  ; ...and neither does this (SPEC.md 12.8)
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_gone - the retired readbig cell (SPEC.md 20.3 slot 0x01E8, 20.8 rule 4)
; out: CF=1, AX = FERR_NAME
;
; dskw_read absorbed it: the destination is ES:BX either way and the count is
; 32-bit either way, so the segment-run sibling has nothing left to be. The
; NUMBER cannot be reused - a shipped slot keeps its contract, and "we no
; longer implement this" is a refusal, not a different routine at the same
; address - so a package built against the older SDK is told no rather than
; being silently handed a call whose BX it never set.
; -----------------------------------------------------------------------------
dskw_gone_x:
    mov ax, FERR_NAME
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_delete - delete a file by name (SPEC.md 18.4, slot 0x0130)
; in:  SI -> name
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_delete_x:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call dskw_dbody
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_rename - rename a file in place (SPEC.md 18.4, slot 0x0138)
; in:  SI -> old name, DI -> new name
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_rename_x:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call dskw_nbody
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_dfree - free space on the mounted volume (SPEC.md 18.4, slot 0x0140)
; in:  nothing
; out: CF=0, DX:AX = free bytes, BX = SECTORS per cluster (spc*512 overflows
;      16 bits at the SPEC.md 18.2-legal spc = 128); CF=1, AX = FERR_*
; clobbers: AX/BX/DX (the outputs), CF
;
; Answered entirely from the resident FAT snapshot - no disk I/O, so a
; package may call it as freely as it likes.
; -----------------------------------------------------------------------------
dskw_dfree_x:
    push cx
    push si
    push di
    push es
    call KERNEL_SEG:cw_dsk_free_clus ; AX = free clusters - ONE counting body for
    jc .bad                     ; this cell and for the status line's figure
    mov dl, [dsk_spc]           ; (SPEC.md 22.7), so the two cannot disagree
    xor dh, dh
    mul dx                      ; DX:AX = free sectors (DX = 0: free
                                ; clusters * spc <= TotSec16 <= 5,760)
    mov cx, 9                   ; bytes = sectors * 512, in 32 bits
.shift:
    shl ax, 1
    rcl dx, 1
    loop .shift
    mov bl, [dsk_spc]
    xor bh, bh
    clc
    jmp short .out
.bad:
    mov ax, FERR_NODISK
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dskw_mkdir - create a subdirectory in the current directory (SPEC.md 18.5)
; in:  SI = NUL 8.3 name; UI-task/window-callback context like every dskw_*
; out: CF=0 AX=0, else CF=1 AX = FERR_*
; clobbers: nothing else
; -----------------------------------------------------------------------------
dskw_mkdir_x:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call dskw_mkbody
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_mkbody - the mkdir pipeline (module-internal)
; in:  SI = the name; out: CF/AX as dskw_mkdir
;
; The SPEC.md 18.4 commit order, unchanged and for the same reason: the new
; directory's own cluster is allocated and fully WRITTEN first, the FAT is
; flushed second, and only then does the one-sector parent entry go down.
; A crash before that last write leaks a formatted-but-unreferenced cluster
; - which any host fsck reclaims - and can never produce a directory entry
; pointing at a cluster that was never initialised. Every failure ahead of
; the commit runs dskw_refat, so a half-built chain cannot survive in RAM
; to be flushed by some later, unrelated write.
;
; The new directory is born with its own '.' and '..', which is what makes
; dsk_dotdot (SPEC.md 19.2) work without the kernel remembering a path, and
; the REST of the cluster is written zero: a slot search reaching
; uninitialised sectors would read whatever the disk last held there and
; could match a name that is not in this directory at all.
; -----------------------------------------------------------------------------
dskw_mkbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov di, dskw_n83
    call dskw_name83            ; SI -> the 11-byte raw name field
    jnc .named
    mov ax, FERR_NAME
    stc
    ret
.named:
    call dskw_find              ; CF=0 = the name is already taken
    jnc .exists
    cmp ax, FERR_NOENT
    jne .fail                   ; FERR_IO: the scan itself failed
    cmp word [dskw_fsec], 0xFFFF
    je .dirfull                 ; scanned to the end with no reusable slot

    call dskw_alloc             ; AX = cluster, already marked end-of-chain
    jnc .haveclus
    inc ax                      ; 0xFFFF -> 0: the mark could not be written,
    jz .allocio                 ; which is an I/O failure and not a full disk
    jmp .nospace
.allocio:
    jmp .wfail                  ; ...and it takes the ROLLBACK path, because a
                                ; half-marked cluster must not survive in RAM
                                ; to be flushed by a later unrelated write
.haveclus:
    mov [dskw_first], ax
    call KERNEL_SEG:cw_dsk_clus2lba ; AX = its first LBA
    jc .badclus
    mov [dskw_lba], ax

    call dskw_dotents           ; '.' and '..' built in dsk_secbuf
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dskw_lba]
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_write
    jc .wfail

    call dskw_zerobuf           ; the cluster's remaining sectors
    mov cl, [dsk_spc]
    xor ch, ch
    dec cx
    jcxz .zeroed
    mov dx, [dskw_lba]
    inc dx
.zsec:
    push cx
    mov ax, dx
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_write
    pop cx
    jc .wfail
    inc dx
    loop .zsec
.zeroed:

    call dskw_flush_x           ; the FAT, THEN the entry (SPEC.md 18.4)
    jc .rollax

    mov word [dskw_len], 0      ; a directory's size field is zero by spec -
    mov word [dskw_lenhi], 0    ; both words of it, since SPEC.md 18.4.1
    mov byte [dskw_new], 1
    mov byte [dskw_isdir], 1
    call dskw_commit            ; the one-sector commit
    mov byte [dskw_isdir], 0
    jc .rollax
    call dskw_sync_x            ; coherence by remount (SPEC.md 18.4)
    xor ax, ax
    clc
    ret

.exists:
    mov ax, FERR_EXIST
    stc
    ret
.dirfull:
    mov ax, FERR_DIRFULL
    stc
    ret
.nospace:
    mov ax, FERR_FULL
    stc
    ret
.badclus:                       ; the allocator handed back a cluster the
    mov ax, FERR_IO             ; geometry cannot address: corrupt volume
    jmp short .rollax
.wfail:
    call dskw_ioerr             ; AX = FERR_IO / FERR_WPROT
.rollax:
    push ax                     ; keep the error across the rollback
    mov byte [dskw_isdir], 0
    call dskw_refat             ; the snapshot goes back to what the disk says
    pop ax
    stc
.fail:
    ret

; -----------------------------------------------------------------------------
; dskw_zerobuf - fill dsk_secbuf with 512 zero bytes
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dskw_zerobuf:
    push ax
    push cx
    push di
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov di, dsk_secbuf
    mov cx, 256
    xor ax, ax
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_dotents - build a new directory's first sector in dsk_secbuf
; in:  [dskw_first] = the new directory's own cluster
; out: nothing (all registers preserved); dsk_secbuf holds '.' then '..'
;      and 480 zero bytes
;
; '..' carries [dsk_cwd], the parent - and when the parent IS the root that
; is 0, which is both the FAT spec's convention and this kernel's own value
; for the root, so nothing has to translate.
; -----------------------------------------------------------------------------
dskw_dotents:
    push ax
    push di
    push es
    call dskw_zerobuf
    call dskw_now               ; [dskw_date] / [dskw_time] (SPEC.md 37)
    mov ax, LOW_SEG
    mov es, ax
    mov di, dsk_secbuf          ; '.' -> this directory
    mov ax, [dskw_first]
    call dskw_dotent
    mov di, dsk_secbuf + 32     ; '..' -> the parent
    mov ax, [dsk_cwd]
    call dskw_dotent
    mov byte [es:dsk_secbuf+33], '.'   ; the second dot
    pop es
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_dotent - write one '.'-style link into a zeroed slot
; in:  ES:DI -> the 32-byte slot, AX = the cluster it points at
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dskw_dotent:
    push ax
    push bx
    push cx
    mov bx, di
    mov byte [es:bx], '.'
    push bx
    inc bx
    mov cx, 10                  ; name[1..10] = spaces
.pad:
    mov byte [es:bx], ' '
    inc bx
    loop .pad
    pop bx
    mov byte [es:bx+DSK_R_ATTR], DSKW_DIR
    mov [es:bx+DSK_R_CLUS], ax
    mov word [es:bx+DSK_R_SIZE], 0
    mov word [es:bx+DSK_R_SIZE+2], 0
    mov ax, [dskw_time]
    mov [es:bx+DSK_R_CTIME], ax
    mov [es:bx+DSK_R_WTIME], ax
    mov ax, [dskw_date]
    mov [es:bx+DSK_R_CDATE], ax
    mov [es:bx+DSK_R_WDATE], ax
    mov [es:bx+DSK_R_ADATE], ax
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_rmdir - remove an EMPTY subdirectory of the current directory (18.6)
; in:  SI = NUL 8.3 name; UI-task/window-callback context like every dskw_*
; out: CF=0 AX=0, else CF=1 AX = FERR_*
; clobbers: nothing else
; -----------------------------------------------------------------------------
dskw_rmdir:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call dskw_rmbody
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_rmbody - the rmdir pipeline (module-internal)
; in:  SI = the name; out: CF/AX as dskw_rmdir
;
; The counterpart to dskw_mkbody, and the reason File > Delete on a folder is
; not an item that can only ever fail. It is dskw_dbody's shape - mark the
; parent's entry deleted FIRST (one sector, the commit), then free the chain
; - and it is that order for dskw_dbody's reason: a crash between the two
; leaks lost clusters, while the reverse would leave a live directory entry
; pointing at free space the next write could hand to someone else. Nothing
; is allocated ahead of the commit, so - again as in dskw_dbody - there is no
; half-built chain and no dskw_refat rollback to run.
;
; The emptiness scan is where the safety actually lives, and it is
; deliberately stricter than a host's rmdir: refusing a folder someone else
; could have removed costs one message, while removing an occupied one costs
; every child's clusters at once. The children's directory SECTORS live
; inside the cluster we are about to free, so the moment it is gone nothing
; on the volume names them any more - there is no fsck answer to that.
; -----------------------------------------------------------------------------
dskw_rmbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov di, dskw_n83
    call dskw_name83            ; '.' and '..' cannot be spelled (18.4), and
    jnc .named                  ; the root has no entry, so neither can be
    mov ax, FERR_NAME           ; the target
    stc
    ret
.named:
dskw_rmbody_n83:                ; the same pipeline with dskw_n83 already
                                ; filled - what dskw_rmtree calls, because it
                                ; works from raw 11-byte names it read off the
                                ; disk rather than from display names
    call dskw_find              ; in the CURRENT directory (SPEC.md 19.2)
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    jc .out
    mov al, [dskw_raw+DSK_R_ATTR]
    test al, DSKW_NPROT
    jnz .prot                   ; read-only, hidden, system or volume label
    test al, DSKW_DIR
    jz .prot                    ; a file: the caller wanted dskw_delete
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_oldclus], ax
    cmp ax, 2                   ; a subdirectory always owns a real cluster,
    jb .corrupt                 ; so anything else is corruption - and a walk
    cmp ax, [dsk_maxclus]       ; we refuse is cheaper than one we bound
    ja .corrupt

    call dskw_isempty           ; CF=1 with AX = FERR_PROT / FERR_IO
    jc .out

    mov byte [dskw_raw], 0xE5   ; THE commit: one directory sector (18.6)
    mov byte [dskw_zapnext], 0
    call dskw_ent_store
    jc .out
    mov ax, [dskw_oldclus]      ; only now is the chain nobody's
    call dskw_free_chain
    call dskw_flush_x
    jc .lateio                  ; committed, but the free did not land:
    call dskw_sync_x            ; lost clusters, and the user should know
    xor ax, ax
    clc
    ret
.prot:
    mov ax, FERR_PROT
    stc
    ret
.corrupt:
    mov ax, FERR_IO
    stc
.out:
    ret
.lateio:
    push ax
    call dskw_sync_x
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_rmtree - remove a subdirectory AND EVERYTHING UNDER IT (SPEC.md 18.6)
; in:  SI = NUL 8.3 name of a subdirectory of the current directory;
;      UI-task/window-callback context like every dskw_*
; out: CF=0 AX=0, else CF=1 AX = FERR_*; the volume is back in the directory
;      the call started in either way
; clobbers: nothing else
;
; **Iterative, and it uses the disk's own parent links instead of a stack.**
; A recursive descent would need one frame per level on a 1,536-byte task
; stack (SPEC.md 8) and a depth bound nobody could justify; but every
; subdirectory records its parent in its own '..' entry (SPEC.md 19.2), so
; the walk can go down with dsk_chdir and come back up with dsk_dotdot,
; carrying nothing between iterations but the volume's position.
;
; The loop is one rule: look at the directory we are standing in, and
;
;   a file      -> delete it            (dskw_dbody_n83)
;   a folder    -> step into it         (dsk_chdir)
;   nothing     -> step out and remove the folder we just emptied
;
; so the tree empties depth-first with no memory of where it has been. The
; folder we just left is re-found in the parent BY ITS FIRST CLUSTER
; (dskw_rt_byclus) rather than by a name we would have had to keep: the
; cluster is what we already hold, and it cannot be ambiguous the way a name
; that failed to convert to 8.3 could.
;
; Two things are deliberately kept from the empty-folder path. Each removal
; goes through dskw_rmbody_n83, which re-runs the emptiness scan - so if the
; scan below and the scan there ever disagreed, the answer is a refusal and
; not a lost subtree. And dskw_dbody_n83 still refuses a read-only, hidden
; or system file: a tree with one of those in it stops there, partially
; deleted, with FERR_PROT - the same thing any rm -r does when it meets a
; permission it does not have, and much better than silently removing files
; the user's other operating system marked.
;
; LFN fragments (attr exactly 0x0F) are the one species this handles that
; dskw_rmdir will not: they carry no cluster chain, so removing one is
; marking its slot 0xE5 and nothing else. Without that, no folder a host OS
; wrote a long name into could ever be deleted, which would have made this
; feature useless on exactly the disks it is for.
;
; DSKW_RT_MAX bounds the whole operation. A cross-linked or cyclic directory
; tree cannot be detected locally - the '..' link that walks you in a circle
; looks exactly like a legitimate one - so the guard is the answer, and it is
; generous enough that no real floppy can reach it.
; -----------------------------------------------------------------------------
DSKW_RT_MAX equ 4096            ; iterations: one per entry removed or level
                                ; crossed. A 1.44MB volume cannot legitimately
                                ; need this many; a cyclic '..' would spin
                                ; forever without it

dskw_rmtree:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call dskw_rtbody
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_rtbody - the recursive-delete pipeline (module-internal)
; in:  SI = the name; out: CF/AX as dskw_rmtree
; -----------------------------------------------------------------------------
dskw_rtbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov ax, [dsk_cwd]           ; where we must end up, whatever happens
    mov [dskw_rt_home], ax
    mov di, dskw_rt_name        ; keep the caller's name: the last removal
    call dskw_name83            ; happens back here, and by then the walk has
                                ; overwritten dskw_n83 many times over
    jnc .named
    mov ax, FERR_NAME
    stc
    ret
.named:
    mov si, dskw_rt_name        ; dskw_find matches through dskw_n83
    mov di, dskw_n83
    mov cx, 11
    call dskw_rt_copy
    call dskw_find
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    jc .out
    mov al, [dskw_raw+DSK_R_ATTR]
    test al, DSKW_NPROT
    jnz .prot                   ; read-only, hidden, system or volume label
    test al, DSKW_DIR
    jz .prot                    ; a file: the caller wanted dskw_delete
    mov ax, [dskw_raw+DSK_R_CLUS]
    call dskw_rt_clchk
    jc .corrupt
    mov [dskw_rt_top], ax
    call dskw_chdir_dl           ; into the folder we are about to empty
    jc .ioerr

    mov word [dskw_rt_guard], DSKW_RT_MAX
.loop:
    dec word [dskw_rt_guard]
    jz .corrupt                 ; a cycle, or a tree past any real disk
    call dskw_rt_scan           ; CF=1 = this directory holds nothing
    jc .empty
    cmp byte [dskw_rt_attr], 0x0F
    je .lfn
    test byte [dskw_rt_attr], DSKW_DIR
    jnz .down
    call dskw_dbody_n83         ; a file: delete it where we stand
    jc .home                    ; FERR_PROT included - stop, and go home
    jmp short .loop
.lfn:
    call dskw_rt_zap            ; an orphaned long-name fragment: no chain,
    jc .home                    ; so the slot is the whole of it
    jmp short .loop
.down:
    mov ax, [dskw_rt_clus]
    call dskw_rt_clchk
    jc .corrupt_home
    call dskw_chdir_dl
    jc .iohome
    jmp short .loop

.empty:
    cmp byte [dskw_rt_ioerr], 0 ; the scan stopped on a read error: that is
    jne .iohome                 ; not the same as a directory with nothing in
    mov ax, [dsk_cwd]           ; it, and treating it as one would free the
    cmp ax, [dskw_rt_top]       ; clusters of files we simply could not see
    je .finish                  ; the folder the caller named is empty now
    mov [dskw_rt_clus], ax      ; the child we are leaving...
    call KERNEL_SEG:cw_dsk_dotdot ; ...and the parent the disk says it has
    jc .corrupt_home
    call dskw_chdir_dl
    jc .iohome
    call dskw_rt_byclus         ; find that child again, by cluster
    jc .corrupt_home
    call dskw_rmbody_n83        ; and remove it, emptiness re-checked
    jc .home
    jmp short .loop

.finish:
    call dskw_rt_go_home
    jc .ioerr
    mov si, dskw_rt_name
    mov di, dskw_n83
    mov cx, 11
    call dskw_rt_copy
    call dskw_rmbody_n83        ; the target itself, from where we began
    ret

.corrupt_home:
    call dskw_rt_go_home
.corrupt:
    mov ax, FERR_IO
    stc
    ret
.iohome:
    call dskw_rt_go_home
.ioerr:
    mov ax, FERR_IO
    stc
    ret
.prot:
    mov ax, FERR_PROT
    stc
.out:
    ret
.home:                          ; a failure part-way down: report it from
    push ax                     ; where the caller left us, not from wherever
    call dskw_rt_go_home        ; the walk happened to stop
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_rt_go_home - put the volume back where dskw_rmtree was called
; out: CF=1 = even that failed (the mount has landed at the root, SPEC 19.2)
; clobbers: AX, CF
; -----------------------------------------------------------------------------
dskw_rt_go_home:
    mov ax, [dskw_rt_home]
    ; fall through

; -----------------------------------------------------------------------------
; dskw_chdir_dl - dsk_chdir on the drive that is mounted (module-internal)
; in:  AX = the directory's first cluster; out: CF as dsk_chdir
; clobbers: AX, CF
; -----------------------------------------------------------------------------
dskw_chdir_dl:
    push dx
    mov dl, [disk_drive]
    call KERNEL_SEG:cw_dsk_chdir
    pop dx
    ret

; -----------------------------------------------------------------------------
; dskw_rt_clchk - is AX a plausible first cluster?
; in:  AX; out: CF=1 = no. Preserves AX.
; -----------------------------------------------------------------------------
dskw_rt_clchk:
    cmp ax, 2
    jb .bad
    cmp ax, [dsk_maxclus]
    ja .bad
    clc
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_rt_copy - CX bytes from DS:SI to DS:DI (module-internal)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dskw_rt_copy:
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
; dskw_rt_scan - the first entry of the CURRENT directory that is not '.',
;                '..' or free
; out: CF=0 with dskw_n83 = its 11 raw name bytes, [dskw_rt_attr] = its
;      attribute and [dskw_rt_clus] = its first cluster; CF=1 = the directory
;      holds nothing else (or could not be read - AX = FERR_IO then, and
;      [dskw_rt_ioerr] says which)
; clobbers: AX, CF
;
; Species rules are dskw_isempty's, because the two answers have to agree:
; anything that routine calls "occupied" this one has to be able to name.
; -----------------------------------------------------------------------------
dskw_rt_scan:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [dskw_rt_ioerr], 0
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dsk_cwd]
    call KERNEL_SEG:cw_dsk_dirw_start
.sector:
    call KERNEL_SEG:cw_dsk_dirw_get ; dsk_secbuf = the next sector (19.2.1)
    jnc .secok
    or ax, ax
    jz .none                    ; the chain ended with nothing found
    jmp short .ioerr
.secok:
    mov [dskw_rt_sec], ax
    mov si, dsk_secbuf
    mov cx, 16                  ; entries per sector
.entry:
    mov al, [es:si]
    test al, al
    jz .none                    ; end of directory: nothing past it counts
    cmp al, 0xE5
    je .next                    ; a deleted entry
    cmp al, '.'
    jne .found
    mov al, [es:si+1]           ; a dot link's second byte is the other dot
    cmp al, '.'                 ; or a pad space (SPEC.md 18.6)
    je .next
    cmp al, ' '
    je .next
.found:
    mov di, dskw_raw            ; the WHOLE entry - dsk_copy_in halves the
    mov cx, 32                  ; count into words, so 11 would drop a byte
    call KERNEL_SEG:cw_dsk_copy_in
    mov ax, si                  ; publish where it is, exactly as dskw_find
    sub ax, dsk_secbuf          ; does, so dskw_rt_zap can rewrite the slot
    mov [dskw_doff], ax         ; without a name lookup (dskw_find refuses to
    mov ax, [dskw_rt_sec]       ; match an LFN entry at all, by design)
    mov [dskw_dsec], ax
    mov si, dskw_raw
    mov di, dskw_n83
    mov cx, 11
    call dskw_rt_copy
    mov al, [dskw_raw+DSK_R_ATTR]
    mov [dskw_rt_attr], al
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_rt_clus], ax
    clc
    jmp short .out
.next:
    add si, 32
    loop .entry
    jmp short .sector
.ioerr:
    mov byte [dskw_rt_ioerr], 1
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_rt_byclus - find the subdirectory of the CURRENT directory whose first
;                  cluster is [dskw_rt_clus]
; out: CF=0 with dskw_n83 = its 11 raw name bytes; CF=1 = no such entry
; clobbers: AX, CF
;
; How the walk gets back the name of the folder it just emptied without
; having carried one down. The cluster is the identity: two directory entries
; cannot legitimately share one, and if a corrupt volume makes them, removing
; either is removing the thing this cluster is.
; -----------------------------------------------------------------------------
dskw_rt_byclus:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dsk_cwd]
    call KERNEL_SEG:cw_dsk_dirw_start
.sector:
    call KERNEL_SEG:cw_dsk_dirw_get ; either failure is "not found" here
    jc .none
    mov si, dsk_secbuf
    mov cx, 16
.entry:
    mov al, [es:si]
    test al, al
    jz .none
    cmp al, 0xE5
    je .next
    cmp al, '.'
    je .next                    ; never the dot links, whatever they point at
    test byte [es:si+DSK_R_ATTR], DSKW_DIR
    jz .next
    mov ax, [es:si+DSK_R_CLUS]
    cmp ax, [dskw_rt_clus]
    jne .next
    mov di, dskw_raw            ; 32, not 11: dsk_copy_in copies WORDS
    mov cx, 32
    call KERNEL_SEG:cw_dsk_copy_in
    mov si, dskw_raw
    mov di, dskw_n83
    mov cx, 11
    call dskw_rt_copy
    clc
    jmp short .out
.next:
    add si, 32
    loop .entry
    jmp short .sector
.none:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_rt_zap - mark the entry dskw_n83 names deleted, and free no chain
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX, CF
;
; For an LFN fragment (attr 0x0F), whose FstClusLO is a name checksum field
; and not a cluster. Marking the slot is the whole removal; going anywhere
; near dskw_free_chain with that word would free somebody else's data.
; -----------------------------------------------------------------------------
dskw_rt_zap:
    call dskw_ent_load          ; [dskw_dsec]/[dskw_doff], published by the
    jc .out                     ; scan - dskw_find would refuse to look
    mov byte [dskw_raw], 0xE5
    mov byte [dskw_zapnext], 0
    call dskw_ent_store
    jc .out
    call dskw_sync_x
    xor ax, ax
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; dskw_isempty - does a subdirectory hold nothing but '.', '..' and free slots?
; in:  [dskw_oldclus] = its first cluster, already bounded by the caller
; out: CF=0 = empty; CF=1 with AX = FERR_PROT (occupied) or FERR_IO (unread)
; clobbers: AX (the output), CF
;
; The walk is dsk_dirw_start / dsk_dirw_next, so the DSK_DIRW_MAX guard
; (SPEC.md 19.2) terminates a cross-linked chain instead of spinning on it
; with [sch_lock] raised. Four things are allowed in a slot and everything
; else - an LFN fragment a host left behind included - counts as occupied.
; That last case is the one worth stating: os8088 never matches or reuses LFN
; entries (SPEC.md 18.4), so it also declines to delete the directory they
; sit in.
; -----------------------------------------------------------------------------
dskw_isempty:
    push bx
    push cx
    push si
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dskw_oldclus]
    call KERNEL_SEG:cw_dsk_dirw_start
.sector:
    call KERNEL_SEG:cw_dsk_dirw_get ; dsk_secbuf = the next sector (19.2.1)
    jnc .secok
    or ax, ax
    jz .empty                   ; the chain ended with nothing found
    jmp short .ioerr
.secok:
    mov si, dsk_secbuf
    mov cx, 16                  ; entries per sector
.entry:
    mov al, [es:si]
    test al, al
    jz .empty                   ; end of directory: nothing past it counts
    cmp al, 0xE5
    je .next                    ; a deleted entry
    cmp al, '.'
    jne .occupied               ; anything that is not '.' or '..'
    mov al, [es:si+1]           ; ...and a dot link's SECOND byte is the other
    cmp al, '.'                 ; dot or a pad space. DOS never makes a name
    je .next                    ; like '.XT     TXT', but a foreign disk may
    cmp al, ' '                 ; carry one, and this module trusts nothing
    jne .occupied               ; it reads (SPEC.md 18.6)
.next:
    add si, 32
    loop .entry
    jmp short .sector
.empty:
    clc
    jmp short .out
.occupied:
    mov ax, FERR_PROT
    stc
    jmp short .out
.ioerr:
    call dskw_ioerr             ; AX = FERR_IO / FERR_WPROT
    stc
.out:
    pop es
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_wbody - the write pipeline (module-internal; dskw_write saves regs)
; in:  SI -> name; [dskw_seg]:[dskw_src] = the bytes,
;      [dskw_lenhi]:[dskw_len] = the 32-bit count
; out: CF + AX = FERR_*
;
; Steps, in the binding SPEC.md 18.4 order: gate, parse the name, size the
; job, find (or make room for) the directory entry, allocate + write the
; data clusters, flush the FAT, write the entry (THE commit), free the old
; chain, flush again, remount. Every failure before the commit rolls the
; FAT snapshot back off the disk, so a half-built chain never survives in
; RAM to be flushed by some later, unrelated write.
;
; There is no "the buffer must not wrap its segment" test any more, and there
; is nothing left for one to say: dskw_norm makes the source a segment RUN,
; which is the whole point of SPEC.md 18.4.1. What replaces it is the sector
; count's own ceiling, below.
; -----------------------------------------------------------------------------
dskw_wbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov di, dskw_n83
    call dskw_name83            ; SI -> the 11-byte raw name field
    jnc .named
    mov ax, FERR_NAME
    stc
    ret
.named:
    call dskw_norm              ; the source becomes seg:0..15 + a segment run

    ; --- size the job: sectors, and the length of a partial final one -----
    call dskw_size32            ; [dskw_rem] / [dskw_tail]
    jnc .sized
    mov ax, FERR_BIG            ; >= 32MB: no volume this kernel mounts can
    stc                         ; hold it, and the count would wrap
    ret
.sized:

    ; --- the directory entry: replace an existing one, or take a slot -----
    call dskw_find              ; CF=0 found; CF=1 AX = NOENT / IO
    jnc .replace
    cmp ax, FERR_NOENT
    je .create
    stc                         ; a read error mid-scan: report it as is
    ret
.create:
    cmp word [dskw_fsec], 0xFFFF
    jne .newslot
    mov ax, FERR_DIRFULL
    stc
    ret
.newslot:
    mov byte [dskw_new], 1      ; the slot dskw_find already spotted
    mov word [dskw_oldclus], 0
    jmp short .data
.replace:
    mov byte [dskw_new], 0
    call dskw_ent_load          ; dskw_raw = the entry's 32 raw bytes
    jc .out
    mov al, DSKW_PROT
    cmp byte [dskw_syswr], 0    ; a kernel file's own rewrite forgives the
    je .pmask                   ; two bits it stamped itself (SPEC.md 19.6)
    mov al, DSKW_SPROT
.pmask:
    test byte [dskw_raw+DSK_R_ATTR], al
    jz .keep
    mov ax, FERR_PROT           ; read-only, hidden, system, label or dir
    stc
    ret
.keep:
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_oldclus], ax      ; freed only after the commit, below

    ; --- 1. allocate the new chain and write its data ---------------------
    ; The old chain stays allocated throughout, so the two are disjoint and
    ; a crash here costs nothing. The price is that a replace briefly needs
    ; room for both copies (SPEC.md 18.4).
.data:
    mov ax, [dskw_rem]          ; arm the progress widget HERE and not up at
    call KERNEL_SEG:cw_fpg_begin    ; .sized, though the count is known there:
                                ; dskw_find's directory scan sits between the
                                ; two and its sectors are not part of the job
                                ; the bar is measuring (SPEC.md 12.8).
                                ; dskw_write_x ends it, whichever way this
                                ; body returns
    call dskw_wdata
    jc .rollback

    ; --- 2. flush the FAT: the new chain is now durable -------------------
    call dskw_flush_x
    jc .rollback

    ; --- 3. the commit: one directory-sector write ------------------------
    call dskw_commit            ; past here the file IS the new bytes, and
    jc .out                     ; rolling back is no longer an option

    ; --- 4. release the replaced chain and flush again --------------------
    mov ax, [dskw_oldclus]
    cmp ax, 2
    jb .ok
    cmp ax, [dsk_maxclus]
    ja .ok                      ; a garbage old link: leave the FAT alone
    call dskw_free_chain
    call dskw_flush_x
    jc .lateio                  ; committed, but the free did not land:
.ok:                            ; lost clusters, and the user should know
    call dskw_sync_x            ; the caches become a mount snapshot again
    xor ax, ax
    clc
    ret
.lateio:
    push ax
    call dskw_sync_x
    pop ax
    stc
.out:
    ret
.rollback:
    push ax
    call dskw_refat             ; drop the half-built chain: re-read the FAT
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_wdata - allocate the chain and write the bytes into it
; in:  [dskw_rem] = sectors, [dskw_tail] = bytes in a partial final sector,
;      [dskw_seg]:[dskw_src] = the source, normalised by dskw_norm
; out: CF=0 with [dskw_first] = the chain's head (0 for an empty file), or
;      CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
;
; The source advances BY SEGMENT (SPEC.md 18.4.1), which is what makes the
; offset horizon unreachable, so a 100KB file is one dskw_write.
;
; **Data goes out in RUNS THAT CROSS CLUSTERS.** The loop allocates, links and
; addresses one cluster at a time, but it does not write one at a time: each
; cluster's whole sectors are appended to a pending run, and the run is spent
; only when the next cluster is not contiguous with it, when it reaches
; dskw_runmax's cap, or when the file ends. dskw_alloc rovers forward, so on
; a volume with room the clusters of a new file ARE contiguous and the whole
; chunk is one transfer.
;
; That used to be one disk_write per sector, on the reasoning that dsk_xfer
; looped int 13h anyway - true for a floppy, and false since a driver-backed
; volume started handing the count to DSV_BLK in one call (SPEC.md 18.7),
; where a run is one IDE command instead of N.
;
; Three things keep it honest. The run is flushed BEFORE the partial final
; sector, because those bytes come after it and because flushing is what
; advances [dskw_wseg] to where the tail's source is. A sector the 8237
; cannot carry still goes alone through the stage, and dskw_runmax is only
; ever asked with no run pending, so that path can never orphan one. And
; every failure exit leaves the run unspent, which is exactly right: a
; half-built chain is rolled back by dskw_refat and nothing that was not
; written needs unwriting.
; -----------------------------------------------------------------------------
dskw_wdata:
    mov word [dskw_first], 0
    mov word [dskw_prev], 0
    mov word [dskw_rlen], 0     ; no data run pending
    mov ax, [dskw_seg]
    mov [dskw_wseg], ax         ; the ADVANCING source segment
    cmp word [dskw_rem], 0
    jne .clus
    clc                         ; an empty file owns no clusters at all
    ret
.full:
    mov ax, FERR_FULL
    stc
    ret
.ioerr:
    call dskw_ioerr             ; AX = FERR_IO / FERR_WPROT
    stc
    ret
.clus:
    call dskw_alloc             ; AX = a cluster, already marked EOC
    jnc .haveclus
    inc ax                      ; 0xFFFF -> 0: an I/O failure wearing the
    jz .ioerr                   ; allocator's refusal, and 'Disk full' is the
    jmp short .full             ; one thing it certainly is not
.haveclus:
    mov [dskw_cur], ax
    mov dx, ax
    cmp word [dskw_first], 0
    jne .link
    mov [dskw_first], ax        ; the chain's head
    jmp short .lba
.link:
    mov ax, [dskw_prev]
    call dskw_setfat            ; prev -> this one
    jc .ioerr                   ; a link that never landed leaves a chain that
                                ; stops short of its own data
.lba:
    mov ax, [dskw_cur]
    mov [dskw_prev], ax
    call KERNEL_SEG:cw_dsk_clus2lba ; AX = the cluster's first LBA
    jc .ioerr                   ; unreachable: dskw_alloc bounds the cluster
    mov [dskw_lba], ax
    mov al, [dsk_spc]           ; take = min(spc, sectors remaining)
    xor ah, ah
    cmp ax, [dskw_rem]
    jbe .take
    mov ax, [dskw_rem]
.take:
    mov [dskw_take], ax

    ; whole sectors of this take, APPENDED TO THE PENDING RUN rather than
    ; written here - see the header. CX is the count, [dskw_clba] the cursor.
    mov cx, ax
    call dskw_last_p            ; ZF=1 if this take ends in a partial sector
    jnz .whole
    dec cx                      ; that last sector goes through the stage
.whole:
    mov ax, [dskw_lba]          ; the cursor, set BEFORE the count is tested:
    mov [dskw_clba], ax         ; a take that is nothing but a partial sector
                                ; skips the walk and .tail still reads it
    or cx, cx                   ; (not jcxz: .tail is out of its reach now)
    jnz .sect
    jmp .tail
.sect:
    cmp word [dskw_rlen], 0
    je .newrun
    mov ax, [dskw_rlba]         ; does this sector continue the pending run?
    add ax, [dskw_rlen]
    cmp ax, [dskw_clba]
    jne .breakrun
    mov ax, [dskw_rcap]         ; ...and is there room left in it?
    cmp ax, [dskw_rlen]
    ja .append
.breakrun:
    call dskw_wflush            ; no: spend it and start again here
    jc .ioerr
    jmp .sect
.newrun:
    call dskw_runmax            ; AX = the cap, from the CURRENT wseg:src -
    or ax, ax                   ; which is where this run's first byte is
    jz .stg                     ; not even one sector fits its DMA page
    mov [dskw_rcap], ax
    mov ax, [dskw_clba]
    mov [dskw_rlba], ax         ; ([dskw_rlen] is already 0)
.append:
    mov ax, [dskw_rcap]
    sub ax, [dskw_rlen]         ; room in the run...
    cmp ax, cx
    jbe .grow
    mov ax, cx                  ; ...and what is left of this cluster's take
.grow:
    add [dskw_rlen], ax
    add [dskw_clba], ax
    sub cx, ax
    jnz .sect
    jmp short .tail
.jioerr:
    jmp .ioerr
.stg:                           ; one sector the 8237 cannot carry, alone,
    mov es, [dskw_wseg]         ; through the scratch - which is 512-aligned
    mov si, [dskw_src]          ; and therefore never straddles. No run can be
    push cx                     ; pending here: dskw_runmax answered 0, and it
    mov cx, 512                 ; is only asked with [dskw_rlen] = 0
    call dskw_stage             ; ES:SI -> LOW_SEG:dsk_secbuf, all 512 of it
    mov ax, LOW_SEG
    mov es, ax
    mov bx, dsk_secbuf
    mov cx, 1
    mov ax, [dskw_clba]
    call KERNEL_SEG:cw_disk_write
    pop cx
    jc .jioerr
    inc word [dskw_clba]
    add word [dskw_wseg], 32    ; 512 bytes = 32 paragraphs on
    dec cx
    jz .tail
    jmp .sect

    ; the partial final sector: staged zero-filled, so nothing of the
    ; caller's neighbourhood past EOF ever reaches the disk. The run goes
    ; first - its bytes come BEFORE these, and flushing it is also what
    ; advances [dskw_wseg] to where the tail's source actually is.
.tail:
    call dskw_last_p
    jnz .advance
    call dskw_wflush
    jc .ioerr
    mov es, [dskw_wseg]
    mov si, [dskw_src]
    mov cx, [dskw_tail]
    call dskw_stage             ; ES:SI -> LOW_SEG:dsk_secbuf, rest zeroed
    mov ax, LOW_SEG
    mov es, ax
    mov bx, dsk_secbuf
    mov cx, 1
    mov ax, [dskw_clba]         ; the sector the whole-sector walk stopped at
    call KERNEL_SEG:cw_disk_write
    jc .ioerr
.advance:
    mov ax, [dskw_take]
    sub [dskw_rem], ax
    jz .done
    jmp .clus                   ; (near: the loop body outruns a short jump)
.done:
    call dskw_wflush            ; the last run of the file
    jc .ioerr
    clc
    ret

; -----------------------------------------------------------------------------
; dskw_wflush - spend the pending data run (module-internal)
; in:  [dskw_rlba]/[dskw_rlen], the source at [dskw_wseg]:[dskw_src]
; out: CF=0 and [dskw_rlen] = 0, [dskw_wseg] advanced past it; CF=1 = I/O
; clobbers: flags
;
; A no-op with nothing pending, which is what lets every path out of the
; cluster loop call it without asking.
; -----------------------------------------------------------------------------
dskw_rflush:
    mov byte [dskw_fop], 0      ; ...and dskw_rdata's, which is the same run
    jmp short dskw_flushrun     ; arithmetic over the same words (SPEC.md
dskw_wflush:                    ; 18.4.2) - one body, the disk_read/disk_write
    mov byte [dskw_fop], 1      ; idiom, so the two cannot drift apart
dskw_flushrun:
    push ax
    push bx
    push cx
    push es
    mov cx, [dskw_rlen]
    jcxz .out
    mov ax, [dskw_rlba]
    mov es, [dskw_wseg]
    mov bx, [dskw_src]
    cmp byte [dskw_fop], 0      ; in: AX=LBA, CX=count, ES:BX
    je .rd
    call KERNEL_SEG:cw_disk_write
    jmp short .did
.rd:
    call KERNEL_SEG:cw_disk_read
.did:
    jc .err
    mov ax, [dskw_rlen]
    mov word [dskw_rlen], 0
    mov cl, 5
    shl ax, cl                  ; sectors -> paragraphs, 32 each
    add [dskw_wseg], ax
.out:
    pop es
    pop cx
    pop bx
    pop ax
    clc
    ret
.err:
    pop es
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_last_p - is the current take the file's last, with a partial sector?
; in:  [dskw_take], [dskw_rem], [dskw_tail]
; out: ZF=1 if this take ends the file AND the last sector is partial
; clobbers: flags
; -----------------------------------------------------------------------------
dskw_last_p:
    push ax
    mov ax, [dskw_take]
    cmp ax, [dskw_rem]
    jne .no                     ; more clusters follow: every sector is full
    cmp word [dskw_tail], 0
    je .no
    cmp ax, ax                  ; ZF=1
    jmp short .out
.no:
    or ax, 1                    ; ZF=0 (AX is a copy; the caller's is safe)
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_rbody - the read pipeline (module-internal; dskw_read saves regs)
; in:  SI -> name; [dskw_seg]:[dskw_src] = destination,
;      [dskw_caphi]:[dskw_cap] = its 32-bit capacity
; out: CF=0 with DX:AX = bytes read, or CF=1 with AX = FERR_*
;
; The mirror of the write loop, minus allocation: walk the chain the entry
; names, read whole sectors straight into the caller's buffer, and stage
; the file's partial final sector through dsk_secbuf so the copy out stops
; exactly at EOF. Any link outside [2, dsk_maxclus] while bytes remain is
; corruption, reported as FERR_IO (the read path's own verdict, SPEC.md 18).
;
; The size compare is 32-bit against the caller's capacity and happens
; BEFORE any data I/O, so FERR_BIG always leaves the destination untouched.
; A size field whose sector count would not fit 16 bits (>= 32MB) cannot be
; a real chain on any volume this kernel mounts (TotSec16 <= 65,535) and is
; refused as FERR_IO up front: a wrapped count could otherwise make the
; chain walk STOP EARLY and report success over a hostile entry (SPEC.md 18
; - every byte off the disk is hostile input).
; -----------------------------------------------------------------------------
dskw_rbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov di, dskw_n83
    call dskw_name83
    jnc .named
    mov ax, FERR_NAME
    stc
    ret
.named:
    call dskw_norm              ; the destination becomes seg:0..15 + a run
    call dskw_find
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    jc .out
    test byte [dskw_raw+DSK_R_ATTR], 0x18
    jz .isfile
    mov ax, FERR_PROT           ; a volume label or a subdirectory
    stc
.out:
    ret
.isfile:
    mov ax, [dskw_raw+DSK_R_SIZE]       ; the 32-bit size...
    mov dx, [dskw_raw+DSK_R_SIZE+2]
    cmp dx, [dskw_caphi]                ; ...against the 32-bit capacity,
    jb .fitsbuf                         ; BEFORE any data I/O
    ja .toobig
    cmp ax, [dskw_cap]
    jbe .fitsbuf
.toobig:                        ; too big for the buffer: nothing is written
    mov ax, FERR_BIG
    stc
    ret
.fitsbuf:
    mov [dskw_len], ax
    mov [dskw_lenhi], dx
    or ax, dx
    jnz .sized
    xor ax, ax                  ; a 0-byte file: no chain to walk, DX:AX = 0
    xor dx, dx
    clc
    ret
.sized:
    call dskw_size32            ; [dskw_rem] / [dskw_tail]
    jnc .secok
    mov ax, FERR_IO             ; >= 32MB: impossible on this media, so the
    stc                         ; entry is hostile rather than merely large
    ret
.secok:
    mov ax, [dskw_rem]          ; the widget's scale (SPEC.md 12.8) - a 116KB
    call KERNEL_SEG:cw_fpg_begin    ; module is 232 sectors and a minute of
                                ; frozen screen; dskw_read_x ends it
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_cur], ax
    call dskw_rdata             ; walk the chain into the caller's buffer
    jc .out
    mov ax, [dskw_len]          ; bytes read = the file's size, 32-bit
    mov dx, [dskw_lenhi]
    clc
    ret

; -----------------------------------------------------------------------------
; dskw_rdata - read [dskw_rem] sectors along the chain at [dskw_cur]
; in:  [dskw_cur] = first cluster, [dskw_rem]/[dskw_tail] sized by the
;      caller, [dskw_seg]:[dskw_src] = the destination, normalised
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX (the output), BX, CX, DI, ES, CF
;
; dskw_wdata's twin, and coalescing the same way (SPEC.md 18.4.2): whole
; sectors are APPENDED TO A PENDING RUN rather than read here, and the run is
; spent by dskw_rflush when the LBA stops being contiguous, when its DMA page
; is full, or at the end. A run crosses CLUSTER boundaries, which is the whole
; point - a file laid down contiguously is one run per track. The partial
; final sector stages through dsk_secbuf with only its live bytes copied out,
; so no memory past EOF reaches the caller - and there is nothing here to
; zero, because the buffer is the caller's.
; -----------------------------------------------------------------------------
dskw_rdata:
    mov word [dskw_rlen], 0     ; no data run pending
    mov ax, [dskw_seg]
    mov [dskw_wseg], ax         ; the ADVANCING destination segment
    jmp short .clus
.corrupt:
    mov ax, FERR_IO             ; any link outside [2, maxclus] while bytes
    stc                         ; remain is corruption (SPEC.md 18)
    ret
.ioerr:
    call dskw_ioerr
    stc
    ret
.clus:
    mov ax, [dskw_cur]
    cmp ax, 2
    jb .corrupt
    cmp ax, [dsk_maxclus]
    ja .corrupt
    call KERNEL_SEG:cw_dsk_clus2lba
    jc .corrupt
    mov [dskw_lba], ax
    mov al, [dsk_spc]           ; take = min(spc, sectors remaining)
    xor ah, ah
    cmp ax, [dskw_rem]
    jbe .take
    mov ax, [dskw_rem]
.take:
    mov [dskw_take], ax
    mov cx, ax
    call dskw_last_p            ; ZF=1 if this take ends in a partial sector
    jnz .whole
    dec cx                      ; that last sector comes in through the stage
.whole:
    mov ax, [dskw_lba]          ; the cursor, set BEFORE the count is tested:
    mov [dskw_clba], ax         ; a take that is nothing but a partial sector
                                ; skips the walk and .tail still reads it
    or cx, cx                   ; (not jcxz: .tail is out of its reach now)
    jnz .sect
    jmp .tail
.sect:
    cmp word [dskw_rlen], 0
    je .newrun
    mov ax, [dskw_rlba]         ; does this sector continue the pending run?
    add ax, [dskw_rlen]
    cmp ax, [dskw_clba]
    jne .breakrun
    mov ax, [dskw_rcap]         ; ...and is there room left in it?
    cmp ax, [dskw_rlen]
    ja .append
.breakrun:
    call dskw_rflush            ; no: spend it and start again here
    jc .ioerr
    jmp .sect
.newrun:
    call dskw_runmax            ; AX = the cap, from the CURRENT wseg:src -
    or ax, ax                   ; which is where this run's first byte is
    jz .stg                     ; not even one sector fits its DMA page
    mov [dskw_rcap], ax
    mov ax, [dskw_clba]
    mov [dskw_rlba], ax         ; ([dskw_rlen] is already 0)
.append:
    mov ax, [dskw_rcap]
    sub ax, [dskw_rlen]         ; room in the run...
    cmp ax, cx
    jbe .grow
    mov ax, cx                  ; ...and what is left of this cluster's take
.grow:
    add [dskw_rlen], ax
    add [dskw_clba], ax
    sub cx, ax
    jnz .sect
    jmp short .tail
.jioerr:
    jmp .ioerr
.stg:                           ; one sector the 8237 cannot carry, alone,
    mov ax, LOW_SEG             ; through the scratch - which is 512-aligned
    mov es, ax                  ; and therefore never straddles. No run can be
    mov bx, dsk_secbuf          ; pending here: dskw_runmax answered 0, and it
    push cx                     ; is only asked with [dskw_rlen] = 0
    mov cx, 1
    mov ax, [dskw_clba]
    call KERNEL_SEG:cw_disk_read
    pop cx
    jc .jioerr
    mov es, [dskw_wseg]
    mov di, [dskw_src]
    push cx
    mov cx, 512
    call dskw_fetch             ; ...and up to the caller, all 512 of it
    pop cx
    inc word [dskw_clba]
    add word [dskw_wseg], 32    ; 512 bytes = 32 paragraphs on
    dec cx
    jz .tail
    jmp .sect

    ; the partial final sector: only its live bytes reach the caller. The run
    ; goes first - its bytes come BEFORE these, and flushing it is also what
    ; advances [dskw_wseg] to where the tail's destination actually is.
.tail:
    call dskw_last_p
    jnz .advance
    call dskw_rflush
    jc .ioerr
    mov ax, LOW_SEG
    mov es, ax
    mov bx, dsk_secbuf
    mov cx, 1
    mov ax, [dskw_clba]         ; the sector the whole-sector walk stopped at
    call KERNEL_SEG:cw_disk_read              ; the last sector, whole, into the scratch
    jc .ioerr
    mov es, [dskw_wseg]
    mov di, [dskw_src]
    mov cx, [dskw_tail]
    call dskw_fetch             ; ...and only its live bytes to the caller
.advance:
    mov ax, [dskw_take]
    sub [dskw_rem], ax
    jz .rdone
    mov ax, [dskw_cur]          ; follow the chain
    call KERNEL_SEG:cw_dsk_next_clus
    jc .corrupt
    mov [dskw_cur], ax
    jmp .clus                   ; (near: the loop body outruns a short jump)
.rdone:
    call dskw_rflush            ; the last run of the file
    jc .ioerr
    clc
    ret

; -----------------------------------------------------------------------------
; dskw_norm - reduce [dskw_seg]:[dskw_src] to the same address with an offset
;             of 0..15 (module-internal)
; out: nothing (all registers preserved)
;
; This is the whole of SPEC.md 18.4.1's unification. Both transfer loops hold
; the offset and walk the SEGMENT, which works for any offset below 16
; (offset + 512 can never carry), so normalising once at the top of each
; pipeline is what lets one contract serve a buffer inside the caller's own
; segment and a heap claim of any size alike.
; -----------------------------------------------------------------------------
dskw_norm:
    push ax
    push cx
    mov ax, [dskw_src]
    mov cl, 4
    shr ax, cl                  ; whole paragraphs of the offset...
    add [dskw_seg], ax          ; ...move into the segment
    and word [dskw_src], 15
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_cross - would a 512-byte int 13h transfer at [dskw_wseg]:[dskw_src]
;              straddle a 64KB PHYSICAL boundary? (module-internal)
; out: CF=1 it would - stage it; CF=0 hand it to the controller directly
; clobbers: flags
;
; The 8237 has no register for address bits 16..19: the page port holds them
; and the chip never carries into them, so a transfer that crosses a page
; wraps to the start of that page and moves the wrong memory - int 13h
; answers 09h (SPEC.md 18.1, and guard 6 in kernel.asm).
;
; The kernel keeps every base of its OWN making 512-aligned, which makes this
; unreachable for them - 64KB is a whole number of sectors, so a sector that
; starts 512-aligned always ends inside its own page. Since SPEC.md 18.4.1
; the buffer is the CALLER's, at any offset it likes, and this is what makes
; that promise true rather than nearly true. It was already nearly true and
; already wrong: an app reading into its own bss straddled whenever its
; region happened to land near a page edge, and the symptom was a "Disk
; error" that moved when anything else in the tree changed size.
;
; The test is one add: the low 16 bits of seg*16 + off ARE the offset within
; the 64KB page, because the carry out of that add is the page number.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dskw_runmax - how many sectors one int 13h may carry from here
; out: AX = 0..127; 0 = not even one (this sector straddles a 64KB DMA page)
; clobbers: AX (the output), flags
;
; dskw_cross's question asked of a RUN rather than a sector, which is what
; lets the write path issue one transfer per cluster instead of one per
; sector. That was free when dsk_xfer looped int 13h itself; it stopped being
; free when a driver-backed volume started handing the whole count to DSV_BLK
; in one call (SPEC.md 18.7), where a run is one IDE command instead of N.
;
; TWO bounds, and both are real:
;   - the 8237 never carries into the page port, so the run must end inside
;     the 64KB page it starts in. Same one-add derivation as dskw_cross: the
;     low 16 bits of seg*16 + off ARE the offset within the page.
;   - dsk_xfer walks BX from the caller's offset, which dskw_norm leaves at
;     0..15 (SPEC.md 18.4.1). 127 sectors is the most that cannot overflow
;     it, and it is also the most a real controller will do in one go.
; -----------------------------------------------------------------------------
dskw_runmax:
    push cx
    mov ax, [dskw_wseg]
    mov cl, 4
    shl ax, cl
    add ax, [dskw_src]          ; AX = the offset within the DMA page
    neg ax                      ; ...so this is the bytes left in it, with 0
    jz .whole                   ; meaning a whole page rather than none
    mov cl, 9
    shr ax, cl
    jmp short .cap
.whole:
    mov ax, 128
.cap:
    cmp ax, 127
    jbe .out
    mov ax, 127
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
dskw_cross:
    push ax
    push cx
    mov ax, [dskw_wseg]
    mov cl, 4
    shl ax, cl                  ; the carry out is the page, and the page is
    add ax, [dskw_src]          ; exactly what we are NOT asking about
    cmp ax, 0xFE01              ; CF=1 = the sector ends inside this page...
    cmc                         ; ...so CF=1 now means it does not
    pop cx                      ; (pops do not touch flags)
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_size32 - the sector count and partial-tail length of a 32-bit byte
;               count (module-internal)
; in:  [dskw_lenhi]:[dskw_len] = the count
; out: CF=0 with [dskw_rem] = sectors and [dskw_tail] = bytes in a partial
;      final sector (0 = none); CF=1 when the sector count would not fit 16
;      bits (>= 32MB), in which case [dskw_rem] is left alone and every
;      caller refuses the job outright
; clobbers: flags
; -----------------------------------------------------------------------------
dskw_size32:
    push ax
    push cx
    push dx
    mov ax, [dskw_len]
    mov dx, [dskw_lenhi]
    mov cx, ax
    and cx, 511                 ; bytes in a partial final sector
    mov [dskw_tail], cx
    mov cx, 9                   ; sectors = count >> 9 (32-bit shift)...
.shr:
    shr dx, 1
    rcr ax, 1
    loop .shr
    cmp word [dskw_tail], 0
    je .whole
    add ax, 1                   ; ...+ 1 for the partial sector
    adc dx, 0
.whole:
    or dx, dx
    jnz .over
    mov [dskw_rem], ax
    clc
.out:
    pop dx                      ; (pops do not touch flags)
    pop cx
    pop ax
    ret
.over:
    stc
    jmp short .out

; -----------------------------------------------------------------------------
; dskw_dbody - the delete pipeline (module-internal)
; in:  SI -> name
; out: CF + AX = FERR_*
;
; Order is the reverse of a write's, for the same reason: mark the entry
; deleted FIRST (one sector write, the commit), then free the chain. A
; crash between the two leaks lost clusters; the opposite order would leave
; a live entry pointing at free space, which the next write could hand out
; to someone else.
; -----------------------------------------------------------------------------
dskw_dbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov di, dskw_n83
    call dskw_name83
    jnc .named
.badarg:
    mov ax, FERR_NAME
    stc
    ret
.named:
dskw_dbody_n83:                 ; dskw_n83 already filled (dskw_rmtree)
    call dskw_find
    jc .out
    jc .out
    test byte [dskw_raw+DSK_R_ATTR], DSKW_PROT
    jz .live
    mov ax, FERR_PROT           ; read-only, hidden, system, label or dir
    stc
.out:
    ret
.live:
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_oldclus], ax
    mov byte [dskw_raw], 0xE5   ; the deleted mark (SPEC.md 19's species)
    mov byte [dskw_zapnext], 0
    call dskw_ent_store         ; the commit
    jc .out
    mov ax, [dskw_oldclus]
    cmp ax, 2
    jb .done
    cmp ax, [dsk_maxclus]
    ja .done
    call dskw_free_chain
    call dskw_flush_x
    jc .lateio
.done:
    call dskw_sync_x
    xor ax, ax
    clc
    ret
.lateio:
    push ax
    call dskw_sync_x
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_nbody - the rename pipeline (module-internal)
; in:  SI -> old name, DI -> new name
; out: CF + AX = FERR_*
;
; Name bytes only: the chain, the size, the ATTRIBUTE and the timestamps are
; the file's own and stay untouched. The new name is parsed and its absence
; proved BEFORE the old entry is located, so the second scan cannot disturb
; the location the write uses.
;
; Because it moves nothing but the name, this is the one path whose
; protection mask is DSKW_NPROT (0x0F) rather than DSKW_PROT (0x1F): a
; subdirectory may be renamed. See DSKW_NPROT up top for why that is safe
; and SPEC.md 18.4 for why replace and delete may not do the same.
; -----------------------------------------------------------------------------
dskw_nbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    push si
    mov si, di                  ; the new name first: parse it, then prove
    mov di, dskw_n83            ; it is free
    call dskw_name83
    pop si
    jnc .named
.badarg:
    mov ax, FERR_NAME
    stc
    ret
.named:
    push si
    mov si, dskw_n83            ; keep it: dskw_find works out of dskw_n83,
    mov di, dskw_n83b           ; which the old-name lookup is about to reuse
    mov cx, 11
    call dskw_copy11
    pop si
    call dskw_find
    jc .vacant                  ; the new name is free (or the scan failed)
    mov ax, FERR_EXIST
    stc
    ret
.vacant:
    cmp ax, FERR_NOENT
    je .old
.out:
    stc                         ; FERR_IO from the scan
    ret
.old:
    mov di, dskw_n83            ; now the old name
    call dskw_name83
    jc .badarg
    call dskw_find
    jc .out
    jc .out
    test byte [dskw_raw+DSK_R_ATTR], DSKW_NPROT
    jz .patch                   ; 0x0F, not 0x1F: a FOLDER may be renamed
    mov ax, FERR_PROT
    stc
    ret
.patch:
    push si
    mov si, dskw_n83b
    mov di, dskw_raw            ; patch the 11 name bytes in the staged copy
    mov cx, 11
    call dskw_copy11
    pop si
    mov byte [dskw_zapnext], 0
    call dskw_ent_store
    jc .out
    call dskw_sync_x
    xor ax, ax
    clc
    ret

; -----------------------------------------------------------------------------
; dskw_sync - make the read-path caches a mount snapshot again (SPEC.md 18.4)
; in:  nothing (the volume just changed under disk_dir / disk_nfiles)
; out: nothing (all registers preserved); CF is NOT propagated - the write
;      already happened, and a mount that now fails is the disk's news, not
;      the write's
;
; Remounting rather than patching is the whole coherence story: disk_dir,
; disk_icons and disk_nfiles stay EXACTLY what a mount produced, so no new
; staleness rule enters the kernel and a written .o88 gets its icon
; harvested for free. It cannot repaint the Disk window (the caller may
; hold the gfx lock) - SPEC.md 22 covers what the user sees.
;
; WHEN that rebuild happens is fmv_gneed's question, and the answer is
; usually "not now" (SPEC.md 18.4 rule 3). The invariant above is about what
; the snapshot IS, never about how soon: a mount snapshot or nothing at all,
; and the empty-listing-plus-debt this leaves is the second of those - the
; identical state a quiet mount publishes (18.9), paid by dsk_relist. What
; that buys is every write the user cannot see, and the Control Panel is the
; measured case: 3 mounts and 47 sectors to save SYSTEM.CFG while the user
; is on B:, none of which changed a pixel.
;
; [dsk_keepcwd] is raised for the same reason dsk_chdir raises it, and
; leaving it out was a real bug (SPEC.md 18.4/19.2): a bare disk_mount
; resets [dsk_cwd] to the root BY DESIGN, so every successful write, delete,
; rename or mkdir performed inside a subdirectory silently teleported the
; volume back to the root. The window kept showing a folder it was no longer
; listing, and the next write resolved its name in the root instead - a
; wrong-directory write, not just a display glitch.
; -----------------------------------------------------------------------------
dskw_sync_x:
    call fmv_mark               ; every Disk window showing THIS folder now
                                ; owes a refresh (SPEC.md 22.8) - marked
                                ; ahead of the batch test, because the folder
                                ; changed whether or not the remount is
                                ; deferred, and a tree copy walks folders
                                ; the single remount at its end has left
    cmp byte [dskw_batch], 0    ; a tree copy remounts ONCE, when it ends -
    jne .defer                  ; not after every one of its files (22.3)
    call fmv_gneed              ; is anything ON SCREEN drawn from the global
    jc dskw_remount_x           ; listing? then rebuild it now, as we always
                                ; did - the Standard File dialog's New Folder
                                ; is that case, and it is a write made while
                                ; the dialog is up
    mov word [disk_nfiles], 0   ; ...otherwise publish the EMPTY listing and
    mov byte [dsk_lstale], 1    ; the debt, exactly the state a quiet mount
                                ; leaves (18.9): a wrong listing is worse than
                                ; no listing, and dsk_relist is the payment
.defer:
    ret

; -----------------------------------------------------------------------------
; dskw_remount_x - rebuild the global snapshot where we stand (SPEC.md 18.4)
; in:  nothing
; out: nothing (all registers preserved); CF is NOT propagated
;
; The remount half of dskw_sync, split out because dsk_relist has to be able
; to FORCE it: dsk_relist used to tail-call dskw_sync, and once dskw_sync
; learned to defer, that pair would have cleared the debt and then declined to
; pay it - a listing that is never rebuilt, which is the one failure this
; whole mechanism must not have.
; -----------------------------------------------------------------------------
dskw_remount_x:
    push ax
    push dx
    mov dl, [disk_drive]
    mov byte [dsk_keepcwd], 1   ; stay where the write happened (18.4)
    call KERNEL_SEG:cw_disk_mount
    mov byte [dsk_keepcwd], 0
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_stat - does NAME exist in the current directory? (SPEC.md 18.4)
; in:  SI -> a NUL-terminated 8.3 name
; out: CF=0 = it exists, with AL = its raw attribute byte, BX = its first
;      cluster and DX:CX = its size; CF=1 with AX = FERR_NODISK / FERR_NAME
;      / FERR_NOENT / FERR_IO
; clobbers: AX, BX, CX, DX (the outputs), CF
;
; The lookup half of every other operation in this module, published because
; filecp.inc has to ask "would this replace something, and is that something
; a folder?" BEFORE it writes anything - which is the whole overwrite prompt
; (SPEC.md 22.3), and which no existing entry point can answer without also
; doing the thing.
; -----------------------------------------------------------------------------
dskw_stat_x:
    push si
    push di
    cmp byte [dsk_mntok], 1
    jne .nodisk
    mov di, dskw_n83
    call dskw_name83
    jc .badname
    call dskw_find              ; ...which now leaves the 32 raw bytes in
    jc .fail                    ; dskw_raw itself (AX = FERR_* on failure)
    mov bx, [dskw_raw+DSK_R_CLUS]
    mov cx, [dskw_raw+DSK_R_SIZE]
    mov dx, [dskw_raw+DSK_R_SIZE+2]
    mov al, [dskw_raw+DSK_R_ATTR]
    clc
    jmp short .out
.badname:
    mov ax, FERR_NAME
    jmp short .fail
.nodisk:
    mov ax, FERR_NODISK
.fail:
    stc
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; dskw_clbytes - bytes per cluster
; out: AX = [dsk_spc] * 512 (a power of two, <= 32,768 by mount rule 8)
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
dskw_clbytes:
    push cx
    mov al, [dsk_spc]
    xor ah, ah
    mov cl, 9
    shl ax, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; dskw_read_at - read one chunk of a file, by BYTE OFFSET (SPEC.md 18.4.4)
; in:  SI -> a NUL 8.3 name, ES:BX -> the buffer, CX = its capacity in bytes,
;      DX:AX = the offset into the file to read from
; out: CF=0 with DX:AX = the bytes delivered, 0 meaning the offset is at or
;      past the end; CF=1 with AX = FERR_*
; clobbers: AX, DX (the outputs), CF
;
; The read half of what SPEC.md 22.5's copy has had internally since it was
; written, and what OSAPI_FILE_READ could never express: that call reads a
; WHOLE file into a buffer that must hold it, so the largest file a package
; could touch was the largest claim it could take. A 116KB module on a small
; machine simply could not be opened.
;
; **STATELESS, AND THE OFFSET IS THE WHOLE ARGUMENT.** fcp_rdnext resumes
; through [dsk_chain_end] and three module words, which is right for a caller
; that is the only thing running; it is wrong for a published slot, because
; the loop this exists for is read -> write -> read and the WRITE walks
; directories and the FAT. A resume token living in the kernel would be
; destroyed by exactly the work it is feeding - the same reasoning that made
; OSAPI_FILE_FIND take an ordinal (SPEC.md 19.7.1).
;
; The price is walking the chain from the front on every call, and it is
; smaller than it looks: the walk is FAT lookups, the FAT window is resident,
; and the caller is spending 65 ms a sector (PERFORMANCE.md - a sector inside
; a well-coalesced run; 238 was the AL bug, not the drive) on the data
; this is handing it. A 116KB file in 32KB chunks is four walks averaging
; ~116 clusters - a few hundred word reads against twelve seconds of floppy.
;
; TWO PRECONDITIONS, both refused rather than mis-read, and both the same
; discipline dskw_append carries at the other end: the offset and the
; capacity must each be a whole number of CLUSTERS. That is what makes the
; resume point exact and lets one dsk_read_chain call serve a chunk. A caller
; asks OSAPI_FILE_DFREE for the cluster size, which has always answered it.
; The FINAL chunk is the exception the arithmetic makes for itself: the take
; is clamped to what is left of the file, so a tail shorter than a cluster is
; delivered whole and the next call answers 0.
; -----------------------------------------------------------------------------
dskw_read_at_x:
    push bx
    push cx
    push si
    push di
    push bp
    mov [dwr_buf], bx
    mov [dwr_cap], cx
    mov [dwr_off], ax
    mov [dwr_off+2], dx

    call dskw_clbytes           ; AX = bytes per cluster on this volume
    mov [dwr_clb], ax
    test ax, ax
    jz .badarg                  ; an unmounted volume has no cluster size

    call dskw_stat_x            ; BX = first cluster, DX:CX = the size
    jc .err
    mov [dwr_clus], bx
    mov [dwr_sz], cx
    mov [dwr_sz+2], dx

    ; AT OR PAST THE END IS TESTED FIRST, AND THE ORDER IS THE CONTRACT.
    ; A normal answer rather than an error - it is how a caller's loop
    ; terminates - and it has to be decided BEFORE the cluster-alignment
    ; tests below, because the final chunk is exactly what makes the next
    ; offset unaligned. The take is clamped to what is left of the file, so a
    ; tail shorter than a cluster leaves the offset mid-cluster; asked about
    ; that offset, the aligned-offset test answered FERR_NAME and the loop
    ; that had just copied the file whole reported a failure on it. Nothing
    ; is read here, so there is nothing to align: the preconditions exist to
    ; make a resume point exact, and past the end there is no resume.
    mov ax, [dwr_off+2]
    cmp ax, dx
    ja .eof
    jb .inrange
    mov ax, [dwr_off]
    cmp ax, cx
    jae .eof
.inrange:
    mov cx, [dwr_clb]
    mov ax, [dwr_off]           ; the offset must land on a cluster boundary
    xor dx, dx
    div cx
    or dx, dx
    jnz .badarg
    mov ax, [dwr_cap]           ; ...and so must the capacity, or a chunk
    xor dx, dx                  ; would not end where the next one starts
    div cx
    or dx, dx
    jnz .badarg
    test ax, ax
    jz .badarg                  ; a capacity under one cluster reads nothing

    mov ax, [dwr_sz]            ; take = min(size - offset, capacity)
    mov dx, [dwr_sz+2]
    sub ax, [dwr_off]
    sbb dx, [dwr_off+2]
    or dx, dx
    jnz .wholecap               ; more than 64KB left: the buffer is the bound
    cmp ax, [dwr_cap]
    jbe .take
.wholecap:
    mov ax, [dwr_cap]
.take:
    mov [dwr_take], ax

    ; --- walk to the cluster holding the offset ------------------------------
    ; offset / cluster_bytes, and one div is enough: a volume caps at 65,535
    ; sectors (SPEC.md 18.7), so the offset is under 33MB and its high word is
    ; under 512 - which is at or below every cluster size there is.
    mov ax, [dwr_off]
    mov dx, [dwr_off+2]
    div word [dwr_clb]          ; AX = clusters to skip
    mov cx, ax
    mov ax, [dwr_clus]
    jcxz .atclus
.skip:
    call KERNEL_SEG:cw_dsk_next_clus    ; AX = the next cluster; CF=1 = the
    jc .corrupt                 ; chain ended before the size said it would
    cmp ax, 2
    jb .corrupt
    cmp ax, [dsk_maxclus]
    ja .corrupt
    loop .skip
.atclus:
    mov [dwr_clus], ax

    ; --- and read it ---------------------------------------------------------
    ; dsk_read_chain moves whole SECTORS, so a tail shorter than one is
    ; rounded up. That cannot overrun the caller: the capacity is a cluster
    ; multiple, the take is at most the capacity, and a cluster is a whole
    ; number of sectors.
    mov ax, [dwr_take]
    add ax, 511
    mov cl, 9
    shr ax, cl
    mov dx, ax                  ; DX = sectors
    mov ax, [dwr_clus]
    mov bx, [dwr_buf]
    call KERNEL_SEG:cw_dsk_read_chain
    jc .ioerr
    mov ax, [dwr_take]
    xor dx, dx
    clc
    jmp short .out
.eof:
    xor ax, ax
    xor dx, dx
    clc
    jmp short .out
.badarg:
    mov ax, FERR_NAME           ; a bad ARGUMENT, and FERR_NAME is this API's
    jmp short .fail             ; word for that (SPEC.md 20.3)
.corrupt:
.ioerr:
    mov ax, FERR_IO
.fail:
    stc
.err:
.out:
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_append - add CX bytes to the end of an existing file (SPEC.md 22.5)
; in:  SI -> a NUL 8.3 name, ES:BX -> the bytes, CX = count (>= 1)
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
;
; MODULE-INTERNAL - no API slot - because it carries a precondition no
; published entry point could: the file's current size must be a whole
; number of CLUSTERS, so the append starts on a fresh one and never has to
; fill a partial sector inside a chain that is already there. filecp.inc's
; copy guarantees it by chunking at a cluster multiple (SPEC.md 22.5), and
; anything else is refused rather than mis-written.
;
; This is what removes the BUFFER's ceiling from a copy. dskw_write has had
; no 64KB ceiling of its own since SPEC.md 18.4.1, but the copy buffer is a
; heap claim of whatever size the machine could spare, and a file may be
; larger than it; a file of any size is moved as a run of appends, so the
; buffer bounds the CHUNK rather than the file. CX stays 16 bits for exactly
; that reason - a chunk is a buffer-full, and fcp_chunkset already caps it at
; 127 sectors.
;
; The commit order is SPEC.md 18.4's, in the only shape an append can have:
; the new sub-chain is allocated and written first (a crash there leaks
; clusters, which is the failure the rule exists to prefer), the FAT is
; flushed so it is durable, the last existing cluster is linked to it and the
; FAT flushed again - and only then does the directory entry take the new
; size, which is the single sector write that makes those bytes part of the
; file.
; -----------------------------------------------------------------------------
dskw_append_x:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [dskw_seg], es
    mov [dskw_src], bx
    mov [dskw_len], cx
    mov word [dskw_lenhi], 0    ; a chunk is 16-bit by contract, above
    call dskw_apbody
    call KERNEL_SEG:cw_fpg_end  ; SPEC.md 12.8, as the other two pipelines
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

dskw_apbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov di, dskw_n83
    call dskw_name83
    jc .badarg
    call dskw_find
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    jc .out
    test byte [dskw_raw+DSK_R_ATTR], DSKW_PROT
    jnz .prot

    call dskw_clbytes           ; the precondition, checked not assumed: a
    dec ax                      ; power of two, so the modulo is a mask, and
    and ax, [dskw_raw+DSK_R_SIZE]   ; only the low word can carry a remainder
    jnz .badarg

    call dskw_norm              ; the source becomes seg:0..15 + a segment run
    call dskw_size32            ; size the job exactly as dskw_wbody does
    jc .badarg                  ; (unreachable: a chunk is one 16-bit count)
    mov ax, [dskw_rem]          ; ...and arm the widget the same way, so a
    call KERNEL_SEG:cw_fpg_begin    ; file-manager copy reports chunk by chunk
    mov word [dskw_zapnext], 0

    call dskw_wdata             ; the new sub-chain, allocated and written
    jc .rollback
    call dskw_flush_x           ; ...and durable
    jc .rollback

    mov ax, [dskw_raw+DSK_R_CLUS]
    or ax, ax
    jz .sethead                 ; the file was empty: the ENTRY takes the
    mov cx, DSKW_RT_MAX         ; head, and there is nothing to link to
.walk:
    mov dx, ax                  ; DX = the last cluster seen
    call KERNEL_SEG:cw_dsk_next_clus
    jc .linkdx
    cmp ax, 2
    jb .linkdx
    cmp ax, [dsk_maxclus]
    ja .linkdx
    loop .walk
    mov ax, FERR_IO             ; a chain longer than any real disk: a cycle
    stc
    ret
.linkdx:
    mov ax, dx
    mov dx, [dskw_first]
    call dskw_setfat            ; the join
    jc .out
    call dskw_flush_x
    jc .out
    jmp short .size
.sethead:
    mov ax, [dskw_first]
    mov [dskw_raw+DSK_R_CLUS], ax
.size:
    mov ax, [dskw_raw+DSK_R_SIZE]   ; the commit: the entry takes the bytes
    add ax, [dskw_len]
    mov [dskw_raw+DSK_R_SIZE], ax
    mov ax, [dskw_raw+DSK_R_SIZE+2]
    adc ax, 0
    mov [dskw_raw+DSK_R_SIZE+2], ax
    call dskw_ent_store
    jc .out
    call dskw_sync_x
    xor ax, ax
    clc
    ret
.badarg:
    mov ax, FERR_NAME
    stc
    ret
.prot:
    mov ax, FERR_PROT
    stc
.out:
    ret
.rollback:
    push ax
    call dskw_refat             ; drop the half-built sub-chain
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_commit - write the directory entry that switches the file over
; in:  [dskw_new], [dskw_first], [dskw_len]; for a replace, dskw_raw holds
;      the existing entry and [dskw_dsec]/[dskw_doff] locate it; for a
;      create, [dskw_fsec]/[dskw_foff] locate the free slot
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_commit:
    push bx
    push cx
    push si
    push di
    call dskw_now               ; [dskw_date] / [dskw_time] from SPEC.md 37
    mov byte [dskw_zapnext], 0
    cmp byte [dskw_new], 0
    je .patch

    ; a fresh entry: 32 zero bytes, the name, ARCHIVE, all three stamps
    mov di, dskw_raw
    mov cx, 16
    xor ax, ax
.zero:
    mov [di], ax
    inc di
    inc di
    loop .zero
    mov si, dskw_n83
    mov di, dskw_raw
    mov cx, 11
    call dskw_copy11
    mov byte [dskw_raw+DSK_R_ATTR], DSKW_ARCH
    cmp byte [dskw_syswr], 0    ; ...or hidden + system, if the kernel is
    je .notsys1                 ; making one of its own (SPEC.md 19.6)
    mov byte [dskw_raw+DSK_R_ATTR], DSKW_SYSAT
.notsys1:
    cmp byte [dskw_isdir], 0     ; a folder is ATTR_DIRECTORY and
    je .notdir1                 ; never ARCHIVE (SPEC.md 18.5)
    mov byte [dskw_raw+DSK_R_ATTR], DSKW_DIR
.notdir1:
    mov ax, [dskw_time]
    mov [dskw_raw+DSK_R_CTIME], ax
    mov ax, [dskw_date]
    mov [dskw_raw+DSK_R_CDATE], ax
    mov [dskw_raw+DSK_R_ADATE], ax
    mov ax, [dskw_fsec]         ; the slot dskw_find spotted on its scan
    mov [dskw_dsec], ax
    mov ax, [dskw_foff]
    mov [dskw_doff], ax
    mov al, [dskw_fend]         ; taking the 0x00 terminator means the next
    mov [dskw_zapnext], al      ; entry must be zeroed to remain one

.patch:
    mov ax, [dskw_first]
    mov [dskw_raw+DSK_R_CLUS], ax
    mov ax, [dskw_len]          ; the size field is 32 bits, and since
    mov [dskw_raw+DSK_R_SIZE], ax       ; SPEC.md 18.4.1 so is the count
    mov ax, [dskw_lenhi]                ; that reaches it
    mov [dskw_raw+DSK_R_SIZE+2], ax
    mov ax, [dskw_time]
    mov [dskw_raw+DSK_R_WTIME], ax
    mov ax, [dskw_date]
    mov [dskw_raw+DSK_R_WDATE], ax
    mov [dskw_raw+DSK_R_ADATE], ax
    cmp byte [dskw_isdir], 0
    jne .noarch                 ; ORing ARCHIVE onto a directory
    or byte [dskw_raw+DSK_R_ATTR], DSKW_ARCH
.noarch:
    call dskw_ent_store         ; CF + AX
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_find - locate a directory entry, and the first free slot on the way
; in:  dskw_n83 = the 11-byte raw name to match
; out: CF=0 - found: [dskw_dsec] = its sector LBA, [dskw_doff] = its offset
;      within that sector. CF=1 with AX = FERR_NOENT (scanned to the end)
;      or FERR_IO. Either way [dskw_fsec]/[dskw_foff] hold the first
;      reusable slot (FFFFh in fsec = none) and [dskw_fend] says whether
;      that slot is the 0x00 end-of-directory marker.
; clobbers: AX (the output), CF
;
; The scan follows SPEC.md 19's species rules, including the one that
; matters most here: a 0x00 first byte ends the directory, so nothing past
; it is ever matched OR reused.
; -----------------------------------------------------------------------------
dskw_find:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov word [dskw_fsec], 0xFFFF
    mov byte [dskw_fend], 0
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dsk_cwd]           ; the CURRENT directory, not the root
    call KERNEL_SEG:cw_dsk_dirw_start ; (SPEC.md 19.2 - one walker, both shapes)
.sector:
    call KERNEL_SEG:cw_dsk_dirw_get ; dsk_secbuf = the next sector (19.2.1)
    jnc .secok
    or ax, ax
    jz .noent                   ; the directory ended
    jmp short .ioerr
.secok:
    mov [dskw_cursec], ax
    mov si, dsk_secbuf
    mov cx, 16                  ; entries per sector
.entry:
    mov al, [es:si]
    test al, al
    jz .endmark
    cmp al, 0xE5
    je .free
    mov al, [es:si+DSK_R_ATTR]
    and al, 0x3F
    cmp al, 0x0F
    je .next                    ; an LFN entry: never matched, never reused
    call dskw_cmp               ; ES:SI vs dskw_n83
    jne .next
    mov ax, [dskw_cursec]       ; found
    mov [dskw_dsec], ax
    mov ax, si
    sub ax, dsk_secbuf
    mov [dskw_doff], ax
    mov di, dskw_raw            ; ...and TAKE THE ENTRY WHILE IT IS HERE.
    mov cx, 32                  ; dsk_secbuf holds this very sector - we read
    call KERNEL_SEG:cw_dsk_copy_in  ; it two instructions ago - and every
    clc                         ; caller that succeeds here wanted these 32
    jmp short .out              ; bytes next. dskw_ent_load used to re-read
                                ; the sector to get them, which on the field
                                ; machine is a whole REVOLUTION: the trace of
                                ; a 16KB read showed LBA 5 twice, 199 ms of a
                                ; 2,090 ms read, and a third of the cost of
                                ; opening a small file (SPEC.md 18.4.3).
                                ; Copying here rather than caching the buffer
                                ; is what makes it safe: there is no window in
                                ; which dsk_secbuf could be reused, so there
                                ; is no invalidation anybody can forget.
.free:
    cmp word [dskw_fsec], 0xFFFF
    jne .next                   ; an earlier slot already won
    call dskw_mark_free
    jmp short .next
.endmark:
    cmp word [dskw_fsec], 0xFFFF
    jne .noent
    call dskw_mark_free
    mov byte [dskw_fend], 1     ; this slot IS the end-of-directory marker
    jmp short .noent
.next:
    add si, 32
    loop .entry
    jmp short .sector
.noent:
    mov ax, FERR_NOENT
    stc
    jmp short .out
.ioerr:
    call dskw_ioerr
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_mark_free - record the current scan position as the free slot
; in:  [dskw_cursec], SI -> the entry inside dsk_secbuf
; out: [dskw_fsec] / [dskw_foff]
; clobbers: flags
; -----------------------------------------------------------------------------
dskw_mark_free:
    push ax
    mov ax, [dskw_cursec]
    mov [dskw_fsec], ax
    mov ax, si
    sub ax, dsk_secbuf
    mov [dskw_foff], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_cmp - compare a raw entry's name field against dskw_n83
; in:  ES:SI -> a raw 32-byte directory entry (ES = LOW_SEG)
; out: ZF=1 if the 11 name bytes are identical
; clobbers: flags
; -----------------------------------------------------------------------------
dskw_cmp:
    push cx
    push si
    push di
    mov di, si                  ; the raw name is the ES-side operand
    mov si, dskw_n83
    mov cx, 11
    cld
    repe cmpsb
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dskw_copy11 - copy CX bytes inside the kernel segment (name fields)
; in:  SI = source, DI = destination, CX = count
; out: nothing; preserves all registers
;
; A plain byte loop, not movsb: ES belongs to the caller's buffer here and
; must not be reloaded just to move eleven bytes.
; -----------------------------------------------------------------------------
dskw_copy11:
    push ax
    push cx
    push si
    push di
.loop:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .loop
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_ent_load - stage the located directory entry into the kernel segment
; in:  [dskw_dsec] = its sector LBA, [dskw_doff] = its offset
; out: CF=0 and dskw_raw = the 32 raw bytes; CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_ent_load:
    push bx
    push cx
    push si
    push di
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dskw_dsec]
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_read
    jc .ioerr
    mov si, dsk_secbuf
    add si, [dskw_doff]
    mov di, dskw_raw
    mov cx, 32
    call KERNEL_SEG:cw_dsk_copy_in ; LOW_SEG:SI -> KERNEL_SEG:DI
    clc
    jmp short .out
.ioerr:
    call dskw_ioerr
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_ent_store - write dskw_raw back into its directory sector
; in:  dskw_raw, [dskw_dsec], [dskw_doff], [dskw_zapnext]
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
;
; Read-modify-write of the whole sector, and the read is done HERE rather
; than trusted from an earlier one: dsk_secbuf is shared scratch, and
; writing back a stale copy would resurrect neighbouring entries from some
; other sector. [dskw_zapnext] keeps the end-of-directory marker alive when
; the entry being written took its place.
; -----------------------------------------------------------------------------
dskw_ent_store:
    push bx
    push cx
    push si
    push di
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dskw_dsec]
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_read
    jc .ioerr
    mov si, dskw_raw            ; DS = kernel, ES = LOW_SEG: a plain movsw
    mov di, dsk_secbuf
    add di, [dskw_doff]
    mov cx, 16
    cld
    rep movsw
    cmp byte [dskw_zapnext], 0
    je .write
    mov ax, [dskw_doff]
    add ax, 32
    cmp ax, 512
    jae .write                  ; the next slot is the next sector's, which
    add ax, dsk_secbuf          ; has never been written and is already 00
    mov di, ax
    mov byte [es:di], 0
.write:
    mov ax, [dskw_dsec]
    mov cx, 1
    mov bx, dsk_secbuf
    call KERNEL_SEG:cw_disk_write
    jc .ioerr
    clc
    jmp short .out
.ioerr:
    call dskw_ioerr
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_alloc - claim one free cluster (SPEC.md 18.4)
; in:  nothing ([dsk_rover] is the scan hint)
; out: CF=0, AX = the cluster, ALREADY marked end-of-chain in the snapshot;
;      CF=1 and AX = 0 = no free cluster; CF=1 and AX = 0xFFFF = the mark could
;      not be written, which is an I/O failure and not a full disk
; clobbers: AX (the output), CF
;
; Marking it on the spot is what makes a multi-cluster chain safe to build:
; the next call cannot hand the same cluster out again, whatever happens in
; between. It also means allocation can fail for a REASON OTHER THAN SPACE,
; and the two must not be reported as one: a disk that answered an I/O error
; while there were thousands of free clusters on it used to say 'Disk full',
; which sends the user to delete files that were never the problem.
; -----------------------------------------------------------------------------
dskw_alloc:
    push bx
    push cx
    push dx
    mov cx, [dsk_maxclus]
    dec cx                      ; candidates: clusters 2..maxclus
    mov ax, [dsk_rover]
.try:
    cmp ax, 2                   ; wrap the rover into range
    jae .inrange
    mov ax, 2
.inrange:
    cmp ax, [dsk_maxclus]
    jbe .check
    mov ax, 2
.check:
    push ax
    call KERNEL_SEG:cw_dsk_next_clus
    jc .skip
    test ax, ax
    jz .got                     ; 0 = free
.skip:
    pop ax
    inc ax
    loop .try
    jmp short .nospace
.got:
    pop ax                      ; AX = the free cluster
    mov dx, DSKW_EOC16
    cmp byte [dsk_fattype], 0
    jne .mark
    mov dx, DSKW_EOC12
.mark:
    call dskw_setfat            ; the EOC mark. A cluster we could not MARK is
    jc .iofail                  ; not a cluster we have taken - reporting it as
                                ; allocated hands the same one out again on the
                                ; next call, which is the cross-link the commit
                                ; order exists to make impossible
    mov bx, ax
    inc bx
    mov [dsk_rover], bx         ; next search starts past it
    clc
    jmp short .out
.iofail:
    mov ax, 0xFFFF              ; ...and say WHICH failure it was
    stc
    jmp short .out
.nospace:
    xor ax, ax
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_free_chain - zero every FAT entry of a chain
; in:  AX = first cluster
; out: nothing (all registers preserved)
;
; Bounded by dsk_maxclus iterations: a looped or cross-linked chain off a
; foreign machine's disk terminates the walk instead of hanging it with
; [sch_lock] raised.
; -----------------------------------------------------------------------------
dskw_free_chain:
    push ax
    push cx
    push dx
    push si
    mov cx, [dsk_maxclus]
    mov si, ax
.loop:
    mov ax, si
    cmp ax, 2
    jb .done
    cmp ax, [dsk_maxclus]
    ja .done
    call KERNEL_SEG:cw_dsk_next_clus ; AX = the link, before we erase it
    jc .done
    mov dx, ax
    mov ax, si
    push dx
    xor dx, dx
    call dskw_setfat            ; free this one
    pop dx
    mov si, dx
    loop .loop
.done:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_setfat - write one FAT entry into the snapshot (SPEC.md 18.4)
; in:  AX = cluster, DX = value
; out: nothing (all registers preserved)
;
; The mirror of dsk_next_clus and the single WRITER of the FAT buffer: ES only,
; never DS. FAT12 is a read-modify-write of the straddling word - the
; entry shares a byte with its neighbour, and that neighbour's nibble must
; survive. The dirty range grows to cover the touched sector and, for a
; straddle, the next one.
;
; **IT GOES THROUGH dsk_fat_ofs, exactly as dsk_next_clus does** (SPEC.md 18.8),
; and that is the whole of the conversion the FAT window needed on this side.
; The offset arithmetic used to be open-coded here and ABSOLUTE - clus*2 for
; FAT16 - which was right while the FAT was a whole snapshot and is wrong for a
; nine-sector window: on a 32MB FAT16 partition FATSz16 is about 64, so an entry
; past cluster 2,048 is written outside the buffer, into whatever LOW_SEG holds
; there (task stacks, disk_dir, disk_icons), while the real FAT never gets it -
; so the allocator hands the same cluster out twice AND the flush writes those
; foreign bytes to both FAT copies. A fresh volume hides it, because below
; cluster 2,048 absolute and window-relative are the same number.
;
; dsk_fat_ofs faults the sector in (and, for a FAT12 straddle, its successor),
; answers BX window-relative and DX as the ABSOLUTE FAT sector, and CF=1 when
; the window could not be loaded - which is an I/O failure and must not be
; silently dropped, or the entry is lost while the caller thinks it was stored.
; -----------------------------------------------------------------------------
dskw_setfat:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    cmp ax, 2
    jb .ok
    cmp ax, [dsk_maxclus]
    ja .ok
    mov si, dx                  ; SI = the value: DX is dsk_fat_ofs's second
                                ; output and cannot carry it
    call KERNEL_SEG:cw_dsk_fat_ofs  ; BX = window-relative, DX = abs sector
    jc .out                     ; the window would not load: CF straight out
    mov cx, [dsk_fatseg]
    mov es, cx
    cmp byte [dsk_fattype], 0
    jne .fat16
    mov ch, al
    and ch, 1                   ; CH = the cluster's parity
    mov ax, [es:bx]             ; the straddling word
    and si, 0x0FFF
    test ch, ch
    jz .even
    and ax, 0x000F              ; odd cluster: keep the low nibble
    mov cl, 4
    shl si, cl
    or ax, si
    jmp short .store
.even:
    and ax, 0xF000              ; even cluster: keep the high nibble
    or ax, si
.store:
    mov [es:bx], ax
    jmp short .dirty
.fat16:
    mov [es:bx], si
.dirty:
                                ; The dirty range is in ABSOLUTE FAT sectors,
                                ; because that is what the flush turns into an
                                ; LBA. BX is window-relative and the window
                                ; starts on a sector boundary, so bx>>9 is the
                                ; offset INTO the window and [dsk_fatw0] is
                                ; what makes it absolute again
    mov ax, bx                  ; the sector holding the entry...
    mov cl, 9
    shr ax, cl
    add ax, [dsk_fatw0]
    call dskw_dirty
    mov ax, bx                  ; ...and the one holding its last byte
    inc ax
    mov cl, 9
    shr ax, cl
    add ax, [dsk_fatw0]
    call dskw_dirty
.ok:
    clc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_dirty - grow the dirty FAT sector range to include AX
; in:  AX = sector index within one FAT
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dskw_dirty:
    cmp ax, [dsk_fatd0]
    jae .hi
    mov [dsk_fatd0], ax
.hi:
    cmp ax, [dsk_fatd1]
    jbe .out
    mov [dsk_fatd1], ax
.out:
    ret

; -----------------------------------------------------------------------------
; dskw_flush - write the dirty FAT sectors back to FAT1 (and FAT2)
; in:  the dirty range [dsk_fatd0, dsk_fatd1]
; out: CF=0 (clean, or written and now clean); CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
;
; Both copies are written from the same buffer bytes, so FAT1 == FAT2
; after every operation - which is exactly what a host fsck checks first.
; Writing the range instead of the whole FAT keeps a save at a couple of
; sector writes instead of eighteen.
;
; **THE DIRTY RANGE IS ABSOLUTE AND THE BUFFER OFFSET IS WINDOW-RELATIVE**
; (SPEC.md 18.8), and the subtraction between them is the second half of the
; FAT-window conversion. Absolute is what an LBA needs; window-relative is what
; the buffer needs; using the absolute index for both read four kilobytes past
; the window on any volume whose FAT is longer than nine sectors and wrote them
; to BOTH copies - destroying the redundancy dsk_fat_window falls back on. With
; the range bounded to the window the shift maxes at 4,096 and BX cannot wrap,
; which the absolute form did outright past FAT sector 128.
; -----------------------------------------------------------------------------
dskw_flush_x:
    push bx
    push cx
    push dx
    push es
    mov ax, [dsk_fatd0]
    cmp ax, 0xFFFF
    je .clean
    mov bx, [dsk_fatw0]         ; nothing resident: there is no buffer the
    cmp bx, 0xFFFF              ; dirty range could be read out of, and
    je .stale                   ; writing one anyway is what would corrupt
    cmp ax, bx
    jb .stale                   ; the range starts below the window
    mov cx, [dsk_fatd1]
    mov dx, bx
    add dx, [dsk_fatwn]
    cmp cx, dx
    jae .stale                  ; ...or ends above it
    sub cx, ax
    inc cx                      ; CX = sectors to write
    mov dx, cx
    mov bx, [dsk_fatseg]
    mov es, bx
    mov bx, ax
    sub bx, [dsk_fatw0]         ; the range's offset IN THE WINDOW
    mov cl, 9
    shl bx, cl
    mov cx, dx
    push ax
    add ax, [dsk_fatlba]        ; FAT1
    call KERNEL_SEG:cw_disk_write ; preserves BX: reusable for FAT2 below
    pop ax
    jc .ioerr
    cmp byte [dsk_nfats], 2
    jne .done
    push ax
    add ax, [dsk_fatlba]
    add ax, [dsk_fatsz]         ; FAT2 = FAT1 + FATSz16
    call KERNEL_SEG:cw_disk_write
    pop ax
    jc .ioerr
.done:
    mov word [dsk_fatd0], 0xFFFF
    mov word [dsk_fatd1], 0
.clean:
    xor ax, ax
    clc
    jmp short .out
.stale:
                                ; The dirty range and the window have parted -
                                ; which nothing in the tree can currently do,
                                ; because dsk_fat_window flushes before it
                                ; evicts. It is a refusal rather than an
                                ; assertion because the alternative is writing
                                ; bytes we cannot vouch for to both FAT copies,
                                ; and the caller's rollback (dskw_refat) drops
                                ; the window, which is exactly the right answer
    mov word [dsk_fatd0], 0xFFFF
    mov word [dsk_fatd1], 0
    mov ax, FERR_IO
    stc
    jmp short .out
.ioerr:
    call dskw_ioerr
    stc
.out:
    pop es
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_refat - roll the FAT snapshot back to what the disk says
; in:  nothing
; out: nothing (all registers preserved)
;
; The rollback for any failure before the commit: a half-built chain must
; never survive in RAM, or a later unrelated write would flush it.
;
; **It is an INVALIDATE now, and does no I/O at all** (SPEC.md 18.8): the FAT
; is a window, so dropping it is one word, and the next access faults a clean
; window in off the disk - which is precisely what re-reading the whole FAT
; used to buy. It can no longer fail, so the write gate no longer has a reason
; to close here.
;
; What the window DOES change is the reach of the rollback: an eviction may
; already have flushed part of a half-built chain to the disk, and no
; invalidate can take that back. It costs LOST CLUSTERS and never a
; cross-link - the same guarantee the commit order gives a crash (18.4) - and
; that is the whole of the difference.
; -----------------------------------------------------------------------------
dskw_refat:
    mov word [dsk_fatw0], 0xFFFF
    mov word [dsk_fatd0], 0xFFFF
    mov word [dsk_fatd1], 0
    ret

; -----------------------------------------------------------------------------
; dskw_ioerr - map the last int 13h status to an error code
; in:  [dsk_ioerr] (set by the failing dsk_xfer)
; out: AX = FERR_WPROT if the media is write-protected, else FERR_IO
; clobbers: AX (the output)
; -----------------------------------------------------------------------------
dskw_ioerr:
    mov ax, FERR_IO
    cmp byte [dsk_ioerr], 0x03
    jne .out
    mov ax, FERR_WPROT
.out:
    ret

; -----------------------------------------------------------------------------
; dskw_stage - stage a partial final sector, zero-filled, into dsk_secbuf
; in:  ES:SI = the caller's remaining bytes, CX = their count (1..512)
; out: nothing (all registers preserved)
;
; DS becomes the caller's buffer segment for the move, so NO kernel
; variable is readable in the body - the dsk_copy_in discipline (SPEC.md
; 18). The zero fill is what keeps whatever sits past the caller's buffer
; off the disk.
; -----------------------------------------------------------------------------
dskw_stage:
    push ax
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov dx, 512
    sub dx, cx                  ; DX = bytes to zero after the copy
    push es
    pop ds                      ; DS:SI = the caller's bytes
    mov ax, LOW_SEG
    mov es, ax
    mov di, dsk_secbuf
    cld
    rep movsb
    mov cx, dx
    xor al, al
    rep stosb
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_fetch - copy a file's live tail bytes out of dsk_secbuf
; in:  ES:DI = the caller's destination, CX = byte count (1..512)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dskw_fetch:
    push ax
    push cx
    push si
    push di
    push ds
    mov ax, LOW_SEG
    mov ds, ax
    mov si, dsk_secbuf
    cld
    rep movsb                   ; DS:SI -> ES:DI
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_now - the current date and time in FAT form (SPEC.md 18.4/37)
; in:  nothing
; out: [dskw_date] / [dskw_time]; all registers preserved
;      date = (year-1980)<<9 | month<<5 | day
;      time = hour<<11 | minute<<5 | second>>1   (2-second resolution)
; -----------------------------------------------------------------------------
dskw_now:
    push ax
    push bx
    push cx
    call KERNEL_SEG:cw_clk_snapshot ; read the fields the SPEC.md 37 way
    xor ah, ah
    mov al, [clk_sn_hour]
    mov cl, 11
    shl ax, cl
    mov bl, [clk_sn_min]
    xor bh, bh
    mov cl, 5
    shl bx, cl
    or ax, bx
    mov bl, [clk_sn_sec]
    xor bh, bh
    shr bx, 1
    or ax, bx
    mov [dskw_time], ax
    mov ax, [clk_sn_year]
    sub ax, 1980
    mov cl, 9
    shl ax, cl
    mov bl, [clk_sn_mon]
    xor bh, bh
    mov cl, 5
    shl bx, cl
    or ax, bx
    mov bl, [clk_sn_day]
    xor bh, bh
    or ax, bx
    mov [dskw_date], ax
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_name83 - display name -> the raw 11-byte FAT name field
; in:  SI -> NUL-terminated name, DI -> an 11-byte destination
; out: CF=0 and the field filled, space-padded and upper-cased; CF=1 if the
;      name is not a legal 8.3 short name
; clobbers: CF (SI/DI and the rest are preserved)
;
; The exact inverse of SPEC.md 19's synthesis: 1..8 stem characters, an
; optional '.' plus 1..3 extension characters, nothing after that. The
; legal set is the FAT short-name set, so a space, a path separator, a
; wildcard or a byte outside 21h..7Eh is a rejection - and 0xE5 (the
; deleted mark) and "." / ".." cannot be spelled at all.
; -----------------------------------------------------------------------------
dskw_name83:
    push ax
    push bx
    push cx
    push si
    push di
    cld
    mov cx, 11                  ; the field starts as eleven spaces
    mov bx, di
    mov al, ' '
.pad:
    mov [bx], al
    inc bx
    loop .pad
    xor cx, cx                  ; CX = characters taken so far
.stem:
    lodsb
    test al, al
    jz .stem_end
    cmp al, '.'
    je .dot
    call dskw_char_x            ; upcase + legality
    jc .bad
    cmp cx, 8
    jae .bad                    ; a ninth stem character
    mov bx, di
    add bx, cx
    mov [bx], al
    inc cx
    jmp short .stem
.stem_end:
    jcxz .bad                   ; an empty name
    jmp short .ok
.dot:
    jcxz .bad                   ; ".TXT" has no stem
    xor cx, cx
.ext:
    lodsb
    test al, al
    jz .ext_end
    call dskw_char_x
    jc .bad                     ; a second '.' lands here too
    cmp cx, 3
    jae .bad
    mov bx, di
    add bx, cx
    mov [bx+8], al
    inc cx
    jmp short .ext
.ext_end:
    jcxz .bad                   ; a trailing dot with no extension
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_char - one short-name character: upper-case it, or reject it
; in:  AL = the character
; out: AL upper-cased, CF=0 if legal; CF=1 if not
; clobbers: AL (the output), CF
; -----------------------------------------------------------------------------
dskw_char_x:
    cmp al, 'a'
    jb .nofold
    cmp al, 'z'
    ja .nofold
    sub al, 32
.nofold:
    cmp al, 'A'
    jb .notalpha
    cmp al, 'Z'
    jbe .good
.notalpha:
    cmp al, '0'
    jb .symbol
    cmp al, '9'
    jbe .good
.symbol:
    push si
    mov si, dskw_syms
.scan:
    cmp byte [si], 0
    je .badsym
    cmp al, [si]
    je .goodsym
    inc si
    jmp short .scan
.goodsym:
    pop si
.good:
    clc
    ret
.badsym:
    pop si
    stc
    ret


%ifndef KERN_SMALL
; =============================================================================
; The FAT12 floppy formatter (SPEC.md 18.96)
;
; **kern_big ONLY** (docs/KERN-SPLIT-PLAN.md), and the FIRST thing to go
; through that seam. It costs 1,024 bytes of footprint - two 512-byte rungs,
; one on the image and one on the cold segment - and kern_small stood at 512
; bytes of spare, so the alternative was a second raise of the guard the
; 128KB machine lives under. The requester's call, and this is the shape it
; took.
;
; It needs NO ABI parity work (docs/KERN-SPLIT-PLAN.md's rule 3): there is no
; API slot here, deliberately (see the note at the foot of SPEC.md 18.96), so
; there is nothing for a package to find missing and one .o88 still serves
; both kernels. What kern_small loses is two items on one menu.
;
; A HIGH-LEVEL format: the boot sector, both FATs and the root directory, and
; nothing else. int 13h AH=05h - writing sector ID fields - is deliberately
; absent, and the line is the same one SPEC.md 52.3 draws for the hard disk
; with a sharper edge here: a surface format is the operation that turns a
; disk this OS merely cannot read into one NOTHING can read, and every disk
; this feature exists for is already perfectly readable.
;
; So 12 sectors on a 360KB disk and 33 on a 1.44MB one, all of them metadata,
; and the data area is never touched.
; =============================================================================

; --- the four standard IBM formats (SPEC.md 18.96) ---------------------------
; NOT computed. tools/os88disk.py's GEOMETRY table already pins three of these
; and this is the same arithmetic; a formatter that re-derives a layout DOS
; has had since 1983 can only differ from it.
;
; 1.44MB's FAT is 9 sectors = DSK_FAT_SECS exactly, and SPEC.md 18.2 rule 10
; refuses a floppy whose FAT is larger - so the biggest thing this can write
; is the biggest thing that can be mounted, by construction.
DFMT_SPT     equ 0              ; word: sectors per track (the MEDIUM's)
DFMT_CYL     equ 2              ; word: cylinders (the DRIVE's - see the probe)
DFMT_TOT     equ 4              ; word: BPB_TotSec16 = spt * 2 * cyl
DFMT_SPC     equ 6              ; byte: BPB_SecPerClus
DFMT_MEDIA   equ 7              ; byte: BPB_Media
DFMT_FATSZ   equ 8              ; word: BPB_FATSz16
DFMT_ROOTN   equ 10             ; word: BPB_RootEntCnt
DFMT_KB      equ 12             ; word: the size the confirmation says
DFMT_SIZE    equ 14

DFMT_ROWS    equ 4

section .text                   ; DATA, so back to the kernel segment: cold
                                ; code reads it through DS (SPEC.md 2.6), and
                                ; a `dw` left on the cold side of this toggle
                                ; is read at the wrong segment with NO
                                ; diagnostic - which is exactly what this
                                ; table did on its first run, and it showed
                                ; as a confirmation offering to format a
                                ; 360KB floppy "as 14594K"
dskw_fmt_tab:
    dw 18, 80, 2880
    db 1, 0xF0
    dw 9, 224, 1440             ; 1.44MB
    dw 15, 80, 2400
    db 1, 0xF9
    dw 7, 224, 1200             ; 1.2MB
    dw 9, 80, 1440
    db 2, 0xF9
    dw 3, 112, 720              ; 720KB
    dw 9, 40, 720
    db 2, 0xFD
    dw 2, 112, 360              ; 360KB
section .cold                   ; ...and back to cold code

; -----------------------------------------------------------------------------
; dskw_fmt_row - a table row's address (module-internal)
; in:  AL = row index
; out: SI -> the row
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
dskw_fmt_row:
    push ax
    push dx
    xor ah, ah
    mov dx, DFMT_SIZE
    mul dx
    mov si, dskw_fmt_tab
    add si, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_probe - which of the four formats is this drive holding?
; in:  [disk_drive] = the volume; UI task, gfx lock held (it reads the disk)
; out: CF=0 with AL = a dskw_fmt_tab row; CF=1 with AX = FERR_*
; clobbers: AX (the output), flags
;
; READS ONLY. That is what lets the Disk window call it before the user has
; agreed to anything, so the confirmation can name the size it is about to
; make (SPEC.md 22.12).
;
; **The geometry is two questions with two different answers**, and running
; them together is the trap this routine exists to avoid.
;
; How many sectors a TRACK holds is a property of the MEDIUM, and it is read
; off it: one disk_read of CHS (0,0,N) for N in 18, 15, 9, first success
; wins. That works because disk_read derives CHS from [disk_spt]/[disk_heads]
; and those are ours to set - LBA N-1 with spt = N is exactly CHS (0,0,N).
;
; How many CYLINDERS it has is a property of the DRIVE and is not readable at
; all. A 40-track disk in an 80-track drive answers a read of cylinder 40
; with whatever is on physical track 40 - which is logical track 20's data,
; written at 48tpi and read at 96 - so the obvious probe returns a confident
; WRONG answer and formats a 360KB disk as 720KB. So the cylinder count comes
; from int 13h AH=08h, and the one case that stays undecidable - 9-sector
; media in a 1.2MB drive, where 360KB and 720KB are indistinguishable - is
; resolved DOWNWARD: a 360KB layout on 720KB media wastes half a disk, and a
; 720KB layout on 360KB media is a volume whose second half does not exist.
; AH=08h failing (an XT ROM need not implement it for floppies) lands on the
; same safe default, which is why there is no error path for it.
;
; The cost is up to two doomed reads before the answer, and a doomed
; disk_read is three attempts and two controller resets. It is bounded and it
; is once per format; the alternative is trusting AH=08h about the MEDIUM,
; which is the thing this routine exists to not do.
; -----------------------------------------------------------------------------
dskw_fmt_probe:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- is it a floppy at all? (SPEC.md 18.7: DV_KIND is the TRANSPORT,
    ;     and a BIOS unit below 80h is the MEDIUM - disk_mount's own test)
    mov bl, [disk_drive]
    call KERNEL_SEG:cw_dsk_vol_row
    jc .noflop
    cmp byte [bx+DV_KIND], DVK_BIOS
    jne .noflop
    test byte [bx+DV_UNIT], 0x80
    jnz .noflop
    mov dl, [bx+DV_UNIT]        ; the int 13h drive, for AH=08h below
    inc byte [dsk_rah_busy]     ; the probe's reads NEVER touch §18.95's
                                ; cache, either way: a hit would answer the
                                ; geometry question from RAM - the previous
                                ; disk's RAM, since the probe only runs after
                                ; a failed mount - and a fill would cache
                                ; sectors read under a TRIAL geometry, keyed
                                ; as if the real one had read them

    ; --- the DRIVE's own idea of itself, before anything is read -----------
    ; DI = its cylinders, SI = its sectors per track. int 13h AH=08h is the
    ; same question DOS's own FORMAT asks, and the answer is about the DRIVE.
    ;
    ; **A BIOS that will not answer is a 360KB machine, and that inference is
    ; the load-bearing one here.** AH=08h for FLOPPIES arrived with the AT,
    ; which is also when 1.2MB and 1.44MB arrived - so a ROM old enough to
    ; refuse it is a ROM driving 40-cylinder 5.25" drives, and 9/40 is not a
    ; guess but the only thing such a machine can be. Measured: neither
    ; GLaBIOS nor the 27 Oct 1982 IBM ROM answers it, and the 1982 ROM does
    ; not support any other format either.
    push bp                     ; AH=08h spends BX, CX, DX, DI and ES, and
    push dx                     ; some BIOSes spend BP as well
    mov ah, 0x08
    int 0x13
    pop dx
    jc .nogeom
    cmp ch, 0xFF                ; CH = 0FFh is not a geometry. (Tested on CH
    je .nogeom                  ; itself: the old widen-and-`inc ax` form made
                                ; 0x0100 of it, which is not zero, so the
                                ; guard never fired)
    mov al, cl
    xor ah, ah                  ; AH first: the `jz` must see `and`'s flags,
    and al, 0x3F                ; not the xor's, which are always zero
    jz .nogeom                  ; a zero would make the descent skip every row
    mov si, ax                  ; SI = the drive's sectors per track
    mov al, ch                  ; CH = max cylinder low 8 (a floppy is < 256)
    xor ah, ah
    inc ax
    mov di, ax                  ; DI = its cylinders
    jmp short .geom
.nogeom:
    mov di, 40                  ; no usable answer is a 360KB machine - and
    mov si, 9                   ; these are SET here, not left standing from
                                ; above: AH=08h returns ES:DI = the diskette
                                ; parameter table even when its numbers are
                                ; unusable, so DI does not survive the call
.geom:
    pop bp

    ; --- the MEDIUM's sectors per track: descend from the drive's ----------
    ; Starting at the drive's own figure rather than at 18 is what keeps a
    ; 360KB machine - the one this project is calibrated against - from
    ; paying two doomed reads it can never need.
    mov ax, LOW_SEG
    mov es, ax
    xor cx, cx                  ; CX = the row walker, and the answer
.row:
    call .try
    jnc .got
    inc cx
    cmp cx, DFMT_ROWS
    jb .row
    call .undo                  ; nothing answered: there is no medium here
    mov ax, FERR_IO             ; that this can read, so there is none it can
    jmp short .fail             ; safely write either
.got:

    ; --- the row: how many CYLINDERS? ---------------------------------------
    ; Rows 0 and 1 are 80-cylinder by construction (there is no 15- or
    ; 18-sector 40-cylinder format), so only the 9-sector pair is in question,
    ; and it is decided by the DRIVE: 80 cylinders only when the drive has 80
    ; AND is not the 5.25" 1.2MB one, whose 9-sector media is undecidable and
    ; is resolved downward to 360KB - a 360KB layout on 720KB media wastes
    ; half a disk, where a 720KB layout on 360KB media is a volume whose
    ; second half does not exist.
    ;
    ; **Reading cylinder 40 to settle it was tried and is WRONG** - the
    ; negative result is worth a paragraph so it is not re-derived. It looks
    ; like the honest test (a 40-cylinder drive cannot seek there, so the read
    ; fails) and it fails two ways at once. The lesser one is the 360KB-in-a-
    ; 1.2MB-drive case, where physical track 40 is inside the written area and
    ; answers with logical track 20's data - a confident wrong answer, which
    ; is what the veto above is for. The greater one is that **it rests on a
    ; read FAILING**, and that is the least portable thing to depend on across
    ; a BIOS, a controller and an emulator: measured, MartyPC's FDC happily
    ; reads sector 18 of a 9-sector track, so a probe built this way reported
    ; a 360KB disk in a 360KB drive as 1.44MB. Every test here is a read
    ; SUCCEEDING for exactly that reason.
    mov al, cl
    cmp cl, 2                   ; row 0 (1.44MB) or row 1 (1.2MB): done
    jb .got2
    mov al, 3                   ; the 9-sector pair: 360KB unless...
    cmp di, 80                  ; ...the drive really has 80 cylinders...
    jb .got2
    cmp si, 15                  ; ...and is not the 5.25" 1.2MB drive
    je .got2
    mov al, 2                   ; 720KB
.got2:
    push ax
    call .undo                  ; the probe leaves NO geometry behind
    pop ax
.done:
    dec byte [dsk_rah_busy]
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

.noflop:
    mov ax, FERR_PROT           ; a hard-disk partition is HDDTOOL's (52.2);
    jmp short .fail_nb          ; refused BEFORE the cache bypass went up
.fail:
    dec byte [dsk_rah_busy]
.fail_nb:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

    ; --- one candidate: can CHS (0,0,spt) be read? -------------------------
    ; A local, so it may read the walker in CX and the DRIVE's spt in SI -
    ; and SI is banked into DX first, because dskw_fmt_row spends SI on the
    ; table row and the outer loop needs the drive's figure for every
    ; candidate after this one.
    ;
    ; A candidate ABOVE what the drive can do is refused WITHOUT a read:
    ; asking a 360KB drive for 18 sectors is a guaranteed failure that costs
    ; three attempts and two controller resets to establish, and on the
    ; machine this project is calibrated against that is both of the doomed
    ; reads gone.
.try:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, si                  ; DX = the drive's spt, before SI becomes the
    mov al, cl                  ; row
    call dskw_fmt_row
    mov ax, [si+DFMT_SPT]
    cmp ax, dx
    ja .try_no
    mov [disk_spt], ax
    mov word [disk_heads], 2
    mov dx, [si+DFMT_TOT]       ; §18.95's read-ahead clamps its fill against
    mov [dsk_totsec], dx        ; the volume's extent, and a failed mount
                                ; leaves whatever the last good one set. It
                                ; only ever shortens a fill, so a stale value
                                ; cannot read wrong bytes - but it can make
                                ; the probe's own surplus meaningless, and
                                ; the mount that follows overwrites this from
                                ; the BPB anyway
    dec ax                      ; LBA spt-1 = CHS (0,0,spt) under this spt
    mov bx, dsk_secbuf
    mov cx, 1
    call KERNEL_SEG:cw_disk_read
    jmp short .try_out
.try_no:
    stc
.try_out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    ; --- put the fallback geometry back ------------------------------------
    ; The probe writes [disk_spt]/[disk_heads] to aim its reads, and it must
    ; not leave them written: its caller may ARM a confirmation and then be
    ; cancelled, and until the next mount those words are what any raw
    ; disk_read/disk_write on this volume would use. Nothing in the tree does
    ; one today - every path back here goes through a mount, which derives
    ; both from the BPB - so this closes a trap rather than fixing a bug, and
    ; it is the cheap end of that trade.
    ;
    ; 9/2 and not the values on the way in, because that is exactly what
    ; disk_mount's own failure path leaves (SPEC.md 18.3) and this routine is
    ; only ever called with a failed mount behind it. dskw_format sets the
    ; row's geometry itself.
.undo:
    mov word [disk_spt], 9
    mov word [disk_heads], 2
    ret

; -----------------------------------------------------------------------------
; dskw_format - lay a fresh FAT12 volume on the floppy in [disk_drive]
; in:  AL = a dskw_fmt_tab row (dskw_fmt_probe's answer), [disk_drive] = the
;      volume. UI task, gfx lock held - dskw_*'s context exactly.
; out: CF=0; CF=1 with AX = FERR_IO / FERR_WPROT
; clobbers: AX (the output), flags
;
; **The boot sector goes FIRST and the root directory LAST**, which is the
; opposite of SPEC.md 52.3's order and is right for the same reason that one
; is. There the tool lays a volume into a partition table that already
; describes it, so the BPB must be the commit; here the volume being replaced
; is one nothing could mount anyway, and what an interruption must not leave
; is a valid BPB over a FAT that was never written. Writing it first means an
; interrupted format leaves a disk that still fails to mount - and so still
; offers Format, which is the state the user was already in.
;
; No volume label entry is written: it is legal to have none, disk_mount
; filters labels out of the listing anyway, and the alternative is a name the
; user was never asked for.
;
; The caller owes the remount. This routine leaves [disk_spt]/[disk_heads]
; set to the format's, which is what its own writes needed - and the mount
; that follows re-reads both off the BPB just written, through the one
; hostile-BPB pipeline (SPEC.md 18.2), rather than trusting the arithmetic
; here.
; -----------------------------------------------------------------------------
dskw_format:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call dskw_fmt_row           ; SI = the row, live for the whole routine
    mov ax, [si+DFMT_SPT]
    mov [disk_spt], ax
    mov word [disk_heads], 2
    mov ax, [si+DFMT_TOT]       ; ...and the extent §18.95's fill clamps to,
    mov [dsk_totsec], ax        ; for dskw_fmt_probe's reason

    ; --- 1. the boot sector, at LBA 0 --------------------------------------
    call dskw_fmt_boot
    xor ax, ax
    call dskw_fmt_put
    jc .ioerr

    ; --- 2. FAT1 and FAT2 ---------------------------------------------------
    ; Sector 0 of a FAT is the only one with content in it - the media byte
    ; and an EOC mark - so it is built once and written to the head of each
    ; copy, and everything after it is the zeroed scratch.
    mov di, 1                   ; DI = the LBA walker, past the reserved
    mov cx, 2                   ; sector. CX = FAT copies left
.fat:
    push cx
    call dskw_fmt_zero
    push es                     ; dsk_secbuf is .lowbss - LOW_SEG, reached
    mov ax, LOW_SEG             ; through ES and NEVER through DS (SPEC.md
    mov es, ax                  ; 2.1). Without the override these three
    mov al, [si+DFMT_MEDIA]     ; stores land in KERNEL_SEG at the same
    mov [es:dsk_secbuf+0], al   ; offset, which is kernel code: the FAT went
    mov byte [es:dsk_secbuf+1], 0xFF    ; out all zeros and three bytes of the
    mov byte [es:dsk_secbuf+2], 0xFF    ; kernel were overwritten, neither of
    pop es                      ; which announced itself
                                ; entry 0 is the media byte then all ones,
                                ; entry 1 the EOC mark - what a host fsck
                                ; reads back to identify the volume
    mov ax, di
    call dskw_fmt_put
    pop cx
    jc .ioerr
    inc di
    push cx
    call dskw_fmt_zero          ; ...and the rest of this copy, all zeros
    mov cx, [si+DFMT_FATSZ]
    dec cx                      ; sector 0 is done
    call dskw_fmt_run
    pop cx
    jc .ioerr
    loop .fat

    ; --- 3. the root directory ----------------------------------------------
    ; Zeroed whole: 0x00 in a name byte is "this entry and every one after it
    ; is free", which is what disk_mount's walk stops on.
    call dskw_fmt_zero
    mov cx, [si+DFMT_ROOTN]
    mov ax, cx
    mov cl, 4
    shr ax, cl                  ; RootDirSectors = RootEntCnt/16, and every
    mov cx, ax                  ; row here is a multiple of 16 by construction
    call dskw_fmt_run
    jc .ioerr

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.ioerr:
    call dskw_ioerr             ; AX = FERR_IO / FERR_WPROT
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_run - write CX copies of the staged sector from LBA DI up
; in:  CX = sectors (0 is legal and writes nothing), DI = first LBA,
;      dsk_secbuf staged
; out: CF=0 and DI past them; CF=1 = I/O. Clobbers DI, CX, flags
;
; One sector per call, because disk_write advances ES:BX per sector and one
; 512-byte buffer cannot cover a run of identical ones. A 1.44MB format is 33
; of these and a 360KB one is 12, which is what a format costs.
; -----------------------------------------------------------------------------
dskw_fmt_run:
    jcxz .out
.next:
    push cx
    mov ax, di
    call dskw_fmt_put
    pop cx
    jc .out
    inc di
    loop .next
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_put - write the staged sector to LBA AX
; in:  AX = LBA, dsk_secbuf staged
; out: CF as disk_write; every register but the flags preserved
;
; dsk_secbuf is the kernel's sector scratch and this is a sector - the same
; buffer disk_mount reads the BPB into, shared on the same terms as every
; other write here (UI task only, SPEC.md 18.4). Nothing can rewrite it
; under the format: dsk_xfer never touches it, and SPEC.md 18.95's cache
; refills into its own claim ([dsk_rah_seg]) and is only ever filled by a
; READ, of which this half of the routine issues none.
;
; It is 512-byte aligned for SPEC.md 2.1.1's reason, which is the DMA
; controller's rather than this routine's.
; -----------------------------------------------------------------------------
dskw_fmt_put:
    push ax
    push bx
    push cx
    push es
    push ax
    mov ax, LOW_SEG
    mov es, ax
    pop ax
    mov bx, dsk_secbuf
    mov cx, 1
    call KERNEL_SEG:cw_disk_write
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_zero - blank the staging sector
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dskw_fmt_zero:
    push ax
    push cx
    push di
    push es
    mov ax, LOW_SEG
    mov es, ax
    mov di, dsk_secbuf
    xor ax, ax
    mov cx, 256
    cld
    rep stosw
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_boot - build the boot sector for the row in SI
; in:  SI = a dskw_fmt_tab row
; out: dsk_secbuf holds it; all registers preserved
;
; Byte for byte what tools/os88disk.py's boot_sector() writes for a data
; floppy, which is what makes a formatted disk and a built image the same
; kind of object: 'MSDOS5.0' as the OEM name, BS_BootSig 0x29 and a volume id,
; 'FAT12   ' as the type string, and the not-bootable stub in the code area.
;
; Every field it does NOT write is one the zeroed scratch already has right:
; BPB_HiddSec, BPB_TotSec32, BS_DrvNum and BS_Reserved1 are all 0 on a floppy,
; and int 13h is told the drive by dsk_xfer rather than by the sector.
; -----------------------------------------------------------------------------
dskw_fmt_boot:
    push ax
    push bx
    push cx
    push si
    push di
    push es

    call dskw_fmt_zero
    mov ax, LOW_SEG
    mov es, ax

    mov di, dsk_secbuf                      ; the jump AND BS_OEMName: eleven
    mov bx, dskw_fmt_jmp                    ; contiguous bytes on the disk, so
    mov cx, 11                              ; one copy rather than two stores
    call dskw_fmt_cpy                       ; and a copy

    mov word [es:dsk_secbuf+DSK_B_BPS], 512
    mov al, [si+DFMT_SPC]
    mov [es:dsk_secbuf+DSK_B_SPC], al
    mov word [es:dsk_secbuf+DSK_B_RSVD], 1
    mov byte [es:dsk_secbuf+DSK_B_NFATS], 2
    mov ax, [si+DFMT_ROOTN]
    mov [es:dsk_secbuf+DSK_B_ROOTN], ax
    mov ax, [si+DFMT_TOT]
    mov [es:dsk_secbuf+DSK_B_TOT16], ax
    mov al, [si+DFMT_MEDIA]
    mov [es:dsk_secbuf+DSK_B_MEDIA], al
    mov ax, [si+DFMT_FATSZ]
    mov [es:dsk_secbuf+DSK_B_FATSZ], ax
    mov ax, [si+DFMT_SPT]
    mov [es:dsk_secbuf+DSK_B_SPT], ax
    mov word [es:dsk_secbuf+DSK_B_HEADS], 2
    ; BPB_HiddSec and BPB_TotSec32 stay 0, and so do BS_DrvNum and
    ; BS_Reserved1: int 13h is told the drive by dsk_xfer, never by this.

    mov byte [es:dsk_secbuf+38], 0x29       ; BS_BootSig: VolID/Lab/Type follow
    mov word [es:dsk_secbuf+39], 0x0888     ; BS_VolID, os88disk.py's own -
    mov word [es:dsk_secbuf+41], 0x8800     ; a fixed serial, so two disks
                                            ; formatted here are as alike as
                                            ; two images built here (18.8.2's
                                            ; accepted residual)
    mov di, dsk_secbuf+43                   ; BS_VolLab
    mov bx, dskw_fmt_lab
    mov cx, 11
    call dskw_fmt_cpy
    mov di, dsk_secbuf+54                   ; BS_FilSysType
    mov bx, dskw_fmt_typ
    mov cx, 8
    call dskw_fmt_cpy

    mov di, dsk_secbuf+62                   ; the not-bootable stub
    mov bx, dskw_fmt_stub
    mov cx, DFMT_STUB_LEN
    call dskw_fmt_cpy
    mov word [es:dsk_secbuf+510], 0xAA55    ; rule 1's signature, last

    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_cpy - CX bytes from DS:BX to ES:DI (module-internal)
; in:  BX = source in the KERNEL segment, ES:DI = destination, CX = count
; out: nothing (all registers preserved)
;
; `rep movsb` is exactly the right instruction - DS:SI to ES:DI is the pair
; this has - so the source arrives in BX only because SI is the live table
; row at every call site and is cheaper to bank here than at four of them.
; -----------------------------------------------------------------------------
dskw_fmt_cpy:
    push cx
    push si
    push di
    mov si, bx
    cld
    rep movsb
    pop di
    pop si
    pop cx
    ret

section .text                   ; DATA, so back to the kernel segment: cold
                                ; code reads it through DS (SPEC.md 2.6)
dskw_fmt_jmp: db 0xEB, 0x3C, 0x90   ; jmp short 0x3E / nop - and SPEC.md 18.2
                                ; rule 2 tests this first byte for 0EBh/0E9h,
                                ; so a sector without it would not mount the
                                ; volume this has just finished writing
              db 'MSDOS5.0'     ; interop OEM name, os88disk.py's
dskw_fmt_lab: db 'NO NAME    '  ; 11 bytes, and DOS's own word for a volume
                                ; nobody has named - which is what this is,
                                ; since no label ENTRY is written either
dskw_fmt_typ: db 'FAT12   '

; The not-bootable stub, byte for byte os88disk.py's BOOT_STUB: set up DS,
; print the message with int 10h AH=0Eh, wait for a key and int 19h. It is 44
; bytes and it is the difference between a machine that says what is wrong
; and one that executes a root directory.
dskw_fmt_stub:
    db 0x31, 0xC0, 0x8E, 0xD8, 0xBE, 0x59, 0x7C, 0xAC
    db 0x08, 0xC0, 0x74, 0x09, 0xB4, 0x0E, 0xBB, 0x07
    db 0x00, 0xCD, 0x10, 0xEB, 0xF2, 0x30, 0xE4, 0xCD
    db 0x16, 0xCD, 0x19
    db 0x0D, 0x0A, 'Not a bootable disk. Press any key.', 0x0D, 0x0A, 0
DFMT_STUB_LEN equ $ - dskw_fmt_stub
section .cold                   ; ...and back to cold code
%endif                          ; KERN_SMALL: no formatter


; -----------------------------------------------------------------------------
section .bss

; --- the located / staged directory entry ------------------------------------
dskw_raw:     resb 32           ; one raw 32-byte entry, staged in the kernel
                                ; segment (the write path's dsk_ent)
dskw_n83:     resb 11           ; the 11-byte raw name being looked for
dskw_n83b:    resb 11           ; the rename target's, held across the scan
dskw_pad:     resb 2            ; keeps the words below even-aligned
dskw_dsec:    resw 1            ; the found entry's sector LBA...
dskw_doff:    resw 1            ; ...and its offset within that sector
dskw_fsec:    resw 1            ; the first reusable slot's, FFFFh = none
dskw_foff:    resw 1
dskw_cursec:  resw 1            ; dskw_find: the sector being scanned
dskw_fend:    resb 1            ; 1 = that free slot is the 00h terminator
dskw_new:     resb 1            ; 1 = this write is creating the entry
dskw_zapnext: resb 1            ; 1 = re-zero the slot after the one written
dskw_pad2:    resb 1            ; (dskw_isdir is NOT here - see .text above)

; --- the transfer in progress -------------------------------------------------
dskw_seg:     resw 1            ; the caller's buffer segment (ES at entry),
                                ; after dskw_norm has folded the offset in
dskw_src:     resw 1            ; and its offset, 0..15 from that same call -
                                ; HELD for the whole transfer, because the
                                ; segment is what moves (SPEC.md 18.4.1)
dskw_wseg:    resw 1            ; the advancing source/destination segment
                                ; (+32 paragraphs per 512-byte sector)
dskw_len:     resw 1            ; the file's byte count, low word...
dskw_lenhi:   resw 1            ; ...and high (the DX:AX answer on a read)
dskw_cap:     resw 1            ; the destination's capacity, low word...
dskw_caphi:   resw 1            ; ...and high
dskw_rem:     resw 1            ; sectors still to move
dskw_tail:    resw 1            ; bytes in the partial final sector (0 = none)
dskw_rlba:    resw 1            ; the pending data run: its first LBA...
dskw_rlen:    resw 1            ; ...how many sectors are in it (0 = none)...
dskw_rcap:    resw 1            ; ...and the most it may ever hold
dskw_clba:    resw 1            ; the whole-sector walk's cursor inside a take
dskw_fop:     resb 1            ; 0 = read, 1 = write, for the ONE flush body
                                ; the two data walkers share (SPEC.md 18.4.2)
                                ; - dsk_op's idiom, and for dsk_op's reason:
                                ; the run arithmetic must not be able to
                                ; differ between reading and writing
dskw_take:    resw 1            ; sectors this cluster contributes
dskw_lba:     resw 1            ; this cluster's first LBA
dskw_cur:     resw 1            ; the cluster being written / read
dskw_prev:    resw 1            ; the one before it, awaiting its link
dskw_first:   resw 1            ; the new chain's head (0 = empty file)
dskw_rt_home:  resw 1           ; dskw_rmtree: the directory it was called in
dskw_rt_top:   resw 1           ; ...the folder it was asked to remove
dskw_rt_clus:  resw 1           ; ...the entry the scan last named
dskw_rt_guard: resw 1           ; ...iterations left (DSKW_RT_MAX)
dskw_rt_attr:  resb 1           ; ...that entry's attribute byte
dskw_rt_ioerr: resb 1           ; ...the scan ended on a read error
dskw_rt_sec:   resw 1           ; ...the sector the scan is reading
dskw_rt_name:  resb 12          ; ...the caller's name as 11 RAW bytes (the
                                ; 12th keeps the buffer even-sized)
dskw_oldclus: resw 1            ; the replaced chain's head, freed after the
                                ; commit (0 = there was none)
dskw_date:    resw 1            ; dskw_now: the FAT-encoded date...
dskw_time:    resw 1            ; ...and time
; --- dskw_read_at's arguments, banked (SPEC.md 18.4.4) -----------------------
; Module scratch on dsk_op's terms: the disk is the UI task's (SPEC.md 18) and
; a transfer holds sch_lock, so nothing can be inside this twice.
dwr_buf:      resw 1            ; the caller's buffer offset (ES is theirs)
dwr_cap:      resw 1            ; ...and its capacity in bytes
dwr_off:      resd 1            ; the byte offset asked for
dwr_sz:       resd 1            ; the file's size, from its directory entry
dwr_clb:      resw 1            ; bytes per cluster on this volume
dwr_clus:     resw 1            ; the cluster the walk reached
dwr_take:     resw 1            ; bytes this call will deliver

section .text

section .cold                   ; far entries (SPEC.md 2.6): the resident
                                ; thunks in kernel.asm far-call these, and
                                ; these near-call the bodies above - so every
                                ; routine here keeps its ordinary near `ret`
dwf_dskw_delete:        call dskw_delete_x
                    retf
dwf_dskw_dfree:         call dskw_dfree_x
                    retf
dwf_dskw_flush:         call dskw_flush_x
                    retf
dwf_dskw_gone:          call dskw_gone_x
                    retf
dwf_dskw_read:          call dskw_read_x
                    retf
dwf_dskw_remount:       call dskw_remount_x
                    retf
dwf_dskw_rename:        call dskw_rename_x
                    retf
dwf_dskw_stat:          call dskw_stat_x
                    retf
dwf_dskw_sync:          call dskw_sync_x
                    retf
dwf_dskw_write:         call dskw_write_x
                    retf
dwf_dskw_write_sys:     call dskw_write_sys_x
                    retf
dwf_dskw_mkdir:         call dskw_mkdir_x
                    retf
dwf_dskw_read_at:       call dskw_read_at_x
                    retf
dwf_dskw_append:        call dskw_append_x
                    retf

section .text
