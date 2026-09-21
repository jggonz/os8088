; =============================================================================
; os8088 - apps/pixelstein/pxstein.asm
;
; PIXELSTEIN 3D's LOADER - the IMAGE of PXSTEIN.O88, and the whole of what the
; kernel launches (SPEC.md 96.9, 20.12.10; apps/skies/csload.asm's shape).
;
; It reads the parts, tells the program what it cannot ask for itself, and
; asks the kernel to treat the program as the program. Then its region is
; freed and it is gone: what runs is apps/pixelstein/pxgame.asm, at PART 0,
; in the parts carve, with an instance and a window and a name of its own.
;
; FIVE PARTS (96.9):
;   0  the program, a whole .o88 image with its bss shipped inside it,
;      OP_COMP so the disk pays for the zeros of two 4KB maps and two
;      spotvis arrays as a run of nothing;
;   1  the level stream tools/pxslevel.py writes (96.7) - EAGER and plain:
;      ~1KB, three sectors of the run. The plan carried it lazy, but the
;      parts table dies with this image, so a lazy part needs a directory
;      the program reads back through OSAPI_FILE_READ_AT (what SKIES does
;      for a world) - worth it for the art, not for this;
;   2  PX_GENKB of scratch the program generates into (96.3): the four DDA
;      bodies and the column driver copied in, then BOTH resolution sets of
;      compiled scalers for the backend in force and their col2tex tables
;      (tools/pxsgen.py is the model; 40,087 bytes on CGA4, the widest
;      phase, plus 232 of bodies, 64 of driver and a 3KB draw queue).
;      OP_OPT: a machine that cannot spare it runs the bodies out of the
;      image and plays Flat;
;   3  PXA_BTKB for the byte-texture set px_bt_build transposes (96.4) -
;      15 materials x 2 shades x 1,024 texels. OP_OPT likewise;
;   4  the ART MASTERS as an LZ4 stream tools/pxsart.py packed (96.4) -
;      LAZY, because a lazy row is not in the eager run that 20.12.7 bounds
;      at 128 unpacked sectors, and NOT OP_COMP because a lazy row cannot be
;      (the two want the same zkb word): pxl_art below fetches it and expands
;      it through OSAPI_DECOMP into a claim of its own, csload's csl_art
;      instruction for instruction, and the program keeps that claim for the
;      session (the F toggle re-transposes from it). A refusal is survivable:
;      the game plays Flat. LAST, because os88pkg.py puts every lazy row
;      after the carve (20.12.4).
;
; IT MUST NOT CREATE A WINDOW (SPEC.md 20.12.10.6): its region is about to be
; freed, so a window whose W_SEG named it would far-call a dead claim on its
; first repaint. The program's window is the one the user sees.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Pixelstein 3D', pxl_entry, OS88_F_ICON | OS88_F_PARTS, OS88_STACK_DEFAULT

    OS88_ICON16                     ; the SAME icon the program carries (the
%include "pxicon.inc"               ; Disk window draws this one before the
    OS88_ICON16_END                 ; launch, the dock the program's after)

%include "os88parts.inc"
%include "pxlev.inc"                ; for PXL_NLEV alone: the level count is
                                    ; the generator's and read here rather
                                    ; than restated
%include "pxart.inc"                ; ...and PXA_SIZE / PXA_KB / PXA_BTKB, the
                                    ; art's numbers, read by both halves

PX_PART_BODY equ 0                  ; the program - a whole .o88 image
PX_PART_LEV  equ 1                  ; the level stream (96.7)
PX_PART_GEN  equ 2                  ; the scalers' scratch (96.3), optional
PX_PART_BT   equ 3                  ; the byte-texture set (96.4), optional
PX_PART_ART  equ 4                  ; the art stream (96.4), LAZY - and so
                                    ; LAST: a lazy row comes after every part
                                    ; of the carve (os88pkg.py's rule, SPEC.md
                                    ; 20.12.4)
PX_NPARTS    equ 5
PX_GENKB     equ 44                 ; part 2: tools/pxsgen.py --sizes reads
                                    ; 40,087 for the widest phase (CGA4) and
                                    ; pxgen.inc puts PXG_SCAL - 3,372 of
                                    ; bodies (232), driver (64), header (4)
                                    ; and queue (3,072) - in front of it:
                                    ; 43,459 of 45,056, 1,597 spare;
                                    ; tests/unit/t_pxsscale.py holds the
                                    ; model to this number and PXH_GENLEN
                                    ; below is the machine's own fence
                                    ; (px_gen_build)

; --- the handoff, at the head of the PROGRAM's bss (SPEC.md 96.9) -----------
; ONE PACKAGE, TWO SOURCES: apps/pixelstein/pxgame.asm declares these and this
; file is the other end of them. The kernel is not involved: it does not zero
; a part, which is the whole of what makes this work.
PXH_MAGIC  equ 0                    ; word: 'PX' - the loader ran
PXH_LEV    equ 2                    ; word: the level stream's segment
PXH_GEN    equ 4                    ; word: the scratch part's segment, 0 = refused
PXH_COLD   equ 6                    ; word: the cold part's segment (96.9), 0 = none
PXH_NLEV   equ 8                    ; word: levels in the stream
PXH_LEVLEN equ 10                   ; word: the level stream's length in bytes
                                    ; (the row's OP_R_LEN): the bound the
                                    ; program holds the stream's own lengths
                                    ; to (96.7)
PXH_ART    equ 12                   ; word: the expanded art masters' claim, 0 = none
PXH_BT     equ 14                   ; word: the byte-texture part's segment, 0 = refused
PXH_GENLEN equ 16                   ; word: the scratch part's length in bytes
                                    ; (PX_GENKB * 1024): the bound the
                                    ; program generates under (96.3)
PXH_SIZE   equ 18

LD_H_IMG   equ 8                    ; ...and the two header fields it reads
LD_H_BSS   equ 10                   ; them at, which are the FORMAT's

; -----------------------------------------------------------------------------
; pxl_art - fetch the art stream and expand it (SPEC.md 96.4; csload's csl_art)
; out: AX = the segment holding PXA_SIZE bytes of masters, or 0
; clobbers: BX, CX, DX, SI, DI, ES, flags
;
; The stream is a LAZY part and the masters are a claim, so this holds two of
; MEM_OWNER_MAX's eight for as long as it takes to decode - and gives one
; straight back (op_drop). Every refusal answers 0, which is the Flat rung
; the program has always been able to draw; op_fetch has already said why.
; The packed length is the ROW's (OP_R_LEN, csload's use of op_row), so the
; include carries no second copy of it.
; -----------------------------------------------------------------------------
pxl_art:
    mov al, PX_PART_ART
    call op_fetch                   ; claims and reads the stream (20.12.4)
    jc .none
    mov al, PX_PART_ART
    call op_seg                     ; AX = where it landed
    or ax, ax
    jz .none
    mov [pxl_zseg], ax
    mov al, PX_PART_ART
    call op_row                     ; SI -> the row: its length is the stream's
    mov cx, [si+OP_R_LEN]
    mov [pxl_zlen], cx
    mov ax, PXA_KB
    call OSAPI_MEM_CLAIM            ; DX = the masters' own claim
    jc .drop
    mov [pxl_aseg], dx
    push ds
    mov es, dx
    mov cx, [pxl_zlen]
    mov ds, [pxl_zseg]              ; DS:SI the stream, T word first...
    xor si, si
    xor di, di                      ; ...ES:0 where it goes, and DI = 0 is the
    xor bx, bx                      ; contract (SPEC.md 20.13.3). BX:DX is the
    mov dx, PXA_SIZE                ; EXACT output, 32 bits, and ours is one
    mov al, OSAPI_LZ_LZ4            ; word - so BX is zero and DX the size
    call OSAPI_DECOMP
    pop ds
    jc .free
    mov al, PX_PART_ART             ; the STREAM's claim goes back: the masters
    call op_drop                    ; are what we hand over
    mov ax, [pxl_aseg]
    ret
.free:
    mov dx, [pxl_aseg]              ; a decode this build cannot do
    call OSAPI_MEM_FREE
.drop:
    mov al, PX_PART_ART
    call op_drop
.none:
    xor ax, ax
    ret

; -----------------------------------------------------------------------------
; pxl_entry - the loader's entry proc (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the name of the file we
;      came out of, gfx lock NOT held
; out: BX = 0, CF clear - and the kernel re-homes instead of publishing us
; -----------------------------------------------------------------------------
pxl_entry:
    call op_load                    ; FIRST, for SPEC.md 20.2's reason: SI is
    jc .no                          ; an offset into the KERNEL's segment at a
                                    ; buffer the loader reuses on the next
                                    ; launch. A refusal is fatal: a body that
                                    ; did not arrive is not a plainer game
    xor ax, ax                      ; THE MASTERS ONLY FOR A LAUNCH THAT CAN
    mov [pxl_aseg], ax              ; USE THEM: px_texok is the AND of the
    mov al, PX_PART_GEN             ; scratch, the byte set and the art, so
    call op_seg                     ; with either optional part refused an
    or ax, ax                       ; 8 KB claim would be held all session
    jz .noart                       ; for a game that is on Flat by
    mov al, PX_PART_BT              ; construction - and claimed before the
    call op_seg                     ; program's own 16 KB shadow (review,
    or ax, ax                       ; wave 2). A lazy row never fetched costs
    jz .noart                       ; nothing
    call pxl_art                    ; AX = the expanded masters, or 0
    mov [pxl_aseg], ax
.noart:
    mov al, PX_PART_BODY
    call op_seg
    or ax, ax
    jz .no
    mov es, ax                      ; ES = where the program is
    mov di, [es:LD_H_IMG]           ; its bss begins here, which the part's
                                    ; own header says
    mov word [es:di+PXH_MAGIC], 'PX'
    mov al, PX_PART_LEV
    call op_seg
    mov [es:di+PXH_LEV], ax
    mov al, PX_PART_LEV
    call op_row                     ; SI -> the level row (csload.asm's use):
    mov ax, [si+OP_R_LEN]           ; its length is what bounds every length
    mov [es:di+PXH_LEVLEN], ax      ; READ OUT OF the stream (96.7)
    mov al, PX_PART_GEN             ; 0 when the optional row was refused:
    call op_seg                     ; op_seg answers 0 for a part that is not
    mov [es:di+PXH_GEN], ax         ; there, and the program falls back to
                                    ; its own copy of the bodies (96.2.1)
    mov al, PX_PART_BT
    call op_seg
    mov [es:di+PXH_BT], ax          ; ...and to the Flat rung without this
    mov word [es:di+PXH_GENLEN], PX_GENKB * 1024
    mov ax, [pxl_aseg]
    mov [es:di+PXH_ART], ax
    mov word [es:di+PXH_COLD], 0    ; no cold part yet (96.9)
    mov word [es:di+PXH_NLEV], PXL_NLEV

    ; --- the hand-over -------------------------------------------------------
    ; AX is what the kernel bounds the part's image + bss against, so it is
    ; OUR word for what is actually there (SPEC.md 20.12.10.4): the part is
    ; padded to image + bss, so its own two header fields ARE that length.
    mov ax, [es:LD_H_IMG]
    add ax, [es:LD_H_BSS]
    mov dx, es
    call OSAPI_PKG_REHOME
    jc .no
    xor bx, bx                      ; NO WINDOW: ours is the region that is
    clc                             ; about to be freed (SPEC.md 20.12.10.6)
    ret
.no:
    stc                             ; ...and the kernel tears down what exists.
    ret                             ; op_load has already said why in a toast

; --- the table, and the standard's own code after it (SPEC.md 20.12.3) ------
    OS88_PARTS_BEGIN PX_NPARTS
      OS88_PART OP_SEG,   OP_COMP   ; 0 THE PROGRAM
      OS88_PART OP_ASSET            ; 1 the level stream, eager and plain
      OS88_PART OP_SEG,   OP_ZERO | OP_OPT, PX_GENKB   ; 2 the scalers' scratch
      OS88_PART OP_ASSET, OP_ZERO | OP_OPT, PXA_BTKB   ; 3 the byte-texture set
      OS88_PART OP_ASSET, OP_LAZY   ; 4 the art stream: lazy, expanded above
    OS88_PARTS_END

    OS88_BSS OP_BSS + PXL_BSS
    OS88_IMAGE_END

pxl_zseg equ os88_image_end + OP_BSS + 0   ; the stream's claim, while it lasts
pxl_aseg equ os88_image_end + OP_BSS + 2   ; ...and the masters', handed over
pxl_zlen equ os88_image_end + OP_BSS + 4   ; the stream's packed length
PXL_BSS  equ 6
