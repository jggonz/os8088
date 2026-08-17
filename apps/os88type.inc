; =============================================================================
; os88type.inc - setting proportional type in a package (SPEC.md 6.3/6.4/6.5)
;
; Include this AFTER os88api.inc and after your own code, then give it its bss
; with the TY_BSS macro, exactly as tests/benchlib.inc is given its:
;
;       %include "os88api.inc"
;           OS88_HEADER 'MYAPP', my_entry
;           ... your code ...
;       %include "os88type.inc"
;           ... your data ...
;       MY_BSS_OWN equ 40
;           OS88_BSS MY_BSS_OWN + TY_BSS_SIZE
;           OS88_IMAGE_END
;       my_var equ os88_image_end + 0
;           TY_BSS os88_image_end + MY_BSS_OWN
;
; THE METHOD, and it is the whole of SPEC.md 6.3: compose the entire row of
; glyphs into a 1bpp band in YOUR OWN RAM, then put it on the screen with ONE
; OSAPI_GFX_BLIT1. A proportional pen lands on an arbitrary bit, so a
; proportional glyph can never reach font_run's single-store path (SPEC.md
; 6.1); lettered one at a time a 104-glyph line is 104 drawing calls at
; SPEC.md 5.7's ~756us floor - 79 ms of FLOOR on the target machine before a
; pixel moves. Composed and emitted once it is one floor, and measured, the
; emit is 4.04 ms of a 25 ms line (PERFORMANCE.md Set 64).
;
; THERE IS NO LINKER (SPEC.md 1), so this is source: every including package
; carries its own copy and the kernel carries none of it.
;
; POLARITY, and it is the one bug here that looks plausible rather than like a
; crash. The band is composed in FINAL SCREEN POLARITY: a set bit is a LIT
; pixel (SPEC.md 5.4.2), so white paper is 0xFF and ink CLEARS bits. Every
; compose below is `and [di], not glyph`. Written the other way round - clear
; to the background byte, OR the ink in - it sets nothing at all against 0xFF
; and every line comes out invisible.
;
; SEGMENTS, because three are in play and only two registers hold them. The
; caller's text is in ES:SI (SPEC.md 20.3's X stubs), the band and every
; [ty_*] cell are in DS - ours - and the glyph bytes are in a third segment
; that is either a face claim or LOW_SEG. So the compose loops fetch the
; CHARACTER through the caller's ES, and only then push ES and point it at the
; glyphs for the rows; DS never moves and the band is always [di]. An earlier
; draft moved DS instead and then could not read its own [ty_rows].
;
; TWO COMPOSE PATHS, AND A CALLER DOES NOT CHOOSE BETWEEN THEM. The general
; one widens each glyph row into a 16-bit window and shifts it by the pen's
; bit offset; the fast one reads a word that was shifted and complemented
; once, at ty_cache time. They draw identical pixels and the fast one is 2.01x
; (PERFORMANCE.md Set 64). ty_putn takes it whenever the cache holds the face
; and style being drawn and the pen is even, and the general one otherwise - a
; missing cache is a SPEED difference, never a visible one.
; =============================================================================

TY_MAXFACE  equ 4               ; faces open at once, INCLUDING face 0
TY_STRIDE   equ 92              ; the band's stride in bytes, an ASSEMBLE-TIME
                                ; constant on purpose (SPEC.md 6.3): with it a
                                ; literal, a glyph's rows are displacements off
                                ; one DI and nothing advances a pointer between
                                ; them. 92 covers the widest line any adapter
                                ; shows - Hercules at 720px is 90 bytes - plus
                                ; the byte a glyph at the right-hand end spills
                                ; into, rounded up to even
TY_BROWS    equ 16              ; ...and the tallest legal face, so one band
TY_BANDSZ   equ TY_STRIDE * TY_BROWS    ; serves every one of them: 1,472 B

TY_FIRST    equ 32              ; the glyph range, and it MUST equal the
TY_LAST     equ 126             ; kernel's FONT_FIRST/FONT_LAST
TY_NGLYPH   equ TY_LAST - TY_FIRST + 1
TY_MINADV   equ 4               ; SPEC.md 6.4: the floor an application's row
                                ; buffer is sized from. A build assertion
                                ; cannot defend it - the constant is in the
                                ; tree and the face comes off a floppy - so it
                                ; is checked at open
TY_PHASES   equ 4               ; pen bit offsets a glyph can land on. FOUR and
                                ; not eight because SPEC.md 6.4 requires every
                                ; advance to be even, so a pen that starts even
                                ; stays even. That halves the table below
TY_PSSZ     equ TY_NGLYPH * TY_PHASES * TY_BROWS * 2    ; 12,160 B at 16 rows
TY_PSKB     equ (TY_PSSZ + 1023) / 1024

; --- the .F88 header, SPEC.md 6.4 -------------------------------------------
TYH_VER     equ 4
TYH_FLAGS   equ 5
TYH_ROWS    equ 6
TYH_ASCENT  equ 7
TYH_LEADING equ 8
TYH_STRIDE  equ 9
TYH_FIRST   equ 10
TYH_LAST    equ 11
TYH_MAXADV  equ 12
TYH_MINADV  equ 13
TYH_MAXOVER equ 14
TYH_ASPECT  equ 15
TYH_NAME    equ 16              ; 16 bytes, NUL-padded
TYH_BM      equ 32              ; 4 words: each style's bitmap block
TYH_MT      equ 40              ; 4 words: each style's metric block
TYH_PS      equ 48              ; 4 words: each style's PostScript name
TYH_LEN     equ 56
TYH_SUM     equ 58
TYH_SIZE    equ 64

; --- a face slot ------------------------------------------------------------
TF_CLAIM    equ 0               ; word: what OSAPI_MEM_FREE is given
TF_SEG      equ 2               ; word: the 512-ALIGNED segment the image
                                ; starts at, at offset 0 (SPEC.md 6.4/22.5)
TF_ROWS     equ 4
TF_ASC      equ 5
TF_LEAD     equ 6
TF_STR      equ 7
TF_MAXADV   equ 8
TF_MINADV   equ 9
TF_OVER     equ 10
TF_ASPECT   equ 11
TF_REF      equ 12              ; 0 = the slot is free
TF_SIZE     equ 16

TY_FACE_KB  equ 8
TY_FACE_MAX equ TY_FACE_KB * 1024

; --- what ty_open answers on CF=1 -------------------------------------------
TYE_NOSLOT  equ 1               ; every face slot is taken
TYE_NOMEM   equ 2               ; no claim for the image
TYE_READ    equ 3               ; the file would not read
TYE_MAGIC   equ 4               ; not an .F88, or not version 1
TYE_SHAPE   equ 5               ; a header field outside SPEC.md 6.4's range
TYE_SUM     equ 6               ; the checksum or the length disagrees

TY_MAXFAM   equ 10              ; families a scan will list. The Font menu is
                                ; the consumer and a menu longer than this is
                                ; a menu nobody reads - but the system disk
                                ; carries TEN faces (SPEC.md 6.4.1), and a
                                ; ceiling that silently drops the last two is
                                ; worse than a menu two rows longer. It costs
                                ; 23 bytes of the including package's bss a
                                ; family, and nothing at all in the kernel
TY_NAMSZ    equ 13              ; an 8.3 name, NUL-terminated, as
                                ; OSAPI_FILE_FIND hands it over
TY_DISPSZ   equ 10              ; ...and the display name built from its stem
TY_FIND_TY  equ 14              ; the FIND record's fields this file reads
TY_FIND_CL  equ 16

; =============================================================================
; ty_scan - what typefaces does this MACHINE have? (SPEC.md 19.8)
; in:  -
; out: [ty_nfam] families listed; CF=1 = the system disk could not be reached
;      and [ty_nfam] is 0. Preserves every register.
;
; FONTS/ on the system disk, which OSAPI_VOL_SYS is what names: a package
; stands in the folder it was launched from and nothing else told it where the
; machine's own files are. The visit is a bracket - bank OSAPI_FILE_HERE, go,
; list, come back - and coming back matters more than it looks, because every
; unqualified name the caller passes the file API afterwards resolves wherever
; this left it standing.
;
; IT IS REAL FLOPPY I/O: four remounts and two listings, a couple of seconds
; on the target machine, UI-TASK CONTEXT ONLY like every other file slot. So
; call it when something is about to USE the list - the first time a Font menu
; opens - and not at launch. A menu nobody opens should cost nothing, which is
; the same argument SPEC.md 6.2 makes about a directory of faces nobody picks
; from.
;
; What it reads is the FILE NAME and not the family inside each header: the
; name is already in the directory entry being walked, and learning the header
; name costs a read per family - a Font menu that spends eight int 13h calls
; before it can draw itself is a menu that feels broken (SPEC.md 19.8).
; =============================================================================
ty_scan:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ES:DI = our own FIND buffer throughout
    mov byte [ty_nfam], 0

    call OSAPI_FILE_HERE            ; bank where the caller was standing
    mov [ty_bkcwd], dx
    mov [ty_bkdrv], bl
    call OSAPI_VOL_SYS              ; ...and where the system disk is
    mov [ty_sysvol], bl
    xor dx, dx                      ; its ROOT
    call OSAPI_FILE_GOTO
    jc .back

    xor cx, cx                      ; --- find FONTS in the root -------------
.f1:
    mov [ty_ord], cx
    mov di, ty_find
    call OSAPI_FILE_FIND
    jc .back
    mov [ty_ord], cx
    cmp word [ty_find + TY_FIND_TY], OSAPI_FT_DIR
    jne .f1n
    mov si, ty_s_fonts
    call ty_nameq                   ; CF=0 = this is it
    jnc .have
.f1n:
    mov cx, [ty_ord]
    jmp short .f1
.have:
    mov dx, [ty_find + TY_FIND_CL]
    mov bl, [ty_sysvol]
    call OSAPI_FILE_GOTO
    jc .back

    xor cx, cx                      ; --- and list the faces inside it -------
.f2:
    mov [ty_ord], cx
    mov di, ty_find
    call OSAPI_FILE_FIND
    jc .back
    mov [ty_ord], cx
    cmp word [ty_find + TY_FIND_TY], OSAPI_FT_DIR
    jae .f2n                        ; a folder, or the synthesized '..'
    call ty_isf88
    jc .f2n
    call ty_addfam
    cmp byte [ty_nfam], TY_MAXFAM
    jae .back
.f2n:
    mov cx, [ty_ord]
    jmp short .f2

.back:
    mov dx, [ty_bkcwd]              ; ...and put the caller back where it was,
    mov bl, [ty_bkdrv]              ; on every path out of here
    call OSAPI_FILE_GOTO
    cmp byte [ty_nfam], 0
    je .none
    clc
    jmp short .out
.none:
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

; ty_nameq - is the FIND record's name SI's? in: SI; out CF=0 equal.
; Preserves every register. Case-folded, because a name off a floppy is
; whatever somebody's other machine wrote there.
ty_nameq:
    push ax
    push bx
    push si
    mov bx, ty_find
.c:
    mov al, [bx]
    or al, 0x20
    mov ah, [si]
    or ah, 0x20
    cmp al, ah
    jne .no
    cmp byte [bx], 0
    je .yes
    inc bx
    inc si
    jmp short .c
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop bx
    pop ax
    ret

; ty_isf88 - does the FIND record name a .F88? out CF=0 yes. Preserves all.
ty_isf88:
    push ax
    push bx
    mov bx, ty_find
.dot:
    mov al, [bx]
    or al, al
    jz .no
    cmp al, '.'
    je .ext
    inc bx
    jmp short .dot
.ext:
    mov al, [bx+1]
    or al, 0x20
    cmp al, 'f'
    jne .no
    cmp byte [bx+2], '8'
    jne .no
    cmp byte [bx+3], '8'
    jne .no
    cmp byte [bx+4], 0
    jne .no
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx
    pop ax
    ret

; ty_addfam - take the FIND record into the family table. Preserves all.
; The display name is the STEM, first letter upper and the rest lower, which
; is what turns CHARTER.F88 into the "Charter" a Font menu shows.
ty_addfam:
    push ax
    push bx
    push cx
    push si
    push di
    mov al, [ty_nfam]
    xor ah, ah
    mov bx, ax
    mov cl, TY_NAMSZ                ; the file name, whole
    mul cl
    mov di, ty_fnames
    add di, ax
    mov si, ty_find
    mov cx, TY_NAMSZ
.n:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .n

    mov ax, bx                      ; ...and the display name from its stem
    mov cl, TY_DISPSZ
    mul cl
    mov di, ty_dnames
    add di, ax
    mov si, ty_find
    mov cx, TY_DISPSZ - 1
    mov al, [si]
    call ty_upper
    mov [di], al
    inc si
    inc di
    dec cx
.d:
    mov al, [si]
    or al, al
    jz .end
    cmp al, '.'
    je .end
    or al, 0x20                     ; the tail is lower case
    mov [di], al
    inc si
    inc di
    loop .d
.end:
    mov byte [di], 0
    inc byte [ty_nfam]
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

ty_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; =============================================================================
; ty_famn - how many families did the scan find?  out AL, preserves the rest
; ty_famname - AL = a family index; out SI = its display name, a NUL string
; ty_openfam - AL = a family index; opens it, answering exactly as ty_open
; =============================================================================
ty_famn:
    mov al, [ty_nfam]
    ret

ty_famname:
    push ax
    push cx
    cmp al, [ty_nfam]
    jb .in
    xor al, al
.in:
    xor ah, ah
    mov cl, TY_DISPSZ
    mul cl
    mov si, ty_dnames
    add si, ax
    pop cx
    pop ax
    ret

ty_openfam:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp al, [ty_nfam]
    jb .in
    mov al, TYE_READ
    stc
    jmp .out
.in:
    xor ah, ah
    mov cl, TY_NAMSZ
    mul cl
    mov si, ty_fnames
    add si, ax

    call OSAPI_FILE_HERE            ; the same bracket ty_scan uses: a face
    mov [ty_bkcwd], dx              ; lives on the system disk and the caller
    mov [ty_bkdrv], bl              ; is standing somewhere else entirely
    call OSAPI_VOL_SYS
    mov [ty_sysvol], bl
    xor dx, dx
    call OSAPI_FILE_GOTO
    jc .fail
    push si
    xor cx, cx
    push ds
    pop es
.g:
    mov [ty_ord], cx
    mov di, ty_find
    call OSAPI_FILE_FIND
    jc .gfail
    mov [ty_ord], cx
    cmp word [ty_find + TY_FIND_TY], OSAPI_FT_DIR
    jne .gn
    mov si, ty_s_fonts
    call ty_nameq
    jnc .gin
.gn:
    mov cx, [ty_ord]
    jmp short .g
.gin:
    mov dx, [ty_find + TY_FIND_CL]
    mov bl, [ty_sysvol]
    call OSAPI_FILE_GOTO
    pop si
    jc .fail
    call ty_open                    ; CF and AL are its answer, and ours
    jmp short .home
.gfail:
    pop si
.fail:
    mov al, TYE_READ
    stc
.home:
    pushf                           ; the walk home cannot be allowed to
    push ax                         ; change the answer
    mov dx, [ty_bkcwd]
    mov bl, [ty_bkdrv]
    call OSAPI_FILE_GOTO
    pop ax
    popf
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

ty_s_fonts: db 'FONTS', 0

; =============================================================================
; ty_init - face 0, and it is always open (SPEC.md 6.5)
; in:  DS = our own segment (package entry)
; out: the face table cleared, face 0 current; preserves all registers
;
; Face 0 IS the kernel's 8x8 face: no file, 95 advances of 8, and its bitmaps
; are whatever OSAPI_FONT_GLYPHS answers - which lives in LOW_SEG and NOT in
; the kernel's own segment, so that segment comes from the call rather than
; from anything this package holds. It exists so no caller ever branches on
; "is this the built-in one": a package measures, wraps, hits and draws through
; the same calls whether or not a .F88 was found, and a missing face file
; degrades to the system face rather than to a second code path nobody runs.
; =============================================================================
ty_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, ds
    mov [ty_selfseg], ax
    push ds
    pop es
    mov di, ty_tab
    mov cx, TY_MAXFACE * TF_SIZE
    xor al, al
    cld
    rep stosb

    call OSAPI_FONT_GLYPHS          ; DX:SI = the ROM table
    mov [ty_tab + TF_SEG], dx
    mov [ty_f0off], si
    mov byte [ty_tab + TF_ROWS], 8
    mov byte [ty_tab + TF_ASC], 8
    mov byte [ty_tab + TF_STR], 1
    mov byte [ty_tab + TF_MAXADV], 8
    mov byte [ty_tab + TF_MINADV], 8
    mov byte [ty_tab + TF_REF], 1

    mov word [ty_psseg], 0
    mov byte [ty_psface], 0xFF
    mov byte [ty_psstyle], 0xFF
    mov byte [ty_ok], 1
    xor ax, ax
    call ty_use                     ; face 0, style regular

    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ty_open - read a .F88 and take a face slot (SPEC.md 6.4)
; in:  SI = a NUL 8.3 name
; out: CF=0 and AL = the handle; CF=1 and AL = TYE_*
; clobbers: AX and flags, nothing else
;
; THE READ BASE IS 512-BYTE ALIGNED BY HAND, and that is not decoration.
; SPEC.md 2.1.1 names "a package's file buffer out of the heap" among its
; int 13h targets and SPEC.md 22.5 states the consequence: mem_claim is only
; paragraph aligned, a transfer straddling a 64KB physical boundary is
; answered with error 09h, and the failure "is not a logic error, so it does
; not fail - it hangs in the BIOS". So the claim is taken with CLAIM_DMA - the
; 64KB question - and its base is then rounded up to a 32-paragraph boundary,
; which is the 512 one. Neither answers the other.
; =============================================================================
ty_open:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov di, ty_tab + TF_SIZE        ; slot 0 is face 0's and never free
    mov cx, TY_MAXFACE - 1
.slot:
    cmp byte [di + TF_REF], 0
    je .have
    add di, TF_SIZE
    loop .slot
    mov al, TYE_NOSLOT
    jmp .fail
.have:
    mov [ty_opdi], di
    mov ax, TY_FACE_KB + 1          ; +1 KB, because the hand alignment below
    mov cx, ax                      ; gives back up to 496 bytes of it
    call OSAPI_MEM_CLAIM_DMA
    jnc .got
    mov al, TYE_NOMEM
    jmp .fail
.got:
    mov di, [ty_opdi]
    mov [di + TF_CLAIM], dx
    add dx, 31                      ; round the SEGMENT to a multiple of 32
    and dx, 0xFFE0                  ; paragraphs = a 512-byte boundary
    mov [di + TF_SEG], dx

    mov es, dx
    xor bx, bx
    mov cx, TY_FACE_MAX
    xor dx, dx
    call OSAPI_FILE_READ            ; SI is still the caller's name
    jnc .read
    mov al, TYE_READ
    jmp .freefail
.read:
    or dx, dx                       ; a face is never 64KB, so a non-zero high
    jnz .badsum                     ; word is a file this is not
    mov [ty_rdlen], ax
    mov di, [ty_opdi]
    call ty_hdrchk                  ; ES = the image; CF=1 and AL = TYE_*
    jc .freefail

    mov byte [di + TF_REF], 1
    mov ax, di
    sub ax, ty_tab
    mov cl, 4                       ; / TF_SIZE
    shr ax, cl
    clc
    jmp .out
.badsum:
    mov al, TYE_SUM
.freefail:
    push ax
    mov di, [ty_opdi]
    mov dx, [di + TF_CLAIM]
    call OSAPI_MEM_FREE
    mov word [di + TF_CLAIM], 0
    mov byte [di + TF_REF], 0
    pop ax
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ty_hdrchk - is this an .F88 this library can use? (SPEC.md 6.4)
; in:  ES = the image at offset 0, DI = the slot, [ty_rdlen] = bytes read
; out: CF=0 and the slot filled in; CF=1 and AL = TYE_*
; clobbers: AX, BX, CX, DX, SI, flags
;
; EVERY BYTE READ OFF A FLOPPY IS HOSTILE (SPEC.md 19), and a face is a file a
; person can copy onto a disk by hand. So this checks the shape rather than
; trusting the tool that usually writes it: the tool is not what runs.
; -----------------------------------------------------------------------------
ty_hdrchk:
    cmp word [es:0], 'F8'
    jne .magic
    cmp word [es:2], 0x1A38         ; '8', 0x1A
    jne .magic
    cmp byte [es:TYH_VER], 1
    jne .magic

    mov al, [es:TYH_ROWS]
    cmp al, 8
    jb .shape
    cmp al, TY_BROWS
    ja .shape
    test al, 1
    jnz .shape                      ; EVEN (SPEC.md 6.4)
    mov [di + TF_ROWS], al
    mov bl, al

    mov al, [es:TYH_ASCENT]
    or al, al
    jz .shape
    cmp al, bl
    ja .shape
    mov [di + TF_ASC], al

    mov al, [es:TYH_LEADING]
    cmp al, 8
    ja .shape
    mov ah, al
    add ah, bl
    test ah, 1
    jnz .shape                      ; the ROW ADVANCE is even too
    mov [di + TF_LEAD], al

    ; STRIDE 1 ONLY, and that is this LIBRARY's limit rather than the format's
    ; (SPEC.md 6.5): a 16-pixel face is two half-cells a glyph and neither
    ; compose loop below is written for one. Refusing is right - a face drawn
    ; for a renderer that cannot set it would letter as noise.
    cmp byte [es:TYH_STRIDE], 1
    jne .shape
    mov byte [di + TF_STR], 1

    cmp byte [es:TYH_FIRST], TY_FIRST
    jne .shape
    cmp byte [es:TYH_LAST], TY_LAST
    jne .shape

    mov al, [es:TYH_MAXADV]
    or al, al
    jz .shape
    cmp al, 8
    ja .shape
    mov [di + TF_MAXADV], al
    mov ah, al
    mov al, [es:TYH_MINADV]         ; the one field an application's row
    cmp al, TY_MINADV               ; buffer is sized from
    jb .shape
    cmp al, ah
    ja .shape
    mov [di + TF_MINADV], al

    mov al, [es:TYH_MAXOVER]
    cmp al, 2
    ja .shape
    mov [di + TF_OVER], al
    mov al, [es:TYH_ASPECT]
    cmp al, 2
    ja .shape
    mov [di + TF_ASPECT], al

    mov ax, [es:TYH_BM]             ; style 0 must be present, and inside the
    or ax, ax                       ; file
    jz .shape
    cmp ax, [ty_rdlen]
    jae .shape
    mov ax, [es:TYH_MT]
    or ax, ax
    jz .shape
    cmp ax, [ty_rdlen]
    jae .shape

    mov ax, [es:TYH_LEN]            ; the length the file claims for itself...
    cmp ax, [ty_rdlen]
    jne .sum
    mov cx, ax
    sub cx, TYH_SIZE - 4
    mov si, TYH_SIZE - 4            ; ...and everything from +60, summed
    xor ax, ax
    xor bh, bh
.sum1:
    mov bl, [es:si]
    add ax, bx
    inc si
    loop .sum1
    cmp ax, [es:TYH_SUM]
    jne .sum
    clc
    ret
.magic:
    mov al, TYE_MAGIC
    stc
    ret
.shape:
    mov al, TYE_SHAPE
    stc
    ret
.sum:
    mov al, TYE_SUM
    stc
    ret

; =============================================================================
; ty_close - give a face back
; in:  AL = a handle; face 0 is ignored
; out: -; preserves all registers
; =============================================================================
ty_close:
    push ax
    push bx
    push cx
    push dx
    or al, al
    jz .out
    cmp al, TY_MAXFACE
    jae .out
    mov bl, al
    xor bh, bh
    mov cl, 4
    shl bx, cl
    add bx, ty_tab
    cmp byte [bx + TF_REF], 0
    je .out
    dec byte [bx + TF_REF]
    jnz .out
    cmp [ty_psface], al             ; the pre-shifted table was this face's
    jne .nops
    mov byte [ty_psface], 0xFF
.nops:
    mov dx, [bx + TF_CLAIM]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [bx + TF_CLAIM], 0
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ty_use - make a face and style current, and copy its metrics into DS
; in:  AL = handle, AH = style 0..3
; out: CF=1 = no such face or that style is absent, and nothing changed
; clobbers: flags
;
; THE ADVANCES ARE COPIED INTO OUR OWN SEGMENT rather than read through ES,
; which is the one thing here that is not obvious. ES is not ours to keep: a
; caller's string arrives in ES:SI and an application's row walk owns it. 95
; bytes once a style beats a segment register we do not have.
; =============================================================================
ty_use:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp al, TY_MAXFACE
    jae .no
    cmp ah, 4
    jae .no
    mov bl, al
    xor bh, bh
    mov cl, 4
    shl bx, cl
    add bx, ty_tab
    cmp byte [bx + TF_REF], 0
    je .no
    mov dx, [bx + TF_SEG]
    mov es, dx
    or al, al
    jnz .file

    ; --- face 0: the kernel's, whose header is three constants --------------
    or ah, ah
    jnz .no                         ; it carries one style, and says so
    mov si, [ty_f0off]
    mov [ty_bm], si
    mov byte [ty_rows], 8
    mov byte [ty_asc], 8
    push ds
    pop es
    mov di, ty_adv
    mov cx, TY_NGLYPH
    mov al, 8
    cld
    rep stosb
    xor al, al
    jmp .keep

.file:
    mov cl, ah                      ; the style's two blocks
    xor ch, ch
    shl cx, 1
    mov si, cx
    mov dx, [es:si + TYH_BM]
    or dx, dx
    jz .no                          ; this family does not carry that style
    mov [ty_bm], dx
    mov si, [es:si + TYH_MT]
    mov dl, [es:TYH_ROWS]
    mov [ty_rows], dl
    mov dl, [es:TYH_ASCENT]
    mov [ty_asc], dl

    mov di, ty_adv                  ; ADV[] into OUR segment, a byte at a time
    mov cx, TY_NGLYPH               ; so that neither DS nor ES has to move
.cp:
    mov dl, [es:si]
    mov [di], dl
    inc si
    inc di
    loop .cp

.keep:
    mov [ty_curface], al
    mov [ty_style], ah
    mov dx, [bx + TF_SEG]
    mov [ty_curseg], dx
    mov dl, [bx + TF_LEAD]
    mov [ty_lead], dl
    mov dl, [bx + TF_ASC]
    mov [ty_asc], dl
    call ty_setrowfn
    clc
    jmp .out
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

; ty_setrowfn - pick the unrolled row set for [ty_rows]. Preserves everything.
ty_setrowfn:
    push ax
    push bx
    mov al, [ty_rows]
    sub al, 8
    shr al, 1                       ; 8,10,12,14,16 -> 0..4
    cmp al, 4
    jbe .in
    xor al, al
.in:
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [bx + ty_rftab]
    mov [ty_rowfn], ax
    pop bx
    pop ax
    ret

; =============================================================================
; ty_cache - build the pre-shifted table for the CURRENT face and style
; in:  -
; out: CF=0 built (or already there); CF=1 no claim, and the general compose
;      is what every line will use
; clobbers: flags
;
; SPEC.md 6.5.2. 95 glyphs x 4 phases x rows, each a word already shifted into
; the pen's bit offset and already complemented, so the compose is three
; instructions with no shift in it. ~160 ms once on the target - a third of one
; int 13h - and 2.01x off every line drawn afterwards.
;
; ONE STYLE AT A TIME, and that is a decision rather than a limit arrived at.
; A caller that mixes styles inside a line - a bold run in the middle of a
; paragraph - would otherwise thrash a rebuild per run. So the table holds the
; style the caller ASKED to cache, every other style composes the general way,
; and nothing is ever rebuilt behind the caller's back.
;
; The write order is glyph-major, then phase, then row - which is exactly the
; order the index is computed in, so the whole table is one walk of `stosw`.
; =============================================================================
ty_cache:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    mov al, [ty_curface]
    cmp al, [ty_psface]
    jne .build
    mov al, [ty_style]
    cmp al, [ty_psstyle]
    jne .build
    cmp word [ty_psseg], 0
    jne .done                       ; already ours
.build:
    cmp word [ty_psseg], 0
    jne .have
    mov ax, TY_PSKB
    xor cx, cx
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [ty_psseg], dx
.have:
    mov al, [ty_curface]
    mov [ty_psface], al
    mov al, [ty_style]
    mov [ty_psstyle], al

    mov bl, [ty_rows]               ; every [ty_*] is read BEFORE DS moves
    xor bh, bh
    mov bp, [ty_bm]
    mov ax, [ty_psseg]
    mov dx, [ty_curseg]
    mov es, ax
    xor di, di
    mov ds, dx                      ; DS:SI = the face, ES:DI = the table
    cld
    mov dx, TY_NGLYPH
.g:
    xor cl, cl                      ; CL = the shift: 0, 2, 4, 6
.p:
    mov si, bp
    mov ch, bl                      ; CH = rows left
.r:
    mov al, [si]
    mov ah, al                      ; the 16-bit window, shifted into the
    xor al, al                      ; phase and complemented once and for all
    shr ax, cl
    not ax
    stosw
    inc si
    dec ch
    jnz .r
    add cl, 2
    cmp cl, TY_PHASES * 2
    jb .p
    add bp, bx                      ; the next glyph's rows
    dec dx
    jnz .g
    pop es
    pop ds
    push ds
    push es
    clc
    jmp .out
.nomem:
    stc
    jmp .out
.done:
    clc
.out:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the current face's metrics. All three preserve every other register.
; ty_getrows: out AL = cell height   ty_getasc: out AL = the baseline row
; ty_getlead: out AL = leading       ty_advof:  in AL = a char, out AL = advance
; =============================================================================
ty_getrows:
    mov al, [ty_rows]
    ret
ty_getasc:
    mov al, [ty_asc]
    ret
ty_getlead:
    mov al, [ty_lead]
    ret
ty_advof:
    push bx
    sub al, TY_FIRST
    cmp al, TY_NGLYPH
    jb .in
    xor al, al                      ; outside the table measures, and draws,
.in:                                ; as a space (SPEC.md 6)
    mov bl, al
    xor bh, bh
    mov al, [bx + ty_adv]
    pop bx
    ret

; =============================================================================
; ty_widthn - how wide is this run, in pixels?
; in:  ES:SI = the text, CX = its length
; out: AX = the width
; clobbers: AX and flags, nothing else
; =============================================================================
ty_widthn:
    push bx
    push cx
    push si
    xor ax, ax
    jcxz .out
.c:
    mov bl, [es:si]
    inc si
    sub bl, TY_FIRST
    cmp bl, TY_NGLYPH
    jb .in
    xor bl, bl
.in:
    xor bh, bh
    mov bl, [bx + ty_adv]
    xor bh, bh
    add ax, bx
    loop .c
.out:
    pop si
    pop cx
    pop bx
    ret

; ty_width - the same for a NUL-terminated string
; in: ES:SI; out AX; clobbers AX and flags
ty_width:
    push bx
    push si
    xor ax, ax
.c:
    mov bl, [es:si]
    or bl, bl
    jz .out
    inc si
    sub bl, TY_FIRST
    cmp bl, TY_NGLYPH
    jb .in
    xor bl, bl
.in:
    xor bh, bh
    mov bl, [bx + ty_adv]
    xor bh, bh
    add ax, bx
    jmp .c
.out:
    pop si
    pop bx
    ret

; =============================================================================
; ty_fit - how much of this run fits in AX pixels?
; in:  ES:SI = the text, CX = its length, AX = the pixels available
; out: CX = characters that fit, AX = their width
; clobbers: AX, CX, flags
;
; The measure a word wrap is built on. It counts CHARACTERS, never bytes of a
; shifted glyph, so a caller's own break rules sit on top of it unchanged.
; =============================================================================
ty_fit:
    push bx
    push dx
    push si
    push di
    mov dx, ax                      ; DX = the budget
    xor ax, ax                      ; AX = the width so far
    xor di, di                      ; DI = characters taken
    jcxz .out
.c:
    mov bl, [es:si]
    sub bl, TY_FIRST
    cmp bl, TY_NGLYPH
    jb .in
    xor bl, bl
.in:
    xor bh, bh
    mov bl, [bx + ty_adv]
    xor bh, bh
    add ax, bx
    cmp ax, dx
    ja .over
    inc si
    inc di
    loop .c
    jmp .out
.over:
    sub ax, bx
.out:
    mov cx, di
    pop di
    pop si
    pop dx
    pop bx
    ret

; =============================================================================
; ty_hit - which character is under this pixel offset?
; in:  ES:SI = the text, CX = its length, AX = a pixel offset into the run
; out: CX = the character index, 0..length
; clobbers: AX, CX, flags
;
; Click-to-caret, and ty_fit's mirror. A click past the MIDDLE of a glyph
; belongs to the next character, which is what makes a caret land where a
; person expects when they click the right-hand half of a letter.
; =============================================================================
ty_hit:
    push bx
    push dx
    push si
    push di
    mov dx, ax                      ; DX = the offset wanted
    xor ax, ax                      ; AX = the pen
    xor di, di                      ; DI = the index
    jcxz .out
.c:
    mov bl, [es:si]
    sub bl, TY_FIRST
    cmp bl, TY_NGLYPH
    jb .in
    xor bl, bl
.in:
    xor bh, bh
    mov bl, [bx + ty_adv]
    xor bh, bh
    push bx
    shr bx, 1
    add bx, ax
    cmp dx, bx
    pop bx
    jb .out
    add ax, bx
    inc si
    inc di
    loop .c
.out:
    mov cx, di
    pop di
    pop si
    pop dx
    pop bx
    ret

; =============================================================================
; ty_band - clear the band to paper
; in:  DX = rows
; out: -; preserves all registers
;
; PAPER IS 0xFF because a set bit is a LIT pixel (SPEC.md 5.4.2). The band is
; cleared a whole TY_STRIDE at a time rather than to any requested width, so
; the byte a glyph at the right-hand end spills into is paper and not whatever
; the last line left there.
; =============================================================================
ty_band:
    push ax
    push cx
    push dx
    push di
    push es
    push ds
    pop es
    or dx, dx
    jz .none
    cmp dx, TY_BROWS
    jbe .rows
    mov dx, TY_BROWS
.rows:
    mov ax, TY_STRIDE
    mul dx                          ; DX:AX = rows * stride, both small
    mov cx, ax
    mov di, ty_bandbuf
    mov al, 0xFF
    cld
    rep stosb
.none:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; ty_putn - set a run of text into the band
; in:  ES:SI = the text, CX = its length, AX = the pen in pixels from the
;      band's left edge
; out: AX = the pen after the run
; clobbers: AX and flags, nothing else
;
; The pen MUST be even to reach the fast path, and it stays even by itself
; because SPEC.md 6.4 requires every advance to be even. An odd pen is not
; refused - it takes the general path for the whole run and draws the same
; pixels more slowly.
; =============================================================================
ty_putn:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, ax
    jcxz .out
    test al, 1
    jnz .general
    mov bl, [ty_curface]
    cmp bl, [ty_psface]
    jne .general
    mov bl, [ty_style]
    cmp bl, [ty_psstyle]
    jne .general
    cmp word [ty_psseg], 0
    je .general
    call ty_put_fast
    jmp short .out
.general:
    call ty_put_slow
.out:
    mov ax, bp
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; ty_put - the same for a NUL-terminated string
; in: ES:SI, AX = the pen; out AX = the pen after it
ty_put:
    push cx
    push si
    xor cx, cx
.n:
    cmp byte [es:si], 0
    je .have
    inc si
    inc cx
    jmp .n
.have:
    pop si
    push si
    call ty_putn
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; ty_put_slow - the general compose: widen, shift, complement, AND
; in:  ES:SI = the text, CX = length, BP = the pen
; out: BP = the pen after the run
;
; Seven instructions a glyph row with a `shr ax,cl` an 8088 charges 8+4/bit
; for. This path always works - any pen, any height, cache or no cache - and
; it is what a missing table falls back to, at 2.01x (PERFORMANCE.md Set 64).
; -----------------------------------------------------------------------------
ty_put_slow:
    mov [ty_left], cx
.glyph:
    mov al, [es:si]                 ; the CHARACTER, through the caller's ES
    inc si
    sub al, TY_FIRST
    cmp al, TY_NGLYPH
    jb .in
    xor al, al
.in:
    mov bl, al
    xor bh, bh
    mov dl, [bx + ty_adv]
    mov [ty_curadv], dl
    mov al, [ty_rows]
    mul bl                          ; AX = rows * glyph (AL*BL, stride 1)
    add ax, [ty_bm]
    mov bx, ax                      ; BX = this glyph's first row

    mov di, bp                      ; DI = the pen's byte column in the band
    shr di, 1                       ; three single shifts: 6 clocks against
    shr di, 1                       ; the 20 an 8088 charges for a shift by CL
    shr di, 1
    add di, ty_bandbuf
    mov cx, bp
    and cl, 7                       ; CL = the pen's bit offset
    mov ch, [ty_rows]

    push es                         ; ES holds the GLYPHS for the rows; the
    mov ax, [ty_curseg]             ; band is [di] in DS and never moves
    mov es, ax
.row:
    mov al, [es:bx]
    mov ah, al
    xor al, al
    shr ax, cl
    not ax                          ; ink CLEARS bits - see the header
    and [di], ah
    and [di + 1], al
    inc bx
    add di, TY_STRIDE
    dec ch
    jnz .row
    pop es

    mov al, [ty_curadv]
    xor ah, ah
    add bp, ax
    dec word [ty_left]
    jnz .glyph
    ret

; -----------------------------------------------------------------------------
; ty_put_fast - the compose from the pre-shifted table (SPEC.md 6.5.2)
; in:  ES:SI = the text, CX = length, BP = the pen (EVEN); the table holds
;      this face and style
; out: BP = the pen after the run
;
; Three instructions a row, no shift and no pointer step: the rows are
; unrolled and TY_STRIDE is a literal, so each is a displacement off the same
; DI. The unroll is per LEGAL HEIGHT - 8, 10, 12, 14, 16, every even height
; SPEC.md 6.4 allows - and the height is dispatched once a GLYPH through
; [ty_rowfn] rather than tested once a row.
; -----------------------------------------------------------------------------
%macro TY_ROWSET 1
    %assign TYR 0
    %rep %1
        mov ax, [es:bx + TYR * 2]
        and [di + TYR * TY_STRIDE], ah
        and [di + TYR * TY_STRIDE + 1], al
        %assign TYR TYR+1
    %endrep
    ret
%endmacro

ty_rf8:     TY_ROWSET 8
ty_rf10:    TY_ROWSET 10
ty_rf12:    TY_ROWSET 12
ty_rf14:    TY_ROWSET 14
ty_rf16:    TY_ROWSET 16

ty_rftab:   dw ty_rf8, ty_rf10, ty_rf12, ty_rf14, ty_rf16

ty_put_fast:
.glyph:
    mov al, [es:si]                 ; the CHARACTER, through the caller's ES
    inc si
    sub al, TY_FIRST
    cmp al, TY_NGLYPH
    jb .in
    xor al, al
.in:
    mov bl, al
    xor bh, bh
    mov dl, [bx + ty_adv]           ; banked, because the multiply below
    mov [ty_curadv], dl             ; clobbers DX
    shl bx, 1
    shl bx, 1                       ; BX = glyph * TY_PHASES
    mov ax, bp
    and ax, 7
    shr ax, 1                       ; the pen's phase: even, so 0..3
    add bx, ax
    mov al, [ty_rows]
    xor ah, ah
    mul bx                          ; AX = index * rows
    shl ax, 1                       ; ...a WORD each
    mov bx, ax

    mov di, bp
    shr di, 1
    shr di, 1
    shr di, 1
    add di, ty_bandbuf

    push es                         ; ES holds the TABLE for the row set; the
    mov ax, [ty_psseg]              ; band is [di] in DS and never moves
    mov es, ax
    call word [ty_rowfn]
    pop es

    mov al, [ty_curadv]
    xor ah, ah
    add bp, ax
    dec cx
    jnz .glyph
    ret

; =============================================================================
; ty_flush - put the band on the screen (SPEC.md 5.4.2/6.5)
; in:  AX = screen x (a multiple of 8), BX = y, CX = width in pixels (a
;      multiple of 8), DX = rows; the gfx lock held
; out: CF=0 drawn; CF=1 the blit refused and NOTHING was drawn
; clobbers: flags
;
; TWO THINGS THIS DOES FOR THE CALLER, both because forgetting either is
; invisible until it is on a screen in front of somebody.
;
; SPEC.md 47's greying. gfx_blit1 is the one drawing primitive in the tree
; that is greying-blind: the band arrives in final polarity and there is no
; colour left to reduce. So this asks OSAPI_GFX_PEN - published so a package
; reaches [gfx_dis] the way the kernel does - and dithers the band itself when
; the flag is set. The phase is the SCREEN row's, not the band's, so a greyed
; run lines up with every other dithered thing on the screen.
;
; And the refusal. kern_small carries the slot and not the body, so CF=1 is a
; real answer on a real machine; the caller letters that row with
; OSAPI_FONT_RUN in the 8x8 face instead. It is a whole-ROW branch taken once
; per build rather than a per-glyph one, and face 0 (SPEC.md 6.5) is what
; makes the measuring side of that fallback need no second code path at all.
; =============================================================================
ty_flush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    call OSAPI_GFX_PEN              ; CF=1 = disabled (SPEC.md 47)
    jnc .live
    call ty_gray
.live:
    mov si, ty_bandbuf
    mov bp, TY_STRIDE
    push ds
    pop es
    call OSAPI_GFX_BLIT1
    mov byte [ty_ok], 1
    jnc .out
    mov byte [ty_ok], 0
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ty_gray - dither the band for a DISABLED control (SPEC.md 47 rule 3)
; in:  BX = the band's screen y, CX = its width in pixels, DX = its rows
; out: -; preserves all registers
;
; Ink is a CLEAR bit, so greying SETS half of them back: `or` a 50% pattern
; across each row. The pattern alternates by SCREEN row parity - 0xAA on even
; rows, 0x55 on odd - which is gfx_fill_gray's own phase, so a greyed run and
; a greyed rect beside it interlock instead of beating against each other.
; The band's byte 0 is at a screen x that is a multiple of 8, so no x phase
; enters it.
; -----------------------------------------------------------------------------
ty_gray:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    add cx, 7
    shr cx, 1
    shr cx, 1
    shr cx, 1                       ; CX = bytes across
    jcxz .out
    mov di, ty_bandbuf
    or dx, dx
    jz .out
.row:
    mov al, 0xAA
    test bl, 1
    jz .even
    mov al, 0x55
.even:
    push cx
    push di
.byte:
    or [di], al
    inc di
    loop .byte
    pop di
    pop cx
    add di, TY_STRIDE
    inc bx
    dec dx
    jnz .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; TY_BSS - the library's bss, placed by the including package
; =============================================================================
%macro TY_BSS 1
ty_bandbuf  equ (%1) + 0        ; the band FIRST: it is the buffer every
                                ; OSAPI_GFX_BLIT1 reads, and keeping it at the
                                ; front puts it on whatever alignment the
                                ; caller placed the block on
ty_adv      equ ty_bandbuf + TY_BANDSZ      ; 95 bytes: the current style's
                                            ; advances, copied into OUR segment
ty_tab      equ ty_adv + TY_NGLYPH          ; TY_MAXFACE face slots
ty_selfseg  equ ty_tab + TY_MAXFACE * TF_SIZE   ; word: our own segment
ty_curseg   equ ty_selfseg + 2  ; word: where the current face's glyphs are
ty_psseg    equ ty_curseg + 2   ; word: the pre-shifted table's claim, 0 = none
ty_bm       equ ty_psseg + 2    ; word: the current style's bitmap block
ty_rowfn    equ ty_bm + 2       ; word: the unrolled row set for [ty_rows]
ty_f0off    equ ty_rowfn + 2    ; word: face 0's table offset in LOW_SEG
ty_rdlen    equ ty_f0off + 2    ; word: bytes ty_open read
ty_opdi     equ ty_rdlen + 2    ; word: the slot ty_open is filling
ty_left     equ ty_opdi + 2     ; word: ty_put_slow's character countdown
ty_rows     equ ty_left + 2     ; byte: the current face's cell height
ty_asc      equ ty_rows + 1     ; byte: ...its baseline row
ty_lead     equ ty_asc + 1      ; byte: ...and its leading
ty_curface  equ ty_lead + 1     ; byte: the current handle
ty_style    equ ty_curface + 1  ; byte: ...and style
ty_psface   equ ty_style + 1    ; byte: which face the table was built for
ty_psstyle  equ ty_psface + 1   ; byte: ...and which style
ty_curadv   equ ty_psstyle + 1  ; byte: the advance, banked across a multiply
ty_ok       equ ty_curadv + 1   ; byte: the last ty_flush drew (SPEC.md 6.5)
ty_nfam     equ ty_ok + 1       ; byte: families ty_scan listed
ty_bkcwd    equ ty_nfam + 1     ; word } where the caller was standing, so a
ty_bkdrv    equ ty_bkcwd + 2    ; byte } visit to FONTS/ can put it back
ty_sysvol   equ ty_bkdrv + 1    ; byte: OSAPI_VOL_SYS's answer, banked across
                                ; the two GOTOs that both need it
ty_ord      equ ty_sysvol + 1   ; word: OSAPI_FILE_FIND's ordinal, in memory
                                ; because the helpers between two of its calls
                                ; are not all obliged to preserve CX
ty_find     equ ty_ord + 2      ; the OSAPI_FIND_SZ record
ty_fnames   equ ty_find + 24    ; TY_MAXFAM x TY_NAMSZ: the 8.3 names
ty_dnames   equ ty_fnames + TY_MAXFAM * TY_NAMSZ    ; ...and the display ones
%endmacro

; ...and its size, for the caller's OS88_BSS. The tail past the face table is
; 27 bytes of scalars; 48 is that with the slack a new one would want.
TY_BSS_SIZE equ TY_BANDSZ + TY_NGLYPH + TY_MAXFACE * TF_SIZE + 48 \
                + 24 + TY_MAXFAM * (TY_NAMSZ + TY_DISPSZ)
