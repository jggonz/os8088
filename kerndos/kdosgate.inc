; =============================================================================
; kerndos/kdosgate.inc - wave 4's entry, now a LAUNCH BLOCK STAGER
;
; **kern_dos's REAL entry is `kdentry.inc`** and this no longer duplicates it.
; W4 shipped the whole of `dos_run`'s setup here, which was right while the
; handoff did not exist and became a second copy the day it did. So what is
; left is the ONE thing this gate is for: it stages a launch block out of its
; own constants and jumps to the real entry, where the stub stages one out of
; a running os8088 and jumps to exactly the same place.
;
; That makes the block's ABI testable on a machine with **no kernel, no
; handoff, no hard disk and no part table** - `python3 tests/kdos.py` is four
; seconds - and it is the reason the ABI is a block at all rather than a pile
; of pokes: two producers, one consumer, one gate.
;
; It is behind KD_GATE and no shipping path defines it.
; =============================================================================
%ifdef KD_GATE
section .text

kd_dos_entry:
    cli
    mov ax, KD_SEG              ; enough of a machine to fill a block in: the
    mov ds, ax                  ; real entry does this again for itself, and
    mov es, ax                  ; has to, because the stub arrives with no
    mov ax, LOW_SEG             ; segment of ours set at all
    mov ss, ax
    mov sp, KD_STACK
    sti
    cld

    ; --- THE BLOCK IS `.bss`, AND `.bss` IS NOT ZERO --------------------------
    ; `-f bin` emits nothing for a `nobits` section, so `kd_glb` arrives
    ; holding whatever RAM the loader left in it - and `kd_entry`, which zeroes
    ; every other byte of `.bss`, DELIBERATELY SKIPS this one, the block being
    ; "the one thing already written" by the time it runs. So the zeros the
    ; block's ABI is built on are THIS routine's to establish and nobody
    ; else's: `KDL_DPT`, `KDL_NVOL` and `KDLF_RAHSH` each spell *"I could not
    ; fill this"* as 0, which is a sentence a sender only says by writing it.
    ; The other producer never had to - the box's block is `rep movsw`'d whole
    ; out of an image the package loader zeroed (SPEC.md 21 step 5) - and this
    ; one is filled a field at a time into a buffer nothing cleared.
    ;
    ; **IT WAS INVISIBLE FOR AS LONG AS `.bss` HAPPENED TO LAND ON ZEROS, AND
    ; WHERE IT LANDS IS DECIDED BY THE BLOB'S LENGTH** (SPEC.md 96.38.4).
    ; `kd_glb` is at KD_SEG:0x767C = 0x7C7C, which is ON the boot sector at
    ; 0x7C00: while `kdboot.bin` was short of `KDL_DPT` the byte read 0 and
    ; `kd_entry` left `int 1Eh` alone; 138 bytes off the image slid the block
    ; down onto the loader's own code, `[kd_glb+KDL_DPT]` read 0x7C, and
    ; `kd_entry` copied eleven bytes of `kdboot.asm` into `dsk_dpt` and pointed
    ; the BIOS at them. Every floppy read then answered AH=09h and the gate
    ; said *"could not mount drive B"* about a disk that was perfectly
    ; readable - SPEC.md 18.92's failure, reached through the one door that
    ; was supposed to be closed by a field being zero.
    mov di, kd_glb
    mov cx, KDL_SIZE
    xor al, al
    rep stosb                   ; **AND BEFORE THE BANNER**: ES is ours and
                                ; `cld` is one line up, where after
                                ; `kd_puts` the only thing between the two
                                ; would be a ROM's `int 10h`

    mov si, kd_g_boot
    call kd_puts

    ; --- the block, as the handoff will stage it ------------------------------
    mov di, kd_glb
    mov word [di+KDL_MAGIC], KDL_SIG & 0xFFFF
    mov word [di+KDL_MAGIC+2], KDL_SIG >> 16
    mov word [di+KDL_VER], KDL_VER_NOW
    mov word [di+KDL_FLAGS], KDLF_REBOOT
    mov word [di+KDL_TLEN], KDL_MINE
    mov word [di+KDL_WAKE + 2], 0   ; no live resume on the W4 gate either -
                                    ; there is no image to come back to
    mov byte [di+KDL_NVOL], 0       ; ...and no volume table either, for the
                                    ; same reason: this gate knows the two
                                    ; floppies the static initialiser already
                                    ; has, and zero means KEEP IT (96.46)
    mov word [di+KDL_MOUBASE], 0    ; no mouse on the W4 gate: it never goes
                                    ; through hbm_dosrun, which is the only
                                    ; thing that knows the port (SPEC.md 96.45)

    ; **THE MACHINE'S OWN KB, OUT OF THE BDA** (0040:0013, which is `int 12h`'s
    ; own answer). The real handoff banks the same word for the same reason: it
    ; is the only place the size of conventional memory is written down, and a
    ; constant here would be a guess about somebody else's machine.
    push ds
    mov ax, 0x0040
    mov ds, ax
    mov ax, [0x0013]
    pop ds
    mov [di+KDL_CAP], ax
    mov word [di+KDL_UNIT], 1   ; B:, kerndos.asm's reason: A: carries the
                                ; loader and the blob raw and has no volume

    ; --- and the fields, which on this arm are two constants and five zeros ---
    ; The gather the BOX does is kdlaunch.inc's table walked the other way;
    ; here there is no box, so the two that matter are written by hand and the
    ; rest are the zeros the clear above put there. That clear is why this
    ; paragraph can say "the rest": it used to say the same thing about zeros
    ; nobody had written, which was true of the machine and not of the code.
    add di, KDL_BODY
    push di
    mov si, kd_g_prog           ; dos_name, the first row of the table
    mov cx, 16
    rep movsb
    pop di
    add di, 16 + DOS_ARGSZ + (DOS_ENVN * DOS_ENVBUF) + 32 + 2
    mov byte [di], 1            ; dos_vol: B:, matching KDL_UNIT above

    mov word [kd_lbp], kd_glb   ; ...and the pointer the stub would have read
    jmp kd_entry                ; out of the header at KD_H_LBP

kd_g_boot: db 13, 10, 'kern_dos gate: staging a launch block', 13, 10, 0
kd_g_prog: db 'KDHELLO.COM', 0, 0, 0, 0, 0

%endif                          ; KD_GATE
