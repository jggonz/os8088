; =============================================================================
; os8088 - apps/pagedesk/pduslot.inc  THE SLOT, ITS CACHE, THE REGIONS AND THE
;                                     LADDER (PAGEDESK-SPEC 20.22.2)
;
; %included by pagedesk.asm after pdstk.inc (pdabi.inc's names). pduslot.c
; holds what an owner edits (the occupant table, each one's workspace) and
; ui's stand-ins; the machinery is here, in assembly, because the primary is
; one 65,024-byte segment and the same logic in C was 2,800 bytes (20.22.2.9).
;
;   claim S   one occupant at its own base: PDXK PDXG PDXF PDXI PDXJ, or
;             PDWRT (id 6) with its buffers as the workspace. Read by offset
;             out of PAGEDESK.O88 (OP_SELF) and expanded onto S's base
;             (OP_COMP) - or copied back from the XMS/EMS cache.
;   claim X   PDENG's and PDREND's regions (PDE_NEEDMEM), regrown per grant.
;   the ladder pdu_ladck: a job slice's answer that is PDE_NEEDMOD or
;             PDE_NEEDMEM is turned into the load or the grant, and the job
;             calls the same verb again at its next slice. Only job slices
;             call it, so the disk is never under a click's chain (D22).
;
; cdecl as pdui.inc: BP DS SS:SP DF kept, AX..DI the caller's to lose; ES is
; put back where it is moved. C helpers called from here may lose AX..DI, so
; nothing lives in a register across one.
; =============================================================================

PDU_M_SLOT_A    equ 2               ; pdu.h's PDU_M_SLOT (pdu_vec[2])
PDU_OCC_WRT_A   equ 6               ; pdu.h's PDU_OCC_WRT
PDU_NOCC_A      equ 7
PDU_XFAIL       equ 0x80            ; PDV_REND_SLOT BX: "this id could not be had"
PDU_SWAPMAX     equ 6               ; loads a page view (20.22.2.6)
PDU_GRANTMAX    equ 4               ; region grants a job
PDU_LADMAX      equ 16              ; ladder answers a job
PDU_RESV_P      equ 2048            ; PD-29's 32 KiB beside a region, paragraphs

section .text

; --- telling the modules (20.22.2.4 steps 2 and 9) ---------------------------
; sl_tell - AX = the PDX_* id (| PDU_XFAIL: it could not be had), DX = the
; door's segment or 0. PDENG hears a load or an empty slot, PDREND every one
sl_tell:
    push dx
    neg dx
    sbb cx, cx
    and cx, PDM_ENTRY
    push cx
    push ax
    test al, PDU_XFAIL
    jnz .r
    mov ax, PDV_ENG_SLOT
    push ax
    xor ax, ax                      ; PDU_M_ENG
    push ax
    call _pdu_mcall
    add sp, 4
.r:
    mov ax, PDV_REND_SLOT
    push ax
    mov ax, 1                       ; PDU_M_REND
    push ax
    call _pdu_mcall
    add sp, 10
    ret

; sl_empty - the slot holds nothing: unbound, told, no workspace handed down
sl_empty:
    xor ax, ax
    cmp [_pdu_socc], ax
    je .f
    mov [_pdu_socc], ax
    push ax
    mov al, PDU_M_SLOT_A
    push ax
    call _pdu_mbind
    add sp, 4
    xor ax, ax
    cwd
    call sl_tell
    xor ax, ax
.f:
    mov [_pdu_xinit + PDXB_INIT_SLOT_SEG], ax       ; D2: never a freed one
    mov [_pdu_xinit + PDXB_INIT_SLOT_KB], ax
    mov [_pdu_xinit + PDXB_INIT_WBUF_SEG], ax
    mov [_pdu_xinit + PDXB_INIT_WBUF_KB], ax
    ret

; void pdu_slot_free(void) - S given back (document close, after a save)
_pdu_slot_free:
    call sl_empty
    xor ax, ax
    xchg ax, [_pdu_sclaim]
    or ax, ax
    jz .r
    push ax
    call _os88_mem_free
    pop cx
.r:
    ret

; -----------------------------------------------------------------------------
; int pdu_slot_load(int occ) - occupant `occ` (1..6) into S (20.22.2.4): 0, or
; a PDE_* with S empty. S is given back and claimed afresh at image + bss +
; the workspace (20.22.2.3's want while PD-29's 32 KiB stays free beside it,
; else the most of at least min that fits) or what the expansion needs (R +
; the read), whichever is larger.
; -----------------------------------------------------------------------------
_pdu_slot_load:
    push bp
    mov bp, sp
    push si
    push di
    mov bx, [bp+4]
    cmp bx, [_pdu_socc]
    je .ok
    mov ax, PDE_NOSLOT
    cmp word [_pdu_socc], PDU_OCC_WRT_A ; a parked Replace holds PDWRT's
    je .out                         ; state: it leaves by pdu_wrt_out alone
    call _pdu_slot_free             ; S is sized afresh for this one
    mov bx, [bp+4]
    mov al, [_pdu_occrow + bx]
    call sl_row_at                  ; [sl_row] [sl_len] [sl_img]
    mov bx, [bp+4]
    mov dl, [_pdu_occws + bx]       ; want
    mov dh, [_pdu_occwmin + bx]     ; min
    cmp bl, PDU_OCC_WRT_A
    jne .w
    mov dl, [_pdu_wkb]              ; PDWRT: this job's buffers, exactly
    mov dh, dl
.w:
    mov [sl_wmin], dh
    xor dh, dh
    push dx
    call _os88_mem_largest_p
    pop dx
    sub ax, [sl_img]
    jnb .r0
    xor ax, ax
.r0:
    mov cl, 6
    shr ax, cl                      ; KiB beside the image
    mov bx, ax
    sub bx, PDU_RESV_P >> 6
    jb .cut
    cmp bx, dx                      ; want, and PD-29's 32 KiB still free
    jae .want
.cut:
    cmp ax, dx                      ; else the most that fits, up to want...
    jb .c1
.want:
    mov ax, dx
.c1:
    mov cl, [sl_wmin]               ; ...and never under min (the claim then
    xor ch, ch                      ; refuses: PDE_NOSLOT)
    cmp ax, cx
    jae .c2
    xchg ax, cx
.c2:
    call sl_wsset
    add ax, [sl_img]
    mov [sl_need], ax
    mov si, [pg_row]
    mov word [pg_cap], 0            ; the whole row
    call pdu_pgeo                   ; no disk: the cache may still serve
    jc .sz
    mov ax, [pg_dst]
    add ax, [pg_rdp]
    cmp ax, [sl_need]
    jbe .sz
    mov [sl_need], ax
.sz:
    push word [sl_need]
    call _os88_mem_claim_p
    pop cx
    or ax, ax
    jz .nos
    mov [_pdu_sclaim], ax
    inc word [_pdu_swaps]
    call sl_copen
    mov bx, [bp+4]
    mov ax, 1                       ; IN, when the cache holds it
    call sl_cio
%ifdef PDU_LADTEST
    jc .disk
    inc word [_pdu_tcache]
    jmp .bind
.disk:
%else
    jnc .bind
%endif
    mov bx, [bp+4]
    shl bx, 1
    mov dx, [_pdu_occmagic + bx]
    mov ax, [_pdu_sclaim]
    call sl_fetch                   ; THE DISK
    or ax, ax
    jnz .fail
%ifdef PDU_LADTEST
    inc word [_pdu_tdisk]
%endif
    mov bx, [bp+4]                  ; the cache's copy: pristine, before
    xor ax, ax                      ; its first verb (OUT)
    call sl_cio
.bind:
    mov bx, [bp+4]
    mov [_pdu_socc], bx
    call sl_bind
    or ax, ax
    jnz .fail
.ok:
    xor ax, ax
.out:
    pop di
    pop si
    pop bp
    ret
.fail:
    push ax
    call sl_empty
    pop ax
    jmp short .out
.nos:
    mov ax, PDE_NOSLOT
    jmp short .out

; sl_wsset - AX = the workspace KiB: [sl_wskb], [sl_ws] (paragraphs) = AX
; paragraphs out in AX
sl_wsset:
    mov [sl_wskb], ax
    mov cl, 6
    shl ax, cl
    mov [sl_ws], ax
    ret

; sl_row_at - AL = a parts row: [sl_row], [pg_row] (op_row's SI), [sl_len]
; its unpacked bytes, [sl_img] those in paragraphs
sl_row_at:
    xor ah, ah
    mov [sl_row], ax
    call op_row
    mov [pg_row], si
    mov ax, [si+OP_R_LEN]
    mov [sl_len], ax
    add ax, 15
    mov cl, 4
    shr ax, cl
    mov [sl_img], ax
    ret

; pdu_home_rd - AX = a lazy row, DX = the claim, CX = its cap (0 = all): the
; row read out of the package from the LAUNCH folder (3.1's bracket - the
; folder stood in comes back after) with pdu_part_rd. AX = 0 or a PDE_*
pdu_home_rd:
    push cx
    push dx
    push ax                         ; pdu_part_rd(row, claim, cap)
    call OSAPI_FILE_HERE
    mov [rd_clus], dx
    mov [rd_vol], bl
    mov dx, [_pdu_home]
    mov bl, [_pdu_home+2]
    call OSAPI_FILE_GOTO_QM
    mov ax, PDE_IO
    jc .r                           ; nothing moved: no way back to take
    call _pdu_part_rd
    push ax
    mov dx, [rd_clus]
    mov bl, [rd_vol]
    call OSAPI_FILE_GOTO_QM
    pop ax
.r:
    add sp, 6
    ret

; sl_fetch - [sl_row]/[sl_len] = a lazy module row, AX = the segment it is
; expanded onto, DX = its magic. Read from the launch folder (3.1's bracket)
; and checked as 3.1 checks the eager modules: magic, PDM_ABI, image past the
; door, image + bss == the row's len. AX = 0 or a PDE_*
sl_fetch:
    mov [sl_dseg], ax
    mov [sl_mag], dx
    xchg dx, ax                     ; DX = the claim
    mov ax, [sl_row]
    xor cx, cx                      ; the whole row
    call pdu_home_rd
.chk:
    or ax, ax
    jnz .r
    push es
    mov es, [sl_dseg]
    mov dx, [sl_mag]
    mov ax, PDE_LIMIT
    cmp [es:0], dx
    jne .hb
    cmp word [es:2], PDM_ABI
    jne .hb
    mov dx, [es:4]
    cmp dx, PDM_ENTRY
    jbe .hb
    add dx, [es:6]
    cmp dx, [sl_len]
    jne .hb
    xor ax, ax
.hb:
    pop es
.r:
    ret

; sl_bind - [_pdu_socc] in S: its door bound at S's OWN base (the claim's
; code twin, 20.22.2.4), the workspace a data window after the image, INIT
; with pdxb_init complete, then PDENG and PDREND told. AX = 0 or the INIT's
; PDE_*. Also the re-INIT after a workspace grant (sl_grow)
sl_bind:
    push word [_pdu_sclaim]
    mov ax, PDU_M_SLOT_A
    push ax
    call _pdu_mbind
    add sp, 4
    xor ax, ax                      ; the workspace: a data window after it
    mov cx, [sl_ws]
    jcxz .nw
    push cx
    push word [sl_img]
    push word [_pdu_sclaim]
    call _os88_seg_sub
    add sp, 6
    or ax, ax
    mov dx, PDE_NOSLOT
    jz .e
.nw:
    mov dx, [sl_wskb]
    mov cx, PDV_X_INIT
    mov bx, PDXB_INIT_SLOT_SEG
    cmp word [_pdu_socc], PDU_OCC_WRT_A
    jne .ini
    mov cx, PDV_WRT_INIT
    mov bx, PDXB_INIT_WBUF_SEG
.ini:
    mov [_pdu_xinit + bx], ax
    mov [_pdu_xinit + bx + 2], dx
    push cx
    mov ax, PDU_M_SLOT_A
    push ax
    call _pdu_minit
    add sp, 4
    or ax, ax
    jnz .r
    mov ax, PDU_M_SLOT_A
    push ax
    call _pdu_mvecseg               ; the bound door: a code selector on
    pop cx                          ; kern_mega
    xchg ax, dx
    mov ax, [_pdu_socc]
    cmp al, PDU_OCC_WRT_A
    jne .t
    xor ax, ax                      ; PDWRT: neither module calls it
.t:
    call sl_tell
    xor ax, ax
.r:
    ret
.e:
    xchg ax, dx
    ret

; sl_grow - AX = the KiB an occupant's verb asked for its workspace
; (PDE_NEEDMEM, CX = PDMEM_SLOT): S regrown to at most the occupant's want,
; only while PD-29's 32 KiB stays free, and the occupant re-INITed either
; way - a refusal is the same slot_kb, and the occupant degrades (APPROX).
; AX = 0 or a PDE_*
sl_grow:
    mov bx, [_pdu_socc]
    or bx, bx
    jz .none
    cmp bl, PDU_OCC_WRT_A
    je .none
    mov dl, [_pdu_occws + bx]
    xor dh, dh
    cmp ax, dx
    jbe .k
    xchg ax, dx                     ; no more than its want
.k:
    cmp ax, [sl_wskb]
    jbe .ini                        ; no growth: the INIT says so
    push ax
    mov cl, 6
    shl ax, cl
    sub ax, [sl_ws]                 ; the paragraphs more
    add ax, PDU_RESV_P
    push ax
    call _os88_mem_largest_p
    pop cx
    pop dx                          ; the KiB
    cmp ax, cx
    jb .ini
    push dx
    xchg ax, dx
    mov cl, 6
    shl ax, cl
    add ax, [sl_img]
    push ax
    push word [_pdu_sclaim]
    call _os88_mem_regrow_p         ; may MOVE S: the image comes with it
    add sp, 4
    pop dx
    or ax, ax
    jz .ini
    mov [_pdu_sclaim], ax
    xchg ax, dx
    call sl_wsset
.ini:
    jmp sl_bind
.none:
    xor ax, ax
    ret

; --- the geometry of a load ---------------------------------------------------
; pdu_pgeo - SI = a parts row, [pg_cap] = the bytes of a STORED row wanted
; (0 = all of it; PDRES's INDEX prefix, pdumod.c). out CF=0, [pg_len] those
; bytes, and in PARAGRAPHS: [pg_off] the file
; offset aligned down to a cluster (32 bits, bytes), [pg_slp] the part's
; paragraphs into that cluster, [pg_dst] where the read lands in the claim
; (R = roundup512(len - packed) for a packed row, so the stream sits above
; its output - SPEC.md 20.13.7; 0 stored) and [pg_rdp] the paragraphs read
; (slack + the bytes on disk, up to a cluster). CF=1: no cluster size.
pdu_pgeo:
    call op_clbget                  ; AX = bytes a cluster, the home volume's
    jc .x
    mov cl, 4
    shr ax, cl
    mov [pg_clp], ax
    xchg ax, bx
    mov ax, [si+OP_R_OFF]
    xor dx, dx
    div bx
    mov [pg_slp], dx
    mov ax, [si+OP_R_OFF]
    sub ax, dx
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    mov [pg_off], ax
    mov [pg_off+2], dx
    mov ax, [pg_cap]
    or ax, ax
    jnz .cp
    mov ax, [si+OP_R_LEN]
.cp:
    mov [pg_len], ax
    xor bx, bx
    test byte [si+OP_R_FLAG], OP_COMP
    jz .st
    mov bx, ax
    sub bx, [si+OP_R_ZKB]
    add bx, 511
    and bx, ~511
    mov cl, 4
    shr bx, cl
    mov ax, [si+OP_R_ZKB]
.st:
    mov [pg_dst], bx
    add ax, 15
    mov cl, 4
    shr ax, cl
    add ax, [pg_slp]
    mov cx, [pg_clp]
    dec cx
    add ax, cx
    not cx
    and ax, cx
    mov [pg_rdp], ax
    clc
.x:
    ret

; unsigned pdu_part_need(int row, unsigned cap) - the paragraphs a read of
; `row` needs in its claim (R + the read), cap as [pg_cap]; 0 = no disk
_pdu_part_need:
    push bp
    mov bp, sp
    push si
    mov ax, [bp+6]                  ; the cap: the SECOND argument here
    call pg_args
    mov ax, 0
    jc .out
    mov ax, [pg_dst]
    add ax, [pg_rdp]
.out:
    pop si
    pop bp
    ret

; pg_args - [bp+4] = the row, AX = the cap: SI = the row, pdu_pgeo's
; answers
pg_args:
    mov [pg_cap], ax
    mov al, [bp+4]
    call op_row
    mov [pg_row], si
    jmp pdu_pgeo

; unsigned pdu_part_rd(int row, unsigned claim, unsigned cap) - read lazy row
; `row` out of the package file (op_name, in the folder we stand in) and
; leave it UNPACKED at claim:0 (a stored row: its first `cap` bytes, 0 =
; all): reads of at most 32 KiB, each into its own
; os88_seg_sub window (SPEC.md 18.4.4: offset and capacity whole clusters,
; and 32 KiB is a multiple of every one), then OSAPI_DECOMP from the window
; the stream sits in, or a stored row moved down by its slack. 0, PDE_IO or
; PDE_LIMIT (the stream refused)
_pdu_part_rd:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [bp+8]                  ; the cap: the THIRD argument here
    call pg_args
    jc .io
    mov word [pg_done], 0
.rd:
    mov ax, [pg_rdp]
    sub ax, [pg_done]
    jbe .got
    cmp ax, 2048
    jbe .c0
    mov ax, 2048
.c0:
    mov [pg_chp], ax
    push ax
    mov ax, [pg_dst]
    add ax, [pg_done]
    push ax
    push word [bp+6]
    call _os88_seg_sub
    add sp, 6
    or ax, ax
    jz .io
    mov es, ax
    xor bx, bx
    mov cx, [pg_chp]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; bytes, <= 32768
    mov si, op_name
    mov ax, [pg_off]
    mov dx, [pg_off+2]
    call OSAPI_FILE_READ_AT
    jc .io
    or ax, ax
    jz .io
    mov ax, [pg_chp]
    add [pg_done], ax
    mov cl, 4
    shl ax, cl
    add [pg_off], ax
    adc word [pg_off+2], 0
    jmp short .rd
.got:
    mov si, [pg_row]
    mov ax, [pg_len]
    test byte [si+OP_R_FLAG], OP_COMP
    jz .wl
    mov ax, [si+OP_R_ZKB]
.wl:
    add ax, 15
    mov cl, 4
    shr ax, cl
    push ax                         ; the window: the bytes on disk
    mov ax, [pg_dst]
    add ax, [pg_slp]                ; ...past the slack
    push ax
    push word [bp+6]
    call _os88_seg_sub
    add sp, 6
    or ax, ax
    jz .io
    mov si, [pg_row]
    mov es, [bp+6]
    xor di, di
    test byte [si+OP_R_FLAG], OP_COMP
    jz .stored
    mov cx, [si+OP_R_ZKB]
    mov dx, [si+OP_R_LEN]
    xor bx, bx
    push ax
    mov al, [op_table + OP_T_FMT]   ; the format, read before DS moves
    xor ah, ah
    pop si
    push ds
    mov ds, si
    xor si, si
    call OSAPI_DECOMP
    pop ds
    jc .bad
    jmp short .ok
.stored:
    cmp word [pg_slp], 0
    je .ok
    mov cx, [pg_len]
    push ds
    mov ds, ax
    xor si, si
    cld
    rep movsb                       ; down onto the base: the writer is below
    pop ds
.ok:
    xor ax, ax
.out:
    pop es
    pop di
    pop si
    pop bp
    ret
.io:
    mov ax, PDE_IO
    jmp short .out
.bad:
    mov ax, PDE_LIMIT
    jmp short .out

; --- the cache (20.22.2.7) ----------------------------------------------------
; sl_copen - the store, once an instance, at its first load: one XMS block
; (CAPS answers the total + 64 KiB), else one EMS handle on a real-mode
; kernel, else none for the session. Off (ui's pdp_xcache) gives it back.
; Lays out ck_at: each occupant's place in KiB (XMS) or 16 KiB pages (EMS)
sl_copen:
    cmp byte [_pdp_cfg + 10], 0     ; ui's pdu_pref_modcache(): 1 = Off
    jne _pdu_cache_drop
    cmp word [_pdu_ckind], 0
    jne .r
    mov word [_pdu_ckind], -1
    mov cl, 10
    mov bx, 1023
    call .lay
    push ax
    call OSAPI_XMEM_CAPS            ; AX = free KB (0 on an 8088, kern_small)
    pop dx
    sub ax, 64
    jb .ems
    cmp ax, dx
    jb .ems
    xchg ax, dx
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    call OSAPI_XMEM_ALLOC
    jc .ems
    mov [_pdu_ctok], ax
    mov [_pdu_ctok+2], dx
    mov word [_pdu_ckind], 1
    ret
.ems:
    test byte [cc_cpuf], CPU_F_PM   ; the frame is no segment of ours there
    jnz .r
    mov cl, 14
    mov bx, 16383
    call .lay
    push ax
    mov bx, (DRVC_EMS << 8) | EMSV_CAPS
    call OSAPI_DRV_CALL             ; AX = pages free
    pop cx
    jc .r
    cmp ax, cx
    jb .r
    xchg ax, cx
    mov bx, (DRVC_EMS << 8) | EMSV_ALLOC
    call OSAPI_DRV_CALL
    jc .r
    mov [_pdu_ctok], ax
    mov word [_pdu_ckind], 2
.r:
    ret
.lay:                               ; CL = the unit's shift, BX = unit - 1
    mov [sl_sh], cl
    mov [sl_msk], bx
    mov si, 1
    xor di, di
.l:
    mov bx, si
    shl bx, 1
    mov [ck_at + bx], di
    mov al, [_pdu_occrow + si]
    push ax
    call _pdu_part_len
    pop cx
    add ax, [sl_msk]                ; up to the unit, in 17 bits
    rcr ax, 1
    mov cl, [sl_sh]
    dec cl
    shr ax, cl
    add di, ax
    inc si
    cmp si, PDU_NOCC_A
    jb .l
    xchg ax, di
    ret

; void pdu_cache_drop(void) - the store given back; the next load asks again
_pdu_cache_drop:
    mov ax, [_pdu_ckind]
    cmp ax, 1
    jne .e
    mov ax, [_pdu_ctok]
    mov dx, [_pdu_ctok+2]
    call OSAPI_XMEM_FREE
    jmp short .z
.e:
    cmp ax, 2
    jne .z
    mov ax, [_pdu_ctok]
    mov bx, (DRVC_EMS << 8) | EMSV_FREE
    call OSAPI_DRV_CALL
.z:
    xor ax, ax
    mov [_pdu_ckind], ax
    mov [sl_cin], al
    ret

; sl_cio - BX = the occupant, AX = 0 OUT of S into the cache, 1 IN from it.
; CF=0 done; CF=1 not (no store, not held, the frame busy). A copy that
; fails gives the store back for the session: the disk, always, after that
sl_cio:
    mov cx, bx
    mov dl, 1
    shl dl, cl
    cmp word [_pdu_ckind], 0
    jle .miss
    or ax, ax
    jz .go
    test [sl_cin], dl
    jz .miss
.go:
    mov [sl_cbit], dl
    mov [sl_dir], ax
    shl bx, 1
    mov dx, [ck_at + bx]            ; its place
    mov cx, [sl_len]
    inc cx
    and cl, 0FEh                    ; an even length (XMEM_COPY's rule)
    cmp word [_pdu_ckind], 2
    jne .x
    push cx
    push dx
    mov bx, (DRVC_EMS << 8) | EMSV_FRAME
    mov al, 1                       ; quarter 0, for this copy only
    call OSAPI_DRV_CALL
    jc .busy
    mov [_pdu_cframe], dx
    pop dx
    pop cx
    call .do
    pushf
    mov bx, (DRVC_EMS << 8) | EMSV_UNFRAME
    mov al, 1
    call OSAPI_DRV_CALL
    popf
    jmp short .af
.x:
    call .do
.af:
    jc .drop
    cmp word [sl_dir], 0
    jne .r
    mov al, [sl_cbit]
    or [sl_cin], al
.r:
    clc
    ret
.drop:
    call _pdu_cache_drop
    mov word [_pdu_ckind], -1
.miss:
    stc
    ret
.busy:
    pop dx
    pop cx
    stc
    ret
.do:                                ; CX = bytes, DX = the place: CF = failed
    push cx
    push dx
    push word [_pdu_sclaim]
    push word [sl_dir]
    call _pdu_cx
    add sp, 8
    neg ax
    ret

; int pdu_cx(int dir, unsigned seg, unsigned at, unsigned bytes) - copy the
; first `bytes` (even) of seg:0 OUT to the store (dir 0) or IN (dir 1), the
; occupant being `at` KiB into the XMS block or `at` pages into the EMS
; handle (EMS: quarter 0 framed at pdu_cframe). 0 done, 1 refused
_pdu_cx:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov word [cx_done], 0
.lp:
    mov cx, [bp+10]
    sub cx, [cx_done]
    jbe .ok
    cmp word [_pdu_ckind], 2
    je .ems
    cmp cx, 32768
    jbe .x0
    mov cx, 32768
.x0:
    mov [cx_n], cx
    mov ax, [bp+8]
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    add ax, [cx_done]
    adc dx, 0
    add ax, [_pdu_ctok]
    adc dx, [_pdu_ctok+2]
    mov es, [bp+6]
    mov si, [cx_done]
    mov cx, [cx_n]
    mov di, [bp+4]
    call OSAPI_XMEM_COPY
    jc .no
    jmp short .nx
.ems:
    cmp cx, 16384
    jbe .e0
    mov cx, 16384
.e0:
    mov [cx_n], cx
    mov ax, [cx_done]
    mov cl, 14
    shr ax, cl                      ; the page: done / 16384
    add ax, [bp+8]
    xchg ax, cx
    mov dx, [_pdu_ctok]
    xor al, al
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
    jc .no
    mov cx, [cx_n]
    mov ax, [_pdu_cframe]
    mov dx, [bp+6]
    mov di, [cx_done]
    xor si, si
    push ds
    cmp word [bp+4], 0
    jne .in
    xchg si, di                     ; OUT: seg:done -> frame:0
    xchg ax, dx
.in:
    mov es, dx
    mov ds, ax
    cld
    shr cx, 1
    rep movsw
    pop ds
.nx:
    mov ax, [cx_n]
    add [cx_done], ax
    jmp .lp
.ok:
    xor ax, ax
    jmp short .out
.no:
    mov ax, 1
.out:
    pop es
    pop di
    pop si
    pop bp
    ret

; --- the regions, claim X (20.22.2.5) ---------------------------------------
PDU_RGLYPH      equ 5               ; PDMEM_GLYPH: fonts' glyph growth
                                    ; (20.22.8), pdabi.h's to pin; 5 here
PDU_NREG        equ 5               ; XIDX OSW XC X8 GLYPH
PDU_XIDXMAX     equ 256             ; KiB: eng's flat index (20.22.3)
PDU_GLYPHMIN    equ 32              ; KiB: the glyph region's second try
PDU_PART_X8_A   equ 10              ; pagedesk.asm's PDX8 row
PDX8_MAGIC_A    equ 0x3850          ; 'P8' (blend, 20.22.10.11)

; xr_segs - every region's window and KiB into pdu_xinit, 0 where not held. A
; window is a SEG_SUBC pair (PDX8 is code in its region) and reaches 64 KiB
; at most (XIDX may hold 256: eng's own addressing past that). A held GLYPH
; region stands in for claim A's glyph row; not held, the row is handed again
xr_segs:
    push si
    push di
    mov si, 2
    mov di, _pdu_xinit + PDXB_INIT_XIDX_SEG
.l:
    xor ax, ax
    cwd
    mov cx, [xr_len + si]
    cmp si, PDU_RGLYPH * 2
    jne .f
    mov di, _pdu_xinit + PDXB_INIT_GLYPH_SEG
    jcxz .ga
.f:
    jcxz .st
    push cx
    mov dx, [xr_claim]
    mov ax, [xr_off + si]
    cmp cx, 4096
    jbe .w
    mov cx, 4096
.w:
    mov bl, SEG_SUBC
    call OSAPI_SEG
    xchg ax, dx
    jnc .ok
    xor ax, ax
.ok:
    pop dx
    mov cl, 6
    shr dx, cl
.st:
    mov [di], ax
    mov [di+2], dx
    add di, 4
    inc si
    inc si
    cmp si, (PDU_NREG + 1) * 2
    jb .l
    pop di
    pop si
    ret
.ga:
    mov ax, [_pdu_dc + 6]           ; claim A's glyph row (PDU_DC_GLYPH 3)
    mov dx, [_pdu_dckb + 6]
    jmp short .st

; x8_fix - PDX8 in the X8 region: its CODE selector at +10 and PDENG's door
; at +12/+14 (blend's 20.22.10.11), after every load and every move of X
x8_fix:
    cmp byte [x8_in], 0
    je .r
    push es
    mov ax, [_pdu_xinit + PDXB_INIT_X8_SEG]
    mov es, ax
    OS88_CODESEL ax, byte [cc_cpuf]
    mov [es:10], ax
    mov word [es:12], PDM_ENTRY
    mov ax, [pdu_vec + 2]           ; PDENG's bound door (a code selector)
    mov [es:14], ax
    pop es
.r:
    ret

; unsigned pdu_region(unsigned r, unsigned kb) - grant PDMEM_* region r of kb
; KiB (1..64; XIDX to 256), appended to X, or grow the LAST one; refuse
; anything else. GLYPH asked past 32 and refused tries 32. X8's start is
; PDX8 (read here at its grant). Then PDENG and PDREND are re-INITed with
; every region (and the occupant, when the glyph changed): 0, or a PDE_*
_pdu_region:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    dec si
    cmp si, PDU_NREG - 1
    ja .init
    mov ax, [bp+6]
    dec ax
    cmp ax, PDU_XIDXMAX - 1
    ja .init
    or si, si                       ; only XIDX passes 64 KiB
    jz .k
    cmp ax, 63
    ja .init
.k:
    cmp word [sl_grants], PDU_GRANTMAX
    jae .init
    inc word [sl_grants]
    inc ax
    inc si
    shl si, 1
.try:
    mov [xr_kb], ax
    mov cl, 6
    shl ax, cl                      ; paragraphs
    mov dx, [xr_len + si]
    or dx, dx
    jz .new
    cmp ax, dx                      ; held: it grows, and only as the last
    jbe .init
    mov bx, [bp+4]
    cmp bx, [xr_last]
    jne .init
    sub ax, dx
.new:
    mov [sl_need], ax
    add ax, PDU_RESV_P
    push ax
    call _os88_mem_largest_p
    pop cx
    cmp ax, cx
    jb .no
    mov ax, [xr_para]
    add ax, [sl_need]
    push ax
    mov ax, [xr_claim]
    or ax, ax
    jz .cl
    push ax
    call _os88_mem_regrow_p         ; may MOVE X: its bytes come with it
    pop cx
    jmp short .c1
.cl:
    call _os88_mem_claim_p
.c1:
    pop cx
    or ax, ax
    jz .no
    mov [xr_claim], ax
    mov ax, [sl_need]
    cmp word [xr_len + si], 0
    jne .g
    mov dx, [xr_para]
    mov [xr_off + si], dx
    mov dx, [bp+4]
    mov [xr_last], dx
    cmp dl, PDMEM_X8                ; a new X8: PDX8 at its start
    jne .g
    add [xr_len + si], ax
    add [xr_para], ax
    call x8_load
    jmp short .init
.g:
    add [xr_len + si], ax
    add [xr_para], ax
    jmp short .init
.no:
    cmp si, PDU_RGLYPH * 2         ; the glyph's second try: 32 KiB
    jne .init
    mov ax, PDU_GLYPHMIN
    cmp [xr_kb], ax
    ja .try
.init:
    call xr_segs
    call x8_fix
    mov ax, PDV_ENG_INIT
    push ax
    xor ax, ax
    push ax
    call _pdu_minit
    add sp, 4
    or ax, ax
    jnz .out
    mov ax, PDV_REND_INIT
    push ax
    mov ax, 1
    push ax
    call _pdu_minit
    add sp, 4
    or ax, ax
    jnz .out
    cmp word [bp+4], PDU_RGLYPH    ; PDXF reads the glyph claim itself
    jne .out                        ; (20.22.8): its INIT hands it again
    cmp word [_pdu_socc], 0
    je .out
    call sl_bind
.out:
    pop si
    pop bp
    ret

; x8_load - the X8 region just granted (the last in X): PDX8 read into its
; start with the slot's reader. A row that does not fit the region, or a
; read that fails, takes the region back: PDREND then sees x8_seg 0
x8_load:
    push word [sl_row]              ; the occupant's row: sl_bind reads it
    push word [sl_len]
    push word [sl_img]
    call xr_segs
    call .ld
    pop word [sl_img]
    pop word [sl_len]
    pop word [sl_row]
    ret
.ld:
    mov al, PDU_PART_X8_A
    call sl_row_at
    mov si, [pg_row]
    mov word [pg_cap], 0
    call pdu_pgeo
    jc .back
    mov ax, [pg_dst]
    add ax, [pg_rdp]
    cmp ax, [xr_len + PDMEM_X8 * 2]
    ja .back
    mov ax, [_pdu_xinit + PDXB_INIT_X8_SEG]
    or ax, ax
    jz .back
    mov dx, PDX8_MAGIC_A
    call sl_fetch
    or ax, ax
    jnz .back
    mov byte [x8_in], 1
    ret
.back:
    xor ax, ax
    xchg ax, [xr_len + PDMEM_X8 * 2]
    sub [xr_para], ax
    ret

; void pdu_region_free(void) - X given back, every region field 0
_pdu_region_free:
    xor ax, ax
    mov [x8_in], al
    xchg ax, [xr_claim]
    or ax, ax
    jz .z
    push ax
    call _os88_mem_free
    pop cx
.z:
    xor ax, ax
    mov [xr_para], ax
    mov [xr_last], ax
    mov bx, PDU_NREG * 2
.l:
    mov [xr_len + bx], ax
    dec bx
    dec bx
    jnz .l
    jmp xr_segs

; --- the ladder (20.22.2.6) -----------------------------------------------------
; void pdu_lad_begin(void) - a job begins: its counts start again
_pdu_lad_begin:
    xor ax, ax
    mov [_pdu_swaps], ax
    mov [sl_ladn], ax
    mov [sl_grants], ax
    ret

; unsigned pdu_ladck(int mod, unsigned r) - r is what module `mod`'s verb
; answered. A ladder status (CF = 0, PDE_NEEDMOD or PDE_NEEDMEM): do the load
; or the grant and answer 0xFFFF - the job calls the same verb at its next
; slice. Anything else: r. A ladder that cannot go on: the PDE_* with pdu_mcf
; set. NEEDMEM's CX = PDMEM_SLOT grows the occupant's workspace (sl_grow);
; any other is a region (pdu_region). NEEDMOD: PDENG's one module is PDXK
; (an encrypted open's key goes through pdovlplat.c instead, 20.22.2.6);
; PDREND's are GFX..JPX, and one that cannot be had is told 0x80 | id
_pdu_ladck:
    push bp
    mov bp, sp
    mov ax, [bp+6]
    cmp word [_pdu_mcf], 0
    jne .out
    cmp ax, PDE_NEEDMOD
    je .lad
    cmp ax, PDE_NEEDMEM
    jne .out
.lad:
    inc word [sl_ladn]
    cmp word [sl_ladn], PDU_LADMAX
    ja .lim
    mov bx, [_pdu_mdx]
    cmp ax, PDE_NEEDMEM
    jne .mod
    mov cx, [_pdu_mcx]
    jcxz .slot                      ; PDMEM_SLOT: the occupant's workspace
    push bx
    push cx
    call _pdu_region
    add sp, 4
    jmp short .res
.slot:
    xchg ax, bx
    call sl_grow
    jmp short .res
.mod:
    cmp word [bp+4], 0              ; PDU_M_ENG
    jne .rend
    cmp bx, PDX_KEY
    jne .lim
    push bx
    call _pdu_slot_load
    pop cx
.res:
    or ax, ax
    jnz .fail
.again:
    mov ax, 0xFFFF
.out:
    pop bp
    ret
.rend:
    cmp bx, PDX_GFX
    jb .no
    cmp bx, PDX_JPX
    ja .no
    cmp word [_pdu_swaps], PDU_SWAPMAX
    jae .no
    push bx
    call _pdu_slot_load
    pop cx
    or ax, ax
    jz .again
.no:
    mov ax, [_pdu_mdx]
    or al, PDU_XFAIL
    xor dx, dx
    call sl_tell
    jmp short .again
.lim:
    mov ax, PDE_LIMIT
.fail:
    mov word [_pdu_mcf], 1
    jmp short .out

; =============================================================================
; THE DOCUMENT SET (20.6.2, D17, D23) - pdumod.c's carve, in assembly for the
; same reason as the rest of this file (20.22.2.9: 982 bytes in C)
; =============================================================================
PDU_DC_BAND_A   equ 5
PDU_DC_DSTK_A   equ 11
PDU_NDC_A       equ 12
PDU_DC_SPLIT_A  equ 4               ; rows 0..3 in claim A, 4.. in B
PDU_FL_PARA_A   equ 2752            ; one Flate slot, 43 KiB
PDU_FL_RESV_A   equ 5120            ; PD-29's 32 KiB + a save's peak

; int pdu_doc_claim(void) - the document set: 0, or PDE_NOMEM with
; pdu_needkb set, or PDE_LIMIT when a module refused its INIT
_pdu_doc_claim:
    push bp
    mov bp, sp
    sub sp, 8                       ; -2 row*2  -4 at  -6 fl  -8 claim
    call _pdu_doc_free
    mov ax, [_pdu_scrw]             ; the band: 16 rows of the screen
    cmp word [_pdu_bpp], 4
    jb .mono
    inc ax
    shr ax, 1
    jmp short .bb
.mono:
    add ax, 7
    mov cl, 3
    shr ax, cl
.bb:
    mov cl, 4
    shl ax, cl
    mov [dc_bandb], ax
    xor bx, bx
    cwd                             ; DX = the KiB the set asks for
.para:
    mov ax, [_pdu_dckb + bx]
    cmp bl, PDU_DC_BAND_A * 2
    jne .kb
    mov ax, [dc_bandb]              ; the band to a whole KiB
    add ax, 1023
    mov cl, 10
    shr ax, cl
.kb:
    add dx, ax
    mov cl, 6
    shl ax, cl
    mov [dc_para + bx], ax
    inc bx
    inc bx
    cmp bl, PDU_NDC_A * 2
    jb .para
    mov [_pdu_needkb], dx
    xor ax, ax
    mov [bp-2], ax
.row:
    mov bx, [bp-2]
    or bx, bx
    jz .claim
    cmp bl, PDU_DC_SPLIT_A * 2
    jne .carve
.claim:
    mov dx, PDU_DC_SPLIT_A * 2      ; this claim's rows end here
    or bx, bx
    jz .sum0
    mov dl, PDU_NDC_A * 2
.sum0:
    xor ax, ax
.sum:
    add ax, [dc_para + bx]
    inc bx
    inc bx
    cmp bx, dx
    jb .sum
    xor cx, cx                      ; fl
    cmp word [bp-2], cx
    je .take
    push ax                         ; B: the Flate slots ride its tail
    call _os88_mem_largest_p
    pop dx
    xor cx, cx
    cmp byte [_pdp_cfg + 9], cl     ; prefs: Flate cache Off
    jne .fl
    mov bx, dx
    add bx, PDU_FL_PARA_A + PDU_FL_RESV_A
    cmp ax, bx
    jb .fl
    inc cx
    add bx, PDU_FL_PARA_A
    cmp ax, bx
    jb .fl
    inc cx
.fl:
    mov [bp-6], cx
    xchg ax, dx
    jcxz .take
.fls:
    add ax, PDU_FL_PARA_A
    loop .fls
.take:
    push ax
    call _os88_mem_claim_p
    pop cx
    or ax, ax
    jz .nomem
    mov bx, [bp-2]                  ; 0 or 8: pdu_dclaim[0] or [1]
    shr bx, 1
    shr bx, 1
    mov [_pdu_dclaim + bx], ax
    mov [bp-8], ax
    mov word [bp-4], 0
.carve:
    mov bx, [bp-2]
    mov ax, [bp-8]
    mov dx, [bp-4]
    or dx, dx                       ; the first row is the claim's own base
    jz .set
    push word [dc_para + bx]
    push dx
    push ax
    call _os88_seg_sub
    add sp, 6
    or ax, ax
    jz .nomem
    mov bx, [bp-2]
.set:
    mov [_pdu_dc + bx], ax
    mov ax, [dc_para + bx]
    add [bp-4], ax
    inc bx
    inc bx
    mov [bp-2], bx
    cmp bl, PDU_NDC_A * 2
    jb .row
    ; PDENG finds slot k at flate_seg + k * 2752 paragraphs: true on every
    ; real-mode kernel; where the second slot's selector is not that, one
    cmp word [bp-6], 0
    je .fdone
    mov ax, [bp-4]
    call dc_flsub
    mov [_pdu_xinit + PDXB_INIT_FLATE_SEG], ax
    cmp word [bp-6], 2
    jne .f1
    mov ax, [bp-4]
    add ax, PDU_FL_PARA_A
    call dc_flsub
    mov dx, [_pdu_xinit + PDXB_INIT_FLATE_SEG]
    add dx, PDU_FL_PARA_A
    cmp ax, dx
    je .f1
    mov word [bp-6], 1
.f1:
    cmp word [_pdu_xinit + PDXB_INIT_FLATE_SEG], 0
    jne .fdone
    mov word [bp-6], 0
.fdone:
    mov ax, [bp-6]
    mov [_pdu_flslots], ax
    mov cl, 43
    mul cl
    mov [_pdu_xinit + PDXB_INIT_FLATE_KB], ax
    or ax, ax
    jz .nf
    or byte [_pdu_xinit + PDXB_INIT_FLAGS], PDXI_FLATE
.nf:
    ; D23: arm the private module stack - 0xCC everywhere, so its high-water
    ; mark can be read off it, and pdu_mcall's canary at its foot
    push es
    mov ax, [_pdu_dc + PDU_DC_DSTK_A * 2]
    mov [_pdu_dstkseg], ax
    mov es, ax
    xor di, di
    mov ax, PD_DSTK_CAN0
    stosw
    mov ax, PD_DSTK_CAN1
    stosw
    mov ax, (PD_DSTK_FILL << 8) | PD_DSTK_FILL
    mov cx, PD_DSTK_BYTES / 2 - 2
    rep stosw
    pop es
    ; the regions pdxb_init names, by row: seg then KiB
    mov si, dc_map
    mov di, _pdu_xinit + PDXB_INIT_CACHE_SEG
.map:
    lodsb
    cbw
    xchg bx, ax
    mov ax, [_pdu_dc + bx]
    mov [di], ax
    mov ax, [_pdu_dckb + bx]
    mov [di+2], ax
    add di, 4
    cmp si, dc_map + 6
    jb .map
    mov ax, [dc_bandb]              ; the claim is a whole KiB
    mov [_pdu_xinit + PDXB_INIT_BAND_BYTES], ax
    mov ax, [_pdu_bank]
    mov [_pdu_xinit + PDXB_INIT_BANK_SEG], ax
    or ax, ax
    jz .bk
    mov ax, [_pdu_bankkb]
.bk:
    mov [_pdu_xinit + PDXB_INIT_BANK_KB], ax
    xor ax, ax
    mov bx, PDV_ENG_INIT
    call dc_init
    jnz .limit
    mov ax, 1
    mov bx, PDV_REND_INIT
    call dc_init
    jnz .limit
    cmp word [_pdu_bank], 0
    je .ok
    mov ax, PDXB_FONTBANK_BYTES
    push ax
    xor ax, ax
    push ax
    push word [_pdu_xb]
    mov ax, _pdu_xfb
    push ax
    call _pd_fcopy_out
    add sp, 8
    xor ax, ax
    push ax
    push ax
    push ax
    mov al, PDV_REND_FONTBANK
    push ax
    mov al, 1                       ; PDU_M_REND
    push ax
    call _pdu_mcall
    add sp, 10
.ok:
    xor ax, ax
    jmp short .out
.limit:
    mov ax, PDE_LIMIT
    jmp short .fail
.nomem:
    mov ax, PDE_NOMEM
.fail:
    push ax
    call _pdu_doc_free
    pop ax
.out:
    add sp, 8
    pop bp
    ret
;
; void pdu_doc_free(void) - the document set given back (and S and X with it)
_pdu_doc_free:
    mov bx, 2
.c:
    xor ax, ax
    xchg ax, [_pdu_dclaim + bx]
    or ax, ax
    jz .n
    push bx
    push ax
    call _os88_mem_free
    pop ax
    pop bx
.n:
    dec bx
    dec bx
    jns .c
    push es
    push ds
    pop es
    mov di, _pdu_dc
    mov cx, PDU_NDC_A
    xor ax, ax
    rep stosw
    pop es
    mov [_pdu_dstkseg], ax          ; D23: verbs run flat again
    mov [_pdu_stkbad], ax
    mov [_pdu_stktold], ax
    mov [_pdu_xinit + PDXB_INIT_CACHE_SEG], ax
    mov [_pdu_xinit + PDXB_INIT_ARENA_SEG], ax
    mov [_pdu_xinit + PDXB_INIT_TABLES_SEG], ax
    mov [_pdu_xinit + PDXB_INIT_DL_SEG], ax
    mov [_pdu_xinit + PDXB_INIT_BAND_SEG], ax
    mov [_pdu_xinit + PDXB_INIT_GLYPH_SEG], ax
    and byte [_pdu_xinit + PDXB_INIT_FLAGS], ~PDXI_FLATE & 0xFF
    mov [_pdu_xinit + PDXB_INIT_FLATE_SEG], ax
    mov [_pdu_xinit + PDXB_INIT_FLATE_KB], ax
    mov [_pdu_flslots], ax
    call _pdu_slot_free
    jmp _pdu_region_free
;
; int pdu_minit(int m, unsigned verb) - module m INITed with pdu_xinit: both
; doors (PDREND's for the occupants) and the block filled in first
_pdu_minit:
    mov ax, [_pdu_xb]
    mov [_pdu_xinit + PDXB_INIT_XB_SEG], ax
    mov ax, PDM_ENTRY
    mov [_pdu_xinit + PDXB_INIT_ENG_OFF], ax
    mov [_pdu_xinit + PDXB_INIT_REND_OFF], ax
    mov ax, [pdu_vec + 2]           ; PDU_M_ENG's bound segment
    mov [_pdu_xinit + PDXB_INIT_ENG_SEG], ax
    mov ax, [pdu_vec + 6]           ; PDU_M_REND's
    mov [_pdu_xinit + PDXB_INIT_REND_SEG], ax
    mov ax, PDXB_INIT_BYTES
    push ax
    xor ax, ax
    push ax
    push word [_pdu_xb]
    mov ax, _pdu_xinit
    push ax
    call _pd_fcopy_out
    add sp, 8
    push bp
    mov bp, sp
    xor ax, ax
    push ax
    push ax
    push word [_pdu_xb]
    push word [bp+6]
    push word [bp+4]
    call _pdu_mcall
    add sp, 10
    pop bp
    cmp word [_pdu_mcf], 0
    je .ok
    or ax, ax
    jnz .r
    mov al, PDE_LIMIT
    ret
.ok:
    xor ax, ax
.r:
    ret

; dc_flsub - AX = paragraphs into the claim at [bp-8]: a Flate slot's segment
dc_flsub:
    mov cx, PDU_FL_PARA_A
    push cx
    push ax
    push word [bp-8]
    call _os88_seg_sub
    add sp, 6
    ret

; dc_init - AX = the module, BX = its INIT verb: ZF clear when it refused
dc_init:
    push bx
    push ax
    call _pdu_minit
    add sp, 4
    or ax, ax
    ret

; =============================================================================
; int pdu_bank_load(void) - the PDRES INDEX and the face's kit, once per
; instance (DECISIONS D3, PAGEDESK-SPEC 20.4.1, 20.21.2). The part is read BY
; OFFSET out of PAGEDESK.O88 itself (an OP_SELF row, stored unpacked so the
; offsets are real), from the launch folder - and ONLY its prefix, the INDEX
; and the kit (pdumod.c's pdu_rescap): PDREND reads each strike's bitmaps
; itself, at part_off + the record's offset. The face's band rides after
; what is read. 0, or a PDE_*; only PDE_NOMEM refuses the launch - a missing
; bank leaves the band and the kernel's face (D3). It was C until the slot's
; regions wanted the room (20.22.2)
; =============================================================================
PDU_PART_RES_A  equ 3               ; pdu.h's PDU_PART_RES; RES2 is the next
PDU_BAND_P      equ 2304 / 16       ; pdkit.inc's band: 96 x 24
_pdu_bank_load:
    push si
    push di
    xor ax, ax
    cmp [_pdu_bankraw], ax
    je .go
.out:                               ; up here: every way out is in reach
    pop di
    pop si
    ret
.go:
    mov al, PDU_PART_RES_A
    call op_row
    mov di, [si+OP_R_OFF]           ; para
    mov cx, [si+OP_R_LEN]
    mov ax, PDE_NOTFOUND
    jcxz .out
    or di, di
    jz .out
    mov ax, PDE_LIMIT               ; RES2 must be RES's next byte in the
    test cl, 15                     ; file, or a strike past 65,024 is read
    jnz .out                        ; from the wrong place (pagedesk.asm)
    mov [bk_para], di
    push cx
    mov dx, cx
    mov cl, 4
    shr dx, cl
    add di, dx
    mov al, PDU_PART_RES_A + 1
    call op_row
    pop dx
    cmp [si+OP_R_OFF], di
    mov ax, PDE_LIMIT
    jne .out
    mov si, dx                      ; RES's length, capped
    cmp si, [_pdu_rescap]
    jbe .l
    mov si, [_pdu_rescap]
.l:
    push si
    mov ax, PDU_PART_RES_A
    push ax
    call _pdu_part_need
    pop cx
    pop cx
    mov di, ax                      ; need
    or ax, ax
    mov ax, PDE_IO
    jz .out
    lea ax, [di+PDU_BAND_P]
    push ax
    call _os88_mem_claim_p
    pop cx
    mov dx, ax
    or ax, ax
    mov ax, PDE_NOMEM
    jz .out
    mov [_pdu_bankraw], dx
    mov ax, PDU_BAND_P
    push ax
    push di
    push dx
    call _os88_seg_sub
    add sp, 6
    mov [bk_band], ax
    push ax
    xor ax, ax
    push ax
    call _pk_init                   ; the band now: the bars come first
    pop cx
    pop cx
    mov ax, PDU_PART_RES_A
    mov dx, [_pdu_bankraw]
    mov cx, si
    call pdu_home_rd
    or ax, ax
    jnz .fail
    mov ax, [_pdu_bankraw]
    mov [_pdu_bank], ax
    push si
    call _pdu_bank_dir              ; 20.4.1's directory
    pop cx
    or ax, ax
    jnz .fail
    mov ax, [_pdu_kitlen]
    or ax, ax
    jz .nokit
    add ax, 15
    mov cl, 4
    shr ax, cl
    push ax
    mov ax, [_pdu_kitoff]
    shr ax, cl
    push ax
    push word [_pdu_bankraw]
    call _os88_seg_sub
    add sp, 6
    push word [bk_band]
    push ax
    call _pk_init
    pop cx
    pop cx
.nokit:
    call bk_fill
    xor ax, ax
    jmp .out
.fail:
    push ax                         ; the claim stays for the band: no fonts,
    push word [bk_band]             ; no pictures, no bank (D3)
    xor ax, ax
    mov [_pdu_bank], ax
    push ax
    call _pk_init
    pop cx
    pop cx
    pop ax
    jmp .out

; bk_fill - the bank is in: its KiB and D3's pdxb_fontbank (SI, DI free)
bk_fill:
    mov ax, [_pdu_banklen]
    add ax, 1023
    mov cl, 10
    shr ax, cl
    mov [_pdu_bankkb], ax
    push es                         ; D3's pdxb_fontbank: where PDREND finds
    push ds                         ; the strikes it reads itself
    pop es
    mov di, _pdu_xfb
    mov cx, PDXB_FONTBANK_BYTES / 2
    xor ax, ax
    rep stosw
    mov di, _pdu_xfb + PDXB_FONTBANK_PKGNAME
    mov si, op_name
    mov cx, PDXB_FONTBANK_PKGNAME_N - 1
.n:
    lodsb
    or al, al
    jz .d
    stosb
    loop .n
.d:
    pop es
    mov ax, [_pdu_home]
    mov [_pdu_xfb + PDXB_FONTBANK_PLACE_CLUS], ax
    mov ax, [_pdu_home+2]
    mov [_pdu_xfb + PDXB_FONTBANK_PLACE_VOL], ax
    mov ax, [bk_para]
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov [_pdu_xfb + PDXB_FONTBANK_PART_OFF], ax
    mov cl, 12
    shr dx, cl
    mov [_pdu_xfb + PDXB_FONTBANK_PART_OFF + 2], dx
    mov ax, [_pdu_bank]
    mov [_pdu_xfb + PDXB_FONTBANK_INDEX_SEG], ax
    mov ax, [_pdu_banklen]
    mov [_pdu_xfb + PDXB_FONTBANK_INDEX_LEN], ax
    ret

; =============================================================================
; int pdu_bank_dir(unsigned len) - 20.4.1's directory at the bank just read:
; 'PDRS', version 1, n entries of 12 bytes. Kind 7 is the INDEX (it sets
; pdu_banklen and must lie inside the `len` read); kind 9 the face's kit, on
; a paragraph and inside what was read (pdu_kitoff / pdu_kitlen, 0 = none).
; 0, PDE_FONT (the bank is not right) or PDE_LIMIT.
; =============================================================================
_pdu_bank_dir:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov es, [_pdu_bank]
    mov dx, [bp+4]
    mov ax, PDE_FONT
    mov word [_pdu_kitlen], 0
    cmp word [es:0], 0x4450         ; "PD"
    jne .x
    cmp word [es:2], 0x5352         ; "RS"
    jne .x
    cmp word [es:4], 1
    jne .x
    mov cx, [es:6]
    mov si, 8
.e:
    jcxz .x
    lea bx, [si+12]
    cmp bx, dx
    ja .x
    cmp word [es:si+6], 0           ; an offset or length past 64 KiB
    jne .n
    cmp word [es:si+10], 0
    jne .n
    mov bx, [es:si+4]
    mov di, [es:si+8]
    cmp word [es:si], PDU_RES_KIND_INDEX_A
    jne .k
    or bx, bx
    jnz .k
    mov [_pdu_banklen], di
    mov ax, PDE_LIMIT
    or di, di
    jz .k
    cmp di, dx
    ja .k
    xor ax, ax
.k:
    cmp word [es:si], PDU_RES_KIND_KIT_A
    jne .n
    test bl, 15
    jnz .n
    or di, di
    jz .n
    cmp bx, dx
    ja .n
    push dx
    sub dx, bx
    cmp di, dx
    pop dx
    ja .n
    mov [_pdu_kitoff], bx
    mov [_pdu_kitlen], di
.n:
    add si, 12
    dec cx
    jmp short .e
.x:
    pop es
    pop di
    pop si
    pop bp
    ret
PDU_RES_KIND_INDEX_A equ 7
PDU_RES_KIND_KIT_A   equ 9

; pdxb_init's six document regions, in its field order: cache, arena,
; tables, dl, band, glyph (rows * 2)
dc_map:         db 0, 2, 4, 8, 10, 6


section .bss
alignb 2
_pdu_socc:      resw 1              ; the slot's occupant, 0 = empty
_pdu_sclaim:    resw 1              ; claim S
_pdu_swaps:     resw 1              ; loads since the job began
_pdu_ckind:     resw 1              ; 0 not tried, 1 XMS, 2 EMS, -1 none
_pdu_ctok:      resw 2              ; the XMS token, or the EMS handle
_pdu_cframe:    resw 1              ; EMS: quarter 0's segment
sl_ladn:        resw 1
sl_grants:      resw 1
sl_row:         resw 1
sl_len:         resw 1
sl_img:         resw 1
sl_ws:          resw 1
sl_wskb:        resw 1
sl_need:        resw 1
sl_wmin:        resb 1
alignb 2
sl_dseg:        resw 1
rd_clus:        resw 1              ; pdu_home_rd's folder to go back to
rd_vol:         resw 1
bk_para:        resw 1              ; pdu_bank_load
bk_band:        resw 1
sl_mag:         resw 1
sl_dir:         resw 1
sl_msk:         resw 1
ck_at:          resw PDU_NOCC_A     ; each occupant's place in the store
xr_claim:       resw 1              ; claim X
xr_para:        resw 1
xr_last:        resw 1              ; the region at X's top
xr_off:         resw PDU_NREG + 1   ; by PDMEM_* (1..5), paragraphs
xr_len:         resw PDU_NREG + 1
xr_kb:          resw 1
x8_in:          resb 1              ; PDX8 is in the X8 region
alignb 2
pg_row:         resw 1
pg_cap:         resw 1
pg_len:         resw 1
pg_off:         resw 2
pg_clp:         resw 1
pg_slp:         resw 1
pg_dst:         resw 1
pg_rdp:         resw 1
pg_chp:         resw 1
pg_done:        resw 1
cx_done:        resw 1
cx_n:           resw 1
sl_cin:         resb 1              ; bit per occupant the store holds
sl_cbit:        resb 1
sl_sh:          resb 1
alignb 2
dc_bandb:       resw 1              ; the band's bytes
dc_para:        resw PDU_NDC_A      ; each document row, in paragraphs
_pdu_kitoff:    resw 1
_pdu_kitlen:    resw 1
%ifdef PDU_LADTEST                 ; the look's counters (pdujob.c)
_pdu_tdisk:     resw 1
_pdu_tcache:    resw 1
%endif
alignb 2
section .text
