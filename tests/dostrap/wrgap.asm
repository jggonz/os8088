; WRGAP.COM - a write PAST the end of a file the same handle created.
; OURS, MIT with the rest of the tree.  docs/DOS-DEBUGGING.md is the manual.
;
; SPEC.md 96.11.6.3.  Microsoft Works saves a document by CREATING the file,
; seeking to 0x180 on it while it is still empty, writing the body there, then
; seeking back to 0 and laying the 384-byte header it had left room for.  That
; is the ordinary shape of a format with a header it can only fill in once the
; body is written, and this box refused the FIRST of those two writes with
; `access denied` - so Works reported `Cannot write file` about a disk with
; 42 free clusters on it.
;
; The refusal was one guard doing two jobs: an append-only handle may not write
; BEHIND the end (96.11.2, and that one is real - this layer cannot overwrite
; the middle of a file it is still accumulating), and the test for it was
; `jne`, which refuses PAST the end in exactly the same breath.  Behind and
; past are opposite cases: past is a GAP, and the in-place loop already lays
; one.
;
; It runs under os8088 AND under a real DOS unchanged, which is the point of
; everything in this directory: the reference says what the answers should be.
;
;   A  create, seek to 384 on the EMPTY file   - the seek answers 384
;   B  write 285 bytes THERE                   - 285, and CF=0
;   C  seek back to 0 and write the 384 header - 384, and CF=0
;   D  close, reopen, seek END                 - the file is 669 bytes
;   E  read it back                            - both spans are what was written
;   F  a gap NOBODY fills is still counted     - 100 + 10 = 110 bytes
;   G  delete a name that is not there         - error 2, not 5 (96.11.9)
;
; THE GAP'S OWN CONTENT IS DELIBERATELY NOT ASSERTED.  DOS leaves it
; undefined, so a probe that checked it would fail against the reference for
; being right.  What E checks is the two spans the program WROTE, which is the
; whole of what a program may rely on - and in Works's own shape the header
; write covers the gap completely anyway.
    org 0x100
    cpu 8086
HDRLEN  equ 384
BODYLEN equ 285
BODYAT  equ 384
TOTAL   equ BODYAT + BODYLEN

start:
    mov si, s_a                     ; A: create and seek past the end
    call puts
    mov dx, n_tmp
    mov ah, 0x41                    ; a leftover from a previous run must not
    int 0x21                        ; decide this run; the answer is ignored
    mov dx, n_tmp
    mov cx, 0
    mov ah, 0x3C
    int 0x21
    jc .afail
    mov [h1], ax
    mov bx, ax
    xor cx, cx
    mov dx, BODYAT
    mov ax, 0x4200
    int 0x21
    jc .afail
    cmp ax, BODYAT
    jne .afail
    or dx, dx
    jnz .afail
    call ok

    mov si, s_b                     ; B: ...and WRITE there
    call puts
    mov di, buf
    mov cx, BODYLEN
    mov al, 0x5A                    ; the body's seed
    call fill
    mov bx, [h1]
    mov cx, BODYLEN
    mov dx, buf
    mov ah, 0x40
    int 0x21
    jc .bfail
    cmp ax, BODYLEN
    jne .bfail
    call ok

    mov si, s_c                     ; C: back to 0, and the header
    call puts
    mov bx, [h1]
    xor cx, cx
    xor dx, dx
    mov ax, 0x4200
    int 0x21
    jc .cfail
    or ax, ax
    jnz .cfail
    mov di, buf
    mov cx, HDRLEN
    mov al, 0xA5                    ; ...and the header's
    call fill
    mov bx, [h1]
    mov cx, HDRLEN
    mov dx, buf
    mov ah, 0x40
    int 0x21
    jc .cfail
    cmp ax, HDRLEN
    jne .cfail
    call ok

    mov si, s_c2                    ; C2: is it in the WINDOW?  Same handle,
    call puts                       ; before any flush - so a pass here with a
    mov bx, [h1]                    ; failure at E puts the loss in the FLUSH
    xor cx, cx                      ; and not in the write path, which is the
    xor dx, dx                      ; one split no amount of reading the code
    mov ax, 0x4200                  ; settled
    int 0x21
    jc .c2fail
    mov di, buf
    mov cx, 16
    call zero
    mov bx, [h1]
    mov cx, 16
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    jc .c2fail
    cmp ax, 16
    jne .c2fail
    mov si, buf
    mov cx, 16
    mov al, 0xA5
    call ckfill
    jc .c2fail
    call ok

    mov si, s_d                     ; D: close, reopen, and how big is it?
    call puts
    mov bx, [h1]
    mov ah, 0x3E
    int 0x21
    jc .dfail
    mov dx, n_tmp
    mov ax, 0x3D00
    int 0x21
    jc .dfail
    mov [h1], ax
    mov bx, ax
    xor cx, cx
    xor dx, dx
    mov ax, 0x4202
    int 0x21
    jc .dfail
    cmp ax, TOTAL
    jne .dfail
    or dx, dx
    jnz .dfail
    call ok

    mov si, s_e                     ; E: and the bytes are the bytes
    call puts
    mov bx, [h1]
    xor cx, cx
    xor dx, dx
    mov ax, 0x4200
    int 0x21
    jc .efail
    mov di, buf
    mov cx, TOTAL
    call zero                       ; so a short read cannot pass by luck
    mov bx, [h1]
    mov cx, TOTAL
    mov dx, buf
    mov ah, 0x3F
    int 0x21
    jc .efail
    cmp ax, TOTAL
    jne .efail
    mov si, buf                     ; the header span...
    mov cx, HDRLEN
    mov al, 0xA5
    call ckfill
    jc .efail
    call ok

    mov si, s_e2                    ; E2: ...and the body's, past the gap the
    call puts                       ; header happens to have covered. TWO steps
    mov si, buf + BODYAT            ; because they fail for different reasons:
    mov cx, BODYLEN                 ; the body is the write that used to be
    mov al, 0x5A                    ; REFUSED and the header the one that
    call ckfill                     ; followed it into the same window
    jc .e2fail
    mov bx, [h1]
    mov ah, 0x3E
    int 0x21
    call ok

    mov si, s_f                     ; F: a gap nobody fills is still counted
    call puts
    mov dx, n_tm2
    mov ah, 0x41
    int 0x21
    mov dx, n_tm2
    mov cx, 0
    mov ah, 0x3C
    int 0x21
    jc .ffail
    mov [h2], ax
    mov bx, ax
    xor cx, cx
    mov dx, 100
    mov ax, 0x4200
    int 0x21
    jc .ffail
    mov bx, [h2]
    mov cx, 10
    mov dx, buf
    mov ah, 0x40
    int 0x21
    jc .ffail
    cmp ax, 10
    jne .ffail
    mov bx, [h2]
    mov ah, 0x3E
    int 0x21
    jc .ffail
    mov dx, n_tm2
    mov ax, 0x3D00
    int 0x21
    jc .ffail
    mov bx, ax
    mov [h2], ax
    xor cx, cx
    xor dx, dx
    mov ax, 0x4202
    int 0x21
    jc .ffail
    cmp ax, 110
    jne .ffail
    or dx, dx
    jnz .ffail
    mov bx, [h2]
    mov ah, 0x3E
    int 0x21
    call ok

    mov si, s_g                     ; G: and a delete of nothing says 2
    call puts
    mov dx, n_no
    mov ah, 0x41
    int 0x21
    jnc .gfail                      ; it is not there, so it cannot succeed
    cmp ax, 2
    jne .gfail                      ; ...and 5 is what this box used to say
    call ok

    mov si, s_pass
    call puts
    jmp short done
.afail:
    mov si, s_f_a
    jmp short bad
.bfail:
    mov si, s_f_b
    jmp short bad
.cfail:
    mov si, s_f_c
    jmp short bad
.dfail:
    mov si, s_f_d
    jmp short bad
.c2fail:
    mov si, s_f_c2
    jmp short bad
.efail:
    mov si, s_f_e
    jmp short bad
.e2fail:
    mov si, s_f_e2
    jmp short bad
.ffail:
    mov si, s_f_f
    jmp short bad
.gfail:
    mov si, s_f_g
bad:
    call puts
    mov si, s_fail
    call puts
done:
    mov ah, 0x08                    ; wait for a key, so the screen can be read:
    int 0x21                        ; the bracket ends at the exit below and the
    mov ax, 0x4C00                  ; console goes with it, so a probe that
    int 0x21                        ; falls straight out reports to nobody

; --- DI = buffer, CX = bytes, AL = seed: a pattern an index alone rebuilds ---
fill:
    push ax
    push cx
    push di
    mov ah, al
    xor bx, bx
.one:
    mov al, bl
    add al, ah
    xor al, bh
    mov [di], al
    inc di
    inc bx
    loop .one
    pop di
    pop cx
    pop ax
    ret

; --- SI = buffer, CX = bytes, AL = seed: CF=1 if one byte is not the pattern -
ckfill:
    push ax
    push cx
    push si
    mov ah, al
    xor bx, bx
.one:
    mov al, bl
    add al, ah
    xor al, bh
    cmp [si], al
    jne .bad
    inc si
    inc bx
    loop .one
    clc
    jmp short .out
.bad:
    stc
.out:
    pop si
    pop cx
    pop ax
    ret

zero:
    push ax
    push cx
    push di
    xor al, al
.one:
    mov [di], al
    inc di
    loop .one
    pop di
    pop cx
    pop ax
    ret

ok:
    mov si, s_ok
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

n_tmp:  db 'WRGAP.TMP', 0
n_tm2:  db 'WRGAP2.TMP', 0
n_no:   db 'WRGAPNO.TMP', 0
s_a:    db 'A seek past the end ', 0
s_b:    db 'B write there       ', 0
s_c:    db 'C header at 0       ', 0
s_c2:   db 'C2 header in window ', 0
s_d:    db 'D size is 669       ', 0
s_e:    db 'E header on disk    ', 0
s_e2:   db 'E2 body on disk     ', 0
s_f:    db 'F unfilled gap      ', 0
s_g:    db 'G delete nothing =2 ', 0
s_ok:   db 'ok', 13, 10, 0
s_f_a:  db 'FAILED', 13, 10, 0
s_f_b:  db 'FAILED', 13, 10, 0
s_f_c:  db 'FAILED', 13, 10, 0
s_f_d:  db 'FAILED', 13, 10, 0
s_f_c2: db 'FAILED', 13, 10, 0
s_f_e:  db 'FAILED', 13, 10, 0
s_f_e2: db 'FAILED', 13, 10, 0
s_f_f:  db 'FAILED', 13, 10, 0
s_f_g:  db 'FAILED', 13, 10, 0
s_pass: db 'WRGAP PASS', 13, 10, 0
s_fail: db 'WRGAP FAIL', 13, 10, 0
h1:     dw 0
h2:     dw 0
buf:
