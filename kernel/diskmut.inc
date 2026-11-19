; =============================================================================
; Directory mutations in DISKOPS.DRV (SPEC.md 18.5.3).
; DS is the kernel, CS this image. Shared FAT/commit/rollback helpers remain
; cold and are reached through wm_cbd, except the two BP-taking private gates.
; The resident dispatcher loads before mutation and releases only its own hold.
; =============================================================================
section .modl

%macro DMO_CALL 1
    mov bp, %1
    call COLD_SEG:wm_cbd
%endmacro
%macro DMO_TAIL 1
    DMO_CALL %1
    ret
%endmacro

; These four verbs are private constants, dispatched before clone state.
dmo_dispatch:
    sub dl, CLV_MKDIR
    mov bl, dl
    xor bh, bh
    shl bx, 1
    jmp [cs:bx+dmo_verbs]
dmo_verbs:
    dw dmo_mkbody, dmo_nbody, dmo_rmbody, dmo_rtbody

; Gate failures discard exactly one NEAR image return address. Never call
; the resident dskw_gate through wm_cbd: its stack escape belongs to .cold.
dmo_gate:
%ifdef DSKW_WSEQ
    DMO_CALL dws_gate
%endif
    cmp byte [dsk_mntok], 1
    jne dmo_mounted.no
%ifdef OS88_REDIR
    cmp byte [dsk_vkind], DVK_FILE
    jne dmo_gate83
    xchg ax, bp
    call COLD_SEG:dskw_dir_fsop_f
    jmp short dmo_mounted.quit
%endif
dmo_gate83:
    DMO_CALL dskw_name83n
    jnc dmo_mounted.ret
    mov ax, FERR_NAME
    jmp short dmo_mounted.fail
dmo_mounted:
%ifdef DSKW_WSEQ
    DMO_CALL dws_gate
%endif
    cmp byte [dsk_mntok], 1
%ifdef OS88_REDIR
    jne .no
    cmp byte [dsk_vkind], DVK_FILE
    ret
%else
    je .ret
%endif
.no:
    mov ax, FERR_NODISK
.fail:
    stc
.quit:
    inc sp
    inc sp
.ret:
    ret

; Allocate and initialize the new cluster, flush FAT, then commit the entry.
; mkclus stays resident because ordinary file writes grow directories too.
dmo_mkbody:
%ifdef OS88_REDIR
    mov ax, FSV_MKDIR
%endif
    call dmo_gate
    DMO_CALL dskw_find
    jnc .exists
    cmp ax, FERR_NOENT
    jne dmo_mounted.ret

    DMO_CALL dskw_dirgrow
    jc .fail

    mov bp, dskw_dotents
    call COLD_SEG:dskw_dir_mkclus_f
    jc .fail

    DMO_CALL dskw_flush_x
    jc .rollax

    xor ax, ax
    mov [dskw_len], ax
    mov [dskw_lenhi], ax
    inc ax
    mov [dskw_new], al
    mov [dskw_isdir], al
    DMO_CALL dskw_commit
    mov byte [dskw_isdir], 0
    jc .rollax
    DMO_TAIL dskw_relold.flush

.exists:
    mov al, FERR_EXIST
    SKIPF2
.dirfull:
    mov al, FERR_DIRFULL
    SKIPF2
.nospace:
    mov al, FERR_FULL
    cbw
    stc
    ret
.badclus:
    mov ax, FERR_IO
    jmp short .rollax
.wfail:
    DMO_CALL dskw_ioerr
.rollax:
    push ax
    mov byte [dskw_isdir], 0
    DMO_CALL dskw_refat
    pop ax
    stc
.fail:
    ret

; Remove an empty folder only after the same species/emptiness checks.
; The resident commit tail is shared with ordinary Delete and truncate.
dmo_rmbody:
%ifdef OS88_REDIR
    mov ax, FSV_RMDIR

%endif
    call dmo_gate

dmo_rmbody_n83:

    call dmo_fdir
    jc .out
    mov [dskw_oldclus], ax

    call dmo_isempty
    jc .out

    DMO_TAIL dskw_rmbody_n83.delent
.out:
    ret

dmo_fdir:
    DMO_CALL dskw_find
    jc .out
    mov al, [dskw_raw+DSK_R_ATTR]
    test al, DSKW_NPROT
    jnz .prot
    test al, DSKW_DIR
    jz .prot
    mov ax, [dskw_raw+DSK_R_CLUS]
    DMO_CALL dskw_clok
    jnc .out
    DMO_TAIL dskw_rdata.corrupt
.prot:
    DMO_TAIL dskw_wbody.prot
.out equ dmo_rmbody_n83.out

; Iterative depth-first delete, bounded by DSKW_RT_MAX. Parent links on the
; disk carry the walk, so nesting never grows the task stack. Return home
; on every failure and commit each removal before releasing its chain.
dmo_rtbody:
    call dmo_mounted
%ifdef OS88_REDIR
    jne .fat

.fsrt:
    mov bp, FSV_RMTREE
    call COLD_SEG:dskw_dir_fsop_f

    jnc .fsok
    or ax, ax
    jnz .fsno

    jmp dmo_rmbody

%else
    jmp short .fat
%endif
%ifdef OS88_REDIR
.fsno:
    stc
%endif
.fsok:
    ret
.fat:
    mov ax, [dsk_cwd]
    mov [dskw_rt_home], ax
    mov di, dskw_rt_name
    DMO_CALL dskw_name83

    jnc .named
    jmp dmo_nbody.badarg
.named:
    call dmo_rtn83
    call dmo_fdir
    jc .fsok

    mov [dskw_rt_top], ax
    call dmo_chdir_dl
    jc .ioerr

    mov word [dskw_rt_guard], DSKW_RT_MAX
.loop:
    dec word [dskw_rt_guard]
    jz .corrupt
    call dmo_rt_scan
    jc .empty
    cmp byte [dskw_rt_attr], 0x0F
    je .lfn
    test byte [dskw_rt_attr], DSKW_DIR
    jnz .down
    DMO_CALL dskw_dbody_n83
    jc .home
    jmp short .loop
.lfn:
    and word [dskw_oldclus], 0
    DMO_CALL dskw_rmbody_n83.delent
    jc .home

    jmp short .loop
.down:
    mov ax, [dskw_rt_clus]
    DMO_CALL dskw_clok
    jc .corrupt_home
    call dmo_chdir_dl
    jc .iohome
    jmp short .loop

.empty:
    cmp byte [dskw_rt_ioerr], 0
    jne .iohome
    mov ax, [dsk_cwd]
    cmp ax, [dskw_rt_top]
    je .finish
    mov [dskw_rt_clus], ax
    DMO_CALL dsk_dotdot_x
    jc .corrupt_home
    call dmo_chdir_dl
    jc .iohome
    call dmo_rt_byclus
    jc .corrupt_home
    call dmo_rmbody_n83
    jc .home
    jmp short .loop

.corrupt_home:
.iohome:
    call dmo_rt_go_home
.corrupt:
.ioerr:
    DMO_TAIL dskw_rdata.corrupt
.home:
    push ax
    call dmo_rt_go_home
    pop ax
    stc
    ret

.finish:
    call dmo_rt_go_home
    jc .ioerr
    call dmo_rtn83
    jmp dmo_rmbody_n83

dmo_rt_go_home:
    mov ax, [dskw_rt_home]

dmo_chdir_dl:
    push dx
    mov dl, [disk_drive]
    DMO_CALL dsk_chdir_x
    pop dx
    ret

; One live entry, including orphan LFN slots; retain I/O failure separately
; from a successfully empty directory.
dmo_rt_scan:
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
    call dmo_scan1
    jnc .found
    jz .none
    mov byte [dskw_rt_ioerr], 1
.none:
    stc
    jmp short .out
.found:
    mov [dskw_cursec], dx

    DMO_CALL dskw_hit

    call dmo_rawn83
    mov al, [dskw_raw+DSK_R_ATTR]
    mov [dskw_rt_attr], al
    mov ax, [dskw_raw+DSK_R_CLUS]
    mov [dskw_rt_clus], ax
    clc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
; Recover the child name by first cluster after returning to its parent.
dmo_rt_byclus:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov ax, [dsk_cwd]
    mov dx, [dskw_rt_clus]
    DMO_CALL dsk_byclus_x
    jc .out
    mov di, dskw_raw
    mov cx, 32
    DMO_CALL dsk_copy_in_x
    call dmo_rawn83
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The same scanner defines both occupancy and what the tree can name.
dmo_isempty:
    push bx
    push cx
    push dx
    push si
    push es
    push ss
    pop es
    mov ax, [dskw_oldclus]
    call dmo_scan1
    cmc
    jnc .none
    mov ax, FERR_PROT
    jmp short .out
.none:
    jz .out
    DMO_CALL dskw_ioerr
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

dmo_scan1:
    DMO_CALL dsk_dirw_start_x
.sector:
    DMO_CALL dsk_dirw_get_x
    jc .out
    xchg dx, ax
    mov si, dsk_secbuf
    mov cx, 16
.entry:
    mov al, [es:si]
    test al, al
    jz .none
    cmp al, 0xE5
    je .next
    cmp al, '.'
    jne .live
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
    stc
.out:
    ret

; Validate the destination first, refuse protected sources, and change only
; the eleven name bytes before using the shared one-sector commit.
dmo_nbody:
    call dmo_mounted
%ifdef OS88_REDIR
    jne .fatn
    mov bp, FSV_RENAME
    call COLD_SEG:dskw_dir_fsop_f
    ret
%endif
.fatn:
    push si
    mov si, di
    DMO_CALL dskw_name83n
    pop si
    jnc .named
.badarg:
    mov ax, FERR_NAME
    stc
    ret
.named:
    push si
    mov si, dskw_n83
    mov di, dskw_n83b
    DMO_CALL dskw_copy11
    pop si
    DMO_CALL dskw_find
    jc .vacant
    jmp dmo_mkbody.exists
.vacant:
    cmp ax, FERR_NOENT
    je .old
.out:
    stc
    ret
.old:
    call dmo_gate83
    DMO_CALL dskw_find
    jc .out
%ifdef OS88_IMG
    DMO_CALL dskw_imgro
%endif
    test byte [dskw_raw+DSK_R_ATTR], DSKW_NPROT
    jnz .protected

.patch:
    push si
    mov si, dskw_n83b
    mov di, dskw_raw
    DMO_CALL dskw_copy11
    pop si
    and word [dskw_oldclus], 0
    DMO_TAIL dskw_rmbody_n83.entrel

.protected:
    DMO_TAIL dskw_dbody_n83.protj

; Name buffers remain kernel data: the FAT parsers consume DS:DI.
dmo_rawn83:
    mov si, dskw_raw
    jmp short dmo_n83go
dmo_rtn83:
    mov si, dskw_rt_name
dmo_n83go:
    mov di, dskw_n83
    DMO_TAIL dskw_copy11

%unmacro DMO_CALL 1
%unmacro DMO_TAIL 1
section .text
