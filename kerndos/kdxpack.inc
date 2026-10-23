; =============================================================================
; kerndos/kdxpack.inc - an EXEPACK program UNPACKED BY THE LOADER
; (SPEC.md 96.40.9.1; -DKD_HMA only, docs/plans/KERN-MEGA-PLAN.md 14.24)
;
; kern_dos in the HMA runs the program with A20 OPEN and its arena below 64KB,
; which is the one arrangement EXEPACK's own unpacker cannot survive: it
; normalises its two pointers DOWNWARD, so the bottom of its output is written
; through a segment below zero and reaches the HMA instead of the PSP's own
; paragraphs. The cure DOS 5 shipped was LOADFIX, and the cure 14.23 shipped
; was LOADFIX taken unasked - ~55KB of the arena gone, on exactly the programs
; that are big enough to have been packed.
;
; THIS IS THE OTHER CURE: the program's unpacker never runs. The format is
; published and small, so the loader does the unpacking itself, with pointers
; that are clamped at the load segment rather than wrapped below it, and then
; enters the program at the entry the packed header names - the arena stays
; where it was. Written from the format, not from anybody's stub:
;
;   CS:0000  dw real IP, real CS (relative to the load segment), a scratch
;            word, the EXEPACK block's own bytes (header + stub + table),
;            real SP, real SS (relative), and the unpacked image in
;            PARAGRAPHS; then 'RB' (the 16-byte header, entry at CS:0010), or
;            a skip count and THEN 'RB' (the 18-byte one, entry at CS:0012)
;   the packed image ends at paragraph CS - skip (skip = 1 on the short
;            form), up to fifteen 0FFh bytes of padding at its very end
;   the stream is read BACKWARD from there and written backward from the top
;            of the unpacked image: a command byte, a 16-bit length below it,
;            then B0h/B1h = one fill byte below that, repeated LENGTH times,
;            or B2h/B3h = LENGTH bytes copied as they lie; bit 0 = the last
;            command. Whatever is below the last command is already in place
;   the relocations follow the stub's "Packed file is corrupt" message and
;            end exactly at the block's last byte: sixteen groups, one per
;            64KB of the image, each a count word and that many offsets
;
; NOTHING IS WRITTEN UNTIL EVERYTHING HAS BEEN CHECKED: the header, the table
; (every fixup inside the unpacked image, and the groups ending exactly where
; the block does) and a DRY WALK of the whole command stream, which proves
; every length in bounds and that no command writes over bytes it has still to
; read. A refusal - an unknown variant, a header that does not add up, no room
; for the table above the image - leaves the image exactly as `dos_exe_setup`
; left it, and `kd_xpack` falls back to raising the floor (14.23's answer).
; =============================================================================
section .text

KXP_MSGLEN  equ 22                  ; "Packed file is corrupt"

; -----------------------------------------------------------------------------
; kd_xunp - unpack an EXEPACK program in place
; in:  `dos_exe_setup` has run: the image at [dos_exe_lseg], its entry at
;      [dos_exe_cs]:[dos_exe_ip], the block [dos_ldpsp] + [dos_ldpara]
; out: CF=0 - not EXEPACK's shape, nothing done; or unpacked, and the four
;      entry cells are the program's own. CF=1 - EXEPACK's shape and refused,
;      nothing written. Every register preserved; DS = KD_SEG
; -----------------------------------------------------------------------------
kd_xunp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [dos_exe_nrel], 0      ; THE SHAPE, as 14.23 keyed it: EXEPACK
    jne .not                        ; packs the relocations itself, so the
    mov bx, [dos_exe_ip]            ; header carries none...
    mov ax, 1                       ; ...the short header: skip is 1
    cmp bx, 0x10
    je .ip
    cmp bx, 0x12                    ; ...or the long one, whose word at 0Eh IS
    jne .not                        ; the skip
.ip:
    mov es, [dos_exe_cs]
    cmp word [es:bx-2], 'RB'
    jne .not
    cmp bx, 0x12
    jne .sk
    mov ax, [es:0x0E]
.sk:
    mov [kxp_skip], ax
    mov ax, [es:0]
    mov [kxp_rip], ax
    mov ax, [es:2]
    mov [kxp_rcs], ax
    mov ax, [es:8]
    mov [kxp_rsp], ax
    mov ax, [es:0x0A]
    mov [kxp_rss], ax
    mov ax, [es:0x0C]
    mov [kxp_dlen], ax
    mov ax, [es:6]
    mov [kxp_exsz], ax
    mov ax, [dos_exe_lseg]
    mov [kxp_l], ax

    ; --- the header adds up --------------------------------------------------
    mov cx, [kxp_exsz]
    cmp cx, 0xFFF0                  ; the block's offsets stay in one segment
    ja .bad
    mov ax, bx
    add ax, KXP_MSGLEN + 32         ; room for a message and sixteen counts
    cmp cx, ax
    jb .bad
    add cx, 15                      ; ...and the block lies inside the image
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    mov ax, es
    sub ax, [kxp_l]                 ; AX = C, the block's paragraph
    mov [kxp_c], ax
    add cx, ax
    jc .bad
    cmp cx, [dos_exe_ipara]
    ja .bad
    mov dx, [kxp_skip]              ; the packed image is C - skip + 1
    or dx, dx                       ; paragraphs, at least one...
    jz .bad
    cmp dx, ax
    ja .bad
    sub ax, dx
    inc ax
    mov [kxp_pp], ax
    cmp ax, [kxp_dlen]              ; ...and does not unpack to LESS
    ja .bad

    ; --- the relocation table: after the message, to the block's end -------
    mov di, bx
.scan:
    mov ax, [kxp_exsz]
    sub ax, KXP_MSGLEN + 32
    cmp di, ax
    ja .bad                         ; no message: not a stub this knows
    push di
    mov si, kxp_msg
    mov cx, KXP_MSGLEN
    cld
    repe cmpsb
    pop di
    je .msg
    inc di
    jmp short .scan
.msg:
    add di, KXP_MSGLEN
    mov [kxp_tbl], di
    mov si, di                      ; ES:SI = the table, in the block
    call kxp_table                  ; every group, every fixup, checked
    jc .bad

    ; --- room for the table ABOVE the unpacked image -------------------------
    ; The image grows over the block, so the table moves first - to the
    ; paragraph straight above the image, which is where the program's own
    ; unpacker would have put its copy and so inside the memory its header
    ; asked for. Checked against the block anyway.
    mov ax, [kxp_l]
    add ax, [kxp_dlen]
    jc .bad
    mov [kxp_t], ax
    mov cx, [kxp_exsz]
    sub cx, [kxp_tbl]
    mov [kxp_tlen], cx
    add cx, 15
    shr cx, 1
    shr cx, 1
    shr cx, 1
    shr cx, 1
    add ax, cx
    jc .bad
    mov cx, [dos_ldpsp]
    add cx, [dos_ldpara]
    cmp ax, cx
    ja .bad

    ; --- the stream, walked DRY ----------------------------------------------
    mov byte [kxp_go], 0
    call kxp_start
    jc .bad
    call kxp_walk
    jc .bad

    ; --- and only now, for real: the table up, the stream, the fixups -------
    call kxp_tmove
    mov byte [kxp_go], 1
    call kxp_start
    call kxp_walk                   ; cannot refuse: the dry walk took every
                                    ; step this one takes
    call kxp_reloc
    mov ax, [kxp_l]                 ; ...and the entry the header names
    mov bx, ax
    add ax, [kxp_rcs]
    mov [dos_exe_cs], ax
    mov ax, [kxp_rip]
    mov [dos_exe_ip], ax
    add bx, [kxp_rss]
    mov [dos_exe_ss], bx
    mov ax, [kxp_rsp]
    mov [dos_exe_sp], ax
    mov ax, [kxp_dlen]
    mov [dos_exe_ipara], ax
.not:
    clc
    jmp short .out
.bad:
    stc
.out:
    cld
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; kxp_table - ES:SI = the packed relocation table: CF=0 when its sixteen
; groups end exactly at the block's end and every fixup's word lies inside
; the unpacked image. Clobbers AX, BX, CX, DX, SI
kxp_table:
    xor dx, dx                      ; DX = the group's paragraph, g * 1000h
.grp:
    mov ax, [kxp_exsz]
    sub ax, si
    cmp ax, 2
    jb .bad
    mov cx, [es:si]
    add si, 2
    sub ax, 2                       ; bytes left after the count
    shr ax, 1
    cmp cx, ax
    ja .bad
    jcxz .next
.ent:
    mov ax, [es:si]
    add si, 2
    mov bx, ax
    shr bx, 1
    shr bx, 1
    shr bx, 1
    shr bx, 1
    add bx, dx                      ; BX = the word's paragraph...
    and al, 15
    cmp al, 15
    jne .in
    inc bx                          ; ...or the next one, for its high byte
    jz .bad
.in:
    cmp bx, [kxp_dlen]
    jae .bad
    loop .ent
.next:
    add dx, 0x1000
    jnc .grp
    cmp si, [kxp_exsz]
    jne .bad
    clc
    ret
.bad:
    stc
    ret

; kxp_start - the walk's two pointers, linear from the load segment: S on the
; packed image's last byte that is not padding, D on the unpacked image's last
; byte. CF=1 = the padding fills the whole last paragraph
kxp_start:
    mov ax, [kxp_dlen]
    call kxp_p2l
    sub ax, 1
    sbb dx, 0
    mov [kxp_d], ax
    mov [kxp_d+2], dx
    mov ax, [kxp_pp]
    call kxp_p2l
    sub ax, 1
    sbb dx, 0
    mov cx, 15                      ; at most fifteen bytes of 0FFh: a
.pad:                               ; paragraph of them is not a command
    call kxp_par
    cmp byte [es:bx], 0xFF
    jne .s
    sub ax, 1
    sbb dx, 0
    loop .pad
.s:
    mov [kxp_s], ax
    mov [kxp_s+2], dx
    clc
    ret

; kxp_walk - the commands, from [kxp_s] and [kxp_d], dry ([kxp_go] = 0) or
; writing. CF=1 = a step the stream cannot take. Clobbers AX-DI, ES
kxp_walk:
.cmd:
    mov ax, [kxp_s]
    mov dx, [kxp_s+2]
    sub ax, 2                       ; the length and the command: S - 2 >= 0
    sbb dx, 0
    js .bad
    call kxp_par                    ; ES:BX = S - 2, BX < 16, so the three
    mov cx, [es:bx]                 ; bytes are contiguous
    mov bl, [es:bx+2]
    mov [kxp_cmd], bl
    sub ax, 1                       ; S -= 3
    sbb dx, 0
    and bl, 0xFE
    cmp bl, 0xB2
    je .copy
    cmp bl, 0xB0
    jne .bad
    ; --- fill: one byte below the length, LENGTH times ---------------------
    or dx, dx
    js .bad
    call kxp_par
    mov bl, [es:bx]                 ; the byte
    sub ax, 1
    sbb dx, 0
    mov [kxp_s], ax
    mov [kxp_s+2], dx
    call kxp_dlen_ok
    jc .bad
    cmp byte [kxp_go], 0
    je .moved
    mov al, bl
    push ax
    mov ax, [kxp_d]
    mov dx, [kxp_d+2]
    call kxp_top
    mov di, bx
    pop ax
    std
    rep stosb
    cld
    jmp short .moved
.copy:
    ; --- copy: LENGTH bytes below the length, as they lie -------------------
    push ax
    push dx
    sub ax, cx                      ; S - LENGTH >= -1
    sbb dx, 0
    add ax, 1
    adc dx, 0
    pop dx
    pop ax
    js .bad
    call kxp_dlen_ok
    jc .bad
    cmp byte [kxp_go], 0
    je .cdry
    push ax
    push dx
    push ds
    call kxp_top                    ; DS:SI = the source's top...
    mov si, bx
    push es
    mov ax, [kxp_d]
    mov dx, [kxp_d+2]
    call kxp_top                    ; ...ES:DI = the destination's
    mov di, bx
    pop ds
    std
    rep movsb
    cld
    pop ds
    pop dx
    pop ax
    mov cx, [kxp_len]
.cdry:
    sub ax, cx
    sbb dx, 0
    mov [kxp_s], ax
    mov [kxp_s+2], dx
.moved:
    mov cx, [kxp_len]
    sub [kxp_d], cx                 ; D -= LENGTH
    sbb word [kxp_d+2], 0
    ; --- nothing read is ever written over first --------------------------
    ; Below the command the next write lands at D and the next read at S, so
    ; D >= S is the whole of it. A fill reads its byte before it writes, so the
    ; LAST command may be a fill that ends below S - the prefix under it is
    ; then never read.
    test byte [kxp_cmd], 1
    jz .chk
    test byte [kxp_cmd], 2
    jz .done
.chk:
    mov ax, [kxp_d]
    mov dx, [kxp_d+2]
    sub ax, [kxp_s]
    sbb dx, [kxp_s+2]
    js .bad
    test byte [kxp_cmd], 1
    jz .cmd
.done:
    clc
    ret
.bad:
    stc
    ret

; kxp_dlen_ok - CX = a command's length: CF=1 unless D + 1 >= it. Banks it in
; [kxp_len]. Every register kept
kxp_dlen_ok:
    push ax
    push dx
    mov [kxp_len], cx
    mov ax, [kxp_d]
    mov dx, [kxp_d+2]
    add ax, 1
    adc dx, 0
    sub ax, cx
    sbb dx, 0
    pop dx
    pop ax
    jns .ok
    stc
    ret
.ok:
    clc
    ret

; kxp_p2l - AX paragraphs -> DX:AX bytes
kxp_p2l:
    xor dx, dx
    mov cx, 4
.s:
    shl ax, 1
    rcl dx, 1
    loop .s
    ret

; kxp_par - DX:AX = linear from the load segment (DX < 10h): ES:BX = it with
; BX < 16. Every other register kept
kxp_par:
    push ax
    push dx
    mov bx, ax
    and bx, 15
    call kxp_l2p
    add ax, [cs:kxp_l]
    mov es, ax
    pop dx
    pop ax
    ret

; kxp_top - the same address with BX as HIGH as it goes: at least 0FFF0h once
; the address is that far up, so a backward `rep` of any 16-bit length stays
; inside the segment - and NEVER a segment below the load segment, which is
; precisely the normalisation EXEPACK's own unpacker gets wrong below 64KB
kxp_top:
    push ax
    push dx
    or dx, dx
    jnz .hi
    cmp ax, 0xFFF0
    jae .hi
    mov bx, ax
    mov ax, [cs:kxp_l]
    jmp short .set
.hi:
    mov bx, ax
    or bx, 0xFFF0                   ; (low nibble kept)
    call kxp_l2p
    sub ax, 0x0FFF
    add ax, [cs:kxp_l]
.set:
    mov es, ax
    pop dx
    pop ax
    ret

; kxp_l2p - DX:AX linear -> AX paragraphs (DX < 10h); clobbers DX
kxp_l2p:
    push cx
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
    pop cx
    ret

; kxp_tmove - the relocation table, [kxp_tlen] bytes from the block to
; [kxp_t]:0. The two can overlap either way round, so the direction is the
; one that reads each byte before it is overwritten
kxp_tmove:
    push ds
    mov cx, [kxp_tlen]
    mov si, [kxp_tbl]
    mov ax, [kxp_l]
    add ax, [kxp_c]
    mov ds, ax                      ; DS:SI = the table where it lies
    mov es, [cs:kxp_t]
    xor di, di                      ; ES:DI = above the image
    mov dx, si
    shr dx, 1
    shr dx, 1
    shr dx, 1
    shr dx, 1
    add ax, dx
    mov dx, es
    cmp dx, ax                      ; destination paragraph against the
    jbe .fwd                        ; source's, rounded down: at or below it
    add si, cx                      ; is a forward copy, above it backward
    dec si
    add di, cx
    dec di
    std
.fwd:
    rep movsb
    cld
    pop ds
    ret

; kxp_reloc - every fixup in the moved table: the load segment added to the
; word at (load + g * 1000h + offset / 16):(offset & 15)
kxp_reloc:
    mov es, [kxp_t]
    xor si, si
    xor dx, dx
.grp:
    mov cx, [es:si]
    add si, 2
    jcxz .next
.ent:
    mov ax, [es:si]
    add si, 2
    push es
    mov bx, ax
    and bx, 15
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add ax, dx
    add ax, [kxp_l]
    mov es, ax
    mov ax, [kxp_l]
    add [es:bx], ax
    pop es
    loop .ent
.next:
    add dx, 0x1000
    jnc .grp
    ret

; =============================================================================
; kd_tolow - switch THIS HMA kern_dos to the LOW one for a refused EXEPACK
; program (SPEC.md 96.40.9.1, docs/plans/KERN-MEGA-PLAN.md 14.26)
; in:  the program did not unpack (kd_xunp refused); SI/ES free; DS = KD_SEG
; out: DOES NOT RETURN on success - it reads part 3 of DOS.O88, relocates it
;      to 0060:0000 where the low image runs, copies the launch block and the
;      core in, and far-jumps into it exactly as the kernel's loader stub
;      would (kernel/hiber.inc). RETURNS when it could not - a field the box
;      did not fill, a mount or a read that failed - and kd_xpack then falls
;      back to 14.23's floor raise. Every register may be clobbered.
;
; **WHY THE SWITCH BEATS THE FLOOR RAISE**: the raise keeps the HMA arm, A20
; open, and gives the program the arena above 64KB (566 KB for TD3); the low
; image runs with A20 SHUT, so the program's own unpacker wraps correctly and
; needs no help, and the arena is the whole of conventional memory (585 KB).
; No re-post to the desktop: the HMA kern_dos is in real mode with its own
; file layer, so it reads part 3 itself. The way home is the low route's,
; because the launch block - park fields and all - is carried verbatim.
;
; **THE COPY-DOWN CLOBBERS THIS IMAGE'S OWN LOW HALF** (kdlayout.inc): the HMA
; arm's FAT snapshot, mount buffers and stack live at 0060h and up, and the
; low image expands over them. So everything that uses the stack - the mount,
; the chain walk, the read, the expand - runs FIRST, into a staging window in
; the arena; the final copy-down and jump then touch no stack at all.
; =============================================================================
section .text
kd_tolow:
    mov si, [kd_lbp]
    cmp word [si+KDL_L3OFF], 0          ; the box filled part 3's row...
    je .ret
    cmp word [si+KDL_L3ZB], 0
    je .ret
    cmp word [si+KDL_O88CL], 0          ; ...and the kernel found DOS.O88
    je .ret

    ; --- mount the volume DOS.O88 is on, and read part 3's packed stream ----
    mov dl, [si+KDL_O88V]
    call disk_mount_x
    jc .ret

    mov ax, [kd_floor]                  ; STG: the staging window, 64KB at the
    mov [cs:kxl_stg], ax                ; arena's floor (nothing is built there
                                        ; yet - the program this replaces is
                                        ; what we are abandoning)

    ; part 3 begins at file SECTOR KDL_L3OFF: skip that many sectors into the
    ; chain, which is (L3OFF / spc) whole clusters and (L3OFF mod spc) sectors
    ; into the one it lands in - dskw_read_at's own arithmetic (18.4.4.2).
    mov ax, [si+KDL_L3OFF]
    xor dx, dx
    mov bl, [dsk_spc]
    xor bh, bh
    or bx, bx
    jz .ret                             ; an unmounted volume: no cluster size
    div bx                              ; AX = clusters to skip, DL = csk
    mov cx, ax
    mov ax, [si+KDL_O88CL]
    jcxz .noskip
.skip:
    push dx                             ; dsk_next_clus keeps DX, but be plain
    call dsk_next_clus_x
    pop dx
    jc .ret
    loop .skip
.noskip:
    mov [dsk_csk], dl                   ; sectors into the landed cluster
    push ax                             ; the landed cluster

    mov ax, [si+KDL_L3ZB]               ; the packed bytes, as whole sectors
    add ax, 511
    mov cl, 9
    shr ax, cl
    mov dx, ax                          ; DX = sectors to read

    ; the stream goes at the TOP of the 64KB window, so the expansion below
    ; writes from the base UP and never overtakes it (SPEC.md 20.13.7)
    mov ax, dx
    mov cl, 5
    shl ax, cl                          ; sectors -> paragraphs of stream
    mov bx, 0x1000
    sub bx, ax                          ; paragraphs from STG to the stream
    add bx, [cs:kxl_stg]
    mov [cs:kxl_strm], bx
    mov es, bx
    xor bx, bx                          ; ES:BX = the stream's place
    pop ax                              ; the landed cluster
    call dsk_read_chain_x               ; DX sectors of it
    jc .ret

    ; --- expand it to the window's base -------------------------------------
    mov si, [kd_lbp]                    ; the packed length, out of the block
    mov cx, [si+KDL_L3ZB]               ; (DS is still KD_SEG here)
    mov ds, [cs:kxl_strm]               ; DS:SI = the stream
    xor si, si
    mov es, [cs:kxl_stg]                ; ES = the window's base
    call kxl_dec                        ; DI = the unpacked length (U)
    mov [cs:kxl_u], di
    push cs
    pop ds                              ; DS = KD_SEG again

    ; --- stage the launch block, LOW and without LOADFIX --------------------
    ; a verbatim copy of ours, the two bits flipped: the low image ignores
    ; KDLF_LOWDOS (it was assembled low) and the park fields carry the way
    ; home. The block lives one 64KB segment above the window.
    mov ax, [cs:kxl_stg]
    add ax, 0x1000
    mov [cs:kxl_blk], ax
    mov es, ax
    mov si, [kd_lbp]
    xor di, di
    mov cx, KDL_SIZE / 2
    rep movsw
    or word [es:KDL_FLAGS], KDLF_LOWDOS
    and word [es:KDL_FLAGS], ~KDLF_LOADFIX & 0xFFFF

    ; --- the point of no return: copy down and jump, touching no stack ------
    ; STG in BX, U in BP, the block one segment above STG. The low image's own
    ; header says where its launch block goes, read from the copy at 0060h.
    mov bx, [cs:kxl_stg]
    mov bp, [cs:kxl_u]
    cli
    cld
    mov ds, bx                          ; the window's base...
    mov ax, KD_LSEG
    mov es, ax                          ; ...to 0060h
    xor si, si
    xor di, di
    mov cx, bp
    rep movsb                           ; 1. the low image (its CORE_ORG hole
                                        ;    arrives as the zeros it ships)
    mov ax, cs
    mov ds, ax
    mov si, CORE_ORG
    mov di, CORE_ORG
    mov cx, CORE_MAX
    rep movsb                           ; 2. the core, into that hole, from our
                                        ;    own image - the same core, built
                                        ;    once and joined to both (96.44.5)
    mov ax, bx
    add ax, 0x1000
    mov ds, ax                          ; 3. the launch block, where the low
    mov di, [es:KD_H_LBP]               ;    image's header points
    mov cx, [es:KD_H_LBSZ]
    shr cx, 1                           ;    its SIZE is in bytes; movsw wants
    xor si, si                          ;    words (the block is one sector)
    rep movsw
    jmp KD_LSEG:0                        ; ...and away, as the stub would
.ret:
    ret

; -----------------------------------------------------------------------------
; kxl_dec - expand ONE os88lz LZ4 stream in place (SPEC.md 20.13.7)
; in:  DS:SI = the stream (SI = 0), CX = its packed bytes, ES = the output
;      segment (DI starts 0); output below the stream in the window
; out: DI = the unpacked length. Clobbers AX, BX, CX, SI, DI, DS, BP, flags
;
; The FOURTH reader of the format kern_dos needs and cannot otherwise reach:
; its kdshim.inc stubs lz_decomp, which is right for a machine whose files are
; bytes on a disk - but part 3 is an lz4 stream and this is the one path that
; must expand one. Pulling the whole of kernel/lz.inc (LZB, the 32-bit frame,
; the segment-cross arithmetic) into the HMA kilobyte would be larger and
; would collide with the constants kdshim.inc mirrors; this is LZ4-only, one
; segment of output (part 3 is 36 KB), and models kds_expand (hiber.inc).
; THE BOUNDS ARE READ THROUGH `cs:`: DS is the stream, then the output, so the
; two words naming the tail cannot live in it.
; -----------------------------------------------------------------------------
kxl_dec:
    xor di, di
    add cx, si                          ; the stream's end (SI = 0)
    mov ax, [si]                        ; the T word...
    add si, 2
    sub cx, ax                          ; ...and so where the raw tail begins
    mov [cs:kxl_tail], cx
    mov [cs:kxl_tlen], ax
.seq:
    cmp si, [cs:kxl_tail]
    jae .tail
    mov al, [si]                        ; the token
    inc si
    mov bl, al                          ; keep it
    mov cl, 4
    shr al, cl                          ; literal length, high nibble
    xor ah, ah
    mov cx, ax
    cmp al, 15
    jne .lits
.lx:
    mov al, [si]
    inc si
    xor ah, ah
    add cx, ax
    cmp al, 255
    je .lx
.lits:
    rep movsb                           ; the literals, DS:SI -> ES:DI
    cmp si, [cs:kxl_tail]
    jae .tail
    mov ax, [si]                        ; the match offset
    add si, 2
    mov bp, ax
    mov al, bl
    and al, 0x0F                        ; match length - 4, low nibble
    xor ah, ah
    mov cx, ax
    cmp al, 15
    jne .mat
.mx:
    mov al, [si]
    inc si
    xor ah, ah
    add cx, ax
    cmp al, 255
    je .mx
.mat:
    add cx, 4
    push ds                             ; a match reads what we have written
    push si
    mov si, di
    sub si, bp
    push es
    pop ds                              ; DS = ES, the output
    rep movsb
    pop si
    pop ds
    jmp short .seq
.tail:
    mov cx, [cs:kxl_tlen]               ; the raw tail, DS:SI standing on it
    rep movsb
    ret

kxp_msg:    db 'Packed file is corrupt'
%if $ - kxp_msg != KXP_MSGLEN
 %error "kxp_msg is EXEPACK's message, KXP_MSGLEN bytes"
%endif

section .bss
kxp_l:      resw 1                  ; the load segment
kxp_c:      resw 1                  ; the block's paragraph, from it
kxp_pp:     resw 1                  ; the packed image's paragraphs
kxp_skip:   resw 1
kxp_rip:    resw 1                  ; the header's real entry...
kxp_rcs:    resw 1
kxp_rsp:    resw 1
kxp_rss:    resw 1
kxp_dlen:   resw 1                  ; ...the unpacked paragraphs...
kxp_exsz:   resw 1                  ; ...and the block's bytes
kxp_tbl:    resw 1                  ; the table's offset in the block
kxp_tlen:   resw 1                  ; ...its bytes
kxp_t:      resw 1                  ; ...and where it moves to
kxp_s:      resd 1                  ; the walk: read here...
kxp_d:      resd 1                  ; ...write here
kxp_len:    resw 1
kxp_cmd:    resb 1
kxp_go:     resb 1                  ; 0 = the dry walk
; --- kd_tolow / kxl_dec (SPEC.md 96.40.9.1) ---------------------------------
kxl_stg:    resw 1                  ; the staging window's base paragraph
kxl_strm:   resw 1                  ; ...where part 3's packed stream was read
kxl_blk:    resw 1                  ; ...and the staged launch block, one 64KB on
kxl_u:      resw 1                  ; the low image's unpacked length
kxl_tail:   resw 1                  ; kxl_dec: where the raw tail begins...
kxl_tlen:   resw 1                  ; ...and how long it is
section .text
