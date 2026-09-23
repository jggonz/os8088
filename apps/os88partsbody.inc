; =============================================================================
; os8088 - apps/os88partsbody.inc
;
; THE PARTS STANDARD'S CODE (SPEC.md 20.12), emitted by OS88_PARTS_CODE.
;
; **DO NOT %include THIS FILE.** It is the second half of apps/os88parts.inc
; and it is emitted for you, after your table - which is the whole reason it
; is a file of its own. The gates below read OP_HAS_XMS, OP_HAS_LAZY,
; OP_HAS_COMP, OP_HAS_ZERO and OP_HAS_OPT, and those are DERIVED FROM YOUR
; TABLE by the OS88_PART macro, so they do not exist until the table has been
; written. Code emitted at the include point cannot be gated on a declaration
; that comes after it.
;
; WHAT THE GATES ARE FOR. The standard grew through five waves after its cost
; was last estimated - scratch and optional parts, XMS, lazy parts,
; compression, the C SDK - and every one of those waves measured its bill in
; KERNEL bytes and found zero, which was true and was the wrong number to
; watch. The PACKAGE's bill went 500-700 (estimated,
; docs/plans/completed/O88-MULTISEG-PLAN.md 6.4) to 2,536 measured, and a
; package using one plain part carried every byte of it. It carries 800 now,
; and a table with every flag on emits what it always did (SPEC.md 20.12.9).
;
; A GATE HERE IS NOT A SIZE KNOB. It is the observation that a package which
; declares no OP_LAZY row can never reach op_fetch, and the call graph says so
; exactly: op_fetch, op_drop, op_lazyok and op_lin are public entry points
; reached from nothing inside this file, and op_xload, op_unpack and op_scrub
; have one caller each in op_load, behind a test that is false for every row
; of such a table. Nothing here decides what a package may do; the table does,
; and this follows it.
; =============================================================================

; -----------------------------------------------------------------------------
; op_load - claim and read every part (SPEC.md 20.12)
; in:  SI = the entry proc's, still holding the kernel's name pointer, and
;      ES = KERNEL_SEG - so call this FIRST, from the entry proc
; out: CF=0 and op_seg answers for every part; CF=1 = refused, and a toast
;      has already said why
; clobbers: AX, BX, CX, DX, SI, DI, ES
;
; THE ORDER IS THE POINT. Sizing comes first and reads nothing: the table is
; already in front of us, so a machine that cannot fit this package is told so
; before a single disk revolution is spent - which is the third thing the
; whole design exists for (docs/plans/completed/O88-MULTISEG-PLAN.md 0).
; -----------------------------------------------------------------------------
op_load:
    ; --- the name, out of the kernel's buffer and into ours ----------------
    ; A byte loop and not a rep movsb: the source is ES:SI and the
    ; destination DS:DI, which is the wrong way round for a string move, and
    ; swapping the segment registers to suit it costs more than this does.
    mov di, op_name
    mov cx, 13
.copy:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .copy
    mov byte [op_name+12], 0    ; ...terminated whatever arrived

    push ds
    pop es                      ; ES is ours from here: every slot below that
                                ; takes ES:SI or ES:BX means OUR buffer

%if OP_HAS_XMS
    ; --- is there a store to put the OP_XMS parts in? ----------------------
    ; ASKED FIRST, because the answer moves those parts between two totals:
    ; with a store they are a span of their own above 1MB, and without one
    ; they are ordinary filed parts inside the carved run. That is what makes
    ; OP_XMS a HINT and not a mode (SPEC.md 20.12.4), and on every 8088 - the
    ; machine this OS targets - there is no store at all (SPEC.md 41).
    ; OSAPI_XMEM_CAPS reads two bytes and is legal in any context.
    ;
    ; GATED (20.12.9): with no OP_XMS row there is nothing the answer could
    ; move, so the question is not asked and [op_xmson] stays the 0 the loader
    ; zeroed it to - which is what every test of it below reads.
    mov byte [op_xmson], 0
    call OSAPI_XMEM_CAPS        ; AX = KB the pool can still hand out
    or ax, ax
    jz .nostore
    mov byte [op_xmson], 1
.nostore:
%endif

    ; --- size, from the table alone: NO DISK IS READ HERE ------------------
    call op_size
    jc .toobig
%if OP_HAS_XMS
    cmp byte [op_xmson], 0
    je .sized
    call OSAPI_XMEM_CAPS        ; ...and what the store must actually hold. A
                                ; second CAPS rather than a trial allocation:
    cmp ax, [op_xmskb]          ; a question costs nothing and a claim that is
    jae .sized                  ; going to be given back is a claim somebody
    mov byte [op_xmson], 0      ; else could have had in between
    call op_size                ; ...so size it again, conventionally
    jc .toobig
.sized:
%endif
    mov ax, [op_allkb]
    or ax, [op_reqkb]
%if OP_HAS_XMS
    jnz .grant
    ; Nothing conventional to claim - but a table of nothing but OP_XMS rows,
    ; on a machine whose store took them, still has a SPAN to load, and it
    ; needs the cluster size the same as a carve does. Without this arm the
    ; span was never read and op_seg answered 0 for every part on a 286, while
    ; the same package worked on an 8088 where the store is absent and the
    ; rows fall back into the carve.
    cmp word [op_xsecs], 0
    je .done                    ; a table of nothing to claim
    call op_clbget
    jc .nodisk
    call op_xload
    jc .ioerr
    jmp short .done
%else
    jz .done                    ; NOTHING TO CLAIM AT ALL - which without an
                                ; OP_XMS row means there is nothing to do
                                ; either, so this is the whole of the arm
                                ; above. A table of one OP_LAZY row is the
                                ; case that reaches it
%endif
.grant:

    ; --- what the machine will grant, and the OPTIONAL parts are the give --
    ; ALL OR NONE, and one retry (SPEC.md 20.12.4). Dropping optional parts
    ; one at a time is a search, and a search whose order the author cannot
    ; predict is worse than a rule they can: a package that wants finer
    ; control declares the parts it can live without and asks op_optok which
    ; way it went.
    mov byte [op_optok], 1      ; ...and it STAYS 1 with no OP_OPT row, which
                                ; is the true answer to `were the optional
                                ; parts granted` when there are none
    call OSAPI_MEM_AVAIL        ; AX = the largest free run, in KB
%if OP_HAS_OPT
    mov bx, ax
    cmp bx, [op_allkb]
    jae .fits
    mov byte [op_optok], 0
    cmp bx, [op_reqkb]
    jb .nomem
.fits:
    mov ax, [op_allkb]
    cmp byte [op_optok], 0
    jne .kbok
    mov ax, [op_reqkb]
.kbok:
%else
    cmp ax, [op_reqkb]          ; GATED (20.12.9): with nothing optional the
    jb .nomem                   ; two totals are the same number, so there is
    mov ax, [op_reqkb]          ; no give to make and no retry to make it on
%endif
    mov [op_kb], ax

    ; --- the cluster size, which is what the alignment turns on ------------
    ; ONLY WHEN THERE IS A RUN. A table of nothing but scratch reads no disk
    ; at all, and asking a volume its geometry when there is nothing to read
    ; off it would make a memory operation depend on a disk being there.
    cmp word [op_secs], 0
%if OP_HAS_XMS
    jne .clb
    cmp word [op_xsecs], 0      ; ...and the SPAN reads the disk too, so a
    je .claim                   ; table of scratch plus OP_XMS rows asks
.clb:                           ; as well - op_xload with [op_clb] = 0 refused
                                ; every such package as `Cannot read my parts`
%else
    je .claim                   ; no span to read, so a run of nothing is the
                                ; only way out of asking the volume
%endif
    call op_clbget              ; ...asked once, and cached (op_clbget)
    jc .nodisk
.claim:
    call op_claim               ; ONE claim: the run and every granted scratch
    jc .nomem                   ; part after it
    cmp word [op_secs], 0
    je .zero
    call op_read
    jc .ioerr
%if OP_HAS_COMP
    cmp byte [op_zn], 0         ; ...and if any of it arrived COMPRESSED, walk
    je .zero                    ; it down out of where it landed (20.12.7). It
    call op_unpack              ; is the only thing a compressed part costs at
    jc .fail                    ; run time, and op_unpack has said why
%endif
.zero:
%if OP_HAS_ZERO
    call op_scrub               ; ...and the scratch, which cost no disk
%endif
%if OP_HAS_XMS
    cmp byte [op_xmson], 0
    je .done
    call op_xload               ; ...and the span that goes above 1MB
    jc .ioerr
%endif
.done:
    clc
    ret

%if OP_HAS_COMP
.fail:                          ; op_unpack has already put its own message up
    stc
    ret
%endif
.toobig:
    mov si, op_s_big
    jmp op_say
.nomem:
    mov si, op_s_mem
    jmp op_say
.nodisk:
    mov si, op_s_disk
    jmp op_say
.ioerr:
    mov si, op_s_read
    jmp op_say

; -----------------------------------------------------------------------------
; op_size - how much the carved parts want, from the table alone
; out: CF=0 with [op_first]/[op_secs]/[op_kb]/[op_bytes] filled, and
;      [op_secs] = 0 meaning nothing is filed; CF=1 = it cannot be expressed
; clobbers: AX, BX, CX, DX, SI, flags
;
; NO DISK IS READ HERE and that is the whole of goal 3: the table is in the
; image the kernel has already read, so a package knows what it needs before
; it has spent a revolution finding out.
;
; The carve is the run from the FIRST filed part's first sector to the LAST
; one's last, so parts contiguous in the file are contiguous in the claim and
; one read serves all of them. os88pkg.py appends in table order, so the rows
; ascend and the first filed row anchors the run.
; -----------------------------------------------------------------------------
op_size:
    mov word [op_secs], 0
    mov word [op_bytes], 0
    mov word [op_usecs], 0      ; ...and these two stay ungated: the .norun
    mov word [op_ubytes], 0     ; path writes neither, and op_claim reads
                                ; op_usecs whatever the table says
%if OP_HAS_COMP
    mov byte [op_zn], 0
%endif
    mov word [op_reqkb], 0
%if OP_HAS_OPT
    mov word [op_allkb], 0      ; ...and with nothing optional this stays 0,
                                ; which is what op_load's `or ax, [op_reqkb]`
                                ; then reads: the two totals are one number
%endif
%if OP_HAS_XMS
    mov word [op_xoff], 0
    mov word [op_xsecs], 0
    mov word [op_xmskb], 0
%endif
    mov si, op_table + OP_T_ROWS
    mov cl, [op_t_n]
    xor ch, ch
    xor bx, bx                  ; BX = the run's first sector, 0 = none yet
    xor dx, dx                  ; DX = one past its last
.one:
    or cx, cx                   ; not jcxz: the body outgrew an 8-bit
    jnz .body                   ; displacement, and an 8086 conditional jump
    jmp .run                    ; is short-range too
.body:
    mov ax, [si+OP_R_OFF]
    or ax, ax
%if OP_HAS_ZERO
    jnz .filed
    jmp .scratch                ; no file bytes: it is scratch, and it costs
                                ; no disk at all - which is what makes a
                                ; working set DECLARABLE rather than claimed
                                ; from the entry proc and refused afterwards
%else
    jz .next                    ; no file bytes and no scratch row declared:
                                ; os88pkg.py gives every filed row an offset,
                                ; so there is nothing this can be
%endif
.filed:
%if OP_HAS_LAZY
    test byte [si+OP_R_FLAG], OP_LAZY
    jz .eager
    jmp .next                   ; A LAZY PART IS IN NO TOTAL AND IN NO RUN.
                                ; That is the whole of what lazy is here: not
                                ; a mechanism, a row this loop steps over
                                ; (docs/plans/completed/O88-MULTISEG-PLAN.md 6.3). op_fetch
                                ; claims and reads it later, on its own, and
                                ; os88pkg.py has put every lazy row AFTER the
                                ; run so stepping over one cannot leave a hole
                                ; in the middle of it
.eager:
%endif
%if OP_HAS_XMS
    test byte [si+OP_R_FLAG], OP_XMS
    jz .conv
    cmp byte [op_xmson], 0
    je .conv                    ; no store on this machine, so it falls back
                                ; and is an ordinary filed part - which is
                                ; what makes OP_XMS a HINT and not a mode
    push ax                     ; --- it is going ABOVE 1MB: its own span ----
    or word [op_xoff], 0
    jnz .xspan
    mov [op_xoff], ax           ; the first XMS row anchors that span, and
.xspan:                         ; os88pkg.py has put them all after the
    mov ax, [si+OP_R_LEN]       ; conventional ones
    add ax, 1023
    jc .ovfa
    push cx
    mov cl, 10
    shr ax, cl
    pop cx
    add [op_xmskb], ax
    jc .ovfa
    pop ax
    push ax
    push cx
    mov cx, [si+OP_R_LEN]
    add cx, 511
    jc .ovfx
    push ax
    mov ax, cx
    mov cl, 9
    shr ax, cl
    mov cx, ax
    pop ax
    add ax, cx
    pop cx
    sub ax, [op_xoff]
    cmp ax, [op_xsecs]
    jbe .xdone
    mov [op_xsecs], ax
.xdone:
    pop ax
    jmp .next
.conv:
%endif
    or bx, bx
    jnz .have
    mov bx, ax                  ; the first filed row anchors the run
.have:
    push cx
    push ax
%if OP_HAS_COMP
    mov ax, [si+OP_R_LEN]       ; the UNPACKED total, which is what the CLAIM
    add ax, 511                 ; is cut from - so a compressed row costs the
    jc .ovfd                    ; machine exactly what it would have cost
    mov cl, 9                   ; uncompressed, and refusing still reads no
    shr ax, cl                  ; disk at all (SPEC.md 20.12.7)
    add [op_usecs], ax
    jc .ovfd
    pop ax
    push ax
%endif

    mov cx, [si+OP_R_LEN]       ; ...and the DISK extent, which on a compressed
%if OP_HAS_COMP
    test byte [si+OP_R_FLAG], OP_COMP   ; row is the PACKED length in zkb. The
    jz .clen                            ; two are the same run measured at its
    mov cx, [si+OP_R_ZKB]               ; two ends
    inc byte [op_zn]
.clen:
%endif
    mov [op_bend], cx           ; ...AND THE RUN'S EXACT BYTE END, which the
                                ; sector count below is not. A part is padded
                                ; UP to a sector in the file, but the last one
                                ; is the end of the FILE - so a packed length
                                ; that is not a sector multiple leaves those
                                ; bytes not merely unread but ABSENT, and
                                ; op_want asking for them is `Cannot read my
                                ; parts` on a package that is entirely
                                ; correct. Rows are laid out in increasing
                                ; order, so the last one to reach here wins
    add cx, 511                 ; the row's length is in BYTES; the run is
    jc .ovfd                    ; measured in whole sectors, because that is
    mov ax, cx                  ; the granularity a part is laid out on
    mov cl, 9
    shr ax, cl
    mov cx, ax
    pop ax
    push ax
    sub ax, bx                  ; sectors from the run's start...
    push cx
    mov cl, 9
    shl ax, cl                  ; ...as bytes, and the run is under 64KB
    pop cx
    add [op_bend], ax           ; ...so this is (off - first) * 512 + packed
    pop ax
    add ax, cx
    pop cx
    jc .bad
    cmp ax, dx
    jbe .next
    mov dx, ax
    jmp short .next             ; ...and this row ends the run so far, which
                                ; op_bend above has already recorded EXACTLY.
                                ; [op_tail] used to be measured here as well -
                                ; the padding after the last body - and
                                ; subtracted again in op_claim, which counted
                                ; it twice
%if OP_HAS_ZERO
.scratch:
    push ax
    mov ax, [si+OP_R_ZKB]
%if OP_HAS_OPT
    add [op_allkb], ax          ; every scratch part counts toward the total...
    jc .ovfa
    test byte [si+OP_R_FLAG], OP_OPT
    jnz .optonly
%endif
    add [op_reqkb], ax          ; ...and only a REQUIRED one toward the floor
    jc .ovfa
.optonly:
    pop ax
%endif
.next:
    add si, OP_ROW
    dec cx
    jmp .one

.run:
%if OP_HAS_XMS
    cmp word [op_xsecs], 128    ; the span is climbed through one WORD of
    jae .bad                    ; bytes (op_xload: `shl ax, 9`), so it has the
                                ; same 64KB ceiling the carve has below - 128
                                ; sectors read as 0 there, and 180 as 26,624
%endif
    or bx, bx
    jz .norun
    mov [op_first], bx
    sub dx, bx
    jbe .bad                    ; a run of nothing, out of rows that are filed
    mov [op_secs], dx
    cmp dx, 128
    jae .bad                    ; 64KB OR MORE, and this wave carves neither:
                                ; op_cap is a word and op_claim's head slack
                                ; is added to it, so the whole carve plus a
                                ; cluster has to be expressible as one. A
                                ; later wave chunks the read and lifts it.
                                ; **`ja` HERE WAS AN OFF-BY-ONE AND IT WAS
                                ; SILENT**: a run of exactly 128 sectors
                                ; passed, and `128 << 9` is 65,536, which in a
                                ; word is ZERO - so op_bytes came out 0,
                                ; op_claim asked for the head slack alone and
                                ; op_read moved nothing at all. The package
                                ; would then be handed segments into a claim
                                ; that was never filled
    mov ax, dx
    mov cl, 9
    shl ax, cl                  ; sectors -> bytes; <= 65,536 by the test above
    mov [op_bytes], ax

    ; ...and the same run at its UNPACKED end, which is what the CLAIM is cut
    ; from. With no compressed row the two are equal by construction - the
    ; packer lays each part at roundup512(len) past the one before - so
    ; everything below reads exactly as it did (SPEC.md 20.12.7).
    ;
    ; GATED (20.12.9): "equal by construction" is not a remark, it is the
    ; whole of what a table with no OP_COMP row needs. The second total is
    ; then the first, copied - and the accumulation that built it in the loop
    ; above went with the gate.
%if OP_HAS_COMP
    mov ax, [op_usecs]
    cmp ax, 128
    jae .bad                    ; the carve's own ceiling, on the side that
                                ; decides the claim rather than the read
    push ax
    mov cl, 9
    shl ax, cl
    mov [op_ubytes], ax
    pop ax
%else
    mov [op_ubytes], ax         ; AX is still op_bytes, and DX still op_secs
    mov [op_usecs], dx
    mov ax, dx
%endif
    inc ax
    shr ax, 1                   ; sectors -> KB, rounded up
    add [op_reqkb], ax          ; the carve is never optional: it is one claim
    jc .bad                     ; and one read, and half of it is not a thing
%if OP_HAS_OPT
    add [op_allkb], ax
    jc .bad
%endif
.norun:
    clc
    ret
%if OP_HAS_XMS
.ovfx:                          ; the .xspan path banks AX *and* CX, so its
    pop cx                      ; overflow exit has to give BOTH back: with a
    pop ax                      ; single pop it returned at depth +1 and `ret`
    stc                         ; took the saved AX for its return address
    ret                         ; (SPEC.md 20.12)
%endif
.ovfd:                          ; the .conv arm banks AX *and* CX, so its
    pop ax                      ; overflow exit gives both back, for the same
    pop cx                      ; reason
    jmp short .bad
%if OP_HAS_ZERO
.ovfa:                          ; ...and the scratch arm banks AX alone
    pop ax
%endif
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; op_claim - ONE claim for the whole carved run, plus the alignment slack
; out: CF=0 and [op_base]/[op_slack]/[op_off]/[op_cap] filled; CF=1 = refused
; clobbers: AX, BX, CX, DX, flags
;
; THE SLACK IS THE ALIGNMENT, AND IT COSTS A SEGMENT ADJUSTMENT AND NOTHING
; ELSE (SPEC.md 20.12.2). OSAPI_FILE_READ_AT wants a cluster-aligned offset,
; a cluster is 512 bytes to 32KB, and the volume is not known when the file is
; packed - so the read starts at the cluster boundary AT OR BELOW the run and
; the head slack lands in front of it. Because parts are 512-aligned in the
; file the slack is a multiple of 512 and therefore of 16, and a heap claim is
; paragraph-aligned: part i's base is claim + (slack + its offset) / 16, exact
; on every volume, with no copy and no second buffer. On a 1.44MB floppy the
; cluster is 512 bytes and the slack is zero.
; -----------------------------------------------------------------------------
op_claim:
    mov word [op_slack], 0
    mov word [op_cap], 0
    mov word [op_runkb], 0
    mov word [op_rpara], 0
    cmp word [op_secs], 0
    je .scratchonly             ; nothing to read: the claim is scratch alone
    mov ax, [op_first]          ; the run's first sector -> its byte offset,
    mov dx, ax                  ; in 32 bits: a 1MB file is 2,048 sectors, so
    mov cl, 9                   ; sector * 512 does not fit a word. The high
    shl ax, cl                  ; half of (n << 9) is (n >> 7), which is two
    mov cl, 7                   ; shifts rather than a 32-bit one the 8086
    shr dx, cl                  ; does not have

    mov bx, [op_clb]            ; slack = offset mod cluster bytes, and a
    dec bx                      ; cluster size is a power of two - so this is
    and bx, ax                  ; an AND, and only the low word can carry it
    mov [op_slack], bx
    sub ax, bx
    sbb dx, 0
    mov [op_off], ax            ; ...and the read starts there
    mov [op_off+2], dx

    mov ax, [op_bend]           ; capacity = slack + the run, rounded up to a
    add ax, bx                  ; whole number of clusters, which is the
    jc .no                      ; slot's other precondition. THE CARRY IS THE
    mov [op_want], ax           ; ...and before the rounding, THIS is what has
                                ; to arrive - op_read's fourth input. NOT less
                                ; [op_tail]: op_bend is the run's EXACT byte
                                ; end (see op_size) and so already excludes the
                                ; padding after the last body, which is what
                                ; the subtraction here was for. Doing both
                                ; counted it TWICE - harmless on a large run
                                ; only in the sense that it asked for `tail`
                                ; bytes FEWER than it should have, weakening
                                ; the one check that says the run was not
                                ; short - and fatal on a small one: a run
                                ; shorter than that padding WRAPPED this word
                                ; and `Cannot read my parts` came back for a
                                ; package that was entirely correct. A parts
                                ; payload under 512 bytes could not load at
                                ; all (SPEC.md 20.12.7.3)
                                ; BOUND op_size could not apply: the slack is
                                ; up to a cluster short of 32KB and is not
                                ; known until here, so a 64KB run on a
                                ; 32KB-cluster volume overflows this word. It
                                ; is refused rather than wrapped
    mov dx, [op_clb]
    dec dx
    add ax, dx
    jc .no
    not dx
    and ax, dx
    mov [op_cap], ax

    ; --- R: how far UP the packed run lands (SPEC.md 20.12.7) --------------
    ; The image's trick one level up, and the same arithmetic: read the packed
    ; bytes high enough that expanding them downwards can never overtake the
    ; source. At the start of part i the write pointer is at sum(len) and the
    ; read pointer at R + sum(packed), both over the rows before it - so the
    ; gap is R - (unpacked - packed) accumulated, which is at its smallest at
    ; the very end and is OP_MARGIN there by construction. Within one part the
    ; measured margin bounds the dip, and os88pkg.py refuses a part that needs
    ; more than OP_MARGIN.
    ;
    ; **AND THE EXTRA SECTOR IS NOT A ROUNDING HABIT.** Both totals above are
    ; measured in whole SECTORS, so a row contributes roundup512(len) to one
    ; and roundup512(packed) to the other - and what row i actually needs is
    ; (len - packed) + its own measured margin, in EXACT bytes. The telescoping
    ; leaves gap(i) >= (roundup512(len) - roundup512(packed)) + OP_MARGIN,
    ; which is short of that by up to 511 when packed lands one byte past a
    ; sector boundary and len lands on one. 512 closes it for every row at
    ; once, and it is 512 bytes of claim ONCE - not per part.
%if OP_HAS_COMP
    cmp byte [op_zn], 0
    je .plainrun                ; nothing compressed: R is 0 and the run lands
    mov ax, [op_ubytes]         ; at the base, exactly as it always has
    sub ax, [op_bytes]
    add ax, OP_MARGIN + 512 + 15
    jc .no
    and ax, 0FFF0h
    mov cl, 4
    shr ax, cl
    mov [op_rpara], ax          ; ...in paragraphs, which is what a segment
.plainrun:                      ; adjustment takes
%endif

    ; The claim's share for the run is R PLUS THE READ, and that one quantity
    ; covers both things that have to fit: the read occupies R + op_cap from
    ; the base, and the unpacked carve occupies slack + op_ubytes - and
    ; R + op_cap >= (unpacked - packed + margin) + (slack + packed), which is
    ; slack + unpacked + margin, so the read is always the larger. With
    ; nothing compressed R is 0 and this is the line that was here before.
%if OP_HAS_COMP
    mov ax, [op_rpara]
    mov cl, 4
    shl ax, cl                  ; R, in bytes - it is under 64KB because
    add ax, [op_cap]            ; op_ubytes is (op_size refuses 128 sectors)
    jc .no
%else
    mov ax, [op_cap]            ; ...and "with nothing compressed R is 0" is
                                ; the line that was here before compression,
                                ; which is exactly what the gate restores
%endif
    add ax, 1023                ; the READ's share of the claim, in KB - the
    jc .no                      ; slack included, because the read starts at
    mov cl, 10                  ; the cluster below the run and those bytes
    shr ax, cl                  ; land in front of it
    mov [op_runkb], ax          ; ...and this is where the scratch begins

.scratchonly:
    ; ONE CLAIM FOR ALL OF IT. MEM_OWNER_MAX is 8 claims per owner word
    ; (SPEC.md 50) and a package's own claims carry its segment, so a package
    ; with six parts must not make six claims. The scratch that was GRANTED is
    ; op_kb less the run's own KB - op_size put the run in both totals, and
    ; op_kb is whichever of them the machine allowed.
    mov dx, [op_usecs]
    inc dx
    shr dx, 1                   ; DX = ceil(run sectors / 2), op_size's figure
    mov bx, [op_kb]             ; - and op_size's is the UNPACKED one, which is
                                ; what it put in both totals
    sub bx, dx                  ; BX = the granted scratch, in KB
    mov ax, [op_runkb]
    add ax, bx
    jc .no
    or ax, ax
    jz .no                      ; a claim of nothing is not a claim
    ; **TOP-DOWN, LIKE THE REGION IT MAY BECOME** (SPEC.md 20.12.10.8). A
    ; package's region is claimed top-down (`mem_claim_hi_x`) and so are a
    ; driver image and a kernel module: the top of the heap IS the set of
    ; CS-based claims, and this is one - it is code you far-call, and on a
    ; re-home it becomes the region itself. Claimed bottom-up it landed at the
    ; FLOOR, left the loader's freed region as a hole beneath it that no single
    ; contiguous claim can use, and cost the DOS box 6 KB of the arena it hands
    ; its program. Measured there: 437 -> 431 KB, 5,120 of hole and 1,024 of
    ; carve head slack. The hole SURVIVED a posted compaction, which is an
    ; open question and not this routine's - SPEC.md 20.12.10.8.1 has the
    ; three measurements and the repro.
    call OSAPI_MEM_CLAIM_HI     ; out DX = the base segment
    jc .no
    mov [op_base], dx
    add dx, [op_rpara]          ; ...and R paragraphs up is where op_read fills
    mov [op_rdseg], dx
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; op_read - the carved run, OP_CHUNK bytes at a time
; out: CF=0; CF=1 = the file could not be read
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
;
; ONE READ PER CHUNK AND NOT ONE PER PART: the parts are contiguous in the
; file and contiguous in the claim, so this is the carve the kernel-side
; design had, arrived at by arithmetic instead of by a kernel mechanism.
;
; IN: [op_off] where, [op_cap] how much to ask for, [op_base] where it lands,
; and [op_want] HOW MUCH MUST ACTUALLY ARRIVE - which is not [op_cap], because
; that is rounded up to a cluster and the file may end inside the last one.
; The caller sets all four: op_claim for the carve, op_fetch for a lazy part.
; It is the caller's because the two reads measure different things, and
; computing it here from the CARVE's words is what made a lazy fetch demand
; the carve's byte count and refuse every time.
; -----------------------------------------------------------------------------
op_read:
    push bp
    mov di, [op_cap]            ; DI = bytes still to move
    mov bp, [op_rdseg]          ; BP = where the next chunk lands - which is
                                ; the claim's base until a part is COMPRESSED,
                                ; and R paragraphs above it when one is
                                ; (SPEC.md 20.12.7). Every caller sets it
.chunk:
    or di, di
    jz .done
    mov cx, OP_CHUNK
    cmp di, cx
    jae .full
    mov cx, di
.full:
    push cx
    mov es, bp
    xor bx, bx
    mov si, op_name
    mov ax, [op_off]
    mov dx, [op_off+2]
    call OSAPI_FILE_READ_AT     ; out DX:AX = the bytes delivered
    pop cx
    jc .fail
    mov bx, ax
    or bx, dx
    jz .done                    ; at or past the end, and [op_want] below is
                                ; what turns that into a refusal

    mov bx, [op_want]           ; ...how much of what MUST arrive just did
    or dx, dx
    jnz .allof                  ; 64KB or more in one chunk cannot happen - a
    cmp ax, bx                  ; chunk is at most OP_CHUNK - so this is the
    jae .allof                  ; whole of what is left either way
    sub bx, ax
    mov [op_want], bx
    jmp short .adv
.allof:
    mov word [op_want], 0
.adv:
    sub di, cx                  ; advance by what was ASKED for, which is a
    add [op_off], cx            ; whole number of clusters; a short final take
    adc word [op_off+2], 0      ; is the contract and the loop ends above
    mov ax, cx
    mov cl, 4
    shr ax, cl                  ; bytes -> paragraphs
    add bp, ax
    jmp short .chunk
.done:
    ; **A SHORT RUN IS A REFUSAL, and it did not used to be.** The loop above
    ; stops when the file runs out, and wave 2 left the consequence to "the
    ; package's own checks on the parts" - which means every package inventing
    ; a way to tell a claim that was filled from one that was not. The
    ; standard can simply tell: it asked for bytes at a known offset and knows
    ; how many arrived. A truncated .O88 - a bad copy, an interrupted write -
    ; now refuses at load with a message, instead of handing the package
    ; segments into a claim holding whatever the heap had. Wave 6's first
    ; consumer is what found it: C64 was about to grow a hand-written "is the
    ; ROM really there" test that belongs here (docs/plans/completed/O88-MULTISEG-PLAN.md 11).
    cmp word [op_want], 0
    jne .fail
    push ds
    pop es
    pop bp
    clc
    ret
.fail:
    push ds
    pop es
    pop bp
    stc
    ret
%if OP_HAS_COMP
; GATED (SPEC.md 20.12.9): op_unpack - only a table with an OP_COMP row can reach it

; -----------------------------------------------------------------------------
; op_unpack - walk the run down out of where it was read (SPEC.md 20.12.7)
; out: CF=0; CF=1 = a stream was refused, and a toast has said so
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
;
; **CALLED ONLY WHEN A ROW IS COMPRESSED**, and it is the whole of what a
; compressed part costs at run time. op_read has put the PACKED run R
; paragraphs up the claim; this walks the rows in order, moving the write
; pointer by roundup512(len) and the read pointer by roundup512(packed), and
; either EXPANDS the row or MOVES it down.
;
; THE GAP IS THE INVARIANT AND IT NEVER CLOSES. At the top of row i the write
; pointer is at sum(len) and the read pointer at R + sum(packed), both over the
; rows already done, so the gap is R - sum(len - packed) - and R was cut as
; (total unpacked - total packed) + OP_MARGIN, so the gap is at its smallest
; at the LAST row and is OP_MARGIN there. Inside one row the decoder's own
; measured margin bounds the dip, and os88pkg.py refuses a part needing more
; than OP_MARGIN. A plain row in between is a forward `rep movsb` down, which
; is safe for the same reason and for the ordinary one as well.
;
; BOTH POINTERS ARE SEGMENTS WITH OFFSET ZERO. Every step is a multiple of 512
; and therefore of 16, so the walk is paragraph arithmetic and nothing here
; can carry past a segment - which is also what OSAPI_DECOMP wants (DI = 0,
; SPEC.md 20.13.3).
; -----------------------------------------------------------------------------
op_unpack:
    push bp
    mov ax, [op_slack]
    mov cl, 4
    shr ax, cl                  ; the slack is a multiple of 512 (20.12.2)
    mov dx, [op_base]
    add dx, ax
    mov bp, dx                  ; BP = where the next row LANDS
    add dx, [op_rpara]
    mov [op_zsrc], dx           ; ...and DX = where its bytes are NOW

    mov si, op_table + OP_T_ROWS
    mov cl, [op_t_n]
    xor ch, ch
.one:
    or cx, cx
    jnz .body
    jmp .done
.body:
    cmp word [si+OP_R_OFF], 0
    je .next                    ; scratch: no file bytes and no place in the run
    test byte [si+OP_R_FLAG], OP_LAZY
    jnz .next                   ; not in the run at all - and never compressed
    test byte [si+OP_R_FLAG], OP_XMS
    jz .inrun
    cmp byte [op_xmson], 0
    jne .next                   ; it went above 1MB and op_xload has it
.inrun:
    push cx
    push si
    test byte [si+OP_R_FLAG], OP_COMP
    jz .plain

    mov cx, [si+OP_R_ZKB]       ; --- expand it ------------------------------
    mov dx, [si+OP_R_LEN]
    push ds
    mov al, [op_table + OP_T_FMT]   ; THE FORMAT, AND IT IS READ HERE BECAUSE
    xor ah, ah                      ; op_table is OUR data - one instruction
                                    ; later DS is the stream's and this byte is
                                    ; unreachable. The first version banked the
                                    ; source SEGMENT in AX instead and handed
                                    ; the decoder its low half as the format,
                                    ; which refused every stream: `A part will
                                    ; not unpack` on a package whose bytes,
                                    ; lengths and pointers were all correct
    mov es, bp
    mov bx, [op_zsrc]
    mov ds, bx
    xor si, si                  ; DS:SI is the stream, at the base of where the
    xor di, di                  ; read put it - **and DI = 0**, which is the
    xor bx, bx                  ; contract (SPEC.md 20.13.3). BX:DX is the
                                ; 32-bit expected output and a part is 16 bits
    call OSAPI_DECOMP           ; by os88pkg.py, so the high word is zero
    pop ds
    push ds
    pop es
    jc .refuse
    pop si
    push si
    mov ax, [si+OP_R_ZKB]       ; the READ pointer moves by the PACKED length
    jmp short .step
.plain:                         ; --- or move it down, which is the same walk -
    mov cx, [si+OP_R_LEN]
    push ds
    mov ax, [op_zsrc]
    mov es, bp
    mov ds, ax
    xor si, si
    xor di, di
    cld
    rep movsb
    pop ds
    push ds
    pop es
    pop si
    push si
    mov ax, [si+OP_R_LEN]
.step:
    add ax, 511                 ; both pointers step by whole SECTORS, which is
    and ax, 0FE00h              ; the granularity a part is laid out on
    mov cl, 4
    shr ax, cl                  ; ...as paragraphs
    add [op_zsrc], ax
    mov ax, [si+OP_R_LEN]       ; ...and the landing pointer by the UNPACKED
    add ax, 511                 ; length, rounded the same way
    and ax, 0FE00h
    mov cl, 4
    shr ax, cl
    add bp, ax
    pop si
    pop cx
.next:
    add si, OP_ROW
    dec cx
    jmp .one
.done:
    pop bp
    clc
    ret
.refuse:
    pop si
    pop cx
    pop bp
    mov si, op_s_zbad
    jmp op_say
%endif                              ; OP_HAS_COMP
%if OP_HAS_XMS
; GATED (SPEC.md 20.12.9): op_xload and op_lin - only a table with an OP_XMS row can reach them

; -----------------------------------------------------------------------------
; op_xload - the OP_XMS span, in ONE block above 1MB (SPEC.md 20.12.4)
; out: CF=0 and [op_xlin] = the block's 32-bit linear base; CF=1 = it could
;      not be had, and nothing above 1MB is left claimed
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
;
; ONE BLOCK FOR THE WHOLE SPAN, and each part's base is an offset inside it.
; That is the SDK's own advice - "the table is small, so take one big block
; and subdivide it yourself rather than many" - and it is the carve's argument
; one address space along: os88pkg.py lays the OP_XMS parts contiguously after
; the conventional ones, so contiguous in the file is contiguous up there.
;
; THE LOADER CANNOT READ A FILE INTO EXTENDED MEMORY, so the bytes go through
; a TRANSIENT conventional claim: read a chunk, copy it up, and give the claim
; back. A machine that cannot spare the staging buffer cannot load the part
; either - the fallback claim would be the whole part, which is bigger - so
; the XMS path never SAVES a machine that is already full at load time. It
; saves the machine afterwards, which is the point.
;
; THE BLOCK IS STAMPED WITH THIS INSTANCE and force-freed at close, because
; the loader brackets the entry call with the instance's stamp (SPEC.md
; 41.5.1) - so a package claiming from its entry proc is attributed to itself,
; and ld_unreserve sweeps it if the entry then refuses. Nothing here is owed
; to teardown.
; -----------------------------------------------------------------------------
op_xload:
    mov word [op_xlin], 0
    mov word [op_xlin+2], 0
    cmp word [op_xsecs], 0
    je .none

    mov ax, [op_xmskb]          ; --- the block, in one ask ------------------
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl                  ; DX:AX = KB * 1024, the high half being KB>>6
    call OSAPI_XMEM_ALLOC       ; out DX:AX = the 32-bit linear base
    jc .no
    mov [op_xlin], ax
    mov [op_xlin+2], dx
    mov [op_xcur], ax           ; the climb starts at the base and walks; the
    mov [op_xcur+2], dx         ; base itself never moves

    ; --- where the read starts, and the head slack it lands past ------------
    ; The same arithmetic op_claim does, for the same reason: a read may only
    ; BEGIN on a cluster boundary and the span begins on a 512-byte one.
    mov ax, [op_xoff]
    mov dx, ax
    mov cl, 9
    shl ax, cl
    mov cl, 7
    shr dx, cl                  ; DX:AX = the span's byte offset, in 32 bits
    mov bx, [op_clb]
    dec bx
    and bx, ax
    mov [op_xslack], bx
    sub ax, bx
    sbb dx, 0
    mov [op_xrd], ax
    mov [op_xrd+2], dx

    mov ax, [op_xsecs]          ; the span's bytes...
    mov cl, 9
    shl ax, cl
    mov [op_xleft], ax          ; ...still to copy UP
    add ax, bx                  ; ...and to READ, with the slack in front
    jc .free
    mov dx, [op_clb]
    dec dx
    add ax, dx
    jc .free
    not dx
    and ax, dx                  ; rounded to whole clusters, the slot's other
    mov [op_xneed], ax          ; precondition

    mov ax, OP_CHUNK            ; --- the staging claim ----------------------
    cmp ax, [op_xneed]
    jbe .stage
    mov ax, [op_xneed]          ; a small span needs no 32KB buffer, and
.stage:                         ; op_xneed is already a cluster multiple
    mov [op_xcap], ax
    add ax, 1023
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM        ; out DX = the base segment
    jc .free
    mov [op_xstg], dx

.chunk:
    cmp word [op_xleft], 0
    je .fin
    mov cx, [op_xcap]           ; read one bufferful, cluster-aligned
    mov es, [op_xstg]
    xor bx, bx
    mov si, op_name
    mov ax, [op_xrd]
    mov dx, [op_xrd+2]
    call OSAPI_FILE_READ_AT
    jc .drop
    mov bx, ax
    or bx, dx
    jz .drop                    ; past the end with [op_xleft] still owed: a
                                ; TRUNCATED file, and SPEC.md 20.12.4 says the
                                ; standard refuses it - the same answer op_read
                                ; gives a short carve. It used to be `.fin`,
                                ; a success with a half-filled block

    mov si, [op_xslack]         ; ...and up it goes. The slack is only in
    mov word [op_xslack], 0     ; front of the FIRST chunk
    mov cx, [op_xcap]
    sub cx, si                  ; CX = the real bytes in this bufferful
    cmp cx, [op_xleft]
    jbe .even
    mov cx, [op_xleft]
.even:
    sub [op_xleft], cx
    inc cx                      ; OSAPI_XMEM_COPY wants an EVEN count and a
    and cl, 0xFE                ; span's last chunk need not be one. The
                                ; buffer holds whole CLUSTERS, so the extra
                                ; byte is inside it, and the block is rounded
                                ; to KB, so it lands inside that too
    mov es, [op_xstg]
    mov ax, [op_xcur]
    mov dx, [op_xcur+2]
    push cx
    xor di, di                  ; 0 = conventional -> extended
    call OSAPI_XMEM_COPY
    pop cx
    jc .drop

    add [op_xcur], cx           ; the linear cursor walks with it...
    adc word [op_xcur+2], 0
    mov ax, [op_xcap]           ; ...and the file cursor by a whole bufferful,
    add [op_xrd], ax            ; because that is what a cluster-aligned
    adc word [op_xrd+2], 0      ; resume point has to be
    jmp .chunk

.fin:
    mov dx, [op_xstg]           ; the buffer was always transient: it exists
    call OSAPI_MEM_FREE         ; for the length of one span's climb
.none:
    clc
    ret
.drop:
    mov dx, [op_xstg]
    call OSAPI_MEM_FREE
.free:
    mov ax, [op_xlin]           ; give the block back: a package that refuses
    mov dx, [op_xlin+2]         ; must leave nothing above 1MB either, and
    or ax, dx                   ; ld_unreserve would only sweep it on an
    jz .no                      ; ABORT - this path may be a soft failure
    mov ax, [op_xlin]
    mov dx, [op_xlin+2]
    call OSAPI_XMEM_FREE
.no:
    mov word [op_xlin], 0
    mov word [op_xlin+2], 0
    stc
    ret

; -----------------------------------------------------------------------------
; op_lin - the 32-bit LINEAR base of part AL, when it went above 1MB
; in:  AL = the part index
; out: DX:AX = its linear base, 0:0 = it is not up there (which is every
;      tier-0 machine, and then op_seg answers instead)
; clobbers: AX, DX (the outputs), flags
; -----------------------------------------------------------------------------
op_lin:
    push bx
    push cx
    push si
    xor ah, ah
    cmp al, [op_t_n]
    jae .none
    cmp byte [op_xmson], 0
    je .none
    call op_row
    test byte [si+OP_R_FLAG], OP_XMS
    jz .none
    mov bx, [si+OP_R_OFF]
    or bx, bx
    jz .none
    sub bx, [op_xoff]           ; sectors into the span...
    mov ax, bx
    mov cl, 9
    xor dx, dx
    shl ax, cl
    mov cl, 7
    shr bx, cl
    mov dx, bx                  ; ...as a 32-bit byte offset
    add ax, [op_xlin]
    adc dx, [op_xlin+2]
    jmp short .out
.none:
    xor ax, ax
    xor dx, dx
.out:
    pop si
    pop cx
    pop bx
    ret
%endif                              ; OP_HAS_XMS

; -----------------------------------------------------------------------------
; op_seg - the base segment of part AL
; in:  AL = the part index
; out: AX = its segment, 0 = it is not there
; clobbers: AX (the output), flags
; -----------------------------------------------------------------------------
op_seg:
    push bx
    push cx
    push dx
    push si
    xor ah, ah
    cmp al, [op_t_n]
    jae .none
    call op_row                 ; SI -> the row, AX preserved
%if OP_HAS_LAZY
    test byte [si+OP_R_FLAG], OP_LAZY
    jz .notlazy
    xor ax, ax                  ; A LAZY ROW BANKS ITS SEGMENT IN ITS OWN zkb
    test byte [si+OP_R_FLAG], OP_FETCHED    ; WORD - but only once OP_FETCHED
    jz .out                                 ; says so (SPEC.md 20.12.7.4).
    mov ax, [si+OP_R_ZKB]                   ; Before that the word is the
    jmp .out                                ; PACKED LENGTH on a compressed row
                                ; and zero on a plain one, so `non-zero` is not
                                ; the question. The
                                ; jump is NEAR: 20.12.7's walk went in below
                                ; and .out is past it. The
                                ; table is in the image, which is this
                                ; instance's own RAM, so it is per-instance
                                ; state that costs no bss and needs no second
                                ; array indexed in step with the rows; zkb is
                                ; free because os88pkg.py refuses OP_LAZY on a
                                ; scratch part, which is the rule that makes
                                ; the word available (SPEC.md 20.12.4)
.notlazy:
%endif
%if OP_HAS_XMS
    test byte [si+OP_R_FLAG], OP_XMS
    jz .conv
    cmp byte [op_xmson], 0
    jne .none                   ; it went ABOVE 1MB: op_lin is what answers,
                                ; and a segment for it does not exist
.conv:
%endif
    mov bx, [si+OP_R_OFF]
    or bx, bx
%if OP_HAS_ZERO
    jz .scratch                 ; no file bytes: it is in the scratch area
%else
    jz .none                    ; no file bytes and no scratch part declared:
                                ; os88pkg.py gives every filed row an offset,
                                ; so this cannot be a row of ours
%endif

%if OP_HAS_COMP
    cmp byte [op_zn], 0
    jne .zwalk                  ; a compressed row is in the table, so the
                                ; claim is NOT the disk's layout any more
%endif

    mov ax, bx                  ; --- a filed part: inside the carved run ----
    sub ax, [op_first]          ; sectors past the run's start...
    mov cl, 5
    shl ax, cl                  ; ...and a sector is 32 paragraphs
    mov dx, [op_slack]
    mov cl, 4
    shr dx, cl                  ; the head slack in paragraphs - exact, because
    add ax, dx                  ; it is a multiple of 512 (SPEC.md 20.12.2)
    add ax, [op_base]
    jmp .out

%if OP_HAS_COMP
    ; --- ...or the UNPACKED layout, which op_unpack laid down (20.12.7) -----
    ; The arithmetic above reads the DISK's layout - part i sits where its own
    ; sectors sit - and that is exactly what compressing one breaks. So this
    ; walks the rows instead and accumulates roundup512(len), which is where
    ; op_unpack put each of them. **It answers the same address as the
    ; arithmetic above when nothing is compressed** - the packer lays part i at
    ; roundup512(len) past part i-1, so the sum IS (off - first) * 512 - which
    ; is why the fast path can stay and why a package with no compressed part
    ; is untouched.
.zwalk:
    push ax                     ; AL = the index we are placing
    mov dx, [op_slack]
    mov cl, 4
    shr dx, cl                  ; DX = paragraphs from the base, running
    xor bx, bx                  ; BX = the row we are stepping over
.zw:
    cmp bl, al
    jae .zfound
    push ax
    mov ax, bx
    call op_row                 ; SI -> row BX
    pop ax
    cmp word [si+OP_R_OFF], 0
    je .zwnext                  ; scratch: not in the run
    test byte [si+OP_R_FLAG], OP_LAZY
    jnz .zwnext                 ; not in the run either
    test byte [si+OP_R_FLAG], OP_XMS
    jz .zwadd
    cmp byte [op_xmson], 0
    jne .zwnext                 ; it went above 1MB
.zwadd:
    push ax
    mov ax, [si+OP_R_LEN]       ; the UNPACKED length, rounded to a sector
    add ax, 511
    and ax, 0FE00h
    mov cl, 4
    shr ax, cl
    add dx, ax
    pop ax
.zwnext:
    inc bx
    jmp short .zw
.zfound:
    pop ax
    mov ax, dx
    add ax, [op_base]
    jmp short .out
%endif

%if OP_HAS_ZERO
.scratch:                       ; --- scratch: after the run, in TABLE order --
    ; It is walked rather than banked because the answer depends on which
    ; OPTIONAL parts were granted, and that is one byte the caller can also
    ; read (op_optok) rather than a second table nobody maintains.
%if OP_HAS_OPT
    test byte [si+OP_R_FLAG], OP_OPT
    jz .place
    cmp byte [op_optok], 0
    je .none                    ; refused, and the package carries on without
                                ; it - which is what OP_OPT is (SPEC.md 47)
%endif
.place:
    mov dx, [op_runkb]          ; KB from the claim's base to the scratch area
    push ax
    xor bx, bx                  ; BX = the index we are placing
.walk:
    cmp bl, al
    jae .found
    push ax
    mov ax, bx
    call op_row
    pop ax
    cmp word [si+OP_R_OFF], 0
    jne .wnext                  ; filed: it is not in this area
%if OP_HAS_OPT
    test byte [si+OP_R_FLAG], OP_OPT
    jz .wadd
    cmp byte [op_optok], 0
    je .wnext                   ; refused: it takes up no room
.wadd:
%endif
    add dx, [si+OP_R_ZKB]
.wnext:
    inc bx
    jmp short .walk
.found:
    pop ax
    mov ax, dx
    mov cl, 6
    shl ax, cl                  ; KB -> paragraphs (1KB = 64 of them)
    add ax, [op_base]
    jmp short .out
%endif                          ; OP_HAS_ZERO - the scratch area
.none:
    xor ax, ax
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; op_say - put SI's string up and refuse (module-internal)
; in:  SI = a NUL-terminated string in our segment; out: CF=1
; -----------------------------------------------------------------------------
op_say:
    push ds
    pop es                      ; OSAPI_TOAST wants ES:SI (SPEC.md 59)
    xor cx, cx
    call OSAPI_TOAST
    stc
    ret

; -----------------------------------------------------------------------------
; op_clbget - this volume's bytes per cluster, asked AT MOST ONCE
; out: CF=0 and [op_clb] filled; CF=1 = the volume would not answer
; clobbers: AX, flags
;
; OSAPI_FILE_DFREE is not free - it counts every entry in the resident FAT
; snapshot, about 105 ms on a 20MB hard disk (SPEC.md 18.4.5) - so the answer
; is cached and asked for only when something is actually going to be read.
; op_load skips it entirely for a table of nothing but scratch, and op_fetch
; is why it cannot simply live there: a package whose parts are ALL lazy has
; no carve, so nothing has asked the volume anything by the time the first
; fetch needs the alignment.
; -----------------------------------------------------------------------------
op_clbget:
    mov ax, [op_clb]
    or ax, ax
    jnz .have
    push bx
    push cx
    call OSAPI_FILE_DFREE       ; out BX = SECTORS per cluster
    jc .no
    or bx, bx
    jz .no
    mov ax, bx
    mov cl, 9
    shl ax, cl                  ; ...and this is BYTES per cluster
    mov [op_clb], ax
    pop cx
    pop bx
.have:
    clc
    ret
.no:
    pop cx
    pop bx
    stc
    ret
%if OP_HAS_LAZY
; GATED (SPEC.md 20.12.9): op_lazykb, op_lazyok, op_fetch and op_drop - the whole lazy surface, and the last three are public entry points a table with no OP_LAZY row has nothing to call

; -----------------------------------------------------------------------------
; op_lazykb - what the row at SI would cost to fetch, in KB (module-internal)
; in:  SI -> the row, [op_clb] filled
; out: AX = KB; CF=1 = it cannot be expressed in a word
; clobbers: AX, flags
;
; The READ's share, slack included, exactly as op_claim computes the carve's:
; the read starts at the cluster at or below the part and those bytes land in
; front of it (SPEC.md 20.12.2).
; -----------------------------------------------------------------------------
op_lazykb:
    push bx
    push cx
    push dx
    mov ax, [si+OP_R_OFF]       ; the part's byte offset, in 32 bits - though
    mov cl, 9                   ; only the low half matters below, because the
    shl ax, cl                  ; slack is an AND with a cluster mask
    mov bx, [op_clb]
    dec bx
    and bx, ax                  ; BX = the head slack: a cluster size is a
                                ; power of two, so this is an AND
    mov ax, [si+OP_R_LEN]
    add ax, bx
    jc .no
    mov dx, [op_clb]
    dec dx
    add ax, dx
    jc .no
    not dx
    and ax, dx                  ; ...rounded to whole clusters, the slot's
    add ax, 1023                ; other precondition
    jc .no
    mov cl, 10
    shr ax, cl
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; op_lazyok - could part AL be had RIGHT NOW? (SPEC.md 20.12.4)
; in:  AL = the index
; out: CF=0 it would fit; CF=1 it would not, or it is not a lazy part
; clobbers: AX, flags
;
; A QUESTION, AND A QUESTION COSTS NOTHING. It exists because SPEC.md 47 says
; grey a FACT and never a guess: a package with a lazy part behind a menu item
; must be able to grey that item BEFORE the user picks it, and the only honest
; alternative is to let them pick it and answer with a toast. It asks
; OSAPI_MEM_AVAIL rather than trying the claim, for op_load's reason - a claim
; that is going to be given back is a claim somebody else could have had, and
; mem_claim sheds every purgeable cache before it refuses one (SPEC.md 50.6.2).
;
; It is a fact about THIS INSTANT and nothing more: between this and op_fetch
; the heap can move, so op_fetch refuses for itself and does not trust it.
; -----------------------------------------------------------------------------
op_lazyok:
    push bx
    push cx
    push dx
    push si
    xor ah, ah
    cmp al, [op_t_n]
    jae .no
    call op_row
    test byte [si+OP_R_FLAG], OP_LAZY
    jz .no
%if OP_HAS_COMP
    cmp word [si+OP_R_ZKB], OP_SPENT    ; dropped and unfetchable - the packed
    je .no                              ; length is gone (SPEC.md 20.12.7.4)
%endif
    test byte [si+OP_R_FLAG], OP_FETCHED
    jnz .yes                    ; already here, and having it costs nothing
    call op_clbget
    jc .no
    call op_lazykb
    jc .no
    push ax                     ; ...BANKED, because OSAPI_MEM_AVAIL answers
    call OSAPI_MEM_AVAIL        ; in TWO registers - AX the largest free run
    pop bx                      ; and BX the total free - so a wanted figure
    cmp ax, bx                  ; left in BX is compared against a number that
    jb .no                      ; is >= it by construction, and this refuses
                                ; every time the heap holds more than one
                                ; free block
.yes:
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; op_fetch - claim and read part AL, now (SPEC.md 20.12.4)
; in:  AL = the index of an OP_LAZY part
; out: CF=0 and op_seg answers for it; CF=1 = it could not be had, and a toast
;      has said why
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
;
; IDEMPOTENT: a part that is already here answers CF=0 having done nothing, so
; a caller that cannot remember whether it fetched need not.
;
; ONE CLAIM PER LAZY PART, and that is what lazy costs. The carve is one claim
; for every eager part because MEM_OWNER_MAX is 8 per owner word (SPEC.md 50),
; and a lazy part cannot be in it - not reading it is the whole point - so
; each one that is HERE spends a slot of that eight. op_drop gives the slot
; back, which is what makes lazy a saving rather than a postponement.
;
; UI TASK ONLY, like op_load and for the same reason: it reads a disk and
; claims memory, and SPEC.md 20.6 rule 7 forbids a worker both. A window
; callback qualifies; a worker does not.
; -----------------------------------------------------------------------------
op_fetch:
    push ds
    pop es                      ; ES is ours: every slot below that takes
                                ; ES:SI or ES:BX means OUR buffer
    xor ah, ah
    cmp al, [op_t_n]
    jae .bad
    call op_row                 ; SI -> the row, AX preserved
    test byte [si+OP_R_FLAG], OP_LAZY
    jz .bad                     ; not a lazy part: op_seg already answers for
                                ; it, so asking is a bug and not a refusal
%if OP_HAS_COMP
    cmp word [si+OP_R_ZKB], OP_SPENT    ; dropped: the word that held its
    je .spent                           ; packed length is where the segment
%endif                                  ; went (SPEC.md 20.12.7.4)
    test byte [si+OP_R_FLAG], OP_FETCHED
    jz .get
    clc                         ; already here
    ret
.get:
    call op_clbget
    jc .nodisk
    call op_lazykb              ; AX = the KB it wants, slack included
    jc .toobig
%if OP_HAS_COMP
    ; A COMPRESSED ROW HOLDS slack + R + packed, and R is `len - packed`
    ; rounded UP to a paragraph - so up to fifteen bytes more than the `len`
    ; op_lazykb sized from. One KB covers it and is transient: the claim lives
    ; for one expansion.
    test byte [si+OP_R_FLAG], OP_COMP
    jz .plainkb
    inc ax
.plainkb:
%endif
    mov bx, ax
    push bx
    call OSAPI_MEM_AVAIL        ; ASK, then claim - op_load's rule, for
    pop bx                      ; op_lazyok's reason
    cmp ax, bx
    jb .nomem
    mov ax, bx
    call OSAPI_MEM_CLAIM        ; out DX = the base segment
    jc .nomem
    push dx                     ; ...banked across everything below

    mov ax, [si+OP_R_OFF]       ; --- where the read starts, and its slack ---
    mov dx, ax
    mov cl, 9
    shl ax, cl
    mov cl, 7
    shr dx, cl                  ; DX:AX = the part's byte offset, 32 bits
    mov bx, [op_clb]
    dec bx
    and bx, ax
    sub ax, bx
    sbb dx, 0
    mov [op_off], ax
    mov [op_off+2], dx
    mov ax, [si+OP_R_LEN]
%if OP_HAS_COMP
    test byte [si+OP_R_FLAG], OP_COMP
    jz .plainwant
    mov ax, [si+OP_R_ZKB]       ; A COMPRESSED ROW READS ITS PACKED LENGTH and
.plainwant:                     ; expands to OP_R_LEN below (SPEC.md 20.12.7.4)
%endif
    add ax, bx                  ; op_lazykb has already proved that neither of
    mov [op_want], ax           ; these carries, and refused if they did - and
                                ; THIS, before the rounding, is what op_read
                                ; must see arrive
    mov dx, [op_clb]
    dec dx
    add ax, dx
    not dx
    and ax, dx
    mov [op_cap], ax

    ; THE CARVE'S READ WORDS ARE BORROWED AND PUT BACK. op_read reads
    ; [op_off] and [op_cap] into [op_rdseg], there is exactly one read in
    ; flight at a time, and a lazy fetch is that same read pointed somewhere
    ; else - which is cheaper than a second copy of the chunk loop and
    ; honester than three more words of bss that are live for one call.
    ; op_seg reads [op_base] for every EAGER filed part, so it goes back
    ; whatever happens - and [op_rdseg] with it, because on a package with a
    ; compressed part that is the base plus R and NOT the base (SPEC.md
    ; 20.12.7).
    ;
    ; **AND A COMPRESSED LAZY ROW IS READ R PARAGRAPHS UP ITS OWN CLAIM**, for
    ; that same reason one part at a time (SPEC.md 20.12.7.4): the stream has
    ; to sit ABOVE where it expands to, or the decoder's write pointer
    ; overtakes its read pointer. R is roundup512(len) - roundup512(packed),
    ; both multiples of 512 and so of 16, and `in_place_margin` is ZERO for a
    ; stream with a tail (20.13.7) - which os88pkg.py asserts on every part it
    ; writes - so R is exactly enough and no margin is reserved.
    pop dx
    push si
    push bx
    push dx
    push word [op_base]
    push word [op_rdseg]
    mov [op_base], dx
    mov [op_rdseg], dx
%if OP_HAS_COMP
    test byte [si+OP_R_FLAG], OP_COMP
    jz .plainrd
    call op_zrpara              ; AX = R, in paragraphs
    add [op_rdseg], ax
.plainrd:
%endif
    call op_read
    pop dx
    mov [op_rdseg], dx
    pop dx
    mov [op_base], dx
    pop dx                      ; the claim
    pop bx                      ; the slack
    pop si                      ; the row
    jc .ioerr

    mov ax, bx                  ; ...and the part sits past the slack, which
    mov cl, 4                   ; is a multiple of 512 and therefore of 16 -
    shr ax, cl                  ; op_seg's arithmetic, one part at a time
    add ax, dx
%if OP_HAS_COMP
    test byte [si+OP_R_FLAG], OP_COMP
    jz .plainbank
    ; --- EXPAND IT, DOWN ITS OWN CLAIM (SPEC.md 20.12.7.4) -----------------
    ; op_unpack's walk with one row in it: the stream is R paragraphs up and
    ; the output starts at the base, so the write pointer chases the read
    ; pointer and never catches it.
    push dx                     ; THE CLAIM, for .zbad - the base is not it
    push si                     ; the row
    push ax                     ; the base
    call op_zrpara              ; AX = R in paragraphs; SI preserved
    pop dx                      ; DX = the base
    push dx
    add ax, dx                  ; ...and AX = where the stream was read to
    push ax
    mov cx, [si+OP_R_ZKB]       ; CX = the PACKED length, while it is still
    mov ax, [si+OP_R_LEN]       ; there - one more store and it is a segment
    mov dx, ax                  ; BX:DX = the 32-bit expected output, and a
    xor bx, bx                  ; part is 16 bits by os88pkg.py
    mov al, [op_table + OP_T_FMT]   ; THE FORMAT, READ BEFORE DS MOVES: one
    xor ah, ah                      ; instruction later op_table is
    pop di                          ; unreachable, which op_unpack's own
    pop es                          ; header records as the defect that
    push es                         ; refused every stream
    push ds
    mov ds, di                  ; DS:SI the stream, ES:DI the output, DI = 0
    xor si, si                  ; being the contract (SPEC.md 20.13.3)
    xor di, di
    call OSAPI_DECOMP
    pop ds                      ; ...and none of these four touches CF
    pop ax                      ; the base
    pop si                      ; the row
    pop dx                      ; the claim
    jc .zbad
.plainbank:
%endif
    or byte [si+OP_R_FLAG], OP_FETCHED  ; ...and the flag is what SAYS so
    mov [si+OP_R_ZKB], ax       ; THE ROW IS WHERE A FETCHED PART LIVES
    clc
    ret

.ioerr:
    call OSAPI_MEM_FREE         ; DX is still the claim, and a fetch that
    mov si, op_s_read           ; failed must leave nothing behind
    jmp op_say
%if OP_HAS_COMP
.zbad:
    call OSAPI_MEM_FREE         ; ...and the same for a stream that would not
    mov si, op_s_zbad           ; expand, which leaves the row at 0 and so
    jmp op_say                  ; re-fetchable: nothing was banked in it
.spent:
    mov si, op_s_spent          ; dropped and gone: a REFUSAL and not a silent
    jmp op_say                  ; read of nothing (SPEC.md 20.12.7.4)
%endif
.bad:
    mov si, op_s_lazy
    jmp op_say
.nodisk:
    mov si, op_s_disk
    jmp op_say
.fail:                          ; op_unpack has already put its own message up
    stc
    ret
.toobig:
    mov si, op_s_big
    jmp op_say
.nomem:
    mov si, op_s_mem
    jmp op_say

; -----------------------------------------------------------------------------
; op_drop - give part AL back (SPEC.md 20.12.4)
; in:  AL = the index of an OP_LAZY part
; out: nothing; op_seg answers 0 for it again. Every register preserved.
;
; THIS IS WHAT MAKES LAZY A SAVING. A part that is only ever fetched is a cost
; deferred, not avoided - the claim slot and the KB are both still spent for
; the life of the instance. Dropping is one call and no disk, and a later
; op_fetch reads it again.
;
; Dropping something that is not here, or is not lazy, is a no-op rather than
; an error: a caller unwinding does not have to know how far it got.
; -----------------------------------------------------------------------------
op_drop:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    xor ah, ah
    cmp al, [op_t_n]
    jae .out
    call op_row
    test byte [si+OP_R_FLAG], OP_LAZY
    jz .out
    mov dx, [si+OP_R_ZKB]
    or dx, dx
    jz .out
    and byte [si+OP_R_FLAG], ~OP_FETCHED & 0xFF  ; not here any more
%if OP_HAS_COMP
    ; **A COMPRESSED ROW IS SPENT, NOT EMPTY** (SPEC.md 20.12.7.4): the packed
    ; length it would need to be read again is the very word the segment went
    ; into, so 0 would have op_fetch read a stream of NOTHING and answer CF=0
    ; over a claim of rubble. AX is free - the line below reloads it.
    xor ax, ax
    test byte [si+OP_R_FLAG], OP_COMP
    jz .plaindrop
    mov ax, OP_SPENT
.plaindrop:
    mov [si+OP_R_ZKB], ax       ; CLEARED FIRST: op_seg must never answer a
%else
    mov word [si+OP_R_ZKB], 0   ; CLEARED FIRST: op_seg must never answer a
%endif
                                ; segment that is on its way back to the heap
    mov ax, [si+OP_R_OFF]       ; ...and the word op_fetch banked is the PART's
    mov cl, 9                   ; base, which sits past the head slack (op_seg
    shl ax, cl                  ; answers it as such). OSAPI_MEM_FREE matches
    mov bx, [op_clb]            ; the CLAIM's base exactly (mem_find_own,
    dec bx                      ; SPEC.md 50.3) and op_drop never reads its CF,
    and ax, bx                  ; so a free of the biased word was a silent
    mov cl, 4                   ; leak on every volume whose cluster is over
    shr ax, cl                  ; 512 bytes. This is op_fetch's slack
    sub dx, ax                  ; arithmetic run backwards; [op_clb] is live
    call OSAPI_MEM_FREE         ; because op_fetch ran op_clbget before it claimed
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; OP_HAS_LAZY

; -----------------------------------------------------------------------------
; op_row - the table row for part AL (module-internal)
; in:  AL = the index; out: SI -> its row. AX preserved.
; -----------------------------------------------------------------------------
op_row:
    push ax
    xor ah, ah
    mov si, ax
    add si, si
    add si, si
    add si, si                  ; index * 8 = OP_ROW
    add si, op_table + OP_T_ROWS
    pop ax
    ret

%if OP_HAS_COMP && OP_HAS_LAZY
; GATED (SPEC.md 20.12.9): only a table with an OP_COMP|OP_LAZY row reaches it

; -----------------------------------------------------------------------------
; op_zrpara - R for a compressed lazy row, in paragraphs (SPEC.md 20.12.7.4)
; in:  SI -> the row
; out: AX = roundup16(len - packed), as paragraphs
; clobbers: AX (the output), flags. SI preserved.
;
; The stream is read R paragraphs up its own claim and expands DOWN to the
; base, so the decoder's write pointer chases its read pointer and the gap it
; starts with is R.
;
; **IT IS `len - packed` AND NOT op_unpack's DIFFERENCE OF TWO ROUNDUPS**,
; which is the shape this was first written in and is wrong here.
; `in_place_margin`'s model - the one os88pkg.py asserts against every stream
; it writes - is the source sitting at the TOP of a region of exactly `len`
; bytes, which puts it `len - packed` above the output; zero margin means the
; writer never overtakes a reader THAT FAR AHEAD, and says nothing about a
; reader any closer. op_unpack rounds both terms because its two pointers walk
; row by row in whole SECTORS, and subtracting two roundups is 210 bytes SHORT
; on the DOS core - so the expansion ran off the end of its own stream and
; refused with `A part will not unpack`.
;
; Rounded UP to a paragraph, because op_read lands on a segment boundary.
; -----------------------------------------------------------------------------
op_zrpara:
    push cx
    mov ax, [si+OP_R_LEN]
    sub ax, [si+OP_R_ZKB]       ; os88pkg.py refuses a part that does not get
    add ax, 15                  ; smaller, so this cannot go negative
    mov cl, 4
    shr ax, cl
    pop cx
    ret
%endif
%if OP_HAS_ZERO
; GATED (SPEC.md 20.12.9): op_scrub - it zeroes the scratch parts, and there are none

; -----------------------------------------------------------------------------
; op_scrub - zero every scratch part that was granted
; in:  the claim made; out: nothing
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
;
; NO DISK IS READ HERE and that is the whole of what a scratch part is: a
; working set DECLARED rather than claimed from the entry proc and refused
; afterwards, which is what every workaround in the tree does today
; (docs/plans/completed/O88-MULTISEG-PLAN.md 2.3).
; -----------------------------------------------------------------------------
op_scrub:
    xor bx, bx
.one:
    cmp bl, [op_t_n]
    jae .done
    mov ax, bx
    call op_row
    cmp word [si+OP_R_OFF], 0
    jne .next                   ; filed: op_read put its bytes there
    mov cx, [si+OP_R_ZKB]
    jcxz .next
    mov ax, bx
    call op_seg                 ; AX = where it landed, 0 = it was refused
    or ax, ax
    jz .next
    push bx
    mov es, ax
    mov dx, cx                  ; DX = KB still to zero
.fill:
    or dx, dx
    jz .filled
    mov cx, 32                  ; 32KB A PASS, and the bound is ES:DI: a
    cmp dx, cx                  ; rep stosw cannot cross 64KB, and 600KB of
    jae .take                   ; scratch is a perfectly ordinary ask
    mov cx, dx
.take:
    sub dx, cx
    push dx
    mov ax, cx                  ; CX KB -> words, THROUGH AX: the count and
    mov cl, 9                   ; the operand cannot be the same register, and
    shl ax, cl                  ; `mov cl, 9` would overwrite the low half of
    mov cx, ax                  ; the very figure being converted
    xor di, di
    xor ax, ax
    cld
    rep stosw
    pop dx
    mov ax, es
    add ax, 0x800               ; ...and on by 32KB of paragraphs
    mov es, ax
    jmp short .fill
.filled:
    pop bx
.next:
    inc bx
    jmp short .one
.done:
    push ds
    pop es
    ret
%endif                              ; OP_HAS_ZERO

; --- refusals, in the package's own words (SPEC.md 47, 59) -------------------
; TOAST_MAX is 24 characters and a cut message reads like a bug in the thing
; it is reporting on, so these are short on purpose.
op_s_big:   db 'Parts do not fit', 0
op_s_mem:   db 'Not enough memory', 0
op_s_disk:  db 'No disk to read from', 0
op_s_read:  db 'Cannot read my parts', 0
%if OP_HAS_LAZY
op_s_lazy:  db 'Not a fetchable part', 0
%endif
%if OP_HAS_COMP
op_s_zbad:  db 'A part will not unpack', 0
%endif
%if OP_HAS_COMP && OP_HAS_LAZY
op_s_spent: db 'That part was dropped', 0   ; ...and cannot come back: a
%endif                                      ; compressed lazy row's packed
                                            ; length is where the segment went
                                            ; (SPEC.md 20.12.7.4)
