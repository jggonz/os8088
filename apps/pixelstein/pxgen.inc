; =============================================================================
; os8088 - apps/pixelstein/pxgen.inc
;
; THE STATIC LADDERS and THE GENERATOR (SPEC.md 97.3).
;
; Two Duff ladders, in the image because `row(r) = r * 80` is known at
; assembly time: eighty `mov [di + 256 + r*80], reg` from row 79 down to row
; 0 and a near ret. Entering at store (80 - n) draws the LAST n stores - rows
; n-1..0 - and a DI biased by the first row's offset lands them on any range,
; so one ladder is the ceiling, the floor and the wall. The tone is a PAIR:
; even rows store one register and odd rows the other, so a 1bpp backend's
; dither is a checkerboard, and px_lad swaps the pair for a range that starts
; on an odd row. The 256 bias makes every store the 4-byte disp16 form the
; bench priced (rows 0 and 1 would otherwise assemble short).
;
; THE GENERATOR (wave 2) writes the scratch part the loader carved (97.9's
; PART 1 - the indices here are os88pkg's, the ones the loader's table and
; PXH_GEN name; the plan's "part 3" is this one) - and tools/pxsgen.py is
; its model, byte for byte, which tests/pxsscale.py reads back off the guest
; and diffs:
;
;   PXG_BODIES  the four quadrant bodies of pxcast.inc, copied (97.2.1);
;   PXG_DRV     the column driver, copied from px_drv_tpl below: the frame's
;               ONE far call into the part, which walks the DRAW QUEUE and
;               near-calls each entry's scaler with SI = its texture column
;               and DI = its column base;
;   PXG_QTEX    the queue's header - ES for the driver's wall pass (part 2,
;               the byte textures), ES for its SPRITE pass (PXG_QSPR: part
;               4, the sprite set, wave 3) and the queue's end - and PXG_Q
;               the queue itself, PXG_QN entries of (scaler, SI, DI, patch),
;               written by px_compose (pxcomp.inc) for the walls and by
;               px_sprites (pxspr.inc) for the posts and the weapon. THE
;               PATCH WORD is the one patch site in part 1 (97.3, 97.6): a
;               sprite post enters a scaler at texel v0's load and must
;               stop at texel v1's, so the driver writes a near `ret`
;               (0xC3) over the first byte of that load - the `es:` prefix,
;               0x26, which is what every texel load begins with - calls,
;               and PUTS THE PREFIX BACK before it tests anything. A wall
;               entry carries 0 and takes the plain call;
;   PXG_SCAL    the generated half: the Full set's 47 scalers, the Low-res
;               set's, then a col2tex table per scaler per set. A scaler for
;               height h is one texel run per texel that covers a row ON THE
;               VIEW - `mov al, [es:si + v]`, the even rows' stores, the odd-
;               row phase (the pixel format's: `ror al, 1` x 2 on CGA4, one
;               `ror al, cl` with CL = 3 on Hercules and WIN1, nothing on
;               C160 and Mode X), the odd rows' stores - and a near ret; the
;               Low-res set stores a WORD a row through `mov ah, al` before
;               each parity's stores. Rows off the view emit no store, so a
;               wall NEVER patches; a height over 120 clamps to the 120-row
;               scaler, and every height 1..120 aliases DOWN to the nearest
;               generated one through px_sctab and the cast's px_hq.
;               EVERY SCALER IS PRECEDED BY ITS codeofs TABLE (wave 3, graft
;               2's companion): 33 words, codeofs[v] = the offset in part 1
;               of the load of the first EMITTED texel at or after v, or 0
;               when no texel from v on emits (the post walk enters at
;               codeofs[v0] and patches codeofs[v1]; a 0 entry means "draw
;               nothing" and "no patch" respectively) - at px_sctab[h] -
;               PXG_COTSZ, so it needs no directory of its own.
;
; THE EMISSION IS BOUNDED BY THE PART (review, wave 2): the loader hands the
; part's length over (PXH_GENLEN) and px_gen_build refuses to start a
; scaler or a col2tex block within PXG_EMITMAX of it - the largest single
; emission - so a phase that outgrows the part walks the Textured rung off
; the ladder FOR THAT PHASE CLASS (px_genbad, the item greyed; a Mode change
; to a class that fits retries) instead of off the end of a heap claim,
; silently, on the target only. PXG_EMITMAX is derived from PXG_PHMAX in
; pxgame.asm, so a longer phase fails to assemble rather than slipping under
; the fence. tests/unit/t_pxsscale.py holds the model to PX_GENKB on the
; host; this is the machine's own fence.
;
; The bodies are copied where they were in wave 1 (offset 0, so the four
; patch offsets and px_qoff are as they were) and run in the image when the
; part was refused; then there is no queue, no scaler set, and the Textured
; rung is not on offer (px_texok = 0).
; =============================================================================

; --- the byte ladder: BL on even rows, BH on odd ------------------------------
px_ladb:
%assign PXR PX_ROWS - 1
%rep PX_ROWS
%if PXR & 1
    mov [di + 256 + PXR * PX_STRIDE], bh
%else
    mov [di + 256 + PXR * PX_STRIDE], bl
%endif
%assign PXR PXR - 1
%endrep
    ret

; --- the word ladder (Resolution: Low res): AX on even rows, DX on odd -------
px_ladw:
%assign PXR PX_ROWS - 1
%rep PX_ROWS
%if PXR & 1
    mov [di + 256 + PXR * PX_STRIDE], dx
%else
    mov [di + 256 + PXR * PX_STRIDE], ax
%endif
%assign PXR PXR - 1
%endrep
    ret
PX_LADSTEP  equ 4                   ; bytes a store, both ladders
; ...AND THE ASSEMBLER CHECKS IT (pxcast.inc's PXB_SIZE guard, the same
; hazard): px_lad enters a ladder at (PX_ROWS - n) * PX_LADSTEP by a computed
; jmp, which is right only while EVERY store is the 4-byte disp16 form. The
; +256 bias is what keeps rows 0 and 1 out of disp8, and a change to the
; bias, PX_STRIDE or PX_ROWS that let one store assemble short would land
; every entry above it inside an instruction - nothing fails at assembly and
; nothing fails at the first frame. Zero bytes, and the comment above stops
; being something the reader is asked to believe.
%if (px_ladw - px_ladb) != PX_ROWS * PX_LADSTEP + 1
%error "px_ladb is not PX_ROWS stores of PX_LADSTEP bytes plus a ret"
%endif
%if ($ - px_ladw) != PX_ROWS * PX_LADSTEP + 1
%error "px_ladw is not PX_ROWS stores of PX_LADSTEP bytes plus a ret"
%endif
%if PX_LADSTEP != 4
%error "px_lad scales the entry by two shifts: PX_LADSTEP is 4 or px_lad changes"
%endif

; r * 80 for r = 0..79: the ladder entry's DI bias, a table rather than two
; shifts and an add per call (SPEC.md 85.3.4's tk_rowoff)
px_row80:
%assign PXR 0
%rep PX_ROWS
    dw PXR * PX_STRIDE
%assign PXR PXR + 1
%endrep

; -----------------------------------------------------------------------------
; px_lad - n rows of one tone pair down a column (DS = the destination)
; in:  AL = the first row, CX = the count (1..80), BX = the tone pair (BL the
;      even-row byte, BH the odd), [cs:px_cdi] = the column's base byte - 256
; clobbers AX, BX, DX, SI, DI
; -----------------------------------------------------------------------------
px_lad:
%ifdef PXPROBE
    inc word [cs:px_pr_lad]
%endif
    xor ah, ah
    mov si, ax
    shl si, 1
    mov di, [cs:px_row80 + si]
    add di, [cs:px_cdi]
    test al, 1
    jz .even
    xchg bl, bh                     ; the pair follows the row's parity
.even:
    mov si, PX_ROWS
    sub si, cx
    shl si, 1
    shl si, 1                       ; * PX_LADSTEP (4, asserted above)
    add si, [cs:px_ladp]            ; px_ladb or px_ladw: the resolution's
    cmp byte [cs:px_bpc], 1
    jne .word
    jmp si                          ; the ladder's ret is ours
.word:
    mov ah, bl
    mov al, bl                      ; AX = the even byte, twice
    mov dh, bh
    mov dl, bh                      ; DX = the odd
    jmp si

; =============================================================================
; THE SCRATCH PART'S LAYOUT (97.3) - constants, because the driver template
; below names them and is copied verbatim
; =============================================================================
PXG_BODIES  equ 0                   ; the four bodies, PXB_ALL bytes
PXG_DRV     equ PXB_ALL             ; the driver, PXG_DRVMAX reserved
PXG_DRVMAX  equ 96
PXG_QTEX    equ PXG_DRV + PXG_DRVMAX    ; word: the wall pass's ES (part 2)
PXG_QSPR    equ PXG_QTEX + 2        ; word: the sprite pass's ES (PXH_SPR)
PXG_QEND    equ PXG_QSPR + 2        ; word: one past the queue's last entry
PXG_Q       equ PXG_QEND + 2        ; the draw queue
PXG_QN      equ 512                 ; ...entries: 80 walls, or the posts and
                                    ; the weapon (the queue is emptied
                                    ; between the two passes, 97.6)
PXG_QSZ     equ 8                   ; (scaler offset, SI, DI, patch offset)
PXG_QLAST   equ PXG_Q + (PXG_QN - 1) * PXG_QSZ  ; the last entry a writer
                                    ; may fill (px_spr_post stops here)
PXG_SCAL    equ PXG_Q + PXG_QN * PXG_QSZ ; the generated half begins here
PXG_HMAX    equ 120                 ; the tallest scaler: 1.5 x the view
PXG_NSC     equ 47                  ; scalers a set
PXG_DIRSZ   equ (PXG_HMAX + 1) * 2  ; a directory: a word per height 0..120
PXG_COTSZ   equ (PXA_TEX + 1) * 2   ; a scaler's codeofs table: 33 words,
                                    ; right before it (97.3)
PXG_PHMAX   equ 4                   ; the longest odd-row phase, bytes: every
                                    ; px_phase row's count is held under it
                                    ; below, and pxgame.asm derives the fence
                                    ; from it after pxart.inc
PXG_EMITMAX equ 768                 ; the most ONE emission can be: the
                                    ; tallest Low-res scaler is 32 loads x 4
                                    ; + 80 stores x 4 + 32 phases x PXG_PHMAX
                                    ; + 64 `mov ah, al` x 2 + a ret = 705,
                                    ; and a col2tex block 1 + 33 (h = 120:
                                    ; w = (120 * 71 + 128) >> 8 = 33). The
                                    ; codeofs table before a scaler is a
                                    ; separate 66-byte emission under the
                                    ; same fence

; -----------------------------------------------------------------------------
; px_drv_tpl - THE COLUMN DRIVER, copied into part 1 at PXG_DRV and entered
;              by ONE far call a pass from px_compose (the walls, ES = part
;              2) and px_sprites (the posts and the weapon, through the
;              second entry PXG_DRV2, ES = the sprite claim - pxspr.inc), DS = the
;              destination both times. Every reference here is either
;              relative (the jumps) or an absolute offset in PART 1 (the cs:
;              words), so the copy runs where it lands. No register
;              preserved (SPEC.md 85.3.4). CL = 3 is the Hercules/WIN1
;              phase's rotate count and costs nothing elsewhere; the queue's
;              end is read ONCE into DX - a 4-byte cs: read a column was ~25
;              clk over a register compare, x 32..80 a frame, and the queue
;              is longer once the posts share it (review, wave 2).
;
; THE ONE PATCH SITE (97.3, 97.6): an entry whose fourth word is not 0 names
; the texel load a post must STOP at; a near ret goes over its `es:` prefix
; for the length of the call and the prefix goes back before the next
; compare - so a fault path cannot leave a scaler cut short (the review's
; rule: restored in the same routine, before any jc). The byte under it is
; 0x26 by construction: codeofs never names anything but a load.
; -----------------------------------------------------------------------------
px_drv_tpl:
    mov es, [cs:PXG_QTEX]           ; the walls: ES = the byte textures
    jmp short .go
.spr:                               ; the second entry (PXG_DRV2)
    mov es, [cs:PXG_QSPR]           ; the sprites: ES = the sprite set
.go:
    mov cl, 3
    mov dx, [cs:PXG_QEND]
    mov bx, PXG_Q
    cmp bx, dx
    jae .done
.col:
    mov si, [cs:bx + 2]
    mov di, [cs:bx + 4]
    mov bp, [cs:bx + 6]             ; the patch, or 0
    or bp, bp
    jnz .post
    call word [cs:bx]               ; the scaler: SI/DI its, ES the textures
.next:
    add bx, PXG_QSZ
    cmp bx, dx
    jb .col
.done:
    retf
.post:
    mov byte [cs:bp], 0xC3          ; a near ret over the stop texel's load
    call word [cs:bx]
    mov byte [cs:bp], 0x26          ; ...and its es: prefix back, FIRST
    jmp short .next
px_drv_end:
PXG_DRVSZ   equ px_drv_end - px_drv_tpl
PXG_DRV2    equ PXG_DRV + (px_drv_tpl.spr - px_drv_tpl)  ; the sprite pass's entry
%if PXG_DRVSZ > PXG_DRVMAX
%error "the column driver outgrew the room reserved for it in part 1"
%endif

; the 47 generated heights (tools/pxsgen.py's HEIGHTS): 2..80 by two, then
; 86..116 by six, and 120
px_hts:
%assign PXH 2
%rep 40
    db PXH
%assign PXH PXH + 2
%endrep
    db 86, 92, 98, 104, 110, 116, 120
%if ($ - px_hts) != PXG_NSC
%error "px_hts is not PXG_NSC heights"
%endif

; px_hq[h] for h = 0..120: the generated height h aliases DOWN to (the cast
; quantises a Textured column's h through this, so top/bot and the drawn
; rows agree; a height over 120 clamps first)
px_hq:
%assign PXH 0
%rep PXG_HMAX + 1
%if PXH < 2
    db 2
%elif PXH <= 80
    db PXH & ~1
%elif PXH < 86
    db 80
%elif PXH < 120
    db 86 + ((PXH - 86) / 6) * 6
%else
    db 120
%endif
%assign PXH PXH + 1
%endrep

; the odd-row phase per backend: (count, up to four bytes), indexed by PXB_*;
; each count an equ so the assembler can hold it under PXG_PHMAX
PXPH_CGA4   equ 4                   ; ror al, 1 x 2
PXPH_HERC   equ 2                   ; ror al, cl (CL = 3), WIN1's too
%if PXPH_CGA4 > PXG_PHMAX || PXPH_HERC > PXG_PHMAX
%error "a phase outgrew PXG_PHMAX: the generator's fence no longer bounds an emission"
%endif
px_phase:   db 0, 0, 0, 0, 0                        ; PXB_NONE
            db PXPH_CGA4, 0xD0, 0xC8, 0xD0, 0xC8    ; PXB_CGA4: ror al, 1 x 2
            db 0, 0, 0, 0, 0                        ; PXB_C160: sixteen solid colours
            db PXPH_HERC, 0xD2, 0xC8, 0, 0          ; PXB_HERC: ror al, cl
            db 0, 0, 0, 0, 0                        ; PXB_MODEX: a DAC
            db PXPH_HERC, 0xD2, 0xC8, 0, 0          ; PXB_WIN1: the Hercules form
            db 0, 0, 0, 0, 0                        ; PXB_WIN4: Mode X's DAC bytes
; ...and which backends share a scaler set and a byte set: the phase class
; and the ink class, so a Mode change or the F toggle rebuilds only what
; differs (Hercules and WIN1 share both; Mode X and WIN4 share both, 97.14 -
; so F from a colour window into Mode X rebuilds nothing)
px_phcls:   db 0, 1, 0, 2, 0, 2, 0
px_inkcls:  db 0, 1, 2, 3, 4, 3, 4
px_inktab:  dw 0, px_it_cga4, px_it_c160, px_it_herc, px_it_modex, px_it_herc
            dw px_it_modex

; -----------------------------------------------------------------------------
; px_gen_init - where the bodies live for this instance, once at entry
; in:  [px_hand + PXH_GEN] = the scratch part's segment, or 0
; out: [px_bseg], px_qoff[4], px_qcur/px_qcurv/px_qcurh's segments, the
;      driver copied and px_drvp filled, [px_texok]
; clobbers AX, CX, SI, DI, ES
; -----------------------------------------------------------------------------
px_gen_init:
    mov byte [px_texok], 0
    mov byte [px_genback], 0xFF     ; nothing generated, nothing transposed,
    mov byte [px_btback], 0xFF      ; nothing refused
    mov byte [px_sprback], 0xFF
    mov byte [px_genbad], 0xFF
    mov ax, [px_hand + PXH_GEN]
    or ax, ax
    jz .image
    mov es, ax                      ; the copy: the four bodies at offset 0
                                    ; of part 1 (the scratch)
    mov di, PXG_BODIES
    mov si, px_bodies
    mov cx, PXB_ALL
    cld
    rep movsb
    mov di, PXG_DRV                 ; ...and the driver after them
    mov si, px_drv_tpl
    mov cx, PXG_DRVSZ
    rep movsb
    mov word [px_drvp], PXG_DRV
    mov [px_drvp + 2], ax
    mov word [px_drv2p], PXG_DRV2
    mov [px_drv2p + 2], ax
    mov si, [px_hand + PXH_BT]
    mov [es:PXG_QTEX], si           ; the wall pass's ES, once
    mov word [es:PXG_QEND], PXG_Q
    push si
    mov si, [px_hand + PXH_SPR]
    mov [es:PXG_QSPR], si           ; ...and the sprite pass's (0 = refused:
    pop si                          ; px_sprok stays 0 and the pass never
                                    ; enters the driver, 97.6)
    or si, si
    jz .nobt
    cmp word [px_hand + PXH_ART], 0
    je .nobt
    mov byte [px_texok], 1          ; every part the rung needs arrived
.nobt:
    xor si, si                      ; ...so body q is at q * PXB_SIZE
    jmp short .tabs
.image:
    mov ax, ds                      ; no scratch: they run where they are
    mov si, px_bodies
.tabs:
    mov [px_bseg], ax
    mov [px_qcur + 2], ax
    mov [px_qcurv + 2], ax
    mov [px_qcurh + 2], ax
    mov [px_qoff + 0], si
    add si, PXB_SIZE
    mov [px_qoff + 2], si
    add si, PXB_SIZE
    mov [px_qoff + 4], si
    add si, PXB_SIZE
    mov [px_qoff + 6], si
    ret

; -----------------------------------------------------------------------------
; px_tex_setup - the scaler set and the byte-texture set for [px_back], each
;                rebuilt only when its class changed (97.3, 97.4). CALLED BY
;                px_apply WHEN THE RUNG IN FORCE IS TEXTURED and never
;                otherwise: in a bracket that is behind the black of the mode
;                set, in a window it is the worker's first Textured frame
;                (lock-free, the UI task stays live), and a launch whose
;                rung is Flat - the 8086's window - pays nothing. ~0.9 s on
;                the 5150 the first time. The first cut built both sets at
;                entry, before the menus, for a rung the window did not
;                start at (review, wave 2). A generation the part cannot
;                hold is refused FOR ITS PHASE CLASS (px_genbad) and the
;                item greyed while that class is in force; px_apply then
;                lands on Flat, having asked, and a Mode change to a class
;                whose set is smaller (C160 / Mode X are 7.7 KB under
;                CGA4's) retries rather than inheriting the refusal for the
;                session (review, wave 2). px_texok is the parts' word alone
; out: CF = 1 the rung cannot be drawn on this backend (a part missing, or
;      this class refused); CF = 0 both sets stand
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_tex_setup:
    cmp byte [px_texok], 0
    je .no
    mov bl, [px_back]
    xor bh, bh
    mov al, [px_phcls + bx]
    cmp al, [px_genbad]
    je .no                          ; this class did not fit: asked once
    cmp al, [px_genback]
    je .gen_ok
    mov [px_genback], al
    call px_gen_build               ; CF: the part could not hold the class
    jnc .gen_ok
    call px_tex_caption             ; refused: "Textured (needs 82 KB free)"
                                    ; is the nearest true sentence, and no
                                    ; byte set is transposed for a rung that
                                    ; cannot be drawn
.no:
    stc
    ret
.gen_ok:
    mov bl, [px_back]
    xor bh, bh
    mov al, [px_inkcls + bx]
    cmp al, [px_btback]
    je .spr
    mov [px_btback], al
    call px_bt_build
.spr:
    cmp al, [px_sprback]            ; ...and the sprite set for the same ink
    je .out                         ; class (97.6), when the sprite claim was made -
    call px_spr_build               ; its own class word, since a Flat frame
                                    ; may have built it first (px_sprites)
.out:
    call px_tex_caption             ; ...and the item says so again after a
    clc                             ; refusal on another class
    ret

; -----------------------------------------------------------------------------
; px_gen_build - emit both scaler sets and the col2tex tables into part 1 for
;                [px_back], and fill the four directories (tools/pxsgen.py);
;                under the part's length (PXH_GENLEN), or CF = 1 with the
;                class banked in px_genbad
; out: CF = 1 the part could not hold this class
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_gen_build:
    mov es, [px_hand + PXH_GEN]
    cld
    mov ax, [px_hand + PXH_GENLEN]  ; the fence: no emission may START past
    sub ax, PXG_EMITMAX             ; here, so none can END past the part
    mov [px_glim], ax
    mov bl, [px_back]               ; the phase: count and bytes
    xor bh, bh
    mov al, bl
    shl al, 1
    shl al, 1
    add bl, al                      ; * 5
    mov al, [px_phase + bx]
    mov [px_gphn], al
    mov ax, [px_phase + bx + 1]
    mov [px_gph], ax
    mov ax, [px_phase + bx + 3]
    mov [px_gph + 2], ax
    mov di, PXG_SCAL
    mov byte [px_gword], 0          ; the Full set first
    mov word [px_gdir], px_sctab
.set:
    mov si, px_hts
.scaler:
    cmp di, [px_glim]
    ja .over
    mov [px_gcot], di               ; the codeofs table goes here (97.3): 33
    add di, PXG_COTSZ               ; words reserved, written after the
                                    ; scaler by px_gen_one
    cmp di, [px_glim]
    ja .over
    mov bl, [si]
    xor bh, bh
    push bx
    shl bx, 1
    mov [px_gent + bx], di          ; where height h's scaler begins
    pop bx
    push si
    call px_gen_one
    pop si
    inc si
    cmp si, px_hts + PXG_NSC
    jb .scaler
    call px_gen_dir                 ; the set's directory, every h aliased
    add word [px_gdir], PXG_DIRSZ
    cmp byte [px_gword], 0
    jne .c2t
    mov byte [px_gword], 1
    jmp short .set
.c2t:
    ; --- col2tex, a block per scaler per set (graft 2): px_c2t's directory
    mov byte [px_gword], 0
    mov word [px_gdir], px_c2t
.cset:
    mov si, px_hts
.cblk:
    cmp di, [px_glim]
    ja .over
    mov bl, [si]
    xor bh, bh
    push bx
    shl bx, 1
    mov [px_gent + bx], di
    pop bx
    push si
    call px_gen_c2t
    pop si
    inc si
    cmp si, px_hts + PXG_NSC
    jb .cblk
    call px_gen_dir
    add word [px_gdir], PXG_DIRSZ
    cmp byte [px_gword], 0
    jne .done
    mov byte [px_gword], 1
    jmp short .cset
.done:
    mov [px_genend], di
    clc
    ret
.over:
    ; THE PART IS FULL (SPEC.md 47): the rung is withdrawn in words rather
    ; than drawn off the end of the claim - for THIS phase class, which
    ; px_genbad remembers (px_tex_setup asks no second time for it and asks
    ; again for another). Nothing written so far is past the part - the
    ; fence is a whole emission short of it - and nothing reads the
    ; directories of a class that is not on offer
    mov al, [px_genback]
    mov [px_genbad], al
    mov byte [px_genback], 0xFF
    mov [px_genend], di
    stc
    ret

; px_gen_dir - [px_gdir][h] = px_gent[px_hq[h]] for h = 0..120
px_gen_dir:
    push di
    xor bx, bx
    mov di, [px_gdir]
.h:
    mov al, [px_hq + bx]
    xor ah, ah
    mov si, ax
    shl si, 1
    mov ax, [px_gent + si]
    mov [di], ax
    inc di
    inc di
    inc bx
    cmp bx, PXG_HMAX + 1
    jb .h
    pop di
    ret

; -----------------------------------------------------------------------------
; px_gen_one - one scaler of height BX into ES:DI (DI advances)
; in:  BX = h, [px_gword] the set, [px_gphn]/[px_gph] the phase
; clobbers AX, CX, DX, BP
;
; Texel v covers the rows [top + (v*h >> 5), top + ((v+1)*h >> 5)) with
; top = (80 - h) >> 1 (signed), clipped to the view; DX walks v*h in steps
; of h so there is no multiply. Even rows first, then the phase and the odd
; rows, and a run that covers no row on the view emits nothing.
; -----------------------------------------------------------------------------
px_gen_one:
    mov ax, PX_ROWS
    sub ax, bx
    sar ax, 1
    mov [px_gtop], ax               ; top, signed
    mov byte [px_gv], 0
    xor dx, dx                      ; DX = v * h
    push di                         ; the codeofs scratch to "none" first -
    push cx                         ; IN OUR OWN SEGMENT: ES is the part
    push es                         ; being emitted into here, and the first
    push ds                         ; cut's `rep stosw` zeroed 66 bytes of
    pop es                          ; part 1 at the scratch's OFFSET, inside
    mov di, px_gcotv                ; an earlier scaler, on every height
    mov cx, PXA_TEX + 1             ; generated (tests/pxssim.py's scene B
    xor ax, ax                      ; at Size 72 read a wall with nine rows
    rep stosw                       ; missing and a byte written off the band)
    pop es
    pop cx
    pop di
.tex:
    mov cl, 5
    mov ax, dx
    shr ax, cl
    add ax, [px_gtop]               ; r0
    mov bp, ax
    add dx, bx
    mov ax, dx
    shr ax, cl
    add ax, [px_gtop]               ; r1, exclusive
    or bp, bp
    jns .r0ok
    xor bp, bp                      ; clipped at the top
.r0ok:
    cmp ax, PX_ROWS
    jle .r1ok
    mov ax, PX_ROWS                 ; ...and the bottom
.r1ok:
    cmp bp, ax
    jge .next                       ; no row on the view: no load, no store
    mov [px_gr1], ax
    push bx
    mov bl, [px_gv]                 ; codeofs[v] = this load (the scratch,
    xor bh, bh                      ; backfilled below for the texels that
    shl bx, 1                       ; emit nothing)
    mov [px_gcotv + bx], di
    pop bx
    mov ax, 0x8A26                  ; es: mov al, [si + v]
    stosw
    mov al, 0x44
    stosb
    mov al, [px_gv]
    stosb
    xor al, al                      ; the even rows...
    call px_gen_rows
    mov al, 1                       ; ...then the phase and the odd rows
    call px_gen_rows
.next:
    inc byte [px_gv]
    cmp byte [px_gv], PXA_TEX
    jb .tex
    mov al, 0xC3                    ; ret - NEAR: the driver is in the part
    stosb
    ; THE codeofs TABLE (97.3): a texel that emitted nothing takes the next
    ; emitted texel's load, so a post entered there draws from the first
    ; texel that has a row and a post stopped there stops where that load
    ; would begin; past the last emitted texel the entry is 0 - "nothing to
    ; draw" for an entry, "no patch" for a stop (the scaler's own ret ends
    ; it). Written into the 33 words reserved before the scaler, backwards
    push di
    push si
    push cx
    mov si, px_gcotv + PXA_TEX * 2  ; codeofs[32] = 0 (the scratch's own)
    mov di, [px_gcot]
    add di, PXG_COTSZ - 2
    xor ax, ax                      ; AX = the next emitted texel's load
    mov cx, PXA_TEX + 1
.cot:
    cmp word [si], 0
    je .cotnone
    mov ax, [si]
.cotnone:
    mov [es:di], ax
    dec di
    dec di
    dec si
    dec si
    loop .cot
    pop cx
    pop si
    pop di
    ret

; px_gen_rows - the stores of parity AL over rows [BP, px_gr1), preceded by
;               the phase (odd) and `mov ah, al` (the word set); nothing if
;               the range holds no row of that parity
; in: AL = 0 even / 1 odd, BP = r0, [px_gr1] = r1. clobbers AX; the rest kept
px_gen_rows:
    push bp
    push cx
    push dx
    push si
    mov dx, bp
    mov ah, dl
    and ah, 1
    cmp ah, al
    je .first
    inc dx                          ; the first row of that parity
.first:
    cmp dx, [px_gr1]
    jge .out                        ; none of that parity in the range
    or al, al
    jz .noph
    mov cl, [px_gphn]               ; odd rows follow the phase
    xor ch, ch
    jcxz .noph
    mov si, px_gph
    rep movsb
.noph:
    cmp byte [px_gword], 0
    je .rows
    mov ax, 0xC488                  ; mov ah, al: the word set's row
    stosw
.rows:
    mov ax, 0x8588                  ; mov [di + disp16], al
    cmp byte [px_gword], 0
    je .op
    mov ax, 0x8589                  ; ...ax on the word set
.op:
    stosw
    mov bp, dx
    shl bp, 1
    mov ax, [cs:px_row80 + bp]      ; r * 80 - [bp] addresses SS without the
    stosw                           ; override (CLAUDE.md): the first cut
                                    ; emitted stack words as displacements
    add dx, 2
    cmp dx, [px_gr1]
    jl .rows
.out:
    pop si
    pop dx
    pop cx
    pop bp
    ret

; -----------------------------------------------------------------------------
; px_gen_c2t - the col2tex block of height BX: [w][col_0..col_w-1], w =
;              (h*71 + 128) >> 8 at Full, (h*71 + 256) >> 9 at Low res, at
;              least 1; col_j = (j * 32) / w by a Bresenham (graft 2)
; in: BX = h. clobbers AX, CX, DX
; -----------------------------------------------------------------------------
px_gen_c2t:
    mov ax, bx
    mov cx, 71
    mul cx
    add ax, 128
    cmp byte [px_gword], 0
    je .full
    add ax, 128
    shr ax, 1
.full:
    mov al, ah                      ; >> 8
    or al, al
    jnz .w
    inc al                          ; AT LEAST ONE: the 2-row scaler at Low
.w:                                 ; res rounds to none, and the walk
    stosb                           ; below divides by it (a hang, once)
    mov cl, al                      ; CL = w
    mov ch, al                      ; CH = columns left
    xor dx, dx                      ; DL = col, DH = the accumulator
.col:
    mov al, dl
    stosb
    add dh, PXA_TEX
.bres:
    cmp dh, cl
    jb .nx
    sub dh, cl
    inc dl
    jmp short .bres
.nx:
    dec ch
    jnz .col
    ret

; -----------------------------------------------------------------------------
; px_bt_build - the byte-texture set for [px_back] into part 2 (97.4): the 15
;               masters, two shades, column-major, one byte a texel through
;               the backend's ink table - a C160 column holding the nibbles
;               of texels u and u+1 of one row. Each master is STAGED into
;               px_bstage first (512 bytes, rep movsb) so the loop has the
;               master and the ink table in DS for `xlat` and the set in ES.
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_bt_build:
    mov bl, [px_back]
    xor bh, bh
    shl bx, 1
    mov ax, [px_inktab + bx]
    mov [px_bink], ax
    mov es, [px_hand + PXH_BT]
    xor di, di
    cld
    mov word [px_bmat], 0           ; the master's offset in the art claim
.mat:
    push es
    push di
    push ds
    pop es
    mov di, px_bstage
    mov si, [px_bmat]
    mov cx, PXA_WALLSZ
    mov ds, [px_hand + PXH_ART]
    rep movsb                       ; the master, staged
    push es
    pop ds
    pop di
    pop es
    mov bx, [px_bink]               ; the lit table
    mov ch, PXA_SHADES
.shade:
    mov si, px_bstage
    mov dh, PXA_TEX / 2             ; column pairs
    cmp byte [px_back], PXB_C160
    je .c160
.pair:
    push si
    mov cl, PXA_TEX                 ; CL = texel rows
.tx:
    mov al, [si]                    ; the pair: u in the high nibble
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat
    mov [es:di], al                 ; column u, texel v
    mov al, ah
    and al, 15
    xlat
    mov [es:di + PXA_TEX], al       ; column u + 1, texel v
    inc di
    add si, PXA_TEX / 2             ; the master's next row
    dec cl
    jnz .tx
    pop si
    inc si                          ; the next byte pair of row 0
    add di, PXA_TEX                 ; ...past column u + 1, already written
    dec dh
    jnz .pair
    jmp short .nextshade
.c160:                              ; both nibbles into ONE byte a texel pair
    push si
    mov cl, PXA_TEX
.tx2:
    mov al, [si]
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat
    mov dl, al                      ; the left texel's attribute nibble
    mov al, ah
    and al, 15
    xlat                            ; ...and the right's
    push cx
    mov cl, 4
    shl dl, cl
    pop cx
    or al, dl
    mov [es:di], al
    inc di
    add si, PXA_TEX / 2
    dec cl
    jnz .tx2
    pop si
    inc si
    dec dh
    jnz .c160
    add di, PXA_TEX * PXA_TEX / 2   ; sixteen columns used of thirty-two
.nextshade:
    add bx, 16                      ; the dark table
    dec ch
    jnz .shade
    add word [px_bmat], PXA_WALLSZ
    cmp word [px_bmat], PXA_NWALL * PXA_WALLSZ
    jb .mat
    ret

; -----------------------------------------------------------------------------
; px_spr_build - THE SPRITE SET for [px_back] into the sprite claim (PXH_SPR, 97.6): every frame
;                of the masters (PXA_NSPR of 32 x 32, then PXA_NWPN weapon
;                frames of 16 x 32) transposed column-major through the LIT
;                ink table - a C160 column holding the nibbles of texels u
;                and u+1 as the walls' does - and, after each frame's texels,
;                a RUN TABLE per column from the master's alpha bits: the
;                count, then (v0, v1) pairs with v1 exclusive, PXS_MAXRUNS at
;                most (the tool refuses a master past that; the machine
;                stops counting at it, since these bytes came off a floppy).
;                Each master is STAGED into px_bstage first (640 bytes), for
;                px_bt_build's reason. Nothing when the part was refused
;                (px_sprok stays 0: the sprites fall to the box arm).
; clobbers everything but DS
; -----------------------------------------------------------------------------
px_spr_build:
    mov byte [px_sprok], 0
    mov bl, [px_back]
    xor bh, bh
    mov al, [px_inkcls + bx]
    mov [px_sprback], al            ; the class this set is for (px_sprites
                                    ; asks once per class)
    mov ax, [px_hand + PXH_SPR]
    or ax, ax
    jz .out
    cmp word [px_hand + PXH_ART], 0
    je .out
    mov es, ax
    mov bl, [px_back]
    xor bh, bh
    shl bx, 1
    mov ax, [px_inktab + bx]
    mov [px_bink], ax
    xor di, di
    cld
    mov word [px_bmat], PXA_SPR0    ; the master's offset in the art claim
    mov byte [px_bcnt], PXA_NSPR
    mov byte [px_bcols], PXA_TEX    ; a frame's columns, and its master's
    mov word [px_bmsz], PXA_SPRMSZ  ; bytes
    mov word [px_balpha], PXA_SPRALPHA
    call .frames
    mov word [px_bmat], PXA_WPN0    ; ...then the weapon's nine
    mov byte [px_bcnt], PXA_NWPN
    mov byte [px_bcols], PXA_WPNW
    mov word [px_bmsz], PXA_WPNMSZ
    mov word [px_balpha], PXA_WPNALPHA
    call .frames
    mov byte [px_sprok], 1
.out:
    ret
.frames:
    push es
    push di
    push ds
    pop es
    mov di, px_bstage
    mov si, [px_bmat]
    mov cx, [px_bmsz]
    mov ds, [px_hand + PXH_ART]
    rep movsb                       ; the master, staged
    push es
    pop ds
    pop di
    pop es
    mov bx, [px_bink]               ; the lit table
    ; --- the texels, a column at a time: column u reads nibble u of every
    ;     row of the staged master (row pitch = columns / 2, kept in BP:
    ;     an addend only, never an address - SS is not DS)
    mov al, [px_bcols]
    shr al, 1
    xor ah, ah
    mov bp, ax                      ; BP = the master's row pitch
    mov dh, [px_bcols]
    shr dh, 1                       ; DH = column pairs
    mov si, px_bstage
    cmp byte [px_back], PXB_C160
    je .c160
.pair:
    push si
    mov cl, PXA_TEX                 ; CL = texel rows
.tx:
    mov al, [si]                    ; the pair: u in the high nibble
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat
    mov [es:di], al                 ; column u, texel v
    mov al, ah
    and al, 15
    xlat
    mov [es:di + PXA_TEX], al       ; column u + 1, texel v
    inc di
    add si, bp                      ; the master's next row
    dec cl
    jnz .tx
    pop si
    inc si                          ; the next byte pair of row 0
    add di, PXA_TEX                 ; ...past column u + 1, already written
    dec dh
    jnz .pair
    jmp short .runs
.c160:                              ; both nibbles into ONE byte a texel pair
    push si
    mov cl, PXA_TEX
.tx2:
    mov al, [si]
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    xlat
    mov dl, al
    mov al, ah
    and al, 15
    xlat
    shl dl, 1
    shl dl, 1
    shl dl, 1
    shl dl, 1
    or al, dl
    mov [es:di], al
    inc di
    add si, bp
    dec cl
    jnz .tx2
    pop si
    inc si
    dec dh
    jnz .c160
    mov al, [px_bcols]              ; the unused half: (cols / 2) x 32 bytes
    shr al, 1
    mov ah, PXA_TEX
    mul ah
    add di, ax
.runs:
    ; --- the run tables: one per column (per PAIR on C160, the two
    ;     texels' bits OR'd), PXS_RUNSZ bytes each, from the alpha bits
    ;     at px_bstage + [px_balpha]: row-major, a row (cols / 8) bytes,
    ;     bit 7 the leftmost column. A column's bit is ONE mask over ONE
    ;     byte a row (a pair's is its two adjacent bits - the left column
    ;     is even, so both sit in the same byte), the pointer stepping the
    ;     row pitch: a `test` a texel. The first cut rebuilt the byte index
    ;     and rotated the byte into place for every one of the 40,960
    ;     texels, and the set took 4.3 s on the 5150 to build - measured
    ;     with breakpoints either side of it at a bracket's entry, where it
    ;     sat on top of the 1.7 s the two scaler sets and the wall transpose
    ;     already cost (97.1) and the gate read no frame in two seconds
    mov al, [px_bcols]
    shr al, 1
    shr al, 1
    shr al, 1
    xor ah, ah
    mov bp, ax                      ; BP = the alpha row pitch (4 or 2)
    xor dl, dl                      ; DL = the column (or pair)
.rcol:
    push di
    mov [px_brtab], di              ; the table: its count byte
    mov byte [es:di], 0
    inc di                          ; DI = the next pair's slot
    mov al, dl
    cmp byte [px_back], PXB_C160
    jne .rmask
    shl al, 1                       ; a pair: its left texel's column
.rmask:
    mov cl, al
    and cl, 7
    mov ah, 0x80
    shr ah, cl                      ; AH = the column's bit
    cmp byte [px_back], PXB_C160
    jne .rone
    mov cl, ah
    shr cl, 1
    or ah, cl                       ; ...and the pair's right texel's beside it
.rone:
    mov bl, al
    xor bh, bh
    shr bx, 1
    shr bx, 1
    shr bx, 1                       ; BX = the byte in the row
    mov si, px_bstage
    add si, [px_balpha]
    add si, bx                      ; SI = that byte of row 0
    xor ch, ch                      ; CH = 0 outside a run, 1 inside
    xor cl, cl                      ; CL = spans opened
    xor dh, dh                      ; DH = v
.rv:
    test [si], ah
    jz .rt
    or ch, ch                       ; opaque: inside a run, nothing; outside
    jnz .rn                         ; one, a run begins - if the table has
    cmp cl, PXS_MAXRUNS             ; room (a fifth span is dropped: the tool
    jae .rn                         ; refuses the master, this is the machine
    push bx                         ; bounding a floppy's bytes)
    mov bx, [px_brtab]
    inc byte [es:bx]                ; the count
    pop bx
    inc cl
    mov [es:di], dh                 ; v0
    inc di
    mov ch, 1
    jmp short .rn
.rt:
    or ch, ch                       ; transparent: outside a run, nothing;
    jz .rn                          ; inside one, the run ends here
    mov [es:di], dh                 ; v1, exclusive
    inc di
    xor ch, ch
.rn:
    add si, bp                      ; the next alpha row
    inc dh
    cmp dh, PXA_TEX
    jb .rv
    or ch, ch
    jz .rdone
    mov byte [es:di], PXA_TEX       ; a run open at the bottom ends at 32
.rdone:
    pop di
    add di, PXS_RUNSZ
    inc dl
    mov al, [px_bcols]
    cmp byte [px_back], PXB_C160
    jne .rn2
    shr al, 1                       ; C160: pairs
.rn2:
    cmp dl, al
    jb .rcol
    ; C160 leaves the pairs' tables in the first half and skips the rest
    cmp byte [px_back], PXB_C160
    jne .fnext
    mov al, [px_bcols]
    shr al, 1
    mov ah, PXS_RUNSZ
    mul ah
    add di, ax
.fnext:
    mov ax, [px_bmsz]
    add [px_bmat], ax
    dec byte [px_bcnt]
    jnz .frames
    ret

