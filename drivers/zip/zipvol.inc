; =============================================================================
; zipvol.inc - ZIP.DRV's volume: DSV_BLK (SPEC.md 51.8, 138.4)
;
; The kernel hands a VOLUME-RELATIVE 32-bit LBA (DI:SI, SPEC.md 18.7.5) and a
; buffer in DX:BX; this file adds the partition's base, holds the transfer to
; the partition's length, and sends it as READ(10)/WRITE(10) commands of at
; most ZV_RUN sectors.
;
; THE MEDIUM IS LEARNED LAZILY (SPEC.md 138.4), on the first READ after
; attach or after the disk changed - TEST UNIT READY, READ CAPACITY and MODE
; SENSE (zs_query), then sector 0 READ INTO THE CALLER'S OWN BUFFER, which a
; read is about to overwrite anyway: so the image carries no 512-byte sector
; buffer of its own. A WRITE that arrives first is answered 06h, and the
; kernel's remount that follows is a read.
; =============================================================================

ZV_RUN      equ 32              ; sectors a command: 16KB, so a normalised
                                ; buffer (offset under 128) never wraps

; -----------------------------------------------------------------------------
; zv_blk - DSV_BLK
; in:  AL = 0 read / 1 write / 2 DSV_GEOM, AH = our handle (0), DI:SI = the
;      volume-relative LBA, CX = sectors, DX:BX = the buffer
; out: CF = 0 done; CF = 1 and AL = an int 13h status (SPEC.md 138.3)
; clobbers: AX (the output), flags
;
; DSV_GEOM (AL = 2) is CF = 1: no ROM reaches this disk (SPEC.md 51.8), so
; the resume stub has nothing to be told.
; -----------------------------------------------------------------------------
zv_blk:
    cmp al, 2
    jae .geom
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [zv_op], al
    mov [zv_lba], si            ; the LBA first: DI is the transport's next
    mov [zv_lba+2], di
    mov [zv_bofs], bx
    mov [zv_bseg], dx
    or ah, ah
    mov al, 0x04                ; "sector not found": a handle we never gave
    jnz .fail
    mov al, [zp_med]
    cmp al, ZM_KNOWN
    je .known
    cmp al, ZM_CHANGED
    mov al, 0x06                ; THE DISK CHANGED under a mounted volume:
    je .chg                     ; say so once, and learn at the remount
    cmp [zv_op], ah             ; (AH = 0) a WRITE with the medium unknown:
    jne .fail                   ; 06h, so the kernel remounts - a read - first
    jcxz .ok
    call zv_learn
    jc .fail
.known:
    cmp byte [zv_op], 0         ; a write to a write-protected disk goes no
    je .fence                   ; further than this
    cmp byte [zp_st], ZIPS_WP
    mov al, 0x03
    je .fail
.fence:                         ; THE PARTITION'S LENGTH FENCES EVERY TRANSFER
    mov ax, [zv_lba]            ; (SPEC.md 138.4, HDD.DRV's HDV_LEN): the
    mov dx, [zv_lba+2]          ; kernel was told "unknown" and bounds nothing
    add ax, cx                  ; past 32MB, so a BPB that overstates its
    adc dx, 0                   ; size would reach past the partition
    jc .f04
    cmp dx, [zp_len+2]
    ja .f04
    jb .in
    cmp ax, [zp_len]
    jbe .in
.f04:
    mov al, 0x04
    jmp short .fail
.in:
    mov ax, [zp_base]           ; volume-relative -> the disk's own LBA
    add [zv_lba], ax
    mov ax, [zp_base+2]
    adc [zv_lba+2], ax
.run:
    jcxz .ok
    call zv_bnorm
    mov si, cx                  ; SI = this command's sectors
    cmp si, ZV_RUN
    jbe .n
    mov si, ZV_RUN
.n:
    call zv_rw
    jc .err
    sub cx, si
    add [zv_lba], si
    adc word [zv_lba+2], 0
    mov ax, si
    xchg al, ah                 ; x 256...
    shl ax, 1                   ; ...x 2: the bytes moved (ZV_RUN x 512 fits)
    add [zv_bofs], ax
    jmp short .run
.err:
    cmp al, 0x06                ; UNIT ATTENTION in the middle of the data:
    jne .fail                   ; reported now, so the medium is relearned
.chg:                           ; at the remount and not changed twice
    mov byte [zp_med], ZM_UNKNOWN
.fail:
    stc
    jmp short .out
.ok:
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.geom:
    stc
    ret

; -----------------------------------------------------------------------------
; zv_rw - one READ(10) / WRITE(10) at [zv_lba] of SI sectors through the
;         buffer [zv_bseg]:[zv_bofs] (normalised)
; in:  SI = sectors, 1..ZV_RUN; [zv_op]
; out: CF = 0 all of them moved; CF = 1 and AL = a status
; clobbers: AX, flags
; -----------------------------------------------------------------------------
zv_rw:
    push bx
    push cx
    push dx
    push di
    push es
    mov al, SC_READ10
    mov bh, [zv_op]             ; ZTD_IN = 0 = read, ZTD_OUT = 1 = write
    or bh, bh
    jz .op
    mov al, SC_WRITE10
.op:
    xor ah, ah
    mov [zs_cdb], ax            ; the opcode, and byte 1 = 0
    mov ax, [zv_lba+2]          ; the LBA, big-endian
    xchg al, ah
    mov [zs_cdb+2], ax
    mov ax, [zv_lba]
    xchg al, ah
    mov [zs_cdb+4], ax
    mov ax, si                  ; the count, big-endian at +7: under 256,
    xchg al, ah                 ; so +7 = 0 (AL) and +8 = the count (AH)
    mov [zs_cdb+7], ax
    xor al, al
    mov [zs_cdb+6], al
    mov [zs_cdb+9], al
    mov bl, 10
    mov cx, si
    xchg cl, ch
    shl cx, 1                   ; the bytes
    mov dx, cx
    les di, [zv_bofs]
    call zs_cmd
    jc .out
    cmp cx, dx                  ; ALL of it, or the drive stopped short of
    je .out                     ; what it was asked (CF = 0 off the equal)
    mov al, 0x20
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zv_bnorm - [zv_bseg]:[zv_bofs] with the offset under 128. clobbers: nothing
;
; cd_bnorm's rule (drivers/cdrom/cdiso.inc), for its reason: in WHOLE
; 128-byte granules, so the segment moves in steps of 8 paragraphs - under
; kern_mega's protected mode (CPU_F_PM, asked HERE, never banked: a boot
; driver attaches before mega_enter) only a segment on that grid is an
; identity selector for what it names, and an LDT selector is no paragraph at
; all, so its offset walks unfolded - the kernel hands one over with the whole
; transfer inside its 64KB (BLKCAP_SEL, SPEC.md 51.8.1).
; -----------------------------------------------------------------------------
zv_bnorm:
    push ax
    call OSAPI_CPU_INFO         ; AH = the flags NOW
    test ah, CPU_F_PM
    jz .fold
    test byte [zv_bseg], 4      ; an LDT selector: leave it
    jnz .out
.fold:
    push cx
    mov ax, [zv_bofs]
    mov cl, 4
    shr ax, cl
    and al, 0xF8                ; whole granules only
    add [zv_bseg], ax
    and word [zv_bofs], 127
    pop cx
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zv_learn - the medium, into zp_base/zp_len (SPEC.md 138.4)
; in:  [zv_bseg]:[zv_bofs] = a buffer of at least one sector - the caller's
; out: CF = 0 [zp_med] = ZM_KNOWN; CF = 1 and AL = a status (AAh: no disk, or
;      none os8088 reads)
; clobbers: AX, flags
;
;   1. a FAT boot sector - a jump (EBh or E9h), 512 bytes a sector, one or
;      two FATs - is a SUPERFLOPPY: the volume is the whole disk;
;   2. else an MBR (55AAh): the FIRST entry, 1..4 in order, of type 01h, 04h,
;      06h or 0Eh with a length. Iomega's own format puts its FAT16 in
;      ENTRY 4, which is why the scan cannot stop at entry 1;
;   3. else AAh.
; The FAT-count test is the one this adds to the contract's two: an MBR whose
; code happens to open with a jump has random bytes where a BPB would be.
; -----------------------------------------------------------------------------
zv_learn:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call zs_query
    jc .out
    call zv_bnorm
    push word [zv_lba+2]        ; the caller's LBA, banked: zv_rw reads
    push word [zv_lba]          ; [zv_lba] and sector 0 is wanted
    xor ax, ax
    mov [zv_lba], ax
    mov [zv_lba+2], ax
    mov si, 1
    call zv_rw
    pop word [zv_lba]           ; (`pop` writes no flag)
    pop word [zv_lba+2]
    jc .out
    les di, [zv_bofs]
    xor ax, ax                  ; a superfloppy's base...
    mov bx, ax
    mov cx, [zp_cap]            ; ...and length, the whole disk
    mov dx, [zp_cap+2]
    mov al, [es:di]
    cmp al, 0xEB
    je .bpb
    cmp al, 0xE9
    jne .mbr
.bpb:
    cmp word [es:di+0x0B], 512
    jne .mbr
    mov al, [es:di+0x10]        ; the FAT count, 1 or 2
    dec ax
    cmp al, 1
    jbe .take                   ; (AX: AH = 0 from the xor, AL 0 or 1 -
.mbr:                           ; .take zeroes it again as the base)
    cmp word [es:di+0x1FE], 0xAA55
    jne .bad
    lea si, [di+0x1BE]
    mov ah, 4
.ent:
    mov al, [es:si+4]           ; the type
    cmp al, 0x01
    je .typ
    cmp al, 0x04
    je .typ
    cmp al, 0x06
    je .typ
    cmp al, 0x0E
    jne .next
.typ:
    mov cx, [es:si+12]          ; its length...
    mov dx, [es:si+14]
    mov bx, cx
    or bx, dx
    jz .next                    ; ...which must be something
    mov ax, [es:si+8]           ; and its first sector
    mov bx, [es:si+10]
    jmp short .take2
.next:
    add si, 16
    dec ah
    jnz .ent
.bad:
    mov al, 0xAA                ; not a disk os8088 can read
    stc
    jmp short .out
.take:
    xor ax, ax
.take2:
    mov [zp_base], ax
    mov [zp_base+2], bx
    mov [zp_len], cx
    mov [zp_len+2], dx
    mov byte [zp_med], ZM_KNOWN
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
