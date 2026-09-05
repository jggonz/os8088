; =============================================================================
; os8088 - apps/thewire/wrarc.inc
;
; THE ARCHIVE (SPEC.md 88.13) AND RUN FROM RAM (SPEC.md 88.14).
;
; A program that is a folder tree - RunCPM's A/0/, the C64's parts - was
; WF_FLOPPY until this file, because the Wire moved one flat file per HTTP
; connection into one folder. An archive moves the tree in ONE stream, and
; both actions run the same unpacker over it: Load Program into a RAM disk,
; Add to Disk where the Save dialog pointed.
;
; --- IT IS TWO HALVES AND THE LINE IS SPEC.md 20.6 RULE 7 --------------------
; The WORKER may not call OSAPI_MEM_*, the file slots or the dialog, so it can
; neither make the claim the unpacker decodes into nor write the file an entry
; becomes. What it CAN do is feed bytes to a state machine. So:
;
;   the worker    wr_afeed / wr_lzb / wr_ostore - a byte at a time, into the
;                 UI task's claim, and it PAUSES at every boundary where the
;                 next thing to do belongs to the other task
;   the UI task   wr_arcbase (the claim and `home`), wr_arcwrite (the folders
;                 and the file), wr_arcend (the launch or the toast),
;                 wr_ramck (the RAM disk decision)
;
; The pause is WS_PAUSE, which is INSIDE the "in flight" range wr_may and
; wr_start test (thewire.asm's WS_* block says why that range is contiguous),
; and it is posted with SPEC.md 88.5's one byte exactly like every other wake:
; everything the handler reads is written first and [wr_wake] LAST.
;
; --- THE PAUSE HOLDS THE WIRE AND COSTS NOTHING ------------------------------
; A paused worker stops calling NETV_RECV. The bytes it has drained and not
; yet fed stay in wr_rxb at [wr_rxp], and TCP's own receive window holds the
; rest on the server. There is no second buffer and no copy: the drain loop
; simply starts where it stopped (wrhttp.inc's .drain).
;
; --- ONE CLAIM FOR THE WHOLE TREE --------------------------------------------
; The header's WA_MAXENT is the largest entry the archive holds, so one claim
; of that size decodes every one of them in turn - which is why the claim can
; only be made once the 32-byte header has arrived, and why the header's
; boundary is a pause of its own (WW_HDR). An LZSS back-reference is a copy
; WITHIN that claim (SPEC.md 88.13: there is no window buffer), which is what
; makes the decoder four bytes of state and therefore resumable across the
; 1,024-byte drains.
;
; --- EVERYTHING OFF THE WIRE IS HOSTILE --------------------------------------
; wr_ahdrck and wr_aentck check every field before a byte is stored, and
; wr_anamck refuses a name with a path separator in it or a leading dot: a
; slot is ONE name, and either spelling would let an archive write outside the
; tree the user chose. A refusal is `That archive is not understood` and
; leaves what was already written, which is SPEC.md 22's rule and 88.8's.
; =============================================================================

; --- the unpacker's phase ([wr_aph]) ------------------------------------------
WAP_HDR     equ 0               ; accumulating the 32-byte file header
WAP_ENT     equ 1               ; ...or a 64-byte entry header
WAP_BODY    equ 2               ; ...or feeding an entry's body
WAP_END     equ 3               ; every entry is complete: a further byte is a
                                ; server that disagrees with its own header

; =============================================================================
; wr_afeed - one body byte into the unpacker (SPEC.md 88.13)
; in:  AL = the byte. THE WORKER, inside wr_take's per-byte loop.
; out: [wr_state] is left WS_BODY to carry on, WS_PAUSE at a boundary (with
;      [wr_apw] = the wake to post), or WS_FAIL on a refusal with [wr_msg]
;      already said. Every register preserved.
; =============================================================================
wr_afeed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bl, [wr_aph]
    cmp bl, WAP_ENT
    je .ent
    cmp bl, WAP_BODY
    je .body
    cmp bl, WAP_HDR
    jne .bad                        ; WAP_END: a byte after the last entry
    mov di, [wr_acnt]               ; --- the file header, 32 bytes
    mov [wr_ahdr + di], al
    inc di
    mov [wr_acnt], di
    cmp di, WARC_HDR
    jb .out
    call wr_ahdrck
    jc .bad
    mov word [wr_acnt], 0
    mov byte [wr_aph], WAP_ENT
    mov byte [wr_apw], WW_HDR       ; ...and the UI task owes us the claim
    jmp short .pause                ; before the first entry byte is stored

.ent:
    mov di, [wr_acnt]               ; --- one entry header, 64 bytes
    mov [wr_ehdr + di], al
    inc di
    mov [wr_acnt], di
    cmp di, WARC_ENT
    jb .out
    call wr_aentck                  ; ...checked, and the counters set up
    jc .bad
    mov word [wr_acnt], 0
    mov byte [wr_aph], WAP_BODY
    cmp word [wr_asto], 0
    jne .out
    jmp short .entdone              ; AN EMPTY FILE has no body at all, so
                                    ; this entry is complete on its header

.body:
    inc word [wr_ain]
    cmp byte [wr_ehdr + WA_METHOD], WAM_LZSS
    je .lz
    call wr_ostore                  ; WAM_STORED: the byte as it stands
    jc .bad
    jmp short .test
.lz:
    call wr_lzb
    jc .bad
.test:
    mov ax, [wr_ain]                ; --- is the entry finished?
    cmp ax, [wr_asto]
    jb .notin
    mov ax, [wr_aout]               ; the stored bytes are all consumed, so
    cmp ax, [wr_asize]              ; the output must be complete TO THE BYTE
    jne .bad                        ; (88.13: a stream that ends short)
    jmp short .entdone
.notin:
    mov ax, [wr_aout]
    cmp ax, [wr_asize]
    jb .out
    jmp short .bad                  ; ...and the other way round: the output
                                    ; is complete with stored bytes left over

.entdone:
    inc word [wr_adone]             ; THE UNPACKER COUNTS, and the handler
    mov ax, [wr_adone]              ; reads the count after banking the wake
    cmp ax, [wr_an]                 ; byte (88.5's order)
    jb .more
    mov byte [wr_aph], WAP_END
    jmp short .epause
.more:
    mov byte [wr_aph], WAP_ENT
.epause:
    mov byte [wr_apw], WW_ENTRY
.pause:
    mov byte [wr_state], WS_PAUSE
    jmp short .out
.bad:
    mov word [wr_msg], wr_s_badarc
    mov byte [wr_state], WS_FAIL
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_ahdrck - is what arrived an archive, and is it THIS record's? (88.13)
; out: CF = 1 refused
;
; The catalog said what the tree unpacks to and what a store must hold, and
; the archive's own header says it again. They have to agree: WC_TOTAL is what
; the Add to Disk free-space check was decided on and WC_NEEDKB is what the
; RAM disk was mounted for, so a header that disagrees with the record has
; already invalidated a decision taken before the first byte moved.
; -----------------------------------------------------------------------------
wr_ahdrck:
    push ax
    push si
    push es
    cmp word [wr_ahdr + WA_MAGIC], 'WP'     ; 'WPAK', as two words
    jne .no
    cmp word [wr_ahdr + WA_MAGIC + 2], 'AK'
    jne .no
    cmp byte [wr_ahdr + WA_VER], WARC_VER
    jne .no
    mov ax, [wr_ahdr + WA_N]
    or ax, ax
    jz .no
    cmp ax, WARC_NMAX
    ja .no
    mov [wr_an], ax
    mov ax, [wr_ahdr + WA_MAXENT]           ; the ONE claim's size, and it is
    or ax, ax                               ; the whole reason this boundary
    jz .no                                  ; is a pause
    cmp ax, WIRE_FILEMAX
    ja .no
    call wr_selrec                  ; --- and the record's own figures
    mov ax, [wr_ahdr + WA_TOTAL]
    cmp ax, [es:si+WC_TOTAL]
    jne .no
    mov ax, [wr_ahdr + WA_TOTAL + 2]
    cmp ax, [es:si+WC_TOTAL+2]
    jne .no
    mov ax, [wr_ahdr + WA_NEEDKB]
    cmp ax, [es:si+WC_NEEDKB]
    jne .no
    mov al, [wr_ahdr + WA_FLAGS]
    mov [wr_aflags], al
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_aentck - one entry header, checked, and its counters set (88.13)
; out: CF = 1 refused
;
; BOTH SIZES ARE HELD IN WORDS and a dword that needs its high half is
; refused here. The unpacked size is bounded by WIRE_FILEMAX = 63KB, which is
; one claim and one OSAPI_FILE_WRITE (88.2); the STORED size has no such bound
; in the format, so this is where it gets one - a body of 64KB or more is
; refused rather than counted in a word that would lap.
; -----------------------------------------------------------------------------
wr_aentck:
    push ax
    push bx
    push cx
    push si
    cmp byte [wr_ehdr + WA_METHOD], WAM_LZSS
    ja .no
    cmp byte [wr_ehdr + WA_DEPTH], WARC_DEPTH
    ja .no
    cmp word [wr_ehdr + WA_SIZE + 2], 0
    jne .no
    mov ax, [wr_ehdr + WA_SIZE]
    cmp ax, WIRE_FILEMAX
    ja .no
    cmp ax, [wr_dmax]               ; ...and it must fit the claim the header
    ja .no                          ; bought, which WA_MAXENT promised
    mov [wr_asize], ax
    cmp word [wr_ehdr + WA_STORED + 2], 0
    jne .no
    mov bx, [wr_ehdr + WA_STORED]
    mov [wr_asto], bx
    or bx, bx
    jnz .sok
    or ax, ax
    jnz .no                         ; no body and a size: a short stream, and
.sok:                               ; saying so here beats waiting for a byte
    mov cl, [wr_ehdr + WA_DEPTH]    ; that never comes
    xor ch, ch
    inc cx                          ; the folders AND the file name
    mov si, wr_ehdr + WA_PATH
.slot:
    push cx
    call wr_anamck
    pop cx
    jc .no
    add si, WARC_SLOT
    loop .slot
    mov word [wr_aout], 0           ; --- and the body's own state
    mov word [wr_ain], 0
    mov byte [wr_lph], 0
    mov byte [wr_lbits], 0
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_anamck - is the WARC_SLOT slot at DS:SI one name? out CF = 1 no
;
; One to TWELVE printable ASCII, NUL-padded when shorter and **filling the
; slot with no NUL when it is twelve** (SPEC.md 88.13): `CONSOLE7.COM` is
; twelve characters and five of the RunCPM master disk's files are, so a
; reader that demanded a terminator would refuse the archive this format
; exists for.
;
; And NO PATH SEPARATOR OF ANY SPELLING and no leading dot. A slot is one
; NAME: the depth field says how many of them there are, so a separator
; inside one - or a `.` or a `..` - is an archive writing outside the folder
; the user chose, which is the only thing in this format that could reach a
; file the Wire was never pointed at.
; -----------------------------------------------------------------------------
wr_anamck:
    push ax
    push bx
    push cx
    mov bx, si
    xor ch, ch
    mov cl, WARC_SLOT
.c:
    mov al, [bx]
    or al, al
    jz .end
    cmp al, ' '
    jbe .no
    cmp al, 0x7F
    jae .no
    cmp al, '/'
    je .no
    cmp al, '\'
    je .no
    cmp al, ':'
    je .no
    inc bx
    inc ch
    loop .c                         ; ...and falling out of the loop is a
.end:                               ; TWELVE-character name, which is legal
    or ch, ch
    jz .no                          ; an empty slot below the depth
    cmp byte [si], '.'
    je .no                          ; '.', '..' and every other spelling of a
    clc                             ; name that is not a name here
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_ostore - AL into the entry's output at [wr_aout]
; out: CF = 1 past the entry's own end; every register preserved
; -----------------------------------------------------------------------------
wr_ostore:
    push bx
    push es
    mov bx, [wr_aout]
    cmp bx, [wr_asize]
    jae .no
    mov es, [wr_dseg]
    mov [es:bx], al
    inc word [wr_aout]
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop bx
    ret

; -----------------------------------------------------------------------------
; wr_lzb - one LZSS byte (SPEC.md 88.13's decoder, pinned)
; in:  AL = the byte; out CF = 1 refused
;
; Groups of one FLAG byte and up to eight items, bit 0 first. A set bit is a
; literal; a clear one is a pair `B0 B1` with distance ((B1 >> 4) << 8 | B0)
; + 1 and length (B1 & 15) + 3. The copy is ONE BYTE AT A TIME and forwards,
; so a distance shorter than the length repeats - which is how the format
; spells a run, and a `rep movsb` here would spell something else.
;
; THE STATE IS FOUR BYTES - the phase, the flag, its bits left and a pair's
; first byte - which is what makes it resumable across a drain that ends in
; the middle of a pair.
; -----------------------------------------------------------------------------
wr_lzb:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [wr_lph], 1
    je .second
    cmp byte [wr_lbits], 0
    jne .item
    mov [wr_lflag], al              ; a new group's flag byte
    mov byte [wr_lbits], 8
    jmp short .ok
.item:
    test byte [wr_lflag], 1
    jz .pair0
    call wr_ostore                  ; a LITERAL
    jc .no
    jmp short .adv
.pair0:
    mov [wr_lb0], al                ; a PAIR, and its second byte is the next
    mov byte [wr_lph], 1            ; one the wire hands over - possibly in
    jmp short .ok                   ; the next chunk entirely
.second:
    mov byte [wr_lph], 0
    mov bl, al
    and al, 0x0F
    add al, WARC_LENMIN
    xor ah, ah
    mov cx, ax                      ; CX = the length, 3..18
    mov al, bl
    shr al, 1                       ; cpu 8086: no `shr reg, imm` but 1
    shr al, 1
    shr al, 1
    shr al, 1
    mov dh, al
    mov dl, [wr_lb0]
    inc dx                          ; DX = the distance, 1..4096
    mov di, [wr_aout]
    cmp dx, di
    ja .no                          ; past the start of this entry's output
    mov ax, di
    add ax, cx
    jc .no
    cmp ax, [wr_asize]
    ja .no                          ; ...or past its end
    mov es, [wr_dseg]
    mov si, di
    sub si, dx
.copy:
    mov al, [es:si]
    mov [es:di], al
    inc si
    inc di
    loop .copy
    mov [wr_aout], di
.adv:
    shr byte [wr_lflag], 1
    dec byte [wr_lbits]
.ok:
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE UI TASK'S HALF - the claim, the folders, the files and the outcome
; =============================================================================

; -----------------------------------------------------------------------------
; wr_arcstart - one transfer for the whole tree (SPEC.md 88.14)
; in:  [wr_sel] is a WF_ARC record and the instance already stands where the
;      tree is to go - the RAM disk's root, or the folder the Save dialog
;      left us in. [wr_job] is set.
; out: nothing; the worker picks it up on its next pass
;
; **THE TRANSFER STARTS WITH NO DESTINATION AT ALL** - AX, BX and CX are zero
; where every other kind passes a claim. The claim's size is the header's
; WA_MAXENT and the header has not arrived yet, so wr_arcbase makes it at the
; WW_HDR pause and writes [wr_dseg] and [wr_dmax] then.
; -----------------------------------------------------------------------------
wr_arcstart:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, wr_fname                ; /wire/pkg/<STEM>.WPK
    mov si, wr_x_wpk
    call wr_stemput
    mov byte [wr_aph], WAP_HDR      ; --- the unpacker, from the top
    mov word [wr_acnt], 0
    mov word [wr_adone], 0
    mov word [wr_an], 0
    mov byte [wr_ldep], 0xFF        ; ...and no folder banked, so the first
                                    ; entry always stands somewhere
    call wr_freefile                ; a claim from a previous chain, if any
    xor ax, ax
    xor bx, bx
    xor cx, cx
    mov dx, WK_ARC
    call wr_start
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_arcbase - the WW_HDR pause: the claim, `home`, and where we stand
; in:  the UI task, NO LOCK (it claims and it makes a folder)
; out: CF = 1 and [wr_msg] said; CF = 0 and [wr_state] is back at WS_BODY
; -----------------------------------------------------------------------------
wr_arcbase:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [wr_ahdr + WA_MAXENT]   ; --- ONE claim for every entry in turn
    add ax, 1023
    and ax, 0xFC00                  ; the capacity in BYTES, rounded up to the
    mov [wr_dmax], ax               ; KB OSAPI_MEM_CLAIM deals in - and
                                    ; wr_aentck refuses an entry against it.
                                    ; WA_MAXENT is 1..WIRE_FILEMAX by the check
                                    ; above, so the KB below cannot come out
                                    ; zero and there is no floor to apply
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [wr_fseg], dx               ; ...and wr_freefile gives it back, so the
    mov [wr_dseg], dx               ; archive leaks nothing an ordinary chain
                                    ; does not
    cmp byte [wr_ahdr + WA_HOME], 0
    je .bank
    mov si, wr_ahdr + WA_HOME       ; --- `home`, made and entered first, so
    call wr_anamck                  ; the whole tree lands under one folder
    jc .badarc                      ; (88.13). A NAME the format refuses is
                                    ; the archive's fault and says so...
    call wr_aname                   ; ...and the kernel wants a terminator
    mov [wr_wname], si
    call wr_amkcd
    jc .nohome                      ; ...and a folder the DISK would not make
                                    ; is the disk's, and names it: `Could not
                                    ; write TREE` is what a full floppy or a
                                    ; RAM disk with no directory rows left
                                    ; gives, and `That archive is not
                                    ; understood` would send the reader to the
                                    ; wrong half of the machine
.bank:
    call OSAPI_FILE_HERE            ; DX = the folder, BL = the drive
    mov [wr_bclus], dx
    mov [wr_bdrv], bl
    mov byte [wr_state], WS_BODY    ; ...and the worker may carry on
    clc
    jmp short .out
.nomem:
    mov word [wr_msg], wr_s_nomem
    jmp short .no
.nohome:
    call wr_wrfail
    jmp short .no
.badarc:
    mov word [wr_msg], wr_s_badarc
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_amkcd - make the folder named at DS:SI if it is missing, and enter it
; out: CF = 1 neither. FERR_EXIST from the make is NOT an error: a second
; archive into the same home, and every entry after the first in one folder,
; both arrive here with the folder already there.
wr_amkcd:
    push si
    call OSAPI_FILE_MKDIR
    pop si
    push si
    call wr_dive
    pop si
    ret

; -----------------------------------------------------------------------------
; wr_arcwrite - this entry's folders, then the file itself
; in:  the UI task, NO LOCK (it makes folders and writes a file)
; out: CF = 1 and [wr_msg] names the file that failed
;
; **THE FOLDER WE STAND IN IS BANKED** (88.14). An entry whose folders match
; the last one's is written without moving at all, which with 88.13's grouping
; rule - the writer puts every entry of a folder together - is ONE folder
; change per folder rather than one per file. On a floppy a folder change is
; an OSAPI_FILE_FIND walk and a GOTO, each priced in int 13h calls, so the
; difference over a sixty-file tree is tens of seconds of the field machine.
; -----------------------------------------------------------------------------
wr_arcwrite:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, wr_fname                ; --- the NAME is the slot after the
    call wr_aslot                   ; folders, and it goes in wr_fname whole:
    mov cx, WARC_SLOT               ; the write wants it, the failure message
    call wr_sputn                   ; wants it and so does the launch. wr_sputn
    mov byte [di], 0                ; and NOT wr_sput: a twelve-character name
                                    ; has no NUL after it and the next SLOT is
                                    ; what wr_sput would run into (88.13)
    mov word [wr_wname], wr_fname   ; BANKED, because on a DVK_FILE volume CX,
                                    ; SI and DI are the driver's across the
                                    ; write itself (SPEC.md 62.9)
    mov cl, [wr_ehdr + WA_DEPTH]    ; --- are we standing there already?
    cmp cl, [wr_ldep]
    jne .move
    or cl, cl
    jz .there                       ; both at depth 0: the base itself
    call wr_apathn                  ; CX = the folders' bytes
    mov si, wr_ehdr + WA_PATH
    mov di, wr_lpath
    repe cmpsb
    je .there
.move:
    mov dx, [wr_bclus]              ; --- back to the base, then down again
    mov bl, [wr_bdrv]
    call OSAPI_FILE_GOTO_QM
    jc .no
    mov cl, [wr_ehdr + WA_DEPTH]
    xor ch, ch
    mov si, wr_ehdr + WA_PATH
    jcxz .bank
.d:
    push cx
    push si
    call wr_aname                   ; a terminated copy: a FOLDER name may be
    call wr_amkcd                   ; twelve characters too
    pop si
    pop cx
    jc .no
    add si, WARC_SLOT
    loop .d
.bank:
    push ds                         ; ES AGAIN: the folder walk above went
    pop es                          ; through OSAPI_FILE_MKDIR and _GOTO_QM,
                                    ; and on a DVK_FILE volume - which a RAM
                                    ; disk is - a file cell owns every register
                                    ; the FSV ABI does not reserve
    mov cl, [wr_ehdr + WA_DEPTH]
    mov [wr_ldep], cl
    xor ch, ch
    jcxz .there
    call wr_apathn
    mov si, wr_ehdr + WA_PATH
    mov di, wr_lpath
    rep movsb
.there:
    mov si, wr_fname                ; --- and the file, whole, in one call
    mov es, [wr_dseg]
    xor bx, bx
    mov cx, [wr_asize]
    xor dx, dx
    call OSAPI_FILE_WRITE
    jc .no
    clc
    jmp short .out
.no:
    call wr_wrfail                  ; 'Could not write <name>' - a failure
    stc                             ; mid-tree LEAVES WHAT WAS WRITTEN (22)
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_aname - SI = a NUL-TERMINATED copy of the slot at DS:SI -------------
; **EVERY KERNEL CELL WANTS A TERMINATOR AND A SLOT MAY NOT HAVE ONE.**
; OSAPI_FILE_WRITE, OSAPI_FILE_MKDIR and wr_dive's own compare all read to a
; NUL, and SPEC.md 88.13's twelve-character name fills its slot without one -
; so the copy is wr_sputn, bounded at WARC_SLOT and stopping at a NUL either
; way, into a thirteen-byte buffer that always gets one.
wr_aname:
    push ax
    push cx
    push di
    push es
    push ds
    pop es                          ; wr_sputn reads ES:SI, and here the slot
                                    ; is in OUR segment rather than in the
                                    ; catalog claim
    mov di, wr_anb
    mov cx, WARC_SLOT
    call wr_sputn
    mov byte [di], 0
    mov si, wr_anb
    pop es
    pop di
    pop cx
    pop ax
    ret

; --- wr_aslot - SI = this entry's file-NAME slot -----------------------------
; The path is WARC_SLOTS slots and the depth says how many of them are
; folders, so the name is the one after them.
wr_aslot:
    push cx
    call wr_apathn                  ; the folders' bytes, and the name is the
    mov si, wr_ehdr + WA_PATH       ; slot after them - one `mul` between the
    add si, cx                      ; two of these rather than one each
    pop cx
    ret

; --- wr_apathn - CX = the bytes the FOLDER slots occupy ----------------------
wr_apathn:
    push ax
    mov al, [wr_ehdr + WA_DEPTH]
    mov ah, WARC_SLOT
    mul ah
    mov cx, ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_arcend - the last entry has landed (SPEC.md 88.14)
; in:  the UI task, NO LOCK
;
; **THE PROGRAM ENTRY IS LAST SO THAT THE LAUNCH COSTS NOTHING**: the claim
; still holds it, byte for byte what the file on the tree now holds, and
; OSAPI_PKG_RUN takes it from there with the instance's directory already ON
; the tree - so the overlay and the sidecars are where the launched package
; will look for them (SPEC.md 73.14, 88.8).
;
; The socket goes back HERE rather than at the close: the worker is parked in
; WS_PAUSE and will never ask for another byte, so a handle held past this
; point is one of NET_SOCKS four held for nothing.
; -----------------------------------------------------------------------------
wr_arcend:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    inc byte [wr_gen]
    call wr_hclose
    mov byte [wr_state], WS_DONE
    cmp byte [wr_ramjob], 0
    je .noprog
    test byte [wr_aflags], WAH_PROGRAM
    jz .noprog
    mov ax, [wr_asize]              ; --- the tree carries a program, and
    mov [wr_rlen], ax               ; wr_fname is already its name
    call wr_pkgrun                  ; NO LOCK: it opens a window (88.5)
    mov byte [wr_job], WJ_NONE
    call wr_freefile
    call OSAPI_GFX_LOCK
    call wr_clip                    ; ...and the launched window is now ON TOP
    call wr_geom                    ; of ours, which is what 88.6.1 is about
    call wr_dend
    jmp short .un
.noprog:
    call wr_lockarm
    call wr_chaindone               ; the toast says which disk it landed on
.un:
    call OSAPI_GFX_UNLOCK
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; wr_ramck - the RAM disk decision (SPEC.md 88.14 steps 1..3, 88.7)
; in:  AL = 0 ASK ONLY (the predicate) / non-0 MOUNT if there is no store
; out: CF = 0 and BL = the volume index to stand on (0xFF when asking only and
;      nothing is mounted yet); CF = 1 and SI = the reason, a NUL string.
;      Every other register preserved.
;
; **THE UI TASK ONLY** - both verbs are (SPEC.md 62.9.16): RDPV_STATE asks
; OSAPI_MEM_AVAIL and RDPV_MOUNT registers a volume and paints a desktop zone,
; neither of which SPEC.md 20.6 rule 7 lets a worker near. Every caller here
; is the painter, a click, a key or a menu pick, and the worker reaches none
; of them.
;
; **RDPV_IDENT FIRST, ALWAYS.** RD_CLASS is DRVC_FILE and NET.DRV is DRVC_FILE
; too (drivers/ramdisk/rdpkg.inc), so a class is a publication SLOT and not a
; driver: without the signature this would hand a cable redirector whatever
; verb 2 means over there and believe the answer.
; =============================================================================
wr_ramck:
    push ax
    push cx
    push dx
    push di
    push es
    mov [wr_rdmnt], al
    call wr_selrec
    mov ax, [es:si+WC_NEEDKB]
    mov [wr_needkb], ax
    mov bh, RD_CLASS
    mov bl, RDPV_IDENT
    call OSAPI_DRV_CALL
    jc .nodrv
    cmp ax, RD_SIG
    jne .nodrv
    mov bh, RD_CLASS
    mov bl, RDPV_STATE
    call OSAPI_DRV_CALL
    jc .nodrv                       ; an OLDER driver, whose RDPV_MAX is 1
    cmp al, 0xFF
    je .none
    mov bl, al                      ; --- A STORE IS MOUNTED and is REUSED:
    mov ax, [wr_needkb]             ; never resized, never unmounted. A second
    cmp cx, ax                      ; Load, a game after RunCPM, lands in the
    jae .yes                        ; same store, and what is on it is the
    sub ax, cx                      ; user's (88.14 step 2)
    mov si, wr_r_rdmore
    mov bx, wr_r_rdroom
    jmp short .why
.none:
    mov ax, [wr_needkb]             ; --- no store: the tree PLUS room for
    add ax, WR_RAMSLACK             ; what the program will save...
    call wr_ramround
    cmp dx, ax
    jae .mount
    mov ax, [wr_needkb]             ; ...and the tree alone if that will not
    call wr_ramround                ; fit (88.14 step 3)
    cmp dx, ax
    jae .mount
.short:
    mov si, wr_r_rdneed
    mov bx, wr_r_rdfree
.why:
    call wr_rreason                 ; '<head>NNNK<tail>', SI = it
    jmp short .no
.mount:
    cmp byte [wr_rdmnt], 0
    jne .doit
    mov bl, 0xFF                    ; the PREDICATE only asked, and mounting
    jmp short .yes                  ; from a painter would put a volume on the
.doit:                              ; desktop every time the window redrew
    mov bh, RD_CLASS
    mov bl, RDPV_MOUNT
    call OSAPI_DRV_CALL             ; AX = the KB, rounded already
    jc .short                       ; it refused after answering that it could:
                                    ; say the size, which is still the thing a
                                    ; person can act on
    mov bl, al
.yes:
    clc
    jmp short .out
.nodrv:
    mov si, wr_r_nord
.no:
    stc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; --- wr_ramround - AX KB up to RD_STEPKB, saturating -------------------------
; RDPV_MOUNT rounds up itself, so this is here for the FIGURE: the reason
; strings name what the user would have to make room for, and naming the
; unrounded size would name a store the driver never mounts.
wr_ramround:
    add ax, RD_STEPKB - 1
    jnc .ok
    mov ax, 0xFFFF                  ; a catalog figure that laps the word is
.ok:                                ; refused by the comparison it feeds
    and ax, ~(RD_STEPKB - 1) & 0xFFFF
    ret

; --- wr_rreason - '<head>NNNK<tail>' into wr_rmsg (SPEC.md 88.7) -------------
; in:  SI = the head, BX = the tail, AX = the KB figure; out SI = wr_rmsg
;
; **IT MAY NOT COMPOSE INTO wr_omsg.** [wr_msg] points at whichever buffer the
; last OUTCOME was written to and the status cell re-reads it on every later
; repaint, while this one is written by the PREDICATE - which runs from every
; paint. Sharing them would make the cell say why a control is greyed where it
; should still be saying what happened.
wr_rreason:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, wr_rmsg
    call wr_sput
    call wr_snum
    mov si, wr_s_k
    call wr_sput
    mov si, bx
    call wr_sput
    mov byte [di], 0
    mov si, wr_rmsg
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
