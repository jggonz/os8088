; =============================================================================
; os8088 - apps/dos/dosbe.inc
;
; **THE os8088 BACK END OF THE DOS CORE, AS ONE SOURCE FOR TWO HOSTS**
; (SPEC.md 96.44.1, 124.21.1). The core stores a `DBE_*` ordinal and its host
; fills `dos_bevec` with these doors' addresses; DOS.O88 and WIN16 are both
; hosts whose doors go to the `OSAPI_*` cells, so the doors, `dos_be_bind`
; and the table it copies are written once, here, and each host includes this
; file. (`kern_dos`'s doors go to the disk layer: kerndos/kdback.inc.)
;
; The table is a MACRO, `DOS_BETAB`, because DOS.O88 has always emitted it
; after its hooks rather than beside the doors, and a host that moved it
; would assemble a different DOS.O88 for nothing.
; =============================================================================
dos_k_goto:
    call OSAPI_FILE_GOTO_QM         ; QM AND NOT Q, and the difference is the
    ret                             ; whole of whether this works when the
                                    ; handler is on a different volume from the
                                    ; document. GOTO_Q moves the MACHINE and not
                                    ; the INSTANCE, and the SDK says what that
                                    ; costs in as many words: "GOTO_Q alone is
                                    ; undone by that next cell, which first
                                    ; re-stands the machine in your instance's
                                    ; folder". Our instance stands where
                                    ; assoc_locate found DOS.O88 - A:\APPS on a
                                    ; cross-volume launch - so the read looked
                                    ; for the program THERE. It worked at all
                                    ; only while the handler happened to sit
                                    ; beside the document, which is the one
                                    ; arrangement a gate disk naturally has

dos_k_read:
    call OSAPI_FILE_READ
    ret

dos_k_find:
    call OSAPI_FILE_FIND
    ret

dos_k_rdat:
    call OSAPI_FILE_READ_AT
    ret

dos_k_write:
    call OSAPI_FILE_WRITE
    ret

dos_k_append:
    call OSAPI_FILE_APPEND
    ret

dos_k_delete:
    call OSAPI_FILE_DELETE
    ret

dos_k_dfree:
    call OSAPI_FILE_DFREE
    ret

dos_k_mkdir:
    call OSAPI_FILE_MKDIR
    ret

dos_k_rmdir:
    call OSAPI_FILE_RMDIR
    ret

dos_k_xcaps:
    call OSAPI_XMEM_CAPS
    ret

dos_k_xalloc:
    call OSAPI_XMEM_ALLOC
    ret

dos_k_xfree:
    call OSAPI_XMEM_FREE
    ret

dos_k_rename:
    call OSAPI_FILE_RENAME
    ret

dos_k_copy:
    mov al, OSAPI_FCP_COPY          ; the file manager's own engine, published
    jmp short dos_k_fcp             ; as ONE cell with a verb (SPEC.md 22.24)
dos_k_move:
    mov al, OSAPI_FCP_MOVE          ; ...whose move is the engine's Cut: the
dos_k_fcp:                          ; re-link first, copy-then-delete where it
    call OSAPI_FILE_COPY            ; declines. The shell's MOVE streams with
    ret                             ; its own buffer only on FERR_FULL, which
                                    ; is the engine unable to claim ITS buffer
                                    ; inside a bracket (96.30.6)

dos_k_xcopy:
    call OSAPI_XMEM_COPY
    ret

dos_k_path:
    call OSAPI_FILE_PATH
    ret

dos_k_here:
    call OSAPI_FILE_HERE
    ret

dos_k_vkind:
    call OSAPI_VOL_KIND
    ret

dos_k_vstat:
    call OSAPI_VOL_STAT
    ret

dos_k_wrat:
    call OSAPI_FILE_WRITE_AT
    ret

dos_k_rseq:                         ; SPEC.md 18.4.8.1: READ_AT's registers
    jmp os88_rseq                   ; plus DS:DI, and READ_AT itself on the
                                    ; small kernel
dos_k_wseq:                         ; SPEC.md 18.4.9 ...
    call OSAPI_FILE_WRITE_SEQ
    jnc .ok
    cmp ax, FERR_NAME               ; ...and on the small kernel, which has
    jne .no                         ; none, APPEND - whose refusals are the
    jcxz .none                      ; same - and a close that is nothing
    call OSAPI_FILE_APPEND
    ret
.none:
    xor ax, ax                      ; CF=0
.ok:
    ret
.no:
    stc
    ret

%include "os88rseq.inc"

; -----------------------------------------------------------------------------
; dos_be_bind - the twenty-two doors, as addresses (SPEC.md 96.44.1)
; in:  nothing; out: nothing, every register preserved
;
; CALLED ONCE, BEFORE THE FIRST DOOR. The core stores a `DBE_*` ORDINAL and
; `dos_be_go` turns it into an address through this table, so the core can be
; assembled ONCE and joined to either back end. The table is DS-relative data
; and the copy is a `rep movsw` of twenty-two words.
; -----------------------------------------------------------------------------
dos_be_bind:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, dos_betab
    mov di, dos_bevec
    mov cx, DBE_NENT
    cld
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    ret

%macro DOS_BETAB 0
dos_betab:
    dw dos_k_goto, dos_k_read, dos_k_find, dos_k_rdat, dos_k_write
    dw dos_k_append, dos_k_delete, dos_k_dfree, dos_k_mkdir, dos_k_rmdir
    dw dos_k_xcaps, dos_k_xalloc, dos_k_xfree, dos_k_xcopy, dos_k_rename
    dw dos_k_copy, dos_k_move, dos_k_path, dos_k_vstat, dos_k_wrat
    dw dos_k_here, dos_k_vkind, dos_k_rseq, dos_k_wseq
%endmacro
