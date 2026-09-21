; =============================================================================
; os8088 - apps/pixelstein/pxgen.inc
;
; THE STATIC LADDERS and THE GENERATOR (SPEC.md 96.3).
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
; THE GENERATOR (wave 2) writes the scratch part the loader carved (96.9's
; PART 2 - the indices here are os88pkg's, the ones the loader's table and
; PXH_GEN name; the plan's "part 3" is this one) - and tools/pxsgen.py is
; its model, byte for byte, which tests/pxsscale.py reads back off the guest
; and diffs:
;
;   PXG_BODIES  the four quadrant bodies of pxcast.inc, copied (96.2.1);
;   PXG_DRV     the column driver, copied from px_drv_tpl below: the frame's
;               ONE far call into the part, which walks the DRAW QUEUE and
;               near-calls each entry's scaler with SI = its texture column
;               and DI = its column base;
;   PXG_QTEX    the queue's header - ES for the driver (part 3, the byte
;               textures) and the queue's end - and PXG_Q the queue itself,
;               PXG_QN entries of (scaler, SI, DI), written by px_compose
;               (pxcomp.inc) and by wave 3's sprite posts;
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
; THE SCRATCH PART'S LAYOUT (96.3) - constants, because the driver template
; below names them and is copied verbatim
; =============================================================================
PXG_BODIES  equ 0                   ; the four bodies, PXB_ALL bytes
PXG_DRV     equ PXB_ALL             ; the driver, PXG_DRVMAX reserved
PXG_DRVMAX  equ 64
PXG_QTEX    equ PXG_DRV + PXG_DRVMAX    ; word: the driver's ES (part 3)
PXG_QEND    equ PXG_QTEX + 2        ; word: one past the queue's last entry
PXG_Q       equ PXG_QEND + 2        ; the draw queue
PXG_QN      equ 512                 ; ...entries: 80 walls and wave 3's posts
PXG_QSZ     equ 6                   ; (scaler offset, SI, DI)
PXG_SCAL    equ PXG_Q + PXG_QN * PXG_QSZ ; the generated half begins here
PXG_HMAX    equ 120                 ; the tallest scaler: 1.5 x the view
PXG_NSC     equ 47                  ; scalers a set
PXG_DIRSZ   equ (PXG_HMAX + 1) * 2  ; a directory: a word per height 0..120
PXG_PHMAX   equ 4                   ; the longest odd-row phase, bytes: every
                                    ; px_phase row's count is held under it
                                    ; below, and pxgame.asm derives the fence
                                    ; from it after pxart.inc
PXG_EMITMAX equ 768                 ; the most ONE emission can be: the
                                    ; tallest Low-res scaler is 32 loads x 4
                                    ; + 80 stores x 4 + 32 phases x PXG_PHMAX
                                    ; + 64 `mov ah, al` x 2 + a ret = 705,
                                    ; and a col2tex block 1 + 33 (h = 120:
                                    ; w = (120 * 71 + 128) >> 8 = 33)

; -----------------------------------------------------------------------------
; px_drv_tpl - THE COLUMN DRIVER, copied into part 2 at PXG_DRV and entered
;              by ONE far call a frame from px_compose (pxcomp.inc) with
;              DS = the destination. Every reference here is either relative
;              (the jumps) or an absolute offset in PART 2 (the cs: words),
;              so the copy runs where it lands. No register preserved
;              (SPEC.md 85.3.4). CL = 3 is the Hercules/WIN1 phase's
;              rotate count and costs nothing elsewhere; the queue's end is
;              read ONCE into DX - a 4-byte cs: read a column was ~25 clk
;              over a register compare, x 32..80 a frame, and the queue is
;              longer once wave 3's posts share it (review, wave 2).
; -----------------------------------------------------------------------------
px_drv_tpl:
    mov es, [cs:PXG_QTEX]
    mov cl, 3
    mov dx, [cs:PXG_QEND]
    mov bx, PXG_Q
    cmp bx, dx
    jae .done
.col:
    mov si, [cs:bx + 2]
    mov di, [cs:bx + 4]
    call word [cs:bx]               ; the scaler: SI/DI its, ES the textures
    add bx, PXG_QSZ
    cmp bx, dx
    jb .col
.done:
    retf
px_drv_end:
PXG_DRVSZ   equ px_drv_end - px_drv_tpl
%if PXG_DRVSZ > PXG_DRVMAX
%error "the column driver outgrew the room reserved for it in part 2"
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
; ...and which backends share a scaler set and a byte set: the phase class
; and the ink class, so a Mode change or the F toggle rebuilds only what
; differs (Hercules and WIN1 share both)
px_phcls:   db 0, 1, 0, 2, 0, 2
px_inkcls:  db 0, 1, 2, 3, 4, 3
px_inktab:  dw 0, px_it_cga4, px_it_c160, px_it_herc, px_it_modex, px_it_herc

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
    mov byte [px_genbad], 0xFF
    mov ax, [px_hand + PXH_GEN]
    or ax, ax
    jz .image
    mov es, ax                      ; the copy: the four bodies at offset 0
                                    ; of part 2 (the scratch)
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
    mov si, [px_hand + PXH_BT]
    mov [es:PXG_QTEX], si           ; the driver's ES, once
    mov word [es:PXG_QEND], PXG_Q
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
;                rebuilt only when its class changed (96.3, 96.4). CALLED BY
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
    je .out
    mov [px_btback], al
    call px_bt_build
.out:
    call px_tex_caption             ; ...and the item says so again after a
    clc                             ; refusal on another class
    ret

; -----------------------------------------------------------------------------
; px_gen_build - emit both scaler sets and the col2tex tables into part 2 for
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
; px_bt_build - the byte-texture set for [px_back] into part 3 (96.4): the 15
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
