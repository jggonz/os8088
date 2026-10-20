; =============================================================================
; os8088 - tests/dosfile/file.asm
;
; The wave-2 file-handle gate's DOS program (SPEC.md 96.11). A .COM that puts
; a file through the whole handle layer and checks the bytes came back:
;
;   - AH=3Ch create, then AH=40h forty times, which is 20,480 bytes and so
;     crosses the 8KB window TWICE - the first flush REPLACES and the two
;     after it APPEND, which is the one ordering OSAPI_FILE_APPEND's
;     cluster-multiple rule allows (SPEC.md 96.11);
;   - AH=42h origin 2, which is how a program asks a file's size;
;   - AH=3Fh in 512-byte reads, every byte checked against its own offset, so
;     a window that refills at the wrong base is caught at the seam rather
;     than looking like a short read;
;   - AH=42h to 12,345 - inside no window boundary - and a read there, which
;     is the random-access case the sequential one cannot catch;
;   - AH=41h delete, and an open afterwards that MUST fail with code 2.
;
; THE FILE IS BYTE N = N & 0FFh, deliberately: it makes every check a
; comparison against the position itself, so an off-by-one window is a wrong
; VALUE and not a missing one.
;
; NOTHING HERE IS THIRD-PARTY. It is ours, MIT with the rest of the tree, and
; it is under tests/ because it is not shipped software (CLAUDE.md, Layout).
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

BLK       equ 512                   ; one write, and one read
NBLK      equ 40                    ; 20,480 bytes: two window crossings
SEEKTO    equ 12345                 ; ...and a point inside none of them
MARK      equ 0x5A                  ; step 4b's rewrite is offset+MARK, which
                                    ; the file cannot already hold anywhere
SZ4D      equ BLK * NBLK + 16 + BLK * 2   ; what 4a..4d leave behind
TRN1      equ 12000                 ; 4g shrinks to this - MORE than the DOS
                                    ; box's window, so it is the copying arm
TRN2      equ 100                   ; ...and then to this, which fits it
GAP       equ 5000                  ; ...and how far past it 4e seeks: bigger
                                    ; than a cluster on every geometry here,
                                    ; so the GAP itself crosses the hand-over
ZGAP      equ 777                   ; ...and how far past THAT 4f's CX=0 seeks

start:
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- 1. create and write ------------------------------------------------
    mov ah, 0x3C
    xor cx, cx
    mov dx, fname
    int 0x21
    jc .cfail
    mov [handle], ax

    xor bx, bx                      ; BX = the running file offset, which is
    mov cx, NBLK                    ; also the byte value at it
.wblk:
    push cx
    call fill                       ; buf[] = (BX+i) & 0FFh
    mov ah, 0x40
    mov bx, [handle]
    mov cx, BLK
    mov dx, buf
    int 0x21
    jc .wfail
    cmp ax, BLK
    jne .wshort
    pop cx
    add word [fpos], BLK
    mov bx, [fpos]
    loop .wblk

    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    jc .clfail
    mov ah, 0x09
    mov dx, msg_wrote
    int 0x21

    ; --- 2. open it again and ask its size ----------------------------------
    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax

    mov ax, 0x4202                  ; seek to the end: DX:AX = the size
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    push ax
    mov ah, 0x09
    mov dx, msg_size
    int 0x21
    pop ax
    call put_dec16
    call put_crlf

    ; --- 3. read it all back, checking every byte ---------------------------
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    mov word [fpos], 0
.rblk:
    mov ah, 0x3F
    mov bx, [handle]
    mov cx, BLK
    mov dx, buf
    int 0x21
    jc .rfail
    or ax, ax
    jz .rdone
    mov cx, ax
    mov bx, [fpos]
    call check                      ; CF=1 with BX = the offset that differed
    jc .vfail
    add [fpos], cx
    jmp short .rblk
.rdone:
    mov ah, 0x09
    mov dx, msg_read
    int 0x21
    mov ax, [fpos]
    call put_dec16
    call put_crlf

    ; --- 4. a seek into the middle, and a read there ------------------------
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, SEEKTO
    int 0x21
    jc .sfail
    mov ah, 0x3F
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .rfail
    cmp ax, 16
    jne .vfail2
    mov cx, 16
    mov bx, SEEKTO
    call check
    jc .vfail                       ; check leaves BX on the byte that differed,
                                    ; and .vfail2 would overwrite it with the
                                    ; seek target - the offset is the finding
    mov ah, 0x09
    mov dx, msg_seek
    int 0x21

    ; --- 4b. SEEK BACK AND REWRITE, IN PLACE (SPEC.md 96.11.6) --------------
    ; The case AH=3Dh could not make before OSAPI_FILE_WRITE_AT (18.4.7): a
    ; handle opened for READ/WRITE, seeked backwards, written over. The read
    ; is through a FRESH handle so the window cannot be answering out of
    ; memory, and the SIZE is asked again afterwards because the whole
    ; restriction that makes the slot cheap is that it cannot move one.
    mov ah, 0x3E                    ; the read-only handle is done with
    mov bx, [handle]
    int 0x21
    mov ax, 0x3D02                  ; ...and THIS one asked to write
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, SEEKTO
    int 0x21
    jc .sfail
    mov bx, SEEKTO + MARK           ; a pattern the file does NOT already hold
    call fill                       ; at this offset
    mov ah, 0x40
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .wfail
    cmp ax, 16
    jne .wshort
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    jc .clfail

    mov ax, 0x3D00                  ; a fresh handle, so the bytes come off
    mov dx, fname                   ; the DISK and not out of the window
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4202                  ; ...and the size did not move
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    cmp ax, BLK * NBLK
    jne .grew
    or dx, dx
    jnz .grew
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, SEEKTO
    int 0x21
    jc .sfail
    mov ah, 0x3F
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .rfail
    cmp ax, 16
    jne .vfail2
    mov cx, 16
    mov bx, SEEKTO + MARK
    call check
    jc .vfail
    mov ah, 0x09
    mov dx, msg_inpl
    int 0x21

    ; --- 4c. ...AND A WRITE AT THE END GROWS IT (SPEC.md 96.11.6) -----------
    ; The other half of a read/write handle, and the one a real DOS program
    ; leans on: open an existing file, seek to the end, add to it. It must
    ; take all sixteen bytes, the size must move by exactly sixteen, and they
    ; must read back - through a fresh handle again.
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    mov ax, 0x3D02
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4202                  ; ...to the end of it
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    mov bx, BLK * NBLK + MARK       ; a pattern nothing in the file holds
    call fill
    mov ah, 0x40
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .wfail
    cmp ax, 16
    jne .eshort                     ; a SHORT count here is the box refusing
    mov ah, 0x3E                    ; to grow the file
    mov bx, [handle]
    int 0x21
    jc .clfail

    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4202
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    cmp ax, BLK * NBLK + 16
    jne .enogrow
    or dx, dx
    jnz .enogrow
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, BLK * NBLK
    int 0x21
    jc .sfail
    mov ah, 0x3F
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .rfail
    cmp ax, 16
    jne .vfail2
    mov cx, 16
    mov bx, BLK * NBLK + MARK
    call check
    jc .vfail
    mov ah, 0x09
    mov dx, msg_grew
    int 0x21

    ; --- 4d. ...ACROSS the slack/append boundary (SPEC.md 18.4.7.2) ---------
    ; 4c grew a file whose size was a whole number of clusters, so it went
    ; straight to the append path. This one starts at 20,496 - a size no
    ; cluster multiple - so the first write lands in the last cluster's SLACK
    ; through OSAPI_FILE_WRITE_AT, and the second runs out of slack partway
    ; and has to hand the rest to OSAPI_FILE_APPEND. That hand-over inside one
    ; AH=40h is the whole composition, and it is what a short count would
    ; expose.
    ;
    ; It asserts BEHAVIOUR and not which arm ran: the size moves by exactly
    ; what was written and the bytes read back, on a volume of any cluster
    ; size.
    mov ax, 0x3D02
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov cx, 2                       ; two blocks, and the SECOND is the one
    mov word [nleft], 2             ; that runs out of slack
.gblk:
    mov ax, 0x4202
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    mov [gpos], ax                  ; where this block starts, for the verify
    mov bx, ax
    call fill
    mov ah, 0x40
    mov bx, [handle]
    mov cx, BLK
    mov dx, buf
    int 0x21
    jc .wfail
    cmp ax, BLK
    jne .eshort                     ; a SHORT count is the hand-over failing
    dec word [nleft]
    jnz .gblk

    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    jc .clfail
    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4202
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    cmp ax, BLK * NBLK + 16 + BLK * 2
    jne .enogrow
    mov ax, 0x4200                  ; ...and the LAST block, which is the one
    mov bx, [handle]                ; that crossed
    xor cx, cx
    mov dx, [gpos]
    int 0x21
    jc .sfail
    mov ah, 0x3F
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .rfail
    cmp ax, 16
    jne .vfail2
    mov cx, 16
    mov bx, [gpos]
    call check
    jc .vfail
    mov ah, 0x09
    mov dx, msg_cross
    int 0x21

    ; --- 4e. ...and a seek PAST the end leaves a GAP (SPEC.md 96.11.6.1) ----
    ; 4c and 4d both wrote AT the file's end. This one seeks GAP bytes BEYOND
    ; it and writes there, which is how every fixed-record program puts record
    ; 40 into a twelve-record file, and how a pre-allocation is spelled. The
    ; size must reach POS + the count, and the bytes must read back from where
    ; the SEEK put them rather than from the old end.
    ;
    ; GAP is bigger than any cluster this runs on, so the gap itself crosses
    ; the slack/append hand-over 4d is about: its first bytes go in through
    ; OSAPI_FILE_WRITE_AT and the rest through OSAPI_FILE_APPEND.
    mov ax, 0x3D02
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, SZ4D + GAP
    int 0x21
    jc .sfail
    mov bx, SZ4D + GAP + MARK
    call fill
    mov ah, 0x40
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .wfail
    cmp ax, 16
    jne .eshort                     ; a SHORT count is the gap being refused
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    jc .clfail

    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4202
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    cmp ax, SZ4D + GAP + 16
    jne .enogrow
    or dx, dx
    jnz .enogrow
    mov ax, 0x4200                  ; the bytes, where the SEEK put them and
    mov bx, [handle]                ; not where the file used to end
    xor cx, cx
    mov dx, SZ4D + GAP
    int 0x21
    jc .sfail
    mov ah, 0x3F
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc .rfail
    cmp ax, 16
    jne .vfail2
    mov cx, 16
    mov bx, SZ4D + GAP + MARK
    call check
    jc .vfail

    ; ...and the gap is REAL STORAGE, read at both of its ends: the slack arm
    ; laid the first bytes and the append accumulator the last, so a hand-over
    ; that lost a window shows up here rather than as a size that looks right.
    ; The ZERO is ours and not DOS's, which leaves a gap undefined - what is
    ; being asserted is that the read succeeds with a FULL count.
    mov cx, SZ4D                    ; the first gap bytes...
    call gapzero
    mov cx, SZ4D + GAP - 16         ; ...and the last
    call gapzero
    mov ah, 0x09
    mov dx, msg_gap
    int 0x21

    ; --- 4f. ...and CX=0 past the end is "the file ends HERE" (96.11.6.2) ---
    ; Writing ZERO bytes is how DOS sets a file's length, and both DOS and this
    ; box answer CF=0 with AX=0 whatever happens - so the SIZE is the only
    ; assertion there can be. Only the extending direction is built; the
    ; shortening one is still a no-op and is not asserted here.
    mov ax, 0x3D02
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    mov dx, SZ4D + GAP + 16 + ZGAP
    int 0x21
    jc .sfail
    mov ah, 0x40
    mov bx, [handle]
    xor cx, cx                      ; the call under test: no bytes at all
    mov dx, buf
    int 0x21
    jc .wfail
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    jc .clfail
    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    jc .ofail
    mov [handle], ax
    mov ax, 0x4202
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc .sfail
    cmp ax, SZ4D + GAP + 16 + ZGAP
    jne .enogrow
    or dx, dx
    jnz .enogrow
    mov ah, 0x09
    mov dx, msg_zlen
    int 0x21

    ; --- 4g. ...and CX=0 SHORT of the end shrinks it (SPEC.md 96.11.6.2) ----
    ; Three truncations, one per arm of the rewrite, and each is checked for
    ; the SIZE and for a byte that has to have SURVIVED it - a rewrite that
    ; drops or shifts the prefix is otherwise a file of exactly the right
    ; length full of the wrong thing.
    ;
    ;   TRN1 is larger than the window, so it is the temporary-file COPY;
    ;   TRN2 fits the window, so it is the one read and one replace;
    ;   zero is dos_fh_touch.
    ;
    ; Both checked offsets are below 20,480, where the file is still step 1's
    ; byte N = N & 0FFh.
    mov cx, TRN1
    call shrink
    mov cx, TRN1
    call sizeis
    mov dx, TRN1 - 16
    mov cx, 16
    call vfy

    mov cx, TRN2
    call shrink
    mov cx, TRN2
    call sizeis
    mov dx, TRN2 - 16
    mov cx, 16
    call vfy

    xor cx, cx
    call shrink
    xor cx, cx
    call sizeis
    mov ah, 0x09
    mov dx, msg_shrink
    int 0x21

    ; --- 5. close, delete, and prove it is gone -----------------------------
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    mov ah, 0x41
    mov dx, fname
    int 0x21
    jc .dfail
    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    jnc .still
    cmp ax, 2
    jne .wrongerr
    mov ah, 0x09
    mov dx, msg_gone
    int 0x21
    jmp .done

.cfail:  push ax
         mov ah, 0x09
         mov dx, msg_ecreate
         int 0x21
         pop ax
         call put_dec16
         call put_crlf
         jmp short .done
.wfail:  mov dx, msg_ewrite
         jmp short .say
.wshort: mov dx, msg_eshort
         jmp short .say
.clfail: mov dx, msg_eclose
         jmp short .say
.ofail:  mov dx, msg_eopen
         jmp short .say
.sfail:  mov dx, msg_eseek
         jmp short .say
.rfail:  mov dx, msg_eread
         jmp short .say
.dfail:  mov dx, msg_edel
         jmp short .say
.still:  mov dx, msg_estill
         jmp short .say
.wrongerr: mov dx, msg_ecode
         jmp short .say
.grew:   mov dx, msg_egrew
         jmp short .say
.eshort: push ax
         push ax
         mov ah, 0x09
         mov dx, msg_eshrt2
         int 0x21
         pop ax
         call put_dec16
         mov ah, 0x09
         mov dx, msg_left
         int 0x21
         mov ax, [nleft]
         call put_dec16
         call put_crlf
         pop ax
         jmp .done
.enogrow: mov dx, msg_enogrow
         jmp short .say
.vfail2: mov bx, SEEKTO
.vfail:  push bx
         mov ah, 0x09
         mov dx, msg_ebad
         int 0x21
         pop ax
         call put_dec16
         call put_crlf
         jmp short .done
.say:
    mov ah, 0x09
    int 0x21
.done:
    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov ax, 0x4C21
    int 0x21

; -----------------------------------------------------------------------------
; shrink - open, seek to CX, write ZERO bytes, close (SPEC.md 96.11.6.2)
shrink:
    mov ax, 0x3D02
    mov dx, fname
    int 0x21
    jc start.ofail
    mov [handle], ax
    mov ax, 0x4200
    mov bx, [handle]
    mov dx, cx
    xor cx, cx
    int 0x21
    jc start.sfail
    mov ah, 0x40
    mov bx, [handle]
    xor cx, cx
    mov dx, buf
    int 0x21
    jc start.wfail
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    jc start.clfail
    ret

; sizeis - a FRESH handle's size must be CX; leaves it open
sizeis:
    push cx
    mov ax, 0x3D00
    mov dx, fname
    int 0x21
    jc start.ofail
    mov [handle], ax
    mov ax, 0x4202
    mov bx, [handle]
    xor cx, cx
    xor dx, dx
    int 0x21
    jc start.sfail
    pop cx
    cmp ax, cx
    jne start.enogrow
    or dx, dx
    jnz start.enogrow
    ret

; vfy - CX bytes at file offset DX are still (DX + i) & 0FFh; closes
vfy:
    push dx
    push cx
    mov ax, 0x4200
    mov bx, [handle]
    xor cx, cx
    int 0x21
    jc start.sfail
    pop cx
    mov ah, 0x3F
    mov bx, [handle]
    mov dx, buf
    int 0x21
    jc start.rfail
    pop bx
    cmp ax, 16
    jne start.vfail
    mov cx, 16
    call check
    jc start.vfail
    mov ah, 0x3E
    mov bx, [handle]
    int 0x21
    ret

; -----------------------------------------------------------------------------
; gapzero - seek to CX, read 16, and every byte must be 0 (SPEC.md 96.11.6.1)
; Jumps out to the caller's own failure arms, which is why it is not a proc
; that returns a flag: there is nothing useful to do with a bad gap but say so.
gapzero:
    mov ax, 0x4200
    mov bx, [handle]
    mov dx, cx
    xor cx, cx
    int 0x21
    jc start.sfail
    mov ah, 0x3F
    mov bx, [handle]
    mov cx, 16
    mov dx, buf
    int 0x21
    jc start.rfail
    cmp ax, 16
    jne start.vfail2
    mov cx, 16
    mov si, buf
    xor bx, bx
.next:
    cmp byte [si], 0
    jne start.vfail
    inc si
    inc bx
    loop .next
    ret

; -----------------------------------------------------------------------------
; fill - buf[0..BLK) = (BX + i) & 0FFh
fill:
    push ax
    push cx
    push di
    mov di, buf
    mov ax, bx
    mov cx, BLK
.next:
    mov [di], al
    inc di
    inc ax
    loop .next
    pop di
    pop cx
    pop ax
    ret

; check - CX bytes at buf[] against (BX + i) & 0FFh
; out: CF=1 with BX = the offset that differed
check:
    push ax
    push cx
    push si
    mov si, buf
    mov ax, bx
.next:
    cmp [si], al
    jne .bad
    inc si
    inc ax
    inc bx
    loop .next
    pop si
    pop cx
    pop ax
    clc
    ret
.bad:
    pop si
    pop cx
    pop ax
    stc
    ret

put_chr:
    push ax
    push dx
    mov dl, al
    mov ah, 0x02
    int 0x21
    pop dx
    pop ax
    ret

put_crlf:
    mov al, 13
    call put_chr
    mov al, 10
    call put_chr
    ret

put_dec16:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.emit:
    pop ax
    add al, '0'
    call put_chr
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
nleft:    dw 0                    ; 4d's block counter...
gpos:     dw 0                    ; ...and where the block it is writing began
fname:    db 'DOSTEST.DAT', 0
handle:   dw 0
fpos:     dw 0

msg_hi:      db 13,10,'os8088 DOS file gate - DOSFILE.COM',13,10,13,10,'$'
msg_wrote:   db 'WROTE 20480 and closed',13,10,'$'
msg_size:    db 'SIZE ','$'
msg_read:    db 'READ ','$'
msg_seek:    db 'SEEK ok',13,10,'$'
msg_gone:    db 'GONE ok',13,10,'$'
msg_inpl:    db 'INPLACE ok',13,10,'$'
msg_grew:    db 'GREW ok',13,10,'$'
msg_cross:   db 'CROSS ok',13,10,'$'
msg_gap:     db 'GAP ok',13,10,'$'
msg_zlen:    db 'ZLEN ok',13,10,'$'
msg_shrink:  db 'SHRINK ok',13,10,'$'
msg_left:    db ' blocks left ','$'
msg_eshrt2:  db 'FAILED - a write at the end took a SHORT count ','$'
msg_enogrow: db 'FAILED - a write at the end did not move the size',13,10,'$'
msg_egrew:   db 'FAILED - the in-place write moved the SIZE',13,10,'$'
msg_ecreate: db 'FAILED at create, code ','$'
msg_ewrite:  db 'FAILED at write',13,10,'$'
msg_eshort:  db 'FAILED - a short write',13,10,'$'
msg_eclose:  db 'FAILED at close',13,10,'$'
msg_eopen:   db 'FAILED at open',13,10,'$'
msg_eseek:   db 'FAILED at seek',13,10,'$'
msg_eread:   db 'FAILED at read',13,10,'$'
msg_edel:    db 'FAILED at delete',13,10,'$'
msg_estill:  db 'FAILED - it opened after the delete',13,10,'$'
msg_ecode:   db 'FAILED - the delete left the wrong error code',13,10,'$'
msg_ebad:    db 'FAILED - a wrong byte at offset ','$'
msg_key:     db 13,10,'READY - press a key to exit with code 33',13,10,'$'

    align 16
buf:
