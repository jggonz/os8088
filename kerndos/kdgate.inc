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
; The payload's claim, in KB. GATE.TXT is 9,000 bytes and the build keeps it
; well under 64KB, so one round number covers it with room to spare.
KD_GATE_KB  equ 16

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

    ; --- find the file, by the name the gate disk was built with ------------
    ; **BY NAME OFF THE DIRECTORY, NEVER OUT OF THE GLOBAL LISTING**
    ; (SPEC.md 96.49.7). This was `dsk_find_name_x` then `dsk_get_dir_x`, and
    ; the count printed above it was `[disk_nfiles]` - both of them readers of
    ; a mount snapshot that `kern_dos` cannot have: since SPEC.md 22.6.3 a
    ; listing goes into the store the CALLER supplies through `dsk_dest_x`,
    ; and this host has no window to supply one, so every mount here is quiet
    ; and the count is 0 whatever is on the disk. The gate said so on the
    ; glass - `entries 0000` then `FILE NOT FOUND` - which is the same defect
    ; `kd_resume` had and the same fix.
    mov si, kd_wanted
    call dskw_stat_x            ; CF=0 with BX = the first cluster and DX:CX =
                                ; the size, off a directory walk; the name is
                                ; NUL-terminated (SPEC.md 19), not the on-disk
                                ; 8.3 padding
    jnc .found
    mov si, kd_s_nofile
    call kd_puts
    jmp kd_halt
.nomem:
    mov si, kd_s_nomem
    call kd_puts
    jmp kd_halt
.found:
    mov [kd_len], cx            ; the low word is all a gate file needs; the
    mov [kd_clus], bx           ; build keeps it well under 64KB

    ; --- somewhere to put it, AND IT HAS TO BE CLAIMED -----------------------
    ; This read `[kd_top]`, under a comment saying that was *"the top of the
    ; allocator's arena, which nothing else has claimed"*. **`[kd_top]` IS the
    ; most recent claim's own base**: `mem_claim_x` over in kerndos/kdshim.inc
    ; hands out `[kd_top] - size` and LOWERS the ceiling to it, so the word
    ; names the bottom of what was last handed out and never free room. The
    ; mount claims SPEC.md 18.95's read-ahead cache off that very ceiling, so
    ; the gate was reading the file straight onto the cache serving the read.
    ; It got away with it while nothing had put anything in that cache before
    ; the chain walk; a directory walk does, and then chunk 0 arrived, the
    ; destination overwrote the records behind it, and chunk 1 came back off
    ; the wrong LBAs - the right NUMBER of bytes and the wrong ones, which is
    ; the one failure this row's checksum exists to catch.
    mov ax, KD_GATE_KB
    xor bx, bx
    call mem_claim_x
    jc .nomem
    mov [kd_buf], dx

    mov ax, [kd_clus]
    mov dx, [kd_len]            ; sectors = ceil(size / 512)
    add dx, 511
    mov cl, 9
    shr dx, cl
    push ds
    mov es, [kd_buf]
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
    mov es, [kd_buf]
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
kd_s_ok:     db 'ok', 13, 10, 0
kd_s_nomount: db 'MOUNT FAILED', 13, 10, 0
kd_s_nofile: db 'FILE NOT FOUND', 13, 10, 0
kd_s_noread: db 'CHAIN READ FAILED', 13, 10, 0
kd_s_nomem:  db 'NO ROOM FOR THE PAYLOAD', 13, 10, 0
kd_s_len:    db 'len ', 0
kd_s_sum:    db 'sum ', 0
kd_s_end:    db 'gate done', 13, 10, 0
kd_wanted:   db 'GATE.TXT', 0

section .bss
kd_len:      resw 1
kd_clus:     resw 1
kd_buf:      resw 1              ; the payload's own claim, NOT [kd_top]
section .text
%endif                          ; KD_GATE
