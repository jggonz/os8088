; =============================================================================
; os8088 - apps/dos/dosload.asm
;
; **THE DOS BOX'S LOADER - the IMAGE of DOS.O88, and the whole of what the
; kernel launches** (SPEC.md 96.44.4, 20.12.10). It reads the box out of part
; 0, tells it where `kern_dos` sits in the file, and asks the kernel to treat
; the box as the program. Then its region is freed and it is gone: what runs
; is `apps/dos/dos.asm`, at part 0, with a window and an instance and no idea
; any of this happened.
;
; **WHY IT EXISTS IS A NUMBER AND THE NUMBER IS A LAUNCH TIME.**
; `tools/os88pkg.py` refuses `--compress` beside parts - a part's offset is
; measured from the start of the file and the table that holds it is INSIDE
; the image, so compressing the image and laying out its parts are circular -
; and 96.40.3 measured what that cost when arm 3 first shipped: the image went
; RAW, `DOS.O88` 26,723 -> 57,272 bytes, and the same click took **+932 ms
; (35%)** because the launch read 21 more sectors. Twelve soak rows went red
; at once, none of them for a reason of its own.
;
; The four-piece shape (docs/plans/KERN-DOS-PLAN.md 4.1.3.1) is the way out and
; it is not a workaround: **only the IMAGE has to be raw, and the image can be
; a kilobyte.** Everything heavy becomes a part, and a part may be `OP_COMP`.
;
; **PART 1 IS LAZY, AND IT HAS TO BE** - it is not a style choice.  `op_load`
; claims and reads every EAGER part into one carve, and `op_size` refuses a
; carve of 64KB or more; part 0 unpacks to ~45KB and `kern_dos` to ~29KB, so
; an eager pair would be refused outright before a sector was read. It is also
; the wrong thing to want: nothing ever `op_fetch`es this row, because 4.1.1's
; whole argument is that the handoff walks the part's bytes into EXTENTS while
; the file layer is alive and a stub reads them with `int 13h` - by then the
; heap has been given away and there is nowhere to `op_load` to. So what the
; box needs off this row is three numbers, and the loader copies them into the
; box's own bss before it disappears.
;
; **`OP_COMP` BESIDE `OP_LAZY` WAS REFUSED AND IS NOT ANY MORE** (SPEC.md
; 20.12.7.4). The refusal was right about the mechanism - one `zkb` word, two
; meanings - and backwards about the conclusion: every lazy row in this tree
; is a compressed stream, and each had grown a packer and an expander outside
; the standard to get there. The word is shared in TIME now - the packed
; length until the fetch, the segment after - and this row never fetches at
; all, so the second meaning never arrives. The stub needs no other figure:
; `kds_expand` takes a byte count and runs to the end of the stream, and
; `KDS_ULEN` is carried and never read.
;
; IT MUST NOT CREATE A WINDOW (SPEC.md 20.12.10.6): its region is about to be
; freed, so a window whose `W_SEG` named it would far-call a dead claim on its
; first repaint. The box's window is the one the user sees.
; =============================================================================
cpu 8086
bits 16

%include "os88api.inc"

    OS88_HEADER 'DOS', dsl_entry, 3 | OS88_F_GLYPH | OS88_F_PARTS

%include "dosicon.inc"          ; ...and the SAME icon, association block and
                                ; document glyph the box carries: all are read out
                                ; of the IMAGE - the Disk window draws this
                                ; one before the launch, and `assoc.inc` reads
                                ; the three extensions off it to decide that a
                                ; .COM is ours at all

%include "doscall.inc"              ; CORE_ORG - where the core has to land
%include "os88parts.inc"

DOS_PART_BOX  equ 0             ; the box - a whole .o88 image (20.12.10)
DOS_PART_CORE equ 1             ; ...the INT 21h core, which goes INSIDE it
DOS_PART_KD   equ 2             ; ...and kern_dos, which nothing here reads

; --- the handoff, at the head of the BOX's bss (SPEC.md 20.12.10.2) ---------
; ONE PACKAGE, TWO SOURCES: `apps/dos/dos.asm` declares these and this file is
; the other end of them. The kernel is not involved and has no opinion; it
; does not zero a part, which is the whole of what makes this work.
;
; IT IS AN `OP_ROW` AND NOT A STRUCT OF OUR OWN, deliberately: the box already
; read `[dos_kdrow + OP_R_OFF]` out of the part table when the table was in
; its own image, so copying the row VERBATIM leaves all four of its read sites
; spelled exactly as they were and moves only the base.
DSLH_KDROW   equ 0              ; OP_ROW bytes, copied as they lie
DSLH_COREROW equ OP_ROW         ; ...and the CORE's beside it (SPEC.md 96.44.5)
DSLH_SIZE    equ OP_ROW * 2

LD_H_IMG    equ 8               ; ...and the two header fields this file reads
LD_H_BSS    equ 10              ; them at, which are the FORMAT's and not ours

; -----------------------------------------------------------------------------
; dsl_row - copy part AL's table row to ES:DI (SPEC.md 96.44.5)
; in:  AL = the part, ES:DI = where it goes
; out: nothing; DI past the row
; clobbers: AX, CX, SI, DI, flags
; -----------------------------------------------------------------------------
dsl_row:
    call op_row                     ; SI -> the table row, AX preserved
; --- dsl_cpy: the same copy from a row we already hold ----------------------
; in:  DS:SI = the row, ES:DI = where it goes
; **BYTE BY BYTE and not `movsw`**: ES is the destination and DS is ours, so
; the string form would want both set, and the stash below is read with ES
; pointing at the BOX.
dsl_cpy:
    mov cx, OP_ROW
.b:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    dec cx
    jnz .b
    ret

; -----------------------------------------------------------------------------
; dsl_core - put the INT 21h core into the hole reserved for it (SPEC.md 96.44.5)
; in:  DX = the box's segment, the parts already loaded
; out: CF=0; CF=1 = it could not be had, and op_fetch has said why
; clobbers: AX, BX, CX, SI, DI, ES, flags. DX preserved.
;
; **op_load CANNOT PUT IT THERE**, which is why this proc exists. The carve
; lays its parts out one after another, and the core has to land at CORE_ORG
; *inside* the box's own segment - the one address both hosts agree on, which
; is what makes every `call dos_load` in the box a near call into the table
; (apps/dos/doscall.inc). So the core is a LAZY part: op_fetch claims it, reads
; it and expands it somewhere of its own, this copies it into place, and
; op_drop gives the claim straight back.
;
; THE PAIRING IS WHAT SPEC.md 20.12.7.4 HAD TO UNREFUSE. A lazy row could not
; be OP_COMP until that section, so the choice here would have been a raw
; 14.5KB part or a packer of this package's own - which is the third time a
; package would have written one.
;
; THE COPY IS ~41 ms on a 4.77MHz 8088 (`rep movsw` at 13.3 cycles a byte,
; PERFORMANCE.md Set 117.2) and it happens once, at a launch that is already
; reading 40KB off a floppy. The core's bss needs no zeroing: it is inside the
; box's own image, which ships those bytes as zeros.
; -----------------------------------------------------------------------------
dsl_core:
    mov al, DOS_PART_CORE
    call op_fetch                   ; claims, reads and expands it (20.12.4)
    jc .no
    mov al, DOS_PART_CORE
    call op_row                     ; SI -> the row, for its unpacked length
    mov cx, [si+OP_R_LEN]
    inc cx
    shr cx, 1                       ; CX = words, rounded up
    mov al, DOS_PART_CORE
    call op_seg                     ; AX = where op_fetch put it
    or ax, ax
    jz .no
    push ax
    ; **THE BOX'S SEGMENT IS READ HERE AND NOT BY THE CALLER**, because
    ; op_fetch CLAIMS - and a claim may compact, which moves the carve the box
    ; is sitting in (SPEC.md 66.4). Read before the fetch, it is a segment the
    ; heap has since moved out from under, and the copy below lands on whoever
    ; owns those paragraphs now. It cost a launch that reported SUCCESS and put
    ; up no window, with 14.5KB written into the middle of nothing.
    mov al, DOS_PART_BOX
    call op_seg
    or ax, ax
    jz .nopop
    mov es, ax                      ; ES:DI = the hole, at the one offset both
    mov di, CORE_ORG                ; hosts agree on
    pop ax
    push ds
    mov ds, ax                      ; DS:SI = the core, where op_fetch put it
    xor si, si
    cld
    rep movsw
    pop ds
    mov al, DOS_PART_CORE
    call op_drop                    ; ...and the claim goes straight back
    clc
    ret
.nopop:
    pop ax
.no:
    stc
    ret
; -----------------------------------------------------------------------------
; dsl_entry - the package entry proc (SPEC.md 20.2)
; in:  SI = the launched file's name in KERNEL_SEG, ES = KERNEL_SEG
; out: CF=0 and BX = 0 (no window of ours); CF=1 = the launch is torn down
; -----------------------------------------------------------------------------
dsl_entry:
    call op_load                    ; sizes first and reads nothing if it will
    jc .no                          ; not fit (20.12); a toast has said why

    ; --- THE CORE'S ROW IS TAKEN BEFORE IT IS SPENT (SPEC.md 20.12.7.4) ----
    ; `dsl_core` below FETCHES this row and drops it again, and a dropped
    ; compressed row's `zkb` is `OP_SPENT` rather than its packed length -
    ; which is deliberate over there and fatal here, because the handoff needs
    ; that exact figure to expand the core after the heap is gone (96.44.5.4).
    ; Copied afterwards it is 0xFFFF, and the stub reads 64KB of rubble.
    push ds
    pop es
    mov di, dsl_crow
    mov al, DOS_PART_CORE
    call dsl_row

    call dsl_core                   ; the core, into the hole inside the box -
    jc .no                          ; and it CLAIMS, so nothing may hold the
                                    ; box's segment across it

    mov al, DOS_PART_BOX
    call op_seg                     ; ...read AFTER every claim this proc makes
    or ax, ax
    jz .no
    mov dx, ax                      ; DX = where the box is

    ; --- the row, into the head of the box's bss ---------------------------
    mov es, dx
    mov di, [es:LD_H_IMG]           ; the bss begins here, which the part's own
    add di, DSLH_KDROW              ; header says
    mov al, DOS_PART_KD
    call dsl_row                    ; ...kern_dos's row
    mov di, [es:LD_H_IMG]
    add di, DSLH_COREROW            ; **AND THE CORE'S**, because the stub reads
    mov si, dsl_crow                ; BOTH on the way to arm 3 (SPEC.md
    call dsl_cpy                    ; 96.44.5.4): kern_dos's image has a hole
                                    ; where the core goes, and only this image
                                    ; knows where either one sits in the file.
                                    ; Out of the stash and not the table: the
                                    ; table's copy has been spent by now

    ; --- and the hand-over --------------------------------------------------
    ; AX is what the kernel bounds the part's image + bss against, so it is OUR
    ; word for what is actually there (SPEC.md 20.12.10.4). The part is padded
    ; to image + bss, so its own two header fields ARE that length - said by
    ; adding them rather than by a constant this file would have to keep in
    ; step with the other one.
    mov ax, [es:LD_H_IMG]
    add ax, [es:LD_H_BSS]
    call OSAPI_PKG_REHOME
    jc .no
    xor bx, bx                      ; NO WINDOW: ours is the region that is
    clc                             ; about to be freed (SPEC.md 20.12.10.6)
    ret
.no:
    stc                             ; ...and the kernel tears down what exists.
    ret                             ; op_load has already said why in a toast

; --- the table, and the standard's own code after it (SPEC.md 20.12.3) ------
    OS88_PARTS_BEGIN 3
      OS88_PART OP_SEG,   OP_COMP   ; 0 THE BOX: a whole .o88 image, its bss
                                    ;   shipped inside it because the kernel
                                    ;   does not zero a part (20.12.10).
                                    ;   OP_COMP is the whole point of this
                                    ;   file - 11,839 of those bytes are the
                                    ;   bss, and a run of zeros is what LZ4 is
                                    ;   best at
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY
                                    ; 1 THE INT 21h CORE, lazy because it does
                                    ;   not belong in the carve - dsl_core
                                    ;   copies it into the hole at CORE_ORG
                                    ;   inside part 0 and drops it again
      OS88_PART OP_ASSET, OP_COMP | OP_LAZY
                                    ; 2 kern_dos. LAZY because op_size would
                                    ;   refuse the pair eagerly (the header
                                    ;   above), and never fetched at all
                                    ;   because the handoff reads it by extent
                                    ;   list with the heap already gone - what
                                    ;   the box wants off this row is three
                                    ;   numbers. COMP because it is 29KB of a
                                    ;   360KB system disk otherwise, and the
                                    ;   pairing is what SPEC.md 20.12.7.4 had
                                    ;   to unrefuse
    OS88_PARTS_END

    OS88_BSS OP_BSS + DSL_BSS
    OS88_IMAGE_END

dsl_crow equ os88_image_end + OP_BSS    ; the CORE row, banked before the fetch
DSL_BSS  equ OP_ROW                     ; that spends it
