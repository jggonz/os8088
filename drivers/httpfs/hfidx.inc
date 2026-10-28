; =============================================================================
; hfidx.inc - the index, and the volume's FSV_* verbs (SPEC.md 122.3, 122.4)
;
; HTTP has no directory listing, so the server carries one: OS88.IDX at the
; prefix, fetched ONCE into a heap claim and every listing answered from it.
; tools/os88httpdrv.py writes it and is the reference reader; every refusal
; here is one of index_parse's.
;
;   header, 16 bytes   'OS88IDX', 0 / dw records / dw 0 / dd serial
;   record, 18 bytes   db[11] the FAT name / db attr (10h = a folder; the
;                      disk's own read-only, hidden and system bits too) /
;                      dw parent / dd size, or for a folder (first, count)
;
; Record 0 is the root; every folder's children are a contiguous run, so a
; listing is a slice and an ordinal is an index into it - FSV_ENUM's stable
; ordinal (SPEC.md 62.9.1) by construction. THE HANDLE IS THE RECORD'S INDEX,
; and 0 is the root's, which is also what "no handle" means to the kernel.
; =============================================================================

HF_MAXREC    equ 896            ; one 16 KB claim: 16 + 18 x 896 = 16,144
HF_IDXKB     equ 16             ; ...the claim's KB, for drv_memk's figure
HF_HDR       equ 16
HF_REC       equ 18
HR_ATTR      equ 11
HR_PAR       equ 12
HR_A         equ 14             ; size low, or a folder's first child
HR_B         equ 16             ; size high, or a folder's child count
HF_DEPTH     equ 16             ; the deepest folder a path is composed for
%if HF_HDR + HF_REC * HF_MAXREC > HF_IDXKB * 1024
  %error "httpfs: HF_MAXREC records do not fit HF_IDXKB"
%endif

; --- hf_rec - AX = a record index -> BX = its offset in the index ----------
hf_rec:
    push ax
    mov bx, ax
    shl bx, 1                   ; 2n
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1                   ; 16n
    add bx, ax
    add bx, HF_HDR
    pop ax
    ret

; -----------------------------------------------------------------------------
; hf_reloc - the index moved (SPEC.md 66.2, NO-PINNING R2). BX = where it was,
; DX = where it is; every register preserved (sk_reloc's shape). [hf_iseg] is
; the one word naming it between verbs; within a verb it is loaded into ES and
; re-read after the one call that can claim (hf_list's OSAPI_FS_ENT). While a
; package WORKER is inside a driver verb, the kernel pins every driver claim
; (SPEC.md 66.6.3.2).
; -----------------------------------------------------------------------------
hf_reloc:
    cmp bx, [hf_iseg]
    jne .out
    mov [hf_iseg], dx
.out:
    ret

; -----------------------------------------------------------------------------
; hf_drop_idx - give the index back. clobbers: nothing
; -----------------------------------------------------------------------------
hf_drop_idx:
    push dx
    xor dx, dx
    xchg dx, [hf_iseg]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
.out:
    mov word [hf_nrec], 0
    mov word [hf_cwd], 0
    pop dx
    ret

; -----------------------------------------------------------------------------
; hf_fetch - GET OS88.IDX, into a claim of its own, and validate it
; out: CF=0, [hf_iseg]/[hf_nrec] set; CF=1 and AX = an HFE_*, nothing held
;      [hf_err] and [hf_failed] say so for the page either way
; -----------------------------------------------------------------------------
hf_fetch:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call hf_drop_idx
    call hf_parse
    jc .fail
    mov si, hf_s_idx            ; the path is the index's own name
    mov byte [hf_rng], 0
    call hf_get
    jc .fail
    mov ax, HFE_STATUS
    cmp word [hf_status], 200
    jne .drop
    mov ax, HFE_INDEX
    cmp byte [hf_hasl], 0
    je .drop
    cmp word [hf_clen+2], 0
    jne .drop
    mov cx, [hf_clen]
    cmp cx, HF_HDR + HF_REC
    jb .drop
    cmp cx, HF_HDR + HF_REC * HF_MAXREC
    ja .drop
    mov ax, cx
    sub ax, HF_HDR
    xor dx, dx
    mov bx, HF_REC
    div bx
    mov [hf_nwant], ax
    mov ax, HFE_INDEX
    or dx, dx
    jnz .drop
    mov ax, cx                  ; the claim, in KB
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM_HI     ; owner = our segment: drv_release sweeps it
    jnc .claimed
    mov ax, HFE_MEM
    jmp short .drop
.claimed:
    mov [hf_tseg], dx
    mov [hf_bseg], dx
    xor ax, ax
    mov [hf_boff], ax
    mov [hf_skip], ax
    mov [hf_skip+2], ax
    mov [hf_prog], al
    mov ax, [hf_clen]
    mov [hf_left], ax
    mov word [hf_left+2], 0
    call hf_body
    jc .free
    call hf_abort               ; NOT KEPT: nothing follows the index on the
                                ; wire, and a hold would last the session
    mov es, [hf_tseg]
    call hf_valid
    mov ax, HFE_INDEX
    jc .free
    mov ax, [hf_tseg]
    mov [hf_iseg], ax
    push dx                     ; ...AND IT MAY MOVE from here (NO-PINNING R2):
    mov dx, ax                  ; it was the network's write target until the
    mov ax, hf_reloc            ; fetch finished, and now [hf_iseg] is the one
    call OSAPI_MEM_MOVABLE      ; word that names it (hf_reloc). A refusal
    pop dx                      ; only pins it
    mov ax, [hf_nwant]
    mov [hf_nrec], ax           ; ([hf_cwd] is the root: hf_drop_idx said so)
    mov byte [hf_failed], 0
    mov word [hf_err], 0
    clc
    jmp short .out
.drop:
    call hf_abort
    jmp short .fail
.free:
    call hf_abort
    mov dx, [hf_tseg]
    call OSAPI_MEM_FREE
.fail:
    mov [hf_err], ax
    mov byte [hf_failed], 1
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

hf_s_idx:   db 'OS88.IDX', 0
hf_s_magic: db 'OS88IDX', 0

; -----------------------------------------------------------------------------
; hf_valid - ES = a fetched index of [hf_nwant] records -> CF=1 = refuse it
; clobbers: AX, BX, CX, DX, SI, DI
;
; EVERYTHING HERE IS HOSTILE, and every check is what makes a later walk
; bounded: a parent below its child is what stops a path walk going round, a
; folder run inside the table is what stops a listing reading past the claim,
; and a run whose every member names the folder is what makes the listing and
; the parent pointers one tree and not two.
; -----------------------------------------------------------------------------
hf_valid:
    mov si, hf_s_magic
    xor di, di
    mov cx, 8
    cld
    repe cmpsb
    jne .bad
    mov cx, [hf_nwant]
    cmp [es:8], cx
    jne .bad
    cmp word [es:10], 0
    jne .bad
    mov bx, HF_HDR
    test byte [es:bx+HR_ATTR], 0x10
    jz .bad
    cmp word [es:bx+HR_PAR], 0
    jne .bad
    xor ax, ax                  ; AX = this record's index, BX its offset
.r:
    test byte [es:bx+HR_ATTR], 0x10
    jz .self
    mov si, [es:bx+HR_B]        ; a folder: its run...
    or si, si
    jz .self
    mov di, [es:bx+HR_A]
    or di, di
    jz .bad                     ; ...starts past the root...
    cmp di, cx
    jae .bad
    mov dx, cx
    sub dx, di
    cmp si, dx
    ja .bad                     ; ...and ends inside the table...
    push bx
    push ax
    mov ax, di
    call hf_rec
    pop ax
.kid:
    cmp [es:bx+HR_PAR], ax      ; ...and every member names this folder
    jne .badkid
    add bx, HF_REC
    dec si
    jnz .kid
    pop bx
.self:
    or ax, ax
    jz .next                    ; the root has no parent and no name
    mov dx, [es:bx+HR_PAR]
    cmp dx, ax
    jae .bad
    push bx
    push ax
    mov ax, dx
    call hf_rec
    test byte [es:bx+HR_ATTR], 0x10
    pop ax
    pop bx
    jz .bad                     ; a parent that is a file
    cmp byte [es:bx], ' '
    je .bad
    push cx
    mov cx, 11
    mov si, bx
.nm:
    mov dl, [es:si]             ; PRINTABLE, and nothing more: ' '..'~', the
    inc si                      ; range the kernel's own listing sanitizer
    sub dl, ' '                 ; passes (SPEC.md 19.1) - what reaches the
    cmp dl, '~' - ' '           ; font renderer is a glyph it has. The first
    ja .badnm                   ; byte's space was refused above
    loop .nm
    pop cx
.next:
    add bx, HF_REC
    inc ax
    cmp ax, cx
    jb .r
    clc
    ret
.badnm:
    pop cx
    stc
    ret
.badkid:
    pop bx
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; hf_need - the index, fetched if it is not held
; out: CF=0 and ES = the index; CF=1 and AX = FERR_IO (ES untouched)
; -----------------------------------------------------------------------------
hf_need:
    cmp word [hf_iseg], 0
    jne .have
    cmp byte [hf_url], 0
    je .no
    call hf_fetch
    jc .no
.have:
    mov es, [hf_iseg]
    clc
    ret
.no:
    mov ax, FERR_IO
    stc
    ret

; --- hf_cwdrun - the current folder's run: CF=0, ES = the index, AX = its
;     first record, CX = how many; CF=1 and AX = a FERR_*. clobbers: BX
hf_cwdrun:
    call hf_need
    jc hf_isret
    mov ax, [hf_cwd]
    call hf_isdir
    jc hf_isret
    mov cx, [es:bx+HR_B]
    mov ax, [es:bx+HR_A]
    ret                         ; CF = 0 from hf_isdir

; --- hf_isdir - AX = a handle -> CF=0 a folder in the index (0 is the root) --
;     in: ES = the index. out: BX = its record. CF=1 and AX = FERR_NOENT
hf_isdir:
    call hf_isrec
    jc hf_isret
    jnz hf_isret                ; a folder: CF = 0 from the test
hf_noent:
    mov ax, FERR_NOENT
    stc
hf_isret:
    ret

; --- hf_isfile - the same, for a file, and 0 is never one ------------------
hf_isfile:
    or ax, ax
    jz hf_noent
    call hf_isrec
    jc hf_isret
    jz hf_isret                 ; a file: CF = 0 from the test
    mov ax, FERR_PROT           ; a folder is not a file to read
    stc
    ret

; --- hf_isrec - AX in the index: CF=1, AX = FERR_NOENT if not; else BX = its
;     record, CF = 0 and ZF = 0 for a folder
hf_isrec:
    cmp ax, [hf_nrec]
    jae hf_noent
    call hf_rec
    test byte [es:bx+HR_ATTR], 0x10
    ret

; -----------------------------------------------------------------------------
; hf_stage - AX = a record index (ES = the index) -> hf_ent, the SPEC.md 19.1
;            staged entry. clobbers: nothing
; -----------------------------------------------------------------------------
hf_stage:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ax
    push es
    push ds
    pop es
    mov di, hf_ent
    xor ax, ax
    mov cx, 16
    cld
    rep stosw
    pop es
    pop ax
    mov [hf_ent+18], ax         ; @18: the handle
    call hf_rec
    mov al, [es:bx+HR_ATTR]     ; @24: the attribute, which FIND reports at
    mov [hf_ent+24], al         ; +13 (SPEC.md 62.9.17.1) - hidden + system
                                ; is how the installer knows KERNEL.SYS
    mov si, bx
    mov di, hf_ent
    call hf_pretty              ; @0: the name
    xor ax, ax
    test byte [es:bx+HR_ATTR], 0x10
    jz .file
    mov al, 2                   ; @16: 2 a folder, and size 0 (SPEC.md 19.1)
    jmp short .type
.file:
    mov cx, [es:bx+HR_A]        ; @20: the size
    mov [hf_ent+20], cx
    mov cx, [es:bx+HR_B]
    mov [hf_ent+22], cx
    cmp word [es:bx+8], 'O8'    ; 1 a package, 0 anything else
    jne .type
    cmp byte [es:bx+10], '8'
    jne .type
    inc ax
.type:
    mov [hf_ent+16], ax
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hf_pretty - ES:SI = an 11-byte FAT name -> 'NAME.EXT', NUL, at DS:DI
; out: DI at the NUL. clobbers: AX, CX
; -----------------------------------------------------------------------------
hf_pretty:
    push si
    mov cx, 11                  ; ONE pass over the eleven: spaces dropped (a
.c:                             ; FAT name has them only as padding, and
    cmp cl, 3                   ; hf_valid refuses a leading one), a '.' put
    jne .b                      ; in front of the extension, and taken off
    mov byte [di], '.'          ; again when no extension followed it
    inc di
.b:
    mov al, [es:si]
    inc si
    cmp al, ' '
    je .n
    mov [di], al
    inc di
.n:
    loop .c
    cmp byte [di-1], '.'
    jne .end
    dec di
.end:
    mov byte [di], 0
    pop si
    ret

; -----------------------------------------------------------------------------
; hf_mkpath - AX = a record (ES = the index) -> [hf_path], its path below the
;             prefix: 'APPS/CALC.O88'. out: CF=1 = deeper than HF_DEPTH
; clobbers: AX, BX, CX, SI, DI
; -----------------------------------------------------------------------------
hf_mkpath:
    mov di, hf_chain
    xor cx, cx
.up:
    or ax, ax
    jz .walked
    cmp cx, HF_DEPTH
    jae .deep
    mov [di], ax
    inc di
    inc di
    inc cx
    call hf_rec
    mov ax, [es:bx+HR_PAR]      ; below it, so this ends (hf_valid)
    jmp short .up
.walked:
    mov si, di                  ; down again from the top
    mov di, hf_path
.dn:
    dec si
    dec si
    mov ax, [si]
    call hf_rec
    push si
    push cx
    mov si, bx
    call hf_pretty
    pop cx
    pop si
    mov byte [di], '/'
    inc di
    loop .dn
    mov byte [di-1], 0          ; the last '/' was never wanted
    clc
    ret
.deep:
    stc
    ret

; -----------------------------------------------------------------------------
; hf_name11 - the NUL 8.3 name at ES:SI (KERNEL_SEG) -> hf_want, eleven
;             bytes, upper case, space padded. clobbers: AX, CX, DI
;
; ONE LOOP: CX is where the part being filled ENDS, so a base past eight or an
; extension past three is read to its end and kept to its length
; -----------------------------------------------------------------------------
hf_name11:
    push si
    mov di, hf_want
    mov cx, 11
.sp:
    mov byte [di], ' '
    inc di
    loop .sp
    mov di, hf_want
    lea cx, [di+8]
.b:
    mov al, [es:si]
    inc si
    or al, al
    jz .out
    cmp al, '.'
    jne .ch
    mov di, hf_want + 8
    lea cx, [di+3]
    jmp short .b
.ch:
    cmp di, cx
    jae .b                      ; a part past its length is no name we hold
    cmp al, 'a'
    jb .put
    cmp al, 'z'
    ja .put
    sub al, 0x20
.put:
    mov [di], al
    inc di
    jmp short .b
.out:
    pop si
    ret

; =============================================================================
; THE VERBS. ES = KERNEL_SEG on entry to each (SPEC.md 20.1)
; =============================================================================

; FSV_WRITE, APPEND, DELETE, RENAME, MKDIR, RMDIR: read-only, in FAT's words
hf_wprot:
    mov ax, FERR_WPROT
    stc
    ret

; FSV_DFREE: nothing free, in 512-byte granules
hf_dfree:
    xor ax, ax
    xor dx, dx
    mov bx, 512
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_CHDIR - AX = a folder's handle, 0 = the root. out: CF=0, DX = its
;             parent's handle; CF=1 and AX = a FERR_*. THE ROOT FETCHES THE
;             INDEX: it is what every mount asks first (SPEC.md 122.1)
; -----------------------------------------------------------------------------
hf_chdir:
    push bx
    push es
    push ax
    call hf_need
    pop bx
    jc .out
    mov ax, bx
    call hf_isdir
    jc .out
    mov [hf_cwd], ax
    mov dx, [es:bx+HR_PAR]      ; the root's is 0, which is never read
    clc
.out:
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; FSV_LIST - append the current folder's entries. out: CF=0 - a full listing
;            stops cleanly, and a missing index has been answered by FSV_CHDIR
; -----------------------------------------------------------------------------
hf_list:
    push ax
    push bx
    push cx
    push si
    push es
    call hf_cwdrun
    jc .out
    jcxz .out
.e:
    call hf_stage
    test byte [hf_ent+24], 0x06 ; hidden or system: a FAT listing leaves it
    jnz .skip                   ; out (dsk_synth), so the drive does too -
                                ; FSV_ENUM still answers it (SPEC.md 122.4)
    mov si, hf_ent              ; ES:SI - the X stub makes ES our DS
    push ax
    push cx
    call OSAPI_FS_ENT           ; AX = where it landed: not ours to keep
    pop cx
    pop ax
    mov es, [hf_iseg]           ; RE-READ: FS_ENT can claim, a claim can
                                ; compact, and the index moves (hf_reloc) -
                                ; the ES hf_cwdrun handed over is stale then
    jc .out                     ; the listing is full
.skip:
    inc ax
    loop .e
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; FSV_ENUM - AX = a folder's handle, CX = the ordinal, DX:BX = 32 bytes
; out: CF=0 and the entry; CF=1 and AX = 0 past the end, or a FERR_*
; -----------------------------------------------------------------------------
hf_enum:
    push bx
    push cx
    push si
    push di
    push es
    mov [hf_bseg], dx
    mov [hf_boff], bx
    push ax
    call hf_need
    pop bx
    jc .out
    mov ax, bx
    call hf_isdir
    jc .out
    cmp cx, [es:bx+HR_B]
    jae .end
    mov ax, [es:bx+HR_A]
    add ax, cx
    call hf_stage
    mov es, [hf_bseg]
    mov di, [hf_boff]
    mov si, hf_ent
    mov cx, 16
    cld
    rep movsw
    clc
    jmp short .out
.end:
    xor ax, ax
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; FSV_STAT - SI = a NUL 8.3 name in KERNEL_SEG, in the current folder
; out: CF=0, AX = the handle, DX:CX = the size, BL = 10h a folder / 0;
;      CF=1 and AX = a FERR_*
; -----------------------------------------------------------------------------
hf_stat:
    push si
    push di
    push es
    call hf_name11              ; (ES = KERNEL_SEG still)
    call hf_cwdrun
    jc .out
    jcxz .noent
.e:
    push cx
    call hf_rec
    mov si, hf_want
    mov di, bx
    mov cx, 11
    cld
    repe cmpsb                  ; DS:SI (ours) against ES:DI (the index)
    pop cx
    je .found
    inc ax
    loop .e
.noent:
    mov ax, FERR_NOENT
    stc
    jmp short .out
.found:
    xor dx, dx                  ; BX IS THE RECORD until the last instruction:
    xor cx, cx                  ; BL is the answer, and setting it first read
    test byte [es:bx+HR_ATTR], 0x10 ; the size out of a record 0x10 bytes off
    jnz .dir                    ; a folder: size 0, as its listing says
    mov cx, [es:bx+HR_A]
    mov dx, [es:bx+HR_B]
    xor bl, bl
    jmp short .ok
.dir:
    mov bl, 0x10
.ok:
    clc
.out:
    pop es
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; hf_open_file - the start FSV_READ and FSV_READAT share: AX = a handle,
;                DX:BX = the buffer -> CF=0, [hf_fsize] and [hf_path] set
;                (ES = the index); CF=1, AX = a FERR_*, DX = 0
; -----------------------------------------------------------------------------
hf_open_file:
    mov [hf_bseg], dx
    mov [hf_boff], bx
    push ax
    call hf_need
    pop dx
    jc .fail
    mov ax, dx
    call hf_isfile
    jc .fail
    mov dx, [es:bx+HR_A]
    mov [hf_fsize], dx
    mov dx, [es:bx+HR_B]
    mov [hf_fsize+2], dx
    push cx
    push si
    push di
    call hf_mkpath
    pop di
    pop si
    pop cx
    mov ax, FERR_IO
    jc .fail
    clc
    ret
.fail:
    xor dx, dx
    stc
    ret

; --- hf_ioerr - [hf_err] = AX (an HFE_*) -> AX = FERR_IO, DX = 0, CF=1 ------
hf_ioerr:
    mov [hf_err], ax
    call hf_abort
    mov ax, FERR_IO
    xor dx, dx
    stc
    ret

; -----------------------------------------------------------------------------
; FSV_READ - AX = a handle, DX:BX = the buffer, DI:CX = its capacity
; out: CF=0 and DX:AX = the bytes read; CF=1 and AX = a FERR_*
;
; The capacity checked HERE before any request: the loader reaches this cell
; without the kernel's own check (SPEC.md 62.9.18). Then it is FSV_READAT's
; one GET for the whole file, and so the body must be the index's size to the
; byte - a tree replaced under a mounted index fails rather than handing over
; a different file.
; -----------------------------------------------------------------------------
hf_read:
%ifdef HFDIAG
    mov byte [hf_dgk], 'R'
    call hf_dgin
%endif
    push bx
    push cx
    push si
    push di
    push es
    call hf_open_file
    jc .out
    mov ax, [hf_fsize+2]
    cmp ax, di
    ja .big
    jb .fits
    mov ax, [hf_fsize]
    cmp ax, cx
    ja .big
.fits:
    xor ax, ax
    mov [hf_rlo], ax
    mov [hf_rlo+2], ax
    mov ax, [hf_fsize]
    mov [hf_rn], ax
    mov dx, [hf_fsize+2]
    mov [hf_rn+2], dx
    or ax, dx
    jz hf_rdout                 ; nothing to fetch: DX:AX = 0, CF = 0
    mov al, 1                   ; the bar is ours to step (SPEC.md 12.8.1)
    jmp short hf_rdgo
.big:
    mov ax, FERR_BIG
    xor dx, dx
    stc
.out:
    jmp short hf_rdout

; -----------------------------------------------------------------------------
; FSV_READAT - AX = a handle, DX:BX = the buffer, CX = its capacity, DI:SI =
;              the offset. out: CF=0 and DX:AX = the bytes delivered, 0 at or
;              past the end
; -----------------------------------------------------------------------------
hf_readat:
%ifdef HFDIAG
    mov byte [hf_dgk], 'A'
    call hf_dgin
%endif
    push bx
    push cx
    push si
    push di
    push es
    call hf_open_file
    jc hf_rdout
    mov [hf_rlo], si
    mov [hf_rlo+2], di
    mov ax, [hf_fsize]          ; what is left past the offset, 32 bits
    mov dx, [hf_fsize+2]
    sub ax, si
    sbb dx, di
    jnc .left
    xor ax, ax                  ; past the end: 0, which is answered below
    cwd
.left:
    or dx, dx
    jnz .clamp
    cmp ax, cx
    jbe .take
.clamp:
    mov ax, cx
.take:
    xor dx, dx
    mov [hf_rn], ax
    mov [hf_rn+2], dx
    or ax, ax
    jz hf_rdout                 ; DX:AX = 0 and CF = 0: at or past the end
    xor al, al                  ; the copy engine steps its own bar
hf_rdgo:
    call hf_xfer
hf_rdout:
%ifdef HFDIAG
    call hf_dgout
%endif
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret
; -----------------------------------------------------------------------------
; hf_xfer - THE GET both reads make: `Range:` for exactly [hf_rn] bytes from
;           [hf_rlo] into [hf_bseg]:[hf_boff] ([hf_fsize] = the whole file's)
; in:  AL = [hf_prog]'s value. out: CF=0 and DX:AX = [hf_rn]; CF=1, AX =
;      FERR_IO, DX = 0, the reason in [hf_err]. clobbers: BX, CX, SI, DI
;
; A 206 must carry the length asked; where it starts is taken on trust - a
; server that honours Range: at all honours its start (SPEC.md 122.4). A
; server that answers 200 has IGNORED the range: the body is the whole file -
; its length checked against the index's - so the offset is DRAINED, and the
; connection is kept only if the read took the body to its end. Correct on any
; server, and slow only on one that will not range.
; -----------------------------------------------------------------------------
hf_xfer:
    mov [hf_prog], al
    mov byte [hf_rng], 1
    mov si, hf_path
    call hf_get
    jc .io
    xor ax, ax
    mov [hf_skip], ax
    mov [hf_skip+2], ax
    cmp word [hf_status], 206
    je .part
    call hf_lenis               ; 200: the whole file, and Range: ignored
    jc .io
    mov ax, [hf_rlo]
    mov [hf_skip], ax
    mov ax, [hf_rlo+2]
    mov [hf_skip+2], ax
    jmp short .body
.part:
    mov ax, HFE_SHORT
    cmp byte [hf_hasl], 0
    je .body
    mov dx, [hf_clen]
    cmp dx, [hf_rn]
    jne .io
    mov dx, [hf_clen+2]
    cmp dx, [hf_rn+2]
    jne .io
.body:
    mov ax, [hf_rn]
    mov [hf_left], ax
    mov ax, [hf_rn+2]
    mov [hf_left+2], ax
    call hf_body
    jc .io
    cmp word [hf_status], 206   ; a 206 was read whole and may be kept...
    je .done
    mov ax, [hf_rlo]            ; ...and a 200 only when the offset and the
    mov dx, [hf_rlo+2]          ; count reach the end of the file: its
    add ax, [hf_rn]             ; unread tail means the handle goes
    adc dx, [hf_rn+2]
    cmp ax, [hf_fsize]
    jne .tail
    cmp dx, [hf_fsize+2]
    jne .tail
.done:
    call hf_done
    jmp short .ok
.tail:
    call hf_abort
.ok:
    mov ax, [hf_rn]
    mov dx, [hf_rn+2]
    clc
    ret
.io:
    jmp hf_ioerr

; --- hf_lenis - a Content-Length, if sent, must be [hf_fsize]. CF=1, AX ----
hf_lenis:
    cmp byte [hf_hasl], 0
    je .ok
    mov ax, [hf_clen]
    cmp ax, [hf_fsize]
    jne .no
    mov ax, [hf_clen+2]
    cmp ax, [hf_fsize+2]
    jne .no
.ok:
    clc
    ret
.no:
    mov ax, HFE_SHORT
    stc
    ret
