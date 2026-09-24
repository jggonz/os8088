; =============================================================================
; os8088 - apps/pixelstein/pxstein.asm
;
; PIXELSTEIN 3D's LOADER - the IMAGE of PXSTEIN.O88, and the whole of what the
; kernel launches (SPEC.md 97.9, 20.12.10; apps/skies/csload.asm's shape).
;
; It reads the parts, tells the program what it cannot ask for itself, and
; asks the kernel to treat the program as the program. Then its region is
; freed and it is gone: what runs is apps/pixelstein/pxgame.asm, at PART 0,
; in the parts carve, with an instance and a window and a name of its own.
;
; FIVE PARTS (97.9):
;   0  the program, a whole .o88 image with its bss shipped inside it,
;      OP_COMP so the disk pays for the zeros of two 4KB maps and two
;      spotvis arrays as a run of nothing;
;   1  PX_GENKB of scratch the program generates into (97.3): the four DDA
;      bodies and the column driver copied in, then BOTH resolution sets of
;      compiled scalers for the backend in force and their col2tex tables
;      (tools/pxsgen.py is the model; 40,087 bytes on CGA4, the widest
;      phase, plus 232 of bodies, 64 of driver and a 3KB draw queue).
;      OP_OPT: a machine that cannot spare it runs the bodies out of the
;      image and plays Flat;
;   2  PXA_BTKB for the byte-texture set px_bt_build transposes (97.4) -
;      15 materials x 2 shades x 1,024 texels. OP_OPT likewise;
;   3  the level stream tools/pxslevel.py writes (97.7) - LAZY and plain
;      since wave 4, as the plan first had it: eight floors are 9.8 KB, 20
;      sectors, and with the HUD, the states and the scores in part 0 the
;      eager run would have been 111 + 20 = 131 unpacked sectors, past
;      the 128 20.12.7 allows (97.9). pxl_lev fetches it into a claim of its own and hands
;      that segment over - no directory is needed, because the loader does
;      the fetch while its table still exists (the reason wave 1 gave for
;      keeping it eager was a program-side lazy fetch, which this is not);
;   4  the ART MASTERS as an LZ4 stream tools/pxsart.py packed (97.4) -
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
PX_PART_GEN  equ 1                  ; the scalers' scratch (97.3), optional
PX_PART_BT   equ 2                  ; the byte-texture set (97.4), optional
PX_PART_LEV  equ 3                  ; the level stream (97.7), LAZY since wave 4
PX_PART_ART  equ 4                  ; the art stream (97.4), LAZY - and the two
                                    ; lazy rows LAST: a lazy row comes after
                                    ; every part of the carve (os88pkg.py's
                                    ; rule, SPEC.md 20.12.4)
PX_NPARTS    equ 5
; THE SPRITE SET IS A CLAIM OF ITS OWN, NOT A PART (97.6, 97.9): PXS_KB
; claimed below once the art has arrived, handed over as PXH_SPR. The first
; cut of wave 3 carried it as a fourth OP_OPT part - and OP_OPT is ALL OR
; NONE (SPEC.md 20.12), so 46 KB of sprites joined the 81 KB the Textured
; walls need, a machine with 90-150 KB free lost its textured walls to the
; sprites, and "the sprites on boxes, the walls textured" was a state the
; loader could not produce (review, wave 3). Claimed only when the largest
; free run would still hold the program's own 16 KB shadow after it
PXL_SHKB     equ 16                 ; the program's shadow (pxgame.asm's
                                    ; PX_SHKB), claimed after this in its
                                    ; entry proc: this claim must leave it
PXL_RESKB    equ (PXL_STREAM + 1023) / 1024 + 2 + PXL_SHKB
                                    ; THE RESERVE held across op_load: the
                                    ; level stream's fetch (PXL_STREAM,
                                    ; pxlev.inc's generated count, rounded
                                    ; up, plus a cluster of slack and
                                    ; op_fetch's transient KB - 2) and the
                                    ; shadow. DERIVED, so a ninth floor
                                    ; grows it (review, wave 6 r2; 28 KB
                                    ; today: 10 + 2 + 16)
PX_GENKB     equ 51                 ; part 1: tools/pxsgen.py --sizes reads
                                    ; 46,291 for the widest phase (CGA4) -
                                    ; both scaler sets, each scaler behind
                                    ; its 66-byte codeofs table (wave 3),
                                    ; and the col2tex blocks - and pxgen.inc
                                    ; puts PXG_SCAL - 4,430 of bodies (232),
                                    ; driver (96), header (6) and queue
                                    ; (4,096) - in front of it: 50,721 of
                                    ; 52,224, 1,503 spare; tests/unit/
                                    ; t_pxsscale.py holds the model to this
                                    ; number and PXH_GENLEN below is the
                                    ; machine's own fence (px_gen_build)

; --- the handoff, at the head of the PROGRAM's bss (SPEC.md 97.9) -----------
; ONE PACKAGE, TWO SOURCES: apps/pixelstein/pxgame.asm declares these and this
; file is the other end of them. The kernel is not involved: it does not zero
; a part, which is the whole of what makes this work.
PXH_MAGIC  equ 0                    ; word: 'PX' - the loader ran
PXH_LEV    equ 2                    ; word: the level stream's segment (its
                                    ; own claim since wave 4, not the carve)
PXH_GEN    equ 4                    ; word: the scratch part's segment, 0 = refused
PXH_COLD   equ 6                    ; word: the cold part's segment (97.9), 0 = none
PXH_NLEV   equ 8                    ; word: levels in the stream
PXH_LEVLEN equ 10                   ; word: the level stream's length in bytes
                                    ; (the row's OP_R_LEN): the bound the
                                    ; program holds the stream's own lengths
                                    ; to (97.7)
PXH_ART    equ 12                   ; word: the expanded art masters' claim, 0 = none
PXH_BT     equ 14                   ; word: the byte-texture part's segment, 0 = refused
PXH_GENLEN equ 16                   ; word: the scratch part's length in bytes
                                    ; (PX_GENKB * 1024): the bound the
                                    ; program generates under (97.3)
PXH_SPR    equ 18                   ; word: the sprite set's segment (97.6),
                                    ; 0 = refused
PXH_SIZE   equ 20

LD_H_IMG   equ 8                    ; ...and the two header fields it reads
LD_H_BSS   equ 10                   ; them at, which are the FORMAT's

; -----------------------------------------------------------------------------
; pxl_art - fetch the art stream and expand it (SPEC.md 97.4; csload's csl_art)
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
; pxl_lev - fetch the level stream (SPEC.md 97.9, wave 4): the lazy row read
;           into a claim op_fetch makes, KEPT - the stream is plain, so the
;           claim is what the program reads, and it becomes the slot's at
;           the re-home like the masters' claim. A refusal refuses the
;           launch: a game with no floors is not a plainer game
; out: AX = the segment, CF = 1 refused (op_fetch has said why)
; -----------------------------------------------------------------------------
pxl_lev:
    mov al, PX_PART_LEV
    call op_fetch
    jc .no
    mov al, PX_PART_LEV
    call op_seg
    or ax, ax
    jz .no
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; pxl_entry - the loader's entry proc (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, SI = the name of the file we
;      came out of, gfx lock NOT held
; out: BX = 0, CF clear - and the kernel re-homes instead of publishing us
; -----------------------------------------------------------------------------
pxl_entry:
    ; THE RESERVE (97.9; review, wave 6): op_load's optional parts are ALL OR
    ; NONE against the largest free run AS IT STANDS, and it knows nothing of
    ; the two claims a launch still has to make after it - the level stream
    ; (pxl_lev, which refuses the launch) and the program's 16 KB shadow. On
    ; a 256 KB 5150 the carve took all 140 KB, optional parts and all, and
    ; the level stream's fetch then answered "Not enough memory": the game
    ; did not open at all, where 97.9 promised it Flat. So those two are held
    ; in a claim of their own ACROSS op_load and handed back after it - the
    ; carve then gives up the optional parts on such a machine, and the
    ; bottom-up hole the reserve leaves is where the two claims land. No
    ; launch path touches SI's buffer in between (SPEC.md 20.2's reason is
    ; the NEXT launch), and every OSAPI slot preserves SI
    mov ax, PXL_RESKB
    call OSAPI_MEM_CLAIM            ; DX = the reserve, or nothing held (a
    jnc .res                        ; machine that small is op_load's to
    xor dx, dx                      ; refuse in its own words)
.res:
    push dx
    call op_load                    ; FIRST of the parts, for SPEC.md 20.2's
    pop dx                          ; reason: SI is an offset into the
    pushf                           ; KERNEL's segment at a buffer the loader
    or dx, dx                       ; reuses on the next launch. A refusal is
    jz .nores                       ; fatal: a body that did not arrive is not
    call OSAPI_MEM_FREE             ; a plainer game
.nores:
    popf
    jc .no
    call pxl_lev                    ; THE FLOORS FIRST: every launch needs
    jc .no                          ; them, the masters only a Textured one
    mov [pxl_lseg], ax
    xor ax, ax                      ; THE MASTERS ONLY FOR A LAUNCH THAT CAN
    mov [pxl_aseg], ax              ; USE THEM: px_texok is the AND of the
    mov [pxl_sseg], ax              ; (and no sprite set without them)
    mov al, PX_PART_GEN             ; scratch, the byte set and the art, so
    call op_seg                     ; with either optional part refused an
    or ax, ax                       ; 8 KB claim would be held all session
    jz .noart                       ; for a game that is on Flat by
    mov al, PX_PART_BT              ; construction - and claimed before the
    call op_seg                     ; program's own 16 KB shadow (review,
    or ax, ax                       ; wave 2). A lazy row never fetched costs
    jz .noart                       ; nothing
    mov al, PX_PART_ART             ; ...AND ONLY WHEN THE MASTERS, THE PACKED
    call op_row                     ; STREAM BESIDE THEM AND THE PROGRAM'S
    mov ax, [si+OP_R_LEN]           ; SHADOW AFTER THEM all fit the largest
    add ax, 2047                    ; run (review, wave 6): the masters kept
    mov cl, 10                      ; and the shadow refused would be a game
    shr ax, cl                      ; that cannot open, for the sake of
    add ax, PXA_KB + PXL_SHKB       ; textures (the stream's KB with a KB of
    mov dx, ax                      ; cluster slack and its rounding; DX,
    call OSAPI_MEM_AVAIL            ; since the answer is AX and BX)
    cmp ax, dx
    jb .noart
    call pxl_art                    ; AX = the expanded masters, or 0
    mov [pxl_aseg], ax
    or ax, ax
    jz .noart                       ; no masters: nothing to transpose
    call OSAPI_MEM_AVAIL            ; AX = the largest free run in KB
    cmp ax, PXS_KB + PXL_SHKB       ; the set AND the shadow after it, or
    jb .noart                       ; the sprites are boxes (97.6)
    mov ax, PXS_KB
    call OSAPI_MEM_CLAIM
    jc .noart
    mov [pxl_sseg], dx
.noart:
    mov al, PX_PART_BODY
    call op_seg
    or ax, ax
    jz .no
    mov es, ax                      ; ES = where the program is
    mov di, [es:LD_H_IMG]           ; its bss begins here, which the part's
                                    ; own header says
    mov word [es:di+PXH_MAGIC], 'PX'
    mov ax, [pxl_lseg]
    mov [es:di+PXH_LEV], ax
    mov al, PX_PART_LEV
    call op_row                     ; SI -> the level row (csload.asm's use):
    mov ax, [si+OP_R_LEN]           ; its length is what bounds every length
    mov [es:di+PXH_LEVLEN], ax      ; READ OUT OF the stream (97.7)
    mov al, PX_PART_GEN             ; 0 when the optional row was refused:
    call op_seg                     ; op_seg answers 0 for a part that is not
    mov [es:di+PXH_GEN], ax         ; there, and the program falls back to
                                    ; its own copy of the bodies (97.2.1)
    mov al, PX_PART_BT
    call op_seg
    mov [es:di+PXH_BT], ax          ; ...and to the Flat rung without this
    mov ax, [pxl_sseg]
    mov [es:di+PXH_SPR], ax         ; ...and to boxes for sprites without this
    mov word [es:di+PXH_GENLEN], PX_GENKB * 1024
    mov ax, [pxl_aseg]
    mov [es:di+PXH_ART], ax
    mov word [es:di+PXH_COLD], 0    ; no cold part yet (97.9)
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
      OS88_PART OP_SEG,   OP_ZERO | OP_OPT, PX_GENKB   ; 1 the scalers' scratch
      OS88_PART OP_ASSET, OP_ZERO | OP_OPT, PXA_BTKB   ; 2 the byte-texture set
      OS88_PART OP_ASSET, OP_LAZY   ; 3 the level stream: lazy, kept (pxl_lev)
      OS88_PART OP_ASSET, OP_LAZY   ; 4 the art stream: lazy, expanded above
    OS88_PARTS_END

    OS88_BSS OP_BSS + PXL_BSS
    OS88_IMAGE_END

pxl_zseg equ os88_image_end + OP_BSS + 0   ; the stream's claim, while it lasts
pxl_aseg equ os88_image_end + OP_BSS + 2   ; ...and the masters', handed over
pxl_zlen equ os88_image_end + OP_BSS + 4   ; the stream's packed length
pxl_sseg equ os88_image_end + OP_BSS + 6   ; the sprite set's claim, or 0
pxl_lseg equ os88_image_end + OP_BSS + 8   ; the level stream's claim
PXL_BSS  equ 10

; the program's greyed Textured caption is the sum of these three, held
; there by an %if on a restated PX_GENKB - and the restatement is held here
%if PX_GENKB != 51
%error "PX_GENKB moved: restate pxgame.asm's PX_GENKB_CAP and the greyed Textured caption"
%endif
