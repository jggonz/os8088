%ifdef CH_OVERLAY
; =============================================================================
; os88chartovl.inc - the resident half of the shared chart module (82.16).
;
; apps/os88chart.inc is ~4,800 bytes of rasterizer that BOTH sheet.asm and
; chart.asm carried a private copy of. It is a module now - one CHART.OVL
; loaded beside the package and far-called - and this file is what each
; package keeps resident to reach it: the loader, the shims the module calls
; back through, and the verb wrapper.
;
; ONLY SHEET OVERLAYS IT, and CHART.OVL IS SHEET'S FILE ALONE. chart.o88
; defines neither CH_ symbol, never includes this file, and carries the
; rasterizer resident; the Makefile cuts CHART.OVL from build/sheet.bin and
; from nothing else.
;
; An earlier version of this comment claimed "ONE FILE SERVES BOTH PACKAGES"
; and that "the Makefile asserts the two independently-assembled .OVLs are
; identical". BOTH HALVES ARE FALSE, and 82.16.1 - written later, and right -
; says why the first one CANNOT be true: the module reaches the ch_* block at
; absolute offsets past the package's own image end, and the two packages'
; images are different lengths, so one binary could never address both. There
; is no identity rule in the Makefile and no second assembly for it to compare
; against.
;
; The cost of leaving that standing is not a wrong result, it is a wrong
; BOUNDARY: a jointly-owned tail is one nothing else may be put in, so the
; comment quietly retires Sheet's only room to grow. It is Sheet's tail, and
; a second tenant belongs in it (82.16.6).
;
; Include it BEFORE os88chart.inc, and after os88fp.inc (the shims name it).
; The caller must have called OSAPI_FILE_HERE at entry and banked the answer
; in ch_ovdir/ch_ovdrv - that is where the .OVL lives, and the user is free to
; navigate away a moment later.
; =============================================================================
CH_OVKB      equ 52                 ; the claim CHART.OVL is read into, KB.
                                    ; 52 at the review of PR #289: main's
                                    ; SYLK borders/notes and the Border
                                    ; dialog's block walk (81.17.2), ported
                                    ; in, 52,372 of 51's 52,224.
                                    ; 48 for 81.102: the Note dialog and
                                    ; os88text.inc moved in, 47,364 bytes.
                                    ; 8 held the rasterizer alone; 82.16.9 put
                                    ; SHEET's file formats in the same tail and
                                    ; took it to 13,485 bytes; 82.16.10's
                                    ; financial family to 19,544; 81.10.10's
                                    ; formula decoder past 20,480, so 22; and
                                    ; 81.59 23, with 122 bytes left at 22 and
                                    ; no tenant yet; 81.62's text, logarithm
                                    ; and information functions 26; 81.63's
                                    ; macro engine 28; 81.65's DATABASE
                                    ; functions 29,220 bytes, so 29; 81.66's
                                    ; CELL 29,956, so 30; 81.67's array/
                                    ; matrix functions 31,638, so 31, and its
                                    ; own regression family (LINEST/LOGEST/
                                    ; TREND/GROWTH) 33,034, so 33 - that
                                    ; is heap, not image; and 81.77's BLANK
                                    ; records 44,031 of 44,032, so 44; and
                                    ; 81.81's Format > Justify 44,9xx, so 45
                                    ; - taken for the SAME reason as the step
                                    ; below, 59 bytes being no more a margin
                                    ; than one was; and 81.82's Data > Parse
                                    ; 45,896, so 46; and the macro plan's
                                    ; wave 1 (SHEET-MACRO-PLAN, subroutines
                                    ; and their ~1 KB of frame state) 48,553,
                                    ; so 50 - two ahead ON PURPOSE, the plan
                                    ; having ~8 KB still to come, and 64 the
                                    ; wall: the module is ONE segment; and
                                    ; wave 2's control functions (81.86)
                                    ; 50,725, so 54 ahead of the information
                                    ; half, which is the bigger one; and wave
                                    ; 3a's command equivalents (81.88) 54,541,
                                    ; so 58; and 63 for the macro text files
                                    ; (81.91), whose buffers are the claim's
                                    ; TAIL past the image - 63 is the most
                                    ; `mov cx, CH_OVKB * 1024` below can say.
                                    ; THAT
                                    ; LAST STEP WAS TAKEN FOR ONE BYTE, and
                                    ; deliberately: 43 still built, with a
                                    ; single byte spare, and a margin of one
                                    ; is not a margin - it is the next change
                                    ; failing a build for a reason that has
                                    ; nothing to do with it. ch_ovneed reads
                                    ; CH_OVKB*1024 bytes and a short claim
                                    ; would silently truncate the module, so
                                    ; it IS checked against the file, twice:
                                    ; the sheet.o88 rule, and os88ovlchk.py's
                                    ; CLAIMS row in the fast tier.
                                    ; AND 81.114.7's Data > Table stage 2
                                    ; (the table list, the two guards and the
                                    ; unmark walk) 49,204, so 51 - THREE
                                    ; ahead, the same deliberate step as 50
                                    ; above and for the same reason:
                                    ; SHEET-TABLE-PLAN has three stages left,
                                    ; and stage 4 is a BIFF reader AND a
                                    ; writer. 48 had 90 bytes spare when
                                    ; stage 2 began and 48 was where stage 2
                                    ; ran out, mid-fix. This is HEAP and not
                                    ; image - 82.16.3 already says a machine
                                    ; too small for the claim still runs the
                                    ; spreadsheet - and 63 is the wall
CHM_DRAW     equ 0                  ; the verbs - the module's dispatch indices
CHM_SCALE    equ 1
CHM_BMPWRITE equ 2
CHM_MAX      equ 2

; -----------------------------------------------------------------------------
; The shims apps/os88chart.inc's vectors point at (82.16).
;
; Each is a near call to the real routine and then the FAR return the module's
; `call far [ch_v_*]` is waiting for. Registers and flags pass through both
; ways untouched, so a shimmed routine keeps its own contract - which is the
; whole reason the shim exists rather than the vector naming the routine: every
; routine here is a near proc ending in `ret`, and far-calling one pops the
; offset, leaves the segment on the stack and returns into nothing.
;
; ch_s_aiszero is the fused one. The module used to load `bx, fp_am0` itself,
; which is an address in THIS package's bss and exactly what a module shared
; with another package cannot know. The shim supplies it.
; -----------------------------------------------------------------------------
ch_s_unpacka:
    call fp_unpack_a
    retf
ch_s_unpackb:
    call fp_unpack_b
    retf
ch_s_a2i:
    call fp_a2i
    retf
ch_s_cmpab:
    call fp_cmpab
    retf
ch_s_round:
    call fp_round
    retf
ch_s_scale10:
    call fp_scale10
    retf
ch_s_aiszero:
    mov bx, fp_am0
    call fp_iszero
    retf
ch_s_dec_s16:                         ; apps/os88dec.inc's two, which sheet.asm
    call dec_s16                      ; claims for .text because sh_itoa calls
    retf                              ; dec_s16 near from there (20.5.2)
ch_s_dec_u16:
    call dec_u16
    retf

; ch_ovbind - fill the vector table: this package's shim offsets, and this
; package's segment. The offsets are assembled in; only the segment is a
; runtime fact, and it is the same one for all of them.
ch_ovbind:
    push ax
    push bx
    push si
    push di
    mov si, ch_ovshims
    mov di, ch_v_first
    mov ax, cs
    mov bx, 9                         ; ch_ovshims' length, and ch_v_*'s
.l:
    push ax
    mov ax, [si]
    mov [di], ax                      ; the shim's offset...
    pop ax
    mov [di+2], ax                    ; ...and our own segment
    add si, 2
    add di, 4
    dec bx
    jnz .l
    pop di
    pop si
    pop bx
    pop ax
    ret

ch_ovshims:
    dw ch_s_unpacka, ch_s_unpackb, ch_s_a2i, ch_s_cmpab
    dw ch_s_round, ch_s_scale10, ch_s_aiszero
    dw ch_s_dec_s16, ch_s_dec_u16     ; ...in ch_v_*'s own order, below

; -----------------------------------------------------------------------------
; ch_ovneed - make sure CHART.OVL is loaded.
; out: CF=0 with [ch_ovseg] holding it; CF=1 and [ch_ovfail] set. Preserves
; every register, because it sits under calls whose arguments are in them.
; A failure is remembered, not retried (sh_m2need's rule): every formula door
; comes through here, and a repaint must not re-claim 51KB per cell. A file
; whose stamp (the host's CH_OVSTAMP: CH_OVLEN, then os88_image_end) is not
; THIS build's is refused the same way - it would read our bss at stale
; offsets and return through a vector table laid out for another build.
;
; REFUSAL IS AN ORDINARY PATH (SPEC.md 47): no heap, or a disk without the
; file, and the chart simply does not draw. The caller sees CF=1 and says so
; in its own voice - this include has no toast of its own and should not grow
; one, because the two packages report differently.
; -----------------------------------------------------------------------------
ch_ovneed:
    cmp word [ch_ovseg], 0
    jne .ok
    cmp byte [ch_ovfail], 0
    jne .fail
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_FILE_HERE            ; where the USER is, to be put back
    mov [ch_ovwas], dx
    mov [ch_ovwdr], bl
    mov dx, [ch_ovdir]              ; ...and off to where we were launched
    mov bl, [ch_ovdrv]
    call OSAPI_FILE_GOTO
    mov ax, CH_OVKB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [ch_ovseg], dx
    mov word [ch_ovfar], 0          ; the dispatcher is at the claim's 0
    mov [ch_ovfar+2], dx
    mov es, dx
    xor bx, bx
    mov cx, CH_OVKB * 1024
    xor dx, dx
    mov si, ch_ovname
    call OSAPI_FILE_READ
    jc .noread
    mov ax, [es:CH_OVSTAMP]         ; THIS build's module, or none
    cmp ax, CH_OVLEN
    jne .noread
    mov ax, [es:CH_OVSTAMP + 2]
    cmp ax, os88_image_end
    jne .noread
    call ch_ovback
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.ok:
    clc
    ret
.noread:
    mov dx, [ch_ovseg]              ; the claim goes back: a half-loaded
    call OSAPI_MEM_FREE             ; module is worse than none
    mov word [ch_ovseg], 0
.nomem:
    mov byte [ch_ovfail], 1
    call ch_ovback
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.fail:
    stc
    ret

; ch_ovback - put the volume back where the user left it. Preserves all.
ch_ovback:
    push bx
    push dx
    mov dx, [ch_ovwas]
    mov bl, [ch_ovwdr]
    call OSAPI_FILE_GOTO
    pop dx
    pop bx
    ret

%endif

%ifdef CH_OVERLAY
; -----------------------------------------------------------------------------
; ch_ovcall - far-call the module.
; in:  BP = the verb (CHM_*), everything else as that verb documents
; out: as the verb; CF=1 if the module could not be loaded at all.
;
; BP carries the verb because none of the four entry points takes it as an
; argument - ch_scale wants DX/SI/DI/CX, ch_bmp_write ES/BX/SI - so it is the
; one register free to say WHICH without disturbing what they were handed.
; -----------------------------------------------------------------------------
ch_ovcall:
    call ch_ovneed
    jc .no
    call far [ch_ovfar]             ; the dispatcher is at the claim's offset
.no:                                ; 0, so the far pointer is (0, the claim)
    ret

ch_ovname:  db 'CHART.OVL', 0
%endif
