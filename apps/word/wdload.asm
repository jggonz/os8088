; =============================================================================
; os8088 - apps/word/wdload.asm
;
; WORD's LOADER - the IMAGE of WORD.O88, and the whole of what the kernel
; launches (SPEC.md 68.10, 20.12.10).
;
; It reads the two parts and asks the kernel to treat part 0 as the program.
; Then its region is freed and it is gone: what runs is apps/word/word.asm,
; at part 0 of the parts carve, with an instance and a window and a name of
; its own and no idea any of this happened.
;
; WHY IT EXISTS IS A NUMBER. `image + bss` is bounded by APP_MAX_SIZE - 61,440
; bytes, the REGION - and Word was at 61,425 of it with work still to do. The
; segment is 65,536, and the carve a parted package is read into may run to
; 65,024 (127 sectors, op_size's bound), so part 1 is the difference: code
; assembled at WD_P1ORG in word.asm's own segment, which op_load lays down
; exactly there because part 0 is exactly WD_P1ORG long. See the comment above
; `section .modc` in word.asm for the rest of the design.
;
; AND IT RETIRES WORD.OVL. That was code beside the package in a file of its
; own, read on demand from the folder Word was launched from - so a copy that
; took WORD.O88 and left WORD.OVL behind was a program whose module features
; refused. Part 1 is inside WORD.O88; there is nothing to leave behind.
;
; IT MUST NOT CREATE A WINDOW (SPEC.md 20.12.10.6): its region is about to be
; freed, so a window whose W_SEG named it would far-call a dead claim on its
; first repaint. The program's window is the one the user sees.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'WORD', wdl_entry, 3 | OS88_F_PARTS, OS88_STACK_256
                                ; bit 0 icon, bit 1 the .DOC association -
                                ; this header is the one the mount reads, so
                                ; it carries both. The stack class is the
                                ; program's (word.asm says why 256)

%include "wdicon.inc"           ; ...and the SAME icon the program carries: the
                                ; Disk window draws this one before the launch
                                ; and the dock draws the program's after it
%include "os88parts.inc"

WDL_PART_PROG equ 0             ; the program - word.asm's image + its bss
WDL_PART_TOP  equ 1             ; ...and the top of its segment (68.10)

LD_H_IMG   equ 8                ; the two header fields the kernel bounds the
LD_H_BSS   equ 10               ; hand-over by, which are the FORMAT's

; -----------------------------------------------------------------------------
; wdl_entry - the loader's entry proc (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the name of the file we
;      came out of, gfx lock NOT held
; out: BX = 0, CF clear - and the kernel re-homes instead of publishing us
;
; THE DOCUMENT IS NOT OURS TO TOUCH. A .DOC double-clicked on the desktop
; launches this image with the name in OSAPI_ARG_FILE, and that is
; READ-AND-CLEAR - so the loader never asks, and the program's own wd_arg
; finds it where the launch left it.
; -----------------------------------------------------------------------------
wdl_entry:
    call op_load                    ; FIRST, for SPEC.md 20.2's reason: SI is
    jc .no                          ; an offset into the KERNEL's segment at a
                                    ; buffer the loader reuses on the next
                                    ; launch
    mov al, WDL_PART_PROG
    call op_seg
    or ax, ax
    jz .no
    mov dx, ax                      ; DX = where the program is
    mov es, ax
    mov ax, [es:LD_H_IMG]           ; AX = image + bss, the program's word for
    add ax, [es:LD_H_BSS]           ; its region - and where it ASSEMBLED part 1

    ; --- part 1 must be where part 0's code will look for it ----------------
    ; op_load lays the run out at roundup512(len) a part, and part 0 is image
    ; + bss long, so this holds by construction. It is checked anyway because
    ; the failure is not a refusal but a jump into whatever the carve has at
    ; that offset - and it costs a few bytes of an image that is freed at once.
    push ax
    mov cl, 4
    shr ax, cl                      ; the region in paragraphs...
    add ax, dx                      ; ...past the program is where part 1 goes
    mov bx, ax
    mov al, WDL_PART_TOP
    call op_seg
    cmp ax, bx
    pop ax
    jne .no

    call OSAPI_PKG_REHOME           ; DX = the program, AX = image + bss
    jc .no
    xor bx, bx                      ; NO WINDOW: ours is the region that is
    clc                             ; about to be freed (SPEC.md 20.12.10.6)
    ret
.no:
    stc                             ; ...and the kernel tears down what exists.
    ret                             ; op_load has already said why in a toast

; --- the table, and the standard's own code after it (SPEC.md 20.12.3) ------
    OS88_PARTS_BEGIN 2
      OS88_PART OP_SEG, OP_COMP     ; 0 THE PROGRAM: word.asm's image, its bss
                                    ;   shipped inside it because the kernel
                                    ;   does not zero a part (20.12.10). The
                                    ;   bss is zeros and LZ4 is best at those
      OS88_PART OP_SEG, OP_COMP     ; 1 THE TOP OF ITS SEGMENT: `.modc`,
                                    ;   assembled at WD_P1ORG. Eager, so it is
                                    ;   in the carve and lands right above part
                                    ;   0 - that adjacency is the whole design
    OS88_PARTS_END

    OS88_BSS OP_BSS
    OS88_IMAGE_END
