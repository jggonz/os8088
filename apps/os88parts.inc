; =============================================================================
; apps/os88parts.inc - THE PARTS STANDARD (SPEC.md 20.12)
;
; A package that needs more than its own 64KB segment, or that wants to carry
; its data rather than trail a sidecar file, embeds this. It is ordinary
; package code, assembled into you, and it owes the kernel nothing: every call
; it makes is a slot that was already published.
;
; THE KERNEL LEARNS THREE THINGS AND NOTHING ELSE (SPEC.md 20.12.1): that a
; *.O88 over 64KB is still a package, that flags bit 2 means the file is
; longer than the image on purpose, and it tells your ENTRY PROC the name of
; the file it came out of. There is no .o88 v6, no kernel-parsed block and no
; API slot. docs/plans/completed/O88-MULTISEG-PLAN.md 1 is why - the kernel-side version of
; this was built to five waves and thrown away at 2,560 resident bytes.
;
; --- what you write ----------------------------------------------------------
;
;     OS88_HEADER 'MYAPP', my_entry, OS88_F_PARTS
;     %include "os88parts.inc"
;
;     OS88_PARTS_BEGIN 4
;       OS88_PART OP_SEG                 ; 0  code you far-call
;       OS88_PART OP_ASSET               ; 1  data you read
;       OS88_PART OP_ASSET, OP_ZERO, 8   ; 2  8KB of scratch, NO DISK AT ALL
;       OS88_PART OP_ASSET, OP_ZERO | OP_OPT, 600
;     OS88_PARTS_END                     ; 3  ...and 600KB it can do without
;
;     my_entry:
;         call op_load                   ; SI is the kernel's; call this FIRST
;         jc .refused                    ; a toast has already said why
;         mov al, 1
;         call op_seg                    ; AX = part 1's segment
;
;     ...and the bss, which is an equ chain in this SDK: OUR words come first,
;     yours follow them, and OS88_BSS is told the sum.
;
;         my_thing  equ os88_image_end + OP_BSS
;         MY_BSS    equ 40
;         OS88_BSS OP_BSS + MY_BSS
;
; --- the three rules ---------------------------------------------------------
;
; 1. CALL op_load FROM THE ENTRY PROC, FIRST. SI arrives holding an offset
;    into the KERNEL's segment, at a 13-byte name buffer the loader reuses on
;    the next launch (SPEC.md 20.2). op_load copies it before anything else,
;    and nothing later can recover it.
;
; 2. SEGMENT 0 OWNS THE IDENTITY (SPEC.md 20.12.5, binding). Your entry proc,
;    every window, every callback and your worker task live in the PRIMARY
;    segment. A part may not call the kernel's ES-fenced slots -
;    OSAPI_MEM_CLAIM and its relatives, OSAPI_MEM_FREE, OSAPI_TASK_SPAWN,
;    OSAPI_FSX_ENTER - because ES is stamped from the caller's DS and the
;    kernel reads it as WHO IS ASKING. The primary claims and passes a
;    segment down.
;
; 3. UI TASK ONLY. op_load reads a disk and claims memory, and SPEC.md 20.6
;    rule 7 forbids a worker both. The entry proc and every window callback
;    qualify; your worker does not.
;
; --- what it costs -----------------------------------------------------------
;
; ONE claim for every part that shares the carve, because MEM_OWNER_MAX is 8
; claims per owner word (SPEC.md 50) and a package's own claims carry its
; segment. And one OSAPI_FILE_READ_AT per 32KB of it, not one per part - each
; int 13h is ~400 ms on the target machine (PERFORMANCE.md), so what is
; counted is calls.
;
; Everything the kernel-side loader would have done it does here instead, and
; it does it AFTER the kernel has already read your image - so the size
; question is answered with NO DISK READ AT ALL and a refusal is instant.
; =============================================================================

; OS88_F_PARTS lives in os88api.inc, beside the other header flags, because
; OS88_HEADER needs it and OS88_HEADER comes before this include.

; WHERE THE STANDARD'S BSS LIVES - see the chain at the foot of this file.
; An assembly package leaves this alone; apps/cc/crt0.asm defines it before
; including, because a C package's bss is a real section rather than the bytes
; past its image.
%ifndef OP_BSS_AT
    %define OP_BSS_AT os88_image_end
%endif

OP_SEG        equ 0             ; kind: code and/or data you far-call
OP_ASSET      equ 1             ; kind: data you reach through a segment

OP_XMS        equ 1             ; flags (SPEC.md 20.12.4) - later waves
OP_ZERO       equ 2
OP_OPT        equ 4
OP_LAZY       equ 8
OP_COMP       equ 16            ; ...and the part is COMPRESSED on the disk
                                ; (SPEC.md 20.12.7). `len` below stays the
                                ; UNPACKED byte count - it is what sizes the
                                ; claim, so a refusal still costs no disk -
                                ; and `zkb`, which the macro already forbids
                                ; on a file-backed row, carries the packed one
OP_MARGIN     equ 0             ; what a compressed run needs ABOVE its
                                ; unpacked bytes: NOTHING, since every stream
                                ; ends in a raw tail (SPEC.md 20.13.7) - the
                                ; name is kept because the arithmetic below
                                ; is written in terms of it and reads better
                                ; with the term in than with it silently gone

OP_ROW        equ 8             ; db kind, db flags, dw off (512-byte units),
OP_R_KIND     equ 0             ; dw len, dw zkb - off and len are os88pkg.py's
OP_R_FLAG     equ 1             ; and zkb is yours
OP_R_OFF      equ 2
OP_R_LEN      equ 4
OP_R_ZKB      equ 6             ; KB of zero-filled scratch, OP_ZERO rows only
                                ; - or the PACKED byte count on an OP_COMP row,
                                ; or a fetched OP_LAZY row's segment. One word,
                                ; three meanings, and no two of them can be on
                                ; the same row: the macro below refuses every
                                ; pairing (SPEC.md 20.12.7)
OP_T_ROWS     equ 10            ; the rows start past the magic and the count
OP_T_FMT      equ 9             ; ...and the byte before them is the FORMAT the
                                ; OP_COMP rows use: 0 = LZ4, 1 = LZB, and
                                ; meaningless with no such row. It is the
                                ; table's own reserved byte, which os88pkg.py
                                ; validated as zero and now writes - the
                                ; package's own scope, so it needs nothing of
                                ; the header's and nothing of the kernel's

OP_CHUNK      equ 32768         ; bytes a read moves at a time: a multiple of
                                ; EVERY legal cluster size (512 bytes to 32KB,
                                ; a power of two capped at 64 sectors by BPB
                                ; rule 4) and safely inside one segment's 64KB

; -----------------------------------------------------------------------------
; OS88_PARTS_BEGIN / OS88_PART kind [, flags] / OS88_PARTS_END
;
; The table is YOUR DATA, in your image, at a label you place. os88pkg.py finds
; it by the magic below rather than at a derived offset, so it does not care
; where you put it and there is no interaction with the icon or the
; association block. `off` and `len` are laid out as zeros and filled in by the
; packer: the assembler cannot know a separately assembled module's length.
;
; IT IS IN THE IMAGE AND THAT IS THE DESIGN (SPEC.md 20.12.3). The kernel has
; already read it by the time your entry proc runs, so sizing costs no disk;
; and it is not hostile input, because it did not come off a disk on its own.
; -----------------------------------------------------------------------------
%macro OS88_PARTS_BEGIN 1
    %assign OS88_PART_N 0
    %assign OS88_PART_WANT %1
    %if (%1) < 1
        %error "OS88_PARTS_BEGIN with no parts in it - drop the block and the flag"
    %endif
op_table:
    db 'O88PARTS'
op_t_n:
    db %1                       ; the count, and THREE things check it: this
                                ; macro's brackets count the rows, os88pkg.py
                                ; counts the --part arguments against the
                                ; filed ones, and op_size walks exactly this
                                ; many
    db 0                        ; reserved
%endmacro

%macro OS88_PART 1-3 0, 0
    %if ((%2) & OP_XMS) && ((%1) != OP_ASSET)
        %error "OP_XMS on a SEGMENT: code has to be addressable and extended memory is not"
    %endif
    %if ((%2) & OP_ZERO) && ((%3) < 1)
        %error "OP_ZERO with no KB: a scratch part of nothing is not a part"
    %endif
    %if !((%2) & OP_ZERO) && ((%3) != 0)
        %error "a KB on a FILE-BACKED part: the carve is contiguous, so a part that wants scratch beside it declares a second, OP_ZERO part"
    %endif
    %if ((%2) & OP_OPT) && !((%2) & OP_ZERO)
        %error "OP_OPT on a file-backed part: it sits inside the carved run, and dropping it would split the one read the carve exists to be"
    %endif
    %if ((%2) & OP_LAZY) && ((%2) & OP_ZERO)
        %error "OP_LAZY on a scratch part: a scratch part earns its declaration by being SIZED before a sector is read, and a lazy part is outside that sizing - so it is OSAPI_MEM_CLAIM with extra steps, and its zkb word is where a fetched part banks its segment"
    %endif
    %if ((%2) & OP_LAZY) && ((%2) & OP_OPT)
        %error "OP_LAZY with OP_OPT: lazy already means you may not get it - op_fetch refuses and op_lazyok answers in advance"
    %endif
    %if ((%2) & OP_LAZY) && ((%2) & OP_XMS)
        %error "OP_LAZY with OP_XMS: the parts above 1MB are ONE block claimed and filled at load, so a lazy member would force that block to be claimed anyway"
    %endif
    %if ((%2) & OP_COMP) && ((%2) & OP_ZERO)
        %error "OP_COMP on a scratch part: there are no file bytes to compress, and zkb is the KB it asks for"
    %endif
    %if ((%2) & OP_COMP) && ((%2) & OP_LAZY)
        %error "OP_COMP with OP_LAZY: a lazy row's zkb word banks the segment it was fetched into, and a compressed row's carries its packed length - one word, and a fetched part would overwrite the length it needs to be fetched again"
    %endif
    %if ((%2) & OP_COMP) && ((%2) & OP_XMS)
        %error "OP_COMP with OP_XMS: the span above 1MB is staged through a transient conventional buffer a chunk at a time, and a stream cannot be expanded a chunk at a time - the parts up there are stored plain"
    %endif
    db %1                       ; +0 kind
    db %2                       ; +1 flags
    dw 0                        ; +2 off, in 512-byte units - the packer's
    dw 0                        ; +4 len, in bytes           - the packer's
    dw %3                       ; +6 zkb, in KB              - yours
    %assign OS88_PART_N OS88_PART_N + 1
%endmacro

%macro OS88_PARTS_END 0
    %if OS88_PART_N < 1
        %error "OS88_PARTS_BEGIN with no parts between the brackets - drop the block and the flag"
    %endif
    %if OS88_PART_N != OS88_PART_WANT
        %error "OS88_PARTS_BEGIN was told a count that is not the number of parts between the brackets"
    %endif
    %if ($ - op_table) != OP_T_ROWS + OP_ROW * OS88_PART_N
        %error "the part table is 10 bytes plus 8 per part - write nothing else between the brackets"
    %endif
%endmacro

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

    ; --- is there a store to put the OP_XMS parts in? ----------------------
    ; ASKED FIRST, because the answer moves those parts between two totals:
    ; with a store they are a span of their own above 1MB, and without one
    ; they are ordinary filed parts inside the carved run. That is what makes
    ; OP_XMS a HINT and not a mode (SPEC.md 20.12.4), and on every 8088 - the
    ; machine this OS targets - there is no store at all (SPEC.md 41).
    ; OSAPI_XMEM_CAPS reads two bytes and is legal in any context.
    mov byte [op_xmson], 0
    call OSAPI_XMEM_CAPS        ; AX = KB the pool can still hand out
    or ax, ax
    jz .nostore
    mov byte [op_xmson], 1
.nostore:

    ; --- size, from the table alone: NO DISK IS READ HERE ------------------
    call op_size
    jc .toobig
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
    mov ax, [op_allkb]
    or ax, [op_reqkb]
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
.grant:

    ; --- what the machine will grant, and the OPTIONAL parts are the give --
    ; ALL OR NONE, and one retry (SPEC.md 20.12.4). Dropping optional parts
    ; one at a time is a search, and a search whose order the author cannot
    ; predict is worse than a rule they can: a package that wants finer
    ; control declares the parts it can live without and asks op_optok which
    ; way it went.
    mov byte [op_optok], 1
    call OSAPI_MEM_AVAIL        ; AX = the largest free run, in KB
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
    mov [op_kb], ax

    ; --- the cluster size, which is what the alignment turns on ------------
    ; ONLY WHEN THERE IS A RUN. A table of nothing but scratch reads no disk
    ; at all, and asking a volume its geometry when there is nothing to read
    ; off it would make a memory operation depend on a disk being there.
    cmp word [op_secs], 0
    jne .clb
    cmp word [op_xsecs], 0      ; ...and the SPAN reads the disk too, so a
    je .claim                   ; table of scratch plus OP_XMS rows asks
.clb:                           ; as well - op_xload with [op_clb] = 0 refused
                                ; every such package as `Cannot read my parts`
    call op_clbget              ; ...asked once, and cached (op_clbget)
    jc .nodisk
.claim:
    call op_claim               ; ONE claim: the run and every granted scratch
    jc .nomem                   ; part after it
    cmp word [op_secs], 0
    je .zero
    call op_read
    jc .ioerr
    cmp byte [op_zn], 0         ; ...and if any of it arrived COMPRESSED, walk
    je .zero                    ; it down out of where it landed (20.12.7). It
    call op_unpack              ; is the only thing a compressed part costs at
    jc .fail                    ; run time, and op_unpack has said why
.zero:
    call op_scrub               ; ...and the scratch, which cost no disk
    cmp byte [op_xmson], 0
    je .done
    call op_xload               ; ...and the span that goes above 1MB
    jc .ioerr
.done:
    clc
    ret

.fail:                          ; op_unpack has already put its own message up
    stc
    ret
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
    mov word [op_tail], 0
    mov word [op_bytes], 0
    mov word [op_usecs], 0
    mov word [op_ubytes], 0
    mov byte [op_zn], 0
    mov word [op_reqkb], 0
    mov word [op_allkb], 0
    mov word [op_xoff], 0
    mov word [op_xsecs], 0
    mov word [op_xmskb], 0
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
    jnz .filed
    jmp .scratch                ; no file bytes: it is scratch, and it costs
                                ; no disk at all - which is what makes a
                                ; working set DECLARABLE rather than claimed
                                ; from the entry proc and refused afterwards
.filed:
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
    or bx, bx
    jnz .have
    mov bx, ax                  ; the first filed row anchors the run
.have:
    push cx
    push ax
    mov ax, [si+OP_R_LEN]       ; the UNPACKED total, which is what the CLAIM
    add ax, 511                 ; is cut from - so a compressed row costs the
    jc .ovfd                    ; machine exactly what it would have cost
    mov cl, 9                   ; uncompressed, and refusing still reads no
    shr ax, cl                  ; disk at all (SPEC.md 20.12.7)
    add [op_usecs], ax
    jc .ovfd
    pop ax
    push ax

    mov cx, [si+OP_R_LEN]       ; ...and the DISK extent, which on a compressed
    test byte [si+OP_R_FLAG], OP_COMP   ; row is the PACKED length in zkb. The
    jz .clen                            ; two are the same run measured at its
    mov cx, [si+OP_R_ZKB]               ; two ends
    inc byte [op_zn]
.clen:
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
    push ax                     ; this row ends the run (so far): the file
    mov ax, [si+OP_R_LEN]       ; carries no padding after it if it is also
    neg ax                      ; the LAST body, so a read to its sector
    and ax, 511                 ; boundary comes back short by exactly this,
    mov [op_tail], ax           ; and [op_want] must not ask for it
    pop ax
    jmp short .next
.scratch:
    push ax
    mov ax, [si+OP_R_ZKB]
    add [op_allkb], ax          ; every scratch part counts toward the total...
    jc .ovfa
    test byte [si+OP_R_FLAG], OP_OPT
    jnz .optonly
    add [op_reqkb], ax          ; ...and only a REQUIRED one toward the floor
    jc .ovfa
.optonly:
    pop ax
.next:
    add si, OP_ROW
    dec cx
    jmp .one

.run:
    cmp word [op_xsecs], 128    ; the span is climbed through one WORD of
    jae .bad                    ; bytes (op_xload: `shl ax, 9`), so it has the
                                ; same 64KB ceiling the carve has below - 128
                                ; sectors read as 0 there, and 180 as 26,624
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
    mov ax, [op_usecs]
    cmp ax, 128
    jae .bad                    ; the carve's own ceiling, on the side that
                                ; decides the claim rather than the read
    push ax
    mov cl, 9
    shl ax, cl
    mov [op_ubytes], ax
    pop ax
    inc ax
    shr ax, 1                   ; sectors -> KB, rounded up
    add [op_reqkb], ax          ; the carve is never optional: it is one claim
    jc .bad                     ; and one read, and half of it is not a thing
    add [op_allkb], ax
    jc .bad
.norun:
    clc
    ret
.ovfx:                          ; the .xspan path banks AX *and* CX, so its
    pop cx                      ; overflow exit has to give BOTH back: with
    pop ax                      ; .ovfc's single pop it returned at depth +1
    stc                         ; and `ret` took the saved AX for its return
    ret                         ; address (SPEC.md 20.12)
.ovfd:                          ; the .conv arm banks AX *and* CX, so its
    pop ax                      ; overflow exit gives both back - .ovfc's
    pop cx                      ; single pop returned at depth +1 and `ret`
    jmp short .bad              ; took the saved word for its return address
.ovfc:
    pop cx
    jmp short .bad
.ovfa:
    pop ax
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
    push ax                     ; ...and before the rounding, THIS is what has
    sub ax, [op_tail]           ; to arrive - op_read's fourth input - less the
    mov [op_want], ax           ; padding after the last row, which is not in
    pop ax                      ; the file when nothing follows it (op_size)
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

    ; The claim's share for the run is R PLUS THE READ, and that one quantity
    ; covers both things that have to fit: the read occupies R + op_cap from
    ; the base, and the unpacked carve occupies slack + op_ubytes - and
    ; R + op_cap >= (unpacked - packed + margin) + (slack + packed), which is
    ; slack + unpacked + margin, so the read is always the larger. With
    ; nothing compressed R is 0 and this is the line that was here before.
    mov ax, [op_rpara]
    mov cl, 4
    shl ax, cl                  ; R, in bytes - it is under 64KB because
    add ax, [op_cap]            ; op_ubytes is (op_size refuses 128 sectors)
    jc .no
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
    call OSAPI_MEM_CLAIM        ; out DX = the base segment
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
    test byte [si+OP_R_FLAG], OP_LAZY
    jz .notlazy
    mov ax, [si+OP_R_ZKB]       ; A LAZY ROW BANKS ITS SEGMENT IN ITS OWN zkb
    jmp .out                    ; WORD, and 0 there means "not fetched". The
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
    test byte [si+OP_R_FLAG], OP_XMS
    jz .conv
    cmp byte [op_xmson], 0
    jne .none                   ; it went ABOVE 1MB: op_lin is what answers,
                                ; and a segment for it does not exist
.conv:
    mov bx, [si+OP_R_OFF]
    or bx, bx
    jz .scratch                 ; no file bytes: it is in the scratch area

    cmp byte [op_zn], 0
    jne .zwalk                  ; a compressed row is in the table, so the
                                ; claim is NOT the disk's layout any more

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

.scratch:                       ; --- scratch: after the run, in TABLE order --
    ; It is walked rather than banked because the answer depends on which
    ; OPTIONAL parts were granted, and that is one byte the caller can also
    ; read (op_optok) rather than a second table nobody maintains.
    test byte [si+OP_R_FLAG], OP_OPT
    jz .place
    cmp byte [op_optok], 0
    je .none                    ; refused, and the package carries on without
                                ; it - which is what OP_OPT is (SPEC.md 47)
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
    test byte [si+OP_R_FLAG], OP_OPT
    jz .wadd
    cmp byte [op_optok], 0
    je .wnext                   ; refused: it takes up no room
.wadd:
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
    cmp word [si+OP_R_ZKB], 0
    jne .yes                    ; already here, and having it costs nothing
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
    cmp word [si+OP_R_ZKB], 0
    je .get
    clc                         ; already here
    ret
.get:
    call op_clbget
    jc .nodisk
    call op_lazykb              ; AX = the KB it wants, slack included
    jc .toobig
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
    ; 20.12.7). A lazy part is never compressed (the macro refuses the
    ; pairing), so this one lands at the claim itself.
    pop dx
    push si
    push bx
    push dx
    push word [op_base]
    push word [op_rdseg]
    mov [op_base], dx
    mov [op_rdseg], dx
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
    mov [si+OP_R_ZKB], ax       ; THE ROW IS WHERE A FETCHED PART LIVES
    clc
    ret

.ioerr:
    call OSAPI_MEM_FREE         ; DX is still the claim, and a fetch that
    mov si, op_s_read           ; failed must leave nothing behind
    jmp op_say
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
    mov word [si+OP_R_ZKB], 0   ; CLEARED FIRST: op_seg must never answer a
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

; --- refusals, in the package's own words (SPEC.md 47, 59) -------------------
; TOAST_MAX is 24 characters and a cut message reads like a bug in the thing
; it is reporting on, so these are short on purpose.
op_s_big:   db 'Parts do not fit', 0
op_s_mem:   db 'Not enough memory', 0
op_s_disk:  db 'No disk to read from', 0
op_s_read:  db 'Cannot read my parts', 0
op_s_lazy:  db 'Not a fetchable part', 0
op_s_zbad:  db 'A part will not unpack', 0

; --- our bss, and YOURS FOLLOWS IT ------------------------------------------
; This SDK's bss is an equ chain off os88_image_end (SPEC.md 20.5), so the
; standard takes the first OP_BSS bytes of it and a package bases its own at
; os88_image_end + OP_BSS. OS88_BSS is told the sum.
;
; OP_BSS_AT IS WHERE THAT CHAIN STARTS, and it is a %define so that a C
; PACKAGE can move it (SPEC.md 73.2). An assembly package's bss really is the
; bytes past its image, so os88_image_end is the base and this is invisible.
; A C package has no such label: SmallerC emits four sections and the loader
; zeroes a real `.bss` between cc_bss_beg and cc_bss_end, so apps/cc/crt0.asm
; reserves OP_BSS bytes IN THAT SECTION and points this at them. One symbol
; moves and the whole chain follows, which is the only reason the standard
; needed no second version for C.
op_name     equ OP_BSS_AT + 0       ; the file we came out of, NUL-term
op_first    equ OP_BSS_AT + 13      ; the carved run's first sector
op_secs     equ OP_BSS_AT + 15      ; ...its length in sectors, 0 = none
op_kb       equ OP_BSS_AT + 17      ; ...in KB
op_bytes    equ OP_BSS_AT + 19      ; ...and in bytes
op_clb      equ OP_BSS_AT + 21      ; this volume's bytes per cluster
op_off      equ OP_BSS_AT + 23      ; the cluster-aligned read offset (32)
op_slack    equ OP_BSS_AT + 27      ; ...how far the run is past it
op_cap      equ OP_BSS_AT + 29      ; the capacity that read asks for
op_base     equ OP_BSS_AT + 31      ; the claim it lands in
op_reqkb    equ OP_BSS_AT + 33      ; the run plus the REQUIRED scratch...
op_allkb    equ OP_BSS_AT + 35      ; ...and plus the optional too
op_runkb    equ OP_BSS_AT + 37      ; KB from the base to the scratch area
op_optok    equ OP_BSS_AT + 39      ; byte: were the optional parts
                                         ; granted? A package that greys a row
                                         ; reads this rather than testing
                                         ; op_seg against zero, because zero
                                         ; is also what a table it mis-declared
                                         ; would answer (SPEC.md 47)
op_xmson    equ OP_BSS_AT + 40      ; byte: is there a store to put the
                                         ; OP_XMS parts in? Asked ONCE, before
                                         ; the sizing, because the answer moves
                                         ; those parts between two totals
op_xoff     equ OP_BSS_AT + 41      ; the XMS span's first sector...
op_xsecs    equ OP_BSS_AT + 43      ; ...its length in sectors...
op_xmskb    equ OP_BSS_AT + 45      ; ...and the KB it wants up there
op_xlin     equ OP_BSS_AT + 47      ; the one block it all landed in (32)
op_xcur     equ OP_BSS_AT + 51      ; ...and where the climb is up to (32),
                                         ; which is a SECOND word and not a
                                         ; walked op_xlin: the first version
                                         ; walked the base and put it back by
                                         ; subtracting the BLOCK's size, while
                                         ; what the cursor advanced by is the
                                         ; SPAN's - so every part came out
                                         ; (block - span) low and op_xload's own
                                         ; failure path freed an address that
                                         ; was never claimed
op_xslack   equ OP_BSS_AT + 55      ; op_xload's own head slack...
op_xrd      equ OP_BSS_AT + 57      ; ...its cluster-aligned read cursor (32)
op_xleft    equ OP_BSS_AT + 61      ; ...bytes of the span still to copy up
op_xneed    equ OP_BSS_AT + 63      ; ...what the read has to cover in all
op_xcap     equ OP_BSS_AT + 65      ; ...one bufferful of it
op_xstg     equ OP_BSS_AT + 67      ; ...and the transient claim it uses
op_want     equ OP_BSS_AT + 69      ; bytes of the run that MUST still arrive,
                                    ; so a file that ends inside it refuses
                                    ; rather than filling half a claim
op_tail     equ OP_BSS_AT + 71      ; ...less this: the sector padding AFTER the
                                    ; run's last row, which the packer does not
                                    ; write (tools/os88pkg.py pads BEFORE each
                                    ; body and never after the last), so a run
                                    ; that ends the file arrives short by it

op_zn       equ OP_BSS_AT + 73      ; byte: how many rows carry OP_COMP. ZERO
                                    ; IS THE WHOLE OF THE OLD BEHAVIOUR - every
                                    ; path below tests it and takes exactly
                                    ; what it took before, so a package with no
                                    ; compressed part is unchanged to the
                                    ; instruction (SPEC.md 20.12.7)
op_usecs    equ OP_BSS_AT + 74      ; the run's UNPACKED length in sectors...
op_ubytes   equ OP_BSS_AT + 76      ; ...and in bytes. The claim is cut from
                                    ; these and the READ from op_secs/op_bytes,
                                    ; and the two are the same run measured at
                                    ; its two ends
op_rpara    equ OP_BSS_AT + 78      ; R, in paragraphs: how far up the claim the
                                    ; packed run lands so op_unpack can walk
                                    ; down onto it. 0 = it lands at the base,
                                    ; which is what it always did
op_rdseg    equ OP_BSS_AT + 80      ; ...and the segment that comes to, which is
                                    ; what op_read fills from. Its callers set
                                    ; it: op_claim to the base plus R, op_fetch
                                    ; to the claim it just made
op_zsrc     equ OP_BSS_AT + 82      ; op_unpack's read pointer, as a segment -
                                    ; BP is its write pointer and there is no
                                    ; third register left to hold this one
op_bend     equ OP_BSS_AT + 84      ; the run's EXACT byte end, where op_bytes
                                    ; is the same run rounded to whole sectors.
                                    ; op_want is cut from this one, because the
                                    ; last part's padding is the end of the
                                    ; FILE and asking for it is a short read
OP_BSS      equ 86
