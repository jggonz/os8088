; =============================================================================
; os8088 - apps/dos/dosw16.inc
;
; A WINDOWS PROGRAM IS WIN16'S AND NOT OURS (SPEC.md 124.3). `.EXE` is DOS's
; association, and a Windows 3.x program is an `.EXE` too: an MZ stub that
; prints "This program requires Microsoft Windows." and a new-format header
; behind it. Before anything is claimed or taken out, the wake reads the
; header and hands an NE to `WIN16.O88` through OSAPI_PKG_START's document
; arm - the program becomes WIN16's document, exactly as a double-click on it
; would have made it if `.EXE` were WIN16's extension.
;
; THE SIGNATURE DECIDES, which is dos_is_exe's rule one header along: an MZ
; whose relocation table starts at 40h or later carries `e_lfanew` at 3Ch,
; and a word `NE` there is the new executable. Anything else - a `.COM`, a
; plain MZ, an LE or PE, a compressed file whose raw bytes are its wrapper -
; is DOS's, and the run goes on as it always did.
;
; WHAT IT COSTS A DOS PROGRAM is one READ_AT of the first cluster, which the
; load that follows reads again, and the FAT count OSAPI_FILE_DFREE makes
; (SPEC.md 18.4.5). A second read only follows the `40h` test, which a plain
; DOS program's header fails.
;
; ONLY IN -DDOS_WIN16, which only the WIN16 test disk's DOS.O88 is built
; with (`make win16test`), until S12 decides where WIN16.O88 ships
; (docs/plans/WIN16-PLAN.md). A forward on a machine that has no WIN16.O88
; ends in the association search's full rebuild and a `Needs WIN16.O88`
; toast with DOS's window already gone, where the stub's own sentence is the
; truer answer - so the shipped DOS.O88 does not carry this, and is
; byte-identical to the one before it. Never on kern_mega: WIN16 refuses
; there (SPEC.md 124.1, D6).
; =============================================================================
%ifdef DOS_MEGA
 %error "DOS_WIN16 on kern_mega: WIN16 refuses there (SPEC.md 124.1)"
%endif

DOS_W16_NE  equ 0x454E              ; 'NE', read as a word

; -----------------------------------------------------------------------------
; dos_w16_try - hand a Windows program to WIN16.O88
; in:  [dos_dir]/[dos_vol]/[dos_name] = the program; UI task, lock NOT held
; out: CF=1 it was one and WIN16 has it: [dos_state] is DST_IDLE, and a launch
;      that did not come from the prompt closes this window. CF=0 = run it
;      here. Preserves every register but the flags.
; -----------------------------------------------------------------------------
dos_w16_try:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov dx, [dos_dir]
    mov bl, [dos_vol]
    call dos_be_goto                ; stand where the file is
    jc .no
    call OSAPI_FILE_DFREE           ; BX = sectors per cluster: READ_AT wants
    jc .no                          ; offset and capacity in whole clusters
    cmp bx, 64
    ja .no                          ; a 64KB cluster: no 16-bit CX names it
    mov ax, bx
    inc ax
    shr ax, 1                       ; KB, at least one
    call OSAPI_MEM_CLAIM            ; DX = the block
    jc .no
    mov di, dx
    mov cl, 9
    shl bx, cl                      ; BX = the cluster in bytes
    push bx                         ; [sp] = it, for the second read

    mov cx, bx                      ; --- the first cluster -------------------
    mov es, di
    xor bx, bx
    xor ax, ax
    xor dx, dx
    mov si, dos_name
    call OSAPI_FILE_READ_AT         ; DX:AX = the bytes delivered
    jc .free
    mov bp, ax
    cmp bp, 0x40
    jb .free
    cmp word [es:0], 0x5A4D         ; 'MZ'
    jne .free
    cmp word [es:0x18], 0x40        ; ...with room for a new header's pointer
    jb .free
    cmp word [es:0x3E], 0           ; e_lfanew's high word: inside 64KB
    jne .free
    mov ax, [es:0x3C]
    test al, 1                      ; even, so the word cannot straddle the
    jnz .free                       ; cluster's end
    pop cx
    push cx                         ; CX = the cluster in bytes
    mov dx, cx
    dec dx                          ; a power of two, so a mask
    mov si, ax
    and si, dx                      ; SI = e_lfanew within its cluster
    not dx
    and ax, dx                      ; AX = that cluster's own offset
    jz .have                        ; the first: already here

    push si                         ; --- ...or the one holding e_lfanew ----
    xor bx, bx
    xor dx, dx
    mov si, dos_name
    call OSAPI_FILE_READ_AT
    pop si
    jc .free
    mov bp, ax
.have:
    lea ax, [si + 2]
    cmp bp, ax                      ; delivered past the signature?
    jb .free
    cmp word [es:si], DOS_W16_NE
    jne .free
    pop cx
    mov dx, di
    call OSAPI_MEM_FREE
    jmp short .isne
.free:
    pop cx
    mov dx, di
    call OSAPI_MEM_FREE
.no:
    clc
    jmp short .out

.isne:                              ; --- IT IS ONE: WIN16's document --------
    mov si, dos_name                ; the 16-byte locator (SPEC.md 21.5.3),
    mov di, dos_pgdoc               ; folded, because the association tables
    mov cx, 13                      ; are upper-case exact (21.5.3.2)
.cp:
    mov al, [si]
    call dos_upc
    mov [di], al
    inc si
    inc di
    loop .cp
    mov byte [dos_pgdoc + 12], 0
    mov ax, [dos_dir]
    mov [dos_pgdoc + 13], ax
    mov al, [dos_vol]
    mov [dos_pgdoc + 15], al
    mov si, dos_w16_pkg             ; WIN16's stem names the slot (SPEC.md
    xor cx, cx                      ; 124.3): a built-in row, found wherever
    xor dx, dx                      ; the volume keeps it
    push ds
    pop es
    mov di, dos_pgdoc
    call OSAPI_PKG_START
    jc .no                          ; nothing opens it: DOS runs the stub
    mov byte [dos_state], DST_IDLE
    cmp byte [dos_fromcon], 0
    jne .con
    mov bx, [dos_win]               ; the desktop launched it: WIN16's window
    call OSAPI_WM_CLOSE             ; replaces ours
    stc
    jmp short .out
.con:
    mov byte [dos_fromcon], 0       ; ...typed at the prompt: the console
    call dos_prompt                 ; stays, and owes its prompt
    mov bx, [dos_win]
    call OSAPI_WM_OBSCURED
    jc .said
    call OSAPI_GFX_LOCK
    call dos_con_draw
    call OSAPI_GFX_UNLOCK
.said:
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dos_w16_pkg: db 'WIN16.O88', 0

