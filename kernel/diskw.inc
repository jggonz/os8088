; =============================================================================
; os8088 - diskw.inc
;
; The FAT write path (SPEC.md 18.4). disk.inc reads a data floppy; this
; module is the only one that modifies one, and the only caller of
; disk_write anywhere in the kernel. Five public routines - dskw_write,
; dskw_read, dskw_delete, dskw_rename, dskw_dfree - are the whole surface,
; and read and write are ONE routine each: both take a far pointer and a
; 32-bit count, so there is no separate big-file entry point any more
; (SPEC.md 18.4.1; its slot 0x0190 has since gone to OSAPI_VOL_KIND),
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
%macro DSKW_CLBA_INC 0
  %ifdef OS88_BIGVOL
    call dskw_clba_inc
  %else
    inc word [dskw_clba]
  %endif
%endmacro

%macro DSKW_CLBA_SET 0
  %ifdef OS88_BIGVOL
    call dskw_clba_set
  %else
    mov [dskw_clba], ax
  %endif
%endmacro

; THE 'NO FREE SLOT' SENTINEL: in [dskw_fsec]'s HIGH word where it has one
; (SPEC.md 18.7.5), because FFFFh is an ordinary low word past 32MB
%ifdef OS88_BIGVOL
  %define DSKW_FNONE dskw_fsec+2
%else
  %define DSKW_FNONE dskw_fsec
%endif

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
DSKW_RDO     equ 0x01          ; read-only
DSKW_SPROT   equ DSKW_PROT & ~DSKW_SYSAT
                               ; the replace mask for dskw_write_sys - the
                               ; same refusals WITHOUT hidden and system,
                               ; because those two are what the kernel just
                               ; stamped on its own file and it must be able
                               ; to rewrite it. The volume label and a
                               ; subdirectory still refuse, and so does an
                               ; ordinary READ-ONLY file.
DSKW_KPROT   equ DSKW_SPROT & ~DSKW_RDO
                               ; ...and the mask for replacing a file that is
                               ; ALREADY WEARING the kernel's own species.
                               ;
                               ; SPEC.md 19.6 stamps THREE bits on KERNEL.SYS
                               ; and every *.DRV - read-only, hidden AND
                               ; system - and DSKW_SPROT forgave two of them,
                               ; so the kernel could not rewrite its own file.
                               ; Nothing noticed while every install went onto
                               ; a freshly formatted disk, where the entry
                               ; does not exist and the replace path is never
                               ; taken; the field found it the first time a
                               ; machine was installed to TWICE, as
                               ; `Install ... KERNEL.SYS` on a disk that was
                               ; already a working system (SPEC.md 19.6.2).
                               ;
                               ; It is narrower than "dskw_write_sys forgives
                               ; read-only": the ENTRY has to be hidden+system
                               ; already, so a driver still cannot replace an
                               ; ordinary read-only file that happens to share
                               ; a name. SYSTEM.CFG needs none of this - it is
                               ; deliberately not read-only, for exactly the
                               ; reason this exists (19.6)
DSK_R_CTIME  equ 14            ; create time (word)
DSK_R_CDATE  equ 16            ; create date (word)
DSK_R_ADATE  equ 18            ; last-access date (word)
DSK_R_WTIME  equ 22            ; last-write time (word)
DSK_R_WDATE  equ 24            ; last-write date (word)

DSKW_EOC12   equ 0x0FFF        ; end-of-chain marks: >= the spec's EOC floor
DSKW_EOC16   equ 0xFFFF        ; (0FF8h / 0FFF8h), so a host fsck walks the
                               ; chains exactly the way this kernel does

%ifdef DSK_STREAM                ; WRITE_SEQ's record rides the append body
%define DSKW_WSEQ                ; (SPEC.md 18.4.9) on kern_big and in
%endif                           ; kern_dos, whose DOS box streams (96.53.1)

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
%ifdef DSKW_WSEQ
dws_hd0:    dw 0xFFFF           ; the HELD volume's dirty FAT range, banked
dws_hd1:    dw 0                ; while the machine stands elsewhere (SPEC.md
                                ; 18.8.5); 0xFFFF = none. .text, for this
                                ; block's reason: the empty value is not 0
%endif
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
; dskw_pmask - which attribute bits refuse THIS replace (SPEC.md 19.6.2)
; in:  dskw_raw holds the existing entry's 32 raw bytes; [dskw_syswr] says
;      whose write this is
; out: AL = the mask to test DSK_R_ATTR against; every other register and the
;      flags preserved
;
; Three tiers, and the third is the one the field paid for:
;
;   an ordinary write        DSKW_PROT   - anything protected refuses
;   dskw_write_sys           DSKW_SPROT  - hidden and system forgiven
;   ...over a KERNEL FILE    DSKW_KPROT  - read-only forgiven as well
;
; The last needs the ENTRY to already be hidden+system, not merely the writer
; to be privileged: those three bits together are what SPEC.md 19.6 stamps on
; KERNEL.SYS and every *.DRV, so forgiving them is the kernel rewriting its
; own file and nothing wider. A driver still cannot replace a user's
; read-only document.
;
; It is a routine rather than two copies because it had been two copies -
; dskw_wbody's and dskw_apbody's, identical and eleven hundred lines apart -
; and the append one would have kept the bug through the fix.
; -----------------------------------------------------------------------------
dskw_pmask:
    push bx
    mov al, DSKW_PROT
    cmp byte [dskw_syswr], 0
    je .out
    mov al, DSKW_SPROT
%ifndef INST_RDONLY             ; `make INSTRO=1` keeps read-only refusing,
                                ; which is the A/B for 19.6.2: with it, a
                                ; second install onto the same disk errors
                                ; naming KERNEL.SYS, and a first onto a fresh
                                ; format still works - the two legs the field
                                ; reported
    mov bl, [dskw_raw+DSK_R_ATTR]
    and bl, DSKW_SYSAT
    cmp bl, DSKW_SYSAT          ; the kernel's own species, both bits?
    jne .out
    mov al, DSKW_KPROT
%endif
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_write - create or replace a whole file (SPEC.md 18.4, slot 0x0103)
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
    mov ax, dskw_wbody          ; THE BODY IN AX, and dskw_append_x arrives at
                                ; dskw_wrp with dskw_apbody in it: AX is an
                                ; OUTPUT of both and an input to neither, and
                                ; everything between here and the call is the
                                ; same for the two. `call ax` is 16 clocks
                                ; against `call rel16`'s 19
dskw_wrp:
    push si
    push di
    push bp
    push es
    mov [dskw_seg], es          ; the caller's buffer segment, for the data
    mov [dskw_src], bx          ; loop (which re-loads ES per transfer)
    mov [dskw_len], cx
    mov [dskw_lenhi], dx
    call ax                     ; out: CF + AX (pops do not touch flags)
    call ct_fpg_end             ; the ONE place the widget is given back,
                                ; however many ways the body returned - and it
                                ; preserves CF and AX too (SPEC.md 12.8)
    mov byte [dskw_syswr], 0    ; one shot, cleared here so it can never
.pop:                           ; leak into the next caller's write
    pop es                      ; (dskw_wrap7's pop run as well)
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
; dskw_read - read a whole file by name (SPEC.md 18.4, slot 0x0109)
; in:  SI -> name, ES:BX -> destination, DX:CX = its capacity in bytes
;      (32-bit, DX high)
; out: CF=0, DX:AX = bytes read (the file's 32-bit size); CF=1, AX = FERR_* -
;      and on FERR_BIG, decided from the directory entry's 32-bit size BEFORE
;      any data I/O, the destination is untouched and DX = the KB the read
;      needs, the UNPACKED size where the file is compressed (20.14.6.3)
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
    call ct_fpg_end             ; ...and neither does this (SPEC.md 12.8)
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_delete - delete a file by name (SPEC.md 18.4, slot 0x010F)
; in:  SI -> name
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_delete_x:
    mov ax, dskw_dbody
    jmp short dskw_wrap7

; -----------------------------------------------------------------------------
; dskw_rename - rename a file in place (SPEC.md 18.4, slot 0x0115)
; in:  SI -> old name, DI -> new name
; out: CF=0, AX = 0; CF=1, AX = FERR_*
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_rename_x:
    mov ax, dskw_nbody
                                ; ...and THE LADDER, once. Three public
                                ; wrappers differ only in the body they call;
                                ; each of them takes SI (rename SI and DI) and
                                ; answers in AX, so AX is free to carry the
                                ; address in. dskw_rmtree is 500 lines below
                                ; and reaches this with a near jmp rather than
                                ; being moved up to it
dskw_wrap7:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call ax
    jmp short dskw_wrp.pop      ; the same seven pops, the same order

; -----------------------------------------------------------------------------
; dskw_dfree - free space on the mounted volume (SPEC.md 18.4, slot 0x0118)
; in:  nothing
; out: CF=0, DX:AX = free bytes, BX = SECTORS per cluster (spc*512 overflows
;      16 bits at the SPEC.md 18.2-legal spc = 128); CF=1, AX = FERR_*
; clobbers: AX/BX/DX (the outputs), CF
;
; Answered entirely from the resident FAT snapshot - NO DISK I/O, WHICH IS NOT
; THE SAME AS CHEAP. dsk_free_clus_x counts every entry in that snapshot, so
; this is O(clusters) of CPU: ~105ms a call on a 20MB hard disk on the field's
; 4.77MHz 8088, measured (SPEC.md 18.4.5). The sentence that used to be here -
; "so a package may call it as freely as it likes" - is what apps/ftpd read
; before asking it once per chunk and spending 44% of an upload in it.
; -----------------------------------------------------------------------------
dskw_dfree_x:
    push cx                     ; the shift count below; dsk_free_clus_x
                                ; banks everything but AX itself
    call dsk_free_clus_x ; AX = free clusters - ONE counting body for
    jc .bad                     ; this cell and for the status line's figure
    mov bl, [dsk_spc]           ; (SPEC.md 22.7), so the two cannot disagree.
    xor bh, bh                  ; BX is this routine's own output and `mul dx`
    mov dx, bx                  ; writes DX:AX only, so one load serves both
    mul dx                      ; DX:AX = free sectors (DX = 0: free
                                ; clusters * spc <= TotSec16 <= 5,760)
    mov cx, 9                   ; bytes = sectors * 512, in 32 bits
.shift:
    shl ax, 1
    rcl dx, 1
    loop .shift
    clc
    jmp short .out
.bad:
    mov ax, FERR_NODISK
    stc
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; dskw_vstat - the volume's geometry and its free count, in FOUR REGISTERS
;              (SPEC.md 18.4.6)
; in:  nothing
; out: CF=0 with AX = sectors per cluster, BX = free clusters, CX = bytes
;      per sector, DX = total clusters (CountOfClusters); CF=1 with
;      AX = FERR_NODISK
; clobbers: AX/BX/CX/DX (the outputs), CF
;
; DOS AH=36h's own register shape, and that is not an accident: the DOS box
; is this slot's only caller and it forwards these four to the program
; unchanged. It used to be a twelve-byte RECORD with eight fields and a
; "the size you pass is the request" rule, so that a short buffer skipped
; the FAT walk - and no caller ever passed a short buffer, or read any field
; but these four (SPEC.md 18.4.6.1). The record cost 147 bytes; this is 26.
;
; Bytes per sector is a constant here because the mount makes it one - rule 3
; (disk.inc) refuses any BPB whose BytsPerSec is not 512 - and a redirected
; volume answers with [dsk_spc] = 1 and SECTORS in BX, exactly as
; dskw_dfree_x above does, because dsk_free_clus_x is the one counting body
; for both (SPEC.md 22.7).
; -----------------------------------------------------------------------------
%ifndef KERN_SMALL                 ; kern_small's cell is a stub (18.4.7.4)
dskw_vstat_x:
    call dsk_free_clus_x        ; AX = free clusters; CF=1 = no volume - and
    jc .bad                     ; it banks every other register itself
    mov bx, ax
    mov al, [dsk_spc]
    xor ah, ah                  ; ...which also leaves CF=0 for the `ret`
    mov cx, 512                 ; mount rule 3
    mov dx, [dsk_maxclus]       ; CountOfClusters + 1 is what the mount stored
    dec dx                      ; (SPEC.md 19 rule 15), and the COUNT is what
    ret                         ; the caller means
.bad:
    mov ax, FERR_NODISK
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; dskw_mkdir - create a subdirectory in the current directory (SPEC.md 18.5)
; in:  SI = NUL 8.3 name; UI-task/window-callback context like every dskw_*
; out: CF=0 AX=0, else CF=1 AX = FERR_*
; clobbers: nothing else
; -----------------------------------------------------------------------------
dskw_mkdir_x:
    mov ax, dskw_mkbody         ; dskw_wrap7's argument. It had a ladder of
    jmp dskw_wrap7              ; its own one push shorter (neither of these
                                ; two spends BP); two bytes of stack buy the
                                ; fifteen of the copy

; -----------------------------------------------------------------------------
; dskw_mounted - the write gate (SPEC.md 18.4): is there a volume to write to?
; dskw_gate / dskw_gate83 - ...and the rest of a name-taking body's opening
; in:  dskw_mounted: nothing
;      dskw_gate: SI -> the name, AX = the FSV_* verb a redirected volume is
;      handed (kern_big; ignored where OS88_REDIR is not built)
;      dskw_gate83: SI -> the name
; out: ALL THREE RETURN ONLY TO CONTINUE. dskw_mounted: there is a volume.
;      dskw_gate and dskw_gate83: that, and it is not a redirected one, and
;      dskw_n83 holds the 11-byte raw name (DI = its address). Otherwise they
;      do not come back to the body at all: they drop the body's return
;      address and answer the BODY'S CALLER with CF=1 and AX = FERR_NODISK /
;      FERR_NAME, or with the redirected verb's own CF/AX
; clobbers: dskw_mounted nothing on the way on; the other two AX and DI, and
;      BP on the redirected arm, as the bodies always did
;
; Every one of the eight operation bodies below opens with the gate, and five
; of them went on with the same DVK_FILE hand-off and 8.3 parse, differing
; only in the FSV_* verb: thirty-two bytes a body. `call dskw_gate` is three.
; The drop is two `inc sp`, which leave the carry alone, so the body's caller
; sees exactly what the inline `ret`s gave it. A body may call any of the
; three ONLY while it has pushed nothing since it was entered - the drop is
; one word deep - which is why dskw_stat_x (a push frame) spells its own.
; -----------------------------------------------------------------------------
dskw_gate:
%ifdef DSKW_WSEQ
    call dws_gate               ; DELETE, MKDIR and RMDIR come in HERE and not
%endif                          ; through dskw_mounted (SPEC.md 18.4.9.2)
    cmp byte [dsk_mntok], 1
    jne dskw_mounted.no
%ifdef OS88_REDIR
    cmp byte [dsk_vkind], DVK_FILE
    jne dskw_gate83
    xchg ax, bp                 ; BP = the verb; AX was don't-care (the body
    call dskw_fsop              ; address dskw_wrap6/7 called through)
    jmp short dskw_mounted.quit ; CF/AX = the driver's answer
%endif                          ; OS88_REDIR
dskw_gate83:
    mov di, dskw_n83
    call dskw_name83            ; SI -> the 11-byte raw name field
    jnc dskw_mounted.ret
    mov ax, FERR_NAME
    jmp short dskw_mounted.fail
dskw_mounted:
%ifdef DSKW_WSEQ
    call dws_gate
%endif
    cmp byte [dsk_mntok], 1
    je .ret
.no:
    mov ax, FERR_NODISK
.fail:
    stc
.quit:
    inc sp                      ; the body's return address; CF survives
    inc sp                      ; `inc`
.ret:
    ret

%ifdef DSKW_WSEQ
; dws_gate - A HELD STREAM ENDS BEFORE ANY OTHER WRITE ON ITS VOLUME (SPEC.md
; 18.4.9) - a delete of its temporary, a rename, anything - so the FAT and the
; entry that write finds are the ones the disk will hold. Not WRITE_SEQ's own
; call, and not on another volume: a commit from in here must not move the
; machine. BOTH gates call it: DELETE, MKDIR and RMDIR open with dskw_gate and
; never reach dskw_mounted, and until 18.4.9.2 they skipped it - a delete of
; the held file freed its committed chain and left the hold pending, to link
; a freed cluster at the next commit. clobbers: nothing
dws_gate:
    push ax
    call dws_ours
    jne .no
    call dws_commit
.no:
    pop ax
    ret
%endif

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
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: mkdir's
    mov ax, FSV_MKDIR
%endif                          ; OS88_REDIR
    call dskw_gate              ; returns only with dskw_n83 filled
    call dskw_find              ; CF=0 = the name is already taken
    jnc .exists
    cmp ax, FERR_NOENT
    jne dskw_mounted.ret        ; FERR_IO: the scan itself failed (CF = 1, as
                                ; 2 < 4). The `ret` just above this routine,
                                ; where .fail's is past short reach (-3)
    cmp word [DSKW_FNONE], 0xFFFF
    je .dirfull                 ; scanned to the end with no reusable slot

    call dskw_alloc             ; AX = cluster, already marked end-of-chain
    jnc .haveclus
    inc ax                      ; 0xFFFF -> 0: the mark could not be written,
    jz .wfail                   ; which is an I/O failure and not a full disk
    jmp .nospace                ; - and it takes the ROLLBACK path, because a
                                ; half-marked cluster must not survive in RAM
                                ; to be flushed by a later unrelated write
.haveclus:
    mov [dskw_first], ax
    call dsk_clus2lba_x ; AX = its first LBA
    jc .badclus
    DSKW_CLBA_SET               ; ...in the data walk's own cursor, which
                                ; nothing else is using between here and the
                                ; commit - all 32 bits of it (SPEC.md 18.7.5)

    call dskw_dotents           ; '.' and '..' built in dsk_secbuf
    push ss                     ; ES = LOW_SEG (SPEC.md 2.1), which is what SS names
    pop es
    DSK_WR1D dskw_clba
    jc .wfail

    call dskw_zerobuf           ; the cluster's remaining sectors
    mov cl, [dsk_spc]
    xor ch, ch
    dec cx
    jcxz .zeroed
.zsec:
    DSKW_CLBA_INC
    push cx
    DSK_WR1D dskw_clba
    pop cx
    jc .wfail
    loop .zsec
.zeroed:

    call dskw_flush_x           ; the FAT, THEN the entry (SPEC.md 18.4)
    jc .rollax

    xor ax, ax                  ; a directory's size field is zero by spec -
    mov [dskw_len], ax          ; both words of it, since SPEC.md 18.4.1.
    mov [dskw_lenhi], ax        ; dskw_commit takes its arguments in MEMORY,
    inc ax                      ; so the accumulator is free across it: 15
    mov [dskw_new], al          ; bytes against 22
    mov [dskw_isdir], al
    call dskw_commit            ; the one-sector commit
    mov byte [dskw_isdir], 0
    jc .rollax
    jmp dskw_relold.flush       ; coherence by remount (SPEC.md 18.4); the
                                ; FAT is clean, so the flush is AX = 0, CF = 0

.exists:                        ; (these three answer for dskw_nbody,
    mov ax, FERR_EXIST          ; dskw_wbody and dskw_wdata too: one
    stc                         ; spelling each)
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
    push ss
    pop es
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
    push cx
    push si
    push di
    push es
    call dskw_zerobuf
    call dskw_now               ; [dskw_date] / [dskw_time] (SPEC.md 37)
    push ss
    pop es
    ; '.' IS BUILT ONCE AND '..' IS ITS COPY. The two links differ in two
    ; bytes - the second dot and the cluster - so the slot is written by
    ; string stores in field order, then moved 32 bytes along and patched.
    ; It was a per-slot routine called twice, which also zeroed the size a
    ; zeroed sector already holds.
    mov di, dsk_secbuf
    cld
    mov al, '.'                 ; name[0]...
    stosb
    mov al, ' '                 ; ...name[1..10] = spaces
    mov cx, 10
    rep stosb
    mov al, DSKW_DIR            ; +11 the attribute
    stosb
    scasw                       ; +12 the compression hint, left 0: `scasw`
                                ; only to step DI two (it reads, never writes)
    mov ax, [dskw_time]
    stosw                       ; +14 create time
    mov ax, [dskw_date]
    stosw                       ; +16 create date
    stosw                       ; +18 last-access date
    scasw                       ; +20 FstClusHI, 0 - stepped over likewise
    mov ax, [dskw_time]
    stosw                       ; +22 last-write time
    mov ax, [dskw_date]
    stosw                       ; +24 last-write date; +26 the cluster below,
                                ; +28 the size, which the sector already holds
%if DSK_R_ATTR != 11 || DSK_R_CTIME != 14 || DSK_R_CDATE != 16 || DSK_R_ADATE != 18 || DSK_R_WTIME != 22 || DSK_R_WDATE != 24
  %error "dskw_dotents stores the dot entry's fields IN ORDER from +11: re-derive the stosb/stosw/scasw run"
%endif
    push ds
    push es
    pop ds                      ; DS = ES = LOW_SEG for the 32-byte copy
    mov si, dsk_secbuf
    mov di, dsk_secbuf + 32     ; '..' -> the parent
    mov cl, 16                  ; (CX = 0 out of the `rep` above)
    rep movsw
    pop ds
    mov byte [es:dsk_secbuf+33], '.'   ; the second dot
    mov ax, [dskw_first]        ; '.' -> this directory
    mov [es:dsk_secbuf+DSK_R_CLUS], ax
    mov ax, [dsk_cwd]           ; '..' -> the parent
    mov [es:dsk_secbuf+32+DSK_R_CLUS], ax
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_rmdir - remove an EMPTY subdirectory of the current directory (18.6)
; in:  SI = NUL 8.3 name; UI-task/window-callback context like every dskw_*
; out: CF=0 AX=0, else CF=1 AX = FERR_*
; clobbers: nothing else
; -----------------------------------------------------------------------------
dskw_rmdir:
    mov ax, dskw_rmbody
    jmp dskw_wrap7

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
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: rmdir's
    mov ax, FSV_RMDIR           ; EMPTINESS is the driver's test, exactly as
                                ; it is the FAT path's: only the side holding
                                ; the directory can answer it
%endif                          ; OS88_REDIR
    call dskw_gate              ; '.' and '..' cannot be spelled (18.4), and
                                ; the root has no entry, so neither can be
                                ; the target
dskw_rmbody_n83:                ; the same pipeline with dskw_n83 already
                                ; filled - what dskw_rmtree calls, because it
                                ; works from raw 11-byte names it read off the
                                ; disk rather than from display names
    call dskw_find              ; in the CURRENT directory (SPEC.md 19.2)
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    mov al, [dskw_raw+DSK_R_ATTR]
    test al, DSKW_NPROT
    jnz .prot                   ; read-only, hidden, system or volume label
    test al, DSKW_DIR
    jz .prot                    ; a file: the caller wanted dskw_delete
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_oldclus], ax
    call dskw_clok              ; a subdirectory always owns a real cluster,
    jc .corrupt                 ; so anything else is corruption - and a walk
                                ; we refuse is cheaper than one we bound

    call dskw_isempty           ; CF=1 with AX = FERR_PROT / FERR_IO
    jc .out

.delent:                        ; ...dskw_dbody's commit too, entered with
                                ; [dskw_oldclus] set as this body has set it
    mov byte [dskw_raw], 0xE5   ; THE commit: one directory sector (18.6)
.entrel:                        ; ...and the write-at truncate enters here,
                                ; its entry already changed (18.4.7.5)
    mov byte [dskw_zapnext], 0
    call dskw_ent_store
    jnc dskw_relold             ; only now is the chain nobody's
    ret
.prot:
    jmp dskw_wbody.prot
.corrupt:
    mov ax, FERR_IO
    stc
.out:
    ret

; -----------------------------------------------------------------------------
; dskw_relold - the shared TAIL of the three bodies that replace or remove an
;               entry: the commit has landed, so free the chain it let go of
; in:  [dskw_oldclus] = that chain's head (0 or garbage = nothing to free);
;      entered by `jmp`, at the depth the body was called at
; out: CF=0 AX=0; CF=1 AX = FERR_* when the free did not reach the disk -
;      committed, but lost clusters, and the user should know. The caches
;      become a mount snapshot again either way (dskw_sync_x)
;
; dskw_rmbody, dskw_wbody and dskw_dbody each spelt this out. A garbage old
; link (0 included) leaves the FAT alone, and dskw_free_chain is what says so:
; its first dsk_next_clus_x refuses a cluster outside [2, dsk_maxclus] and the
; walk ends having changed nothing. The dskw_clok that stood here asked it
; first, a second asking of a question with one answer.
; -----------------------------------------------------------------------------
dskw_relold:
    mov ax, [dskw_oldclus]
    call dskw_free_chain
.flush:                         ; ...but FLUSH EITHER WAY: a commit point
    call dskw_flush_x           ; leaves the window CLEAN (dsk_fatw_park
                                ; relies on it - dirt carried across a media
                                ; change lands on the NEXT disk's FAT), and
                                ; a truncate at the file's own allocated end
                                ; has re-marked the last cluster and frees
                                ; nothing (18.4.7.5). Clean is no I/O, and
                                ; its answer is already ours: CF=0 AX=0, or
                                ; CF=1 AX = the FERR_*
    pushf                       ; dskw_sync_x preserves every register but
    call dskw_sync_x            ; not the flags, and the answer is CF
    popf
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
; A recursive descent would need one frame per level on a 384-byte task
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
                                ; forever without it. **OUTSIDE THE BAND**: it
                                ; emits nothing, and `dskw_append`'s own chain
                                ; walk bounds itself with it too

%ifndef KD_BUILD                ; **DOS HAS NO RECURSIVE DELETE** (SPEC.md
                                ; 96.44.8): AH=3Ah removes an EMPTY directory
                                ; and refuses a populated one, which is
                                ; `dskw_rmdir` with AL=0 - the strict arm
                                ; `dos_k_rmdir` already takes. This pipeline is
                                ; the file manager's Trash verb, and its only
                                ; callers outside itself are `files.inc` and
                                ; `filecp.inc`, neither of which is in a
                                ; kerndos root
dskw_rmtree:
    mov ax, dskw_rtbody
    jmp dskw_wrap7

; -----------------------------------------------------------------------------
; dskw_rtbody - the recursive-delete pipeline (module-internal)
; in:  SI = the name; out: CF/AX as dskw_rmtree
; -----------------------------------------------------------------------------
dskw_rtbody:
    call dskw_mounted           ; returns only when there is a volume
    cmp byte [dsk_vkind], DVK_FILE
    jne .fat                    ; A REDIRECTED VOLUME HAS NO DIRECTORY SECTORS
                                ; (SPEC.md 62.9) and everything below this line
                                ; reads them - dskw_find, dskw_rt_scan,
                                ; dsk_dotdot and dskw_rt_byclus all walk the
                                ; disk. Without this the walk went straight
                                ; into dsk_xfer, which REFUSES a DVK_FILE
                                ; volume outright, so Delete on a folder of a
                                ; network or RAM-disk volume did nothing at
                                ; all, silently, and left the folder on screen
                                ; with no verdict. This is the tenth of ten
                                ; write bodies in this file and the only one
                                ; that never grew the branch: SPEC.md
                                ; 62.10.4.5 verified the os8088 side with
                                ; `File > New Folder` alone - the one verb
                                ; with no application behind it - and nothing
                                ; ever drove a Delete from this end.
                                ;
                                ; SO THE TREE IS WALKED BY THE SIDE THAT HOLDS
                                ; IT (SPEC.md 62.10.7). FSV_RMTREE is one
                                ; command frame and one status where a
                                ; kernel-side walk is an FSV_ENUM plus an
                                ; FSV_DELETE per file and an FSV_CHDIR per
                                ; level - FSV_COPY's argument (62.9.8) in the
                                ; other direction, against the same 3,741
                                ; bytes a second.
.fsrt:
    mov bp, FSV_RMTREE          ; SI = the name, which is what we were handed
    call dskw_fsop              ; ...AND NOT drv_fs_call: that is the whole of
                                ; what this shares with the other nine write
                                ; bodies, and the half that is not the call is
                                ; dskw_sync_x - the ONE routine a successful
                                ; file operation passes through, so no
                                ; operation can forget to mark the folder it
                                ; changed (SPEC.md 22.8). Hand-rolled around
                                ; it, the delete WORKED and the Disk window
                                ; went on listing the folder that was gone:
                                ; a stale row is a wrong FILE the moment
                                ; anything resolves an index against it
                                ; (docs/FIELD-NOTES.md 4). BP is spent by
                                ; drv_fs_call and dskw_rmtree banks it
    jnc .fsok
    or ax, ax
    jnz .fsno                   ; THE FAR SIDE TRIED AND SAID NO, and that is
                                ; its FERR_* to report: a whole volume, a
                                ; protected file, a read-only run
                                ;
                                ; AX = 0 IS THE OTHER THING ENTIRELY - it is
                                ; what drv_fs_call answers for a verb the
                                ; driver does not publish, and it is the same
                                ; word an absent FSV_ENUM gives (62.9.7). So
                                ; a driver that declines this falls back to
                                ; the empty-only FSV_RMDIR, which is exactly
                                ; what a redirected volume did before the verb
                                ; existed: emptiness is the driver's test, a
                                ; folder with anything in it comes back
                                ; FERR_PROT, and the file manager says so.
                                ; RAMDISK.DRV declines on purpose, which is
                                ; what keeps this branch tested.
    jmp dskw_rmbody             ; ...and it goes THERE rather than to
                                ; dskw_fsop so that "what a redirected rmdir
                                ; is" has one answer: that body's own
                                ; DVK_FILE arm two hundred lines up. It
                                ; re-runs the [dsk_mntok] gate, which is free
.protj:                         ; ...and the two attribute refusals below take
    jmp dskw_wbody.prot         ; this, on a path nothing falls into: NASM
                                ; spelt each of them as a five-byte inverted
                                ; pair, an 8086 having no near `jcc`, and from
                                ; here they are two bytes each (-3)
.fsno:                          ; (`or` cleared CF on the way here)
    stc
.fsok:                          ; ...and CF = 0 is the `jnc`'s
    ret
.fat:
    mov ax, [dsk_cwd]           ; where we must end up, whatever happens
    mov [dskw_rt_home], ax
    mov di, dskw_rt_name        ; keep the caller's name: the last removal
    call dskw_name83            ; happens back here, and by then the walk has
                                ; overwritten dskw_n83 many times over
    jnc .named
    jmp dskw_nbody.badarg       ; FERR_NAME, spelled once
.named:
    mov si, dskw_rt_name        ; dskw_find matches through dskw_n83
    mov di, dskw_n83
    call dskw_copy11
    call dskw_find
    jc .fsok                    ; AX = FERR_NOENT / FERR_IO already - and
                                ; .fsok is a bare `ret` a hop back (this
                                ; routine's own, at its foot, was one past
                                ; short reach, and nothing else reached it)
    mov al, [dskw_raw+DSK_R_ATTR]
    test al, DSKW_NPROT
    jnz .protj                  ; read-only, hidden, system or volume label
    test al, DSKW_DIR
    jz .protj                   ; a file: the caller wanted dskw_delete
    mov ax, [dskw_raw+DSK_R_CLUS]
    call dskw_clok
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
    call dskw_clok
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
    call dsk_dotdot_x ; ...and the parent the disk says it has
    jc .corrupt_home
    call dskw_chdir_dl
    jc .iohome
    call dskw_rt_byclus         ; find that child again, by cluster
    jc .corrupt_home
    call dskw_rmbody_n83        ; and remove it, emptiness re-checked
    jc .home
    jmp short .loop

.corrupt_home:                  ; a corrupt link and an I/O error are the same
.iohome:                        ; answer, so the two pairs are one: go home,
    call dskw_rt_go_home        ; then FERR_IO
.corrupt:
.ioerr:
    jmp dskw_rdata.corrupt      ; FERR_IO, spelled once
.home:                          ; a failure part-way down: report it from
    push ax                     ; where the caller left us, not from wherever
    call dskw_rt_go_home        ; the walk happened to stop
    pop ax
    stc
    ret

.finish:                        ; BELOW the shared tail, which it only jumps
    call dskw_rt_go_home        ; into: above it, it kept the dskw_clok's
    jc .ioerr                   ; `jc .corrupt` five bytes past short reach
    mov si, dskw_rt_name
    mov di, dskw_n83
    call dskw_copy11
    jmp dskw_rmbody_n83         ; the target itself, from where we began - a
                                ; tail call, both ends `.cold`

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
    call dsk_chdir_x
    pop dx
    ret

%endif                          ; KD_BUILD - and `dskw_clok` below is NOT in the
                                ; band: `dskw_rmdir` asks it too (the header
                                ; below says ten places do)

; -----------------------------------------------------------------------------
; dskw_clok - is AX inside [2, dsk_maxclus], i.e. a cluster that could exist?
; in:  AX; out: CF=1 = no. Preserves AX and every other register.
;
; TEN places ask this and eight of them used to spell it out; it was rmtree's
; alone, which is what the old name said and what kept the other eight from
; finding it. NOT extended to dsk_next_clus_x's callers in disk.inc, and
; specifically not into dskw_alloc's `.try` loop, which runs it up to
; [dsk_maxclus] times: eleven microseconds a cluster is 440 ms on a
; nearly-full 20MB partition.
; -----------------------------------------------------------------------------
dskw_clok:
    cmp ax, 2
    jb .out                     ; CF = 1: below the first data cluster
    cmp [dsk_maxclus], ax       ; CF = 1 exactly when AX is past the last -
.out:                           ; the compare turned round, so the flag IS
    ret                         ; the answer and needs no stc/clc arms

%ifndef KD_BUILD                ; ...and the second band of the same pipeline

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
    push ss
    pop es
    mov ax, [dsk_cwd]
    call dskw_scan1             ; the walk is dskw_isempty's, shared
    jnc .found
    jz .none
    mov byte [dskw_rt_ioerr], 1
.none:
    stc
    jmp short .out
.found:
    mov [dskw_cursec], dx       ; ...and its high word is the walker's

    call dskw_hit               ; the WHOLE entry, and where it is - exactly
                                ; as dskw_find does, so dskw_rt_zap can
                                ; rewrite the slot without a name lookup
                                ; (dskw_find refuses to match an LFN entry at
                                ; all, by design)
    mov si, dskw_raw
    mov di, dskw_n83
    call dskw_copy11
    mov al, [dskw_raw+DSK_R_ATTR]
    mov [dskw_rt_attr], al
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_rt_clus], ax
    clc
.out:
    jmp dskw_find.out           ; the same six pushes in the same order, so
                                ; dskw_find's pop run is this one's too

; -----------------------------------------------------------------------------
; dskw_rt_byclus - find the subdirectory of the CURRENT directory whose first
;                  cluster is [dskw_rt_clus]
; out: CF=0 with dskw_n83 = its 11 raw name bytes; CF=1 = no such entry
; clobbers: AX, BP, CF
;
; How the walk gets back the name of the folder it just emptied without
; having carried one down. The cluster is the identity: two directory entries
; cannot legitimately share one, and if a corrupt volume makes them, removing
; either is removing the thing this cluster is.
;
; THE SCAN IS disk.inc's dsk_byclus (SPEC.md 19.2.4): OSAPI_FILE_PATH names
; each level of a path by exactly this search, and the slot's first cycle
; carried a second copy of it. What is left here is the raw entry's trip
; into dskw_raw and dskw_n83, which the remove body reads.
; -----------------------------------------------------------------------------
dskw_rt_byclus:
    push cx
    push dx
    push si
    push di
    mov ax, [dsk_cwd]
    mov dx, [dskw_rt_clus]
    call dsk_byclus_x           ; SS:SI -> the entry; dsk_ent is a by-product
    jc .out
    mov di, dskw_raw            ; 32, not 11: dsk_copy_in copies WORDS
    mov cx, 32
    call dsk_copy_in_x
    mov si, dskw_raw
    mov di, dskw_n83
    call dskw_copy11
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
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
    and word [dskw_oldclus], 0  ; a delete's commit with NO chain to release:
    jmp dskw_rmbody_n83.delent  ; dskw_relold's dskw_clok refuses 0. And
                                ; dskw_rt_scan staged the entry and published
                                ; [dskw_dsec]/[dskw_doff] with it, so there is
                                ; nothing here to read back off the disk

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
%endif                          ; KD_BUILD - **`dskw_isempty` IS LIVE** and is
                                ; why the band stops here: `dskw_rmdir` calls
                                ; it to refuse a populated directory, which is
                                ; exactly what AH=3Ah is
dskw_isempty:
    push bx
    push cx
    push dx
    push si
    push es
    push ss
    pop es
    mov ax, [dskw_oldclus]
    call dskw_scan1             ; the walk is dskw_rt_scan's, shared
    cmc                         ; CF = 1 now means "something is in it"
    jnc .none
    mov ax, FERR_PROT           ; anything that is not '.' or '..'
    jmp short .out
.none:
    jz .out                     ; the chain ended with nothing found: CF = 0
    call dskw_ioerr             ; AX = FERR_IO / FERR_WPROT, CF = 1
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_scan1 - the first LIVE entry of a directory: not free, not deleted,
;              not '.' or '..' - the one walk dskw_isempty and dskw_rt_scan
;              share, so "occupied" there and "nameable" here cannot disagree
; in:  AX = the directory's first cluster (0 = the root), ES = SS
; out: CF=0 - ES:SI -> the entry inside dsk_secbuf, DX = that sector's LBA
;      (the walker holds its high word); CF=1, ZF=1 - there is none;
;      CF=1, ZF=0 - a sector could not be read ([dsk_ioerr])
; clobbers: AX, CX, DX, SI, flags
;
; A dot link's SECOND byte is the other dot or a pad space. DOS never makes a
; name like '.XT     TXT', but a foreign disk may carry one, and this module
; trusts nothing it reads (SPEC.md 18.6) - so such an entry is live.
; -----------------------------------------------------------------------------
dskw_scan1:
    call dsk_dirw_start_x
.sector:
    call dsk_dirw_get_x         ; dsk_secbuf = the next sector (19.2.1)
    jc .out                     ; ...and ZF says which failure it was
    xchg dx, ax                 ; DX = its LBA
    mov si, dsk_secbuf
    mov cx, 16                  ; entries per sector
.entry:
    mov al, [es:si]
    test al, al
    jz .none                    ; end of directory: nothing past it counts
    cmp al, 0xE5
    je .next                    ; a deleted entry
    cmp al, '.'
    jne .live                   ; anything that is not '.' or '..'
    mov al, [es:si+1]
    cmp al, '.'
    je .next
    cmp al, ' '
    jne .live
.next:
    add si, 32
    loop .entry
    jmp short .sector
.live:
    clc
    ret
.none:
    stc                         ; ZF = 1 from the `test`, which is the answer
.out:
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
%ifdef OS88_REDIR
; dskw_fsargs - a redirected transfer's buffer and count, as FSV_READ and
; FSV_WRITE both take them (SPEC.md 51.8's rule): DX:BX = [dskw_seg]:
; [dskw_src], DI:CX = [dskw_lenhi]:[dskw_len]. Clobbers exactly those four.
dskw_fsargs:
    mov bx, [dskw_src]
    mov dx, [dskw_seg]
    mov cx, [dskw_len]
    mov di, [dskw_lenhi]
    ret
%endif

; -----------------------------------------------------------------------------
; dskw_fsop - run one FSV_* write verb and reconcile (module internal)
; in:  BP = the verb, plus whatever that verb's own arguments are
; out: CF and AX exactly as the FAT body it replaced would have answered
; clobbers: BP, and whatever the verb clobbers
;
; The tail all six write bodies share on a redirected volume (SPEC.md 62.9),
; and sharing it is the point rather than a saving: dskw_sync_x is what marks
; every Disk window showing this folder (SPEC.md 22.8) and rebuilds the global
; snapshot, and it has to run after a SUCCESSFUL write of any kind. Six copies
; of that would be six chances to forget one, which is precisely how a
; coherence rule decays - and the FAT side already learned it, which is why
; dskw_sync has one caller per operation and not one per code path.
;
; THE PROTECTION MASK IS THE DRIVER'S HERE, and that is a real difference
; worth stating. DSKW_PROT is a test on the raw FAT attribute byte out of a
; directory entry, and a redirected volume has neither. A driver that wants
; a file to be undeletable refuses FSV_DELETE; the kernel cannot do it for it.
; -----------------------------------------------------------------------------
dskw_fsop:
    call drv_fs_call
    jc .out                     ; AX = the driver's FERR_*, passed through
    call dskw_sync_x            ; the folder changed (SPEC.md 22.8/18.4)
    clc                         ; ...and dskw_sync does not propagate CF
.out:
    ret

dskw_wbody:
    call dskw_mounted           ; returns only when there is a volume
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: the write's
    cmp byte [dsk_vkind], DVK_FILE
    jne .fatw
    call dskw_norm              ; the source becomes seg:0..15 + a run, so the
                                ; driver is handed a normalised (seg, offset)
                                ; exactly as a read is
    mov cx, [dskw_len]          ; ...and arm the widget as the FAT arm below
    mov dx, [dskw_lenhi]        ; does (DX:CX = the job in BYTES, SPEC.md
    call ct_fpg_begin               ; 12.8.1): the driver steps it from inside
                                ; FSV_WRITE through OSAPI_FS_PROG, and without
                                ; this that report has nothing to report into.
                                ; dskw_write_x ends it either way
    call dskw_fsargs            ; DX:BX = the bytes, DI:CX = how many
    mov bp, FSV_WRITE
    jmp dskw_fsop
%endif                          ; OS88_REDIR
.fatw:
    call dskw_gate83            ; SI -> the 11-byte raw name field
    call dskw_norm              ; the source becomes seg:0..15 + a segment run

    ; --- size the job: sectors, and the length of a partial final one -----
    call dskw_size32            ; [dskw_rem] / [dskw_tail]
    jnc .sized
.big:                           ; (dskw_rbody's refusal too)
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
    cmp word [DSKW_FNONE], 0xFFFF
    jne .newslot
    jmp dskw_mkbody.dirfull
.newslot:
    mov byte [dskw_new], 1      ; the slot dskw_find already spotted
    mov word [dskw_oldclus], 0
    jmp short .data
.replace:
    mov byte [dskw_new], 0      ; dskw_find left the entry's 32 raw bytes in
                                ; dskw_raw two instructions ago (SPEC.md
                                ; 18.4.3), so this path re-read the sector it
                                ; had just been handed - a whole revolution
    call dskw_pmask             ; AL = what may not be replaced here
    test byte [dskw_raw+DSK_R_ATTR], al
    jz .keep
.prot:                          ; (dskw_nbody, dskw_wabody and
    mov ax, FERR_PROT           ; dskw_rmbody_n83 answer here too)
    stc                         ; read-only, hidden, system, label or dir
    ret
.keep:
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_oldclus], ax      ; freed only after the commit, below

    ; --- 1. allocate the new chain and write its data ---------------------
    ; The old chain stays allocated throughout, so the two are disjoint and
    ; a crash here costs nothing. The price is that a replace briefly needs
    ; room for both copies (SPEC.md 18.4).
.data:
    mov cx, [dskw_len]          ; arm the progress widget HERE and not up at
    mov dx, [dskw_lenhi]        ; .sized, though the count is known there:
    call ct_fpg_begin               ; (DX:CX = the job in BYTES, SPEC.md 12.8.1)
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
    jmp dskw_relold
.out:
    ret
.rollback:
    call dskw_refat             ; drop the half-built chain: invalidate the
    stc                         ; FAT window. It keeps AX, the error
    ret

; -----------------------------------------------------------------------------
; dskw_wdata - allocate the chain and write the bytes into it
; in:  [dskw_rem] = sectors, [dskw_tail] = bytes in a partial final sector,
;      [dskw_seg]:[dskw_src] = the source, normalised by dskw_norm
; out: CF=0 with [dskw_first] = the chain's head (0 for an empty file), or
;      CF=1 with AX = FERR_*
; clobbers: AX (the output), CF - and SI, DI, ES, BP like every module-
;           internal body: dskw_wrp banked them, so nothing here does. A
;           caller holding a flag in SI across this call loses it, which is
;           how dskw_wabody's first version stopped moving the size
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
; cannot carry still goes alone through the stage (.stg, and SPEC.md 18.4.2.1
; is why that sentence only became true there), and dskw_runmax is only
; ever asked with no run pending, so that path can never orphan one. And
; every failure exit leaves the run unspent, which is exactly right: a
; half-built chain is rolled back by dskw_refat and nothing that was not
; written needs unwriting.
; -----------------------------------------------------------------------------
dskw_wdata:
    xor ax, ax                  ; AX is this routine's OUTPUT and never its
    mov [dskw_first], ax        ; input - the contract above says so - so the
    mov [dskw_prev], ax         ; four stores are the accumulator's: 15 bytes
    mov [dskw_rlen], ax         ; against 23, and no data run is pending
    inc ax
    mov [dskw_fop], al          ; the direction, set ONCE for the body rather
                                ; than three bytes ahead of every flush
    mov ax, [dskw_seg]
    mov [dskw_wseg], ax         ; the ADVANCING source segment
    cmp word [dskw_rem], 0
    jne .clus
    ret                         ; an empty file owns no clusters at all, and
                                ; CF = 0 out of the compare with 0
.clus:
    call dskw_alloc             ; AX = a cluster, already marked EOC
    jnc .haveclus
    inc ax                      ; 0xFFFF -> 0: an I/O failure wearing the
    jz .ioerr                   ; allocator's refusal, and 'Disk full' is the
    jmp dskw_mkbody.nospace     ; one thing it certainly is not
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
    call dskw_take1             ; whole sectors of this take, APPENDED TO THE
    jc .ioerr                   ; PENDING RUN rather than written here - see
                                ; the header. CX is the count, [dskw_clba]
                                ; the cursor. (The carry is unreachable:
                                ; dskw_alloc bounds the cluster)
.addrun:
    call dskw_runadd            ; the whole sectors of this take, appended to
    jc .ioerr                   ; the pending run (SPEC.md 18.4.2). CX comes
    jcxz .tail                  ; back non-zero when the run cap said 0, and
                                ; THAT FALLS THROUGH TO .stg (SPEC.md
                                ; 18.4.2.1). It did not until this: .jioerr
                                ; sat here, so dskw_runadd's third answer -
                                ; "not one sector fits this DMA page, stage
                                ; it" - landed on the media-error arm and .stg
                                ; had no way in at all, from 2e8e292 onwards
.stg:                           ; one sector the 8237 cannot carry, alone,
    push cx                     ; through the scratch - which is 512-aligned
    mov cx, 512                 ; and therefore never straddles. No run can be
    call dskw_wone              ; pending here: dskw_runmax answered 0, and it
    pop cx                      ; is only asked with [dskw_rlen] = 0
    jc .jioerr
    DSKW_CLBA_INC
    add word [dskw_wseg], 32    ; 512 bytes = 32 paragraphs on. The source is
    loop .addrun                ; now 1..511 bytes PAST the page edge, so the
    jmp short .tail             ; next dskw_runadd gets the whole 127 back:
                                ; one staged sector per 64KB of source, at most
.ioerr:                         ; THE ERROR ARM ITSELF, here in the middle of
.jioerr:                        ; the routine below an unconditional jump: the
    jmp dskw_ioerr              ; arms above and below both reach it short, so
                                ; the relay that used to stand here (SPEC.md
                                ; 18.4.2.1) has nothing left to relay to.
                                ; AX = FERR_IO / FERR_WPROT, CF = 1: a tail
                                ; call, dskw_ioerr's own `stc / ret`

    ; the partial final sector: staged zero-filled, so nothing of the
    ; caller's neighbourhood past EOF ever reaches the disk. The run goes
    ; first - its bytes come BEFORE these, and flushing it is also what
    ; advances [dskw_wseg] to where the tail's source actually is.
.tail:
    call dskw_last_p
    jnz .advance
    call dskw_flushrun
    jc .jioerr
    mov cx, [dskw_tail]
    call dskw_wone              ; the sector the whole-sector walk stopped at,
    jc .jioerr                  ; the live bytes and the rest zeroed
.advance:
    mov ax, [dskw_take]
    sub [dskw_rem], ax
    jz .done
    jmp .clus                   ; (near: the loop body outruns a short jump)
.done:
    call dskw_flushrun            ; the last run of the file
    jc .jioerr
    ret                         ; CF = 0 from the `jc`

; -----------------------------------------------------------------------------
; dskw_runadd - append CX whole sectors at [dskw_clba] to the pending run
; in:  CX = sectors, [dskw_clba] = the first of them, [dskw_fop] = direction
; out: CF=1 = I/O error, AX = FERR_*.  CF=0 with CX=0 = all of them are in the
;      run.  CF=0 with CX != 0 = dskw_runmax answered 0, so not even one
;      sector fits this DMA page: the caller stages that one and calls again
;      (SPEC.md 18.4.2.1 - and NEITHER CALLER DID until it, because a shared
;      `jmp .ioerr` trampoline sat where the third answer fell through)
; clobbers: AX, CX (an output), flags
;
; THE RUN ARITHMETIC, ONCE. dskw_wdata and dskw_rdata carried a copy each and
; they were identical to the instruction bar the flush they called - which is
; the same thing [dskw_fop] and dskw_flushrun were introduced to stop (SPEC.md
; 18.4.2): only the flush had been unified, and the arithmetic around it was
; still two copies free to drift.
;
; The entry `jcxz` is the callers' own `or cx,cx / jz .tail`, moved in. It
; matters: a take that is nothing but a partial sector arrives here with CX=0
; and a run possibly pending, and going round the loop with CX=0 would flush
; that run one boundary early.
; -----------------------------------------------------------------------------
dskw_runadd:
    jcxz .done
.sect:
    cmp word [dskw_rlen], 0
    je .newrun
%ifdef OS88_BIGVOL
    push dx                     ; does this sector continue the pending run?
    mov ax, [dskw_clba]         ; In 32 bits (SPEC.md 18.7.5): cursor - run
    sub ax, [dskw_rlba]         ; start == run length, which is a word - so
    mov dx, [dskw_clba+2]       ; the high half of the difference is 0 and
    sbb dx, [dskw_rlba+2]       ; its low half is the length. DX is the
    sub ax, [dskw_rlen]         ; caller's (`mov` leaves the borrow alone)
    or ax, dx
    pop dx
%else
    mov ax, [dskw_rlba]         ; does this sector continue the pending run?
    add ax, [dskw_rlen]
    cmp ax, [dskw_clba]
%endif
    jnz .breakrun
    mov ax, [dskw_rcap]         ; ...and is there room left in it?
    cmp ax, [dskw_rlen]
    ja .append
.breakrun:
    call dskw_flushrun          ; no: spend it and start again here
    jc .out
    jmp short .sect
.newrun:
    call dskw_runmax            ; AX = the cap, from the CURRENT wseg:src -
    or ax, ax                   ; which is where this run's first byte is
    jz .out                     ; not even one sector fits its DMA page, and
                                ; `or` clears CF: CX != 0 is the answer
    mov [dskw_rcap], ax
    mov ax, [dskw_clba]
    mov [dskw_rlba], ax         ; ([dskw_rlen] is already 0)
%ifdef OS88_BIGVOL
    mov ax, [dskw_clba+2]
    mov [dskw_rlba+2], ax
%endif
.append:
    mov ax, [dskw_rcap]
    sub ax, [dskw_rlen]         ; room in the run...
    cmp ax, cx
    jbe .grow
    mov ax, cx                  ; ...and what is left of this cluster's take
.grow:
    add [dskw_rlen], ax
    add [dskw_clba], ax
%ifdef OS88_BIGVOL
    adc word [dskw_clba+2], 0
%endif
    sub cx, ax
    jnz .sect
.done:
    clc
.out:
    ret

; -----------------------------------------------------------------------------
; dskw_flushrun - spend the pending data run (module-internal)
; in:  [dskw_rlba]/[dskw_rlen], the source at [dskw_wseg]:[dskw_src]
; out: CF=0 and [dskw_rlen] = 0, [dskw_wseg] advanced past it; CF=1 = I/O
; clobbers: flags
;
; A no-op with nothing pending, which is what lets every path out of the
; cluster loop call it without asking.
;
; [dskw_fop] is set ONCE at the top of dskw_wdata / dskw_rdata now rather than
; three bytes ahead of each flush. That is a weaker invariant than it was:
; it holds because this routine has exactly two callers' worth of call sites
; and nothing in either body's call tree re-enters the other (dsk_rah_fill
; and the FAT window both go through disk_read, not through here).
; -----------------------------------------------------------------------------
dskw_flushrun:
    push ax
    push bx
    push cx
    push es
    mov cx, [dskw_rlen]
    jcxz .out
%ifdef OS88_BIGVOL
    mov bx, dskw_rlba           ; AX = the run's LBA and its high word armed
    call dsk_armp               ; for the transfer that is next (18.7.5)
%else
    mov ax, [dskw_rlba]
%endif
    mov es, [dskw_wseg]
    mov bx, [dskw_src]
    cmp byte [dskw_fop], 0      ; in: AX=LBA, CX=count, ES:BX
    je .rd
    call disk_write_x
    jmp short .did
.rd:
    call disk_read_x
.did:
    jc .pop                     ; CF = 1 is the answer, as it arrives
    xor ax, ax
    xchg ax, [dskw_rlen]        ; read the run and clear it in one (-3)
    mov cl, 5
    shl ax, cl                  ; sectors -> paragraphs, 32 each
    add [dskw_wseg], ax
.out:
    clc                         ; the answer first, then ONE pop ladder: a
.pop:                           ; `pop` writes no flag
    pop es
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_clba_inc - the data walk's cursor, one sector on, in 32 bits
; out: nothing (every register preserved; flags clobbered)
; DSKW_CLBA_INC is the call, or the plain `inc` it always was on kern_small
; -----------------------------------------------------------------------------
%ifdef OS88_BIGVOL
dskw_clba_inc:
    add word [dskw_clba], 1
    adc word [dskw_clba+2], 0
    ret

; dskw_clba_set - [dskw_clba] = AX:[dsk_c2hi], dsk_clus2lba_x's whole answer.
; Every register and the flags preserved. DSKW_CLBA_SET is the call, or the
; plain word store on kern_small
dskw_clba_set:
    mov [dskw_clba], ax
    push word [dsk_c2hi]
    pop word [dskw_clba+2]
    ret
%endif

; -----------------------------------------------------------------------------
; dskw_take1 - size one cluster's TAKE for the data loops
; in:  AX = the cluster, [dskw_rem]/[dskw_tail] sized by the caller
; out: CF=1 the geometry cannot address it; else CF=0, [dskw_take] =
;      min(spc, sectors remaining), [dskw_clba] = the cluster's first LBA and
;      CX = the whole sectors of the take (one fewer when it ends in a
;      partial sector, which goes through the stage)
; clobbers: AX, CX (an output), flags
;
; dskw_wdata and dskw_rdata carried this per-cluster prologue as two copies,
; identical to the instruction. The cursor is set BEFORE the count is tested:
; a take that is nothing but a partial sector skips the walk and .tail still
; reads it. Once a cluster, not once a sector: a call and a return on a loop
; whose every turn ends in the FAT and the run arithmetic.
; -----------------------------------------------------------------------------
dskw_take1:
    call dsk_clus2lba_x         ; AX = the cluster's first LBA
    jc .out
    DSKW_CLBA_SET               ; ...all 32 bits of it (SPEC.md 18.7.5)
    mov al, [dsk_spc]           ; take = min(spc, sectors remaining)
    xor ah, ah
    cmp ax, [dskw_rem]
    jbe .take
    mov ax, [dskw_rem]
.take:
    mov [dskw_take], ax
    mov cx, ax
    call dskw_last_p            ; ZF=1 if this take ends in a partial sector,
    jnz .out                    ; and CF=0 on both of its answers
    dec cx                      ; (dec leaves CF alone)
.out:
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
    call dskw_mounted           ; returns only when there is a volume
    cmp byte [dsk_vkind], DVK_FILE
    jne .fatname
    jmp .fsread                 ; a REDIRECTED volume (SPEC.md 62.9): there is
                                ; no chain to walk and no entry to walk it from
.fatname:
    call dskw_gate83
    call dskw_norm              ; the destination becomes seg:0..15 + a run
    call dskw_find
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    test byte [dskw_raw+DSK_R_ATTR], 0x18
    jz .isfile
    jmp dskw_wbody.prot         ; a volume label or a subdirectory: FERR_PROT
.out:
    ret
.isfile:
    mov al, [dskw_raw+DSK_R_CZM]    ; **AND IF THE DIRECTORY DID NOT SAY, ASK
    sub al, DSK_CZ_MARK             ; THE FILE** (SPEC.md 20.14.6). The hint is
    cmp al, LZ_LZB                  ; a CACHE and a foreign tool drops it: a
    jbe .sniffed                    ; file copied in by DOS reached the
    call dskw_czsniff               ; application packed. A file that HAS one
.sniffed:                           ; never gets here, so our own disks pay a
                                    ; compare and no I/O at all
    ; --- ONE PLACEMENT FOR BOTH KINDS OF FILE (SPEC.md 20.14.2) -------------
    ; A compressed file's packed bytes are read HIGH in the caller's buffer,
    ; R = U - P above its base, and expanded downwards onto themselves - no
    ; second claim and no scratch, exactly as the package loader does it
    ; (SPEC.md 20.13). A plain file is the SAME arithmetic with U = P: R is 0
    ; and the read lands where it always did. There is no margin in R any
    ; more - the stream's own raw tail is what makes U - P exactly enough
    ; (SPEC.md 20.13.7) - so the whole capacity question is U against the
    ; caller's capacity, once, BEFORE any data I/O; FERR_BIG still leaves the
    ; destination untouched, and a buffer of exactly U is enough for either
    ; format. The sliding window that used to serve that case, and the rule
    ; refusing to write an LZ4 file that would land in it, went with the
    ; margin.
    ;
    ; **ALL OF IT IS 32-BIT**, because the file this whole feature is for is
    ; BEVERLY.MOD at 116,085 bytes: 42,169 compressed, 36.3%, 145 sectors of a
    ; floppy. `R` is a byte offset that can be tens of KB, so it goes into the
    ; SEGMENT and never into [dskw_src], which is how the destination reaches
    ; past 64KB without any of this arithmetic being able to wrap (SPEC.md
    ; 18.4.1's idiom, one layer up); its low nibble lives in [dskw_src], which
    ; every consumer already reads - dskw_rdata writes to seg:src and
    ; dskw_czexp finds the 'CZ' header at it.
    mov ax, [dskw_raw+DSK_R_SIZE]       ; P: what the file OCCUPIES, and what
    mov dx, [dskw_raw+DSK_R_SIZE+2]     ; gets READ, compressed or not
    mov [dskw_len], ax
    mov [dskw_lenhi], dx
    mov bx, ax
    mov cl, [dskw_raw+DSK_R_CZM]        ; the hint: the mark LESS its format,
    sub cl, DSK_CZ_MARK                 ; so 0x5A and 0x5B pass and nothing
    cmp cl, LZ_LZB                      ; else (SPEC.md 20.14.1)
    ja .sizes                           ; a plain file becomes what it is.
                                        ; P PAST 64KB IS FINE: the decoder's
                                        ; source crosses too (SPEC.md
                                        ; 20.14.5.1), and the hint's U being
                                        ; bigger than P is checked below
    mov ax, [dskw_raw+DSK_R_CZL]        ; U, 24 bits out of the hint. A hint
    mov dl, [dskw_raw+DSK_R_CZH]        ; no bigger than the file it describes
    mov dh, 0                           ; has drifted from it, and it is caught
    mov [dskw_uz], ax                   ; below: R = U - P BORROWS, and a
    mov [dskw_uzhi], dx                 ; borrowed R is past the megabyte
    cmp word [dskw_src], 0
    jne .czbad                          ; a compressed file expands into a
                                        ; PARAGRAPH-ALIGNED buffer: lz_decomp_x
                                        ; wants DI = 0 (SPEC.md 20.13.3), the
                                        ; SDK says so, and R's own low nibble
                                        ; is about to share this word - so an
                                        ; offset here could not be told from
                                        ; it later, and is refused now
.sizes:                                 ; DX:AX = U, BX = P's low word
    cmp dx, [dskw_caphi]                ; U against the caller's 32-bit
    jb .fits                            ; capacity, BEFORE any data I/O
    ja .toobig
    cmp ax, [dskw_cap]
    jbe .fits
.toobig:                        ; too big for the buffer: nothing is written
%ifdef OS88_SHORTCUTS
    cmp byte [dskw_trunc], 0    ; ...UNLESS the caller asked for the file's
    je .big                     ; first bytes and no more - mod_need's
    cmp ax, bx                  ; settings core (SPEC.md 2.8.7) - and the
    jne .big                    ; file is PLAIN (U = P): a packed stream's
    mov ax, [dskw_cap]          ; prefix decodes to nothing. P becomes the
    mov dx, [dskw_caphi]        ; capacity, so the chain walk stops there and
    mov bx, ax                  ; the answer is what arrived
    mov [dskw_len], ax
    mov [dskw_lenhi], dx
    jmp .fits
.big:
%endif
    cmp dx, 0x3FF               ; 64MB or more SATURATES at 0xFFFF KB, which
    jb .kb                      ; no claim meets: the shift below would drop
    mov dx, 0xFFFF              ; U's top bits and the inc would wrap, and a
    jmp short .kbsaid           ; small wrong figure is a retry that lies
.kb:
    mov cl, 10                  ; ...and DX = the KB it WOULD take, rounded
    shr ax, cl                  ; up (SPEC.md 20.14.6.3). U is in hand here
    mov cl, 6                   ; and nowhere a caller can reach: a HINTLESS
    shl dx, cl                  ; compressed file was sized by FIND at what
    or dx, ax                   ; it OCCUPIES, and only this read's sniff
    inc dx                      ; knows what it becomes. One over on an
.kbsaid:
    jmp dskw_wbody.big          ; exact multiple, which a claim in KB does
                                ; not mind; CX is dskw_read_x's to restore
.fits:
    mov cx, ax
    or cx, dx
    jz .empty                   ; a 0-byte file: no chain to walk, and DX:AX
                                ; is already the 0 to answer with
    sub ax, bx                  ; R = U - P: where the packed bytes go so that
    sbb dx, [dskw_lenhi]        ; the writer can never overtake the reader
                                ; (SPEC.md 20.14.2) - and 0 for a plain file
    cmp dx, 0x10
    jae .czbad                  ; R past a megabyte, which is what a hint
                                ; smaller than its file comes out as, and not
                                ; an address in this machine either way - the
                                ; shift below is the one thing here that could
                                ; lose it
    mov cx, [dskw_seg]          ; where the expansion LANDS, banked before R
    mov [dskw_czseg], cx        ; goes into [dskw_seg]
    add ax, [dskw_src]          ; R's low nibble is the OFFSET - and a plain
    mov [dskw_src], ax          ; read's own 0..15 survives, R being 0
    and word [dskw_src], 15     ; there...
    mov cl, 4
    shr ax, cl                  ; ...and the rest is paragraphs, the high
    mov cl, 12                  ; word carrying what does not fit
    shl dx, cl
    or ax, dx
    add [dskw_seg], ax
    call dskw_size32            ; [dskw_rem] / [dskw_tail], off [dskw_len]
    jc .czbad                   ; >= 32MB: impossible on this media, so the
                                ; entry is hostile rather than merely large
    mov cx, [dskw_len]          ; the widget's scale, in BYTES (SPEC.md
    mov dx, [dskw_lenhi]        ; 12.8.1) - a 116KB module is a minute of
    call ct_fpg_begin               ; frozen screen; dskw_read_x ends it
%ifdef OS88_REDIR
    cmp byte [dsk_vkind], DVK_FILE
    je .fsdata                  ; **THE ONE THING THE TRANSPORT DECIDES**
%endif                          ; (SPEC.md 20.14.6): who moves the bytes.
    mov ax, [dskw_raw+DSK_R_CLUS]   ; Everything either side of this - the
    mov [dskw_cur], ax              ; capacity refusal, the compressed
    call dskw_rdata             ; walk the chain into the caller's buffer
.read_done:
    jc .ret                     ; (a `ret` a hop on: .out is past short
                                ; reach, and CF=1 rides through either)
    mov al, [dskw_raw+DSK_R_CZM]    ; ...and if it arrived COMPRESSED, expand
    sub al, DSK_CZ_MARK             ; it before anyone is told it is there.
    cmp al, LZ_LZB                  ; The hint again, dskw_raw being untouched
    ja .plainlen                    ; by the transfer: a flag would cost more
    call dskw_czexp
    jc .czbad
    mov ax, [dskw_uz]           ; the caller is told the UNPACKED size, which
    mov dx, [dskw_uzhi]         ; is the whole point (SPEC.md 20.14) - CF = 0
.ret:                           ; from the `jc`
    ret
.plainlen:
    mov ax, [dskw_len]          ; bytes read = the file's size, 32-bit
    mov dx, [dskw_lenhi]
.empty:                         ; CF = 0 on both ways in: `ja` is taken only
    ret                         ; with CF = 0, and .empty's `jz` follows an `or`
.czbad:
    jmp dskw_rdata.corrupt      ; the same answer, spelled once

    ; --- a REDIRECTED volume: STAT to a handle, then READ (SPEC.md 62.9.1) ---
    ; Reads are BY HANDLE and only FSV_STAT takes a name, so this is `open`
    ; and `read` - which is also what makes the refusals below identical to
    ; the FAT path's: the same 0x18 attribute test and the same 32-bit size
    ; check against the caller's capacity, BOTH before a byte crosses.
.fsread:
    call dskw_norm              ; the destination becomes seg:0..15 + a run,
                                ; exactly as it does for a chain walk - the
                                ; driver is handed a normalised (seg, offset)
                                ; and cannot be made to carry past 64KB
    push bp
    mov bp, FSV_STAT
    call drv_fs_call              ; SI = the name -> AX = the handle,
    pop bp                      ; DX:CX = size, BL = attributes
    jc .fsfail
    test bl, 0x18
    jnz .fsprot                 ; a folder or a volume label, as .isfile says
    ; **AND THEN IT JOINS THE ONE FLOW** (SPEC.md 20.14.6). What stood here
    ; was this arm's own copy of the capacity test, its own FERR_BIG and its
    ; own fpg_begin - and none of that is about the TRANSPORT. Writing the
    ; size where the FAT arm reads it, and the handle where the FAT arm keeps
    ; its first cluster, leaves exactly one thing that differs: who moves the
    ; bytes, which is the branch at the read below. The compressed placement,
    ; the 32-bit capacity refusal and the expansion are had for nothing.
    mov [dskw_raw+DSK_R_SIZE], cx
    mov [dskw_raw+DSK_R_SIZE+2], dx
    mov [dskw_raw+DSK_R_CLUS], ax   ; the handle, IN THE CELL THE FAT ARM
                                ; keeps its first cluster in - dskw_raw is
                                ; this arm's to fill (there is no directory
                                ; entry here to overwrite), and putting it
                                ; there is what lets dskw_czsniff and
                                ; dsk_peek_x below be told the transport by
                                ; [dsk_vkind] alone rather than by an argument
    xor ax, ax                          ; NO DIRECTORY ENTRY HERE, so there is
    mov [dskw_raw+DSK_R_CZM], ax        ; no hint to inherit and no stale one
    mov [dskw_raw+DSK_R_CZL], ax        ; to trust - and through the
                                        ; accumulator, which is 8 bytes where
                                        ; two `mov word [m], 0` are 12
    call dskw_czsniff                   ; to trust: the file's own header is
    jmp .sniffed                        ; the only thing that can say
.fsprot:
    mov ax, FERR_PROT
.fsfail:
    stc
    ret
%ifdef OS88_REDIR
.fsdata:                        ; the widget's scale is BYTES (SPEC.md 12.8.1)
                                ; and fpg_begin above was handed them: a
                                ; redirected volume has no sectors to give it,
                                ; so before that the one file operation that
                                ; can take a MINUTE over a cable was the only
                                ; one in the machine reporting nothing at all.
                                ; The driver steps it from inside FSV_READ
                                ; through OSAPI_FS_PROG, which is the other
                                ; half: the kernel is not standing in a loop
                                ; here, it is one far call deep and blind
                                ; until the verb returns
    push bp
    mov ax, [dskw_raw+DSK_R_CLUS]   ; the handle FSV_STAT answered, banked
    call dskw_fsargs            ; ...and the destination the placement above
                                ; has already moved up by R when the file is
                                ; compressed, which is the whole point of
                                ; joining: P bytes, where P is what the FILE
    mov bp, FSV_READ            ; occupies and never what it becomes
    call drv_fs_call              ; -> DX:AX = bytes read
    pop bp
    jmp short .read_done        ; ...and the expansion below is shared
%endif

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
    mov byte [dskw_fop], 0      ; the direction, set ONCE for the body
    mov word [dskw_rlen], 0     ; no data run pending
    mov ax, [dskw_seg]
    mov [dskw_wseg], ax         ; the ADVANCING destination segment
.clus:
    mov ax, [dskw_cur]
    call dskw_take1             ; CX = whole sectors, [dskw_clba] the cursor -
    jc .corrupt                 ; and CF=1 for a cluster outside [2,
                                ; dsk_maxclus], dsk_clus2lba_x being the first
                                ; thing it asks: a dskw_clok here as well was
                                ; the same test twice a cluster
.addrun:
    call dskw_runadd            ; the whole sectors of this take, appended to
    jc .ioerr                   ; the pending run (SPEC.md 18.4.2). CX comes
    jcxz .tail                  ; back non-zero when the run cap said 0, and
                                ; THAT FALLS THROUGH TO .stg, exactly as the
                                ; write path's does (SPEC.md 18.4.2.1)
.stg:                           ; one sector the 8237 cannot carry, alone,
    push cx                     ; through the scratch - which is 512-aligned
    mov cx, 512                 ; and therefore never straddles. No run can be
    call dskw_rone              ; pending here: dskw_runmax answered 0, and it
    pop cx                      ; is only asked with [dskw_rlen] = 0 - and all
    jc .jioerr                  ; 512 of it goes up to the caller
    DSKW_CLBA_INC
    add word [dskw_wseg], 32    ; 512 bytes = 32 paragraphs on
    loop .addrun
    jmp short .tail
.corrupt:                       ; (dskw_rbody and dskw_wabody answer here too)
    mov ax, FERR_IO             ; any link outside [2, maxclus] while bytes
    stc                         ; remain is corruption (SPEC.md 18)
    ret
.ioerr:                         ; ...and the error arms THEMSELVES, below the
.jioerr:                        ; loop's own jmp where nothing falls into them
    jmp dskw_ioerr              ; and every arm above and below reaches them
                                ; short: the write path's shape (dskw_wdata),
                                ; with no relay and no jump over them at entry

    ; the partial final sector: only its live bytes reach the caller. The run
    ; goes first - its bytes come BEFORE these, and flushing it is also what
    ; advances [dskw_wseg] to where the tail's destination actually is.
.tail:
    call dskw_last_p
    jnz .advance
    call dskw_flushrun
    jc .ioerr
    mov cx, [dskw_tail]         ; the sector the whole-sector walk stopped at,
    call dskw_rone              ; whole, into the scratch - and only its live
    jc .jioerr                  ; bytes to the caller
.advance:
    mov ax, [dskw_take]
    sub [dskw_rem], ax
    jz .rdone
    mov ax, [dskw_cur]          ; follow the chain
    call dsk_next_clus_x
    jc .corrupt
    mov [dskw_cur], ax
    jmp .clus                   ; (near: the loop body outruns a short jump)
.rdone:
    call dskw_flushrun            ; the last run of the file
    jc .jioerr
    ret                         ; CF = 0 from the `jc`

; -----------------------------------------------------------------------------
; dskw_czexp - expand the compressed file dskw_rdata just delivered
; out: CF=0 done; CF=1 the stream was refused
; clobbers: AX BX CX DX SI DI ES (DS is restored)
;
; The 'CZ' header is checked HERE and not from the directory hint, and that is
; the whole safety argument for the hint: a foreign tool may drop those four
; bytes or leave them stale, so what the kernel BELIEVES comes out of the file
; - the magic and the format. The length it is handed is the HINT's, because
; that is what sized the destination; a header disagreeing with it is refused
; by the decoder itself, which must produce exactly that many bytes.
; -----------------------------------------------------------------------------
dskw_czexp:
    push ds
    mov dx, [dskw_uz]           ; every kernel word FIRST, while DS reaches
    mov bx, [dskw_uzhi]         ; them: BX:DX = the 32-bit expected output...
    mov cx, [dskw_len]
    mov ax, [dskw_lenhi]        ; ...AH:CX = the stream, past the header, 24
    sub cx, DSK_CZ_HDR          ; bits (SPEC.md 20.14.5.1). P is under the
    sbb al, 0                   ; hint's U, so the top byte is all there is -
    jc .bad                     ; and a file shorter than the header borrows
    mov ah, al                  ; out of it and is refused here...
    mov es, [dskw_czseg]        ; ...ES:0 = where it expands: source and
    xor di, di                  ; destination are one buffer, the packed bytes
                                ; R paragraphs up and the expansion walking
                                ; down onto them...
    lds si, [dskw_src]          ; ...and DS:SI = the packed bytes, where
                                ; dskw_rdata put them - one load, the two
                                ; words being adjacent for it
    cmp word [si], DSK_CZ_MAG   ; the file's own word, not the hint's
    jne .bad
    mov al, [si+2]              ; the format, which the decoder checks
    add si, DSK_CZ_HDR
    call lz_decomp_big          ; ...AH being the length's top byte
    pop ds
    ret
.bad:
    pop ds
    stc
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
; dskw_runmax - how many sectors one int 13h may carry from here
; out: AX = 0..127; 0 = not even one (this sector straddles a 64KB DMA page)
; clobbers: AX (the output), flags
;
; THE 8237 HAS NO REGISTER FOR ADDRESS BITS 16..19: the page port holds them
; and the chip never carries into them, so a transfer that crosses a page
; wraps to the start of that page and moves the wrong memory - int 13h answers
; 09h (SPEC.md 18.1, and guard 6 in kernel.asm).
;
; The kernel keeps every base of its OWN making 512-aligned, which makes that
; unreachable for them - 64KB is a whole number of sectors, so a sector that
; starts 512-aligned always ends inside its own page. Since SPEC.md 18.4.1 the
; buffer is the CALLER's, at any offset it likes, and this is what makes that
; promise true rather than nearly true. It was already nearly true and already
; wrong: an app reading into its own bss straddled whenever its region
; happened to land near a page edge, and the symptom was a "Disk error" that
; moved when anything else in the tree changed size.
;
; This is that question asked of a RUN rather than of a sector, which is what
; lets the write path issue one transfer per cluster instead of one per
; sector. That was free when dsk_xfer looped int 13h itself; it stopped being
; free when a driver-backed volume started handing the whole count to DSV_BLK
; in one call (SPEC.md 18.7), where a run is one IDE command instead of N.
;
; TWO bounds, and both are real:
;   - the run must end inside the 64KB page it starts in. The test is ONE ADD:
;     the low 16 bits of seg*16 + off ARE the offset within the page, because
;     the carry out of that add is the page number.
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
    cmc                         ; meaning a whole page rather than none:
    rcr ax, 1                   ; dsk_runcap's 17-bit negation, CF being the
    mov cl, 8                   ; 65,536s bit, so AX = whole sectors left in
    shr ax, cl                  ; the page, 0..128
    cmp al, 128                 ; ...and 128 only from a page-aligned base,
    jb .out                     ; which the 127 bound below takes one off
    dec ax
.out:
    pop cx
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
    push cx                     ; the tail, banked across the shift, so the
    mov cx, 9                   ; test below is a jcxz and not a re-read
.shr:                           ; sectors = count >> 9 (32-bit shift)...
    shr dx, 1
    rcr ax, 1
    loop .shr
    pop cx
    jcxz .whole
    add ax, 1                   ; ...+ 1 for the partial sector
    adc dx, 0
.whole:
    or dx, dx                   ; (`or` clears CF: the answer below)
    jnz .over
    mov [dskw_rem], ax
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
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: delete's
    mov ax, FSV_DELETE
%endif                          ; OS88_REDIR
    call dskw_gate              ; returns only with dskw_n83 filled
dskw_dbody_n83:                 ; dskw_n83 already filled (dskw_rmtree)
    call dskw_find
    jc .out
    test byte [dskw_raw+DSK_R_ATTR], DSKW_PROT
    jz .live
    jmp dskw_wbody.prot         ; read-only, hidden, system, label or dir:
.out:                           ; FERR_PROT
    ret
.live:
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_oldclus], ax
    jmp dskw_rmbody_n83.delent  ; the deleted mark (SPEC.md 19's species),
                                ; the commit, and the chain it let go of -
                                ; rmdir's own ending, byte for byte

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
    call dskw_mounted           ; returns only when there is a volume
%ifdef OS88_REDIR               ; SPEC.md 62.9.2.3: rename's
    cmp byte [dsk_vkind], DVK_FILE
    jne .fatn
    mov bp, FSV_RENAME          ; SI = the old name, DI = the new one, which
    jmp dskw_fsop               ; is this cell's own argument order already
%endif                          ; OS88_REDIR
.fatn:
    push si
    mov si, di                  ; the new name first: parse it, then prove
    mov di, dskw_n83            ; it is free
    call dskw_name83
    pop si
    jnc .named
.badarg:                        ; (dskw_rtbody and dskw_wabody answer here too)
    mov ax, FERR_NAME
    stc
    ret
.named:
    push si
    mov si, dskw_n83            ; keep it: dskw_find works out of dskw_n83,
    mov di, dskw_n83b           ; which the old-name lookup is about to reuse
    call dskw_copy11
    pop si
    call dskw_find
    jc .vacant                  ; the new name is free (or the scan failed)
    jmp dskw_mkbody.exists
.vacant:
    cmp ax, FERR_NOENT
    je .old
.out:
    stc                         ; FERR_IO from the scan
    ret
.old:
    call dskw_gate83            ; now the old name (nothing is pushed here)
    call dskw_find
    jc .out
    test byte [dskw_raw+DSK_R_ATTR], DSKW_NPROT
    jz .patch                   ; 0x0F, not 0x1F: a FOLDER may be renamed
    jmp dskw_wbody.prot
.patch:
    push si
    mov si, dskw_n83b
    mov di, dskw_raw            ; patch the 11 name bytes in the staged copy
    call dskw_copy11
    pop si
    and word [dskw_oldclus], 0  ; the entry store, then dskw_relold's sync -
    jmp dskw_rmbody_n83.entrel  ; with no chain to free (dskw_clok refuses 0)

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
; WHEN that rebuild happens is never "now" any more (SPEC.md 38.2: the
; Standard File dialog was the one thing ON SCREEN drawn from the global
; listing, and it is a Disk window now, painting from its own cache) - it is
; always the debt (SPEC.md 18.4 rule 3). The invariant above is about what
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
%ifdef DSK_STREAM
    add word [dsk_mgen], 2      ; a write happened: every READ_SEQ cursor and
                                ; WRITE_SEQ token on the machine goes stale
                                ; (SPEC.md 18.4.8, 18.4.9) -
                                ; here, because the remount below may be
                                ; DEFERRED and the cursor must not wait for it
%endif
    call fmv_mark               ; every Disk window showing THIS folder now
                                ; owes a refresh (SPEC.md 22.8) - marked
                                ; ahead of the batch test, because the folder
                                ; changed whether or not the remount is
                                ; deferred, and a tree copy walks folders
                                ; the single remount at its end has left
    cmp byte [dskw_batch], 0    ; a tree copy remounts ONCE, when it ends -
    jne .defer                  ; not after every one of its files (22.3)
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
    call disk_mount_x
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
    cmp byte [dsk_vkind], DVK_FILE
    jne .fat

    ; --- a REDIRECTED volume: ASK (SPEC.md 62.9.1) --------------------------
    ; The 8.3 name is handed over VERBATIM rather than through dskw_name83:
    ; that routine's job is to build the 11-byte padded FAT field a directory
    ; entry is compared against, and there is no directory here to compare it
    ; to. The driver gets the caller's string and answers about it.
    push bp
    mov bp, FSV_STAT
    call drv_fs_call              ; -> AX = the handle, DX:CX = size, BL =
    pop bp                          ; a FAT-style attribute byte
    jc .out                     ; CF = 1 and the driver's AX, as they arrive
    xchg ax, bx                 ; ...and this cell answers BX = the "first
                                ; cluster" and AL = the attributes, so ONE
                                ; instruction puts each where the contract
                                ; says: AX takes the attribute byte in AL,
                                ; BX takes the handle. DX:CX is already the
                                ; size. `mov bx, ax` first would have eaten
                                ; the attribute byte, BL being where it is
    jmp short .out              ; (CF = 0 from the `jc`; xchg writes no flag)
.fat:
    mov di, dskw_n83
    call dskw_name83
    jc .badname
    call dskw_find              ; ...which now leaves the 32 raw bytes in
    jc .fail                    ; dskw_raw itself (AX = FERR_* on failure)
    mov bx, [dskw_raw+DSK_R_CLUS]
    mov cx, [dskw_raw+DSK_R_SIZE]
    mov dx, [dskw_raw+DSK_R_SIZE+2]
    mov al, [dskw_raw+DSK_R_ATTR]
    jmp short .out              ; (CF = 0 from the `jc .fail`)
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
; and the caller is spending ~24 ms per 512 bytes (PERFORMANCE.md Part 2,
; Set 24; it was 65 at Set 17 and 238 under the AL bug) on the data this is
; handing it. A 116KB file in 32KB chunks is four walks averaging ~116
; clusters - a few hundred word reads against ~5.6 seconds of floppy.
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

    cmp byte [dsk_vkind], DVK_FILE
    jne .fatat
    jmp .fsat                   ; a REDIRECTED volume (SPEC.md 62.9): the
                                ; driver serves any offset, so BOTH of this
                                ; routine's cluster-alignment preconditions
                                ; simply do not arise
.fatat:
    call dskw_clbytes           ; AX = bytes per cluster on this volume
    mov [dwr_clb], ax
    test ax, ax
    jz .badarg                  ; an unmounted volume has no cluster size

%ifdef DSK_STREAM
    xor di, di                  ; DI = 0 (it is pushed above, and nothing
    call dwr_stat               ; below reads it but the walk): BX = first
                                ; cluster, DX:CX = the size - or a READ_SEQ
                                ; cursor's, and then DI = 1 (18.4.8)
%else
    call dskw_stat_x            ; BX = first cluster, DX:CX = the size
%endif
    jc .err
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
%ifdef DSK_STREAM
    or di, di                   ; ...ONE, from a HOT READ_SEQ cursor's
    jz .walk                    ; cluster, which is the last one the previous
    mov cx, di                  ; call read (SPEC.md 18.4.8) - so its FAT link
.walk:                          ; is this walk's, checked as every link is
%endif
    mov ax, bx                  ; the walk's start, the stat's BX, which
    jcxz .atclus                ; nothing above has touched
.skip:
    call dsk_next_clus_x    ; AX = the next cluster; CF=1 = the
    jc .corrupt                 ; chain ended before the size said it would
    loop .skip                  ; ...and a link outside [2, dsk_maxclus] is
                                ; refused by whatever reads it next: the next
                                ; pass's dsk_next_clus_x, or dsk_read_chain_x's
                                ; own range test (FERR_IO either way). It was
                                ; asked here as well, a dskw_clok a cluster
.atclus:
    push ax                     ; THE CLUSTER IS NEVER STORED. It was banked
                                ; in [dwr_clus], which on kern_big IS the
                                ; READ_SEQ cursor's +6 - so a read that FAILED
                                ; handed the caller a cursor whose +6 was
                                ; already the cluster at +12, under a
                                ; generation that still matched, and the
                                ; retry walked one link too far and delivered
                                ; the wrong cluster with CF=0. +6 is the
                                ; wrapper's to write, on success (18.4.8)

    ; --- and read it ---------------------------------------------------------
    ; dsk_read_chain moves whole SECTORS, so a tail shorter than one is
    ; rounded up. That cannot overrun the caller: the capacity is a cluster
    ; multiple, the take is at most the capacity, and a cluster is a whole
    ; number of sectors.
    mov ax, [dwr_take]
    call dsk_bytes_secs_x       ; DX = sectors
    pop ax
    mov bx, [dwr_buf]
    call dsk_read_chain_x
    jc .ioerr
    mov ax, [dwr_take]
    xor dx, dx                  ; (`xor` clears CF: the answer)
    jmp short .out
.eof:
    xor ax, ax
    xor dx, dx
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

    ; --- a REDIRECTED volume: STAT to a handle, then READAT (62.9.1) --------
    ; dskw_stat_x is redirected too, so this could have fallen through to the
    ; code above and got the right handle and size out of it - and then walked
    ; a cluster chain that does not exist. The branch is here rather than
    ; after the stat because everything between them is FAT arithmetic.
.fsat:
    mov bp, FSV_STAT
    call drv_fs_call              ; SI = the name -> AX = the handle,
    jc .fsbad                   ; DX:CX = size, BL = attributes
    test bl, 0x18
    jnz .fsprot                 ; a folder is not readable, as everywhere else
    mov bx, [dwr_buf]           ; AX is still the handle
    mov dx, es                  ; DX:BX = the buffer, ES being the caller's
    mov cx, [dwr_cap]           ; and DX not carrying it into the driver
    mov si, [dwr_off]           ; DI:SI = the 32-bit offset, and a driver is
    mov di, [dwr_off+2]         ; free to answer 0 for "at or past the end"
    mov bp, FSV_READAT
    call drv_fs_call              ; -> DX:AX = the bytes delivered
    jmp short .out              ; CF and DX:AX are already the answer
.fsprot:
    mov ax, FERR_PROT
.fsbad:
    stc
    jmp short .out

;
; -----------------------------------------------------------------------------
; dskw_read_seq - a streaming read, whose place the CALLER keeps (SPEC.md
;                 18.4.8, slot OSAPI_FILE_READ_SEQ; kern_big)
; in:  SI -> a NUL 8.3 name, DX:BX -> the buffer, CX = its capacity (a
;      cluster multiple), ES:DI -> the 16-byte cursor (zeroed, +12 = where to
;      start), in any segment
; out: CF=0 with DX:AX = the bytes delivered (0 = at or past the end) and the
;      cursor advanced past them; CF=1 with AX = FERR_*, the cursor unmoved
; clobbers: AX, DX (the outputs), CF
;
; READ_AT without the walk. The cursor caches what READ_AT recomputes every
; call - the stat by name and the chain walk to the offset, 142 ms a MB on the
; W0 machine - and is TRUSTED ONLY UNDER THE MOUNT GENERATION it was seeded
; in: every snapshot the kernel builds bumps [dsk_mgen] in disk_mount_x, so a
; navigation, a media change, a write's remount or another instance moving
; the volume all make the next call re-seed from the name, silently, at the
; offset the cursor holds. The token is the caller's, so the copy loop's
; writes that §18.4.4 kept statelessness for cannot destroy it - they only
; cost one re-walk.
; -----------------------------------------------------------------------------
%ifdef DSK_STREAM
RSQ_GEN     equ 0               ; the mount generation it was seeded under
RSQ_VOL     equ 2               ; the volume
RSQ_CUR     equ 6               ; the cluster holding FSEQ_OFF
RSQ_SIZE    equ 8               ; the file's size
FSEQ_OFF    equ 12              ; the offset: the caller's to set (SDK)
FSEQ_SIZE   equ 16
%if FSEQ_SIZE != 16
  %error "disk.inc seeds [dwr_c] as 16 bytes: FSEQ_SIZE is its size"
%endif

; IT IS READ_AT, with the cursor standing in for READ_AT's two per-call costs:
; [dwr_seq] makes dskw_read_at's stat come from dwr_stat below, which answers
; out of a cursor still valid and marks the walk to be skipped, and this
; wrapper does the bookkeeping READ_AT has no words for - the size, the
; generation, and the cluster one FAT link past what was read.
;
; IT IS ITS OWN FAR ENTRY (SPEC.md 2.6): nothing near-calls it, so the cell
; reaches it by name and it ends in `retf` - the `call`/`retf` pair a separate
; dwf_ stub would add is four bytes that buy nothing.
dwf_dskw_read_seq:
    push bx
    push cx
    push si
    push di
    push es                     ; ...the caller's ES:DI, which is ALSO where
                                ; the cursor goes back to at .out
    push si                     ; THE CURSOR IS COPIED IN AND OUT, so it can
    push cx                     ; live anywhere the caller likes and the buffer
    push ds                     ; can be a segment of its own - a player's ring
    push es                     ; reads every chunk into a different one
    pop ds                      ; (SPEC.md 18.4.8). DS = the cursor's segment,
    pop es                      ; ES = ours; SI is the NAME, which READ_AT
    mov si, di                  ; still needs, and CX the capacity
    mov di, dwr_c
    mov cx, FSEQ_SIZE / 2
    cld
    rep movsw
    push es
    pop ds                      ; DS = KERNEL_SEG again
    pop cx
    pop si
    mov es, dx                  ; ES:BX = the caller's DX:BX buffer
    mov ax, [dwr_c+FSEQ_OFF]
    mov dx, [dwr_c+FSEQ_OFF+2]
    call dskw_read_at_x         ; DX:AX = the bytes, or CF and FERR_*
    jc .out
    add [dwr_c+FSEQ_OFF], ax    ; the cursor moves only when bytes arrived
    adc [dwr_c+FSEQ_OFF+2], dx
    cmp byte [dsk_vkind], DVK_FILE
    je .out                     ; a REDIRECTED volume (SPEC.md 62.9) serves
                                ; any offset: its cursor is only that. CF=0,
                                ; the compare having been equal
    push ax                     ; ...otherwise it is trusted under THIS
    mov ax, [dsk_chain_end]     ; snapshot, on this volume: the LAST cluster
    mov [dwr_c+RSQ_CUR], ax     ; read, whose one FAT link is the next call's
    mov ax, [dsk_mgen]          ; whole walk (READ_AT's own, above). The size
    mov [dwr_c+RSQ_GEN], ax     ; is already in the cursor: [dwr_sz] IS its
    mov al, [disk_drive]        ; field on kern_big, stored by READ_AT from
    mov [dwr_c+RSQ_VOL], al     ; the stat, whichever way it answered. After a
    pop ax                      ; call that delivered nothing the cluster is
    clc                         ; stale and unread: READ_AT answers the end
.out:                           ; before it walks
    pop es                      ; ...and the cursor goes back where it came
    pop di                      ; from, success or not - to the caller's own
    push di                     ; ES:DI, banked at the door. No flag is
    mov si, dwr_c               ; written from here to the retf, so CF is
    mov cx, FSEQ_SIZE / 2       ; still the answer
    cld
    rep movsw
    mov [dwr_c+RSQ_GEN], cx     ; ...and the KERNEL's copy is left invalid
    pop di                      ; (CX = 0 after the rep): dwr_stat consults
    pop si                      ; it on EVERY READ_AT, and a plain one must
    pop cx                      ; find no cursor there
    pop bx
    retf

; dwr_stat - dskw_read_at's stat: dskw_stat_x's contract (BX = the walk's
; start, DX:CX = the size), from the READ_SEQ cursor's kernel copy while it is
; valid (DI 0 -> 1 then says the walk is ONE link, from the last cluster the
; previous call read), from the name otherwise. A plain READ_AT asks too,
; and finds [dwr_c]'s generation 0, which [dsk_mgen] never is: the wrapper
; leaves it so after every call, and it is seeded so in .text
dwr_stat:
    mov ax, [dsk_mgen]          ; STILL THE SNAPSHOT IT WAS SEEDED IN, on the
    cmp ax, [dwr_c+RSQ_GEN]     ; same volume? then its cluster and size mean
    jne .name                   ; what they meant
    mov al, [disk_drive]
    cmp al, [dwr_c+RSQ_VOL]
    jne .name
    inc di                      ; hot: DI = 1, one link
    mov bx, [dwr_c+RSQ_CUR]
    mov cx, [dwr_c+RSQ_SIZE]
    mov dx, [dwr_c+RSQ_SIZE+2]
    ret                         ; CF=0: the cmp above was equal
.name:
    jmp dskw_stat_x
%endif

; -----------------------------------------------------------------------------
; dskw_write_seq - a streaming append, whose place the KERNEL keeps and the
;                  caller holds a TOKEN for (SPEC.md 18.4.9, slot
;                  OSAPI_FILE_WRITE_SEQ; kern_big)
; in:  SI -> a NUL 8.3 name, ES:BX -> the bytes, CX = the count (a cluster
;      multiple, but the last), AL = flags (bit 0 = HELD), DI = the token the
;      last call handed back (0, or anything stale, = none); CX = 0 CLOSES
; out: CF=0, AX = 0, DI = the token for the next call; CF=1 with AX = FERR_*
;      and DI as it was. A close touches neither DI nor anything but AX
; clobbers: AX, DI (the outputs), CF
;
; APPEND without the name lookup and without the walk. There is ONE stream
; record, the kernel's (dws_rec): the entry as the lookup found it, where it
; lives, the file's last cluster and its volume, written by every successful
; call. A token is the [dsk_mgen] that call left, and it is the record's
; only key: every WRITE and every mount that re-reads a boot sector moves
; [dsk_mgen] on, and so does the first call of a NEW hold (the one kind of
; call that moves nothing else), so a token equal to it names the stream
; the record describes and no other. A banked hop inside a batch (18.9.3)
; moves nothing, so the join's C: -> A: -> C: every block keeps it; the
; volume is checked as well, the token being the same on either side.
;
; HELD (AL bit 0) writes the data and grows an UNREACHABLE chain - never
; linked to the file, so a FAT flush at any moment puts nothing on the disk a
; crash could make wrong - and the ONE link, the FAT flush and the entry wait
; for dws_commit: at a close, before any other write on the volume, before a
; mount leaves it outside a batch, at a cold call, and at gfx_unlock, which
; is every way the user can reach the drive (18.9.3). The flag is the
; STREAM's: a caller keeps one value for a file's whole life.
;
; THERE IS NO SECOND BODY. This door decides HOT or COLD and dskw_wabody, the
; one append/write-at body, reads [dws_flg]: a cold call is an APPEND, a hot
; one loads the record in place of the lookup and the walk, and HELD changes
; only what .grow does once the data is down. So everything an append
; refuses, WRITE_SEQ refuses the same way, and a redirected volume (SPEC.md
; 62.9) takes it as the append it is.
; -----------------------------------------------------------------------------
%ifdef DSKW_WSEQ
WSQF_HELD   equ 0x01            ; the caller's AL...
WSQF_SYS    equ 0x02            ; ...and APPEND_SYS's permission (18.4.4):
                                ; a hidden + system file may be grown
WSQF_HOT    equ 0x40            ; the door's: the token names the record
WSQF_IN     equ 0x80            ; ...and inside a call at all
DWS_REC0    equ dskw_raw + DSK_R_CLUS   ; the record's run: the entry's head
DWS_RECW    equ 8               ; and size, dskw_dsec, dskw_doff, dskw_prev,
                                ; dskw_first - ADJACENT for it, and asserted
                                ; where declared

%ifndef KD_BUILD
; dwsf_dskw_write_seq - THE CELL's way in (slot 0x0461), and the kernel's
; writers call the door below it. WSEQF_SYS is APPEND_SYS's permission, so it
; takes APPEND_SYS's FENCE (api_file_sysc, SPEC.md 19.6.1): drv_owns_seg on
; the caller's DS, which api_rn banked under the far return api_far built -
; BP, BX, IP, CS, the caller's SI, the caller's DS. A close reads no flag
dwsf_dskw_write_seq:
    jcxz dwf_dskw_write_seq
    test al, WSQF_SYS
    jz dwf_dskw_write_seq
    push bx
    push bp
    mov bp, sp
    mov bx, [bp+10]             ; the caller's DS (SS: the stack's own)
    call drv_owns_seg_x         ; CF = 0: a loaded driver's segment
    pop bp
    pop bx
    jnc dwf_dskw_write_seq
    mov ax, FERR_PROT           ; a package: protected, DI as it was
    retf
%endif
dwf_dskw_write_seq:
    and al, WSQF_HELD | WSQF_SYS    ; the CALLER's bits: WSQF_HOT is the door's
    or al, WSQF_IN
    mov [dws_flg], al
    jcxz .cold                  ; a CLOSE: the commit, and nothing more
    cmp di, [dsk_mgen]          ; the token still names the record...
    jne .cold
    call dws_ours.vol           ; ...on this volume
    jne .cold
    or byte [dws_flg], WSQF_HOT
    jmp short .go
.cold:
    call dws_commit             ; a hold ends before a file is looked up
    jc .out                     ; again: its size IS the lookup's answer - and
    jcxz .out                   ; a close's answer is the commit's (AX = 0
.go:                            ; when nothing was held)
    mov al, [dws_flg]           ; WSQF_SYS is [dskw_syswr], which dskw_wrp
    and al, WSQF_SYS            ; clears on the way out - set on EVERY call,
    mov [dskw_syswr], al        ; so none can leak into the next write
    call dskw_append_x
    jc .out                     ; DI as it was: the record is untouched too
    mov di, [dsk_mgen]          ; THE TOKEN: what the call left (`mov`: CF
.out:                           ; stays the answer)
    mov byte [dws_flg], 0       ; `mov`: CF stays the answer
    retf

; dws_load - the record back into what the lookup would have answered: the
; entry's head and size, [dskw_dsec], [dskw_doff] - the rest of dskw_raw is
; NOT the entry, which is why a grow stores only its tail (dskw_ent_tail).
; clobbers: CX, SI, DI, ES - dskw_wrp's banked registers inside the body, and
; dws_commit's own
dws_load:
    mov si, dws_rec
    mov di, DWS_REC0
.mv:
    push ds
    pop es
    mov cx, DWS_RECW
    cld
    rep movsw
    ret

; -----------------------------------------------------------------------------
; dws_commit - end the pending HELD stream: its chain durable, its ONE link,
;              its size (SPEC.md 18.4.9)
; in:  nothing; out: CF=0 (or nothing held); CF=1 with AX = FERR_*
; clobbers: AX, flags - everything else, [disk_drive] and [dsk_cwd] included,
;           as it was: a commit that had to hop to the held volume hops back
;
; It is NOT a second link-and-store: it loads the record and enters the
; append body at .seal, the same link, flush and entry store an append makes
; once its data is down (18.4.7.6's single flush included), once per stream
; instead of once per call. The hold is cleared FIRST, so a failure is not
; retried at every gate and the remount dskw_sync may run cannot re-enter.
; -----------------------------------------------------------------------------
dws_commit:
    xor ax, ax
    xchg al, [dws_hold]
    or al, al
    jz .none                    ; nothing held: AX = 0, CF = 0
    add word [dsk_mgen], 2      ; ANY end of a hold stales every token
                                ; (dws_hlose's rule): a FAILED commit leaves
                                ; the record's size past the linked chain
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [disk_drive]
    push ax                     ; where we stood, to hop back to
    push word [dsk_cwd]
    mov dl, [dws_vol]
    cmp dl, al
    je .here
    xor ax, ax                  ; the volume's ROOT: the entry is found by
    call dsk_chdir_q_x          ; its sector, so any folder of the volume will
    jc .back                    ; do, and inside the batch the hop is banked
.here:
    call dws_load               ; the entry, and [dskw_first] the held head
    mov ax, [dws_hlink]         ; the committed last cluster, 0 = empty...
    inc ax                      ; ...or 0FFFFh, POISONED (dws_hlose) - before
    jz .lost                    ; the hop or BY it: the chain never reached
    dec ax                      ; the disk, so there is only a stream to
    call dskw_wabody.seal       ; report lost
    jmp short .back
.lost:
    mov ax, FERR_IO
    stc
.back:
    pop dx                      ; [dsk_cwd] as it was
    pop bx                      ; BL = the volume
    pushf
    push ax
    cmp bl, [disk_drive]
    je .home
    xchg ax, dx
    mov dl, bl
    call dsk_chdir_q_x          ; ...back where the caller stood
.home:
    pop ax
    popf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
.none:
    ret

; dws_ours - ZF=1: a hold is pending on [disk_drive] and no WRITE_SEQ call is
; running (the word is [dws_hold] = 1 with [dws_flg] = 0 beside it); .vol
; asks only whether [disk_drive] is the record's volume. clobbers: AL, flags
dws_ours:
    cmp word [dws_hold], 1
    jne .no
.vol:
    mov al, [disk_drive]
    cmp al, [dws_vol]
.no:
    ret

; -----------------------------------------------------------------------------
; THE HELD VOLUME'S DIRTY FAT, BANKED ACROSS A HOP (SPEC.md 18.8.5)
;
; The FAT windows are per volume (18.8.1-18.8.4) but the dirty range is one
; pair, so dsk_fatw_park used to WRITE the outgoing volume's dirt - a seek to
; the FAT and back per hop. Every writer but a held stream has flushed by the
; time it returns, so the held volume's is the only dirt a park can meet, and
; one bank holds it: park keeps it with the window, pick gives it back when
; the window is reused. What must not happen is the WINDOW going while its
; dirt is banked, and there are four ways, each answered here:
;   - a SHED takes the claim, or the PIN is evicted from under it: both
;     leave the volume's banked sector 0xFFFF (dsk_fatw_demote,
;     dsk_fatw_evict), so the way back to it is a LOAD, and dws_hload
;     loses the hold there - before anything could be linked into it,
;     because the one thing that links, dws_commit, goes back first;
;   - the volume is RE-READ: the same disk keeps the window and its dirt, a
;     different one loses the hold (dws_hload);
;   - a HIBERNATE zeroes the range: it runs from a menu, and gfx_unlock has
;     committed every hold before any menu can be reached.
; LOSING the hold POISONS it: the stream is abandoned to its last commit,
; and because its dirt never reached the disk, nothing links to it. The
; token goes stale with it, and the cold call that follows meets the poison
; in dws_commit and answers FERR_IO rather than appending past a hole.
; -----------------------------------------------------------------------------

; dws_hpark - CF=0 the dirty range was the held volume's and is banked (the
; live pair cleared); CF=1 = flush it as ever. in: [disk_drive] = the volume
; being left, DL = the one being mounted. clobbers: AX, flags
;
; ONLY WHERE THIS HOP CANNOT TAKE THE WINDOW AWAY. A mounted volume always has
; a window (18.8.3's "no third state"), but when it is the PIN, the incoming
; volume may evict it - and on a machine the heap refused, two volumes do
; that at EVERY hop. So it banks when the incoming volume already has a claim
; of its own, which cannot take anybody's window; otherwise the flush is
; today's, and slower and never lost. (A held volume with a claim of its own
; would be safe to bank either way; it is one test cheaper not to ask, and
; the join's target is the pin in the case that matters - the system disk.)
dws_hpark:
    call dws_ours
    jne .no
    push bx                     ; the incoming volume has a claim of its own
    mov bl, dl                  ; to go to, so it cannot take the held one's
    call dsk_fatw_slot          ; window whichever it is: the pin or a claim
    cmp word [bx+DVOL_MAX*2], 0 ; (never the pin, which only one volume holds
    pop bx                      ; - and if that is the held one, the pin is
    jnz dws_hswap               ; exactly what must not be taken)
.no:
    stc
    ret

; dws_hload - dsk_fatw_pick is about to RE-READ the window of [disk_drive].
; The held volume's, with dirt banked: the hold is LOST. Inside the batch a
; held stream lives in, a hop is a QUIET reuse, so this is a full mount or a
; different disk, and neither can vouch for the banked bytes. clobbers: AX
dws_hload:
    call dws_hmine
    je dws_hlose
    ret

; dws_hret - the window of [disk_drive] is live again: the held volume's
; takes its banked dirt back. out: CF=0. clobbers: AX
dws_hret:
    call dws_hmine
    jne dws_hswap.out
; dws_hswap - exchange the live dirty pair with the bank: EMPTY (0xFFFF, 0)
; is what each side is left holding, so one routine banks and restores.
; out: CF=0. clobbers: AX
dws_hswap:
    mov ax, [dsk_fatd0]
    xchg ax, [dws_hd0]
    mov [dsk_fatd0], ax
    mov ax, [dsk_fatd1]
    xchg ax, [dws_hd1]
    mov [dsk_fatd1], ax
.out:
    clc
    ret

; dws_hmine - ZF=1 when [disk_drive] is the held volume and dirt is banked
; for it. clobbers: AX, flags
dws_hmine:
    cmp word [dws_hd0], 0xFFFF
    jne dws_ours.vol

    or al, 1                    ; ZF=0 (AL is anything but zero after it)
    ret

; dws_hlose - the held stream is POISONED: its banked dirt goes with its
; window, and every token goes stale. The hold stays pending, so the first
; commit to meet it - a cold call, a close, the unlock - is the one that
; answers FERR_IO and clears it; nothing it names ever reached the disk, so
; there is nothing to undo. clobbers: flags
dws_hlose:
    or word [dws_hlink], -1     ; no cluster is 0xFFFF: the poison
    or word [dws_hd0], -1       ; ...and the bank EMPTY, both halves: a stale
    and word [dws_hd1], 0       ; top would be swapped into the live range at
                                ; the next park and widen it past the window
    add word [dsk_mgen], 2
    ret
%endif                          ; DSKW_WSEQ

; -----------------------------------------------------------------------------
; dskw_write_at - write CX bytes at a byte offset: INSIDE what the file has
;                 allocated, or exactly AT its end, in which case it grows
;                 (SPEC.md 18.4.7)
; in:  SI -> a NUL 8.3 name, ES:BX -> the bytes, CX = count (>= 1),
;      DX:AX = the byte offset (a multiple of the cluster)
; out: CF=0 with AX=0; CF=1 with AX = FERR_*
; clobbers: AX (the output), CF
;
; dskw_append - add CX bytes to the END of a file (SPEC.md 18.4.4, slot 0x0286)
; in:  SI -> a NUL 8.3 name, ES:BX -> the bytes, CX = count (>= 1)
; out/clobbers: dskw_write_at's
;
; ONE BODY, TWO DOORS, and an append is a write-at whose offset is the file's
; size: the append door enters with the offset's high word 0xFFFF, which no
; real offset can carry (a volume caps at 32MB), and the body substitutes the
; size once it has the entry in hand. That is what turns the append's own
; precondition - the size must be a whole number of clusters (SPEC.md 18.4.4)
; - into the SAME test the write-at makes on any offset: it must land on a
; cluster boundary. Two bodies used to make that test two ways.
;
; The offset decides which of two exclusive things happens, and the DOS box's
; window (SPEC.md 96.11.6) is why there is no third:
;
;   INSIDE  - offset + count fits what the file OWNS (its size rounded up to
;             a cluster). Walk to the offset's cluster and write over what is
;             there; no cluster is allocated, no FAT sector is written, and
;             the only thing touched beyond the data is the size word, UP,
;             when the end passes it (18.4.7.2).
;   AT END  - the offset is exactly the allocated end. Allocate and write the
;             new sub-chain, flush, link it to the last cluster (or make it
;             the head of an empty file), flush again, and only then let the
;             entry take the new size - SPEC.md 18.4's commit order in the
;             only shape an append can have: a crash before the commit leaks
;             clusters, which is the failure the rule exists to prefer.
;
; Anything else - past the allocated end, or straddling it - is FERR_NAME. A
; caller that has a partly-full last cluster to top up before it grows makes
; two calls, which is what the DOS box's window does by construction: its
; last window ends at the size and the first window past it starts at the
; allocated end.
;
; THE TWO GRANULARITIES DIFFER ON PURPOSE. The OFFSET is a cluster multiple
; because that makes the walk exact - offset/cluster clusters to skip and no
; partial-cluster arithmetic - which is dskw_read_at's own rule. The COUNT is
; a SECTOR multiple only while the write is wholly inside the file's size:
; the transfer moves whole sectors, and a count rounded up there would write
; bytes from past the CALLER's buffer over bytes the file owns. At or past
; the size the rounded tail lands on slack, so any count goes (18.4.7.2).
; -----------------------------------------------------------------------------
dskw_append_sys_x:
    mov byte [dskw_syswr], 1    ; ...which dskw_wrp clears on the way out
dskw_append_x:
    push bx
    push cx
    push dx
    mov dx, 0xFFFF              ; "at the END": the body reads the size in
                                ; place of an offset (AX is don't-care)
%ifndef KERN_SMALL                 ; the offset door is kern_big's (18.4.7.4)
    jmp short dskw_wat
dskw_write_at_x:
    push bx
    push cx
    push dx
    mov [dwr_off], ax           ; THE OFFSET FIRST: dskw_wrp spends DX as the
%endif                          ; count's high word, and a count here is one
dskw_wat:                       ; 16-bit number by contract
    mov [dwr_off+2], dx
    xor dx, dx
    mov ax, dskw_wabody
    jmp dskw_wrp

; THE EPILOGUES ARE IN THE MIDDLE, and that is a measured choice rather than
; untidiness: the 8086 has no near `jcc`, so a conditional jump to a label
; past 127 bytes assembles as `jcc short; jmp near` - five bytes for two -
; and this body is 350 of them. With .prot beside the one arm that jumps to
; it and the rest at the half-way mark, every refusal in it is two bytes.
dskw_wabody:
    call dskw_mounted
    jnc .gated
.out:                           ; ...and a `ret` any error can share, AX
    ret                         ; and CF being already the answer
.gated:
    cmp byte [dsk_vkind], DVK_FILE
    jne .fat
    mov ax, [dwr_off+2]         ; a REDIRECTED volume (SPEC.md 62.9) has
    inc ax                      ; FSV_APPEND and no write-at verb to pair
    jnz .prot                   ; with FSV_READAT, so an offset has nothing
                                ; to be handed to
    call ct_fpg_begin               ; CX = the count and DX = 0, both exactly
                                ; as the cell was handed them: arm the widget
                                ; as the FAT arm does, so a file-manager copy
                                ; over a cable reports chunk by chunk too
                                ; (SPEC.md 12.8.1). dskw_wrp ends it
    mov dx, es                  ; DX:BX = the bytes, CX = how many - the same
    mov bp, FSV_APPEND          ; three registers this cell was handed
    jmp dskw_fsop
.prot:
    jmp dskw_wbody.prot
.fat:
    call dskw_gate83
%ifdef DSKW_WSEQ
    test byte [dws_flg], WSQF_HOT   ; --- A HOT TOKEN (SPEC.md 18.4.9) ----
    jz .find                    ; the lookup's three answers, from the record
    call dws_load               ; (the door decided, and committed any hold a
    jmp short .args             ; cold call must not look past)
.find:
%endif
    call dskw_find
    jc .out                     ; AX = FERR_NOENT / FERR_IO already
    call dskw_pmask             ; AL = what may not be written here
    test byte [dskw_raw+DSK_R_ATTR], al
    jnz .prot
.args:
    call dskw_clbytes           ; --- the arguments, all refused ------------
    mov bx, ax                  ; BX = bytes per cluster for the rest of the
    test ax, ax                 ; arithmetic; nothing between here and the
    jz .badarg                  ; walk touches it. 0 = an unmounted volume
    cmp word [dwr_off+2], 0xFFFF
    jne .have                   ; an APPEND: the offset is the size
    mov dx, [dskw_raw+DSK_R_SIZE]
    mov [dwr_off], dx
    mov dx, [dskw_raw+DSK_R_SIZE+2]
    mov [dwr_off+2], dx
.have:
    dec ax                      ; the offset on a cluster boundary. A power of
    and ax, [dwr_off]           ; two, so the modulo is a mask - and the mask
    jnz .badarg                 ; is under 65,536, so only the LOW word of a
                                ; 32-bit offset can carry a remainder
    call dskw_norm              ; the source becomes seg:0..15 + a segment run
    call dskw_size32            ; ...and [dskw_rem]/[dskw_tail] size it. It
                                ; cannot refuse a 16-bit count
    mov byte [dskw_zapnext], 0  ; dskw_ent_store consumes this and nothing
                                ; clears it: a create that took the end
                                ; marker leaves it set, and the next entry
                                ; store would zap the slot after ITS entry
    mov ax, [dskw_len]
%ifdef KERN_SMALL
    test ax, ax
    jz .badarg                  ; nothing to write is a bad argument here, not
                                ; a no-op: the caller has miscomputed
%endif                          ; ...and on kern_big it is THE FILE ENDS HERE
                                ; (SPEC.md 18.4.7.5), decided at .loose below:
                                ; a count of 0 makes END the offset, so the
                                ; compare that follows is the truncate's own
                                ; "not past the size" test, answered in SI

    add ax, [dwr_off]           ; --- END = offset + count, and it DECIDES ---
    mov [dwr_end], ax           ; whether the tail may be rounded and whether
    mov ax, [dwr_off+2]         ; the size moves (18.4.7.2): SI = 1 when it
    adc ax, 0                   ; passes the size
    mov [dwr_end+2], ax
    xor si, si
    cmp ax, [dskw_raw+DSK_R_SIZE+2]
    jb .strict                  ; end < size
    ja .up
    mov ax, [dwr_end]
    cmp ax, [dskw_raw+DSK_R_SIZE]
    ja .up
    je .loose                   ; ...at the size: loose, and it stays
.strict:
    cmp word [dskw_tail], 0     ; WHOLLY INSIDE THE FILE, so the transfer may
    je .loose                   ; not round: the bytes past the count are the
                                ; file's own and the caller did not give them
.badarg:
    jmp dskw_nbody.badarg       ; FERR_NAME, spelled once
.corrupt:
.ioerr:
    jmp dskw_rdata.corrupt      ; the same answer, spelled once
%ifndef KERN_SMALL
    ; --- A COUNT OF ZERO TRUNCATES (SPEC.md 18.4.7.5) ----------------------
    ; The file keeps offset/cluster clusters - the offset being a cluster
    ; multiple, checked above - and takes the offset as its size. The ENTRY is
    ; written first and the FAT after (18.4, dskw_dbody's order): a crash
    ; between the two leaks the freed clusters rather than leaving an entry
    ; that names clusters somebody else may be handed. Only DOWN: a size past
    ; the file's own is refused, the growing door being the one above.
.trunc:
    or si, si                   ; END (the offset) past the size: refused
    jnz .badarg
    mov ax, [dwr_off]
    mov dx, [dwr_off+2]
    cmp dx, bx                  ; a quotient past 16 bits: only an entry
    jae .corrupt                ; claiming more than any volume holds
    mov [dskw_raw+DSK_R_SIZE], ax
    mov [dskw_raw+DSK_R_SIZE+2], dx
    div bx                      ; the clusters kept - one div, for .in's reason
    xchg cx, ax
    mov ax, [dskw_raw+DSK_R_CLUS]
    jcxz .tnone                 ; none: the entry gives up its whole chain
    jmp short .tloop            ; ...else CX - 1 steps, `loop` counting first
.tskip:
    call dsk_next_clus_x        ; to the LAST cluster kept. A chain shorter
    jc .corrupt                 ; than the entry's size says ends in a value
.tloop:                         ; this refuses on the next step
    loop .tskip
.tlast:
    mov si, ax
    call dsk_next_clus_x        ; ...the first one freed, or the end mark
    jc .corrupt
    xchg ax, si
    mov dx, 0xFFFF              ; setfat keeps 12 bits of it on FAT12
    call dskw_setfat            ; the chain ENDS here
    jc .corrupt
    jmp short .tstore
.tnone:
    mov [dskw_raw+DSK_R_CLUS], cx
    mov si, ax                  ; SI = the first cluster freed, both arms
.tstore:
    mov [dskw_oldclus], si      ; ...which dskw_relold frees once the entry
    jmp dskw_rmbody_n83.entrel  ; has landed: THE COMMIT, then the tail a
                                ; delete ends in (an end mark frees nothing)
%endif
.up:
    inc si
.loose:                         ; ...at or past the size, where a rounded tail
                                ; lands on slack the file already owns
%ifndef KERN_SMALL
    cmp word [dskw_len], 0      ; a count of 0 arrives here whatever the
    je .trunc                   ; offset, its [dskw_tail] being 0 too
%endif

    ; --- what the file OWNS: its size rounded up to a cluster, in bytes -----
    ; ((size - 1) | (clb - 1)) + 1: clb is a power of two under 65,536, so
    ; the round-up is one OR on the low word, and an EMPTY file comes out
    ; right by wrapping - 0xFFFFFFFF | mask, plus one, is 0. Comparing in
    ; BYTES rather than clusters is what makes the two questions below one
    ; compare each and needs no cluster count at all.
    mov ax, [dskw_raw+DSK_R_SIZE]
    mov dx, [dskw_raw+DSK_R_SIZE+2]
    sub ax, 1
    sbb dx, 0
    mov cx, bx
    dec cx
    or ax, cx
    add ax, 1
    adc dx, 0                   ; DX:AX = the allocated end
    xor bp, bp                  ; BP = 0: the write is INSIDE what is owned...
    cmp dx, [dwr_end+2]
    ja .in
    jb .at
    cmp ax, [dwr_end]
    jae .in
.at:
    inc bp                      ; ...1: it starts exactly AT the allocated end
    cmp dx, [dwr_off+2]
    jne .jbad                   ; past it, or across it: this is not the
    cmp ax, [dwr_off]           ; allocator, and the DOS box never asks
    jne .jbad
.in:
%ifdef DSKW_WSEQ
    mov ax, [dwr_end]           ; THE NEW SIZE, into the entry now: nothing
    mov [dskw_raw+DSK_R_SIZE], ax   ; below reads the old one, .size stores
    mov ax, [dwr_end+2]         ; it only when it moved, and .stamp keeps
    mov [dskw_raw+DSK_R_SIZE+2], ax ; the entry a HELD call's commit will
                                ; store - so the copy is made once, here
    test byte [dws_flg], WSQF_HOT   ; a hot record KNOWS the last cluster: no
    jz .div                     ; walk at all (BP is 1 - its size is the
    mov ax, [dws_last]          ; allocated end, as an append's always is)
    jmp short .atclus
.div:
%endif
    mov ax, [dwr_off]           ; clusters to skip. One div is enough: the
    mov dx, [dwr_off+2]         ; offset is under 33MB (SPEC.md 18.7), so its
    div bx                      ; high word is under 512, which is at or below
    xchg cx, ax                 ; every cluster size there is
    jcxz .walk                  ; an empty file owns no cluster to stop on -
    sub cx, bp                  ; the entry's head reads 0 below. Otherwise a
                                ; grow stops ON the last cluster, one step
                                ; short of the count, to link to it

    ; --- the walk: CX clusters along the chain from the head ---------------
.walk:
    mov ax, [dskw_raw+DSK_R_CLUS]
    jcxz .atclus
.skip:
    call dsk_next_clus_x
    jc .jcor                    ; the chain ended before the entry's size said
    call dskw_clok              ; it would
    jc .jcor
    loop .skip
.atclus:
%ifndef KERN_SMALL                 ; kern_small only ever arrives by the append
    or bp, bp                   ; door, whose offset IS the allocated end, so
    jnz .grow                   ; BP is 1 there and this arm is unreachable

    ; --- INSIDE: the transfer, into clusters the file has -----------------
    mov dx, [dskw_rem]          ; sectors; the walk arms the widget itself
    mov bx, [dskw_src]
    mov es, [dskw_seg]
    call dsk_write_chain_x
    jc .jio                     ; a media error, mapped as one: 03h is
    jmp short .size             ; FERR_WPROT here as on every other write path
.jio:
    cmp al, 1                   ; AL = 2 is the CHAIN ending before the size
    jne .jcor                   ; said: corruption, not [dsk_ioerr]'s last
    jmp dskw_ioerr              ; (and possibly stale) transfer status
.jcor:                          ; TWO RELAYS, here where nothing falls in:
    jmp dskw_rdata.corrupt      ; .corrupt and .badarg above are out of short
.jbad:                          ; reach of this arm on kern_big, so its five
    jmp dskw_nbody.badarg       ; refusals were five-byte inverted pairs (-9)
%else
.jcor equ .corrupt              ; ...and in reach on kern_small, which is
.jbad equ .badarg               ; shorter by the INSIDE arm, so no relay there
%endif

    ; --- AT END: allocate, write, flush, link, flush ----------------------
.grow:
    push ax                     ; the last cluster the file owns; 0 = none
    mov cx, [dskw_len]          ; arm the widget as dskw_wbody does, so a
    xor dx, dx                  ; file-manager copy reports chunk by chunk
    call ct_fpg_begin
    call dskw_wdata             ; the new sub-chain, allocated and written
    jc .rollback
%ifdef DSKW_WSEQ
    test byte [dws_flg], WSQF_HELD
    jnz .held
%endif
    pop ax

    ; --- THE SEAL: the new sub-chain durable, then linked, then the size ---
    ; in: AX = the file's last cluster (0 = it was empty), [dskw_first] the
    ; sub-chain's head, dskw_raw the entry. dws_commit calls in HERE with a
    ; held stream's, so a commit is exactly this tail and never a copy of it.
    ;
    ; **ONE FLUSH WHEN ONE SECTOR IS THE WHOLE UPDATE** (SPEC.md 18.4.7.6).
    ; The two flushes keep the link off the disk until the chain it points
    ; into is there - which only a MULTI-sector flush can get wrong. So the
    ; last cluster's own end mark is stored back unchanged first: that puts
    ; its sector (both, for a FAT12 straddle) in the dirty range without
    ; changing a byte, and a range that is still ONE sector holds the link and
    ; every new entry, which a single write lands whole or not at all.
    ; Otherwise the chain goes alone, then the link.
.seal:
    mov si, sp                  ; THE SIZE MOVES, whatever SI said: it always
                                ; does on this arm, and dskw_wdata has just
                                ; spent SI on the source (it is module-
                                ; internal and banks nothing). SP is never 0
    mov dx, DSKW_EOC16          ; the mark it already holds (setfat keeps 12
    call dskw_setfat            ; bits), stored back UNCHANGED: its sector -
                                ; both, for a FAT12 straddle - joins the dirty
                                ; range, and an empty file's 0 is no store at
                                ; all. No CF test: it fails only when the
                                ; window will not load, and .link's store is
                                ; the same load and answers the same way
    mov dx, [dskw_first]
    or ax, ax
    jnz .grown
    mov [dskw_raw+DSK_R_CLUS], dx   ; the file was empty: the ENTRY takes the
.grown:                         ; head, stored at .size after the chain
    mov cx, [dsk_fatd0]         ; **ONE FLUSH WHEN ONE SECTOR IS THE WHOLE
    cmp cx, [dsk_fatd1]         ; UPDATE** (SPEC.md 18.4.7.6): the link and
    je .link                    ; every new entry share it, and one write
                                ; lands them whole or not at all. (A store
                                ; above that MOVED the window flushed the
                                ; chain first, leaving the link's sector
                                ; alone: the same one-sector case.)
    xchg bx, ax                 ; otherwise the chain alone, durable and
    call dskw_flush_x           ; unreached, first (dskw_flush_x keeps BX
    jc .rbdrop                  ; and DX)
    xchg bx, ax
.link:
    call dskw_setfat            ; THE link: last -> first. setfat refuses an
    jc .lnkio                   ; empty file's 0 with CF = 0: no link
    call dskw_flush_x
    jc .out2

    ; --- and the SIZE, if it moved (SPEC.md 18.4.7.2) ---------------------
    ; Only ever UP: a write inside the file leaves the entry alone, and a
    ; grow always passes it, so this is the one store that makes the new
    ; bytes part of the file (18.4) and the head store above rides on it.
.size:
    or si, si
    jz .same
%ifdef DSKW_WSEQ                 ; (the size was copied in at .in)
    call dskw_ent_tail          ; the single sector write that makes those
%else                           ; bytes part of the file (18.4): the head and
    mov ax, [dwr_end]           ; the size, which are all a grow changes -
    mov [dskw_raw+DSK_R_SIZE], ax   ; and all a HOT record or a commit has
    mov ax, [dwr_end+2]
    mov [dskw_raw+DSK_R_SIZE+2], ax
    call dskw_ent_store
%endif
    jc .out2
.same:
    call dskw_sync_x
%ifdef DSKW_WSEQ
    test byte [dws_flg], WSQF_IN
    jnz .stamp                  ; after the sync: the token carries its bump
%endif
    xor ax, ax                  ; ...and CF=0 with it
    ret
.rollback:
    pop cx                      ; the banked last cluster, into a dead register
%ifdef DSKW_WSEQ
    test byte [dws_flg], WSQF_HELD  ; A FAILED HELD CALL LOSES ITSELF AND
    jz .rbdrop                  ; NOTHING ELSE (SPEC.md 18.4.9), as a failed
    push ax                     ; APPEND does. Dropping the window would take
    mov ax, [dskw_first]        ; **ITS OWN SUB-CHAIN FREED FIRST** (SPEC.md
    call dskw_free_chain        ; 18.4.9.1): never linked, so it is this
                                ; call's alone, and a flush of it would leak
                                ; every cluster it took - on a DISK FULL,
                                ; every free cluster the volume had left
    call dskw_flush_x           ; the held chain's unflushed allocations with
    pop ax                      ; it, and the commit would then link the file
    jnc .rbkept                 ; into clusters the disk calls free. So FLUSH
    call dws_hlose              ; it: the held chain is unreachable until the
                                ; commit; this call's own half-built
                                ; sub-chain is freed above rather than left
                                ; as lost clusters. Only a
                                ; flush that ALSO fails loses the stream: it
                                ; is POISONED, as a lost bank is, so its next
                                ; call answers FERR_IO rather than appending
                                ; past what never reached the disk
%endif
.rbdrop:
    call dskw_refat             ; drop the half-built sub-chain; it keeps AX
.rbkept:
    stc
.out2:
    ret
.lnkio:                         ; setfat's CF = 1 is a FAT window that would
    jmp dskw_ioerr              ; not load, with AX still a CLUSTER: map it
%ifdef DSKW_WSEQ
    ; --- HELD: an unreachable chain grows; dws_commit links it -------------
.held:
    pop ax                      ; the file's last cluster, or the held tail
    mov dl, 1
    xchg dl, [dws_hold]
    or dl, dl
    jnz .hmore
    mov [dws_hlink], ax         ; a NEW hold: the link dws_commit will make,
    add word [dsk_mgen], 2      ; ...and a token no older one can equal: the
    jmp short .stamp            ; one call that moves nothing else. Its head
                                ; is [dskw_first], which the record keeps
.hmore:
    mov dx, [dskw_first]        ; the held tail -> the new sub-chain: both
    call dskw_setfat            ; unreachable, so any flush of it is harmless
    jc .lnkio                   ; (and no sync: nothing the disk shows moved)
    mov ax, [dws_hfirst]        ; ...and the record goes on naming the HELD
    mov [dskw_first], ax        ; chain's head, not this call's

    ; --- THE RECORD, after a WRITE_SEQ call that wrote (SPEC.md 18.4.9) ---
    ; the entry's head and size, where it lives, the last cluster, and the
    ; volume; the door hands [dsk_mgen] back as the token
.stamp:
    mov al, [disk_drive]
    mov [dws_vol], al
    mov si, DWS_REC0
    mov di, dws_rec
    call dws_load.mv
    xor ax, ax
    ret
%endif

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

    ; a fresh entry: 32 zero bytes, the name, the creation stamps. ARCHIVE is
    ; .patch's, which ORs it onto every file whatever made it - so it is not
    ; stored here as well, and neither is the access date .patch writes
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
    call dskw_copy11
    cmp byte [dskw_syswr], 0    ; hidden + system, if the kernel is making
    je .notsys1                 ; one of its own (SPEC.md 19.6)
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
    call dskw_take_slot         ; adopt the slot dskw_find spotted

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
    call dskw_czstamp           ; ...and the compression hint (SPEC.md 20.14)
    call dskw_ent_store         ; CF + AX
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_czstamp - set, or clear, the compressed-file hint (SPEC.md 20.14)
; in:  dskw_raw = the entry about to be stored; [dskw_seg]:[dskw_src] = the
;      bytes being written, [dskw_len]/[dskw_lenhi] = how many
; clobbers: AX, BX, CX, flags - BX and CX being what dskw_commit, its one
;           caller, pops back on its way out
;
; THE HINT IS DERIVED FROM THE BYTES and never propagated from another entry,
; which is what makes it unable to describe a different file: a save, a
; file-manager copy and a package's own write all arrive here with the file's
; first sector in hand, so all three get it right without knowing it exists.
; CLEARING IS THE HALF THAT MATTERS MOST - a plain file landing on a
; compressed one's slot has to take the mark off, or that slot goes on
; describing a file which is no longer there.
;
; Only dskw_wbody and the mkdir path reach dskw_commit, so this can never see
; an APPEND's later chunk: [dskw_src] is byte 0 of the file every time it runs.
; -----------------------------------------------------------------------------
dskw_czstamp:
    push es
    xor ax, ax                  ; cleared on every path, first: the mark and
    mov [dskw_raw+DSK_R_CZM], ax    ; the high byte are one word, and the
    mov [dskw_raw+DSK_R_CZL], ax    ; accumulator is 8 bytes where two
                                ; `mov word [m], 0` are 12 - AND it goes on
                                ; paying below, `cmp [m], al` being 4 bytes
                                ; where `cmp byte [m], 0` is 5. The zero has
                                ; to survive as far as `.look`, which is why
                                ; nothing between here and there spends AX
    cmp [dskw_isdir], al
    jne .out                    ; a folder has no bytes to look at
    cmp [dskw_lenhi], ax
    jne .look                   ; a file over 64KB is still a candidate: it is
    cmp word [dskw_len], DSK_CZ_HDR ; only this kernel's READ path that caps
    jbe .out                    ; there (SPEC.md 20.14.2), and a hint written
.look:                          ; now outlives the cap
    mov es, [dskw_seg]
    mov bx, [dskw_src]
    call dskw_czhdr
.out:
    pop es
    ret

; -----------------------------------------------------------------------------
; dskw_czsniff - ask the FILE, when the directory did not say (SPEC.md 20.14.6)
; in:  dskw_raw's entry, with the hint already read as ABSENT, and
;      DSK_R_CLUS holding its first cluster or its driver handle
; out: nothing; the hint in dskw_raw when the file's own header says 'CZ'
; clobbers: AX, BX, CX, DX, ES, flags - both call sites reload everything
;           they want from memory at `.sniffed`, so there is nothing to save
;
; THE HINT IS A CACHE AND THIS IS THE MISS PATH. `disk.inc` has said since it
; was written that a foreign tool may drop those four bytes and that the read
; path checks the file's own header too - and it did not: `dskw_czexp`
; VALIDATES the hint, it never DISCOVERS compression, so a file copied in by
; DOS, or onto a volume with no directory entry to carry a hint at all,
; reached the application as packed bytes.
;
; **IT COSTS NO EXTRA `int 13h` ON A FILE THAT HAS A HINT**, because it is not
; called for one - and on a file that has not, it costs none either: the peek
; is a one-sector `disk_read`, SPEC.md 18.95's cache fills the slot to the end
; of the track, and `dskw_rdata`'s first sector comes straight back out of it.
; The one case that pays is a hintless file opened while `MEM_P_DIRW` has been
; shed and stayed shed, and that is one sector, once, on OPEN and not on a
; listing.
;
; A redirected volume has no directory entry at all, so this is the ONLY thing
; that can tell it - and neither transport is spelled here: dsk_peek_x
; (SPEC.md 20.14.6.2) is that, shared with the loader's header peek and the
; icon harvest's. It FALLS THROUGH into the judgement below rather than
; calling it, which is what the two halves being adjacent is for.
; -----------------------------------------------------------------------------
dskw_czsniff:
    mov ax, [dskw_raw+DSK_R_CLUS]   ; the first cluster - or, on a redirected
                                    ; volume, THE HANDLE, which .fsread banks
                                    ; in the same cell precisely so that this
                                    ; routine never learns the difference
    mov cx, DSK_CZ_HDR
    call dsk_peek_x             ; ES:BX = the bytes, AX = how many arrived
    jc dskw_czhdr.no            ; ...and CX the count that was asked for, so
    cmp ax, cx                  ; the compare costs 2 bytes and not 3
    jne dskw_czhdr.no           ; a file shorter than the header is not one
                                ; ...and falls through

; -----------------------------------------------------------------------------
; dskw_czhdr - a file's first bytes -> the hint (SPEC.md 20.14.6)
; in:  ES:BX = at least DSK_CZ_HDR bytes of the file, from its byte 0
; out: nothing; the hint in dskw_raw is written only when the bytes say 'CZ'
; clobbers: AX, CX, flags
;
; FACTORED OUT OF dskw_czstamp because the READ path needs the same judgement
; from a different place (SPEC.md 20.14.6): the writer has the bytes in hand
; and the reader has to go and get them, and what they do with them once they
; have them is the same six stores. The CLEARING stays in the stamper - that
; is the writer's half, and a reader that cleared would erase a hint it is
; about to want.
; -----------------------------------------------------------------------------
dskw_czhdr:
    cmp word [es:bx], DSK_CZ_MAG
    jne .no
    mov al, [es:bx+2]
    cmp al, LZ_LZB
    ja .no                      ; a format no decoder in this kernel has
    mov cx, [es:bx+6]           ; the unpacked size, high word - of which the
                                ; hint holds a BYTE, 16MB being past any
                                ; volume here. A size that does not fit, or is
                                ; no bigger than the file, is stamped as it
                                ; comes: the READ refuses either as FERR_IO,
                                ; which is the right verdict on a header that
                                ; is not describing its file, and saying it
                                ; here as well cost 20 bytes
    mov [dskw_raw+DSK_R_CZH], cl
    mov cx, [es:bx+4]
    mov [dskw_raw+DSK_R_CZL], cx
    add al, DSK_CZ_MARK             ; ...and the FORMAT rides in the mark
    mov [dskw_raw+DSK_R_CZM], al    ; (SPEC.md 20.14.1)
.no:
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
    mov word [DSKW_FNONE], 0xFFFF   ; NONE, in the HIGH word: no directory
                                ; sector's LBA reaches FFFFxxxxh, and a low
                                ; word of FFFFh is an ordinary one past
                                ; 32MB (SPEC.md 18.7.5)
    mov byte [dskw_fend], 0
    push ss
    pop es
    mov ax, [dsk_cwd]           ; the CURRENT directory, not the root
    call dsk_dirw_start_x ; (SPEC.md 19.2 - one walker, both shapes)
.sector:
    call dsk_dirw_get_x ; dsk_secbuf = the next sector (19.2.1)
    jnc .secok
    jz .noent                   ; the directory ended
    jmp short .ioerr
.secok:
    mov [dskw_cursec], ax       ; ...and its high word is the walker's
                                ; [dsk_c2hi], which nothing moves before the
                                ; next sector: dskw_hit and dskw_mark_free
                                ; read it there (SPEC.md 18.7.5)
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
    call dskw_hit               ; found - and TAKE THE ENTRY WHILE IT IS HERE.
                                ; dsk_secbuf holds this very sector - we read
    clc                         ; it two instructions ago - and every
    jmp short .out              ; caller that succeeds here wanted these 32
                                ; bytes next. dskw_ent_load used to re-read
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
    cmp word [DSKW_FNONE], 0xFFFF
    jne .next                   ; an earlier slot already won
    call dskw_mark_free
    jmp short .next
.endmark:
    cmp word [DSKW_FNONE], 0xFFFF
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
    call dskw_ioerr             ; ...which sets CF itself
.out:                           ; ...AND FIVE OTHER ROUTINES' POP RUN: the
    pop es                      ; ones that push this order jump to .out,
.pdi:                           ; .pdi or .pdx (a `pop` writes no flag, so
    pop di                      ; each one's CF rides through). kernel.asm's
    pop si                      ; kretc_* ladder pops AX too, which none of
.pdx:                           ; these may
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_hit - a scan's MATCH: publish where the entry is and take its 32 bytes
; in:  SI -> the entry inside dsk_secbuf, [dskw_cursec] = that sector (and
;      [dsk_c2hi] its high word, the walker's, SPEC.md 18.7.5)
; out: [dskw_dsec]/[dskw_doff] = its location, dskw_raw = the whole entry
; clobbers: AX, CX, DI, flags
;
; dskw_find and dskw_rt_scan each carried this. Once a scan, on its hit - not
; once an entry - so the per-entry loops are exactly as they were. 32 and not
; 11: dsk_copy_in halves the count into words, so 11 would drop a byte.
; -----------------------------------------------------------------------------
dskw_hit:
    mov di, dskw_dsec
    call dskw_here
    mov di, dskw_raw
    mov cx, 32
    jmp dsk_copy_in_x

; -----------------------------------------------------------------------------
; dskw_mark_free - record the current scan position as the free slot
; in:  [dskw_cursec] and [dsk_c2hi], SI -> the entry inside dsk_secbuf
; out: [dskw_fsec] / [dskw_foff]
; clobbers: flags
; -----------------------------------------------------------------------------
dskw_mark_free:
    push ax
    push di
    mov di, dskw_fsec
    call dskw_here
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_here - store the scan's position as a {sector, offset} record
; in:  DI -> dskw_dsec or dskw_fsec (each is followed by its offset word),
;      SI -> the entry inside dsk_secbuf, [dskw_cursec] = that sector and
;      [dsk_c2hi] its high word - the walker's, which nothing between the
;      sector's read and here moves (SPEC.md 18.7.5)
; clobbers: AX, flags
;
; dskw_hit and dskw_mark_free each carried this, for the two records
; dskw_ent_store and dskw_take_slot read
; -----------------------------------------------------------------------------
%ifdef OS88_BIGVOL
DSKW_LBASZ   equ 4             ; bytes of an LBA cell (LBARES)
%else
DSKW_LBASZ   equ 2
%endif
dskw_here:
    mov ax, [dskw_cursec]
    mov [di], ax
%ifdef OS88_BIGVOL
    mov ax, [dsk_c2hi]
    mov [di+2], ax
%endif
    mov ax, si
    sub ax, dsk_secbuf
    mov [di+DSKW_LBASZ], ax
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
; dskw_copy11 - copy an 11-byte raw name inside the kernel segment
; in:  SI = source, DI = destination
; out: nothing; preserves all registers
;
; THE COUNT IS IN HERE, not at the seven call sites: every one of them moves
; a raw 8.3 name and every one of them used to spell `mov cx, 11` first. That
; also makes CX genuinely preserved, where before the sites left it holding
; 11 - two of the seven are inside a loop whose counter that was.
;
; ES belongs to the caller's buffer here and is not this routine's to
; reload; disk.inc's dsk_ncopy banks it around its string move, and it is the
; same body four files each used to carry.
; -----------------------------------------------------------------------------
dskw_copy11:
    push cx
    mov cx, 11
    call dsk_ncopy
    pop cx
    ret

; -----------------------------------------------------------------------------
; dskw_take_slot - adopt the directory slot dskw_find spotted (SPEC.md 19.2)
; in:  [dskw_fsec]/[dskw_foff]/[dskw_fend], as dskw_find left them
; out: [dskw_dsec]/[dskw_doff]/[dskw_zapnext], which is what dskw_ent_store
;      reads
; clobbers: AX.  Both callers overwrite AX immediately - dskw_commit's
;      `.patch` reloads it and fcp_relink's next act is dskw_ent_store, whose
;      AX is an OUTPUT - so it is not banked
;
; Taking the 0x00 terminator means the next entry must be zeroed to remain
; one, which is what [dskw_zapnext] carries.  Written out twice until size
; pass 2: dskw_commit above and filecp.inc's fcp_relink, whose copy carried
; these same two comments - which is the tell.  Both are `.cold`, so near.
; -----------------------------------------------------------------------------
dskw_take_slot:
    mov ax, [dskw_fsec]
    mov [dskw_dsec], ax
%ifdef OS88_BIGVOL
    mov ax, [dskw_fsec+2]
    mov [dskw_dsec+2], ax
%endif
    mov ax, [dskw_foff]
    mov [dskw_doff], ax
    mov al, [dskw_fend]
    mov [dskw_zapnext], al
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
%ifdef DSKW_WSEQ
; dskw_ent_tail - dskw_ent_store for the entry's LAST SIX BYTES: its head and
; its size, which are all a grow changes (SPEC.md 18.4.9) and all WRITE_SEQ's
; record keeps. Same in, out and clobbers
dskw_ent_tail:
    push bx
    mov bx, 0x0300 + DSK_R_CLUS ; BH = words, BL = where they start
    jmp short dskw_ent_store.go
%endif
dskw_ent_store:
    push bx
%ifdef DSKW_WSEQ
    mov bx, 0x1000              ; the whole entry: 16 words from byte 0
.go:
%endif
    push cx
    push si
    push di
    push es
    push ss
    pop es
%ifdef DSKW_WSEQ
    push bx                     ; (DSK_RD1D names its LBA in BX)
%endif
    DSK_RD1D dskw_dsec          ; all 32 bits of it (SPEC.md 18.7.5)
%ifdef DSKW_WSEQ
    pop bx                      ; `pop` leaves CF alone
%endif
    jc .ioerr
%ifdef DSKW_WSEQ
    xor cx, cx
    xchg cl, bh                 ; CX = the words, BX = the byte offset
    lea si, [bx+dskw_raw]       ; DS = kernel, ES = LOW_SEG: a plain movsw
    mov di, dsk_secbuf          ; (an OFFSET, the ES side: no lea through
    add di, bx                  ; a LOW_SEG label - SPEC.md 2.1)
%else
    mov si, dskw_raw            ; DS = kernel, ES = LOW_SEG: a plain movsw
    mov di, dsk_secbuf
    mov cx, 16
%endif
    add di, [dskw_doff]
    cld
    rep movsw
%ifdef DSKW_WSEQ
    or bl, bl                   ; a TAIL store is never a new slot's: no zap,
    jnz .write                  ; whatever a failed create left in the flag
%endif
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
    DSK_WR1D dskw_dsec
    jc .ioerr
    jmp short .out              ; CF = 0 from the `jc`
.ioerr:
    call dskw_ioerr             ; ...which sets CF itself
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
    cmp ax, [dsk_maxclus]       ; wrap the rover into range: past the last
    ja .wrap                    ; cluster or below the first, both are 2
    cmp ax, 2
    jae .check
.wrap:
    mov ax, 2
.check:
    push ax
    call dsk_next_clus_x
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
    mov dx, DSKW_EOC16          ; ...on FAT12 too: dskw_setfat keeps the low
                                ; 12 bits there, which is DSKW_EOC12 exactly
    call dskw_setfat            ; the EOC mark. A cluster we could not MARK is
    jc .iofail                  ; not a cluster we have taken - reporting it as
                                ; allocated hands the same one out again on the
                                ; next call, which is the cross-link the commit
                                ; order exists to make impossible
    inc ax
    mov [dsk_rover], ax         ; next search starts past it (CF = 0: the
    dec ax                      ; `jc` above, and inc/mov/dec keep it)
    jmp short .out
.iofail:
    sbb ax, ax                  ; ...and say WHICH failure it was: 0xFFFF,
    jmp short .out              ; out of the CF the `jc` arrived with, and
                                ; CF = 1 still (the borrow)
.nospace:
    xor ax, ax
    stc
.out:
    jmp dskw_find.pdx           ; its pop run is ours (a pop writes no flag)

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
    call dsk_next_clus_x        ; AX = the link, before we erase it - and CF=1
    jc .done                    ; for a cluster outside [2, dsk_maxclus], which
                                ; is dskw_clok's own test: it was asked here
                                ; first as well, the same answer twice
    xchg ax, si                 ; SI = the link, AX = this cluster
    xor dx, dx
    call dskw_setfat            ; free this one (it preserves AX..SI)
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
    call dskw_clok
    jc .ok
    mov si, dx                  ; SI = the value: DX is dsk_fat_ofs's second
                                ; output and cannot carry it
    call dsk_fat_ofs_x  ; BX = window-relative, DX = abs sector
    jc .out                     ; the window would not load: CF straight out
    mov es, [dsk_fatseg]
    cmp byte [dsk_fattype], 0
    jne .fat16
    and si, 0x0FFF
    test al, 1                  ; the cluster's parity, asked BEFORE AX is
    mov ax, [es:bx]             ; reloaded with the straddling word - `mov`
    jz .even                    ; leaves the flags alone
    and ax, 0x000F              ; odd cluster: keep the low nibble
    mov cl, 4
    shl si, cl
    jmp short .or
.even:
    and ax, 0xF000              ; even cluster: keep the high nibble
.or:
    or ax, si
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
    inc ax                      ; (CL is still 9: dskw_dirty reads AX and
    shr ax, cl                  ; writes only memory and flags)
    add ax, [dsk_fatw0]
    call dskw_dirty
.ok:
    clc
.out:
    pop es
    jmp kretc_si

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
    je .done                    ; clean (and .done's re-store of the empty
                                ; range is a no-op: d1 is 0 whenever d0 is)
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
    mov es, [dsk_fatseg]
    mov bx, ax
    sub bx, [dsk_fatw0]         ; the range's offset IN THE WINDOW
    mov cl, 9
    shl bx, cl
    mov cx, dx
    push ax
    add ax, [dsk_fatlba]        ; FAT1
    call disk_write_x ; preserves BX: reusable for FAT2 below
    pop ax
    jc .ioerr
    cmp byte [dsk_nfats], 2
    jne .done
    push ax
    add ax, [dsk_fatlba]
    add ax, [dsk_fatsz]         ; FAT2 = FAT1 + FATSz16
    call disk_write_x
    pop ax
    jc .ioerr
.done:
    xor ax, ax                  ; (CF = 0 with it)
.reset:                         ; the range is EMPTY now, whichever way out
    call dskw_fatclean          ; (it leaves CF alone)
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
    mov ax, FERR_IO
    stc
    jmp short .reset
.ioerr:
    call dskw_ioerr             ; ...which sets CF itself
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
dskw_fatclean:                  ; ...the dirty range EMPTY, which is the half
    mov word [dsk_fatd0], 0xFFFF ; dskw_flush_x ends every way out with and
    mov word [dsk_fatd1], 0     ; disk_mount_x starts with: `mov` only, so
    ret                         ; every flag (CF above all) comes back as sent

; -----------------------------------------------------------------------------
; dskw_ioerr - map the last int 13h status to an error code
; in:  [dsk_ioerr] (set by the failing dsk_xfer)
; out: AX = FERR_WPROT if the media is write-protected, else FERR_IO, and
;      CF = 1 - so a caller's `call dskw_ioerr / stc / ret` is a `jmp`
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
dskw_ioerr:
    mov ax, FERR_IO
    cmp byte [dsk_ioerr], 0x03
    jne .out
    mov al, FERR_WPROT          ; (AH is 0 from the line above)
.out:
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_wone / dskw_rone - ONE sector through dsk_secbuf, at [dskw_clba]
; in:  CX = the live bytes (1..512), at [dskw_wseg]:[dskw_src]
; out: CF = 1 an I/O error (dskw_ioerr maps it); CX and SI preserved
; clobbers: AX, BX, DI, ES, flags
;
; The staged-sector arm of dskw_wdata and dskw_rdata, and each one's partial
; final sector: four sites that each loaded the caller's segment and offset,
; called a copier, set ES back and spelt the one-sector transfer. They were a
; stager and a fetcher that took ES:SI from the caller; they are the whole
; step now, which is what all four sites were writing out around them.
;
; dskw_wone stages the bytes ZERO-FILLED - so whatever sits past the caller's
; buffer never reaches the disk - and writes the scratch. DS is the caller's
; buffer segment for the move, so NO kernel variable is readable between the
; `mov ds` and the `pop ds`: the dsk_copy_in discipline (SPEC.md 18).
; dskw_rone reads the scratch and copies only the live bytes up, so no
; memory past EOF reaches the caller - and there is nothing to zero, the
; buffer being the caller's.
; -----------------------------------------------------------------------------
dskw_wone:
    push cx
    push dx
    push si
    push ds
    mov dx, 512
    sub dx, cx                  ; DX = bytes to zero after the copy
    mov si, [dskw_src]          ; (read through the KERNEL's DS, before...)
    mov ds, [dskw_wseg]         ; ...DS:SI = the caller's bytes
    push ss
    pop es
    mov di, dsk_secbuf
    cld
    rep movsb
    mov cx, dx
    xor al, al
    rep stosb
    pop ds
    DSK_WR1D dskw_clba          ; ES = LOW_SEG already: the scratch goes out
    pop si
    pop dx
    pop cx
    ret

dskw_rone:
    push ss
    pop es
    push cx
    DSK_RD1D dskw_clba          ; the sector, whole, into the scratch
    pop cx
    jc .out
    push si
    push cx
    mov es, [dskw_wseg]         ; ES:DI = the caller's destination
    mov di, [dskw_src]
    push ds
    push ss
    pop ds
    mov si, dsk_secbuf
    cld
    rep movsb                   ; DS:SI -> ES:DI
    pop ds
    pop cx
    pop si                      ; CF = 0 from the `jc`, which nothing since
.out:                           ; has touched
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
    mov al, [clk_sn_hour]       ; Horner's rule, one field at a time: shift
    cbw                         ; what is there up by the next field's width
    mov bl, [clk_sn_min]        ; and OR it in (.pk) - every field is below
    mov cl, 6                   ; its width, so the OR is an add
    call .pk                    ; hour:minute
    mov bl, [clk_sn_sec]
    shr bl, 1                   ; FAT keeps seconds in twos
    dec cx                      ; CL = 5 (.pk leaves CL alone)
    call .pk
    mov [dskw_time], ax
    mov ax, [clk_sn_year]
    sub ax, 1980
    mov bl, [clk_sn_mon]
    mov cl, 4
    call .pk
    mov bl, [clk_sn_day]
    inc cx                      ; CL = 5
    call .pk
    mov [dskw_date], ax
    jmp kretc_cx
.pk:
    shl ax, cl
    or al, bl
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
    push dx                     ; DX banked only so the exit is kretc_di's
    push si                     ; rung: one byte here against six of pops
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
    jz .ext_end                 ; ...whose test - an empty name - is .ext_end's
    cmp al, '.'
    je .dot
    call dskw_char_x            ; upcase + legality
    jc .bad
    cmp cx, 8
    jae .bad                    ; a ninth stem character
    mov bx, cx                  ; [bx+di]: the count IS the column
    mov [bx+di], al
    inc cx
    jmp short .stem
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
    mov bx, cx
    mov [bx+di+8], al
    inc cx
    jmp short .ext
.ext_end:
    jcxz .bad                   ; a trailing dot with no extension (or, from
    clc                         ; .stem, no name at all)
    jmp short .out
.bad:
    stc
.out:
    jmp kretc_di

; -----------------------------------------------------------------------------
; dskw_char - one short-name character: upper-case it, or reject it
; in:  AL = the character
; out: AL upper-cased, CF=0 if legal; CF=1 if not
; clobbers: AL (the output), CF
; -----------------------------------------------------------------------------
dskw_char_x:
    call dsk_upper              ; disk.inc's, and this WAS its five
                                ; instructions again; both files are `.cold`,
                                ; so it is a plain near call - the same kind
                                ; dskw_* already makes to dsk_next_clus_x
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


; =============================================================================
; The FAT12 floppy formatter (SPEC.md 18.96)
;
; **AN ON-DEMAND MODULE** (SPEC.md 2.8), shipped as FORMAT.DRV and read into a
; heap claim when the user asks to format a disk. It qualifies on
; docs/plans/completed/ONDEMAND-PLAN.md 1's test the way the Control Panel does and for a
; different reason: preparing a disk is a PREPARED operation with a trip to
; the disk box already in it, and it is among the rarest deliberate actions
; in the system. MS-DOS drew the same line in 1981 - COPY lives in
; COMMAND.COM and FORMAT is a file on the system disk - and the constraint
; that produced that split is the hardware, which is the same hardware.
;
; **kern_small HAS IT NOW, and that is the payoff rather than the saving.**
; This was `%ifndef KERN_SMALL` - a whole feature compiled out, so a 128KB
; machine could not format a floppy at all - and on demand it costs that
; build the strings, the menu item and a stub. `kern_big` gives back the
; ~750 bytes of cold code as well. The first edition of ONDEMAND-PLAN read
; the guard as a saving already taken; it was a missing feature.
;
; There is no API slot here, deliberately (see the note at the foot of
; SPEC.md 18.96), so nothing a package can name changed and one .o88 still
; serves both kernels.
;
; Every TRACK is laid down first with int 13h AH=05h (dskw_fmt_low, SPEC.md
; 18.96.3), and then the boot sector, both FATs and the root directory - 12
; sectors on a 360KB disk and 33 on a 1.44MB one - over the tracks just laid.
; It used to be that second half alone, on the argument that a surface format
; turns a disk this OS cannot read into one nothing can; every disk anybody
; actually brought to it was one it could not read, so that was the wrong
; way round.
; =============================================================================

; --- the four standard IBM formats (SPEC.md 18.96) ---------------------------
; NOT computed. tools/os88disk.py's GEOMETRY table already pins three of these
; and this is the same arithmetic; a formatter that re-derives a layout DOS
; has had since 1983 can only differ from it.
;
; 1.44MB's FAT is 9 sectors = DSK_FAT_SECS exactly, and SPEC.md 18.2 rule 10
; refuses a floppy whose FAT is larger - so the biggest thing this can write
; is the biggest thing that can be mounted, by construction.
;
; **A ROW IS THE BPB ITSELF**, bytes 13 to 25 of the boot sector in the boot
; sector's own order - so dskw_fmt_boot lays a row down with ONE copy where it
; used to load and store six fields and three constants - plus the cylinder
; count the BPB does not carry. The size the confirmation says is TOT/2.
DFMT_SPC     equ 0              ; byte: BPB_SecPerClus
DFMT_RSVD    equ 1              ; word: BPB_RsvdSecCnt (1, every row)
DFMT_NFATS   equ 3              ; byte: BPB_NumFATs (2, every row)
DFMT_ROOTN   equ 4              ; word: BPB_RootEntCnt
DFMT_TOT     equ 6              ; word: BPB_TotSec16 = spt * 2 * cyl
DFMT_MEDIA   equ 8              ; byte: BPB_Media
DFMT_FATSZ   equ 9              ; word: BPB_FATSz16
DFMT_SPT     equ 11             ; word: BPB_SecPerTrk (the MEDIUM's)
DFMT_BPB     equ 13             ; ...the BPB span ends here
DFMT_CYL     equ 13             ; byte: cylinders (the DRIVE's - see the probe)
DFMT_SIZE    equ 14
%if DFMT_RSVD != DSK_B_RSVD-DSK_B_SPC || DFMT_NFATS != DSK_B_NFATS-DSK_B_SPC || DFMT_ROOTN != DSK_B_ROOTN-DSK_B_SPC || DFMT_TOT != DSK_B_TOT16-DSK_B_SPC || DFMT_MEDIA != DSK_B_MEDIA-DSK_B_SPC || DFMT_FATSZ != DSK_B_FATSZ-DSK_B_SPC || DFMT_SPT != DSK_B_SPT-DSK_B_SPC || DFMT_BPB != DSK_B_HEADS-DSK_B_SPC
  %error "a dskw_fmt_tab row is the BPB from BPB_SecPerClus to BPB_SecPerTrk, byte for byte: dskw_fmt_boot copies it whole"
%endif

DFMT_ROWS    equ 4

; The entry indices - this image's ABI with the resident stubs below.
FME_PROBE   equ 0
FME_FORMAT  equ 1
FME_REACH   equ 2
FME_LINE    equ 3               ; the status line's own composer (SPEC.md
                                ; 2.8.6): every string the confirmation, the
                                ; swap prompt and the two verdicts letter is
                                ; in THIS FILE now, read through CS
FM_NENT     equ 4

%ifndef KD_BUILD                    ; **NO MODULE UNDER kern_dos** (SPEC.md
                                    ; 96.43): `mod_need` is not in that root,
                                    ; so nothing could load this image - and
                                    ; INT 21h has no format function, so
                                    ; nothing would ask. 1,235 bytes of a
                                    ; DISK FORMATTER that sat inside a build
                                    ; whose every byte is a byte off the DOS
                                    ; program's own memory
section .modf                   ; the on-demand image (SPEC.md 2.8)
modf_hdr:
    dw 0x384F                   ; MOD_H_MAGIC
    db MOD_VER                  ; MOD_H_VER
    db MOD_FMT                  ; MOD_H_ID
    dw BUILD_NUM                ; MOD_H_BUILD - another COMMIT...
    dw MOD_STAMP                ; ...and another BUILD of this one
    dw modf_end                 ; MOD_H_IMG
    dw FM_NENT                  ; MOD_H_NENT
    dw modf_e_probe             ; the FAR entries, never the bodies: an entry
    dw modf_e_format            ; is reached by `call far`, so it has to end
    dw modf_e_reach             ; in a retf, and every body below keeps the
    dw modf_e_line              ; near `ret` cold code has always had

modf_e_probe:   call dskw_fmt_probe_x
            retf
modf_e_format:  call dskw_format_x
            retf
modf_e_reach:   call dskw_fmt_reach_x
            retf
modf_e_line:    call dskw_fmt_line_x
            retf

; -----------------------------------------------------------------------------
; dskw_fmt_ioerr - dskw_ioerr's six instructions, in the image
;
; The formatter's one call OUT that is not already a cw_* shim. Duplicated
; rather than far-entered on docs/plans/completed/HDD-SPLIT-PLAN.md 3.2's rule: a boundary
; crossing for two compares costs more than the eleven bytes it saves, and a
; far entry would be a fifth thing the resident half publishes for the sake
; of a predicate. It reads the same [dsk_ioerr] through the same DS.
; -----------------------------------------------------------------------------
dskw_fmt_ioerr:
    mov ax, FERR_IO
    cmp byte [dsk_ioerr], 0x03
    jne .out
    mov ax, FERR_WPROT
.out:
    ret


; -----------------------------------------------------------------------------
; dskw_fmt_row_x - a table row's address (module-internal)
; in:  AL = row index
; out: SI -> the row
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
dskw_fmt_row_x:
    push ax
    mov ah, DFMT_SIZE
    mul ah                      ; AX = AL * DFMT_SIZE (AL <= 3)
    add ax, dskw_fmt_tab
    xchg si, ax
    pop ax
    ret

; The four standard IBM formats - IN THE IMAGE, and read through CS like its
; strings (SPEC.md 2.8.6). They were `.text`, 56 resident bytes on every
; machine, because CLONE.DRV sized a disk image against the same rows; the
; cloner carries its own four (size, spt) pairs now (clo_geom_img), and
; nothing but this image reads a row.
;
;        SPC RSVD NFATS ROOTN  TOT  MEDIA FATSZ SPT         CYL
dskw_fmt_tab:
    db 1
    dw 1
    db 2
    dw 224, 2880
    db 0xF0
    dw 9, 18
    db 80                       ; 1.44MB
    db 1
    dw 1
    db 2
    dw 224, 2400
    db 0xF9
    dw 7, 15
    db 80                       ; 1.2MB
    db 2
    dw 1
    db 2
    dw 112, 1440
    db 0xF9
    dw 3, 9
    db 80                       ; 720KB
    db 2
    dw 1
    db 2
    dw 112, 720
    db 0xFD
    dw 2, 9
    db 40                       ; 360KB
%if $ - dskw_fmt_tab != DFMT_ROWS * DFMT_SIZE
  %error "dskw_fmt_tab's rows are DFMT_SIZE bytes"
%endif

; =============================================================================
; The formatter's own STRINGS and the composer that letters them (SPEC.md
; 2.8.6). None of this is in KERNEL.SYS.
;
; Every line below goes on screen only while a format confirmation or its swap
; prompt is up, and both are armed after `mod_need` succeeds and taken down by
; the `fm_edit_end` that drops this image - so there is no moment at which one
; is on screen and the image is not. That is the ordering §2.8.6 asks for, and
; here it holds by construction rather than by care.
;
; Three of the formatter's strings STAYED in `files.inc`, and each is one of
; §2.8.6's three shapes: the menu item (the bar is built by the press that
; leads to loading), the swap prompt's step 0 and its key line (that prompt
; exists BECAUSE this file could not be read), and the `Format needs disk A:`
; toast beside it.
; =============================================================================
dskf_s_pr2:  db 'ERASE and format ', 0  ; ...when the volume MOUNTED, which is
                                ; the whole of what removing §22.12's old
                                ; refusal costs: the disk has files on it and
                                ; the question has to say so
dskf_s_pr:   db 'Format ', 0    ; TWO lines (SPEC.md 22.12): 'Format A: as
dskf_s_as:   db ': as ', 0      ; 720K?' in the row above, the answers on the
dskf_s_q:    db 'K?', 0         ; status line itself - as one line it is 44
                                ; characters and fm_stat_line truncates at
                                ; ([fm_cw]-12)/8 = 38 on the 320px template,
                                ; losing 'Esc=no'
dskf_s_k:    db 'Enter=yes  Esc=no', 0
dskf_s_k2:   db 'Spc=size  Enter=yes  Esc=no', 0  ; ...when there IS another
                                ; size to offer (rows 2 and 3 alone)
dskf_s_swap: db 'Insert the disk to format, then Enter', 0   ; mode 6 step 1,
                                ; which runs AFTER this image is loaded
dskf_s_ok:   db 'Formatted ', 0 ; ...and the verdict, SPEC.md 59.6's own
                                ; reasoning: a format is seconds of floppy the
                                ; user waited for, so success speaks rather
                                ; than only failure
dskf_s_bad:  db 'Made 360K, not 720K', 0    ; the OUTCOME, where this said the
                                ; cause first and the outcome last and had 40
                                ; characters to do it in. What the user has to
                                ; know is which disk they are now holding
                                ; (SPEC.md 59.8)
dskf_s_col:  db ':', 0

; -----------------------------------------------------------------------------
; dskw_fmt_cat - fm_scat over THIS IMAGE's strings (SPEC.md 2.8.6)
; in:  SI = a NUL string in this segment, DI = where it goes in KERNEL_SEG
; out: DI -> the terminator's place; SI past the NUL
; clobbers: AL, SI, DI, flags
;
; `[cs:si]`, and clone.inc's `clo_cat` is the same eleven bytes for the same
; reason - duplicated rather than shared because the two live in different
; images and a far call between two on-demand modules is a thing this
; mechanism deliberately cannot do (only one of them need be loaded).
; -----------------------------------------------------------------------------
dskw_fmt_cat:
    mov al, [cs:si]
    inc si
    mov [di], al
    or al, al
    jz .done
    inc di
    jmp short dskw_fmt_cat
.done:
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_sizeable - may this volume's size be overruled? (SPEC.md 18.96.2)
; in:  AL = the volume, [fm_fmtrow] live
; out: CF=0 = there is another size to offer; CF=1 = there is not
; clobbers: flags
;
; files.inc's `fm_fmt_sizeable` verbatim, in the image, because the LINE that
; names the Space key and the KEY that acts on it must not disagree and only
; one of them is resident now. Five instructions; a far entry for them would
; be a resident byte per instruction saved.
; -----------------------------------------------------------------------------
dskw_fmt_sizeable:
    cmp byte [fm_fmtrow], 2     ; the 9-sector pair, and only it
    jb .no
    push bx
    mov bl, al
    call COLD_SEG:dskf_dsk_vol_row
    jc .nopop
    cmp byte [bx+DV_UNIT], 2    ; ...on an EXTERNAL drive, and only there
    jb .nopop
    pop bx                      ; (CF = 0 from the `jb`)
    ret
.nopop:
    pop bx
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_line_x - stage one of the formatter's lines (SPEC.md 2.8.6)
; in:  AL = which, AH = the volume (selector 2 alone), DI = the destination in
;      KERNEL_SEG; [fm_fmtrow], [fm_ldrv] and [fm_lmok] published by the
;      caller's own paint for the two that are PROMPTS
; out: DI -> the NUL it wrote
; clobbers: DI (the output), flags
;
;   0  the confirmation's row line   'ERASE and format A: as 720K?'
;   1  the confirmation's key line   'Enter=yes  Esc=no' / with 'Spc=size'
;   2  the success verdict           'Formatted B:'
;   3  mode 6 step 1's row line      'Insert the disk to format, then Enter'
;   4  the fell-back verdict         'Made 360K, not 720K'
;
; 2 and 4 are TOASTS rather than prompt lines, and they are here for §2.8.6's
; own reason: `fm_edit_commit` composes them before its `fm_edit_end`, so the
; image is still loaded, and `toast_show` copies the buffer - so the message
; outlives the module without the module's bytes outliving it.
; -----------------------------------------------------------------------------
dskw_fmt_line_x:
    push ax
    push bx
    push cx
    push si
    mov bl, ah                  ; the VERDICT's volume comes from the caller
                                ; and not from [fm_ldrv]: that word is a
                                ; PAINT's snapshot, and a verdict is composed
                                ; on a keystroke, after which any window may
                                ; have painted (SPEC.md 22.9)
    cmp al, 1
    je .keys
    cmp al, 2
    je .ok
    cmp al, 3
    je .swap
    cmp al, 4
    je .fell

    mov si, dskf_s_pr           ; the confirmation. The SIZE is the point of
    cmp byte [fm_lmok], 0       ; naming it: dskw_fmt_probe cannot always tell
    je .pr                      ; 360K media from 720K in a 1.2MB drive, and
    mov si, dskf_s_pr2          ; this line is the one place the user can
.pr:                            ; catch that before it costs them anything
    call dskw_fmt_cat
    mov al, [fm_ldrv]
    add al, 'A'
    mov [di], al
    inc di
    mov si, dskf_s_as
    call dskw_fmt_cat
    mov al, [fm_fmtrow]
    call dskw_fmt_row_x         ; SI = the row it is about to write
    mov ax, [cs:si+DFMT_TOT]    ; ...in sectors, so the KB are half of it
    shr ax, 1
    call COLD_SEG:fmf_fm_utoa   ; the one thing this image does NOT carry: a
                                ; decimal formatter is real code and a second
                                ; copy of it is a second thing to be wrong
    mov si, dskf_s_q
    jmp short .tail
.keys:
    mov si, dskf_s_k
    push ax
    mov al, [fm_ldrv]
    call dskw_fmt_sizeable      ; ...and the key is named only where it does
    pop ax                      ; something (SPEC.md 18.96.2/47)
    jc .tail
    mov si, dskf_s_k2
    jmp short .tail
.ok:
    mov si, dskf_s_ok
    call dskw_fmt_cat
    mov al, bl
    add al, 'A'
    mov [di], al
    inc di
    mov si, dskf_s_col
    jmp short .tail
.fell:
    mov si, dskf_s_bad
    jmp short .tail
.swap:
    mov si, dskf_s_swap
.tail:
    call dskw_fmt_cat           ; ...which stores the NUL and leaves DI on it
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_probe_x - which of the four formats is this drive holding?
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
dskw_fmt_probe_x:
    push bx
    push cx
    push dx
    push si
    push di
    push es


    ; --- is it a floppy at all? (SPEC.md 18.7: DV_KIND is the TRANSPORT,
    ;     and a BIOS unit below 80h is the MEDIUM - disk_mount's own test)
    mov bl, [disk_drive]
    call COLD_SEG:dskf_dsk_vol_row
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
    push word [disk_spt]        ; ...and the three words .try writes, banked
    push word [disk_heads]      ; on the stack until .got2 puts them back -
    push word [dsk_totsec]      ; the probe leaves NO geometry behind (see
                                ; there). After the refusals: none of them
                                ; has aimed anything anywhere yet

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
.noflop:                        ; (HERE, a hop from the three tests at the top
    mov ax, FERR_PROT           ; that jump to it, rather than past the whole
    jmp .stcpop                 ; routine) a hard-disk partition is HDDTOOL's
                                ; (52.2), refused BEFORE the cache bypass went
                                ; up - and the probe's ONLY refusal now: a
                                ; medium that nothing reads is formatted, not
                                ; refused (SPEC.md 18.96.3)
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
    push ss                     ; ES = LOW_SEG, which SS always is
    pop es
    xor cx, cx                  ; CX = the row walker, and the answer
.row:
    call .try
    jnc .got
    inc cx
    cmp cx, DFMT_ROWS
    jb .row
    ; NOTHING ANSWERED, and that is not a refusal any more (SPEC.md
    ; 18.96.3). It used to be FERR_IO - "no medium here this can read, so
    ; none it can safely write either" - which was true of a formatter that
    ; only ever rewrote METADATA over sectors that had to exist already. Now
    ; that every format lays the tracks down itself, an unreadable disk is
    ; exactly the one it exists for: a 1.44MB disk taped for a 250 kbps
    ; drive, whose HD recording nothing here can read; a disk whose ID
    ; fields are damaged; a medium formatted for another machine. What it
    ; gets is what the DRIVE makes - the same answer DOS's FORMAT gives -
    ; so the row is chosen from SI (its sectors per track) and, for the
    ; 9-sector pair, DI (its cylinders) by the code below, exactly as a
    ; readable 9-sector disk's is.
    mov cx, 2                   ; the 9-sector pair, 720K or 360K below...
    cmp si, 15
    jb .got
    dec cx                      ; ...1.2MB on the 5.25" HD drive...
    cmp si, 18
    jb .got
    dec cx                      ; ...and 1.44MB on the 3.5" one
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
    pop word [dsk_totsec]       ; THE PROBE LEAVES NO GEOMETRY BEHIND: .try
    pop word [disk_heads]       ; wrote these three to aim its reads, and they
    pop word [disk_spt]         ; go back as they came in (the account is the
                                ; paragraph below .try)
.done:
    dec byte [dsk_rah_busy]
.clcpop:                        ; ...and the module's ONE pop run, for
    clc                         ; dskw_format_x and dskw_fmt_reach_x too:
    jmp short .pop6             ; they push the same six

.stcpop:
    stc
.pop6:                          ; (a pop writes no flag)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

    ; --- one candidate: can CHS (0,0,spt) be read? -------------------------
    ; A local, so it may read the walker in CX and the DRIVE's spt in SI -
    ; and SI is banked into DX first, because dskw_fmt_row_x spends SI on the
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
    call dskw_fmt_row_x
    mov ax, [cs:si+DFMT_SPT]
    cmp ax, dx
    ja .try_no
    mov [disk_spt], ax
    mov word [disk_heads], 2
    mov dx, [cs:si+DFMT_TOT]    ; §18.95's read-ahead clamps its fill against
    mov [dsk_totsec], dx        ; the volume's extent, and a failed mount
                                ; leaves whatever the last good one set. It
                                ; only ever shortens a fill, so a stale value
                                ; cannot read wrong bytes - but it can make
                                ; the probe's own surplus meaningless. .got2
                                ; puts the caller's back, and a format's own
                                ; mount re-derives it from the BPB regardless
    dec ax                      ; LBA spt-1 = CHS (0,0,spt) under this spt
    mov bx, dsk_secbuf
    mov cx, 1
    call COLD_SEG:dskf_disk_read
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

    ; --- why the geometry is banked and put back (.got2) ------------------
    ; The probe writes [disk_spt]/[disk_heads] to aim its reads, and it must
    ; not leave them written: its caller may ARM a confirmation and then be
    ; cancelled, and until the next mount those words are what any raw
    ; disk_read/disk_write on this volume would use. Nothing in the tree does
    ; one today - every path back here goes through a mount, which derives
    ; both from the BPB - so this closes a trap rather than fixing a bug, and
    ; it is the cheap end of that trade.
    ;
    ; **The values on the way IN, and that is a correction.** It used to force
    ; 9/2 - exactly what disk_mount's own failure path leaves (SPEC.md 18.3) -
    ; on the reasoning that the probe is only ever called with a failed mount
    ; behind it. docs/FIELD-NOTES.md 17.2 took that premise away: a volume that
    ; mounted can be formatted now, so the probe runs over live geometry, and
    ; forcing 9/2 there would leave a mounted 1.44MB volume claiming 9 sectors
    ; a track after a CANCELLED confirmation - a raw read at the wrong CHS with
    ; nothing at all to say so. Banking is two words more than the constant was
    ; and it is right whichever way the caller arrived. dskw_format_x sets the
    ; row's geometry itself and owes this nothing.

; -----------------------------------------------------------------------------
; dskw_format_x - lay a fresh FAT12 volume on the floppy in [disk_drive]
; in:  AL = a dskw_fmt_tab row (dskw_fmt_probe_x's answer), [disk_drive] = the
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
dskw_format_x:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    call dskw_fmt_row_x         ; SI = the row, IN THIS IMAGE, live throughout
    mov ax, [cs:si+DFMT_SPT]
    mov [disk_spt], ax
    mov word [disk_heads], 2
    mov ax, [cs:si+DFMT_TOT]    ; ...and the extent §18.95's fill clamps to,
    mov [dsk_totsec], ax        ; for dskw_fmt_probe_x's reason

    ; --- 0. every TRACK, laid down fresh (SPEC.md 18.96.3) -----------------
    call dskw_fmt_low
    jc .ioerr

    ; --- 1-3. every METADATA sector, in LBA order --------------------------
    ; The boot sector at 0, FAT1 from 1, FAT2 from BX, the root directory
    ; after it - ONE walk, each sector zeroed and then given what it holds.
    ; Sector 0 of a FAT is the only one with content in it - the media byte
    ; then all ones (entry 0), and entry 1's EOC mark - which is what a host
    ; fsck reads back to identify the volume. The root is zeroed whole: 0x00
    ; in a name byte is "this entry and every one after it is free", which is
    ; what disk_mount's walk stops on. The boot sector going FIRST and the
    ; root directory LAST is the header's order and is kept by walking up.
    mov bx, [cs:si+DFMT_FATSZ]
    inc bx                      ; BX = FAT2's first sector (FAT1's is 1)
    mov ax, [cs:si+DFMT_ROOTN]
    mov cl, 4
    shr ax, cl                  ; RootDirSectors = RootEntCnt/16, and every
    add ax, bx                  ; row here is a multiple of 16 by construction
    add ax, bx
    dec ax                      ; 1 + 2*FATSZ + RootDirSectors...
    xchg dx, ax                 ; ...DX = the first sector past the root
    xor di, di                  ; DI = the LBA
.meta:
    call dskw_fmt_zero          ; ...and ES = LOW_SEG, where dsk_secbuf is
                                ; (.lowbss, SPEC.md 2.1): every store below is
                                ; `es:`, and without it one lands in the
                                ; KERNEL at the same offset - which is exactly
                                ; how a FAT once went out all zeros
    or di, di
    jnz .notboot
    push bx                     ; --- the boot sector -----------------------
    mov di, dsk_secbuf          ; Byte for byte what tools/os88disk.py's
    mov bx, dskw_fmt_jmp        ; boot_sector() writes for a data floppy,
    mov cx, DFMT_T1             ; which is what makes a formatted disk and a
    call dskw_fmt_cpy           ; built image the same kind of object: the
    mov bx, si                  ; jump, 'MSDOS5.0' and BPB_BytsPerSec; the
    mov cl, DFMT_BPB            ; ROW, which IS the BPB from SecPerClus to
    call dskw_fmt_cpy           ; SecPerTrk; then the heads, BootSig 0x29 and
    mov bx, dskw_fmt_t2         ; a volume id, 'FAT12   ' and the
    mov cl, DFMT_T2             ; not-bootable stub. Every byte past the stub
    call dskw_fmt_cpy           ; is the zeroed scratch's.
    mov word [es:dsk_secbuf+510], 0xAA55    ; rule 1's signature, last
    pop bx
    xor di, di
    jmp short .put
.notboot:
    cmp di, 1
    je .head                    ; FAT1's first sector...
    cmp di, bx
    jne .put                    ; ...or FAT2's
.head:
    mov al, [cs:si+DFMT_MEDIA]
    mov [es:dsk_secbuf], al
    mov word [es:dsk_secbuf+1], 0xFFFF
.put:
    mov ax, di
    call dskw_fmt_put
    jc .ioerr
    inc di
    cmp di, dx
    jb .meta
    jmp dskw_fmt_probe_x.clcpop ; the same six pops
.ioerr:
    call dskw_fmt_ioerr             ; AX = FERR_IO / FERR_WPROT
    jmp dskw_fmt_probe_x.stcpop

;-----------------------------------------------------------------------------
; dskw_fmt_low - lay every track of the row in SI down fresh (SPEC.md 18.96.3)
; in:  SI = a dskw_fmt_tab row, [disk_drive] = the volume (a BIOS floppy: the
;      probe refused anything else); UI task, gfx lock held
; out: CF=0; CF=1 with [dsk_ioerr] = the BIOS status of the call that failed
; clobbers: AX, flags
;
; int 13h AH=05h, one call per track with a four-byte (C, H, R, N) address
; field per sector - which is what DOS's FORMAT does and what 18.96 once
; refused to, on the grounds that it can turn a disk this OS merely cannot
; read into one nothing can. That was the wrong way round for the disks
; people actually bring to a formatter: every one this OS could not reclaim
; was a disk it could not READ - HD recording taped for a DD drive, ID
; fields a failed write had damaged, a foreign filesystem on HD media - and
; only new tracks reclaim those. Laid at the row's own geometry, the result
; is a disk this drive reads by construction.
;
; **The track's sector count reaches the controller through the DPT**, not
; AL: the IBM ROM's FORMAT takes SC out of byte 4 (EOT) and the gap and
; fill out of bytes 7 and 8, so byte 4 is written for this row first, as
; dsk_xfer writes it before every transfer. On an AT-class BIOS, AH=18h
; (SET MEDIA TYPE FOR FORMAT) is asked first and its table used instead -
; that is the call that picks the DATA RATE, and a 720KB format in a 1.44MB
; drive at the wrong rate is not a 720KB disk. An XT ROM answers it with
; CF=1 and the kernel's own table stands. Either way the table in force
; before is put back after: it is int 1Eh's for the rest of the session
; (18.92).
;
; No per-track VERIFY (AH=04h). The IBM ROM checks the whole track's DMA
; extent against a 64KB page even though a verify moves nothing, and
; dsk_secbuf is 512 bytes; a buffer that could stand in would be a claim of
; its own. The metadata writes, the remount and 18.96.2's last-sector test
; are what check the result.
; -----------------------------------------------------------------------------
dskw_fmt_low:
    push bx
    push cx
    push dx
    push di
    push bp
    push es
    ; STKBALANCE-LOOP: .bank pushes six words a turn at a time and .rest pops the six; the count is in CX
    mov bx, dsk_dpt+10          ; the table in force, banked whole: DPT_LEN
    mov cx, 6                   ; is 11, so six words cover it - and the
.bank:                          ; twelfth byte is fdd_unit's, which nothing
    push word [bx]              ; on this path writes, so putting it back is
    dec bx                      ; putting back what was there. A loop and not
    dec bx                      ; six `push word [mem]`: 12 bytes against 24,
    loop .bank                  ; and the same again at .out
%if DPT_LEN > 12
  %error "dskw_fmt_low banks the DPT as six words"
%endif
    cld

    ; --- an AT BIOS picks the data rate for this medium --------------------
    push si
    mov ah, 0x18
    mov ch, [cs:si+DFMT_CYL]
    dec ch                      ; the LAST cylinder, as AH=18h takes it
    mov cl, [cs:si+DFMT_SPT]
    call .i13
    pop si
    jc .noset                   ; an XT ROM: the kernel's table stands
    push si
    push ds                     ; (the one to put back)
    push ds
    push es
    pop ds                      ; DS:SI = the BIOS's table for this medium...
    pop es                      ; ...ES:DI = int 1Eh's, which is ours (18.92)
    mov si, di                  ; - so the BIOS reads it from there
    mov di, dsk_dpt
    mov cx, DPT_LEN
    rep movsb
    pop ds
    pop si
.noset:
    mov al, [cs:si+DFMT_SPT]    ; SC for the FORMAT command, read from EOT
    mov [dsk_dpt+4], al

    ; --- the bar: the whole disk, a TRACK a unit (SPEC.md 12.8) -----------
    mov al, [cs:si+DFMT_CYL]
    cbw                         ; (a cylinder count is under 128)
    mov cx, 1024                ; two 512-byte units a cylinder, so .ok
    mul cx                      ; steps the bar one unit a track
    xchg ax, cx                 ; DX:CX = that, in bytes
    call KERNEL_SEG:fpg_begin

    xor bp, bp                  ; BP = the track: cylinder BP/2, head BP&1
.track:
    push ss                     ; the address fields go in dsk_secbuf, which
    pop es                      ; is 512-aligned (SPEC.md 2.1.1) and so
    mov ax, bp                  ; cannot straddle a DMA page
    shr ax, 1                   ; AL = C, AH = 0, CF = H...
    adc ah, 0                   ; ...so AX = C | H<<8: the first half of every
    push ax                     ; field, and DL/DH for every attempt below
    mov di, dsk_secbuf
    mov cx, [cs:si+DFMT_SPT]
    mov dx, 0x0201              ; R from 1, N = 2: 512 bytes
.id:
    stosw                       ; C, H
    xchg ax, dx
    stosw                       ; R, N
    xchg ax, dx
    inc dx                      ; R never reaches DH: SPT <= 18
    loop .id

    mov di, 3                   ; attempts at this track
.try:
    pop dx                      ; DL = C, DH = H
    push dx
    mov ch, dl                  ; CH = the cylinder
    mov cl, 1                   ; (CL is no diskette FORMAT's, and is set as
                                ; dsk_xfer would so no ROM sees sector 0)
    mov bx, dsk_secbuf          ; ES:BX = the address fields
    mov al, [cs:si+DFMT_SPT]
    mov ah, 0x05
    call .i13
    jnc .ok
    mov [dsk_ioerr], ah
    cmp ah, 0x03
    je .fail                    ; write-protected: retrying cannot help
    xor ah, ah                  ; reset, and try the track again
    call .i13
    dec di
    jnz .try
.fail:
    pop dx
    stc
    jmp short .out
.ok:
    pop dx
    mov ax, 512                 ; one unit: this track
    call KERNEL_SEG:apic_fpg_stepb
    inc bp
    mov ax, bp
    shr ax, 1                   ; AL = the next track's cylinder
    cmp al, [cs:si+DFMT_CYL]
    jb .track                   ; ...and CF = 0 falling out of it
.out:
    call KERNEL_SEG:fpg_end     ; preserves the flags (fprog.inc)
    mov bx, dsk_dpt             ; the table back: `mov`, `pop`, `inc` and
    mov cx, 6                   ; `loop` all leave CF alone
.rest:
    pop word [bx]
    inc bx
    inc bx
    loop .rest
    pop es
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    ret

    ; --- one int 13h at [disk_drive]'s unit, in dsk_xfer's bracket ---------
    ; in: AH = the function and its other registers but DL. out: the BIOS's.
    ; DL is looked up here rather than carried, because it is every call's
    ; and the BIOS need not give it back. [sch_lock] because nothing switches
    ; under an int 13h (SPEC.md 18), and CURXFER because the arrow may move
    ; through it (SPEC.md 7.4) - the reset included, as dsk_xfer's is; `mov`
    ; and `dec` leave CF alone, so the BIOS's answer reaches the caller
.i13:
    push bx
    mov bl, [disk_drive]
    call COLD_SEG:dskf_dsk_vol_row
    mov dl, [bx+DV_UNIT]
    pop bx
    inc byte [sch_lock]
    CURXFER_ON
    int 0x13
    CURXFER_OFF
    dec byte [sch_lock]
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_put - write the staged sector to LBA AX
; in:  AX = LBA, dsk_secbuf staged, ES = LOW_SEG (dskw_fmt_zero leaves it)
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
    push bx
    push cx
    mov bx, dsk_secbuf
    mov cx, 1
    call COLD_SEG:dskf_disk_write   ; (which preserves AX: dsk_xfer is KENT)
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_zero - blank the staging sector
; in:  nothing
; out: ES = LOW_SEG, where dsk_secbuf is - and every caller's next store is
;      into it, so ES is an OUTPUT rather than a register to bank (both callers
;      bank the caller's ES at their own entry); DF = 0. All else preserved.
; -----------------------------------------------------------------------------
dskw_fmt_zero:
    push ax
    push cx
    push di
    mov ax, LOW_SEG
    mov es, ax
    mov di, dsk_secbuf
    xor ax, ax
    mov cx, 256
    cld
    rep stosw
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dskw_fmt_reach_x - can this drive actually reach the volume just declared?
; in:  AL = the dskw_fmt_tab row that was written, [disk_drive] = the volume,
;      [disk_spt]/[disk_heads] still the format's (dskw_format_x leaves them)
; out: CF=0 = the last sector was written and read back intact; CF=1 = it was
;      not. Clobbers AX, flags.
;
; SPEC.md 18.96.2. The size on the confirmation can be the USER's rather than
; the probe's, and a 720KB pick on a 40-cylinder drive is the one wrong answer
; that stays silent: the whole layout is boot + 6 FAT + 7 root = LBA 0..13,
; all of it inside CYLINDER 0, so the format completes, the volume mounts and
; the disk lies about its size until something writes past cylinder 39 - by
; which time it is mountable, so 22.12's predicate has greyed Format Disk out
; and there is no way back.
;
; **The LAST sector, not the 360KB boundary.** LBA 720 is cylinder 40, the
; first sector a 360KB disk has not got, and it is too weak a test: plenty of
; 40-track drives step to 41 or 42, so a probe there PASSES on exactly the
; drive it exists to catch. No 40-cylinder mechanism reaches cylinder 79, it
; is one seek either way, and it validates the extent the BPB just claimed.
;
; **It writes, and that is what makes it a POSITIVE test** (18.96.1's rule:
; every test is a read SUCCEEDING). A bare read of cylinder 79 breaks that
; twice - it rests on a failure, and a head stopped at track 39 can answer
; with track 39's data. A marker written first removes both, and the buffer
; is re-zeroed BETWEEN the write and the read so a read that never touched it
; cannot compare equal against our own staging.
;
; It destroys the sector it tests, which is free here and nowhere else: the
; only caller is a commit the user has already agreed to erase the disk for.
; -----------------------------------------------------------------------------
dskw_fmt_reach_x:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call dskw_fmt_row_x
    mov di, [cs:si+DFMT_TOT]
    dec di                      ; DI = the last sector the BPB now claims
    call dskw_fmt_zero          ; ...and ES = LOW_SEG: dsk_secbuf is .lowbss
                                ; (SPEC.md 2.1), so the marker goes in
    mov word [es:dsk_secbuf+0], 'o8'    ; through ES and never through DS -
    mov word [es:dsk_secbuf+2], '8R'    ; dskw_format_x's own trap
    mov [es:dsk_secbuf+4], di   ; ...and the LBA, so a seek that landed on a
                                ; DIFFERENT track cannot match by accident
    mov ax, di
    call dskw_fmt_put
    jc .no
    call dskw_fmt_zero          ; a read that never happened must not compare
                                ; equal against what we staged for the write
    mov ax, di
    mov bx, dsk_secbuf
    mov cx, 1
    call COLD_SEG:dskf_disk_read
    jc .no
    cmp word [es:dsk_secbuf+0], 'o8'
    jne .no
    cmp word [es:dsk_secbuf+2], '8R'
    jne .no
    cmp [es:dsk_secbuf+4], di
    jne .no
    jmp dskw_fmt_probe_x.clcpop ; the same six pops
.no:
    jmp dskw_fmt_probe_x.stcpop

; -----------------------------------------------------------------------------
; dskw_fmt_cpy - CX bytes from CS:BX to ES:DI (module-internal)
; in:  BX = source IN THIS IMAGE, ES:DI = destination, CX = count, DF = 0
; out: BX and DI past the copy, CX = 0 - so the boot sector is three calls
;      that each start where the last ended; AL clobbered
;
; IT WAS `rep movsb` FROM `.text`, and the blocks it copies are now in the
; image with it (SPEC.md 2.8.6): they are bytes written into a SECTOR and
; nothing ever draws them, so the ordering rule has no window to fail in.
; What that costs is the instruction: a `cs rep movsb` is refused on this
; machine because an 8086 drops the segment prefix if an interrupt lands
; mid-`rep` (SPEC.md 5.9), and the rest of a boot sector would then come from
; whatever is at that offset in KERNEL_SEG - which assembles, runs, and writes
; a disk that SPEC.md 18.2 rule 2 will not mount. So it is a byte loop
; (kernel/vga12.inc's is the other site that obeys the rule).
; -----------------------------------------------------------------------------
dskw_fmt_cpy:
    mov al, [cs:bx]
    inc bx
    stosb
    loop dskw_fmt_cpy
    ret

                                ; DATA, and it STAYS IN THE IMAGE (SPEC.md
                                ; 2.8.6): nothing here is ever drawn, it is
                                ; bytes written into a sector, so the ordering
                                ; rule is satisfied by construction - there is
                                ; no window in which something wants these
                                ; while FORMAT.DRV is not loaded, because the
                                ; only thing that wants them is the format
                                ; itself. dskw_fmt_cpy above reads them
                                ; through CS.
; THE BOOT SECTOR, as two templates either side of the row (dskw_format_x):
; bytes 0-12 here, 13-25 the dskw_fmt_tab row, and 26 onward the second
; template - which runs on into the stub, so the boot sector is three copies.
; Every field NOT written is the zeroed scratch's, and right: nothing past the
; stub, and nothing a floppy wants non-zero.
dskw_fmt_jmp: db 0xEB, 0x3C, 0x90   ; jmp short 0x3E / nop - and SPEC.md 18.2
                                ; rule 2 tests this first byte for 0EBh/0E9h,
                                ; so a sector without it would not mount the
                                ; volume this has just finished writing
              db 'MSDOS5.0'     ; interop OEM name, os88disk.py's
              dw 512            ; BPB_BytsPerSec
DFMT_T1 equ $ - dskw_fmt_jmp
dskw_fmt_t2:  dw 2              ; BPB_NumHeads: every row is double-sided
              dd 0, 0           ; BPB_HiddSec, BPB_TotSec32
              db 0, 0           ; BS_DrvNum, BS_Reserved1: int 13h is told the
                                ; drive by dsk_xfer, never by this
              db 0x29           ; BS_BootSig: VolID/Lab/Type follow
              dw 0x0888, 0x8800 ; BS_VolID, os88disk.py's own - a fixed
                                ; serial, so two disks formatted here are as
                                ; alike as two images built here (18.8.2's
                                ; accepted residual)
              db 'NO NAME    '  ; BS_VolLab: DOS's own word for a volume
                                ; nobody has named - which is what this is,
                                ; since no label ENTRY is written either
              db 'FAT12   '     ; BS_FilSysType

; The not-bootable stub, byte for byte os88disk.py's BOOT_STUB: set up DS,
; print the message with int 10h AH=0Eh, wait for a key and int 19h. It is 44
; bytes and it is the difference between a machine that says what is wrong
; and one that executes a root directory.
    db 0x31, 0xC0, 0x8E, 0xD8, 0xBE, 0x59, 0x7C, 0xAC
    db 0x08, 0xC0, 0x74, 0x09, 0xB4, 0x0E, 0xBB, 0x07
    db 0x00, 0xCD, 0x10, 0xEB, 0xF2, 0x30, 0xE4, 0xCD
    db 0x16, 0xCD, 0x19
    db 0x0D, 0x0A, 'Not a bootable disk. Press any key.', 0x0D, 0x0A, 0
DFMT_T2 equ $ - dskw_fmt_t2
%if DFMT_T1 != DSK_B_SPC || DFMT_T1 + DFMT_BPB != DSK_B_HEADS || DFMT_T2 > 255 || DSK_B_HEADS + DFMT_T2 > 510
  %error "the boot sector is template 1, a dskw_fmt_tab row, then template 2 and the stub - contiguous, and short of the signature"
%endif

%endif                              ; KD_BUILD
section .text                   ; ...and back to the kernel segment for what
                                ; follows (CLAUDE.md 1: a file ends in .text)
section .cold                   ; ...and the RESIDENT stubs (SPEC.md 2.8.1)
;
; Four public names, unchanged, so not one call site in files.inc moved. Each
; loads the image if it is not there and far-calls into it; each REFUSES the
; way its body would have refused, because a caller that has to learn a new
; failure mode is a caller that has to be found.
;
; There is no resident dskw_fmt_row. SPEC.md 2.8.6 moved the status line's
; composer INTO the image as dskw_fmt_line_x, which calls dskw_fmt_row_x
; directly, and nothing outside the image has asked for a row since.
; dskw_fmt_load - the four stubs' shared prologue: is the image in RAM?
; out: CF=1 it is not. AX and every other register preserved - `pop ax` and
;      `ret` write no flag, so mod_need's CF reaches the caller intact
dskw_fmt_load:
    push ax
    mov al, MOD_FMT
    call mod_need
    pop ax
    ret

dskw_fmt_probe:
    call dskw_fmt_load
    jc .p_no
    call far [FMFP + FME_PROBE*4]
    ret
.p_no:
    mov ax, FERR_NODISK         ; dskw_fmt_probe_x's own refusal for a medium
    ret                         ; it cannot read - the nearest true thing, and
                                ; the toast names the disk either way. NO stc:
                                ; the only way in is the `jc` above, and
                                ; `mov r16, imm16` (B8 iw) writes no flag -
                                ; which this file already states in two
                                ; headers for `pop ax` and `ret`

dskw_format:                    ; ...AND THE SAME FIVE BYTES, so the refusal
    call dskw_fmt_load          ; is dskw_fmt_probe's. The two routines are
    jc dskw_fmt_probe.p_no      ; fifteen bytes apart, so the jc is short -
    call far [FMFP + FME_FORMAT*4]  ; and their ORDER in this file is now
    ret                         ; load-bearing

dskw_fmt_reach:
    call dskw_fmt_load
    jc .r_no                    ; "the volume's last sector was NOT reached",
    call far [FMFP + FME_REACH*4]   ; which is what an absent formatter has to
.r_no:                          ; say - and CF is already set on the one way
    ret                         ; in, so the refusal is nothing but the `ret`
                                ; the success path was going to take anyway

; dskw_fmt_line - the formatter's own status line (SPEC.md 2.8.6)
;
; dskw_fmt_row's twin in every respect, including the one that matters: it is
; called from the STATUS LINE's composer, once per repaint while a format
; prompt is up, and the prompt can only have been armed by a probe that
; already loaded the image. Its refusal writes an empty string, so a line
; composed after the image somehow went away is BLANK rather than rubbish.
dskw_fmt_line:
    call dskw_fmt_load
    jc .l_no
    call far [FMFP + FME_LINE*4]
    ret
.l_no:
    mov byte [di], 0
    ret

; fm_fmt_drop - the formatter is finished with (SPEC.md 2.8.3)
;
; Called from every path that ends a format confirmation, which is the one
; moment nothing is on the module's stack and nothing will call it again.
fm_fmt_drop:
    push ax
    mov al, MOD_FMT
    call mod_drop
    pop ax
    ret


; -----------------------------------------------------------------------------
section .bss

; --- the located / staged directory entry ------------------------------------
%ifdef DSKW_WSEQ
dws_rec:      resb DWS_RECW*2   ; WRITE_SEQ's ONE stream record (18.4.9): the
                                ; entry's head and size, where the entry is,
                                ; and the file's last cluster (the held tail),
                                ; as the stream's last call left them...
dws_last      equ dws_rec + DWS_RECW*2 - 4
dws_hfirst    equ dws_rec + DWS_RECW*2 - 2   ; the HELD chain's head: .hmore
                                ; puts it back in dskw_first after every call
dws_vol:      resb 1            ; ...and its volume. The held stream's too
dws_hold:     resb 1            ; 1 = a HELD stream is pending (dws_commit),
                                ; 0FFh = pending and POISONED (dws_hlose)
dws_flg:      resb 1            ; inside WRITE_SEQ: WSQF_*. ADJACENT to
                                ; dws_hold: the gates read one word
dws_hlink:    resw 1            ; the file's committed last cluster; 0 = empty
%endif
dskw_raw:     resb 32           ; one raw 32-byte entry, staged in the kernel
                                ; segment (the write path's dsk_ent)
dskw_dsec:    LBARES 1            ; the found entry's sector LBA, 32 bits...
dskw_doff:    resw 1            ; ...and its offset within that sector
dskw_prev:    resw 1            ; dskw_wdata: the cluster before the one being
                                ; written, awaiting its link - and at the end,
                                ; the chain's LAST...
dskw_first:   resw 1            ; ...and the new chain's head (0 = empty file).
                                ; Here and not beside dskw_cur so that dws_rec
                                ; takes both
dskw_fsec:    LBARES 1            ; the first reusable slot's, high word FFFFh
                                ; = none (SPEC.md 18.7.5)
dskw_foff:    resw 1
%if dskw_doff - dskw_dsec != DSKW_LBASZ || dskw_foff - dskw_fsec != DSKW_LBASZ
    %error "each slot's offset must follow its LBA: dskw_here stores both"
%endif
%ifdef DSKW_WSEQ
%if dskw_first + 2 - DWS_REC0 != DWS_RECW*2 || dskw_dsec != dskw_raw + 32
    %error "dws_load moves the entry's head and size, dskw_dsec, dskw_doff, dskw_prev and dskw_first as ONE run: keep them adjacent"
%endif
%endif
dskw_n83:     resb 11           ; the 11-byte raw name being looked for
dskw_n83b:    resb 11           ; the rename target's, held across the scan
                                ; (a dskw_pad was here, for the alignment an
                                ; 8-bit bus does not charge for. dskw_n83b
                                ; above is a dskw_copy11 target and
                                ; dskw_copy11 is `mov cx, 11` + dsk_ncopy,
                                ; which is a BYTE loop - so no neighbour's
                                ; store reached into it. Its sibling
                                ; dskw_pad2 below took a narrowing first)
dskw_cursec:  resw 1          ; dskw_find: the sector being scanned (its
                                ; high word is [dsk_c2hi], SPEC.md 18.7.5)
dskw_fend:    resb 1            ; 1 = that free slot is the 00h terminator
dskw_new:     resb 1            ; 1 = this write is creating the entry
dskw_zapnext: resb 1            ; 1 = re-zero the slot after the one written.
                                ; (dskw_isdir is NOT here - see .text above.)
                                ; A `dskw_pad2` USED TO FOLLOW, and every
                                ; grep in the tree called it dead - the
                                ; declaration was its only mention. It was
                                ; not: :2717 was the one
                                ; `mov word [dskw_zapnext], 0` of six stores,
                                ; so a neighbour's WORD store was zeroing the
                                ; pad and deleting it would have put that zero
                                ; into dskw_seg's low byte, in the middle of
                                ; the write path. Narrowing that one store to
                                ; `mov byte` is what made the pad free, and
                                ; the pad then went. THE ORDER MATTERED and
                                ; the two halves have to stay together.

; --- the transfer in progress -------------------------------------------------
; dskw_src FIRST and dskw_seg right behind it, IN THAT ORDER: dskw_czexp
; loads the pair with one `lds si, [dskw_src]` (SPEC.md 20.14.2)
dskw_src:     resw 1            ; the caller's buffer offset, 0..15 after
                                ; dskw_norm has folded the rest into the
                                ; segment - HELD for the whole transfer,
                                ; because the segment is what moves (SPEC.md
                                ; 18.4.1)...
dskw_seg:     resw 1            ; ...and that segment (ES at entry), right
                                ; behind it for the `lds`
dskw_wseg:    resw 1            ; the advancing source/destination segment
                                ; (+32 paragraphs per 512-byte sector)
%if dskw_wseg - dskw_seg != 2   ; ADJACENT ON PURPOSE: memory.inc's mem_rr_tab
    %error "dskw_seg and dskw_wseg must stay adjacent (mem_rr_tab names them as one row)"
%endif                          ; names the pair as ONE row of two words
dskw_len:     resw 1            ; the file's byte count, low word...
dskw_lenhi:   resw 1            ; ...and high (the DX:AX answer on a read)
dskw_cap:     resw 1            ; the destination's capacity, low word...
dskw_caphi:   resw 1            ; ...and high
%ifdef OS88_SHORTCUTS
dskw_trunc:   resb 1            ; 1 = read a PLAIN file's first [dskw_cap]
                                ; bytes rather than refuse it as too big -
                                ; mod_need's settings core, for one read
                                ; (SPEC.md 2.8.7). Zero at boot, like .bss
%endif
dskw_rem:     resw 1            ; sectors still to move
dskw_tail:    resw 1            ; bytes in the partial final sector (0 = none)
dskw_rlba:    LBARES 1            ; the pending data run: its first LBA, 32
                                ; bits (SPEC.md 18.7.5)...
dskw_rlen:    resw 1            ; ...how many sectors are in it (0 = none)...
dskw_rcap:    resw 1            ; ...and the most it may ever hold
dskw_clba:    LBARES 1            ; the whole-sector walk's cursor inside a take
dskw_fop:     resb 1            ; 0 = read, 1 = write, for the ONE flush body
                                ; the two data walkers share (SPEC.md 18.4.2)
                                ; - dsk_op's idiom, and for dsk_op's reason:
                                ; the run arithmetic must not be able to
                                ; differ between reading and writing
dskw_take:    resw 1            ; sectors this cluster contributes
dskw_cur:     resw 1            ; the cluster being written / read
dskw_rt_home:  resw 1           ; dskw_rmtree: the directory it was called in
dskw_rt_top:   resw 1           ; ...the folder it was asked to remove
dskw_rt_clus:  resw 1           ; ...the entry the scan last named
dskw_rt_guard: resw 1           ; ...iterations left (DSKW_RT_MAX)
dskw_rt_attr:  resb 1           ; ...that entry's attribute byte
dskw_rt_ioerr: resb 1           ; ...the scan ended on a read error
dskw_rt_name:  resb 12          ; ...the caller's name as 11 RAW bytes (the
                                ; 12th keeps the buffer even-sized)
dskw_oldclus: resw 1            ; the replaced chain's head, freed after the
                                ; commit (0 = there was none)
dskw_uz:      resw 1            ; a COMPRESSED read (SPEC.md 20.14): what the
dskw_uzhi:    resw 1            ; entry's hint says the file expands to, 32
                                ; bits of it, and what the caller is told it
                                ; read. BEVERLY.MOD is 116,085 bytes, which is
                                ; the whole reason this is not one word
dskw_czseg:   resw 1            ; ...and the segment it expands INTO - the
                                ; caller's own base, banked before R is folded
                                ; into [dskw_seg]
dskw_date:    resw 1            ; dskw_now: the FAT-encoded date...
dskw_time:    resw 1            ; ...and time
; --- dskw_read_at's arguments, banked (SPEC.md 18.4.4) -----------------------
; Module scratch on dsk_op's terms: the disk is the UI task's (SPEC.md 18) and
; a transfer holds sch_lock, so nothing can be inside this twice.
dwr_buf:      resw 1            ; the caller's buffer offset (ES is theirs)
dwr_cap:      resw 1            ; ...and its capacity in bytes
%ifdef DSK_STREAM               ; ...TWO OF THEM ARE READ_SEQ'S CURSOR on
dwr_off     equ dwr_c + FSEQ_OFF ; kern_big (SPEC.md 18.4.8): the offset and
dwr_sz      equ dwr_c + RSQ_SIZE ; the size are the cursor's own fields, so
%else                           ; READ_AT leaves them where the wrapper wants
dwr_off:      resd 1            ; them. Outside a READ_SEQ call [dwr_c]'s
dwr_sz:       resd 1            ; generation is 0 and these are plain scratch,
%endif                          ; which is all READ_AT and WRITE_AT ask. The
                                ; CLUSTER is not one of them and must never
                                ; be: +6 is the wrapper's, written on success
                                ; only, or a FAILED read hands back a cursor
                                ; already one link on (dskw_read_at's .atclus)
dwr_clb:      resw 1            ; bytes per cluster on this volume
dwr_take:     resw 1            ; bytes this call will deliver
dwr_end:      resd 1            ; dskw_write_at: offset + count (SPEC.md
                                ; 18.4.7.2), which decides both whether the
                                ; tail may round and whether the size moves

section .text

section .cold                   ; far entries (SPEC.md 2.6): the resident
                                ; thunks in kernel.asm far-call these, and
                                ; these near-call the bodies above - so every
                                ; routine here keeps its ordinary near `ret`
dwf_dskw_delete:        call dskw_delete_x
                    retf
dwf_dskw_dfree:         call dskw_dfree_x
                      retf
%ifndef KERN_SMALL
dwf_dskw_vstat:         call dskw_vstat_x
                    retf
%endif
dwf_dskw_read:          call dskw_read_x
                    retf
dwf_dskw_rename:        call dskw_rename_x
                    retf
dwf_dskw_stat:          call dskw_stat_x
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
%ifndef KERN_SMALL
dwf_dskw_write_at:      call dskw_write_at_x
                    retf
%ifdef KERN_BIG
%ifndef KD_BUILD
dwsf_commit:            push ax         ; gfx_unlock's (SPEC.md 18.4.9): every
                        call dws_commit ; register kept, the answer dropped -
                        pop ax          ; an unlock has nobody to tell
                    retf
%endif
%endif
%else                           ; (kern_big's READ_SEQ door is its body)
dwsf_dskw_write_seq:            ; ...and the cell's fenced way in to it
dwf_dskw_write_seq:             ; SPEC.md 18.4.9: kern_big's alone too
dwf_dskw_read_seq:  mov ax, FERR_NAME   ; SPEC.md 18.4.8: kern_big's alone, and
                                        ; the cell is in both kernels (20.8
                                        ; rule 4) - falling into the refusal
dwf_dskw_write_at:  stc         ; the DOS box is the one caller (SPEC.md
                    retf        ; 18.4.7.4) and it does not ship on the small
                                ; disks: the INSIDE arm, dsk_write_chain and
                                ; this door are kern_big's, and an append
                                ; still has the whole body. Cold, because the
                                ; N cell reaches the far entry by name
                                ; (SPEC.md 20.3.2)
%endif
dwf_dskw_append_sys:    call dskw_append_sys_x
                    retf
; THE AL DISPATCH IS HERE, in the COLD far entry, and not in a second resident
; thunk: both bodies are already cold, so branching here costs one far entry
; and one `.text` thunk instead of two of each (SPEC.md 18.6).
%ifndef KD_BUILD            ; ...and AL=1 is the tree arm, gated with it
dwf_dskw_rmany:         or  al, al
                    jz  .empty
                    call dskw_rmtree
                    retf
.empty:             call dskw_rmdir
                    retf
%endif                      ; KD_BUILD - the file manager is the only caller
                            ; and no kerndos root has one

section .text
