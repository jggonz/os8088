; =============================================================================
; os8088 - apps/os88link.inc
;
; LINKS, and asking to be STARTED AT BOOT (SPEC.md 26.10).
;
; A LINK is a 50-byte file, *.LNK, that names an entry by its path - what a
; desktop shortcut holds, minus the picture. Opening one opens what it names,
; the way a double-click on that entry would: a package runs, a document goes
; to its association, a folder gets a Disk window. The kernel follows them;
; this file is how a PACKAGE writes one.
;
; AUTOSTART is a folder of links: \AUTOSTRT in the root of the volume the
; machine booted from. Every link in it is opened once the boot is done, and
; holding either Shift through the boot skips them. A link is a file the user
; can see, copy and delete in a Disk window - which is how one is removed, so
; there is no remove call here: a package that put one there can delete it
; with OSAPI_FILE_DELETE the same way it wrote it, standing in the folder.
;
; NEEDS apps/os88data.inc FIRST, ASSEMBLED WITH OD_BOOTVOL: the walk is that
; file's (od_qm, od_dive, od_back), on the boot volume, and so is the bank of
; where the instance stood - which this leaves it standing in again.
;
; WHAT YOU GET
;   OS88_LINK label, volume, kind, 'path'
;             a link record, LNK_SIZE bytes, in your image. volume is an index
;             (0 = A:) or LNK_SELF, the volume the LINK FILE is on - which is
;             what a program asking to start a program on its own boot disk
;             wants, and is still right after the disk is copied to C:. kind
;             is 1 for a package, 0 for a document, 2 for a folder. path is
;             below the root and has no leading backslash: 'SYSTEM\RLAPPLY.O88'
;   ol_autostart
;             SI = the link's NUL 8.3 name ('RLAPPLY.LNK'), BX = its record
;             in DS. out CF=0 written (an existing one of that name is
;             replaced), or CF=1 AX = FERR_*. \AUTOSTRT is made if it is not
;             there. Every register but AX preserved; the instance is left
;             where it stood. UI task only, like every file cell.
;
; Every label is ol_-prefixed.
; =============================================================================

%ifndef OD_BOOTVOL
  %error "os88link.inc: include apps/os88data.inc with OD_BOOTVOL defined first - autostart is the BOOT volume's (SPEC.md 26.10.5)"
%endif

; --- a LINK file (SPEC.md 26.10.1) - mirrored from kernel/desksc.inc, and
; tests/unit/t_mirror.py compares the two --------------------------------------
LNK_SIG     equ 0               ;  2: 'LK'
LNK_VER     equ 2               ;  1: LNK_V
LNK_VOL     equ 3               ;  1: the volume, or LNK_SELF
LNK_KIND    equ 4               ;  1: 0 document, 1 package, 2 folder
LNK_PATH    equ 5               ; 45: below the root, NUL
LNK_SIZE    equ 50
LNK_V       equ 1
LNK_SELF    equ 0xFF

%macro OS88_LINK 4
%1: db 'LK', LNK_V, %2, %3, %4, 0
    times LNK_SIZE - ($ - %1) db 0
%if $ - %1 != LNK_SIZE
  %error "OS88_LINK: the path is longer than a link holds (44 characters)"
%endif
%endmacro

ol_s_auto:  db 'AUTOSTRT', 0

ol_autostart:
    push es
    push bx
    push cx
    push dx
    push si
    push bx                         ; the record, for the write
    call OSAPI_FILE_HERE            ; DX = our folder, BL = our drive
    mov [od_home], dx
    mov [od_homedrv], bl
    call OSAPI_VOL_SYS              ; BL = the volume the machine booted from
    mov [od_tdrv], bl
    xor dx, dx
    call od_qm                      ; its ROOT
    jc .fail
    push si
    mov si, ol_s_auto
    call od_dive                    ; \AUTOSTRT, if it is there...
    jnc .in
    call OSAPI_FILE_MKDIR           ; ...and made if it is not
    jc .failsi
    call od_dive
    jc .failsi
.in:
    pop si                          ; the link's name
    pop bx                          ; ...and its record
    push ds
    pop es
    mov cx, LNK_SIZE
    xor dx, dx
    call OSAPI_FILE_WRITE           ; creates or replaces
    jmp short .out
.failsi:
    pop si
.fail:
    pop bx
    stc
.out:
    call od_back                    ; flags kept: the answer
    pop si
    pop dx
    pop cx
    pop bx
    pop es
    ret
