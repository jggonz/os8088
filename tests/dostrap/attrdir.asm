; ATTRDIR.COM - what does AH=43h say about a DIRECTORY?
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.12.4.  Microsoft Works's Save As, given a name on another drive,
; asks AH=43h about the DIRECTORY the file would go in before it writes
; anything - and reports `Directory not found` when that is refused. Under
; this box it was refused for every directory there is: `.att_get` resolves
; the name through `dos_fh_stat`, which is the FILE lookup AH=3Dh opens
; through, so a name that is a folder finds nothing and answers 2.
;
; The root is its own question and the reason this probe prints rather than
; asserts on the first run: DOS is not uniform about `A:\` across versions,
; and what this box should answer is what the machine on the other side of
; the cable actually says. It runs under os8088 AND under a real DOS
; unchanged, so the reference IS the specification.
;
;   A  AH=43h on "\"            - the current drive's root
;   B  AH=43h on "A:\"          - a root, drive-qualified, Works's own shape
;   C  AH=43h on a SUBDIR       - made here, so the disk needs nothing
;   D  AH=43h on a real FILE    - the control that must succeed
;   E  AH=43h on a missing name - the control that must fail with 2
    org 0x100
    cpu 8086
start:
    mov dx, n_sub                   ; C's subject, made rather than assumed
    mov ah, 0x39
    int 0x21                        ; ...and an existing one is fine

    mov si, s_a                     ; A, B, C: a DIRECTORY succeeds with bit 4
    mov dx, n_root
    mov bl, 0x10
    call ask
    mov si, s_b
    mov dx, n_aroot
    mov bl, 0x10
    call ask
    mov si, s_c
    mov dx, n_sub
    mov bl, 0x10
    call ask
    mov si, s_d                     ; D: a FILE succeeds with ARCHIVE
    mov dx, n_self
    mov bl, 0x20
    call ask
    mov si, s_e                     ; E: and only a MISSING name fails, with 2
    mov dx, n_no
    mov bl, 0x00
    call ask

    mov dx, n_sub                   ; tidy up, so a second run reads the same
    mov ah, 0x3A
    int 0x21

    ; --- THE VERDICT. It asserts the PROPERTY and not DOS's exact CX for a
    ; root: IBM DOS 3.30 answers 0074 for `\` and `A:\`, which is bits it
    ; never deliberately set - a root has no directory entry to read them
    ; from. What every caller tests is CF and bit 4, and copying an
    ; uninitialised byte would be copying a bug and calling it a contract.
    cmp byte [n_bad], 0
    jne .fail
    mov si, s_pass
    call puts
    jmp short .done
.fail:
    mov si, s_fail
    call puts
.done:
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C00
    int 0x21

; --- ask AH=43h about DS:DX and print `NAME ax=.... cx=.... cf=.` ------------
ask:
    push bx
    push dx
    call puts
    pop dx
    pop bx
    mov ax, 0x4300
    int 0x21
    ; --- BANK THE CARRY BEFORE ANYTHING CAN TOUCH IT. It is the whole answer
    ; and it is the most fragile thing on the machine: a `push`, an `or bl, bl`
    ; or a `cmp` between here and the test all clear it, and the judgement then
    ; reads its OWN flag and passes whatever the box said. `mov` is the one
    ; instruction here that does not write flags, so this `jnc` still sees the
    ; carry INT 21h returned.
    mov byte [n_cf], 0
    jnc .nocf
    mov byte [n_cf], 1
.nocf:
    push ax
    push cx
    or bl, bl
    jz .want_fail
    cmp byte [n_cf], 0
    jne .bad                        ; wanted success...
    test cl, bl                     ; ...with that bit set
    jz .bad
    jmp short .judged
.want_fail:
    cmp byte [n_cf], 0
    je .bad                         ; wanted the refusal...
    cmp ax, 2
    jne .bad                        ; ...and DOS's own code for it
    jmp short .judged
.bad:
    mov byte [n_bad], 1
.judged:
    pop cx
    pop ax
    mov si, s_ax
    call puts
    call putw
    mov si, s_cx
    call puts
    mov ax, cx
    call putw
    mov si, s_cf
    call puts
    mov al, [n_cf]                  ; ...the carry as it was, not as it is
    add al, '0'
    mov dl, al
    mov ah, 0x02
    int 0x21
    mov si, s_nl
    call puts
    ret

putw:
    push ax
    push cx
    push dx
    mov cx, 4
.digit:
    push cx
    mov cl, 4
    rol ax, cl
    pop cx
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 7
.out:
    mov dl, al
    push ax
    mov ah, 0x02
    int 0x21
    pop ax
    pop ax
    loop .digit
    pop dx
    pop cx
    pop ax
    ret

puts:
    push ax
    push dx
.one:
    mov dl, [si]
    or dl, dl
    jz .out
    mov ah, 0x02
    int 0x21
    inc si
    jmp short .one
.out:
    pop dx
    pop ax
    ret

n_root:  db '\', 0
n_aroot: db 'A:\', 0
n_sub:   db 'ATTRDIRX', 0
n_self:  db 'ATTRDIR.COM', 0
n_no:    db 'NOSUCH.XYZ', 0
s_a:     db 'A root  "\"        ', 0
s_b:     db 'B root  "A:\"      ', 0
s_c:     db 'C subdir ATTRDIRX  ', 0
s_d:     db 'D file  ATTRDIR.COM', 0
s_e:     db 'E missing NOSUCH   ', 0
s_ax:    db ' ax=', 0
s_cx:    db ' cx=', 0
s_cf:    db ' cf=', 0
s_nl:    db 13, 10, 0
s_pass:  db 'ATTRDIR PASS', 13, 10, 0
s_fail:  db 'ATTRDIR FAIL', 13, 10, 0
n_bad:   db 0
n_cf:    db 0
