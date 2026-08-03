; =============================================================================
; os8088 - diskw.inc
;
; The FAT write path (SPEC.md 18.4). disk.inc reads a data floppy; this
; module is the only one that modifies one, and the only caller of
; disk_write anywhere in the kernel. Five public routines - dskw_write,
; dskw_read, dskw_delete, dskw_rename, dskw_dfree - are the whole surface,
; for the OS and for packages alike (they ARE the API slots 0x0098..0x00A8,
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
FERR_BIG     equ 10            ; file larger than the buffer (or than 64KB)

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
; bytes, and the .fartext blob is copied out from ON TOP of .bss at boot
; (SPEC.md 33), so an uninitialised .bss byte reads as leftover far code:
; reliably NON-ZERO. In .bss this byte made dskw_commit stamp attribute 0x10
; on every file the OS ever created - which every reader then refused as
; FERR_PROT (a directory is protected), so nothing could be read back or
; deleted, and every chain those writes allocated leaked. It is exactly the
; kind of flag that is only ever cleared on a path that has never run.
dskw_isdir: db 0                ; 1 = dskw_commit is stamping a folder

; -----------------------------------------------------------------------------
; dskw_write - create or replace a whole file (SPEC.md 18.4, slot 0x0120)
; in:  SI -> NUL-terminated 8.3 name, ES:BX -> the bytes, CX = byte count
;      (0 = an empty file)
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_write:
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
    call dskw_wbody             ; out: CF + AX (pops do not touch flags)
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_read - read a whole file by name (SPEC.md 18.4, slot 0x0128)
; in:  SI -> name, ES:BX -> destination, CX = destination capacity
; out: CF=0, AX = bytes read (the file's size); CF=1, AX = FERR_* - and on
;      FERR_BIG the destination is untouched
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_read:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [dskw_seg], es
    mov [dskw_src], bx
    mov [dskw_cap], cx
    call dskw_rbody
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_delete - delete a file by name (SPEC.md 18.4, slot 0x0130)
; in:  SI -> name
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_delete:
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
dskw_rename:
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
dskw_dfree:
    push cx
    push si
    push di
    push es
    mov ax, FERR_NODISK
    cmp byte [dsk_mntok], 1
    jne .bad
    xor di, di                  ; DI = free cluster count
    mov si, 2                   ; SI = cluster under test
.scan:
    cmp si, [dsk_maxclus]
    ja .counted
    mov ax, si
    call dsk_next_clus          ; AX = the raw FAT entry
    jc .next                    ; (unreachable: SI is in range)
    test ax, ax
    jnz .next
    inc di                      ; 0 = free
.next:
    inc si
    jmp short .scan
.counted:
    mov ax, di
    mov dl, [dsk_spc]
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
dskw_mkdir:
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
    jc .nospace
    mov [dskw_first], ax
    call dsk_clus2lba           ; AX = its first LBA
    jc .badclus
    mov [dskw_lba], ax

    call dskw_dotents           ; '.' and '..' built in dsk_secbuf
    mov ax, LOW_SEG
    mov es, ax
    mov ax, [dskw_lba]
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_write
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
    call disk_write
    pop cx
    jc .wfail
    inc dx
    loop .zsec
.zeroed:

    call dskw_flush             ; the FAT, THEN the entry (SPEC.md 18.4)
    jc .rollax

    mov word [dskw_len], 0      ; a directory's size field is zero by spec
    mov byte [dskw_new], 1
    mov byte [dskw_isdir], 1
    call dskw_commit            ; the one-sector commit
    mov byte [dskw_isdir], 0
    jc .rollax
    call dskw_sync              ; coherence by remount (SPEC.md 18.4)
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
    call dskw_find              ; in the CURRENT directory (SPEC.md 19.2)
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    call dskw_ent_load
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
    call dskw_flush
    jc .lateio                  ; committed, but the free did not land:
    call dskw_sync              ; lost clusters, and the user should know
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
    call dskw_sync
    pop ax
    stc
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
    call dsk_dirw_start
.sector:
    call dsk_dirw_next          ; AX = the next sector's LBA
    jc .empty                   ; the chain ended with nothing found
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read
    jc .ioerr
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
; in:  SI -> name; [dskw_seg]:[dskw_src] = the bytes, [dskw_len] = count
; out: CF + AX = FERR_*
;
; Steps, in the binding SPEC.md 18.4 order: gate, parse the name, size the
; job, find (or make room for) the directory entry, allocate + write the
; data clusters, flush the FAT, write the entry (THE commit), free the old
; chain, flush again, remount. Every failure before the commit rolls the
; FAT snapshot back off the disk, so a half-built chain never survives in
; RAM to be flushed by some later, unrelated write.
; -----------------------------------------------------------------------------
dskw_wbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov ax, [dskw_src]          ; the caller's buffer must not wrap its
    add ax, [dskw_len]          ; segment - a write that did would take its
    jnc .fits                   ; bytes from offset 0 upward, not from the
    test ax, ax                 ; buffer. Ending exactly at 0000h is the one
    jz .fits                    ; legal carry: the segment's last byte.
.badarg:
    mov ax, FERR_NAME
    stc
    ret
.fits:
    mov di, dskw_n83
    call dskw_name83            ; SI -> the 11-byte raw name field
    jc .badarg

    ; --- size the job: sectors, and the length of a partial final one -----
    mov ax, [dskw_len]
    mov cl, 9
    shr ax, cl                  ; AX = whole sectors
    mov dx, [dskw_len]
    and dx, 511                 ; DX = tail bytes (0 = sector-aligned)
    jz .sized
    inc ax
.sized:
    mov [dskw_rem], ax          ; <= 128: the length is a 16-bit count
    mov [dskw_tail], dx

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
    test byte [dskw_raw+DSK_R_ATTR], DSKW_PROT
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
    call dskw_wdata
    jc .rollback

    ; --- 2. flush the FAT: the new chain is now durable -------------------
    call dskw_flush
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
    call dskw_flush
    jc .lateio                  ; committed, but the free did not land:
.ok:                            ; lost clusters, and the user should know
    call dskw_sync              ; the caches become a mount snapshot again
    xor ax, ax
    clc
    ret
.lateio:
    push ax
    call dskw_sync
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
;      [dskw_seg]:[dskw_src] = the source
; out: CF=0 with [dskw_first] = the chain's head (0 for an empty file), or
;      CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_wdata:
    mov word [dskw_first], 0
    mov word [dskw_prev], 0
    mov ax, [dskw_src]
    mov [dskw_bx], ax
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
    jc .full
    mov [dskw_cur], ax
    mov dx, ax
    cmp word [dskw_first], 0
    jne .link
    mov [dskw_first], ax        ; the chain's head
    jmp short .lba
.link:
    mov ax, [dskw_prev]
    call dskw_setfat            ; prev -> this one
.lba:
    mov ax, [dskw_cur]
    mov [dskw_prev], ax
    call dsk_clus2lba           ; AX = the cluster's first LBA
    jc .ioerr                   ; unreachable: dskw_alloc bounds the cluster
    mov [dskw_lba], ax
    mov al, [dsk_spc]           ; take = min(spc, sectors remaining)
    xor ah, ah
    cmp ax, [dskw_rem]
    jbe .take
    mov ax, [dskw_rem]
.take:
    mov [dskw_take], ax

    ; whole sectors of this take, straight out of the caller's buffer
    mov cx, ax
    call dskw_last_p            ; ZF=1 if this take ends in a partial sector
    jnz .whole
    dec cx                      ; that last sector goes through the stage
.whole:
    jcxz .tail
    mov ax, [dskw_lba]
    mov es, [dskw_seg]
    mov bx, [dskw_bx]
    call disk_write             ; in: AX=LBA, CX=count, ES:BX
    jc .ioerr
    mov ax, cx                  ; advance the source by count*512
    push cx
    mov cl, 9
    shl ax, cl
    pop cx
    add [dskw_bx], ax

    ; the partial final sector: staged zero-filled, so nothing of the
    ; caller's neighbourhood past EOF ever reaches the disk
.tail:
    call dskw_last_p
    jnz .advance
    mov es, [dskw_seg]
    mov si, [dskw_bx]
    mov cx, [dskw_tail]
    call dskw_stage             ; ES:SI -> LOW_SEG:dsk_secbuf, rest zeroed
    mov ax, LOW_SEG
    mov es, ax
    mov bx, dsk_secbuf
    mov cx, 1
    mov ax, [dskw_lba]
    add ax, [dskw_take]
    dec ax                      ; the take's last sector
    call disk_write
    jc .ioerr
.advance:
    mov ax, [dskw_take]
    sub [dskw_rem], ax
    jz .done
    jmp .clus                   ; (near: the loop body outruns a short jump)
.done:
    clc
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
; in:  SI -> name; [dskw_seg]:[dskw_src] = destination, [dskw_cap] = its size
; out: CF=0 with AX = bytes read, or CF=1 with AX = FERR_*
;
; The mirror of the write loop, minus allocation: walk the chain the entry
; names, read whole sectors straight into the caller's buffer, and stage
; the file's partial final sector through dsk_secbuf so the copy out stops
; exactly at EOF. Any link outside [2, dsk_maxclus] while bytes remain is
; corruption, reported as FERR_IO (the read path's own verdict, SPEC.md 18).
; -----------------------------------------------------------------------------
dskw_rbody:
    cmp byte [dsk_mntok], 1
    je .gated
    mov ax, FERR_NODISK
    stc
    ret
.gated:
    mov ax, [dskw_src]
    add ax, [dskw_cap]
    jnc .fits
    test ax, ax                 ; as in dskw_wbody: a buffer may END at the
    jz .fits                    ; segment's last byte, it may not wrap past it
.badarg:
    mov ax, FERR_NAME
    stc
    ret
.fits:
    mov di, dskw_n83
    call dskw_name83
    jc .badarg
    call dskw_find
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    call dskw_ent_load
    jc .out
    test byte [dskw_raw+DSK_R_ATTR], 0x18
    jz .isfile
    mov ax, FERR_PROT           ; a volume label or a subdirectory
    stc
.out:
    ret
.isfile:
    cmp word [dskw_raw+DSK_R_SIZE+2], 0
    jne .toobig                 ; >= 64KB: unrepresentable here
    mov ax, [dskw_raw+DSK_R_SIZE]
    cmp ax, [dskw_cap]
    jbe .fitsbuf
.toobig:                        ; too big for the buffer: nothing is written
    mov ax, FERR_BIG
    stc
    ret
.fitsbuf:
    mov [dskw_len], ax
    test ax, ax
    jnz .sized
    clc                         ; a 0-byte file: no chain to walk, AX = 0
    ret
.sized:
    mov cl, 9
    shr ax, cl
    mov dx, [dskw_len]
    and dx, 511
    jz .nsec
    inc ax
.nsec:
    mov [dskw_rem], ax
    mov [dskw_tail], dx
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_cur], ax
    mov ax, [dskw_src]
    mov [dskw_bx], ax
    call dskw_rdata             ; walk the chain into the caller's buffer
    jc .out
    mov ax, [dskw_len]          ; bytes read = the file's size
    clc
    ret

; -----------------------------------------------------------------------------
; dskw_rdata - read [dskw_rem] sectors along the chain at [dskw_cur]
; in:  [dskw_cur] = first cluster, [dskw_rem]/[dskw_tail] sized by the
;      caller, [dskw_seg]:[dskw_bx] = the destination
; out: CF=0, or CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_rdata:
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
    call dsk_clus2lba
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
    call dskw_last_p
    jnz .whole
    dec cx
.whole:
    jcxz .tail
    mov ax, [dskw_lba]
    mov es, [dskw_seg]
    mov bx, [dskw_bx]
    call disk_read
    jc .ioerr
    mov ax, cx
    push cx
    mov cl, 9
    shl ax, cl
    pop cx
    add [dskw_bx], ax
.tail:
    call dskw_last_p
    jnz .advance
    mov ax, LOW_SEG
    mov es, ax
    mov bx, dsk_secbuf
    mov cx, 1
    mov ax, [dskw_lba]
    add ax, [dskw_take]
    dec ax
    call disk_read              ; the last sector, whole, into the scratch
    jc .ioerr
    mov es, [dskw_seg]
    mov di, [dskw_bx]
    mov cx, [dskw_tail]
    call dskw_fetch             ; ...and only its live bytes to the caller
.advance:
    mov ax, [dskw_take]
    sub [dskw_rem], ax
    jz .done
    mov ax, [dskw_cur]          ; follow the chain
    call dsk_next_clus
    jc .corrupt
    mov [dskw_cur], ax
    jmp .clus                   ; (near: the loop body outruns a short jump)
.done:
    clc
    ret

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
    call dskw_find
    jc .out
    call dskw_ent_load
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
    call dskw_flush
    jc .lateio
.done:
    call dskw_sync
    xor ax, ax
    clc
    ret
.lateio:
    push ax
    call dskw_sync
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
    call dskw_ent_load
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
    call dskw_sync
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
; [dsk_keepcwd] is raised for the same reason dsk_chdir raises it, and
; leaving it out was a real bug (SPEC.md 18.4/19.2): a bare disk_mount
; resets [dsk_cwd] to the root BY DESIGN, so every successful write, delete,
; rename or mkdir performed inside a subdirectory silently teleported the
; volume back to the root. The window kept showing a folder it was no longer
; listing, and the next write resolved its name in the root instead - a
; wrong-directory write, not just a display glitch.
; -----------------------------------------------------------------------------
dskw_sync:
    push ax
    push dx
    mov dl, [disk_drive]
    mov byte [dsk_keepcwd], 1   ; stay where the write happened (18.4)
    call disk_mount
    mov byte [dsk_keepcwd], 0
    pop dx
    pop ax
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
    mov ax, [dskw_len]
    mov [dskw_raw+DSK_R_SIZE], ax
    mov word [dskw_raw+DSK_R_SIZE+2], 0
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
    call dsk_dirw_start         ; (SPEC.md 19.2 - one walker, both shapes)
.sector:
    call dsk_dirw_next          ; AX = next sector LBA, CF=1 = end of dir
    jc .noent
    mov [dskw_cursec], ax
    mov cx, 1
    mov bx, dsk_secbuf
    call disk_read
    jc .ioerr
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
    clc
    jmp short .out
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
    call disk_read
    jc .ioerr
    mov si, dsk_secbuf
    add si, [dskw_doff]
    mov di, dskw_raw
    mov cx, 32
    call dsk_copy_in            ; LOW_SEG:SI -> KERNEL_SEG:DI
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
    call disk_read
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
    call disk_write
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
;      CF=1 = no free cluster
; clobbers: AX (the output), CF
;
; Marking it on the spot is what makes a multi-cluster chain safe to build:
; the next call cannot hand the same cluster out again, whatever happens in
; between.
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
    call dsk_next_clus
    jc .skip
    test ax, ax
    jz .got                     ; 0 = free
.skip:
    pop ax
    inc ax
    loop .try
    stc
    jmp short .out
.got:
    pop ax                      ; AX = the free cluster
    mov dx, DSKW_EOC16
    cmp byte [dsk_fattype], 0
    jne .mark
    mov dx, DSKW_EOC12
.mark:
    call dskw_setfat
    mov bx, ax
    inc bx
    mov [dsk_rover], bx         ; next search starts past it
    clc
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
    call dsk_next_clus          ; AX = the link, before we erase it
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
; The mirror of dsk_next_clus and the single WRITER of FAT_SEG: ES only,
; never DS. FAT12 is a read-modify-write of the straddling word - the
; entry shares a byte with its neighbour, and that neighbour's nibble must
; survive. The dirty range grows to cover the touched sector and, for a
; straddle, the next one.
; -----------------------------------------------------------------------------
dskw_setfat:
    push ax
    push bx
    push cx
    push dx
    push es
    cmp ax, 2
    jb .out
    cmp ax, [dsk_maxclus]
    ja .out
    mov bx, FAT_SEG
    mov es, bx
    cmp byte [dsk_fattype], 0
    jne .fat16
    mov ch, al
    and ch, 1                   ; CH = the cluster's parity
    mov bx, ax                  ; off = clus + clus/2
    shr bx, 1
    add bx, ax
    mov ax, [es:bx]             ; the straddling word
    and dx, 0x0FFF
    test ch, ch
    jz .even
    and ax, 0x000F              ; odd cluster: keep the low nibble
    mov cl, 4
    shl dx, cl
    or ax, dx
    jmp short .store
.even:
    and ax, 0xF000              ; even cluster: keep the high nibble
    or ax, dx
.store:
    mov [es:bx], ax
    jmp short .dirty
.fat16:
    mov bx, ax
    shl bx, 1                   ; off = clus*2
    mov [es:bx], dx
.dirty:
    mov ax, bx                  ; the sector holding the entry...
    mov cl, 9
    shr ax, cl
    call dskw_dirty
    mov ax, bx                  ; ...and the one holding its last byte
    inc ax
    mov cl, 9
    shr ax, cl
    call dskw_dirty
.out:
    pop es
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
; Both copies are written from the same snapshot bytes, so FAT1 == FAT2
; after every operation - which is exactly what a host fsck checks first.
; Writing the range instead of the whole FAT keeps a save at a couple of
; sector writes instead of eighteen.
; -----------------------------------------------------------------------------
dskw_flush:
    push bx
    push cx
    push dx
    push es
    mov ax, [dsk_fatd0]
    cmp ax, 0xFFFF
    je .clean
    mov cx, [dsk_fatd1]
    sub cx, ax
    inc cx                      ; CX = sectors to write
    mov dx, cx
    mov bx, FAT_SEG
    mov es, bx
    mov bx, ax
    mov cl, 9
    shl bx, cl                  ; BX = the range's offset in the snapshot
    mov cx, dx
    push ax
    add ax, [dsk_fatlba]        ; FAT1
    call disk_write             ; preserves BX: reusable for FAT2 below
    pop ax
    jc .ioerr
    cmp byte [dsk_nfats], 2
    jne .done
    push ax
    add ax, [dsk_fatlba]
    add ax, [dsk_fatsz]         ; FAT2 = FAT1 + FATSz16
    call disk_write
    pop ax
    jc .ioerr
.done:
    mov word [dsk_fatd0], 0xFFFF
    mov word [dsk_fatd1], 0
.clean:
    xor ax, ax
    clc
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
; never survive in RAM, or a later unrelated write would flush it. If even
; the re-read fails, the snapshot is untrustworthy and the write gate
; closes until the next mount - the one place this module revokes it.
; -----------------------------------------------------------------------------
dskw_refat:
    push ax
    push bx
    push cx
    push es
    mov ax, FAT_SEG
    mov es, ax
    mov ax, [dsk_fatlba]
    mov cx, [dsk_fatsz]
    xor bx, bx
    call disk_read
    jnc .ok
    mov byte [dsk_mntok], 0     ; unknowable FAT: no more writes until a mount
.ok:
    mov word [dsk_fatd0], 0xFFFF
    mov word [dsk_fatd1], 0
    pop es
    pop cx
    pop bx
    pop ax
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
; in:  ES:SI = the caller's remaining bytes, CX = their count (1..511)
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
; in:  ES:DI = the caller's destination, CX = byte count (1..511)
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
    call clk_snap               ; read the fields the SPEC.md 37 way
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
    call dskw_char              ; upcase + legality
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
    call dskw_char
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
dskw_char:
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
dskw_seg:     resw 1            ; the caller's buffer segment (ES at entry)
dskw_src:     resw 1            ; and its offset
dskw_bx:      resw 1            ; the moving source/destination offset
dskw_len:     resw 1            ; the file's byte count
dskw_cap:     resw 1            ; dskw_read: the destination's capacity
dskw_secs:    resw 1            ; sectors the file occupies
dskw_rem:     resw 1            ; sectors still to move
dskw_tail:    resw 1            ; bytes in the partial final sector (0 = none)
dskw_take:    resw 1            ; sectors this cluster contributes
dskw_lba:     resw 1            ; this cluster's first LBA
dskw_cur:     resw 1            ; the cluster being written / read
dskw_prev:    resw 1            ; the one before it, awaiting its link
dskw_first:   resw 1            ; the new chain's head (0 = empty file)
dskw_oldclus: resw 1            ; the replaced chain's head, freed after the
                                ; commit (0 = there was none)
dskw_date:    resw 1            ; dskw_now: the FAT-encoded date...
dskw_time:    resw 1            ; ...and time

section .text
