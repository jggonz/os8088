; =============================================================================
; os8088 - RAMDISK.DRV / rdimg.inc
;
; The settings, the mount, and the volume as one file (SPEC.md 62.9.12).
;
; TWO WRITES AND TWO READS, and that is the whole format: a fixed 4,096-byte
; metadata block carrying the directory and the chain table, then the arena
; verbatim. The arena is copied rather than repacked, so every extent lands
; back where it was and a restore is a read plus a fix of three words.
;
; **RDI_META AND THE CHUNK ARE THE SIZES THEY ARE BECAUSE OF
; OSAPI_FILE_APPEND**, which refuses a file whose length is not a whole number
; of clusters (SPEC.md 18.4.4), and OSAPI_FILE_READ_AT, which says the same of
; an offset and a capacity. 4,096 and 32,768 are multiples of every cluster
; size this OS makes; the store's own size is held to a multiple of RD_STEPKB
; (8KB) so that what is LEFT at the last chunk is a multiple of 4,096 too.
; That last one is the easy one to lose: without it the final append of a
; 40KB store is 8KB on a 4KB-cluster volume and fine, and 5KB on a 216KB one
; and refused with FERR_NAME - a size-dependent failure with nothing on screen
; to connect it to the size.
; =============================================================================

; -----------------------------------------------------------------------------
; rd_kb_clamp - AX = a size in KB -> a legal one
; out: AX
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
rd_kb_clamp:
    cmp ax, RD_MINKB
    jae .lo
    mov ax, RD_MINKB
.lo:
    push bx
    call rd_kb_max              ; BX = what THIS machine will actually give
    cmp ax, bx
    jbe .hi
    mov ax, bx
.hi:
    pop bx
    push cx                     ; ...and ROUNDED DOWN to a whole step, which is
    mov cl, 3                   ; what keeps the last chunk of a preserve
    shr ax, cl                  ; cluster-aligned (see the header). DOWN and
    shl ax, cl                  ; not up, or the round could put it back past
    pop cx                      ; the ceiling the clamp above just applied
    cmp ax, RD_MINKB            ; the rounding above may not take it under the
    jae .out                    ; floor, which on a machine with almost no heap
    mov ax, RD_MINKB            ; left is exactly where it would land
.out:
    ret

; -----------------------------------------------------------------------------
; rd_xms_here - has this machine a store above 1MB to offer?
; out: CF = 0 yes; CF = 1 = none, and RDL_XMS is not a choice
; clobbers: flags
;
; The FIGURE and never the CPU tier, which is SPEC.md 41.8's rule: a 386 whose
; A20 gate did not verify answers 0 here just as an 8088 does, and so does a
; machine the overlay never loaded on.
; -----------------------------------------------------------------------------
rd_xms_here:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_XMEM_CAPS        ; AX = KB the pool can still hand out
    cmp ax, 1                   ; CF = 1 exactly when that is none
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_kb_max - the largest store THIS MACHINE will fund right now (SPEC.md
;             62.9.10.1)
; in:  nothing ([rd_loc] says which memory is being asked about)
; out: BX = KB
; clobbers: BX (the output), flags
;
; **THE CEILING IS A FACT ABOUT THE MACHINE AND NOT A CONSTANT**, which is the
; whole of what makes the extended store worth having: RD_MAXKB is only the
; arithmetic limit - 2,048 extents of 8KB - and a 4MB machine should offer 4MB
; where a 256KB one offers what is left of its heap.
;
; It is asked LIVE, on every clamp, because the answer moves: an application
; opening takes heap, and a size chosen against a figure from five minutes ago
; is a Mount that fails for reasons the user cannot see.
;
; Conventional: the LARGEST FREE RUN, less the chain table, which comes out of
; the same heap. Extended: what the pool will still hand out, but ALSO capped
; by conventional room for the table and the bounce - a store up there with no
; way down to it is not a store, and the failure without this test is a Mount
; that claims 4MB of extended memory and then cannot find 4KB for its table.
; -----------------------------------------------------------------------------
rd_kb_max:
    push ax
    push cx
    push dx
    call OSAPI_MEM_AVAIL        ; AX = the largest free RUN in KB, DX = total
    mov bx, ax
    cmp byte [rd_loc], RDL_XMS
    jne .conv
    cmp bx, RD_TABMAXKB + RD_EXTMAXKB   ; **A GATE, NOT A CAP** (SPEC.md
    jb .none                    ; 62.9.10.3). Conventional memory funds the
                                ; chain TABLE and the BOUNCE - at most 12KB
                                ; between them - and the store itself is a
                                ; megabyte up. Subtracting it from the ceiling
                                ; made the extended store no bigger than the
                                ; conventional one it exists to escape: on a
                                ; 640KB machine with ~276K of free heap the
                                ; page offered 264K and would not go past it,
                                ; whatever the pool held
    call OSAPI_XMEM_CAPS        ; AX = KB the pool can still hand out
    or ax, ax
    jz .none
    mov bx, ax                  ; ...and THAT is the ceiling, clamped to
    jmp short .cap              ; RD_MAXKB below
.conv:
    sub bx, RD_TABMAXKB         ; the table is heap too - and here so is the
    jbe .none                   ; store, which is why THIS one really is a cap
.cap:
    cmp bx, RD_MAXKB
    jbe .out
    mov bx, RD_MAXKB
    jmp short .out
.none:
    mov bx, RD_MINKB            ; nothing to give: the floor, so the page still
                                ; shows a number and Mount is what reports it
.out:
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; THE SETTINGS BLOB (SPEC.md 51.9.1)
; =============================================================================

; -----------------------------------------------------------------------------
; rd_cfg_load - the blob into the live state
; in:  nothing
; out: nothing
;
; A blob nobody has written reads back as zeroes, so RDC_VER is what tells a
; saved state from the defaults. Anything else leaves every default standing,
; which is SPEC.md 51.5 rule 2 one level down.
; -----------------------------------------------------------------------------
rd_cfg_load:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es                      ; the blob comes to ES:SI, and ours is DS
    mov si, rd_blob
    mov cx, DRV_RBLOB_SZ
    xor al, al                  ; read
    call OSAPI_DRV_CFG
    jc .out
    cmp byte [rd_blob+RDC_VER], RDC_VER_V
    jne .out                    ; a version this build does not know: the
                                ; defaults stand, and the bytes are NOT
                                ; rewritten until something changes one
    mov si, rd_blob + RDC_KB    ; the live settings ARE the blob's layout
    mov di, rd_kb               ; (ramdisk.asm), so this is the whole read
    mov cx, RDC_SZ - RDC_KB
    cld
    rep movsb
    mov [rd_iname+12], cl       ; CX = 0: the name is NUL-ended whatever the
                                ; blob said, SYSTEM.CFG being a disk file
    mov al, RDL_CONV            ; ...then the two values are VALIDATED. The
    xchg al, [rd_loc]           ; size is clamped against the conventional
    push ax                     ; heap, which is what [rd_loc] has always been
    mov ax, [rd_kb]             ; at this point - a fresh image, loaded a
    call rd_kb_clamp            ; moment ago - and the blob's own location is
    mov [rd_kb], ax             ; put back only below
    pop ax
    cmp al, RDL_XMS
    jne .out                    ; anything this build does not know is the
                                ; conventional heap, which every machine has
    call rd_xms_here            ; ...AND SO IS `Xms` ON A MACHINE WITH NONE.
    jc .out                     ; A settings disk carried from a 386 to an
                                ; 8088 would otherwise select a store that
                                ; cannot be claimed - and the page greys both
                                ; radios on that machine, so there would be no
                                ; way back: Mount refuses for ever and the one
                                ; control that could undo it is disabled.
                                ; SPEC.md 39.11's `VM` key refusing an adapter
                                ; the machine does not have, one layer down
    mov [rd_loc], al
.out:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_cfg_mark - the live state into the blob, staged for the panel's close
; in:  nothing
; out: nothing
;
; ONE SAVE PATH AND IT NEVER TOUCHES A DISK (SPEC.md 31.8): every control on
; the page ends here, and the Control Panel's teardown is what writes the file.
; -----------------------------------------------------------------------------
rd_cfg_mark:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, rd_kb               ; the live settings, which ARE the blob's
    mov di, rd_blob + RDC_KB    ; layout from RDC_KB on (ramdisk.asm)
    mov cx, RDC_SZ - RDC_KB
    cld
    rep movsb
    mov byte [rd_blob+RDC_VER], RDC_VER_V
    mov si, rd_blob
    mov cl, DRV_RBLOB_SZ        ; CH is 0 after the rep
    mov al, 1                   ; write - into the kernel's copy, not the disk
    call OSAPI_DRV_CFG
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; MOUNT AND UNMOUNT (SPEC.md 62.9.13)
; =============================================================================

; -----------------------------------------------------------------------------
; rd_mount - claim the store and put the volume on the desktop
; in:  nothing
; out: CF = 0 mounted; CF = 1 and AL = RDERR_*
; clobbers: AL, flags
;
; THE ONLY THING THAT PUTS A DRIVE ICON ON THE DESKTOP. Attaching claims
; nothing and mounts nothing (SPEC.md 62.9.13): the size and the location are
; questions that can only be answered before the store is claimed, so ticking
; the row cannot be what claims it.
;
; No repaint is owed here - osapi_vol_add posts [desk_zdirty] and the UI task
; spends it (SPEC.md 26.3), which is exactly why it may be called with the gfx
; lock held.
; -----------------------------------------------------------------------------
rd_mount:
    cmp byte [rd_vol], 0xFF
    je .go
    clc                         ; already up: idempotent, so the page's button
    ret                         ; can be pressed twice without a special case
.go:
    call rd_store_free          ; A STORE WITH NO VOLUME IS STALE, and it is
                                ; not this Mount's: a Load that failed after
                                ; claiming (a short arena read, no drive
                                ; letter) leaves one behind holding the
                                ; image's chain table and the image's
                                ; geometry. rd_store_get would hand it back
                                ; as it is - every extent that image used
                                ; still marked taken under a cleared
                                ; directory, and, if Size moved since (it is
                                ; live while nothing is mounted), [rd_kb]
                                ; disagreeing with the extents, so a
                                ; Preserve walked [rd_kb] of arena past the
                                ; end of the claim. Fresh, at the live size
    call rd_store_get           ; CF = 1 and AL = RDERR_MEM
    jc .out
    call rd_dir_clear
%ifdef RDSEED
    call rd_seed_all
%endif
    call rd_mount_after         ; CF = 1 and AL = RDERR_VOL / RDERR_MNT, with
    jnc .out                    ; no row left behind - and then the store goes
    call rd_store_free          ; too, which preserves AL
    stc
.out:
    ret

; -----------------------------------------------------------------------------
; rd_unmount - take the volume away and give the store back
; in:  nothing
; out: nothing
;
; IT DISCARDS, and that is the honest reading of "unmount a RAM disk" rather
; than an oversight: the memory going back is the whole point of the control,
; and a store kept alive behind a volume nobody can reach is neither a disk
; nor free memory. Preserve is how anything on it survives - and THE DIRECTORY
; GOES BACK WITH THE STORE, being in the chain table's claim (SPEC.md
; 62.9.13.1), so there is no row left to clear: the next Mount clears the one
; it claims, and a Load fills it.
; -----------------------------------------------------------------------------
rd_unmount:
    push ax
    cmp byte [rd_vol], 0xFF
    je .out
    mov al, [rd_vol]
    call OSAPI_VOL_DEL
    mov byte [rd_vol], 0xFF
.out:
    call rd_store_free
    pop ax
    ret

; =============================================================================
; PRESERVE AND LOAD
; =============================================================================

; -----------------------------------------------------------------------------
; rd_wbuf_get / rd_wbuf_free - the 4KB block the metadata is built in
; out: CF = 0 and [rd_wseg] names it
;
; The extended path already holds a bounce and lends it: the metadata is
; written in full before the first arena chunk is staged, so the two uses
; cannot overlap, and rd_store_get sizes that claim at RDI_META even when the
; extent is smaller (SPEC.md 62.9.10). Conventional, it is a transient claim -
; a machine that never preserves never pays for it.
;
; THE LOAN IS ONLY MADE WHEN THERE IS A BOUNCE TO LEND. [rd_loc] is a
; PREFERENCE the page writes while nothing is mounted, so `Xms` with no store
; claimed is an ordinary state and [rd_bounce] is 0 in it - and lending 0 puts
; Load's 4,096 bytes over the interrupt vector table. Falling through to the
; transient claim is what a conventional machine does anyway.
;
; ...and the loan INVALIDATES THE STAGE for rd_blk_out's reason: the block
; written here lands on top of whatever extent rd_ext_seg believes is staged.
; -----------------------------------------------------------------------------
rd_wbuf_get:
    push ax
    push dx
    cmp byte [rd_loc], RDL_XMS
    jne .claim
    mov ax, [rd_bounce]
    or ax, ax                   ; CF = 0
    jz .claim
    mov word [rd_sext], RD_NOEXT
    mov [rd_wseg], ax           ; ...[rd_wown] is already 0: a loan
    jmp short .out
.claim:
    mov ax, RDI_META / 1024
    call OSAPI_MEM_CLAIM
    jc .out
    mov [rd_wseg], dx
    mov byte [rd_wown], 1       ; (a `mov`: CF = 0 stays the answer)
.out:
    pop dx
    pop ax
    ret

rd_wbuf_free:
    push ax
    push dx
    cmp byte [rd_wown], 0
    je .out
    mov dx, [rd_wseg]
    call OSAPI_MEM_FREE
    mov byte [rd_wown], 0
.out:
    mov word [rd_wseg], 0
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_blk_out - a run of the arena, ready to be handed to the file API
; in:  DX:AX = the byte offset into the arena, CX = how many (a multiple of
;      RD_EXTB, and <= the chunk)
; out: BX:0 = where those bytes are readable  (BX is a SEGMENT)
; clobbers: BX (the output), flags
;
; Conventional this is arithmetic on the arena's base; extended it is one
; OSAPI_XMEM_COPY down into the bounce. The staged extent is FORGOTTEN rather
; than written back, and that is safe by construction: rd_ext_flush writes
; through on every store write, so there is never a dirty extent to lose.
;
; A REFUSED COPY IS RECORDED AND NEVER SWALLOWED (SPEC.md 62.9.10.2): the
; bounce would still hold the PREVIOUS chunk and the caller would append it to
; the image and report success. The caller clears [rd_xerr] before its run and
; tests it once a chunk.
; -----------------------------------------------------------------------------
rd_blk_out:
    cmp byte [rd_loc], RDL_XMS
    je .xms
    push cx
    mov cl, 4
    shr ax, cl                  ; DX:AX is at most 229,376 and the offset is
    mov bx, dx                  ; a multiple of RD_EXTB, so the paragraph
    mov cl, 12                  ; number is the low 12 bits of DX over the top
    shl bx, cl                  ; 4 of AX
    or bx, ax
    add bx, [rd_arena]
    pop cx
    ret
.xms:
    mov word [rd_sext], RD_NOEXT
    mov bx, [rd_bounce]
    push di
    mov di, 1                   ; in from extended
    call rd_xcopy
    pop di
    ret

; -----------------------------------------------------------------------------
; rd_blk_in - the other direction: BX:0's CX bytes into the arena at DX:AX
; in:  DX:AX = the offset, CX = the count, BX = the segment they arrived in
; out: nothing
; clobbers: flags
;
; Conventional the read went straight into the arena and there is nothing to
; do, which is why the caller asks rd_blk_out for its destination too.
; -----------------------------------------------------------------------------
rd_blk_in:
    cmp byte [rd_loc], RDL_XMS
    jne .out
    mov word [rd_sext], RD_NOEXT
    push di
    xor di, di                  ; out to extended
    call rd_xcopy
    pop di
.out:
    ret

; -----------------------------------------------------------------------------
; rd_here_bank / rd_here_back - stand where the image is, and come back
; in:  rd_here_bank: nothing; rd_here_back: nothing
; out: nothing (rd_here_back preserves the FLAGS)
;
; drv_vol_bank's idiom (SPEC.md 51.5.2), and its rule: the flags are preserved
; because every caller's verdict is in CF and a failed remount must not read
; as a failed preserve. A failed restore is swallowed for the same reason.
;
; QUIET (SPEC.md 19.2.2): this is standing somewhere to read or write BY NAME
; and never to show it, so the directory scan, the sort and the icon harvest
; are all bought for nothing.
; -----------------------------------------------------------------------------
rd_here_bank:
    push di
    mov di, rd_bdrv
    call rd_here_into
    pop di
    ret

rd_here_go:                     ; ...off to the image's folder
    push di
    mov di, rd_idrv
    call rd_goto_q
    pop di
    ret

rd_here_back:
    pushf
    push di
    mov di, rd_bdrv
    call rd_goto_q
    pop di
    popf
    ret

; rd_here_into - where the machine is standing (OSAPI_FILE_HERE), into the
;                (volume byte, folder word) pair at DI
; rd_goto_q    - ...and stand at the pair at DI again, quietly
;
; FOUR SUCH PAIRS, and every one is a byte followed by its word: the image's
; ([rd_idrv]), the user's across a visit to it ([rd_bdrv]), the system
; volume's ([rd_pvol]) and the user's across a page load ([rd_pvsav]) -
; which ramdisk.asm asserts, the four being defined in three files.
; Both preserve every register.
rd_here_into:
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE        ; DX = the folder, BL = its volume
    mov [di], bl
    mov [di+1], dx
    jmp short rd_goto_q.out

rd_goto_q:
    push ax
    push bx
    push dx
    mov bl, [di]
    mov dx, [di+1]
    call OSAPI_FILE_GOTO_Q
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_here_take - remember the folder the volume is standing in NOW
; in:  nothing
; out: nothing
;
; Called from the file dialog's completion, where SPEC.md 38.10 has left the
; volume at the folder the user committed in - so this is where an image
; chosen through the dialog gets its (volume, folder) from, and there is
; nothing for the page to pass.
; -----------------------------------------------------------------------------
rd_here_take:
    push di
    mov di, rd_idrv
    call rd_here_into
    pop di
    ret

; -----------------------------------------------------------------------------
; rd_preserve - write the whole volume to [rd_iname]
; in:  nothing (the caller has set the name and the folder)
; out: CF = 0 written; CF = 1 and AL = RDERR_*
; clobbers: AL, flags
; -----------------------------------------------------------------------------
rd_preserve:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [rd_vol], 0xFF
    je rd_img_emnt
    cmp byte [rd_iname], 0
    je rd_img_enoimg
    call rd_wbuf_get
    jc rd_img_emem
    call rd_meta_build
    mov es, [rd_wseg]           ; --- the metadata block, one write ----------
    xor bx, bx
    mov cx, RDI_META
    xor dx, dx
    mov si, rd_iname
    call OSAPI_FILE_WRITE
    jc rd_img_efile
    call rd_ctab_build          ; --- ...then the chain table, one append ----
    mov es, [rd_wseg]           ; ...which OPENS the image's one HELD stream
    xor bx, bx                  ; (SPEC.md 18.4.9): no lookup and no walk a
    mov cx, RDI_CTAB            ; chunk, and no FAT or directory write until
    mov word [rd_itok], 0       ; the close below. An image is useless until
    call rd_wseq                ; it is whole, so HELD's one loss is nothing
    jc rd_img_efile
    call rd_wbuf_free
    call rd_chunk_first         ; --- then the arena, chunk by chunk ---------
.chunk:
    jc .done
    call rd_blk_out             ; BX = a segment those bytes are readable in
    cmp byte [rd_xerr], 0       ; a refused copy down leaves the PREVIOUS chunk
    jne rd_img_efile                ; there, and appending it is the corruption
    mov cx, [rd_ilen]           ; BANKED: a file slot's CX is an argument and
    mov es, bx                  ; not a documented survivor
    xor bx, bx
    call rd_wseq
    jc rd_img_efile
    call rd_chunk_next
    jmp short .chunk
.done:
    xor cx, cx                  ; THE CLOSE: the link, the FAT and the size,
    call OSAPI_FILE_WRITE_SEQ   ; once. A failure is the disk's
    jc rd_img_efile
    mov byte [rd_flags], RDCF_BOOT
    call rd_cfg_mark
    clc
    jmp short rd_img_out
rd_img_emnt:                    ; (first: rd_preserve's opening test is the
    mov al, RDERR_MNT           ; farthest jump to this block)
    jmp short rd_img_fail
rd_img_enoimg:
    mov al, RDERR_NOIMG
    jmp short rd_img_fail
rd_img_emem:
    mov al, RDERR_MEM
    jmp short rd_img_fail
rd_img_ebad:                    ; rd_load's: rd_meta_check said which
    mov al, [rd_badcode]
    jmp short rd_img_fail
rd_img_efile:
    mov al, RDERR_FILE
rd_img_fail:                    ; AL = the RDERR_*. The 4KB block goes back if
    call rd_wbuf_free           ; it is held, which is a no-op when it is not
    stc                         ; ([rd_wown] is 0 outside a get/free pair) -
                                ; so every failure can come through here
rd_img_out:                     ; rd_preserve's and rd_load's ONE exit: their
    pop es                      ; frames are the same six registers
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; rd_load - read [rd_iname] into the store and mount it
; in:  nothing (the caller has set the name and the folder)
; out: CF = 0 loaded and mounted; CF = 1 and AL = RDERR_*
; clobbers: AL, flags
;
; The volume goes DOWN first, whatever happens next. A load replaces every
; byte of the store including its size, so there is no state in which half of
; the old volume and half of the new one are both reachable - which is the one
; failure this could have that nothing on screen would show.
; -----------------------------------------------------------------------------
rd_load:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [rd_iname], 0
    je rd_img_enoimg
    call rd_wbuf_get            ; the metadata first, into whatever buffer -
    jc rd_img_emem              ; note this may be the EXTENDED path's bounce,
                                ; which is why the store below is claimed
                                ; AFTER the block has been read and checked
    mov di, rd_rcur             ; a fresh cursor: the image may not be the
    call os88_rseq_new          ; file the last Load read
    mov cx, RDI_META
    xor dx, dx
    xor ax, ax
    call rd_rblk                ; DX:AX = what arrived
    jc rd_img_efile
    cmp ax, RDI_META
    jne rd_img_efile            ; a short read is not one of ours
    call rd_meta_check
    jc rd_img_ebad
    mov ax, [es:RDI_KB]
    mov [rd_want], ax           ; the size the image was made at

    call rd_wbuf_free           ; ...and only now is the store touched
    call rd_unmount
    mov ax, [rd_want]
    mov [rd_kb], ax
    call rd_store_get
    jc rd_img_emem
    call rd_wbuf_get            ; a fresh one - on the extended path this is
    jc rd_img_emem              ; the bounce the new store just claimed

    mov cx, RDI_META            ; re-read the block: rd_wbuf_free let it go,
    xor dx, dx                  ; and re-reading is cheaper than a second
    xor ax, ax                  ; buffer held across the claim above
    call rd_rblk
    jc rd_img_efile
    call rd_meta_apply          ; the directory, out of the block just read

    mov cx, RDI_CTAB            ; ...and the CHAIN TABLE, out of its own block
    xor dx, dx
    mov ax, RDI_META
    call rd_rblk
    jc .nofile     
    call rd_ctab_apply
    call rd_wbuf_free

    call rd_chunk_first         ; --- then the arena -------------------------
.chunk:
    jc .done
    call rd_blk_out             ; the DESTINATION, by the same arithmetic that
    push bx                     ; answers where a run is readable
    mov es, bx
    xor bx, bx
    mov cx, [rd_ilen]
    mov dx, [rd_pos+2]
    mov ax, [rd_pos]
    add ax, RDI_ARENA           ; the arena starts after BOTH metadata blocks
    adc dx, 0                   ; one FAT link a chunk (SPEC.md 18.4.8.1)
    call rd_rseq_at             ; CX is the capacity; DX:AX comes back as the
    pop bx                      ; count, which for a whole chunk is CX
    jc .nofile
    cmp ax, [rd_ilen]           ; ...and a SHORT one is a truncated image, the
    jne .nofile                ; same test the metadata block already makes -
                                ; without it the store keeps whatever the fresh
                                ; claim held and mounts over it
    mov ax, [rd_pos]
    mov dx, [rd_pos+2]
    mov cx, [rd_ilen]
    call rd_blk_in              ; extended: push the chunk up. Conventional it
                                ; landed in the arena already and this is a ret
    cmp byte [rd_xerr], 0       ; a refused copy either way means this chunk
    jne .nofile                ; never reached the store (SPEC.md 62.9.10.2)
    call rd_chunk_next
    jmp short .chunk
.done:
    mov byte [rd_flags], RDCF_BOOT
    call rd_cfg_mark
    call rd_mount_after
    jc .nomnt
    jmp rd_img_out              ; CF = 0, and the frame is rd_preserve's
.nomnt:
    jmp rd_img_emnt
.nofile:                        ; the chunk loop's refusals, which are too far
    jmp rd_img_efile            ; down for a short jump back to the block

; rd_wseq - one HELD chunk of the image: ES:BX, CX bytes, onto [rd_iname]
; under [rd_itok]. out: CF/AX as OSAPI_FILE_WRITE_SEQ. clobbers: AX, SI, DI
rd_wseq:
    mov si, rd_iname
    mov di, [rd_itok]
    mov al, WSEQF_HELD
    call OSAPI_FILE_WRITE_SEQ
    mov [rd_itok], di           ; (`mov`: CF stays the answer)
    ret

; -----------------------------------------------------------------------------
; rd_chunk_first / rd_chunk_next - the arena's runs, for a Preserve or a Load
; out: CF = 1 = the whole arena has moved, and DX:AX = 0, what is left of it;
;      CF = 0 = CX = [rd_ilen] = the next run's length and DX:AX = [rd_pos] =
;      where in the arena it starts
; clobbers: AX, CX, DX, flags
;
; rd_chunk_first starts the run - [rd_pos] = 0 and [rd_xerr] clear, ONE run
; being one verdict (SPEC.md 62.9.10.2) - and rd_chunk_next first advances
; [rd_pos] past the run it handed out last. A run is a whole chunk, or the
; TAIL, which is a multiple of 4,096 because the store's size is (see the
; header). [rd_ilen] is BANKED and not left in CX alone: a file slot's CX is
; an argument and not a documented survivor.
;
; THE CLOSE THAT FOLLOWS IN rd_preserve RELIES ON DX:AX = 0 HERE: it hands
; OSAPI_FILE_WRITE_SEQ the AL the last pass left, exactly as it always has.
; -----------------------------------------------------------------------------
rd_chunk_first:
    xor ax, ax
    mov [rd_xerr], al
    mov [rd_pos], ax
    mov [rd_pos+2], ax
    mov [rd_ilen], ax
rd_chunk_next:
    mov ax, [rd_ilen]
    add [rd_pos], ax
    adc word [rd_pos+2], 0
    call rd_kb_bytes            ; DX:AX = the arena...
    sub ax, [rd_pos]
    sbb dx, [rd_pos+2]          ; ...less what has moved
    mov cx, ax
    or cx, dx
    stc
    jz .out                     ; nothing left
    mov cx, RDI_CHUNK
    cmp byte [rd_loc], RDL_XMS
    jne .sz
    mov cx, RDI_XCHUNK          ; through the bounce, which is RDI_META or an
.sz:                            ; extent, whichever is larger - so 4KB a time
    or dx, dx
    jnz .full                   ; more than a chunk left, whatever the chunk is
    cmp ax, cx
    jae .full
    mov cx, ax                  ; ...the tail
.full:
    mov [rd_ilen], cx
    mov ax, [rd_pos]
    mov dx, [rd_pos+2]
    clc
.out:
    ret

; rd_kb_bytes - DX:AX = [rd_kb] * 1024, the store in bytes: 16MB is far past
; a word, and the two shifts are the whole of the arithmetic.
; clobbers: AX, DX (the output)
rd_kb_bytes:
    push cx
    mov ax, [rd_kb]
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    pop cx
    ret

; -----------------------------------------------------------------------------
; rd_meta_build - the 4,096-byte block, in [rd_wseg]
; in:  nothing
; out: nothing
;
; ES is ours to spend: every caller has banked it.
; -----------------------------------------------------------------------------
rd_meta_build:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [rd_wseg]
    xor di, di                  ; a zeroed block, so every reserved byte and
    mov cx, RDI_META / 2        ; every unused record is a known value
    xor ax, ax
    cld
    rep stosw

    mov word [es:RDI_MAG], RDI_MAGIC0
    mov word [es:RDI_MAG+2], RDI_MAGIC1
    mov word [es:RDI_VER], RDI_VER_V
    mov ax, [rd_extb]           ; the extent size it was written with - DERIVED
    mov [es:RDI_EXTB], ax       ; from the size now, so it is recorded and
    mov ax, [rd_kb]             ; adopted rather than compared (SPEC.md 62.9.10)
    mov [es:RDI_KB], ax
    call rd_kb_bytes            ; the arena's byte count, in 32 bits
    mov [es:RDI_BYTES], ax
    mov [es:RDI_BYTES+2], dx

    xor dx, dx                  ; DX = live rows written
    mov di, RDI_DIR
    push ds
    mov ds, [rd_dtab]           ; the directory is the SOURCE of the moves
    xor si, si                  ; below, so it is DS for the length of the
    xor bx, bx                  ; walk (SPEC.md 62.9.13.1). BL = the row index
.row:
    cmp byte [si+RDE_TYPE], RD_FREE
    je .next
    push si                     ; A RECORD IS THE ROW WITH TWO BYTES ADDED -
    mov [es:di], bl             ; the index in front and a pad after the type
    inc di                      ; (rdabi.inc) - so it is two string moves
    movsw                       ; around them: PAR and TYPE...
    inc di                      ; ...RDI_E_PAD, which the zeroed block holds...
    mov cx, (RDI_ESZ - RDI_E_EXT) / 2
    rep movsw                   ; ...and EXT, SIZE and the name's first 14
    pop si
    inc dx
.next:
    add si, RDE_SZ
    inc bx
    cmp bl, RD_MAXENT
    jb .row
%if RDE_PAR != 0 || RDE_TYPE != 1 || RDI_E_PAR != 1 || RDI_E_TYPE != 2 || RDI_E_EXT != 4 || RDE_EXT != 2 || RDI_E_SIZE - RDI_E_EXT != RDE_SIZE - RDE_EXT || RDI_E_NAME - RDI_E_EXT != RDE_NAME - RDE_EXT || RDI_ESZ - RDI_E_NAME > RDE_SZ - RDE_NAME || (RDI_ESZ - RDI_E_EXT) & 1
  %error "ramdisk: rd_meta_build's two moves assume the record is the row plus IDX and PAD"
%endif
.rows:
    pop ds
    mov [es:RDI_NENT], dx
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_ctab_build - the CHAIN TABLE's own block, in [rd_wseg]
; in:  nothing
; out: nothing
;
; A BLOCK OF ITS OWN because the chain is a word per extent now: 2,048 entries
; is 4,096 bytes and there is no room for it beside the directory. All of it is
; written, live entries or not, so the block's length is a constant and the
; arena's offset in the file does not depend on the store's size.
; -----------------------------------------------------------------------------
rd_ctab_build:
    push ax
    push bx
    push cx
    push di
    push es
    mov es, [rd_wseg]
    xor di, di
    mov cx, RDI_CTAB / 2
    mov ax, RD_CFREE
    cld
    rep stosw                   ; an extent this store does not have reads as
                                ; FREE, which is what a shorter table would
                                ; have meant anyway
    xor bx, bx
    mov cx, [rd_next]
    jcxz .out
.one:
    call rd_cget
    mov di, bx
    shl di, 1
    mov [es:di], ax
    inc bx
    loop .one
.out:
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; rd_rblk    - CX bytes of the image at DX:AX into the 4KB block, [rd_wseg]:0
; rd_rseq_at - ...or into ES:BX
; out: os88_rseq's - CF, DX:AX = what arrived; ES = the buffer
; clobbers: SI, DI (and BX, ES for rd_rblk)
rd_rblk:
    mov es, [rd_wseg]
    xor bx, bx
rd_rseq_at:
    mov si, rd_iname
    mov di, rd_rcur
    jmp os88_rseq

; -----------------------------------------------------------------------------
; rd_mount_after - put the volume on the desktop over a store that is ALREADY
;                  filled (rd_load's tail, and rd_mount's)
; out: CF = 0; CF = 1 and AL = RDERR_VOL (no volume row) or RDERR_MNT (the
;      kernel refused), with the row given back
;
; rd_mount's second half, without the claim and without the directory clear -
; which is the whole difference, the directory having just been restored.
; -----------------------------------------------------------------------------
rd_mount_after:
    push cx
    push dx
    push si
    xor al, al                  ; our own volume handle: there is one RAM disk
    xor cx, cx                  ; no sector count - there are no sectors
    xor dx, dx                  ; no donated listing claim: the .lowbss floor
    mov si, rd_label
    call OSAPI_VOL_ADD
    jc .norow
    mov [rd_vol], al
    call OSAPI_VOL_MOUNT        ; AL survives: it is the volume index
    jnc .out
    mov al, [rd_vol]
    call OSAPI_VOL_DEL
    mov byte [rd_vol], 0xFF
    mov al, RDERR_MNT
    jmp short .fail
.norow:
    mov al, RDERR_VOL
.fail:
    stc
.out:
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; rd_meta_check - is the block in ES one of ours, and can this build take it?
; out: CF = 0 yes; CF = 1 and [rd_badcode] = RDERR_FMT / RDERR_BIG
; clobbers: flags
;
; EVERY BYTE READ OFF A DISK IS HOSTILE (SPEC.md 18.2's rule, applied to a
; format of our own): the size decides how much store is claimed and how far
; the restore walks, so it is checked against the ceiling AND against the step
; the chunk arithmetic assumes.
; -----------------------------------------------------------------------------
rd_meta_check:
    push ax
    mov byte [rd_badcode], RDERR_FMT
    cmp word [es:RDI_MAG], RDI_MAGIC0
    jne .no
    cmp word [es:RDI_MAG+2], RDI_MAGIC1
    jne .no
    cmp word [es:RDI_VER], RDI_VER_V
    jne .no
    cmp word [es:RDI_NENT], RD_MAXENT
    ja .no
    mov ax, [es:RDI_KB]
    test ax, RD_STEPKB - 1
    jnz .no                     ; not a whole step: the chunk arithmetic in
                                ; this file assumes one, and an image that
                                ; does not have it was not written by us
    cmp ax, RD_MINKB
    jb .no
    mov byte [rd_badcode], RDERR_BIG
    cmp ax, RD_MAXKB            ; past what this build can address at all...
    ja .no
    push bx                     ; ...or past what the machine will fund, which
    call rd_kb_max              ; is the answer that actually stops a 4MB image
    cmp ax, bx                  ; being read into a 256KB machine
    pop bx
    ja .no
    push ax                     ; and the extent size it was written at has to
    mov ax, [es:RDI_EXTB]       ; be one this build derives, or its chain table
    cmp ax, RD_EXTMINKB * 1024  ; means something else. It is not compared
    jb .noext                   ; against OUR current one: the store is rebuilt
    cmp ax, RD_EXTMAXKB * 1024  ; at the image's size, so the size decides
    ja .noext
    pop ax
    pop ax
    clc
    ret
.noext:
    pop ax
    mov byte [rd_badcode], RDERR_FMT
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; rd_meta_apply - the block in ES -> the directory
; in:  ES = the block, and the store is already claimed at its size - which is
;      what claimed the directory too (SPEC.md 62.9.13.1)
; out: nothing; ES = the directory
;
; THE ROW INDEX COMES OUT OF THE RECORD, which is what makes a handle survive
; a round trip: a handle is `row index + 1` and the parent column is a handle,
; so a restore that repacked rows would renumber every parent link on the
; volume (SPEC.md 62.9.12).
;
; Everything it reads is bounds-checked against this build's own limits, an
; out-of-range record being dropped rather than believed.
; -----------------------------------------------------------------------------
rd_meta_apply:
    push ax
    push cx
    push si
    push di
    push ds
    push es                     ; the block...
    call rd_dir_clear           ; ...ES = the directory...
    pop ds                      ; ...and DS = the block, so the block is read
    mov cx, [RDI_NENT]          ; plainly and the rows through ES - and the
    jcxz .done                  ; name is one rd_scopy, DS:SI -> ES:DI
    mov si, RDI_DIR
.rec:
    mov al, [si+RDI_E_IDX]
    cmp al, RD_MAXENT
    jae .next                   ; a row this build does not have
    mov ah, RDE_SZ              ; ...and the row's offset, by a byte multiply
    mul ah
    xchg ax, di
    mov al, [si+RDI_E_TYPE]
    cmp al, 2
    ja .next                    ; not a type we serve
    mov [es:di+RDE_TYPE], al
    mov al, [si+RDI_E_PAR]
    cmp al, RD_MAXENT
    ja .drop
    mov [es:di+RDE_PAR], al
    mov ax, [si+RDI_E_EXT]
    cmp ax, RD_NOEXT
    je .ext
    cmp ax, [cs:rd_next]        ; an extent this store does not have: the
    jb .ext                     ; record came off a DISK and is not to be
                                ; believed (CS: DS is the block here)
.drop:
    mov byte [es:di+RDE_TYPE], RD_FREE
    jmp short .next
.ext:
    mov [es:di+RDE_EXT], ax
    mov ax, [si+RDI_E_SIZE]
    mov [es:di+RDE_SIZE], ax
    mov ax, [si+RDI_E_SIZE+2]
    mov [es:di+RDE_SIZE+2], ax
    push cx
    push si
    add si, RDI_E_NAME
    add di, RDE_NAME
    mov cx, 13                  ; ...whatever the image said, ours is
    call rd_scopy               ; NUL-ended: 13 characters at most, then a NUL
    pop si
    pop cx
.next:
    add si, RDI_ESZ
    loop .rec
.done:                          ; ([rd_cwd] = 0, the root: rd_dir_clear)
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_ctab_apply - the chain block in ES -> the live table
; in:  ES = the block, and the store is already claimed at its size
; out: nothing
;
; Only the entries this store HAS are taken; the rest of the block belonged to
; a bigger one and is not ours to believe. rd_cget refuses an out-of-range
; index by answering RD_CEND, so a cell naming an extent past the end ends its
; chain rather than walking off the store - which is the second half of the
; same rule (SPEC.md 62.9.12).
; -----------------------------------------------------------------------------
rd_ctab_apply:
    push ax
    push bx
    push cx
    push si
    xor bx, bx
    mov cx, [rd_next]
    jcxz .out
.one:
    mov si, bx
    shl si, 1
    mov ax, [es:si]
    cmp ax, RD_CEND
    je .put
    cmp ax, [rd_next]           ; a cell is `next + 1`, so the largest legal
    jbe .put                    ; one is the extent count itself
    mov ax, RD_CEND
.put:
    call rd_cput
    inc bx
    loop .one
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; rd_boot_restore - DRVV_READY's one reason to touch a disk (SPEC.md 62.9.12)
; in:  nothing
; out: nothing - a failure is QUIET
;
; The disk the image was on may simply not be in the drive, and a machine that
; boots to a notice because a RAM disk could not be restored is worse than one
; that boots to no RAM disk at all. The page says so when it is next opened.
; -----------------------------------------------------------------------------
rd_boot_restore:
    push ax
    test byte [rd_flags], RDCF_BOOT
    jz .out
    cmp byte [rd_iname], 0
    je .out
    call rd_here_bank
    call rd_here_go
    call rd_load
    jnc .back
    mov [rd_err], al            ; ...and the page reports it when it opens
.back:
    call rd_here_back
.out:
    pop ax
    ret
