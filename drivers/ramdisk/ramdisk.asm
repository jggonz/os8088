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
; WHY THIS EXISTS, AND WHY IT IS NOT THE CABLE. SPEC.md 62.9.3 is explicit:
; the harness for the redirector is a RAM DISK and not NET.DRV. A RAM disk
; needs no hardware, so every branch site the redirector added to the kernel
; runs on a cycle-accurate 8088 in a container - which is the one thing block
; mode never had, and the reason two of its bugs reached the field (docs/
; NET-PLAN.md 1.2.0). It has since found four more, in three milestones.
;
; THE STORE IS A HEAP ARENA OF FIXED EXTENTS, and that is a deliberate choice
; of the SIMPLEST thing that is HONEST rather than the cleverest thing that
; fits. One extent per file, RD_EXTKB each: allocation is a scan of a byte
; array, delete frees one, append extends inside one, and FSV_DFREE is a
; count times a constant - EXACT, where a bump allocator would have had to
; either lie about free space or grow a compactor. What it costs is a hard
; per-file ceiling of RD_EXTKB, which is honest in the other direction: a file
; that will not fit is refused with FERR_BIG before a byte moves.
;
; ES IS KERNEL_SEG ON ENTRY TO EVERY CALLBACK (SPEC.md 20.1). Every string
; instruction in this file therefore loads its own segments and puts them
; back; a bare `rep stosw` here writes into the kernel's own image, which is
; the trap apps/modplug shipped with until it was caught.
; =============================================================================

%include "os88drv.inc"

    OS88_DRIVER 'Ram Disk', DRVC_FILE, rd_entry

RD_EXTKB    equ 2               ; KB per extent, and so the largest file
RD_EXTB     equ RD_EXTKB * 1024
RD_NEXT     equ 16              ; extents -> a 32KB arena
RD_ARENAKB  equ RD_EXTKB * RD_NEXT
RD_MAXENT   equ 20              ; directory rows. The kernel's own listing cap
                                ; is 32 (SPEC.md 19), so this is the tighter
                                ; of the two and the one that answers first
RD_NOEXT    equ 0xFF            ; RDE_EXT: no content (a folder, or empty)
RD_FREE     equ 0xFF            ; RDE_TYPE: this row is not in use

; --- a directory row ---------------------------------------------------------
; THE HANDLE IS THE ROW INDEX PLUS ONE, which is what makes rd_byhand one
; multiply instead of a scan and 0 a natural "the root". It behaves exactly as
; a first-cluster word does on a FAT volume - reused after a delete, so a
; handle held across one names whatever took the row - and the kernel is
; documented not to care (SPEC.md 62.9.1: it is opaque).
RDE_PAR     equ 0               ; db: the parent's handle, 0 = the root
RDE_TYPE    equ 1               ; db: 0 file / 1 package / 2 folder / RD_FREE
RDE_EXT     equ 2               ; db: its extent, or RD_NOEXT
RDE_SIZE    equ 4               ; dd: bytes
RDE_NAME    equ 8               ; db[16]: NUL-terminated 8.3 display name
RDE_SZ      equ 24

; --- the seed tree, in the image, copied into the arena at attach ------------
; Declared before the table that names it so every reference below is
; BACKWARDS: the row macro pads with `times`, and a forward reference inside a
; `times` count is the one thing NASM cannot resolve in one pass.
;
; MINES.O88 is the real package, embedded from build/ (the Makefile passes
; -I build). It is what makes "a package launches off a redirected volume" a
; thing that can be clicked rather than argued - and it proves 62.9.2's other
; half by accident: MINES has an EMBEDDED ICON and draws with 25's generic one
; here, because a redirected volume has no first sector to harvest from.
%ifdef RDSEED                   ; ...the seeded volume: `make ramseed` (62.9.5.1)
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

; NEVER END A LINE IN A TABLE LIKE THIS WITH A BACKSLASH: NASM treats a
; trailing `\` as a LINE CONTINUATION even inside a comment, so an ASCII-art
; brace down the right-hand side silently swallows the row below it. That
; shipped here once and presented as an unrelated window refusing to open
; (SPEC.md 62.9.4). The %if guards below are what make it impossible to repeat.
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
%else
; NOT SEEDED, which is what ships (SPEC.md 62.9.5.1). The files were a
; harness for the redirector's branch sites and cost the FLOPPY 1.8KB of
; driver image to carry a copy of MINES.O88 nobody asked for; the volume
; mounts empty, which every path in the kernel already handles because a
; refused arena produces exactly that. `make ramseed` builds the populated
; one for debugging.
RD_NSEED    equ 0
%endif

%ifdef RDSEED
  %if rd_d_mines_end - rd_d_mines > RD_EXTB
    %error "ramdisk: the seeded package does not fit one extent"
  %endif
%endif
%if RD_NSEED > RD_MAXENT
  %error "ramdisk: more seeds than directory rows"
%endif

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
; probed, nothing is hooked and no vector is taken. THE ARENA IS NOT CLAIMED
; HERE either, and that is the rule rather than caution - a refused claim
; would have to un-hook an attach that is documented to have hooked nothing.
; It is claimed at DRVV_READY, where a refusal is an ordinary state.
; -----------------------------------------------------------------------------
rd_attach:
    mov byte [rd_cwd], 0
    mov si, rd_svc
    clc
    ret

; -----------------------------------------------------------------------------
; DRVV_READY - the earliest point a fence keyed on our publication answers
;              (SPEC.md 51.2.2), so this is where the arena and the volume go
;
; A REFUSED ARENA IS NOT A FAILED ATTACH. The volume still mounts and still
; browses - it just has no store, so every write verb answers FERR_FULL and
; the seeded tree is empty. That is SPEC.md 50.3's rule (a refusal is a normal
; path with a fallback) rather than a driver that vanishes on a small machine.
; -----------------------------------------------------------------------------
rd_ready:
    cmp byte [rd_vol], 0xFF     ; DRVV_READY is sent once, but a driver that
    jne .out                    ; re-attached must not add a second volume
    call rd_arena_get
    call rd_seed_all
    xor al, al                  ; our own volume handle: there is one RAM disk
    xor cx, cx                  ; no sector count - there are no sectors
    xor dx, dx                  ; no donated listing claim: the .lowbss floor
    mov si, rd_label
    call OSAPI_VOL_ADD
    jc  .norow                  ; the table is full: there is no volume to
                                ; reach the arena through, so it goes back
                                ; here rather than sitting on the heap until
                                ; the user unticks a driver that does nothing
    mov [rd_vol], al
    call OSAPI_VOL_MOUNT        ; AL survives: it is the volume index
    jmp short .out
.norow:
    cmp word [rd_arena], 0
    je .out
    mov dx, [rd_arena]
    call OSAPI_MEM_FREE
    mov word [rd_arena], 0
.out:
    clc                         ; a refused mount is the Drivers page's to
    ret                         ; report, not a reason to fail the attach

; -----------------------------------------------------------------------------
; DRVV_DETACH - cannot fail (SPEC.md 51.6 rule 1)
;
; The kernel drops our volume itself before it frees the image, and since
; DV_CLASS it drops OURS rather than every driver-backed row (SPEC.md 51.8).
; The arena is given back explicitly even though drv_release would sweep it:
; a claim owned by a segment that is about to be freed is exactly the thing
; SPEC.md 51.6 says to hand over before returning, not after.
; -----------------------------------------------------------------------------
rd_detach:
    mov byte [rd_vol], 0xFF
    cmp word [rd_arena], 0
    je .none
    mov dx, [rd_arena]
    call OSAPI_MEM_FREE
    mov word [rd_arena], 0
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; rd_arena_get / rd_seed_all - build the store (internal, DRVV_READY only)
; -----------------------------------------------------------------------------
rd_arena_get:
    mov ax, RD_ARENAKB
    call OSAPI_MEM_CLAIM        ; owner = our segment, so drv_release sweeps
    jc .none                    ; it even if detach somehow does not
    mov [rd_arena], dx
    ret
.none:
    mov word [rd_arena], 0
    ret

; Every row starts free, then the seeds are laid in ORDER so that row i is
; seed i and the handles the seed table's parent column names come out right.
rd_seed_all:
    push si
    push di
    push cx
    mov di, rd_dir              ; every row free first
    mov cx, RD_MAXENT
.blank:
    mov byte [di+RDE_TYPE], RD_FREE
    add di, RDE_SZ
    loop .blank

    cmp word [rd_arena], 0
    je .out                     ; no store: the volume mounts empty, which is
                                ; a state the whole kernel already handles
%if RD_NSEED                    ; ...and it ASSEMBLES AWAY when there are no
                                ; seeds rather than counting zero: `loop` with
                                ; CX = 0 decrements to 0xFFFF and runs 65,536
                                ; times, which is the shape a `jcxz` would be
                                ; guarding against - and there is no reason to
                                ; carry the instructions at all
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
%endif
.out:
    pop cx
    pop di
    pop si
    ret

; rd_seed_one - SI = a seed row, DI = its directory row
;
; Inside the gate with the seed data it reads: RDS_DATA and RDS_SIZE are the
; SEED row's offsets and exist only when there are seed rows, and nothing else
; calls this (SPEC.md 62.9.5.1).
%if RD_NSEED
rd_seed_one:
    push ax
    push bx
    push cx
    push dx
    mov al, [si+RDE_PAR]
    mov [di+RDE_PAR], al
    mov al, [si+RDE_TYPE]
    mov [di+RDE_TYPE], al
    mov byte [di+RDE_EXT], RD_NOEXT
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
    call rd_ext_alloc           ; AL = a free extent
    jc .out                     ; ...and a seed that cannot get one simply is
    mov [di+RDE_EXT], al        ; not there, which is better than half there
    mov [di+RDE_SIZE], cx
    mov bx, [si+RDS_DATA]       ; the content, in this image
    call rd_ext_off             ; AX = the extent's offset in the arena
    call rd_blit_in             ; DS:BX -> arena:AX, CX bytes
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; =============================================================================
; ARENA AND DIRECTORY HELPERS
; =============================================================================

; rd_ext_off - AL = an extent index -> AX = its byte offset in the arena
;
; DX IS SAVED, AND THAT IS THE WHOLE POINT OF THE PUSH. `mul` writes DX:AX,
; and DX is THE DESTINATION SEGMENT at both call sites that matter - so
; without this every read handed rd_blit_user a segment of 0 and wrote the
; file over the interrupt vector table. It presented as the machine running
; away in the heap some time later, with the operation itself reporting
; success, which is the worst shape a bug can have: the damage and the symptom
; were in different subsystems.
%if RD_NEXT * RD_EXTB > 0xFFFF
  %error "ramdisk: the arena does not fit one segment - rd_ext_off would wrap"
%endif
rd_ext_off:
    push cx
    push dx
    xor ah, ah
    mov cx, RD_EXTB
    mul cx                      ; DX:AX, and the %if above keeps DX at 0
    pop dx
    pop cx
    ret

; rd_ext_alloc - claim a free extent. out CF=0 and AL = it; CF=1 = full
rd_ext_alloc:
    push bx
    xor bx, bx
.scan:
    cmp bx, RD_NEXT
    jae .full
    cmp byte [rd_ext+bx], 0
    je .got
    inc bx
    jmp short .scan
.got:
    mov byte [rd_ext+bx], 1
    mov ax, bx
    pop bx
    clc
    ret
.full:
    pop bx
    stc
    ret

; rd_ext_free - AL = an extent index (RD_NOEXT = nothing to do)
rd_ext_free:
    push bx
    cmp al, RD_NOEXT
    je .out
    mov bl, al
    xor bh, bh
    cmp bx, RD_NEXT
    jae .out
    mov byte [rd_ext+bx], 0
.out:
    pop bx
    ret

; rd_row - AL = a handle -> CF=0 and SI = its row; CF=1 = no such handle
; A free row answers CF=1, so a handle held across a delete cannot resolve.
rd_row:
    push ax
    or al, al
    jz .no
    cmp al, RD_MAXENT
    ja .no
    dec al
    xor ah, ah
    push cx
    push dx
    mov cx, RDE_SZ
    mul cx
    pop dx
    pop cx
    mov si, ax
    add si, rd_dir
    cmp byte [si+RDE_TYPE], RD_FREE
    je .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; rd_hand - SI = a row -> AL = its handle (index + 1)
rd_hand:
    push dx
    push cx
    mov ax, si
    sub ax, rd_dir
    xor dx, dx
    mov cx, RDE_SZ
    div cx
    inc ax
    pop cx
    pop dx
    ret

; rd_free_row - out CF=0 and DI = a free row; CF=1 = the directory is full
rd_free_row:
    push cx
    mov di, rd_dir
    mov cx, RD_MAXENT
.scan:
    cmp byte [di+RDE_TYPE], RD_FREE
    je .got
    add di, RDE_SZ
    loop .scan
    pop cx
    stc
    ret
.got:
    pop cx
    clc
    ret

; -----------------------------------------------------------------------------
; rd_settype - SI = a row whose name is set -> its RDE_TYPE (internal)
;
; A PACKAGE IS DECIDED BY ITS EXTENSION, and a redirected volume has to decide
; it the same way disk_mount's own dsk_synth does. Without this a written or
; copied `.O88` lands as type 0 - a plain file - and the double-click goes to
; SPEC.md 54's association path instead of the loader. Nothing is associated
; with `.O88`, so it does NOTHING AND SAYS NOTHING: the file is listed, the
; right size, byte-perfect on the volume, and inert.
; -----------------------------------------------------------------------------
rd_settype:
    push ax
    push bx
    push cx
    mov bx, si
    add bx, RDE_NAME
    xor cx, cx
.len:
    cmp byte [bx], 0
    je .got
    inc bx
    inc cx
    jmp short .len
.got:
    cmp cx, 4
    jb .file
    sub bx, 4                   ; the last four characters
    cmp byte [bx], '.'
    jne .file
    mov al, [bx+1]
    call rd_upper
    cmp al, 'O'
    jne .file
    cmp byte [bx+2], '8'
    jne .file
    cmp byte [bx+3], '8'
    jne .file
    mov byte [si+RDE_TYPE], 1
    jmp short .out
.file:
    mov byte [si+RDE_TYPE], 0
.out:
    pop cx
    pop bx
    pop ax
    ret

; rd_upper - AL = ASCII -> upper case
rd_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 'a' - 'A'
.out:
    ret

; rd_ncopy - DS:SI -> DS:DI, a NUL name, at most 15 characters (internal)
rd_ncopy:
    push ax
    push cx
    mov cx, 15
.next:
    mov al, [si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .next
    mov byte [di], 0
.out:
    pop cx
    pop ax
    ret

; rd_ncopy_es - ES:SI -> DS:DI, the same, for a name the KERNEL is handing us
rd_ncopy_es:
    push ax
    push cx
    push si
    push di
    mov cx, 15
.next:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .next
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; rd_ncmp - row SI's name against the caller's at ES:DI, case-insensitive
; out: CF=0 equal; SI and DI preserved
;
; The caller's string is in KERNEL_SEG and ours is in our own image, which is
; why ES is read explicitly: ES is KERNEL_SEG on entry to everything the
; kernel far-calls into a driver (SPEC.md 20.1).
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

; rd_look - find ES:DI's name in the CURRENT folder
; out: CF=0 and SI = the row; CF=1 = no such name
rd_look:
    mov si, rd_dir
.row:
    cmp si, rd_dir + RD_MAXENT * RDE_SZ
    jae .none
    cmp byte [si+RDE_TYPE], RD_FREE
    je .nextrow
    mov al, [si+RDE_PAR]
    cmp al, [rd_cwd]            ; this folder only: a name resolves where the
    jne .nextrow                ; volume is standing (SPEC.md 19.2)
    call rd_ncmp
    jnc .out
.nextrow:
    add si, RDE_SZ
    jmp short .row
.none:
    stc
.out:
    ret

; rd_blit_in  - DS:BX -> arena:AX, CX bytes (the seed path)
; rd_blit_out - arena:AX -> ES(target):DI ... see rd_read
;
; Each loads its own segments and puts them back, because ES arrived as
; KERNEL_SEG and DS is the only one that names our own data.
rd_blit_in:
    push ds
    push es
    push si
    push di
    mov si, bx
    mov di, ax
    mov es, [rd_arena]
    cld
    rep movsb
    pop di
    pop si
    pop es
    pop ds
    ret

; rd_blit_kern - DX:BX (the kernel's buffer) -> arena:AX, CX bytes
rd_blit_kern:
    push ds
    push es
    push si
    push di
    mov di, ax
    mov si, bx
    mov es, [rd_arena]          ; read while DS is still ours...
    mov ds, dx                  ; ...and only then leave
    cld
    rep movsb
    pop di
    pop si
    pop es
    pop ds
    ret

; rd_blit_prog - rd_blit_user, in pieces, reporting each (SPEC.md 12.8.1)
; in/out: rd_blit_user's exactly
;
; The one place this harness pretends to be slow, and it earns its keep: a
; DRVC_FILE driver is the only producer the kernel CANNOT step the activity
; bar for - it arms the widget from FSV_STAT's size and is then one far call
; deep inside here, blind, until this returns (SPEC.md 62.9.1). A RAM disk
; delivers a whole file in one rep movsb, so without this the OSAPI_FS_PROG
; path would ship untested against nothing but the cable it was written for,
; which is exactly how OS88NET.COM reached the field twice unexecuted.
;
; RD_PCHUNK is small on purpose: 116KB of module is ~1,800 reports, and each
; one is a compare inside the kernel that usually finds the bar's extent
; unmoved. Reporting a SHORT count is the ordinary case for a wire and the
; remainder is carried, not rounded - so an odd tail needs no special case.
RD_PCHUNK   equ 64              ; a plausible frame, not a tuned figure
rd_blit_prog:
    push ax
    push bx
    push cx
    push si
.chunk:
    jcxz .done
    mov si, cx                  ; SI = this piece's length: what is left, or
    cmp si, RD_PCHUNK           ; a whole frame, whichever is smaller - the
    jbe .go                     ; odd tail needs no case of its own, because
    mov si, RD_PCHUNK           ; a short report is what a wire does anyway
.go:
    push cx                     ; the running remainder, across a blit that
    mov cx, si                  ; spends CX down to 0
    call rd_blit_user           ; arena:AX -> DX:BX, CX bytes
    add ax, si                  ; advance the arena offset and the destination
    add bx, si                  ; (the kernel normalised BX to 0..15 and an
                                ; extent is RD_EXTKB, so neither can wrap)
    push ax
    mov ax, si
    call OSAPI_FS_PROG          ; AX = bytes SINCE THE LAST REPORT, never a
    pop ax                      ; running total (SPEC.md 12.8.1)
    pop cx
    sub cx, si
    jmp short .chunk
.done:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; rd_blit_user - arena:AX -> DX:BX (the kernel's buffer), CX bytes
rd_blit_user:
    push ds
    push es
    push si
    push di
    mov si, ax
    mov di, bx
    mov es, dx
    mov ds, [rd_arena]
    cld
    rep movsb
    pop di
    pop si
    pop es
    pop ds
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
; -----------------------------------------------------------------------------
rd_chdir:
    push si
    or ax, ax
    jz .root
    call rd_row
    jc .no
    cmp byte [si+RDE_TYPE], 2   ; folders only - nothing chdirs into a file
    jne .no
    mov [rd_cwd], al
    mov dl, [si+RDE_PAR]
    xor dh, dh
    pop si
    clc
    ret
.root:
    mov byte [rd_cwd], 0
    xor dx, dx                  ; the root's parent is never read: [dsk_cwd]=0
    pop si                      ; makes disk_mount synthesize no '..' at all
    clc
    ret
.no:
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_LIST - append this folder's entries (SPEC.md 62.9.1)
;
; THE DRIVER APPENDS AND THE KERNEL COUNTS, and the kernel also SORTS (19.4)
; and synthesizes `..` (19.5) - two places deciding a listing's order is how a
; display index stops meaning what the hit-test thinks it means.
; -----------------------------------------------------------------------------
rd_list:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, rd_dir
    mov cx, RD_MAXENT
.next:
    cmp byte [si+RDE_TYPE], RD_FREE
    je .skip
    mov al, [si+RDE_PAR]
    cmp al, [rd_cwd]
    jne .skip
    call rd_stage               ; SI's row -> rd_ent
    push si
    mov si, rd_ent              ; ES is set by the X stub from our own DS, so
    call OSAPI_FS_ENT           ; there is nothing to load here
    pop si
    jc .full                    ; the listing is full: SPEC.md 19's cap, and
.skip:                          ; stopping cleanly is what the kernel's own
    add si, RDE_SZ              ; directory scan does
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
; FSV_ENUM - the Nth entry of a folder, by ordinal (SPEC.md 62.9.7)
; in:  AL = the folder's handle (0 = the root), CX = the ordinal,
;      DX:BX = a 32-byte buffer
; out: CF=0 and the buffer holds a staged SPEC.md 19.1 entry;
;      CF=1 with AX = 0 = past the end (not an error)
;
; rd_list's walk, stopped at an ordinal instead of appending all of it - and
; it is a separate verb for the reason SPEC.md 62.9.7 gives: FSV_LIST appends
; into the kernel's ONE global listing, and a folder copy walks a folder that
; is not the one on screen.
;
; THE ORDINAL HAS TO BE STABLE, and that is a property of walking the ROW
; ARRAY BY INDEX rather than of anything clever: rows are laid down in
; creation order and never compacted, so the same (handle, ordinal) names the
; same row for as long as the FOLDER is unmodified. Deleting from it
; renumbers everything after the hole - which is inside the contract, because
; a copy does not modify its source, and it is exactly what the FAT path's
; raw slot ordinal already assumes.
; -----------------------------------------------------------------------------
rd_enum:
    push bx
    push cx
    push dx
    push si
    push di
    mov ah, al                  ; AH = the folder, so AL is free to load rows
    mov si, rd_dir
    mov di, RD_MAXENT
.row:
    cmp byte [si+RDE_TYPE], RD_FREE
    je .skip
    mov al, [si+RDE_PAR]
    cmp al, ah
    jne .skip                   ; somebody else's child
    jcxz .found                 ; ...this is the one asked for
    dec cx
.skip:
    add si, RDE_SZ
    dec di
    jnz .row
    xor ax, ax                  ; past the end, and NOT an error: it is how
    stc                         ; every walk of every folder finishes
    jmp short .out
.found:
    push bx                     ; RD_STAGE SPENDS BX - its name copy walks the
    call rd_stage               ; row through it - and BX is the caller's
    pop bx                      ; buffer offset, which nothing else holds
    push es                     ; ...and out to that buffer, which is DX:BX
    mov es, dx                  ; because ES is KERNEL_SEG on entry to
    mov si, rd_ent              ; anything the kernel far-calls (SPEC.md 51.8)
    mov di, bx
    mov cx, DSK_DE_SIZE
    cld
    rep movsb
    pop es
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_stage - one row -> the 32-byte staged SPEC.md 19.1 entry (internal)
; in:  SI = the row
; out: rd_ent filled; AX/BX/DI clobbered, CX PRESERVED
;
; CX is saved because the caller's loop counter is in it, and the first
; version of this did not save it: the zeroing loop below spent CX, rd_list's
; `loop` then ran ~65,000 times off the end of the table, and the mount filled
; the listing to its cap with whatever followed. It read as `disk_nfiles` = 32
; for a three-entry root - which is also what a driver appending too much on
; purpose looks like, so the cap held and nothing said so.
; -----------------------------------------------------------------------------
rd_stage:
    push cx
    mov di, rd_ent
    mov cx, DSK_DE_SIZE/2
.zero:
    mov word [di], 0            ; DS-relative, NOT `rep stosw`: ES is
    inc di                      ; KERNEL_SEG here (SPEC.md 20.1)
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
    call rd_hand                ; @18: OUR OPAQUE HANDLE
    xor ah, ah
    mov [rd_ent+18], ax
    mov ax, [si+RDE_SIZE]       ; @20: the size dword, which fm_measure sums
    mov [rd_ent+20], ax
    mov ax, [si+RDE_SIZE+2]
    mov [rd_ent+22], ax
    pop cx
    ret

; -----------------------------------------------------------------------------
; FSV_STAT - a name in the current folder -> a handle (SPEC.md 62.9.1)
; in:  SI -> a NUL 8.3 name, in KERNEL_SEG
; out: CF=0, AX = the handle, DX:CX = the size, BL = a FAT-style attribute
;      byte; CF=1 and AX = FERR_*
; clobbers: AX, BX, CX, DX, SI, DI
;
; The ONLY verb that takes a name, and every read composes on it - which is
; what keeps `the thing in AX, unless it is 0, in which case the string in SI`
; out of the ABI. BL is a FAT attribute byte and not the 19.1 type word, so
; dskw_rbody's existing `test 0x18` refuses a folder with FERR_PROT.
; -----------------------------------------------------------------------------
rd_stat:
    mov di, si                  ; ES:DI = the caller's name
    call rd_look
    jc .none
    mov cx, [si+RDE_SIZE]
    mov dx, [si+RDE_SIZE+2]
    xor bx, bx
    cmp byte [si+RDE_TYPE], 2
    jne .attr
    mov bl, 0x10                ; a directory, in the kernel's own vocabulary
.attr:
    call rd_hand
    xor ah, ah
    clc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_READ - the whole file, by handle (SPEC.md 62.9.1)
; in:  AX = a handle, DX:BX = the buffer, DI:CX = its 32-bit capacity
; out: CF=0 and DX:AX = the bytes read; CF=1 and AX = FERR_*
;
; THE CAPACITY IS CHECKED AGAIN HERE and that is not redundant: dskw_rbody
; checks it before it calls, but dsk_read_chain - the loader's path - reaches
; this cell without passing that check at all, and a driver that trusts its
; caller's arithmetic writes past the end of somebody else's claim.
; -----------------------------------------------------------------------------
rd_read:
    call rd_row
    jc .bad
    or di, di
    jnz .fits                   ; the capacity's high word alone clears it
    mov ax, [si+RDE_SIZE]
    cmp ax, cx
    ja .big
.fits:
    mov cx, [si+RDE_SIZE]
    mov [rd_n], cx
    jcxz .none                  ; an empty file: 0 bytes, and not an error
    mov al, [si+RDE_EXT]
    call rd_ext_off
    call rd_blit_prog           ; arena:AX -> DX:BX, CX bytes, in RD_PCHUNK
                                ; pieces with one OSAPI_FS_PROG per piece
.none:
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
; in:  AX = a handle, DX:BX = the buffer, CX = its capacity, DI:SI = the offset
; out: CF=0 and DX:AX = the bytes delivered, 0 = at or past the end
;
; No alignment precondition at all, where the FAT path needs whole clusters at
; both ends: there are no clusters here, so dskw_read_at's two refusals simply
; do not arise on this volume.
; -----------------------------------------------------------------------------
rd_readat:
    mov [rd_off], si
    mov [rd_offhi], di
    mov [rd_cap], cx
    call rd_row
    jc .bad
    cmp word [rd_offhi], 0
    jne .eof                    ; past the end of anything we serve
    mov ax, [si+RDE_SIZE]
    cmp ax, [rd_off]
    jbe .eof                    ; at or past the end: 0 bytes, NOT an error
    sub ax, [rd_off]            ; AX = what is left of the file
    cmp ax, [rd_cap]
    jbe .take
    mov ax, [rd_cap]            ; ...clamped to what was asked for
.take:
    mov [rd_n], ax
    mov cx, ax
    mov al, [si+RDE_EXT]
    call rd_ext_off
    add ax, [rd_off]
    call rd_blit_user
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

; -----------------------------------------------------------------------------
; FSV_WRITE - create or replace a file (SPEC.md 62.9.1)
; in:  SI = a NUL name in KERNEL_SEG, DX:BX = the bytes, DI:CX = how many
; out: CF=0; CF=1 and AX = FERR_*
;
; REPLACE KEEPS THE EXTENT, which is what makes this atomic enough to be
; honest: there is no window in which the name exists with no store behind it,
; because the store never moves. A FAT volume has to commit an order (SPEC.md
; 18.4 rule 1) precisely because it cannot do that.
; -----------------------------------------------------------------------------
rd_write:
    mov [rd_nameo], si          ; banked: rd_look and rd_free_row both spend
    mov [rd_n], cx              ; the registers this needs at the end
    mov [rd_bufo], bx
    mov [rd_bufs], dx
    cmp word [rd_arena], 0
    je .full
    or di, di
    jnz .big                    ; a 32-bit count is past one extent by itself
    cmp cx, RD_EXTB
    ja .big
    mov di, si                  ; ES:DI = the name
    call rd_look
    jnc .have
    call rd_free_row            ; a new file: a row...
    jc .dirfull
    call rd_ext_alloc           ; ...and a store for it
    jc .full
    mov si, di                  ; SI = the row rd_free_row found
    mov [si+RDE_EXT], al
    mov byte [si+RDE_TYPE], 0
    mov al, [rd_cwd]
    mov [si+RDE_PAR], al
    push si
    add di, RDE_NAME            ; DI is still that row
    mov si, [rd_nameo]          ; ES:SI = the caller's name, banked above
    call rd_ncopy_es
    pop si
    call rd_settype             ; ...and only now can the extension be read
    jmp short .store
.have:
    cmp byte [si+RDE_TYPE], 2
    jne .store
    mov ax, FERR_PROT           ; a folder is not a file to be overwritten
    stc
    ret
.store:
    mov cx, [rd_n]
    mov [si+RDE_SIZE], cx
    mov word [si+RDE_SIZE+2], 0
    jcxz .ok
    cmp byte [si+RDE_EXT], RD_NOEXT ; a file that was created empty has no
    jne .haveext                    ; store yet, and a replace is where it
    call rd_ext_alloc               ; earns one
    jc .full
    mov [si+RDE_EXT], al
.haveext:
    mov al, [si+RDE_EXT]
    call rd_ext_off
    mov bx, [rd_bufo]
    mov dx, [rd_bufs]
    call rd_blit_kern           ; DX:BX -> arena:AX, CX bytes
.ok:
    clc
    ret
.big:
    mov ax, FERR_BIG
    stc
    ret
.full:
    mov ax, FERR_FULL
    stc
    ret
.dirfull:
    mov ax, FERR_DIRFULL
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_APPEND - add CX bytes to the end of an existing file (SPEC.md 62.9.1)
; in:  SI = a NUL name in KERNEL_SEG, DX:BX = the bytes, CX = how many
; out: CF=0; CF=1 and AX = FERR_*
;
; This is what the copy engine streams through (SPEC.md 22.5), so it is the
; one write verb whose COST matters on a cable: it appends in place inside the
; file's own extent and moves nothing else.
; -----------------------------------------------------------------------------
rd_append:
    mov [rd_n], cx
    mov [rd_bufo], bx
    mov [rd_bufs], dx
    mov di, si
    call rd_look
    jc .none
    cmp byte [si+RDE_TYPE], 2
    je .prot
    mov ax, [si+RDE_SIZE]       ; where it goes, and whether it fits
    mov [rd_off], ax
    add ax, [rd_n]
    jc .big
    cmp ax, RD_EXTB
    ja .big
    mov [si+RDE_SIZE], ax
    mov cx, [rd_n]
    jcxz .ok
    cmp byte [si+RDE_EXT], RD_NOEXT ; appending to a file created empty
    jne .haveext
    call rd_ext_alloc
    jc .full
    mov [si+RDE_EXT], al
.haveext:
    mov al, [si+RDE_EXT]
    call rd_ext_off
    add ax, [rd_off]
    mov bx, [rd_bufo]
    mov dx, [rd_bufs]
    call rd_blit_kern
.ok:
    clc
    ret
.big:
    mov ax, FERR_BIG
    stc
    ret
.full:
    mov ax, FERR_FULL
    stc
    ret
.prot:
    mov ax, FERR_PROT
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_DELETE - remove a file (SPEC.md 62.9.1)
; in:  SI = a NUL name in KERNEL_SEG
;
; A FOLDER IS REFUSED HERE and removed by FSV_RMDIR, which is the kernel's own
; split (dskw_delete against dskw_rmdir) and not a choice this driver gets to
; make: files.inc calls one or the other from the entry's TYPE.
; -----------------------------------------------------------------------------
rd_delete:
    mov di, si
    call rd_look
    jc .none
    cmp byte [si+RDE_TYPE], 2
    je .prot
    mov al, [si+RDE_EXT]
    call rd_ext_free
    mov byte [si+RDE_TYPE], RD_FREE
    clc
    ret
.prot:
    mov ax, FERR_PROT
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_RENAME - SI = the old name, DI = the new one, both in KERNEL_SEG
; -----------------------------------------------------------------------------
rd_rename:
    mov [rd_nameo], di          ; bank the NEW name: rd_look spends DI
    mov di, si
    call rd_look
    jc .none
    push si                     ; the row it found
    mov di, [rd_nameo]
    call rd_look                ; is the new name taken? (rd_look uses ES:DI)
    pop si
    jnc .exists
    push si
    mov di, si
    add di, RDE_NAME
    mov si, [rd_nameo]
    call rd_ncopy_es            ; ES:SI -> DS:DI
    pop si
    clc
    ret
.exists:
    mov ax, FERR_EXIST
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_MKDIR - SI = a NUL name in KERNEL_SEG
;
; A folder costs a ROW and no extent, so a directory-heavy volume runs out of
; rows long before it runs out of store - which is why FERR_DIRFULL and
; FERR_FULL are different answers here as they are on a FAT volume.
; -----------------------------------------------------------------------------
rd_mkdir:
    mov [rd_nameo], si
    mov di, si
    call rd_look
    jnc .exists
    call rd_free_row
    jc .dirfull
    mov si, di
    mov byte [si+RDE_TYPE], 2
    mov byte [si+RDE_EXT], RD_NOEXT
    mov word [si+RDE_SIZE], 0
    mov word [si+RDE_SIZE+2], 0
    mov al, [rd_cwd]
    mov [si+RDE_PAR], al
    add di, RDE_NAME
    mov si, [rd_nameo]
    call rd_ncopy_es
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

; -----------------------------------------------------------------------------
; FSV_RMDIR - SI = a NUL name in KERNEL_SEG; the folder must be EMPTY
;
; Emptiness is tested HERE because only the side holding the directory can
; answer it - which is the same reason the FAT path tests it inside dskw_rmdir
; rather than making files.inc walk the folder first.
; -----------------------------------------------------------------------------
rd_rmdir:
    mov di, si
    call rd_look
    jc .none
    cmp byte [si+RDE_TYPE], 2
    jne .prot                   ; a file is dskw_delete's, not ours
    call rd_hand                ; AL = this folder's handle...
    push si
    mov si, rd_dir              ; ...and nothing may still name it as a parent
    mov cx, RD_MAXENT
.scan:
    cmp byte [si+RDE_TYPE], RD_FREE
    je .nextrow
    cmp [si+RDE_PAR], al
    je .notempty
.nextrow:
    add si, RDE_SZ
    loop .scan
    pop si
    mov byte [si+RDE_TYPE], RD_FREE
    clc
    ret
.notempty:
    pop si
    mov ax, FERR_PROT           ; the kernel's own word for "not empty" here
    stc
    ret
.prot:
    mov ax, FERR_PROT
    stc
    ret
.none:
    mov ax, FERR_NOENT
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_DFREE - free space (SPEC.md 62.9.1)
; out: CF=0, DX:AX = free BYTES, BX = the granule
;
; EXACT, because the store is fixed extents: count the free ones and multiply.
; A bump allocator would have had to either lie here or grow a compactor, and
; this is the whole reason the arena is shaped the way it is.
; -----------------------------------------------------------------------------
rd_dfree:
    push cx
    push si
    xor cx, cx
    xor si, si
.scan:
    cmp si, RD_NEXT
    jae .counted
    cmp byte [rd_ext+si], 0
    jne .next
    inc cx
.next:
    inc si
    jmp short .scan
.counted:
    mov ax, cx
    mov cx, RD_EXTB
    mul cx                      ; DX:AX = free extents * RD_EXTB
    mov bx, RD_EXTB
    pop si
    pop cx
    clc
    ret

; --- state -------------------------------------------------------------------
rd_arena:   dw 0                ; the store's segment, 0 = none was granted
rd_n:       dw 0                ; bytes this operation is about to move,
                                ; banked across the `rep movsb` that spends CX
rd_off:     dw 0                ; FSV_READAT's offset, and FSV_APPEND's...
rd_offhi:   dw 0
rd_cap:     dw 0                ; ...and READAT's capacity
rd_bufo:    dw 0                ; the kernel's buffer, banked across a lookup
rd_bufs:    dw 0
rd_nameo:   dw 0                ; a name in KERNEL_SEG, likewise
rd_cwd:     db 0                ; the folder we are standing in, 0 = the root
rd_vol:     db 0xFF             ; the volume index we registered, FF = none
rd_ent:     times 32 db 0       ; the staged SPEC.md 19.1 entry, ours to fill
rd_ext:     times RD_NEXT db 0  ; extent owners: 0 = free
rd_dir:     times RD_MAXENT * RDE_SZ db 0

    OS88_DRV_END
