; =============================================================================
; kerndos/kdgate.inc - wave 3's entry point: mount a floppy and read a file
;
; **THIS IS A GATE AND NOT kern_dos's REAL ENTRY** (docs/plans/KERN-DOS-PLAN.md
; KERN-DOS-PLAN §7, which is W5's). It exists because *the shim assembles* and *the shim
; works* are different claims, and only the second one is worth anything: a
; `ret` where a value was expected assembles perfectly and returns garbage.
;
; So it does the smallest thing that exercises the whole layer end to end -
; mount A:, find one file by name, read its cluster chain, and report the
; length and a checksum through the ROM's teletype - and tests/kerndos.py
; computes the same two numbers on the host with tools/os88fat.py.
;
; It is behind KD_GATE and no shipping path defines it.
; =============================================================================
%ifdef KD_GATE
; SPEC.md 19.1's staged record, named here because nothing else needs them.
KDE_NAME    equ 0               ; 16 bytes, NUL-terminated
KDE_TYPE    equ 16
KDE_CLUS    equ 18              ; the handle: the first cluster
KDE_SIZE    equ 20              ; ...and the size, a dword

section .text

kd_gate_entry:
    cli
    mov ax, KD_SEG
    mov ds, ax
    mov es, ax
    mov ax, LOW_SEG             ; **SS IS NOT DS HERE AND THAT IS THE POINT**
    mov ss, ax                  ; (SPEC.md 2.1): disk.inc's three big buffers
    mov sp, KD_STACK            ; live in .lowbss and are reached through SS,
    sti                         ; which is the one thing a root has to get
    cld                         ; right before the layer will work at all

    mov ax, LOW_SEG + (KD_LOW_KB * 64)   ; the allocator's two ends: the
    mov [kd_floor], ax                   ; first paragraph above .lowbss...
    mov ax, 0x9000                       ; ...and a ceiling well under the
    mov [kd_top], ax                     ; 640KB mark, this being a gate

    mov si, kd_s_mount
    call kd_puts
    mov dl, 1                   ; **B:, AND THAT IS THE BOOT DISK'S FAULT**:
                                ; a FAT12 volume's sectors 1..n are its FATs,
                                ; so the blob cannot ride there. A: carries
                                ; the loader and the blob raw and B: is the
                                ; volume under test
    call disk_mount_x
    jnc .mounted
    mov si, kd_s_nomount
    call kd_puts
    jmp kd_halt
.mounted:
    mov si, kd_s_ok
    call kd_puts
    mov ax, [disk_nfiles]       ; how many entries the mount synthesized
    call kd_hex16
    call kd_crlf

    ; --- find the file, by the name the gate disk was built with ------------
    mov si, kd_wanted
    call dsk_find_name_x        ; out CF=0 and **AX** = the entry index; the
                                ; synthesized name is NUL-terminated (SPEC.md
                                ; 19), not the on-disk 8.3 padding
    jnc .found
    mov si, kd_s_nofile
    call kd_puts
    jmp kd_halt
.found:
    call dsk_get_dir_x          ; SI -> the staged 32-byte entry
    ; **THE STAGED RECORD IS SPEC.md 19.1's AND NOT THE ON-DISK ONE**: name
    ; 0..15 NUL-terminated, type 16, handle 18, size dword 20..23. The
    ; DSK_R_* offsets beside them in disk.inc are the FAT's own layout, and
    ; reading a size at 28 out of this record answers ZERO - which then reads
    ; as a chain corruption two calls later, because a length of 0 asks for
    ; 0 sectors.
    mov ax, [si+KDE_SIZE]       ; the low word is all a gate file needs; the
    mov [kd_len], ax            ; build keeps it well under 64KB
    mov ax, [si+KDE_CLUS]
    mov dx, [kd_len]            ; sectors = ceil(size / 512)
    add dx, 511
    mov cl, 9
    shr dx, cl
    push ds
    mov bx, [kd_top]            ; ...into the top of the allocator's arena,
    mov es, bx                  ; which nothing else has claimed
    xor bx, bx
    call dsk_read_chain_x
    pop ds
    jnc .read
    push ax
    mov si, kd_s_noread
    call kd_puts
    pop ax
    xor ah, ah
    call kd_hex16               ; 1 = disk error, 2 = chain corrupt
    call kd_crlf
    jmp kd_halt
.read:
    mov si, kd_s_len
    call kd_puts
    mov ax, [kd_len]
    call kd_hex16
    call kd_crlf

    ; --- the checksum: a 16-bit rotate-and-add over exactly kd_len bytes ----
    ; Not a sum: a plain add cannot tell a reordering or a zero run from the
    ; real bytes, and a chain read is exactly the thing that gets those wrong.
    mov si, kd_s_sum
    call kd_puts
    mov cx, [kd_len]
    mov ax, [kd_top]
    mov es, ax
    xor bx, bx
    xor ax, ax
    jcxz .done
.sum:
    rol ax, 1
    mov dl, [es:bx]
    xor dh, dh
    add ax, dx
    inc bx
    loop .sum
.done:
    call kd_hex16
    call kd_crlf
    mov si, kd_s_end
    call kd_puts
kd_halt:
    hlt
    jmp kd_halt

; --- the ROM's teletype, which is the whole of this gate's output -----------
kd_puts:
    push ax
    push bx
    push si
.c:
    mov al, [si]
    or al, al
    jz .out
    inc si
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp short .c
.out:
    pop si
    pop bx
    pop ax
    ret

kd_crlf:
    push ax
    push bx
    mov ax, 0x0E0D
    mov bx, 0x0007
    int 0x10
    mov ax, 0x0E0A
    int 0x10
    pop bx
    pop ax
    ret

kd_hex16:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax
    mov cx, 4
.n:
    rol dx, 1
    rol dx, 1
    rol dx, 1
    rol dx, 1
    mov al, dl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    loop .n
    pop dx
    pop cx
    pop bx
    pop ax
    ret

kd_s_mount:  db 'kern_dos gate: mount B: ', 0
kd_s_ok:     db 'ok, entries ', 0
kd_s_nomount: db 'MOUNT FAILED', 13, 10, 0
kd_s_nofile: db 'FILE NOT FOUND', 13, 10, 0
kd_s_noread: db 'CHAIN READ FAILED', 13, 10, 0
kd_s_len:    db 'len ', 0
kd_s_sum:    db 'sum ', 0
kd_s_end:    db 'gate done', 13, 10, 0
kd_wanted:   db 'GATE.TXT', 0

section .bss
kd_len:      resw 1
section .text
%endif                          ; KD_GATE
