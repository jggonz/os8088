; =============================================================================
; os8088 - apps/scribe/scload.asm
;
; SCRIBE's LOADER - the IMAGE of SCRIBE.O88, and the whole of what the kernel
; launches (SPEC.md 95.8.8, 20.12.10).
;
; It reads the two parts and asks the kernel to treat part 0 as the program.
; Then its region is freed and it is gone: what runs is apps/scribe/scribe.asm,
; at part 0 of the parts carve, with an instance and a window and a name of
; its own and no idea any of this happened.
;
; IT RETIRES SCRIBE.OVL, and it is WORD's loader (apps/word/wdload.asm) one
; fork along, because the case is the same one. SCRIBE.OVL was the file formats
; and the picture decoders in a file of their own beside SCRIBE.O88, read at
; entry from the folder Scribe was launched from into a claim of their own -
; a file the user saw in the listing and never opened, and a pair a copy could
; separate. Part 1 is inside SCRIBE.O88; there is nothing to leave behind.
;
; And it is not merely the same file moved inside: part 1 lands at the top of
; the PROGRAM'S OWN SEGMENT, because part 0 is exactly SC_P1ORG long and
; op_load lays the carve out part after part. So the far vectors, the stamped
; segment and the build stamp the module needed are gone with it, and the
; region can move (SPEC.md 95.8.8, 66.6.1.1).
;
; IT MUST NOT CREATE A WINDOW (SPEC.md 20.12.10.6): its region is about to be
; freed, so a window whose W_SEG named it would far-call a dead claim on its
; first repaint. The program's window is the one the user sees.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SCRIBE', scl_entry, 1 | OS88_F_PARTS
                                ; bit 0 icon, and NOT bit 1 - Scribe declares
                                ; no association (SPEC.md 95.2), and this
                                ; header is the one the mount reads. The stack
                                ; class is the program's: the default, as
                                ; scribe.asm's header has it

%include "scicon.inc"           ; ...and the SAME icon the program carries: the
                                ; Disk window draws this one before the launch
                                ; and the dock draws the program's after it
%include "os88parts.inc"

SCL_PART_PROG equ 0             ; the program - scribe.asm's image + its bss
SCL_PART_TOP  equ 1             ; ...and the top of its segment (95.8.8)

LD_H_IMG   equ 8                ; the two header fields the kernel bounds the
LD_H_BSS   equ 10               ; hand-over by, which are the FORMAT's

; -----------------------------------------------------------------------------
; scl_entry - the loader's entry proc (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the name of the file we
;      came out of, gfx lock NOT held
; out: BX = 0, CF clear - and the kernel re-homes instead of publishing us
; -----------------------------------------------------------------------------
scl_entry:
    call op_load                    ; FIRST, for SPEC.md 20.2's reason: SI is
    jc .no                          ; an offset into the KERNEL's segment at a
                                    ; buffer the loader reuses on the next
                                    ; launch
    mov al, SCL_PART_PROG
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
    ; the failure is not a refusal but a near call into whatever the carve has
    ; at that offset - and it costs a few bytes of an image that is freed at
    ; once.
    push ax
    mov cl, 4
    shr ax, cl                      ; the region in paragraphs...
    add ax, dx                      ; ...past the program is where part 1 goes
    mov bx, ax
    mov al, SCL_PART_TOP
    call op_seg
    cmp ax, bx
    pop ax
    jne .no

    OS88_REHOME                     ; DX = the program; the KEEP form, so
                                    ; the carve is cut where PART 1 ends and
                                    ; not where the program does (SPEC.md
                                    ; 20.12.10.5)
    jc .no
    xor bx, bx                      ; NO WINDOW: ours is the region that is
    clc                             ; about to be freed (SPEC.md 20.12.10.6)
    ret
.no:
    stc                             ; ...and the kernel tears down what exists.
    ret                             ; op_load has already said why in a toast

; --- the table, and the standard's own code after it (SPEC.md 20.12.3) ------
    OS88_PARTS_BEGIN 2
      OS88_PART OP_SEG, OP_COMP     ; 0 THE PROGRAM: scribe.asm's image, its
                                    ;   bss shipped inside it because the
                                    ;   kernel does not zero a part
                                    ;   (20.12.10). The bss is zeros and LZ4
                                    ;   is best at those
      OS88_PART OP_SEG, OP_COMP     ; 1 THE TOP OF ITS SEGMENT: `.modc`,
                                    ;   assembled at SC_P1ORG - the file
                                    ;   formats and the picture decoders that
                                    ;   were SCRIBE.OVL. Eager, so it is in
                                    ;   the carve and lands right above part
                                    ;   0 - that adjacency is the whole design
    OS88_PARTS_END

    OS88_BSS OP_BSS
    OS88_IMAGE_END
