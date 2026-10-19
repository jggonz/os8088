; =============================================================================
; os8088 - apps/video/vdec.inc
;
; THE VIDEO PLAYER'S FRAME DECODER (SPEC.md 98.1.3, 98.3): VIDEO.O88's, and
; tests/vidbench's, which is how wave 0 measured it against XDC's own code
; (docs/plans/VIDEO-PLAN.md 2.2). One copy, included by both.
;
; A frame's video part is TEN lists, in this order, each ending in a 0 byte:
;
;   list    = segment* 00
;   segment = count(1..127) address(16) entry * count     skip-coded
;           | 80h+count(1..127)         aentry * count    absolute (P1..P6)
;   entry   = skip, then the change      aentry = address(16), the change
;   P1..P6  = 1..6 bytes                 (a list per length)
;   SLICE   = len8 bytes (7..255)        RUN  = len8 value (6..255)
;   SLICEL  = len16 bytes (256+)         RUNL = len16 value (256+)
;
; A LIST PER SHORT LENGTH is wave 0's first finding: a 3-6 byte change sent
; through `rep movsw` + `rep movsb` measured +40-60 cycles against XDC's
; unrolled stores, all of it rep start-up. Here each length's store is
; straight-line - `movsw / movsb` for 3, as XDC writes it - so the only cost
; over XDC is reading the skip.
;
; ABSOLUTE SEGMENTS are the second: a skip segment's set-up measured ~228
; cycles, and half of all segments held ONE entry. An isolated change goes
; into a pooled absolute segment instead - `lodsw / add ax,bp / xchg ax,di`
; and the store, ~11 cycles more than a skip entry and no set-up of its own.
;
; `skip` is the distance from the end of the previous write in the segment
; (from the segment's address for its first entry). tools/os88vid.py's
; to_lists() writes it and decode_lists() is the reference this must match.
;
; ONE DECODER. The addresses are the target adapter's own memory image - a
; file is laid out for its surface on the HOST - so every entry applies as it
; stands, with BP added to every segment address to place the canvas (a
; window's content origin, a letterbox). Wave 0 built the other design too:
; a row-table decoder that placed a canvas on a surface of another layout at
; playback. It cost ~480 cycles a row change (the frames change rows almost
; every entry) and it forced every span to be split at a row end, which is
; what turned the heaviest frames into 290 runs where XDC has one rep stosb.
; A file on the wrong adapter is decoded here into a RAM shadow and its dirty
; band copied to the screen instead (VIDEO-PLAN 3).

; THE INNER LOOPS ARE XDC's STORES WITH THE ADDRESS FACTORED OUT. A 1-byte
; change is `lodsb / add di,ax / movsb` - AH = 0 is an invariant of every
; loop here, so the skip needs no zero-extend - against XDC's
; `mov di,imm16 / movsb`. The pokes are unrolled eight times and entered
; Duff-style on the segment's count, so there is no per-entry loop control.
;
; in:       DS:SI = the ten lists, ES = the surface segment,
;           BP = added to every segment address
; out:      SI past the last list
; clobbers: AX, BX, CX, DX, DI, BP, flags. DS and ES are preserved.
; =============================================================================

; -----------------------------------------------------------------------------
; the native decoder
; -----------------------------------------------------------------------------
; (vd_native and vd_nmap are laid out in front of vd_n_p1, below, so the
; entry falls into the first list)

; the stores for a change of 1..6 bytes - XDC_CODE.PAS's own unrolling
%macro VD_ST1 0
    movsb
%endmacro
%macro VD_ST2 0
    movsw
%endmacro
%macro VD_ST3 0
    movsw
    movsb
%endmacro
%macro VD_ST4 0
    movsw
    movsw
%endmacro
%macro VD_ST5 0
    movsw
    movsw
    movsb
%endmacro
%macro VD_ST6 0
    movsw
    movsw
    movsw
%endmacro

; VD_NPOKE name, store, c160, next, gate - one Duff-entered unrolled list of
; fixed-size changes; `next` is the list its end jumps on to (0: it returns),
; and `gate` 1 has it read its bit of [vd_map] first (98.1.3.2.4)
; VD_AE store, n - one absolute entry at unrolled position n
%macro VD_AE 2-3 0
.a%2:
    lodsw
%if %3
    shl ax, 1                       ; C160: a cell's attribute, at 2a + BP
%endif
    add ax, bp
    xchg ax, di
    %1
%endmacro

; VD_GATE next - a list's bit of the SUB2 list map: there, on into the
; list at .seg; not, straight on to `next` (0: return). Its jump on is the
; list's own END as well (.done): a list's 0 jumps BACK to it, a short `jz`
; where the end was `jnz` over a near `jmp` - not taken a segment, so 12
; cycles a segment less on an 8088, and five bytes a list
%macro VD_GATE 1
    shr byte [cs:vd_map], 1         ; (a BYTE: lists 0..7 are its low one)
    jc .seg
.done:
%ifidn %1, 0
    ret
%else
    jmp %1
%endif
%endmacro

; VD_GATEN next[, byte] - VD_GATE for a list whose `next` is in a short
; jump's reach (or 0, and .ret a `ret` in reach): not there is a `jnc` to
; it, and there falls into .seg - 12 cycles less a present list, 3 an absent
; one. The list's 0 is a `jz` to `next` itself, so no .done. The map is
; shifted a BYTE at a time - a word's read-modify-write is 8 cycles more on
; an 8088 - so lists 8 and 9 (the long spans) name its high byte
%macro VD_GATEN 1-2 vd_map
    shr byte [cs:%2], 1
%ifidn %1, 0
    jnc .ret
%else
    jnc %1
%endif
%endmacro

; VD_SHRU - CX / VD_U, VD_U a power of two
%macro VD_SHRU 0
%assign vd_k VD_U
%rep 4
%if vd_k > 1
    shr cx, 1
%assign vd_k vd_k / 2
%endif
%endrep
%endmacro

; VD_SKIP c160 - an entry's skip byte, onto DI: a cell is TWO screen bytes
; on C160 (AH = 0 throughout, so the doubled add needs no shift)
%macro VD_SKIP 1
    lodsb
    add di, ax
%if %1
    add di, ax
%endif
%endmacro

; VD_YCK - THE IN-PLACE DECODE'S YIELD (SPEC.md 98.3.18.11): SI past
; [vd_ylim] calls vd_yield, which on a 286 decoding a record held in XMS
; through a LOADALL'd DS opens the interrupts for a moment and borrows again
; at the same SI. Everywhere else [vd_ylim] is FFFFh and it is never taken:
; a compare at each poke list's head, each span segment's and each long
; span's - never inside a poke segment, the hot loop an 8088 lives in
; EVERY SITE STARTS AS A JUMP OVER ITSELF, and only an in-place session
; patches it to two NOPs (vd_ypatch), so an 8088 - which can never decode in
; place - pays a taken short jump, ~15 cycles, where the compare it skips
; cost ~41: measured on MartyPC's 5150, the compare at every site was +410
; cycles an empty frame and +0.9-2.4% a real one. The macro numbers its
; sites and vd_ytab lists them, so a site added later is in the table
%assign vd_ny 0
%macro VD_YCK 0
%assign vd_ny vd_ny + 1
..@vd_y %+ vd_ny:
    jmp short %%ok                  ; 2 bytes: EB 0A, or 90 90 in place
%%live:                             ; (reached once vd_ypatch has run)
    cmp si, [cs:vd_ylim]            ; 5
    jb %%ok                         ; 2
    call vd_yield                   ; 3
%%ok:
%endmacro

%macro VD_NPOKE 2-7 0, 0, 0, 0, 0
%if %6
%assign VD_U %6                     ; (a round as the caller says: write mode
%elif %3                            ; 3's stores are five bytes each)
%assign VD_U 4                      ; C160's stores are twice the bytes: a
%else                               ; round of four keeps the body in reach
%assign VD_U 8
%endif
%ifn %5
%ifidn %7, 0
%define VD_PDONE %%done
%%done:                             ; (ungated: the list's END sits in front
%ifidn %4, 0                        ; of it, where its 0 reaches it short -
    ret                             ; or `done` names it, laid out by the
%else                               ; caller in reach)
    jmp %4
%endif
%else
%define VD_PDONE %7
%endif
%endif
%1:
    VD_YCK                          ; (a poke list's head: SPEC.md 98.3.18.11)
%if %5 == 2
    VD_GATEN %4                     ; (gate 2: `next` follows, in reach)
%elif %5
    VD_GATE %4
%endif
.seg:
    lodsb
    or al, al
%if %5 == 2
    jz %4
%elif %5
    jz .done                        ; (BACK to the gate's jump on: the end
%else                               ; past the body was out of a short
    jz VD_PDONE                     ; jump's reach, and `jnz` over a near
%endif                              ; `jmp` cost a TAKEN jump a segment)
    js .abs
%if VD_U > 1
    mov bx, ax                      ; AH = 0, so BX = the count, and a
    add al, VD_U - 1                ; byte's add (127 + 7 is still a byte)
%endif                              ; is a byte shorter than CX's
    xchg ax, cx                     ; the rounds, the partial one a round
    VD_SHRU                         ; too (AX = CX's 0: lodsw's next)
    lodsw
%if %3
    shl ax, 1
%endif
    add ax, bp
    xchg ax, di
    xor ax, ax
%if VD_U > 1
    and bx, VD_U - 1                ; ...entered at count mod U, a whole
    shl bx, 1                       ; round (.tab's first) at 0
    jmp [cs:bx + .tab]
%endif                              ; (a round of ONE is a plain loop)
%assign i VD_U
%rep VD_U
.r %+ i:
    VD_SKIP %3
    %2
%assign i i-1
%endrep
    loop .r %+ VD_U
    jmp .seg
.abs:
    and al, 0x7F                    ; 80h: a count of 0 is no segment, and
    jz .seg                         ; not the 65,536 rounds `loop` makes of it
%if VD_U > 1
    mov bx, ax
    add al, VD_U - 1
%endif
    xchg ax, cx
    VD_SHRU
%if VD_U > 1
    and bx, VD_U - 1
    shl bx, 1
    jmp [cs:bx + .atab]
%endif
%assign i VD_U
%rep VD_U
    VD_AE %2, i, %3
%assign i i-1
%endrep
    loop .a %+ VD_U
    xor ax, ax                      ; the invariant, back
    jmp .seg
%if VD_U > 1
.tab:
    dw .r %+ VD_U
%assign i 1
%rep VD_U - 1
    dw .r %+ i
%assign i i+1
%endrep
.atab:
    dw .a %+ VD_U
%assign i 1
%rep VD_U - 1
    dw .a %+ i
%assign i i+1
%endrep
%endif
%endmacro

; VD_PAR p - a pad where nothing runs, so the next byte is at an address of
; parity p (0 even). WORD-ALIGNED LOOP HEADS: on a 16-bit bus (an 8086, a
; V30, every 286) a jump to an odd address fetches a byte it cannot use, so
; each list's .seg and its round's head - the two jumps a segment takes -
; are put on even addresses; an 8088 fetches a byte at a time and does not
; care. Each use below is placed for the head length of the list after it
; (its .seg's offset from its label): an edit that moves one must move its
; VD_PAR with it (tools/os88rdiff.py's map shows them). The package is
; loaded at a paragraph, so an offset's parity is the address's
%macro VD_PAR 1
    times (($ - $$ + %1) & 1) db 0x90
%endmacro

..@vd_beg:                          ; (the decoder's first byte, for its end)
    VD_PAR 0                        ; (vd_n_p1's .seg, 20 + 22 on)
; vd_nmap - vd_native for a SUB2 sub-record (SPEC.md 98.1.3.2.4): DS:SI is
; its LIST MAP, a word whose bit k says list k is there, and then those
; lists alone. Each list's head shifts the map once - `shr` and a jump, ~35
; cycles, where an empty list's own 0 cost ~50 and a byte of the file
vd_nmap:
    cld
    lodsw
    mov [cs:vd_map], ax
    jmp short vd_ngo1

vd_native:
    mov word [cs:vd_map], 0xFFFF    ; every list there (vd_nmap's map, full)
    cld
vd_ngo1:
    xor ax, ax
    xor cx, cx                      ; the run list sets only CL: CH must be 0
                                    ; ...and the lists CHAINED, each ending in
                                    ; a jump to the next and the last in the
                                    ; ret: ten calls were ~250 cycles a frame.
                                    ; The entry falls into the first
    VD_NPOKE vd_n_p1, VD_ST1, 0, vd_n_p2, 1
    VD_PAR 0
    VD_NPOKE vd_n_p2, VD_ST2, 0, vd_n_p3, 1
    VD_PAR 0
    VD_NPOKE vd_n_p3, VD_ST3, 0, vd_n_p4, 1
    VD_PAR 0
    VD_NPOKE vd_n_p4, VD_ST4, 0, vd_n_p5, 1
    VD_PAR 0
    VD_NPOKE vd_n_p5, VD_ST5, 0, vd_n_p6, 1
    VD_PAR 0
    VD_NPOKE vd_n_p6, VD_ST6, 0, vd_n_slice, 1

; VD_NSPAN name, entry, c160, next - a Duff-entered list of spans, four to a
; round, ending in a jump to `next` as VD_NPOKE does. The
; round counter is DX, because a span's own rep needs CX (left 0 by it, which
; the next entry's `mov cl` relies on)
%macro VD_NSPAN 2-5 0, 0, 0
%1:
%if %5
    VD_GATEN %4                     ; (`next` is in reach: the body is short)
%endif
.seg:
    VD_YCK                          ; (a span segment's head: 98.3.18.11)
    lodsb
    or al, al
%if %5
    jz %4                           ; (straight on: no .done to jump through)
%else
    jz .done
%endif
    mov bx, ax                      ; the count (AH = 0),
    add al, 3                       ; rounds of four, the partial one too
    xchg ax, dx                     ; (AX: lodsw's, next)
    shr dx, 1
    shr dx, 1
    lodsw
%if %3
    shl ax, 1
%endif
    add ax, bp
    xchg ax, di
    xor ax, ax                      ; (CX is 0 at every list's segment: each
    and bx, 3                       ; store before leaves it so, and the
                                    ; entries zero it)
    shl bx, 1
    jmp [cs:bx + .tab]
.r4:
    %2
.r3:
    %2
.r2:
    %2
.r1:
    %2
    dec dx
    jnz .r4
    jmp .seg
%ifn %5
.done:
%ifidn %4, 0
    ret
%else
    jmp %4
%endif
%endif
.tab:
    dw .r4, .r1, .r2, .r3
%endmacro

; a SLICE: skip, len8, the bytes
%macro VD_SLICE 0
    lodsb
    add di, ax                      ; the skip
    lodsb
    mov cx, ax                      ; the length (AH = 0)
    shr cx, 1
    rep movsw                       ; ~12.5 cycles a byte where movsb is 17
    adc cx, cx
    rep movsb
%endmacro

; a RUN: skip, then len8 and the value in ONE word
%macro VD_RUN 0
    lodsb
    add di, ax                      ; the skip
    lodsw                           ; AL = the length, AH = the value
    mov cl, al                      ; CH = 0: the last rep left CX = 0
    mov al, ah                      ; AX = the value twice
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb
    xor ah, ah                      ; the invariant, back
%endmacro

    VD_PAR 1                        ; (a GATEN head is 7 bytes)
    VD_NSPAN vd_n_slice, VD_SLICE, 0, vd_n_run, 1
    VD_PAR 1
    VD_NSPAN vd_n_run, VD_RUN, 0, vd_n_slicel, 1

; the long ones: a span of 256+ bytes, len16 - rare, so a plain loop
vd_n_slicel:
    VD_GATEN vd_n_runl, vd_map + 1
.seg:
    lodsb
    or al, al
    jz vd_n_runl
    mov dx, ax
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_YCK                          ; (a long span: 98.3.18.11)
    lodsb
    add di, ax
    lodsw
    mov cx, ax
    shr cx, 1
    rep movsw
    adc cx, cx
    rep movsb
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg

vd_n_runl:
    VD_GATEN 0, vd_map + 1
.seg:
    lodsb
    or al, al
    jz .ret
    mov dx, ax
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_YCK                          ; (a long span: 98.3.18.11)
    lodsb
    add di, ax
    lodsw
    mov cx, ax                      ; the length
    lodsb
    mov ah, al                      ; the value twice
    shr cx, 1
    rep stosw
    adc cx, cx
    rep stosb
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.ret:
    ret

; =============================================================================
; WRITE MODE 3 (SPEC.md 98.1.3.2.4): a VGA4 sub-record led 10h + c is ONE
; COLOUR, c, and every byte its lists carry is a MASK - the pixels of that
; byte that take c, in all four planes at once. The caller has the GC in read
; mode 1 / write mode 3 with Color Don't Care 0, so a read answers FFh and
; loads the latches, and `and [es:di], al` writes c under AL and the byte as
; it was under the rest: one store a byte, and never a moment in which the
; byte holds some of its planes and not others (the tearing it exists for).
; A RUN of FFh needs no read at all - every bit is c - so it is `rep stosb`,
; 1.15 us a byte against the AND's 3.20 on the owner's 286 (VM3BENCH).
;
; vd_wmap - DS:SI = the sub-record's list map and its lists, ES the screen,
; BP the origin: vd_nmap's contract. Clobbers AX, BX, CX, DX, DI
; =============================================================================
    VD_PAR 0                        ; (vd_w_p1's .seg: 10 + 22 on)
vd_wmap:
    cld
    lodsw
    mov [cs:vd_map], ax
    xor ax, ax
    xor cx, cx                      ; (on into vd_w_p1, which follows)

%macro VD_W1 0
    lodsb
    and [es:di], al
    inc di
%endmacro
%macro VD_W2 0
    VD_W1
    VD_W1
%endmacro
%macro VD_W3 0
    VD_W2
    VD_W1
%endmacro
%macro VD_W4 0
    VD_W2
    VD_W2
%endmacro
%macro VD_W5 0
    VD_W4
    VD_W1
%endmacro
%macro VD_W6 0
    VD_W4
    VD_W2
%endmacro

; (unrolled less than vd_native's: a colour sub-record's bytes are the few
; a frame's tearing reaches, VIDEO-PALETTE-PLAN 10.6, and a round's `loop`
; is ~5% of a store that reads and writes the card - where a round of eight
; was 1,702 bytes of a package at 55.8 KB of its 60)
    VD_NPOKE vd_w_p1, VD_W1, 0, vd_w_p2, 1, 4
    VD_PAR 1                        ; (its gate's jump on is SHORT: 21)
    VD_NPOKE vd_w_p2, VD_W2, 0, vd_w_p3, 1, 2
    VD_PAR 1                        ; (gate 2's head: 19)
    VD_NPOKE vd_w_p3, VD_W3, 0, vd_w_p4, 2, 1     ; (a round of one is
    VD_PAR 1                                      ; short enough that the
    VD_NPOKE vd_w_p4, VD_W4, 0, vd_w_p5, 2, 1     ; next list is in a short
    VD_PAR 1                                      ; jump's reach: gate 2)
    VD_NPOKE vd_w_p5, VD_W5, 0, vd_w_p6, 2, 1
    VD_PAR 1
    VD_NPOKE vd_w_p6, VD_W6, 0, vd_w_slice, 2, 1

; a SLICE of masks: skip, len8, the masks, a read and a write each
%macro VD_WSLICE 0
    lodsb
    add di, ax
    lodsb
    mov cx, ax
    jcxz %%z                        ; (a file's 0 is none, 98.1.6)
%%m:
    lodsb
    and [es:di], al
    inc di
    loop %%m
%%z:
%endmacro

; a RUN of one mask: skip, len8 and the mask in a word. FFh is the run that
; is common - a stretch of one colour - and takes no read
%macro VD_WRUN 0
    lodsb
    add di, ax
    lodsw
    mov cl, al
    mov al, ah
    call vd_wfill
    xor ah, ah
%endmacro

    VD_PAR 1
    VD_NSPAN vd_w_slice, VD_WSLICE, 0, vd_w_run, 1
    VD_PAR 1
    VD_NSPAN vd_w_run, VD_WRUN, 0, vd_w_slicel, 1

; vd_wfill - CX bytes of mask AL at ES:DI, write mode 3. Out: CX = 0, DI
; past them. A length of 0 is none
vd_wfill:
    jcxz .r
    cmp al, 0xFF
    jne .m
    rep stosb
.r:
    ret
.m:
    and [es:di], al
    inc di
    loop .m
    ret

vd_w_slicel:
    VD_GATEN vd_w_runl, vd_map + 1
.seg:
    lodsb
    or al, al
    jz vd_w_runl
    mov dx, ax
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_YCK                          ; (a long span: 98.3.18.11)
    lodsb
    add di, ax
    lodsw
    mov cx, ax
    jcxz .n
.m:
    lodsb
    and [es:di], al
    inc di
    loop .m
.n:
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg

vd_w_runl:
    VD_GATEN 0, vd_map + 1
.seg:
    lodsb
    or al, al
    jz .ret
    mov dx, ax
    lodsw
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_YCK                          ; (a long span: 98.3.18.11)
    lodsb
    add di, ax
    lodsw
    mov cx, ax
    lodsb
    call vd_wfill
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.ret:
    ret

; -----------------------------------------------------------------------------
; vd_walk - DS:SI = a SUB2 sub-record's list map and its lists: every byte
; they write handed to [cs:vw_fn] in turn, DI = its address + BP and AL its
; value, and SI past them. The slow, general form, for the two places a
; colour sub-record is not stored by the GC - round the text box (vd_clip)
; and into a RAM image (vp_decram) - which a play reaches only with text up
; or through a shadow. [vw_fn] may clobber AX, BX, CX, DX and must keep SI,
; DI and BP. Clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
    VD_PAR 0                        ; (.ent, .bv and .rv even with it)
vd_walk:
    cld
    lodsw
    mov [cs:vd_map], ax
    mov byte [cs:vw_lst], 0         ; the list, 0..9
.list:
    shr word [cs:vd_map], 1
    jnc .next
.seg:
    lodsb
    xor ah, ah
    or al, al
    jz .next
    mov byte [cs:vw_abs], 0
    jns .sk                         ; (the `or`'s sign: a mov keeps flags)
    and al, 0x7F
    jz .seg                         ; (80h: no segment)
    mov byte [cs:vw_abs], 1
    jmp short .cnt
.sk:
    push ax
    lodsw
    add ax, bp
    mov di, ax
    pop ax
.cnt:
    mov [cs:vw_n], ax
.ent:
    cmp byte [cs:vw_abs], 0
    je .skip
    lodsw
    add ax, bp
    mov di, ax
    jmp short .len
.skip:
    lodsb
    xor ah, ah
    add di, ax
.len:
    mov al, [cs:vw_lst]
    xor ah, ah
    cmp al, 6
    jae .span
    inc ax                          ; P1..P6: 1..6 bytes, each its own
    mov cx, ax
    jmp short .bytes
.span:
    test al, 8                      ; 6 SLICE, 7 RUN: len8; 8, 9: len16
    jnz .l16
    lodsb
    xor ah, ah
    jmp short .l
.l16:
    lodsw
.l:
    mov cx, ax
    test byte [cs:vw_lst], 1        ; a RUN: one value, CX times
    jz .bytes
    lodsb
    jcxz .done1
.rv:
    push ax
    push cx
    call [cs:vw_fn]
    pop cx
    pop ax
    inc di
    loop .rv
    jmp short .done1
.bytes:
    jcxz .done1
.bv:
    lodsb
    push cx
    call [cs:vw_fn]
    pop cx
    inc di
    loop .bv
.done1:
    dec word [cs:vw_n]
    jnz .ent
    jmp .seg
.next:
    inc byte [cs:vw_lst]
    cmp byte [cs:vw_lst], 10
    jb .list
    ret

vd_map:       dw 0xFFFF             ; SUB2's list map, spent a bit a list
vd_ylim:      dw 0xFFFF             ; VD_YCK's limit: FFFFh but inside an
                                    ; in-place decode (SPEC.md 98.3.18.11)
%ifndef VD_YIELD
vd_yield:                           ; (an includer with no in-place decode:
    ret                             ; never reached, its sites never patched)
%else
; vd_ytab - every VD_YCK site, for vd_ypatch
vd_ytab:
%assign vd_i 1
%rep vd_ny
    dw ..@vd_y %+ vd_i
%assign vd_i vd_i + 1
%endrep
VD_NY       equ vd_ny

; vd_ypatch - AL = 1: every yield site live (an in-place session starting),
; 0: every one a jump over itself again. CS = DS = the package. Preserves
; all but flags
vd_ypatch:
    push bx
    push cx
    push si
    mov cx, VD_NY
    mov si, vd_ytab
.s:
    mov bx, [si]
    mov word [bx], 0x0AEB           ; jmp short +10
    or al, al
    jz .n
    mov word [bx], 0x9090
.n:
    inc si
    inc si
    loop .s
    pop si
    pop cx
    pop bx
    ret
%endif
vw_fn:        dw 0                  ; vd_walk's per-byte routine
vw_n:         dw 0                  ; ...the entries left in a segment
vw_lst:       db 0                  ; ...the list it is in
vw_abs:       db 0                  ; ...and whether its addresses are absolute

%ifdef VD_C160
; =============================================================================
; THE C160 DECODER (SPEC.md 98.3.12.1): the same ten lists, written STRAIGHT
; to the text screen. A C160 file's addresses are the PACKED image's - a byte
; per cell, the attribute - and the screen holds each at the odd address of a
; two-byte cell, so canvas byte a is screen byte 2a + BP, BP being twice the
; packed origin plus one. It replaced a decode into a RAM shadow and a copy of
; every dirty row at the stride of two, which measured FOUR TIMES the decode:
; 67% of a 5150 against 16% on a 160 x 75 canvas (VIDEO-PLAN 15.8).
;
; Every change is its bytes at every other address: `movsb / inc di` each,
; the character byte between them never touched (it is 0DEh for the whole
; play, vp_c16fill). A RUN writes the cells whole - `rep stosw` of 0DEh and
; the value - which is the one place the character is stored, as itself.
;
; in:       DS:SI = the ten lists, ES = the text screen, BP = 2 x origin + 1
; out:      SI past the last list
; clobbers: AX, BX, CX, DX, DI, flags. DS, ES and BP are preserved
; =============================================================================
%macro VD_CST1 0
    movsb
    inc di
%endmacro
%macro VD_CST2 0
    VD_CST1
    VD_CST1
%endmacro
%macro VD_CST3 0
    VD_CST2
    VD_CST1
%endmacro
%macro VD_CST4 0
    VD_CST2
    VD_CST2
%endmacro
%macro VD_CST5 0
    VD_CST4
    VD_CST1
%endmacro
%macro VD_CST6 0
    VD_CST4
    VD_CST2
%endmacro

    VD_PAR 0                        ; (vd_c_p1's .seg: 3 + 5 + 12 on)
vd_c_p1d:
    jmp vd_c_p2                     ; (vd_c_p1's END, in front of the entry so
vd_c160:                            ; that the entry falls into the list)
    cld
    xor ax, ax
    xor cx, cx                      ; (chained, as vd_native's are)
    VD_NPOKE vd_c_p1, VD_CST1, 1, vd_c_p2, 0, 0, vd_c_p1d
    VD_PAR 1                        ; (its END in front: 3 + 12)
    VD_NPOKE vd_c_p2, VD_CST2, 1, vd_c_p3
    VD_PAR 1
    VD_NPOKE vd_c_p3, VD_CST3, 1, vd_c_p4
    VD_PAR 1
    VD_NPOKE vd_c_p4, VD_CST4, 1, vd_c_p5
    VD_PAR 1
    VD_NPOKE vd_c_p5, VD_CST5, 1, vd_c_p6
    VD_PAR 1
    VD_NPOKE vd_c_p6, VD_CST6, 1, vd_c_slice

; a SLICE: skip, len8, the bytes - a cell's attribute each, eight to a round
%macro VD_CSLICE 0
    VD_SKIP 1
    lodsb
    mov cx, ax                      ; the length (7..255, AH = 0)
    call vd_c_mov
%endmacro

; a RUN: skip, then len8 and the value - the cells whole, 0DEh and the value
%macro VD_CRUN 0
    VD_SKIP 1
    lodsw                           ; AL = the length, AH = the value
    mov cl, al                      ; CH = 0: the last rep left CX = 0
    mov al, 0xDE
    dec di                          ; (the cell's character byte)
    rep stosw
    inc di
    xor ah, ah                      ; the invariant, back
%endmacro

    VD_PAR 0                        ; (no gate: .seg is the label)
    VD_NSPAN vd_c_slice, VD_CSLICE, 1, vd_c_run
    VD_PAR 0
    VD_NSPAN vd_c_run, VD_CRUN, 1, vd_c_slicel

; vd_c_mov - CX bytes from DS:SI, each to an odd address from ES:DI:
; eight to a round, entered Duff-style. Out: CX = 0, BX clobbered. A
; file's 0 is none, not 65,536 rounds of a `loop` (98.1.6)
    VD_PAR 1                        ; (its round's heads at odd offsets)
vd_c_mov:
    jcxz .z
    mov bx, cx
    and bx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1
    or bx, bx
    jz .r8
    inc cx
    shl bx, 1
    jmp [cs:bx + .tab]
.r8:
    VD_CST1
.r7:
    VD_CST1
.r6:
    VD_CST1
.r5:
    VD_CST1
.r4:
    VD_CST1
.r3:
    VD_CST1
.r2:
    VD_CST1
.r1:
    VD_CST1
    loop .r8
.z:
    ret
.tab:
    dw 0, .r1, .r2, .r3, .r4, .r5, .r6, .r7

vd_c_slicel:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax
    lodsw
    shl ax, 1
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_SKIP 1
    lodsw
    mov cx, ax
    call vd_c_mov
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.done:
    jmp vd_c_runl

vd_c_runl:
.seg:
    lodsb
    or al, al
    jz .done
    mov dx, ax
    lodsw
    shl ax, 1
    add ax, bp
    xchg ax, di
    xor ax, ax
.e:
    VD_SKIP 1
    lodsw
    mov cx, ax                      ; the length
    lodsb
    mov ah, al                      ; the value, over 0DEh
    mov al, 0xDE
    dec di
    rep stosw
    inc di
    xor ax, ax
    dec dx
    jnz .e
    jmp short .seg
.done:
    ret
; ...and the decoder's LENGTH at the parity it had (4,897 bytes, the size
; pass's base): every VD_PAR above pins an ADDRESS, so the pads it takes
; differ with where the package puts the decoder - VP_NOLIVESND's is a byte
; on from the plain build's - and this evens that out, so what follows keeps
; its parity in every arm
    times ((($ - ..@vd_beg) - 4897) & 1) db 0x90
%endif
