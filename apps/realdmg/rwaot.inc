; =============================================================================
; os8088 - apps/realdmg/rwaot.inc
;
; A PRECOMPILED CARTRIDGE's load (docs/REALDMG-SPEC.md section 20.10): the
; RDG_AOT build of realdmg.asm, one game's package, which tools/gbaot.py's
; `package` writes. Included by realdmg.asm after rdgaot.inc.
;
; THE PARTS (realdmg.asm's table): 0 and 1 the core with the host's code in
; its holes, 2 the view, 3 the block table (16KB or 32KB, the tokens F000h +
; n in its segment words), 4 the meta, from 5 the relocations' and the
; list's parts (eager, in the carve), then each code image's two 32KB
; halves, which are OP_SELF: rw_aread reads them into a claim of 64KB each,
; through a bounce (rw_apart).
;
; THE META, part 4:
;   0   'RDAOT2'
;   6   db the code images past the core, 7 db the table's mask (7 for
;       16KB, 15 for 32KB: kern_mega's packages)
;   8   dw the list's entries, 10 dw the relocations
;   12  db the relocations' first row, 0 for none, 13 db how many rows:
;       each its own eager part, 4 bytes an entry - db the image the far
;       jump is in (0 the core), db the image it jumps to | 80h when the
;       site is a switchable bank's (`cmp byte [ss:S_BANK],B / jne +5`
;       first, the jump 8 bytes on), dw the site: the `mov dx,SITE` the
;       host linked over
;   14  the cartridge's header, 0134h-014Fh (28 bytes)
;   42  the ROM's file name, NUL-terminated (13 bytes)
;   55  db kern_mega's package: the recompiler's part's row, the last, or 0
;       (REALDMG-SPEC 20.16: the misses translated)
;   56  db the list's first row, 57 db how many rows (1 to RDG_LXMAX): each
;       its own eager part, sorted, the parts in key order, so the search
;       picks a part by its first entry and bisects only that one
;       (REALDMG-SPEC 20.17.1). The filter is after the table, in its part
;   58  db the first row of the ROM's pieces, 0 for none (REALDMG-SPEC
;       20.13): 32KB each, plain, so they lie back to back in the file
;   59  db the HOT images - the first so many hold what the counters ran
;       (REALDMG-SPEC 20.11), and they are claimed before the ROM, the rest
;       after it; 0 = all of them
;   60  dw the ROM's KB
;
; AN IMAGE THE HEAP CANNOT HOLD IS LEFT ON THE DISK, and what it held is
; interpreted: its table and list entries get offset 0 (rdgaot.inc's miss
; falls to the interpreter at one), and every far jump into it becomes the
; exit it was before the host linked it - `mov dx,SITE / jmp tr_lsite` -
; which tr_link, finding no block, leaves to the interpreter at run time.
; The images are claimed in the order the host wrote them, so what goes is
; the last.
;
; THE CARVE IS PINNED (rwmove.inc's rw_mdecl): the images name the core's
; segment in their far jumps. Every image is a claim that is never declared,
; so it stays put too.
; =============================================================================

RWA_TOKEN       equ 0F000h          ; a segment word the loader has to give

; rw_ameta - at the entry, after op_load: the meta checked against the table
; it came with, and every segment the run needs banked. CF = 1: it is not one
; of ours, and that is said
rw_ameta:
    xor al, al
    call op_seg
    mov [rw_acore], ax
    mov [rw_asega], ax
    mov al, RWA_TABLE
    call op_seg
    mov [rw_atseg], ax
    mov al, RWA_META
    call op_seg
    mov [rw_amseg], ax
    or ax, ax
    jz .bad
    push es
    mov es, ax
    cmp word [es:0], 'RD'
    jne .bad1
    cmp word [es:2], 'AO'
    jne .bad1
    cmp word [es:4], 'T2'           ; (RDAOT2: the relocations and the
    jne .bad1                       ;   list in parts of their own, 20.17)
    mov al, [es:6]
    cmp al, RWA_MAX
    ja .bad1
    mov [rw_ans], al
    mov dl, [es:59]                 ; (the hot ones: all of them, unsaid)
    mov dh, [rw_ans]
    or dl, dl
    jz .hall
    cmp dl, dh
    jbe .hset
.hall:
    mov dl, dh
.hset:
    mov [rw_ahot], dl
    mov ax, RWA_CODE                ; the table the package was made with:
    mov dl, [es:12]                 ;   the relocations' rows and the list's
    mov dh, [es:13]                 ;   first, each where the last left off
    mov [rw_arr], dl                ;   (20.17: the carve's, which come
                                    ;   before every lazy row)...
    mov [rw_arn], dh
    or dh, dh
    jz .nrel
    cmp dl, al
    jne .bad1
    add al, dh
.nrel:
    mov dl, [es:56]
    mov dh, [es:57]
    cmp dh, RDG_LXMAX
    ja .bad1
    mov [rw_alrow], dl
    mov [rw_alxn], dh
    or dh, dh
    jz .nlst
    cmp dl, al
    jne .bad1
    add al, dh
.nlst:
    mov [rw_acode], al              ; ...two rows an image...
    mov dl, [rw_ans]
    xor dh, dh
    shl dx, 1
    add ax, dx
    mov dx, [es:60]                 ; ...and a row a 32KB of the ROM in it
    mov [rw_aromkb], dx
    cmp byte [es:58], 0
    je .nor
    cmp [es:58], al                 ; (where they begin: past the images)
    jne .bad1
    mov cl, 5
    shr dx, cl
    add ax, dx
.nor:
%ifdef PKG_MEGA
    mov dl, [es:55]                 ; (20.16: the recompiler's part, the
    or dl, dl                       ;   last row)
    jz .noj
    cmp dl, al
    jne .bad1
    inc ax
.noj:
    mov [rw_ajrow], dl
%endif
    cmp al, [op_t_n]
    jne .bad1
    mov al, [es:7]
    mov [rw_ahm], al
    xor di, di                      ; the list's parts: each its own carve
    mov dl, [rw_alrow]              ;   part (a segment, or a selector under
    mov cl, [rw_alxn]               ;   CPU_F_PM), its entries its length / 8
    xor ch, ch
    jcxz .lxz
.lx:
    mov al, dl
    call op_seg
    or ax, ax
    jz .bad1
    mov [rw_alx + di], ax
    mov al, dl
    push si
    call op_row
    mov ax, [si + OP_R_LEN]
    pop si
    shr ax, 1
    shr ax, 1
    shr ax, 1
    mov [rw_alx + di + 2], ax
    inc dl
    add di, 4
    loop .lx
.lxz:
    mov ax, [rw_alx]                ; (part 0, or none: 0 entries)
    mov [rw_alseg], ax
    mov ax, [rw_alx + 2]
    mov [rw_aln], ax
    mov al, [rw_ahm]                ; the filter and the list's cache:
    xor ah, ah                      ;   after the table in ITS part, (mask
    inc ax                          ;   + 1) x 256 x 8 bytes on - where the
    mov cl, 7                       ;   meta part had them, a long list took
    shl ax, cl                      ;   it past 65,024 bytes (Link's
%ifdef PKG_MEGA                     ;   Awakening: 65,264)
    mov dx, [rw_atseg]              ; (a selector under CPU_F_PM: the sum
    call rw_sub                     ;   is OSAPI_SEG's, as the list's is)
    jc .bad1
    mov [rw_afseg], dx
%else
    add ax, [rw_atseg]
    mov [rw_afseg], ax
%endif
    pop es
    clc
    ret
.bad1:
    pop es
.bad:
    mov si, rw_s_ameta
    call rw_toast
    stc
    ret

; rw_aname - launched with no document: the ROM the package was made from,
; in the package's own folder - KIRBY.O88 beside KIRBY.GB is the whole of
; how it is used
rw_aname:
    push es
    mov es, [rw_amseg]
    cmp byte [es:58], 0             ; THE ROM IN THE PACKAGE: the file is
    je .side                        ;   our own, at the first piece's offset
    mov bl, [es:58]
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov ax, [op_table + OP_T_ROWS + bx + OP_R_OFF]
    mov dx, ax
    mov cl, 9
    shl ax, cl
    mov cl, 7
    shr dx, cl
    mov [rw_arombase], ax
    mov [rw_arombase + 2], dx
    mov byte [rw_aemb], 1
    push ds
    pop es
    mov si, op_name
    jmp short .cp
.side:
    cmp byte [es:42], 0
    je .r
    mov si, 42
.cp:
    mov di, rw_name
    mov cx, 13
.c:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .c
    mov byte [rw_name + 13], 0
    mov dx, [rw_pkgdir]
    mov [rw_argdir], dx
    mov al, [rw_pkgvol]
    mov [rw_argvol], al
    mov byte [rw_argpend], 1
.r:
    pop es
    ret

; rw_anoname - the ROM could not be read: SI = 'REALDMG: no NAME.GB', the
; name it looked for, in rw_stmsg - most often the file is not beside the
; package under that name, and "read failed" did not say which
rw_anoname:
    push ax
    push cx
    push di
    push ds
    pop es
    mov si, rw_s_ano
    mov di, rw_stmsg
.c:
    lodsb
    or al, al
    jz .n
    stosb
    jmp short .c
.n:
    mov si, rw_name
    mov cx, 12
.m:
    lodsb
    stosb
    or al, al
    jz .x
    loop .m
    mov byte [di], 0
.x:
    mov si, rw_stmsg
    pop di
    pop cx
    pop ax
    ret

; rw_aromrd - the ROM in the package into [rw_romseg]'s claim, a bank at a
; time (rw_load's one claim). CF = 1: it could not be; else DX:AX = its bytes
rw_aromrd:
    push bx
    push cx
    push si
    push di
    push bp
    push es
    xor bx, bx                      ; BX = the bank
    mov bp, [rw_romseg]
.b:
%ifdef PKG_MEGA
    mov ax, bx                      ; (BP = the claim + bank x 400h: a
    mov cl, 10                      ;   window under CPU_F_PM, 20.15)
    shl ax, cl
    mov dx, [rw_romseg]
    call rw_sub
    jc .x
    mov bp, dx
%endif
    mov ax, bx
    push bx
    call rw_aebank1                 ; bank BX to BP:0
    pop bx
    jc .x
%ifndef PKG_MEGA
    add bp, 400h
%endif
    inc bx
    mov ax, [rw_aromkb]
    mov cl, 4
    shr ax, cl
    cmp bx, ax
    jb .b
    mov ax, [rw_aromkb]             ; DX:AX = KB x 1024
    mov cx, 1024
    mul cx
    clc
.x:
    pop es
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    ret

; rw_aebread - rw_bread's (rwbank.inc) for the ROM in the package: its next
; bank into DX:0000, the banks in order from 0. CF = 1 unless it all came
rw_aebread:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov bp, dx
    mov ax, [rw_aebank]
    inc word [rw_aebank]
    call rw_aebank1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_aebank1 - AX = a bank: its 16KB out of the package to BP:0. CF = 1: it
; could not be. Clobbers AX-DI and ES
rw_aebank1:
    mov cx, 16384
    mul cx                          ; DX:AX = its offset in the ROM
    add ax, [rw_arombase]
    adc dx, [rw_arombase + 2]
    mov word [rw_awant], 16384
    mov word [rw_adst], 0
    jmp rw_acopy

; rw_ahdr - ES = the cartridge's bank 0 (rw_hdrchk's): ZF = 1 when its
; header is the one the package was made from, else SI = why. Every other
; register kept
rw_ahdr:
    push cx
    push di
    push ds
    mov ds, [rw_amseg]
    mov si, 14
    mov di, 0134h
    mov cx, 014Fh + 1 - 0134h
    cld
    repe cmpsb
    pop ds
    mov si, rw_s_other              ; (a mov: ZF stays the compare's)
    pop di
    pop cx
    ret

; rw_aclaim - AL = images: a 64KB claim for each code image below that not
; claimed yet, while the heap has one - all of them before the ROM's plan,
; any the plan gave back after it. Once they are read (rw_aread) they are
; the session's and this does nothing. FROM THE CEILING: an image is a CS
; that never moves, which is what the high door is for (SPEC.md 50.3.2) -
; claimed from the bottom, Looney Tunes' first image landed mid-heap and the
; bank cache's one claim then had no 152KB run beside two more of them
rw_aclaim:
    cmp byte [rw_afix], 0
    jne .r
    mov ah, al
%ifdef PKG_MEGA
    call rw_axclaim                 ; (CPU_F_PM: one pool block first, 20.15)
%endif
.l:
    mov al, [rw_anseg]
    cmp al, ah
    jae .r
    push ax
    mov ax, 64
    call OSAPI_MEM_CLAIM_HI         ; DX = its segment
    pop ax
    jc .r
    mov bl, [rw_anseg]
    xor bh, bh
    shl bx, 1
    mov [rw_aseg + bx], dx
    inc byte [rw_anseg]
    jmp short .l
.r:
    ret

%ifdef PKG_MEGA
; rw_axclaim - rw_aclaim's first, kern_mega's arm under CPU_F_PM (REALDMG-
; SPEC 20.15): AH = images wanted. As many of them as the extended pool holds
; - all of them, on most protected-mode machines - in ONE block
; (OSAPI_XMEM_ALLOC: the claim records stay free, a package holds eight),
; image n a SEG_SUBC pair at (n - 1) x 64KB through one OSAPI_XMEM_SEL
; selector over the block's start, its data half in rw_aseg (rw_csel answers
; the code half). The heap's claims take any it leaves, as before. Once:
; [rw_axbase] says it is made. Keeps AX
rw_axclaim:
    cmp byte [rw_pm], 0
    je .r
    cmp word [rw_axbase + 2], 0
    jne .r
    cmp word [rw_axbase], 0
    jne .r
    cmp byte [rw_anseg], 0
    jne .r
    push ax
    push bx
    push cx
    push dx
    mov cl, ah
    xor ch, ch                      ; CX = images, from the most down
    jcxz .x
.try:
    mov dx, cx                      ; DX:AX = CX x 64KB
    xor ax, ax
    call OSAPI_XMEM_ALLOC
    jnc .got
    loop .try
    jmp short .x
.got:
    mov [rw_axbase], ax
    mov [rw_axbase + 2], dx
    push cx
    xor cx, cx
    call OSAPI_XMEM_SEL             ; AX = its first 64KB's selector
    pop cx
    jc .nosel
    mov dx, ax
    xor ax, ax                      ; AX = paragraphs into the block
    xor bx, bx
.p:
    push dx
    call rw_subc                    ; DX = image BX+1's data half
    jc .pe
    mov [rw_aseg + bx], dx
    inc byte [rw_anseg]
    inc byte [rw_axn]
    pop dx
    add ah, 10h                     ; (past 1MB of the block the paragraphs
    jnz .pn                         ;   wrap: on from this image's own
    mov dx, [rw_aseg + bx]          ;   selector, its base + 64KB - image
    mov ah, 10h                     ;   17 was image 1's bytes again)
.pn:
    inc bx
    inc bx
    loop .p
    jmp short .x
.pe:
    pop dx                          ; (no LDT entry: the rest stay out)
    jmp short .x
.nosel:
    mov ax, [rw_axbase]
    call OSAPI_XMEM_FREE
    xor ax, ax
    mov [rw_axbase], ax
    mov [rw_axbase + 2], ax
.x:
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

rw_axbase       dw 0, 0             ; the images' pool block, or 0
rw_axn          db 0                ; ...the images in it, the first ones
%endif

; rw_aloose - the load's hold let go (rw_load pins every claim of ours):
; from the images' claims to the slots' (rw_bload) nothing holds one of our
; segments in a register, and the plan's OSAPI_MEM_AVAIL and the claim after
; it must see our small claims as the movable ones they are. Held, the frame
; buffers and the damage memory stood mid-heap where the window's caches had
; put them, and Looney Tunes' 8 slots found no 152KB run beside 3 images
; that would have fitted once compacted. The carve stays pinned (rw_mdecl).
; rw_atight takes the hold again; both keep every register and flag
rw_aloose:
    pushf
    cmp byte [rw_aunh], 0
    jne .r
    mov byte [rw_aunh], 1
    dec byte [rw_mhold]
    call rw_mfree
.r:
    popf
    ret

rw_atight:
    pushf
    cmp byte [rw_aunh], 0
    je .r
    mov byte [rw_aunh], 0
    inc byte [rw_mhold]
    call rw_mpin
.r:
    popf
    ret

; rw_adrop - the ROM's plan did not fit: the last image claimed given back.
; CF = 1: none to give (or they are read already, and stay)
rw_adrop:
    cmp byte [rw_afix], 0
    jne .no
%ifdef PKG_MEGA
    cmp word [rw_axbase + 2], 0     ; (the pool's block is not given back
    jne .no                         ;   an image at a time)
%endif
    cmp byte [rw_anseg], 0
    je .no
    dec byte [rw_anseg]
    mov bl, [rw_anseg]
    xor bh, bh
    shl bx, 1
    xor dx, dx
    xchg dx, [rw_aseg + bx]
    call OSAPI_MEM_FREE
    clc
    ret
.no:
    stc
    ret

; rw_aread - after the plan, on the UI task in the game's folder: every
; image claimed read from the package's own folder (rw_jpart's reason), each
; one that cannot be read given back, and then everything put right
; (rw_afixup). Once a session
rw_aread:
    cmp byte [rw_afix], 0
    jne .r
    mov byte [rw_afix], 1
    push bp
    mov dx, [rw_pkgdir]
    mov bl, [rw_pkgvol]
    call rw_fgo
    jnc .go
    xor bx, bx                      ; no folder: none of them, interpreted -
.z:                                 ;   every claim given back unread, or
    cmp bl, [rw_anseg]              ;   rw_afixup links heap it never read
    jae .fix                        ;   (rw_fgo freed the hold already)
    push bx
%ifdef PKG_MEGA
    cmp bl, [rw_axn]                ; (one of the pool block's: a window,
    jae .zh                         ;   not a claim - zeroed, and it goes
    shl bx, 1                       ;   with the block, as .lost's)
    mov word [rw_aseg + bx], 0
    jmp short .zn
.zh:
%endif
    shl bx, 1
    xor dx, dx
    xchg dx, [rw_aseg + bx]
    or dx, dx
    jz .zn
    call OSAPI_MEM_FREE
.zn:
    pop bx
    inc bx
    jmp short .z
.go:
    xor bx, bx
.l:
    cmp bl, [rw_anseg]
    jae .back
    push bx
    shl bx, 1
    mov bp, [rw_aseg + bx]
    shr bx, 1
    mov al, bl
    shl al, 1
    add al, [rw_acode]
    call rw_apart                   ; the first half...
    jc .lost
    inc al
%ifdef PKG_MEGA
    push ax                         ; (its second 32KB: a window under
    push dx                         ;   CPU_F_PM, 20.15)
    mov dx, bp
    mov ax, 800h
    call rw_sub
    mov bp, dx
    pop dx
    pop ax
    jc .lost
%else
    add bp, 800h
%endif
    call rw_apart                   ; ...and the second
    jnc .ok
.lost:
    pop bx
    push bx
%ifdef PKG_MEGA
    cmp bl, [rw_axn]                ; (one of the pool block's: a window,
    jae .lh                         ;   not a claim - it goes with the
    shl bx, 1                       ;   block)
    mov word [rw_aseg + bx], 0
    jmp short .ok
.lh:
%endif
    shl bx, 1
    xor dx, dx
    xchg dx, [rw_aseg + bx]
    call OSAPI_MEM_FREE
.ok:
    pop bx
    inc bx
    jmp short .l
.back:
%ifdef PKG_MEGA
    call rw_hyload                  ; (the misses' recompiler, 20.16)
%endif
    call rw_fback
.fix:
    call rw_afixup
    pop bp
.r:
    ret

; rw_apart - AL = a row, BP = where it goes: the part read there, packed or
; plain. CF = 1: it could not be. Every register but the flags kept; the
; folder is the package's (rw_fgo).
;
; THE READ IS CLUSTER-ALIGNED, THROUGH A BOUNCE (rw_abounce): OSAPI_FILE_READ_AT
; refuses an offset or a capacity that is not a whole number of CLUSTERS, and
; a part starts on a 512-byte boundary. On a floppy a cluster is 512 bytes, so
; the first package read in place and every emulator run was clean; on a hard
; disk every image was refused and interpreted - the owner's 286 ran at a
; third of the recompiler's speed. So the stream is read a chunk of whole
; clusters at a time into the bounce and only its own bytes are copied to
; BP:R, which also means nothing lands past the part and the image's claim
; is 64KB, not 65.
rw_apart:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, op_table + OP_T_ROWS
    xor ah, ah
    mov cl, 3
    shl ax, cl
    add si, ax                      ; SI = the row
    mov [rw_arow], si
    mov bx, [si + OP_R_LEN]         ; BX = what it is in memory
    mov cx, bx                      ; (plain: copied where it goes)
    xor di, di
    test byte [si + OP_R_FLAG], OP_COMP
    jz .rd
    mov cx, [si + OP_R_ZKB]         ; CX = the packed bytes
    sub bx, cx                      ; BX = R: the stream sits at the top,
    jbe .no                         ;   exactly, and expands down to 0
    mov di, bx
.rd:
    mov [rw_awant], cx
    mov [rw_adst], di
    mov si, [rw_arow]
    mov ax, [si + OP_R_OFF]         ; DX:AX = the part's byte offset
    mov dx, ax
    mov cl, 9
    shl ax, cl
    mov cl, 7
    shr dx, cl
    call rw_acopy
    jc .no
    mov si, [rw_arow]
    test byte [si + OP_R_FLAG], OP_COMP
    jz .ok
    mov cx, [si + OP_R_ZKB]         ; CX = the packed bytes
    mov dx, [si + OP_R_LEN]         ; DX = what they expand to
    mov si, dx
    sub si, cx                      ; SI = R
    mov al, [op_table + OP_T_FMT]
    xor ah, ah
    xor bx, bx
    xor di, di
    mov es, bp
    push ds
    mov ds, bp                      ; DS:SI = the stream, ES:0 its output
    call OSAPI_DECOMP
    pop ds
    jc .no
.ok:
    clc
    jmp short .r
.no:
    stc
.r:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rw_acopy - DX:AX = a byte offset in the package's own file (op_name, in
; the current folder): [rw_awant] bytes from there to BP:[rw_adst], read a
; chunk of whole clusters at a time into the bounce. CF = 1: they could not
; be. Clobbers AX-DI and ES; [rw_awant] and [rw_adst] are spent
rw_acopy:
    push ax
    push dx
    call op_clbget                  ; AX = bytes a cluster
    mov [rw_aclb], ax
    pop dx
    pop ax
    jc .no
    call rw_abounce                 ; [rw_abseg], [rw_abcap]
    jc .no
    mov bx, [rw_aclb]
    dec bx
    mov cx, ax
    and cx, bx                      ; CX = how far past a cluster's start
    mov [rw_askip], cx
    not bx
    and ax, bx                      ; DX:AX = that cluster's
    mov [rw_apos], ax
    mov [rw_apos + 2], dx
.chunk:
    mov es, [rw_abseg]
    xor bx, bx
    mov cx, [rw_abcap]
    mov ax, [rw_apos]
    mov dx, [rw_apos + 2]
    mov si, op_name
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    jc .nob
    sub ax, [rw_askip]              ; AX = this chunk's bytes of the part
    jbe .nob
    cmp ax, [rw_awant]
    jbe .take
    mov ax, [rw_awant]
.take:
    mov cx, ax
    push ds
    mov si, [rw_askip]
    mov di, [rw_adst]
    mov es, bp
    mov ds, [rw_abseg]
    cld
    rep movsb
    pop ds
    add [rw_adst], ax
    sub [rw_awant], ax
    mov word [rw_askip], 0
    mov ax, [rw_abcap]
    add [rw_apos], ax
    adc word [rw_apos + 2], 0
    cmp word [rw_awant], 0
    jne .chunk
    call rw_abfree
    clc
    ret
.nob:
    call rw_abfree
.no:
    stc
    ret

; rw_abounce - a bounce for rw_apart's reads: [rw_abseg] and [rw_abcap], a
; whole number of clusters. The frame buffers' claim ([rw_pseg], 17,280
; bytes, idle while a game loads) when a cluster is 16KB or less; else a
; claim of one cluster of its own, given back by rw_abfree. CF = 1: none
rw_abounce:
    push ax
    push dx
    mov byte [rw_abown], 0
    mov ax, [rw_aclb]
%ifdef PKG_MEGA
    cmp byte [rw_pm], 0             ; (kern_mega's arm: the frame buffers are
    je .fb                          ;   OS88_HIGHMEM's, above 1MB, and a read
    mov word [rw_abcap], 16384      ;   there bounces a sector at a time -
    cmp ax, 16384                   ;   so a conventional bounce of its own,
    jbe .dma                        ;   20.15)
    mov [rw_abcap], ax
.dma:
    mov ax, [rw_abcap]
    mov dl, ah
    shr dl, 1
    shr dl, 1
    mov al, dl
    xor ah, ah
    push cx
    xor cx, cx
    call OSAPI_MEM_CLAIM_DMA        ; DX = its segment, conventional
    pop cx
    jc .r
    mov [rw_abseg], dx
    mov byte [rw_abown], 1
    jmp short .r
.fb:
    mov ax, [rw_aclb]
%endif
    cmp ax, 16384
    ja .own
    mov ax, [rw_pseg]
    or ax, ax
    jz .own
    mov [rw_abseg], ax
    mov word [rw_abcap], 16384      ; (a multiple of every cluster to 16KB)
    clc
    jmp short .r
.own:
    mov ax, [rw_aclb]
    mov [rw_abcap], ax
    mov dl, ah                      ; KB = bytes / 1024
    shr dl, 1
    shr dl, 1
    mov al, dl
    xor ah, ah
    call OSAPI_MEM_CLAIM            ; DX = its segment
    jc .r
    mov [rw_abseg], dx
    mov byte [rw_abown], 1
.r:
    pop dx
    pop ax
    ret

; rw_abfree - the bounce given back, if it was a claim of its own. Flags kept
rw_abfree:
    pushf
    cmp byte [rw_abown], 0
    je .r
    push dx
    mov dx, [rw_abseg]
    call OSAPI_MEM_FREE
    pop dx
    mov byte [rw_abown], 0
.r:
    popf
    ret

; rw_aimg - AL = an image, 0 the core: AX = its segment, or 0 when it is
; not in memory. Every other register kept
rw_aimg:
    or al, al
    jnz .c
    mov ax, [rw_acore]
    ret
.c:
    cmp al, [rw_anseg]
    ja .none
    push bx
    mov bl, al
    xor bh, bh
    dec bx
    shl bx, 1
    mov ax, [rw_aseg + bx]
    pop bx
    ret
.none:
    xor ax, ax
    ret

%ifdef PKG_MEGA
; rw_acsset - kern_mega's arm (REALDMG-SPEC 20.15): rw_acs[n] = image n's
; CODE selector (0 the core), rw_csel's answer for its data segment - the
; segment itself on a real-mode kernel, a pair's code half under CPU_F_PM -
; or 0 when it is not in memory. Asked once, with DS ours, before anything
; is given one. Every register kept
rw_acsset:
    push ax
    push bx
    xor bx, bx
.l:
    mov al, bl
    call rw_aimg
    or ax, ax
    jz .s
    call rw_csel
.s:
    push bx
    shl bx, 1
    mov [rw_acs + bx], ax
    pop bx
    inc bx
    cmp bx, RWA_MAX + 1
    jb .l
    pop bx
    pop ax
    ret

; rw_aimgc - AL = an image, 0 the core: AX = its CODE selector, or 0 when it
; is not in memory (rw_acsset's). Every other register kept
rw_aimgc:
    cmp al, RWA_MAX
    ja .none
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [rw_acs + bx]
    pop bx
    ret
.none:
    xor ax, ax
    ret

rw_acs          times RWA_MAX + 1 dw 0
%endif

; rw_afixup - the far jumps' segments given, or the exit put back where the
; image it jumps to is not here; then every token in the table and the list
; made a segment, or the entry emptied. S_JITSEGA's image chosen
rw_afixup:
    push bp
    push es
%ifdef PKG_MEGA
    call rw_acsset                  ; (each image's CODE selector, 20.15)
%endif
    mov al, [rw_arr]                ; the relocations, a part at a time
    mov [rw_arcur], al              ;   (20.17)
    mov al, [rw_arn]
    mov [rw_arleft], al
.rr:
    cmp byte [rw_arleft], 0
    je .tab
    dec byte [rw_arleft]
    mov al, [rw_arcur]
    inc byte [rw_arcur]
    call op_row                     ; SI -> its row, AX kept
    mov cx, [si + OP_R_LEN]
    shr cx, 1
    shr cx, 1                       ; CX = its relocations
    call op_seg
    or ax, ax
    jz .rr
    mov es, ax
    xor si, si
    jcxz .rr
.rl:
    mov al, [es:si]                 ; the image the jump is in
    call rw_aimg
    or ax, ax
    jz .rn                          ; (not here: nothing in it runs)
    mov bp, ax                      ; BP = its segment
    mov al, [es:si + 1]
    mov dl, al                      ; DL = the form
    and al, 7Fh
%ifdef PKG_MEGA
    call rw_aimgc                   ; AX = the target's CODE selector, or 0
%else
    call rw_aimg                    ; AX = the target's, or 0
%endif
    mov di, [es:si + 2]             ; DI = the site
    push es
    mov es, bp
    or ax, ax
    jz .unl
    mov bx, di
    test dl, 80h
    jz .b0
    add bx, 8                       ; a bank's site: the jump after the test
.b0:
    mov [es:bx + 3], ax             ; jmp far off:SEG
    jmp short .rp
.unl:
    mov byte [es:di], 0BAh          ; mov dx, SITE...
    mov [es:di + 1], di
    mov byte [es:di + 3], 0E9h      ; ...jmp tr_lsite, as rt_exit wrote it
    mov ax, RDGC_TR_LSITE - 6
    sub ax, di
    mov [es:di + 4], ax
.rp:
    pop es
.rn:
    add si, 4
    loop .rl
    jmp short .rr
.tab:
    mov es, [rw_atseg]              ; the table: (mask + 1) x 256 entries
    mov cl, [rw_ahm]
    xor ch, ch
    inc cx
    xchg cl, ch                     ; CX = x 256
    mov dl, 1                       ; (the table's: emptied out of reach)
    call .ents
    xor bx, bx                      ; ...and the list, a part at a time
.lp:                                ;   (20.17)
    mov al, bl
    cmp al, [rw_alxn]
    jae .lpd
    push bx
    shl bx, 1
    shl bx, 1
    mov es, [rw_alx + bx]
    mov cx, [rw_alx + bx + 2]
    xor dl, dl                      ; (the list's: kept in its order)
    call .ents
    pop bx
    inc bx
    jmp short .lp
.lpd:
    mov bx, 1                       ; S_JITSEGA: the first image here, or
.sa:                                ;   the core
    cmp bl, [rw_anseg]
    ja .sx
    mov al, bl
    call rw_aimg
    or ax, ax
    jnz .sg
    inc bx
    jmp short .sa
.sg:
    mov [rw_asega], ax
.sx:
    pop es
    pop bp
    ret
.ents:                              ; CX entries at ES:0
    xor di, di
    jcxz .er
.e:
    cmp word [es:di + 4], 0
    je .en
    mov ax, [es:di + 6]
    sub ax, RWA_TOKEN
    cmp ax, RWA_MAX
    ja .en                          ; (a segment already: a second load)
%ifdef PKG_MEGA
    call rw_aimgc                   ; (what tr_enter far-jumps through)
%else
    call rw_aimg
%endif
    or ax, ax
    jnz .es
    mov [es:di + 4], ax             ; not here: offset 0, interpreted -
    or dl, dl                       ;   and a TABLE entry is given a key no
    jz .en                          ;   lookup asks, PC 8000h (RAM below
    mov word [es:di], 8000h         ;   HRAM, never looked up): one that
    mov byte [es:di + 2], 0         ;   still matched would read as a block
    jmp short .en                   ;   being COUNTED, and past RDG_JITHOT
                                    ;   every entry asked the translator's
                                    ;   door - the host's stack and back -
                                    ;   for nothing: 20x the interpreter
.es:
    mov [es:di + 6], ax
.en:
    add di, 8
    loop .e
.er:
    ret

; rw_aoton - DS = the view, after rdg_init: what the precompiled machine
; is (aothost.asm's mode 3 says the same to the harness): the table fixed
; and the host's, nothing translated - not even HRAM's code - the control
; transfers looking for translated code, and the table's misses searching
; the list. Every register kept
rw_aoton:
    push ax
    push cx
    mov ax, [cs:rw_asega]
    mov [S_JITSEGA], ax
    mov [S_JITSEG], ax
%ifdef PKG_MEGA
    push bx                         ; KERN_MEGA'S ARM (REALDMG-SPEC 20.15):
    push si                         ;   what is JUMPED to by its code
    push di                         ;   selector, and every image named to
    xor bx, bx                      ;   the core as a pair, so a link made
    mov di, S_JITXT                 ;   at run time into one (tr_lpatch)
.xt:                                ;   is written through its data half
    inc bx                          ;   (tr_lpx, 16.2.4) - S_JITXT's 55
    cmp bx, RWA_MAX                 ;   pairs hold RWA_MAX's 14
    ja .xd
    mov si, bx
    shl si, 1
    mov ax, [cs:rw_acs + si]
    or ax, ax
    jz .xt
    mov [di + 2], ax
    mov ax, [cs:rw_aseg + si - 2]
    mov [di], ax
    add di, 4
    jmp short .xt
.xd:
    mov ax, di
    sub ax, S_JITXT
    shr ax, 1
    shr ax, 1
    mov [S_JITXN], al
    mov ax, [cs:rw_asega]           ; S_JITSEGA's code half: the image it
    xor si, si                      ;   is (rw_afixup's first loaded), or
.ca:                                ;   the core's
    add si, 2
    cmp si, RWA_MAX * 2
    ja .c0
    cmp ax, [cs:rw_aseg + si - 2]
    jne .ca
    mov ax, [cs:rw_acs + si]
    jmp short .cs
.c0:
    mov ax, [cs:rw_acs]
.cs:
    mov [S_JITCSA], ax
    mov [S_JITCS], ax
    mov word [S_JITCSB], 0
    pop di
    pop si
    pop bx
%endif
    mov [S_JITOUT + 2], ax
    mov word [S_JITSEGB], 0
    mov ax, [cs:rw_atseg]
    mov [S_JITHSEG], ax
    mov al, [cs:rw_ahm]
    mov [S_HMASK], al
    mov byte [S_JITNOC], 1
    mov word [S_JITVEC], rw_aotstub
    mov [S_JITVEC + 2], cs
    mov word [S_RESVEC], RDGC_TR_ENTER
    mov byte [S_JITON], 1
    mov byte [S_JITFIX], 1
    mov byte [S_JITPRE], 2
    mov word [S_JITEND], 0
    call rw_jdon
    push si                         ; the list's parts to the core's own
    push di                         ;   directory (20.17)
    mov al, [cs:rw_alxn]
    mov [S_AOTLX], al
    xor si, si
    xor di, di
.lxd:
    mov ax, [cs:rw_alx + si]
    mov [S_AOTLXT + di], ax
    mov ax, [cs:rw_alx + si + 2]
    mov [S_AOTLXT + di + 6], ax
    add si, 4
    add di, 8
    cmp si, RDG_LXMAX * 4
    jb .lxd
    pop di
    pop si
    mov ax, [cs:rw_alseg]
    mov cx, [cs:rw_aln]
    push bx
    mov bx, [cs:rw_afseg]
    call rdg_aotattach
    pop bx
%ifdef PKG_MEGA
    call rw_hyon                    ; (the misses translated, 20.16)
%endif
    pop cx
    pop ax
    ret

; rw_jdon - DS = the view: the control transfers look for translated code
; (rdgjit.inc's rt_dispatch with AL = 1): each of RDGC_JDTAB's sites made
; cmp si,8000h / jae - ROM PCs to the table, the rest dispatched
rw_jdon:
    push bx
    push cx
    push si
    push es
    mov es, [S_COREDS]              ; the core, as DATA (16.2.4: S_DISP+2
                                    ;   is its code selector on kern_mega)
    mov si, rw_jdtab
    mov cx, RDGC_JDN
.s:
    mov bx, [cs:si]
    mov word [es:bx], 0FE81h
    mov word [es:bx + 2], 8000h
    mov byte [es:bx + 4], 73h
    inc si
    inc si
    loop .s
    pop es
    pop si
    pop cx
    pop bx
    ret

; rw_apre - the F1 line's tail at ES:DI: ' Pn/m', the code images in memory
; against the meta's, NUL-terminated - how a photograph of the glass says
; whether the package is running its own code. Keeps all but DI
rw_apre:
    push ax
    push bx
    push cx
    mov al, ' '
    stosb
    mov al, 'P'
    stosb
    xor cx, cx
    xor bx, bx
.l:
    cmp bl, [rw_anseg]
    jae .d
    push bx
    shl bx, 1
    cmp word [rw_aseg + bx], 0
    pop bx
    je .n
    inc cx
.n:
    inc bx
    jmp short .l
.d:
    mov al, cl
    call .hex
    mov al, '/'
    stosb
    mov al, [rw_ans]
    call .hex
    xor al, al
    stosb
    pop cx
    pop bx
    pop ax
    ret
.hex:
    add al, '0'
    cmp al, '9'
    jbe .h
    add al, 'A' - '9' - 1
.h:
    stosb
    ret

%ifdef PKG_MEGA
; MISSES TRANSLATED (REALDMG-SPEC 20.16), kern_mega's package alone: what the
; host did not precompile - a jump table's targets, a stretch no replay ran
; - is counted as the interpreter takes it, and at HY_HOT entries the
; recompiler (the last part, rdgjitp.asm built RDG_HYB) makes it a block in
; a RUNTIME tier, filed in a table of its own (rdgaot.inc's rdg_hyins). One
; pool block holds it all: RWH_T tiers at most, the part, the table.
RWH_T           equ 4

; rw_hyload - in rw_aread, on the UI task in the package's folder: the pool
; block claimed (as many tiers as fit, one at least) and the part read into
; it. Nothing claimed when the package has no part or the machine is not
; protected-mode; on any failure nothing is kept. Every register kept
rw_hyload:
    cmp byte [rw_pm], 0
    je .r0
    cmp byte [rw_ajrow], 0
    je .r0
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov cx, RWH_T + 2               ; tiers, the part and the table
.try:
    mov dx, cx                      ; DX:AX = CX x 64KB
    xor ax, ax
    call OSAPI_XMEM_ALLOC
    jnc .got
    dec cx
    cmp cx, 3
    jae .try
    jmp short .x
.got:
    mov [rw_hybase], ax
    mov [rw_hybase + 2], dx
    push cx
    xor cx, cx
    call OSAPI_XMEM_SEL             ; AX = its first 64KB's selector
    pop cx
    jc .free
    mov dx, ax
    xor ax, ax                      ; AX = paragraphs into the block
    xor bx, bx
.p:
    push dx
    call rw_subc                    ; DX = that 64KB's data half
    jc .pe
    mov [rw_hyseg + bx], dx
    pop dx
    add ah, 10h
    inc bx
    inc bx
    loop .p
    mov si, bx                      ; the last two: the part, the table
    sub si, 4
    mov ax, si
    shr ax, 1
    mov [rw_hyn], al                ; (the tiers before them)
    mov bp, [rw_hyseg + si]
    mov al, [rw_ajrow]
    call rw_apart
    jc .free
    mov [rw_hyp], bp
    mov ax, [rw_hyseg + si + 2]
    mov [rw_hyh], ax
    mov ax, bp                      ; the doors, by its code selector
    call rw_csel
    mov [rw_jhyon + 2], ax
    mov [rw_jxlat + 2], ax
    mov [rw_jmore + 2], ax
    jmp short .x
.pe:
    pop dx
.free:
    mov ax, [rw_hybase]
    call OSAPI_XMEM_FREE
    xor ax, ax
    mov [rw_hybase], ax
    mov [rw_hybase + 2], ax
    mov [rw_hyp], ax
.x:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r0:
    ret

; rw_hyon - DS = the view, at the end of rw_aoton (every machine start):
; the runtime table emptied, the tiers past the first put in S_JITXT AHEAD
; of the images (rt_room's turn reaches a precompiled image only when every
; runtime tier is written, and stops there), the first made current
; (rt_hyon) and the core told to count. Every register kept
rw_hyon:
    cmp word [cs:rw_hyp], 0
    je .r0
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [cs:rw_hyh]             ; the table and the counts, empty
    mov [S_HYSEG], ax
    mov es, ax
    xor di, di
    mov cx, HY_SEGB / 2
    xor ax, ax
    cld
    rep stosw
    mov [S_HYN], ax
    mov [S_HYMISS], ax
    mov bl, [cs:rw_hyn]             ; BL = runtime tiers past the first
    dec bl
    jz .one
    push ds                         ; the images' pairs moved up BL places
    pop es
    mov al, [S_JITXN]
    xor ah, ah
    mov cx, ax
    shl cx, 1                       ; (CX = their words)
    shl ax, 1
    shl ax, 1
    mov si, S_JITXT - 2
    add si, ax                      ; SI = the last word
    mov al, bl
    xor ah, ah
    shl ax, 1
    shl ax, 1
    mov di, si
    add di, ax
    std
    rep movsw
    cld
    mov bh, [S_JITXN]
    mov byte [S_JITXN], 0           ; ...and the tiers in front (rt_more:
    mov si, rw_hyseg + 2            ;   their tails, no generation)
    mov cl, bl
    xor ch, ch
.t:
    mov ax, [cs:si]
    push ax
    call rw_csel
    mov dx, ax
    pop ax
    call far [cs:rw_jmore]
    inc si
    inc si
    loop .t
    add [S_JITXN], bh
    mov bl, [S_JITXN]               ; (the table's zero after them)
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov word [S_JITXT + bx], 0
.one:
    mov byte [S_HYON], 1            ; (first: rt_tier takes the images'
    mov ax, [cs:rw_hyseg]           ;   generation when it is on)
    push ax
    call rw_csel
    mov dx, ax
    pop ax
    call far [cs:rw_jhyon]
    mov word [S_JITVEC], rw_hyjit
    mov [S_JITVEC + 2], cs
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.r0:
    ret

; rw_hyjit - the translator's door under S_HYON (rdgaot.inc's .cnt): the
; miss at SI made a block (the part's rt_translate) and filed (rdg_hyins),
; then back at the core's tr_back - into it, or the interpreter's when none
; could be made
rw_hyjit:
    RDG_TOHOST
    push ax
    push bx
    push cx
    push dx
    push di
    push bp
    push es
    push si
    call far [cs:rw_jxlat]
    pop si
    push si
    call rdg_hyins
    pop si
    pop es
    pop bp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    jmp rdg_ret

rw_hybase       dw 0, 0             ; the pool block, or 0
rw_hyseg        times RWH_T + 2 dw 0    ; its 64KBs' data halves
rw_hyn          db 0                ; ...the runtime tiers among them
rw_hyp          dw 0                ; the part's data half, 0: none loaded
rw_hyh          dw 0                ; ...and the table's
rw_ajrow        db 0                ; the part's row (the meta's byte 55)
rw_jhyon        dw 50, 0            ; the part's rt_hyon, AX and DX as its
rw_jxlat        dw 60, 0            ; ...and its rt_translate, SI the PC
%endif

; the translator's door: nothing is translated, so a block asked for is the
; interpreter's (aothost.asm's h_jitstub)
rw_aotstub:
    RDG_TOHOST
    mov word [S_JITOUT], 0
    jmp rdg_ret
