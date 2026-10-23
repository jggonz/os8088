; =============================================================================
; os8088 - ascw.inc
;
; The ASSOC.DAT flush (SPEC.md 54.7.6.4): FORMAT.DRV's fifth entry, FME_ASCW.
; Prefix `asw_`.
;
; A harvest that reads a package this volume's cache does not hold, and a
; search that finds a program the cache did not place, both teach the tables
; something (SPEC.md 54.3.1, 54.4.2) - and until this file neither wrote it
; down, so every boot and every heap shed paid for it again. asc_post marks the
; volume, the UI task's next pass calls asc_flush, and THIS rebuilds the
; volume's ASSOC.DAT from what is on it.
;
; It is the hard-disk installer's algorithm (SPEC.md 52.10.14, which reached it
; as hd_iassoc in HDDTOOL.DRV and now reaches it here through
; OSAPI_ASSOC_FLUSH) with one change that is the whole point at runtime: a row
; the OLD file already holds is COPIED, not re-read. In incremental mode
; nothing is read off a package at all, bar the few headers step 5 of 54.7.6.4
; names - the body and glyph come from the icon store and the declarations
; from the capture the harvest kept (54.7.6.2) - so a flush costs the same for
; one new package as for twenty.
;
; IT READS THE VOLUME IT WRITES ABOUT, so it cannot write a cache that
; describes a different disk: a floppy swapped between the post and the flush
; gets a file built from the floppy that is in the drive.
;
; A MODULE (SPEC.md 2.8): CS is the image, DS is KERNEL_SEG, SS is LOW_SEG.
; Every resident routine it wants is .cold and ends in a near `ret`, so it is
; reached through wm_cbd - `call bp / retf` in COLD_SEG - and costs this file
; five bytes a call and the kernel nothing (ASWX below). Its own scratch is
; in the image (asw_v, below asw_s_name), written before anything reads it,
; and BP addresses it throughout.
; =============================================================================

%ifdef OS88_ASSOC
%ifndef KD_BUILD

; --- the work claim: transient, MEM_K_ASC, freed before the entry returns ---
ASW_OLD     equ 0                       ; the old file, as read
ASW_NEW     equ ASC_KB * 1024           ; the new one, as built
ASW_EXT     equ ASW_NEW + ASC_HDR + ASC_NAPP * ASC_ROW
                                        ; ext rows STAGED where they would land
                                        ; were every app row used, and moved
                                        ; down once the count is known
                                        ; (hd_ia_close's reason: always DOWN,
                                        ; so one forward movsb serves)
ASW_LIST    equ ASW_NEW + ASC_KB * 1024 ; one directory's packages:
ASW_LSZ     equ 14                      ;   stem 8, size 2, first cluster 2,
ASW_NPKG    equ 96                      ;   folder cluster 2
ASW_FOLD    equ ASW_LIST + ASW_NPKG * ASW_LSZ   ; the root's folders, a word each
ASW_NFOLD   equ 32
ASW_BLK     equ ASW_FOLD + ASW_NFOLD * 2        ; one association block (16)
ASW_STG     equ ASW_BLK + 16                    ; one row's icon 64 + glyph 8
ASW_END     equ ASW_STG + DSK_ICO_SIZE + 8
ASW_KB      equ (ASW_END + 1023) / 1024
%if ASW_EXT + ASC_NEXT * 4 > ASW_LIST
  %error "ascw.inc: the staged ext rows overrun the package list"
%endif
%if ASC_ROWGLY != ASC_ROWICO + DSK_ICO_SIZE
  %error "ascw.inc copies a row's icon and glyph as ONE 72-byte run from ASW_STG"
%endif

; ASWX - call a resident .cold routine from the image, through asw_cold below:
; the routine is NAMED BY THE WORD AFTER THE CALL, so a site is five bytes
; where `mov bp, x / call COLD_SEG:wm_cbd` was eight, and every register - BP
; included - reaches the routine and comes back as the routine leaves it,
; flags too (kernel size pass 13).
%macro ASWX 1
    call asw_cold
    dw %1
%endmacro

; THE SCRATCH IS ADDRESSED OFF BP: the block at asw_v, in the image. BP
; points at it for the whole of ascw_flush_x and nothing below writes BP, so a
; cell is [cs:bp+asw_x-asw_v], a d8 - a byte shorter at every one of the
; hundred-odd sites than [cs:asw_x]'s disp16 (kernel size pass 13). Spelled
; out rather than through a macro: os88ovlchk reads the `cs:` in the source

section .modf

; -----------------------------------------------------------------------------
; ascw_flush_x - FME_ASCW (SPEC.md 54.7.6.4)
; in:  DL = the volume, AL = 0 incremental / 1 full (OSAPI_ASSOC_FLUSH's)
; out: CF=0, AX = 0 = the volume's ASSOC.DAT describes it now (written, or
;      already right, or there is nothing on it to describe);
;      CF=1, AX = FERR_* = it could not, and nothing was written
; clobbers: AX (the output) and the flags - every other register preserved
; -----------------------------------------------------------------------------
ascw_flush_x:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bp, asw_v               ; the scratch block's base, held throughout
    CSDO mov [CSEG bp+asw_vol-asw_v], dl
    and al, ASCF_FULL           ; the hold bits are asc_flush's, not ours
    CSDO mov [CSEG bp+asw_mode-asw_v], al
    CSDO mov word [CSEG bp+asw_seg-asw_v], 0
    CSDO mov word [CSEG bp+asw_err-asw_v], FERR_NODISK

    ; --- 1. a volume that can hold a cache: a FAT one this kernel writes ---
    mov bl, dl
    ASWX dsk_vol_row_x
    jc .done                    ; a free row or out of range
    mov al, [bx+DV_KIND]
    cmp al, DVK_DRV             ; BIOS (0) or a block driver's (1); a
    ja .done                    ; REDIRECTED volume (the cable, the RAM disk)
                                ; has no FAT of ours to keep a cache on

    ; --- 2. where the user is, banked, and the volume's root ---------------
    mov al, [disk_drive]
    CSDO mov [CSEG bp+asw_udrv-asw_v], al
    mov ax, [dsk_cwd]
    CSDO mov [CSEG bp+asw_ucwd-asw_v], ax
    mov ax, ASW_KB              ; --- 3. the work claim ---
    mov bx, MEM_K_ASC
    ASWX mem_claim_x
    jc .back                    ; a refusal is the normal path: next time
    CSDO mov [CSEG bp+asw_seg-asw_v], dx
    mov al, [cs:bp+asw_vol-asw_v]
    xor bx, bx                  ; the root
    ASWX dsk_goto
    jc .ferr

    call asw_begin              ; counters, and the new file zeroed
    xor ax, ax
    call asw_dir                ; --- 4. the root: its packages, its folders
    call asw_old                ; ...and the old file it named
    call asw_pkgs
    xor cx, cx
.fold:
    cmp cx, [cs:bp+asw_nfold-asw_v]
    jae .built
    mov es, [cs:bp+asw_seg-asw_v]
    mov si, cx
    shl si, 1
    mov ax, [es:si+ASW_FOLD]
    push cx
    call asw_dir                ; --- 5. each folder one level down
    call asw_pkgs
    pop cx
    inc cx
    jmp short .fold

.built:
    cmp word [cs:bp+asw_err-asw_v], FERR_IO
    je .back                    ; a walk's read failed: the old file stands
    call asw_close              ; CX = the file's size; CF = nothing to write
    jc .same
    mov es, [cs:bp+asw_seg-asw_v]        ; --- 7. the same bytes write nothing ---
    cmp cx, [cs:bp+asw_osz-asw_v]
    jne .write
    cmp cx, [cs:bp+asw_ofs-asw_v]
    jne .write
    push cx
    push ds
    push es
    pop ds
    mov si, ASW_OLD
    mov di, ASW_NEW
    cld
    repe cmpsb
    pop ds
    pop cx
    je .same
.write:                         ; --- 8. hidden + system, in the root ---
    mov si, asw_s_name
    mov di, assoc_fnb           ; staged into the kernel segment, where every
    push es                     ; name a dskw_* call takes is read: a module's
    push ds                     ; strings live in the image (SPEC.md 2.8.6),
    pop es                      ; and assoc_fnb is the search's own name buffer,
    push cx                     ; idle whenever a flush runs
    mov cx, 10
    cld
.nm:
    cs lodsb
    stosb
    loop .nm
    pop cx
    pop es
    mov si, assoc_fnb
    mov bx, ASW_NEW
    xor dx, dx
    call COLD_SEG:dwf_dskw_write_sys
    jnc .wrote
    CSDO mov [CSEG bp+asw_err-asw_v], ax
    mov al, [cs:bp+asw_vol-asw_v]        ; --- 9. REFUSED: no more posts for it this
    mov [asc_wno], al           ; session (write-protected, full, read-only)
    jmp short .back
.wrote:
    mov byte [asc_vol], 0xFF    ; the next harvest re-absorbs what we wrote
.same:
    CSDO mov word [CSEG bp+asw_err-asw_v], 0
    jmp short .back
.ferr:
    CSDO mov [CSEG bp+asw_err-asw_v], ax

    ; --- 10. the capture spent, the user's place back, the claim freed ----
.back:
    mov byte [ico_capn], 0      ; per ACTION, whatever happened (54.7.6.2)
    mov al, [cs:bp+asw_udrv-asw_v]
    mov bx, [cs:bp+asw_ucwd-asw_v]
    ASWX dsk_goto               ; a failure leaves what assoc_back's would
    mov dx, [cs:bp+asw_seg-asw_v]
    or dx, dx
    jz .done
    mov bx, MEM_K_ASC
    ASWX mem_free_x
.done:
    mov ax, [cs:bp+asw_err-asw_v]
    cmp ax, 1                   ; CF = (AX == 0) ? 0 : 1 - `cmp` borrows only
    cmc                         ; for 0, and the complement is the answer
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

; -----------------------------------------------------------------------------
; asw_begin - every counter, and the new file zeroed (module internal)
; -----------------------------------------------------------------------------
asw_begin:
    mov es, [cs:bp+asw_seg-asw_v]
    mov di, ASW_NEW
    mov cx, ASC_KB * 512        ; words
    xor ax, ax
    cld
    rep stosw
    CSDO mov [CSEG bp+asw_nf-asw_v], ax  ; the FRONT index: declaring packages, up
    CSDO mov word [CSEG bp+asw_nb-asw_v], ASC_NAPP ; ...the BACK one, the rest, down
    CSDO mov [CSEG bp+asw_ne-asw_v], ax
    CSDO mov [CSEG bp+asw_nfold-asw_v], ax
    CSDO mov [CSEG bp+asw_onapp-asw_v], ax ; no old file until asw_old finds one
    CSDO mov [CSEG bp+asw_onext-asw_v], ax
    CSDO mov [CSEG bp+asw_osz-asw_v], ax
    CSDO mov [CSEG bp+asw_dclus-asw_v], ax
    ret

; -----------------------------------------------------------------------------
; asw_dir - one directory's packages into the list (module internal)
; in:  AX = its first cluster, 0 = the root - which also collects the root's
;      FOLDERS and ASSOC.DAT's own entry
;
; The raw entries, through the kernel's one directory iterator (SPEC.md 19.2):
; a package is an 8.3 name with the extension O88, not a volume label, not a
; folder and not deleted, whose first cluster is a cluster. The KEY is its raw
; size's low word, which is what the cache and the store both key on (SPEC.md
; 54.7, 25.9.4) - never what a compressed file expands to.
; -----------------------------------------------------------------------------
asw_dir:
    CSDO mov [CSEG bp+asw_pcl-asw_v], ax
    CSDO mov word [CSEG bp+asw_npkg-asw_v], 0
    ASWX dsk_dirw_start_x
.sec:
    ASWX dsk_dirw_get_x         ; dsk_secbuf (LOW_SEG = SS) holds a sector
    jnc .got
    jz .end                     ; ...or the directory ended, or a read FAILED,
    CSDO mov word [CSEG bp+asw_err-asw_v], FERR_IO ; and a flush that missed part
.end:                           ; of the volume writes nothing (ascw_flush_x)
    ret
.got:
    mov si, dsk_secbuf
    mov cx, 512 / 32
.ent:
    mov al, [ss:si]
    or al, al
    jz .out                     ; 0x00 ends the directory entirely
    cmp al, 0xE5
    je .next
    mov al, [ss:si+11]          ; the attribute
    cmp al, 0x0F
    je .next                    ; a long-name fragment
    test al, 0x08
    jnz .next                   ; the volume label
    test al, 0x10
    jnz .dir
    cmp word [cs:bp+asw_pcl-asw_v], 0
    jne .pkg
    push si                     ; the ROOT's own ASSOC.DAT?
    push cx
    mov di, asc_s_name          ; 'ASSOC   DAT', the raw 8.3 form (.text)
    mov cx, 11
.cmp:
    mov al, [ss:si]
    cmp al, [di]
    jne .ncmp
    inc si
    inc di
    loop .cmp
.ncmp:                          ; CX = 0 exactly when all eleven matched: the
    or cx, cx                   ; `inc di` above clears ZF, so the compare's
    pop cx                      ; own flag cannot be read here
    pop si
    jnz .pkg
    mov ax, [ss:si+26]
    CSDO mov [CSEG bp+asw_dclus-asw_v], ax
    mov ax, [ss:si+28]
    CSDO mov [CSEG bp+asw_dsz-asw_v], ax
    mov ax, [ss:si+30]
    CSDO mov [CSEG bp+asw_dszh-asw_v], ax
    jmp .next
.pkg:
    cmp word [ss:si+8], 'O8'
    jne .next
    cmp byte [ss:si+10], '8'
    jne .next
    mov ax, [ss:si+26]
    cmp ax, 2
    jb .next
    mov di, [cs:bp+asw_npkg-asw_v]
    cmp di, ASW_NPKG
    jae .next                   ; a folder of more than 96 packages: the rest
    CSDO inc word [CSEG bp+asw_npkg-asw_v] ; stay as their old rows say, or uncached
    mov es, [cs:bp+asw_seg-asw_v]
    push ax
    mov ax, ASW_LSZ
    mul di                      ; DX is spent: nothing here holds it
    mov di, ax
    add di, ASW_LIST
    pop ax
    push si
    push cx
    mov cx, 8                   ; the stem: the raw name, space-padded, is
.stem:                          ; exactly the cache's shape (SPEC.md 54.2)
    mov bl, [ss:si]
    mov [es:di], bl
    inc si
    inc di
    loop .stem
    pop cx
    pop si
    mov bx, [ss:si+28]
    mov [es:di], bx             ; +8  the size's low word
    mov [es:di+2], ax           ; +10 the first cluster
    mov bx, [cs:bp+asw_pcl-asw_v]
    mov [es:di+4], bx           ; +12 the folder it is in
    jmp short .next
.dir:
    cmp word [cs:bp+asw_pcl-asw_v], 0    ; folders are collected in the ROOT only:
    jne .next                   ; one level, build_assoc's shape
    mov ax, [ss:si+26]
    cmp ax, 2
    jb .next
    mov di, [cs:bp+asw_nfold-asw_v]
    cmp di, ASW_NFOLD
    jae .next
    CSDO inc word [CSEG bp+asw_nfold-asw_v]
    mov es, [cs:bp+asw_seg-asw_v]
    shl di, 1
    mov [es:di+ASW_FOLD], ax
.next:
    add si, 32
    dec cx
    jz .nsec                    ; (the loop body is past a short `loop`)
    jmp .ent
.nsec:
    jmp .sec
.out:
    ret

; -----------------------------------------------------------------------------
; asw_old - read the old ASSOC.DAT the root walk found (module internal)
;
; asc_use_x's tests, every one (SPEC.md 54.7.1): under 64KB and no more than
; ASC_KB*2 sectors (the buffer is fixed), version 2's magic, both counts
; inside this build's caps, and the counts fitting the bytes actually read. A
; file that fails any of them is the same as no file: every row is then read
; from the store or the header, and the write replaces it.
; -----------------------------------------------------------------------------
asw_old:
    mov ax, [cs:bp+asw_dclus-asw_v]
    cmp ax, 2
    jb .no
    cmp word [cs:bp+asw_dszh-asw_v], 0
    jne .no
    mov dx, [cs:bp+asw_dsz-asw_v]
    CSDO mov [CSEG bp+asw_ofs-asw_v], dx
    add dx, 511
    mov cl, 9
    shr dx, cl
    jz .no
    cmp dx, ASC_KB * 2
    ja .no
    mov es, [cs:bp+asw_seg-asw_v]
    mov bx, ASW_OLD
    ASWX dsk_read_chain_x       ; ES:BX, DX sectors
    jc .ioerr
    mov si, asc_s_magic         ; DS:SI against ES:DI, ES the claim
    xor di, di
    mov cx, 7
    cld
    repe cmpsb
    jne .no
    mov ax, [es:7]              ; AL = app rows, AH = association rows
    cmp al, ASC_NAPP
    ja .no
    cmp ah, ASC_NEXT
    ja .no
    mov bl, ah
    xor bh, bh
    shl bx, 1
    shl bx, 1                   ; the ext rows' bytes
    xor ah, ah
    CSDO mov [CSEG bp+asw_onapp-asw_v], ax
    mov cl, ASC_ROW
    mul cl                      ; napp <= 32: the product fits AX
    add ax, bx
    add ax, ASC_HDR             ; what the header says the file holds
    cmp ax, [cs:bp+asw_ofs-asw_v]
    ja .no                      ; ...and more than was read: no cache
    CSDO mov [CSEG bp+asw_osz-asw_v], ax
    shr bx, 1
    shr bx, 1
    CSDO mov [CSEG bp+asw_onext-asw_v], bx
    ret
.ioerr:                         ; a file that did not READ is not no file:
    CSDO mov word [CSEG bp+asw_err-asw_v], FERR_IO ; the flush must not replace it
.no:
    xor ax, ax
    CSDO mov [CSEG bp+asw_onapp-asw_v], ax
    CSDO mov [CSEG bp+asw_onext-asw_v], ax
    CSDO mov [CSEG bp+asw_osz-asw_v], ax
    ret

; -----------------------------------------------------------------------------
; asw_pkgs - every package asw_dir listed (module internal)
; -----------------------------------------------------------------------------
asw_pkgs:
    xor cx, cx
.one:
    cmp cx, [cs:bp+asw_npkg-asw_v]
    jae .out
    mov ax, ASW_LSZ
    mul cx
    add ax, ASW_LIST
    CSDO mov [CSEG bp+asw_ent-asw_v], ax
    push cx
    call asw_one
    pop cx
    inc cx
    jmp short .one
.out:
    ret

; -----------------------------------------------------------------------------
; asw_one - one package into the new file (module internal)
; in:  [asw_ent] = its list entry's offset in the claim
;
; THREE SOURCES, IN ORDER (SPEC.md 54.7.6.4 step 5):
;   the OLD FILE, keyed (stem, size): the row is copied, its folder cluster
;     rewritten, and its association rows come with it;
;   the STORE: body and glyph from its row, declarations from the capture -
;     and with no record, a package that holds an app slot has its header read
;     (a built-in program, or one absorbed from ANOTHER volume's cache), while
;     one that holds none is plain;
;   the HEADER, in full mode only - incremental leaves a package nothing has
;     learned about out, and its folder's next harvest posts again.
; -----------------------------------------------------------------------------
asw_one:
    mov es, [cs:bp+asw_seg-asw_v]

    ; --- the old file ---
    xor cx, cx
.old:
    cmp cx, [cs:bp+asw_onapp-asw_v]
    jae .notold
    mov ax, ASC_ROW
    mul cx
    add ax, ASW_OLD + ASC_HDR
    mov di, ax
    mov si, [cs:bp+asw_ent-asw_v]
    push cx
    push di
    push ds
    push es
    pop ds
    mov cx, 10                  ; stem and size, both sides
    cld
    repe cmpsb
    pop ds
    pop di
    pop cx
    je asw_take_old
    inc cx
    jmp short .old

.notold:
    CSDO mov byte [CSEG bp+asw_hblk-asw_v], 0 ; no association block yet
    CSDO mov byte [CSEG bp+asw_hdr-asw_v], 0 ; ...and no header wanted yet
    cmp word [ico_seg], 0       ; a shed store is NOT re-claimed for this
    je .nostore
    mov si, [cs:bp+asw_ent-asw_v]
    mov dx, [es:si+8]
    ASWX ico_key_stem           ; ES:SI = the stem, DX = the size
    ASWX ico_find               ; AL = the row
    jc .nostore
    CSDO mov [CSEG bp+asw_row-asw_v], al

    ; body and glyph off the store row: one 72-byte run (ICO_R_GLYPH follows
    ; ICO_R_BODY, as ASC_ROWGLY follows ASC_ROWICO - disk.inc asserts it)
    push ds
    mov ah, ICO_ROW
    mul ah
    add ax, ICO_R_BODY
    mov si, ax
    mov di, ASW_STG
    mov ds, [ico_seg]
    mov cx, (DSK_ICO_SIZE + 8) / 2
    cld
    rep movsw
    pop ds

    ; the declarations: the capture's record for this row, if it kept one
    push es
    mov es, [ico_seg]
    mov cl, [ico_capn]
    xor ch, ch
    cmp cl, ICO_CAP_MAX
    jbe .capn
    mov cl, ICO_CAP_MAX
.capn:
    mov bx, ICO_CAP             ; the first record (disk.inc: a row byte
    mov al, [cs:bp+asw_row-asw_v]   ; and the block, ICO_CAP_RSZ a record)
    jcxz .nocap
.cap:
    cmp [es:bx], al
    je .rec
    add bx, ICO_CAP_RSZ
    loop .cap
.nocap:
    pop es
    ; no record: does it hold an APP SLOT? Then it may declare and the
    ; capture never saw it - read its header for the block alone
    push es
    push ds
    pop es
    mov si, [cs:bp+asw_ent-asw_v]
    mov di, assoc_stemb         ; assoc_app_of reads the stem through DS
    mov cx, 8
    push ds
    mov ds, [cs:bp+asw_seg-asw_v]
    cld
    rep movsb
    pop ds
    pop es
    CSDO mov byte [CSEG bp+asw_hdr-asw_v], 1 ; ...declarations only: the body is the store's
    cmp byte [cs:bp+asw_mode-asw_v], 0   ; FULL mode trusts nothing it did not read:
    jne .hdr                    ; the installer's file must be exact
    mov si, assoc_stemb
    ASWX assoc_app_of
    jnc .hdr                    ; a slot: it may declare, the capture never
                                ; saw it (a built-in program, or a row
                                ; absorbed from ANOTHER volume's cache)
    call asw_slotfull           ; no slot, and there was ROOM for one: a
    jnc .emit                   ; package that declared would have taken it
                                ; (assoc_note_app, asc_merge_ext), so this
    jmp short .hdr              ; one is plain. With the table FULL that
                                ; proves nothing - read it
.rec:
    lea si, [bx+1]              ; past the row byte: the block
    pop es                      ; ES = the claim again; the record is in the
    push ds                     ; STORE's
    mov ds, [ico_seg]
    mov di, ASW_BLK
    mov cx, 8
    cld
    rep movsw
    pop ds
    CSDO mov byte [CSEG bp+asw_hblk-asw_v], 1
    jmp short .emit

.nostore:
    cmp byte [cs:bp+asw_mode-asw_v], 0
    je .skip                    ; incremental: nothing learned it, leave it out
    CSDO mov byte [CSEG bp+asw_hdr-asw_v], 2 ; full: the header for everything
.hdr:
    call asw_header
    jnc .emit
    cmp byte [cs:bp+asw_hdr-asw_v], 2
    je .skip                    ; nothing to make a row of
.emit:
    jmp asw_emit
.skip:
    ret

; -----------------------------------------------------------------------------
; asw_take_old - copy old row CX into the new file (asw_one's continuation)
; in:  CX = the old row, DI = its offset; ES = the claim
; -----------------------------------------------------------------------------
asw_take_old:
    CSDO mov [CSEG bp+asw_oix-asw_v], cl
    ; does any old association row name it? Then it DECLARES: the front
    call asw_oext
    mov al, 0                   ; AL = "declares": one bit, from the scan
    adc al, 0
    call asw_index              ; AL = 1 front / 0 back -> AL = the new row
    jc .out                     ; full
    CSDO mov [CSEG bp+asw_ix-asw_v], al
    push di
    call asw_rowoff             ; DI = the new row's offset
    pop si                      ; SI = the old row
    push di
    push ds
    push es
    pop ds
    mov cx, ASC_ROW / 2
    cld
    rep movsw
    pop ds
    pop di
    mov si, [cs:bp+asw_ent-asw_v]
    mov ax, [es:si+12]          ; ...where it lives NOW (SPEC.md 54.7.1)
    mov [es:di+ASC_ROWCLUS], ax
    ; and its association rows, re-indexed
    mov cx, [cs:bp+asw_onext-asw_v]
    jcxz .out
    mov ax, ASC_ROW
    mul word [cs:bp+asw_onapp-asw_v]
    add ax, ASW_OLD + ASC_HDR
    mov si, ax
.ext:
    mov al, [cs:bp+asw_oix-asw_v] ; each pass: asw_ext leaves the NEW index
    cmp [es:si+3], al
    jne .nx
    push si
    call asw_ext                ; ES:SI = 3 bytes
    pop si
.nx:
    add si, 4
    loop .ext
.out:
    ret

; asw_oext - CF = 1 when an old association row names old row [asw_oix]
asw_oext:
    push cx
    push si
    mov cx, [cs:bp+asw_onext-asw_v]
    jcxz .no
    mov ax, ASC_ROW
    mul word [cs:bp+asw_onapp-asw_v]
    add ax, ASW_OLD + ASC_HDR
    mov si, ax
    mov al, [cs:bp+asw_oix-asw_v]
.s:
    cmp [es:si+3], al
    je .yes
    add si, 4
    loop .s
.no:
    clc
    jmp short .out
.yes:
    stc
.out:
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; asw_header - read the package's own header (module internal)
; in:  [asw_ent], [asw_hdr] = 1 the block only / 2 the body, glyph and block
; out: CF=1 = no usable header
;
; THE INSTALLER'S TESTS (hd_ia_row, build_assoc): 'O8' and PKG_FMT, and every
; field taken only if the bytes that arrived reach it - SPEC.md 18.2, every
; byte off a disk is hostile. The icon is at 32 when flags bit 0 says so; the
; block follows the icon, or the header with no icon; the glyph is at
; LD_H_GLYPH and only behind both (the tool requires all three in front of it).
; -----------------------------------------------------------------------------
asw_header:
    mov es, [cs:bp+asw_seg-asw_v]
    mov si, [cs:bp+asw_ent-asw_v]
    mov ax, [es:si+10]          ; the first cluster
    mov cx, DSK_PEEK
    ASWX dsk_peek_x             ; LOW_SEG:dsk_secbuf, AX = bytes arrived
    mov es, [cs:bp+asw_seg-asw_v]        ; (dsk_peek clobbers ES, BX and CX)
    jc .no
    CSDO mov [CSEG bp+asw_take-asw_v], ax
    cmp ax, 32
    jb .no
    cmp word [ss:dsk_secbuf], 'O8'
    jne .no
    cmp byte [ss:dsk_secbuf+2], PKG_FMT
    jne .no
    mov bl, [ss:dsk_secbuf+3]   ; BL = the flags, for the whole routine
    cmp byte [cs:bp+asw_hdr-asw_v], 2
    jne .block
    ; --- the icon and the glyph, or the all-zero sentinels ---
    mov di, ASW_STG
    mov cx, (DSK_ICO_SIZE + 8) / 2
    xor ax, ax
    cld
    rep stosw
    test bl, 1
    jz .block
    cmp word [cs:bp+asw_take-asw_v], 96
    jb .block
    mov si, dsk_secbuf + 32
    mov di, ASW_STG
    mov cx, DSK_ICO_SIZE / 2
    call asw_fromlow
    mov al, bl
    and al, 0x23
    cmp al, 0x23                ; a glyph, behind the icon AND the block
    jne .block
    cmp word [cs:bp+asw_take-asw_v], LD_H_GLYPH + 8
    jb .block
    mov si, dsk_secbuf + LD_H_GLYPH
    mov di, ASW_STG + DSK_ICO_SIZE
    mov cx, 4
    call asw_fromlow
.block:
    test bl, 2                  ; it declares at all?
    jz .ok
    mov ax, 32
    test bl, 1
    jz .based
    mov ax, 96
.based:
    mov si, ax
    add ax, 16
    cmp ax, [cs:bp+asw_take-asw_v]
    ja .ok                      ; promised and not delivered: no block
    add si, dsk_secbuf
    mov di, ASW_BLK
    mov cx, 8
    call asw_fromlow
    CSDO mov byte [CSEG bp+asw_hblk-asw_v], 1
.ok:
    clc
    ret
.no:
    stc
    ret

; asw_slotfull - CF=1 when every app slot (SPEC.md 54.2) is taken: a free one
; is a stem whose first byte is 0, which a sanitized name never is
asw_slotfull:
    push bx
    mov bx, assoc_stem
.s:
    cmp byte [bx], 0
    je .free
    add bx, 8
    cmp bx, assoc_stem + ASSOC_NAPP * 8
    jb .s
    stc
    pop bx
    ret
.free:
    clc
    pop bx
    ret

; asw_fromlow - CX words from LOW_SEG:SI to ES:DI (SS is LOW_SEG)
asw_fromlow:
    push ds
    push ss
    pop ds
    cld
    rep movsw
    pop ds
    ret

; asw_cold - ASWX's body: far-call the resident .cold routine named by the
; word after our caller's `call`, through wm_cbd (`call bp / retf` in
; COLD_SEG), and return PAST that word. Every register reaches the routine and
; comes back as it left them, BP restored here; `pop` and `ret` write no flag,
; so the routine's CF is the caller's
asw_cold:                       ; STKBALANCE-OK: steps its own return address
    push bp                     ; past the inline word, and pushes and pops
    mov bp, sp                  ; nothing else
    add word [bp+2], 2
    mov bp, [bp+2]
    mov bp, [cs:bp-2]
    call COLD_SEG:wm_cbd
    pop bp
    ret

; -----------------------------------------------------------------------------
; asw_emit - one row from ASW_STG and the block (asw_one's continuation)
; -----------------------------------------------------------------------------
asw_emit:
    mov es, [cs:bp+asw_seg-asw_v]
    call asw_ndecl              ; AL = usable declarations
    or al, al
    jz .side
    mov al, 1
.side:
    call asw_index
    jc .out                     ; full: this one is dropped
    CSDO mov [CSEG bp+asw_ix-asw_v], al
    call asw_rowoff             ; DI = the row
    mov si, [cs:bp+asw_ent-asw_v]
    push ds
    push es
    pop ds
    cld
    movsw                       ; the stem, 8...
    movsw
    movsw
    movsw
    movsw                       ; ...the size, 2
    inc si                      ; (the file's own first cluster: not a field)
    inc si
    movsw                       ; the folder, 2 - ASC_ROWCLUS
    xor ax, ax
    stosw                       ; the four reserved bytes
    stosw
    mov si, ASW_STG             ; the icon and the glyph, 72
    mov cx, (DSK_ICO_SIZE + 8) / 2
    rep movsw
    pop ds
    cmp byte [cs:bp+asw_hblk-asw_v], 0
    je .out
    mov si, ASW_BLK             ; and the extensions it declares
    mov cl, [es:si]
    cmp cl, ASSOC_DECLMAX
    ja .out                     ; a corrupt count declares nothing
    xor ch, ch
    jcxz .out
    inc si
.e:
    push si
    call asw_dok
    jc .bad
    call asw_ext
.bad:
    pop si
    add si, 3
    loop .e
.out:
    ret

; asw_ndecl - AL = the usable declarations in ASW_BLK (0 with no block)
asw_ndecl:
    push cx
    push si
    xor al, al
    cmp byte [cs:bp+asw_hblk-asw_v], 0
    je .out
    mov si, ASW_BLK
    mov cl, [es:si]
    cmp cl, ASSOC_DECLMAX
    ja .out
    xor ch, ch
    jcxz .out
    inc si
.one:
    call asw_dok
    jc .skip
    inc al
.skip:
    add si, 3
    loop .one
.out:
    pop si
    pop cx
    ret

; asw_dok - is the extension at ES:SI usable? CF=0 = yes (hd_ia_dok's test:
; not blank, and not O88 - a package cannot claim the package extension)
asw_dok:
    cmp byte [es:si], ' '
    jbe .no
    cmp word [es:si], 'O8'
    jne .ok
    cmp byte [es:si+2], '8'
    je .no
.ok:
    clc
    ret
.no:
    stc
    ret

; asw_ext - stage the extension at ES:SI against new row [asw_ix]. The ext
; table being full drops the rest, assoc_declare's own rule. Preserves CX.
asw_ext:
    mov di, [cs:bp+asw_ne-asw_v]
    cmp di, ASC_NEXT
    jae .out
    CSDO inc word [CSEG bp+asw_ne-asw_v]
    shl di, 1
    shl di, 1
    add di, ASW_EXT
    push ds
    push es
    pop ds
    movsw
    movsb
    pop ds
    mov al, [cs:bp+asw_ix-asw_v]
    stosb
.out:
    ret

; -----------------------------------------------------------------------------
; asw_index - take a row index (hd_ia_front / hd_ia_back, SPEC.md 52.10.14)
; in:  AL = 1 a declaring package (the FRONT) / 0 a plain one (the BACK)
; out: CF=0, AL = the index; CF=1 = full and this one is dropped
;
; The array fills from BOTH ENDS in one pass, because nothing here can sort
; what it has not seen yet. When the halves meet a further plain package is
; dropped and a further declaring one OVERWRITES the topmost plain row - safe
; because no association row ever names a plain row, which is also what lets
; asw_close slide the plain rows down without moving any index.
; -----------------------------------------------------------------------------
asw_index:
    push bx
    mov bx, [cs:bp+asw_nb-asw_v]
    or al, al
    jz .back
    mov ax, [cs:bp+asw_nf-asw_v]
    cmp ax, bx
    jb .front
    cmp bx, ASC_NAPP            ; full, and no plain row to evict
    jae .no
    CSDO inc word [CSEG bp+asw_nb-asw_v] ; evict the lowest plain row: its index IS the
.front:                         ; one this front row is about to take
    CSDO inc word [CSEG bp+asw_nf-asw_v]
    jmp short .yes
.back:
    cmp [cs:bp+asw_nf-asw_v], bx
    jae .no
    dec bx
    CSDO mov [CSEG bp+asw_nb-asw_v], bx
    mov ax, bx
.yes:
    clc
    pop bx
    ret
.no:
    stc
    pop bx
    ret

; asw_rowoff - DI = row [asw_ix]'s offset in the new file (AX spent)
asw_rowoff:
    mov al, [cs:bp+asw_ix-asw_v]
    mov ah, ASC_ROW
    mul ah                      ; <= 31 * 88: the product fits AX
    add ax, ASW_NEW + ASC_HDR
    mov di, ax
    ret

; -----------------------------------------------------------------------------
; asw_close - compact, head and size the new file (hd_ia_close's shape)
; out: CF=0 and CX = its size; CF=1 = no packages, so no file to write
; -----------------------------------------------------------------------------
asw_close:
    mov es, [cs:bp+asw_seg-asw_v]
    mov ax, ASC_NAPP
    sub ax, [cs:bp+asw_nb-asw_v]         ; the plain rows
    mov dx, ax
    add ax, [cs:bp+asw_nf-asw_v]
    or ax, ax
    jz .none
    CSDO mov [CSEG bp+asw_napp-asw_v], ax
    ; --- the plain rows down against the declaring ones ---
    mov ax, ASC_ROW
    mul dx                      ; DX = 0 after, which nothing reads
    mov cx, ax
    jcxz .exts
    mov ax, ASC_ROW
    mul word [cs:bp+asw_nb-asw_v]
    add ax, ASW_NEW + ASC_HDR
    mov si, ax
    mov ax, ASC_ROW
    mul word [cs:bp+asw_nf-asw_v]
    add ax, ASW_NEW + ASC_HDR
    mov di, ax
    call asw_move
.exts:
    mov ax, ASC_ROW
    mul word [cs:bp+asw_napp-asw_v]
    add ax, ASW_NEW + ASC_HDR
    mov di, ax                  ; where the ext rows belong
    mov cx, [cs:bp+asw_ne-asw_v]
    shl cx, 1
    shl cx, 1
    mov si, ASW_EXT
    push di
    push cx
    call asw_move
    pop cx
    pop di
    add di, cx
    sub di, ASW_NEW
    mov cx, di                  ; the whole file's size
    ; --- the header ---
    push cx
    mov si, asc_s_magic         ; 'OS88AC' and the version, from .text
    mov di, ASW_NEW
    mov cx, 7
    cld
    rep movsb
    mov al, [cs:bp+asw_napp-asw_v]
    stosb
    mov al, [cs:bp+asw_ne-asw_v]
    stosb                       ; ...and +9..15 stay the zeros asw_begin wrote
    pop cx
    clc
    ret
.none:
    stc
    ret

; asw_move - CX bytes from ES:SI to ES:DI, DOWNWARD (DI <= SI), inside the claim
asw_move:
    jcxz .out
    cmp di, si
    je .out
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
.out:
    ret

asw_s_name: db 'ASSOC.DAT', 0   ; the write's name (read through CS, staged)

; THE SCRATCH IS IN THE IMAGE, as zeros, and not in a nobits `.modfb`: the
; claim is the image's size rounded up to a KB (mod_need), and a nobits tail
; is a CLIFF - the image growing past a KB boundary takes the room away all at
; once (MODULE-SELFCONTAIN-PLAN 3.2). Forty-odd zero bytes cost the compressed
; file a few bytes and remove the cliff. Written before it is read: the entry
; and asw_begin set every cell.
asw_v:                            ; ASV's base: every cell below is a d8
asw_vol:    db 0
asw_mode:   db 0
asw_udrv:   db 0                  ; the user's volume...
asw_ix:     db 0                  ; the new row being written
asw_ucwd:   dw 0                  ; ...and folder, banked
asw_seg:    dw 0                  ; the work claim, 0 = none
asw_err:    dw 0                  ; the answer: 0 or FERR_*
asw_nf:     dw 0                  ; the next FRONT row (declaring)
asw_nb:     dw 0                  ; the next BACK row (plain), filling down
asw_ne:     dw 0                  ; association rows staged
asw_napp:   dw 0                  ; app rows, once asw_close knows
asw_nfold:  dw 0                  ; the root's folders
asw_npkg:   dw 0                  ; this directory's packages
asw_pcl:    dw 0                  ; ...and its cluster
asw_ent:    dw 0                  ; the package being placed
asw_dclus:  dw 0                  ; the old ASSOC.DAT: its first cluster...
asw_dsz:    dw 0                  ; ...its size...
asw_dszh:   dw 0    
asw_ofs:    dw 0                  ; ...as a word, once it is under 64KB
asw_osz:    dw 0                  ; ...and what its header says it holds, 0 = none
asw_onapp:  dw 0                  ; its app rows
asw_onext:  dw 0                  ; ...and association rows
asw_take:   dw 0                  ; header bytes that arrived
asw_row:    db 0                  ; the store row
asw_oix:    db 0                  ; the old row
asw_hblk:   db 0                  ; 1 = ASW_BLK holds a block
asw_hdr:    db 0                  ; 0 / 1 block only / 2 everything

section .text

%endif                          ; KD_BUILD
%endif                          ; OS88_ASSOC

section .text
