; =============================================================================
; os8088 - apps/os88m83.inc
;
; THE 8.3 NAME MANGLE (SPEC.md 77.20, 70.11.4): a sender's pathname -> the
; name this machine stores it under. ONE implementation, included by every
; package that takes a name off a wire - FTPD's `fd_split` and Telnet's Zmodem
; receive - so a file downloaded by Zmodem and the same file uploaded by FTP
; land on the same name by CONSTRUCTION rather than by a test that two copies
; agree. It was two copies until the shared pass (~300 bytes each), and they
; had drifted twice: Telnet wrote `NAME~1.` for an extension with nothing
; legal in it, and FTPD neither cut at `\` nor saw past the 12th byte of a
; path's last component.
;
; The reference is `tools/os88bbs.py`'s `mangle83`, and `tests/m83.py` runs
; THIS file under Unicorn against every row of its `MANGLE83_CASES` - all
; sixteen, where a wire can carry only twelve.
;
;   * everything up to and including the last `/` or `\` is dropped;
;   * a name that is ALREADY legal 8.3 is copied verbatim, case and all - that
;     is what makes the mapping round-trip;
;   * otherwise the name is the first six legal characters before the LAST
;     dot, upcased, then `~1`, then `.` and the first three legal characters
;     after that dot - and no `.` at all when none of them is legal;
;   * always `~1`, never a search for a free `~2` (a counter resolved against
;     the directory lands the same file differently on two disks);
;   * nothing legal before the dot at all -> `FILE`.
;
; "Legal" is the kernel's `dskw_char_x` set: A-Z, 0-9 and `$%'-_@~`!(){}^#&`.
;
; --- THE API -----------------------------------------------------------------
;   m83_mangle  SI = a NUL pathname, DI = a 13-byte buffer (it must not
;               overlap the path). Out: the stored name at DI, NUL-ended.
;   m83_is83    SI = a NUL name -> CF=0 if it is already legal 8.3
;   m83_leg     AL -> AL upcased, CF=0 if 8.3 may hold it
; All three keep every register but their outputs. DS = your own segment;
; ES is not touched (it is the kernel's in a callback); DF=0 (SPEC.md 1).
;
; No state at all, so nothing in bss and nothing per instance. Include it
; anywhere in your code.
; =============================================================================
%ifndef OS88M83_INC
%define OS88M83_INC

m83_mangle:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si                      ; BX = the leaf: past the last separator
    xor dx, dx                      ; DX = just past the leaf's LAST dot, or 0
.lf:
    lodsb
    or al, al
    jz .lfd
    cmp al, '.'
    jne .ns
    mov dx, si
.ns:
    cmp al, '/'
    je .cut
    cmp al, '\'
    jne .lf
.cut:
    mov bx, si                      ; a separator: the leaf starts again, and
    xor dx, dx                      ; a dot in a FOLDER name is not its dot
    jmp short .lf
.lfd:
    mov si, bx
    call m83_is83
    jc .st0
.cp:                                ; already legal: copied VERBATIM
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .cp
    jmp short .out
.st0:
    xor cx, cx                      ; the stem: six legal characters before
.st:                                ; the dot
    lodsb
    or al, al
    jz .std
    cmp si, dx                      ; that was the last dot (DX = 0 never
    je .std                         ; matches: SI is past a byte it has read)
    call m83_leg
    jc .st
    cmp cl, 6
    jae .std
    mov [di], al
    inc di
    inc cx
    jmp short .st
.std:
    or cl, cl
    jnz .tilde
    mov word [di], 'FI'             ; nothing legal: it still has to be a name
    mov word [di+2], 'LE'           ; a person can see and delete
    add di, 4
.tilde:
    mov word [di], '~1'
    inc di
    inc di
    or dx, dx
    jz .term
    mov si, dx
    mov byte [di], '.'              ; written now, and taken back below when
    inc di                          ; nothing after the dot is legal
    xor cx, cx
.ex:
    lodsb
    or al, al
    jz .exd
    call m83_leg
    jc .ex
    cmp cl, 3
    jae .exd
    mov [di], al
    inc di
    inc cx
    jmp short .ex
.exd:
    or cl, cl
    jnz .term
    dec di                          ; `x. ` is X~1, never X~1.
.term:
    mov byte [di], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- m83_is83 - CF=0 if DS:SI is already a legal 8.3 name ---------------------
; Stem 1..8 legal characters, at most one dot, extension 1..3 if it is there.
; CH = the part's size (8, then 3), CL = the room left in it.
m83_is83:
    push ax
    push cx
    push si
    mov cx, 0x0808
.c:
    lodsb
    or al, al
    jz .end
    cmp al, '.'
    je .dot
    call m83_leg
    jc .no
    dec cl
    jns .c                          ; a ninth (or fourth) character: too long
.no:
    stc
    jmp short .ret
.dot:
    cmp ch, 8
    jne .no                         ; a SECOND dot
    cmp cl, ch
    je .no                          ; `.TXT` has no stem
    mov cx, 0x0303
    jmp short .c
.end:
    cmp cl, ch                      ; an empty name, or a trailing dot
    je .no
    clc
.ret:
    pop si
    pop cx
    pop ax
    ret

; --- m83_leg - AL legal in an 8.3 name? CF=0 and AL upcased if it is ----------
m83_leg:
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 32
.up:
    cmp al, 'A'
    jb .dig
    cmp al, 'Z'
    jbe .yes
.dig:
    cmp al, '0'
    jb .sym
    cmp al, '9'
    jbe .yes
.sym:
    push bx
    mov bx, m83_syms
.s:
    cmp byte [bx], 0
    je .no
    cmp al, [bx]
    je .hit
    inc bx
    jmp short .s
.no:
    pop bx
    stc
    ret
.hit:
    pop bx
.yes:
    clc
    ret

m83_syms:   db "$%'-_@~`!(){}^#&", 0

%endif
