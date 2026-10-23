; =============================================================================
; os8088 - tests/dostrap/xkidrun.asm
;
; XKIDRUN.COM - SPEC.md 96.40.9.1's CHILD probe: a parent that EXECs an
; EXEPACK child with AH=4Bh and exits with the CHILD's exit code, so a single
; run says whether the HMA host unpacked the child (docs/plans/KERN-MEGA-PLAN.md
; 14.26, Task B).
;
;   make build/XKIDRUN.COM            (and build/XKIDRUNT.COM, -DCHILD2)
;
; The child is XUNP.EXE (tools/os88xpack.py packs tests/dostrap/xunp.asm): a
; program whose EXEPACK STUB only refuses - it prints "Packed file is corrupt"
; and exits 0FFh - so its real code (which checks it was unpacked and
; relocated) runs ONLY if a loader unpacked it. kern_dos's AH=4Bh loader does
; not pass through kd_entry's `kd_xpack`, so before 14.26 the child ran its
; own stub below 64KB with A20 open and exited 255; the DHK_XUNP hook now
; unpacks it in place first, the same `kd_xunp` as the top-level program.
;
;   on the HMA host, hook present:  XUNP unpacked -> its checks pass -> 0x30
;                                   (48), and XKIDRUN exits 48
;   hook absent / a child it refuses: the child's own stub runs -> 0xFF (255)
;
; **THE BREAK-ON-PURPOSE IS -DCHILD2**: XKIDRUNT.COM EXECs XUNPT.EXE, whose
; relocation table is spoiled, so `kd_xunp` REFUSES it as a child exactly as
; it does at the top level - the child's dummy stub then runs and exits 255.
; 48 from XKIDRUN beside 255 from XKIDRUNT is the hook working and declining
; correctly; 255 from BOTH is the hook not firing.
;
; OURS, MIT with the rest of the tree, under tests/ because it is not shipped.
; =============================================================================

    cpu 8086
    bits 16
    org 0x100

start:
    ; **THE STACK MUST BE INSIDE THE SHRUNK BLOCK** before running a LARGE
    ; child: a .COM is entered with SP = 0FFFEh, 64KB up, and after the shrink
    ; below that is the CHILD's memory - so a 128KB child overwrites the
    ; parent's stack and the return is a crash. A launcher moves its stack
    ; down first, which is what this is. (The dosexec gate's DOSKID is too
    ; small to reach it, so parent.asm never had to.)
    mov sp, 0x1FFE                  ; the top of the 8KB we keep
    mov [parm+4], ds                ; the tail's segment, at run time
    mov ah, 0x09
    mov dx, msg_hi
    int 0x21

    ; --- shrink to 8KB so the child has the rest of the arena --------------
    mov ah, 0x4A
    mov bx, 0x200
    push ds
    pop es
    int 0x21
    jc .shfail

    mov ah, 0x09
    mov dx, msg_run
    int 0x21

    ; --- EXEC the EXEPACK child --------------------------------------------
    mov ax, 0x4B00
    mov dx, cname
    mov bx, parm
    push ds
    pop es
    int 0x21
    jc .exfail

    ; --- its exit code, which is this program's answer ----------------------
    mov ah, 0x4D
    int 0x21
    xor ah, ah
    mov [childcode], al
    mov ah, 0x09
    mov dx, msg_code
    int 0x21
    mov al, [childcode]
    xor ah, ah
    call put_dec16
    call put_crlf
    jmp short .done

.shfail:
    mov dx, msg_esh
    mov ah, 0x09
    int 0x21
    mov byte [childcode], 0xF0
    jmp short .done
.exfail:
    push ax
    mov ah, 0x09
    mov dx, msg_eex
    int 0x21
    pop ax
    call put_dec16
    call put_crlf
    mov byte [childcode], 0xF1
.done:
    mov ah, 0x09
    mov dx, msg_key
    int 0x21
    mov ah, 0x08
    int 0x21
    mov al, [childcode]             ; EXIT WITH THE CHILD'S CODE, so a run that
    mov ah, 0x4C                    ; cannot watch the screen still reads it
    int 0x21

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
%ifdef CHILD2
cname:    db 'XUNPT.EXE', 0
%else
cname:    db 'XUNP.EXE', 0
%endif
tail:     db 0, 13
parm:     dw 0
          dw tail
          dw 0
          dd 0
          dd 0

childcode: db 0xFF

msg_hi:   db 13,10,'os8088 EXEPACK child gate - XKIDRUN.COM',13,10,13,10,'$'
msg_run:  db 'RUNNING the EXEPACK child...',13,10,'$'
msg_code: db 'CHILD exit code ','$'
msg_esh:  db 'FAILED at AH=4Ah',13,10,'$'
msg_eex:  db 'FAILED at AH=4Bh, code ','$'
msg_key:  db 13,10,'READY - press a key to exit with the child code',13,10,'$'
