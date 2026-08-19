; =============================================================================
; assoc.inc - file type associations (SPEC.md 54)
;
; An extension names a PROGRAM, and the program owns the glyph. Two tables
; rather than one flat row, because BMP and GIF both mean Paint and a flat
; table would cache Paint's glyph twice:
;
;   assoc_stem[i]   8 bytes   the app's 8.3 stem, space-padded ('.O88' implied)
;   assoc_glyph[i]  8 bytes   its icon reduced to 8x8, one byte a row
;   assoc_ext[j]    4 bytes   3 extension bytes + the app index it names
;
; Every stride is a power of two on purpose: an 8086 has no `shl reg, imm`
; past 1, so a shift through CL is far cheaper than the `mul` an odd stride
; would force - and the harvest does this lookup once per listed file.
;
; NO TABLE CARRIES A FLAGS BYTE. "Slot free" is stem[0] / ext[0] == 0 - a
; sanitized display name's bytes are 0x21..0x7E and can never be 0 (SPEC.md
; 19.1) - and "glyph unresolved" is the eight bytes ORing to zero, which is
; exactly the all-zero sentinel fm_draw_icon16 already reads on disk_icons.
;
; ALL OF IT LIVES IN .text, not .bss: -f bin zeroes nothing, so initialised
; data is in the image and is writable at runtime (the ui_tm_cwd / dsk_fatw0 /
; fdlg_win idiom). The build-time defaults ARE the table, and a runtime
; registration writes over a row - one copy of the data, no init code, and
; "take over an existing one" needs no special case.
; =============================================================================

ASSOC_NAPP   equ 12             ; app slots
ASSOC_NEXT   equ 24             ; extension slots
ASSOC_STICKY equ 0x80           ; ext slot bit 7: claimed by a RUNTIME
                                ; registration, so a header declaration
                                ; (SPEC.md 54.5) may not take it back
ASSOC_IX     equ 0x7F           ; ...and the index under it
ASSOC_DECLMAX equ 5             ; extensions one header block can declare
                                ; (SPEC.md 54.6) - the block is 16 bytes: a
                                ; count and five 3-byte extensions

; The document page: the 16x16 body every document icon is built on, and the
; second icon in this kernel that is not harvested off a disk (dsk_folder_ico
; is the other). A dog-eared page with its top-right corner cut, whose white
; interior holds an 8x8 inset at x=4..11, y=5..12 - one pixel clear of the
; border on every side. 16 mask words (the white underlay, so the page reads
; on the desktop dither) then 16 data words, bit 15 = leftmost.
assoc_page:
    dw 0x3FE0, 0x3FF0, 0x3FF8, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC
    dw 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC, 0x3FFC
    dw 0x3FE0, 0x2030, 0x2028, 0x203C, 0x2004, 0x2004, 0x2004, 0x2004
    dw 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x2004, 0x3FFC

ASSOC_INSET  equ 32 + 2*5       ; the first inset row's word in the body:
                                ; past the 16 mask words, then data row 5
ASSOC_INSHL  equ 4              ; ...and x=4 is bit 11, so a glyph byte's
                                ; bit 7 lands there with one shift by 4

; --- the app slots: stems, then glyphs -------------------------------------
assoc_stem:
    db 'PAINT   '
    db 'NOTEPAD '
    db 'TRACKER '
    db 'ARTFUL  '
    times (ASSOC_NAPP - 4) * 8 db 0

; The four defaults' glyphs are BAKED AT BUILD TIME by tools/os88mini.py out
; of each package's own .o88, so the kernel ships knowing what its own
; applications look like and a document icon costs no disk read on the first
; boot of any machine. Generated and never hand-pasted: pasted bytes go stale
; in silence when an app's icon changes, where the make dependency cannot.
assoc_glyph:
%include "associco.inc"
    times (ASSOC_NAPP - 4) * 8 db 0

; --- the extension slots ---------------------------------------------------
assoc_ext:
    db 'BMP', 0                 ; -> PAINT
    db 'GIF', 0                 ; -> PAINT
    db 'TXT', 1                 ; -> NOTEPAD
    db 'MOD', 2                 ; -> TRACKER
    db 'MD ', 3                 ; -> ARTFUL
    times (ASSOC_NEXT - 5) * 4 db 0


; --- per-app side arrays ---------------------------------------------------
; Parallel rather than inside the slot, so the 8-byte strides survive. The
; hint is a place to LOOK and never an answer: a cluster means nothing on a
; disk that has been swapped, so the open path re-checks the name on arrival.
assoc_clus:  times ASSOC_NAPP dw 0     ; the directory it was last SEEN in
assoc_drv:   times ASSOC_NAPP db 0xFF  ; ...on which volume (0xFF = never seen)
assoc_dfold: db 1, 1, 1, 1             ; build-time folder: 0 root, 1 APPS,
             times ASSOC_NAPP - 4 db 0 ; 2 GAMES. The one part of a default
                                       ; that cannot be a cluster, because no
                                       ; cluster is knowable at build time

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - but the DATA is
                                ; not, and stays in .text at every toggle here.
                                ; Nothing in this module runs faster than a
                                ; double-click, and its heaviest callees
                                ; (loader.inc, files.inc, diskw.inc) are cold
                                ; too, so those calls are NEAR from here

; -----------------------------------------------------------------------------
; assoc_ext_of - the extension of a staged display name (SPEC.md 54.2)
; in:  SI -> a NUL-padded 16-byte display name (the SPEC.md 19.1 staged shape)
; out: CF=0 and DI -> 3 space-padded extension bytes in assoc_extb;
;      CF=1 = the name carries no extension
; clobbers: DI (the output), CF
;
; Uppercase-exact throughout, matching SPEC.md 19.1's own "O88" rule and for
; the same reason: foreign OSes uppercase short names on write, and a name
; that reaches here has already been sanitized to 0x21..0x7E.
; -----------------------------------------------------------------------------
assoc_ext_of:
    push ax
    push cx
    push dx
    push si
    mov di, assoc_extb
    mov byte [di], ' '          ; a short extension pads itself, so a two-
    mov byte [di+1], ' '        ; letter 'MD' matches a declared 'MD '
    mov byte [di+2], ' '
    xor ax, ax                  ; AX = the byte after the dot, 0 = none seen
    mov cx, 16
.scan:
    mov dl, [si]                ; DL, never AH: AX is holding the offset of
    or dl, dl                   ; the dot and a byte load into AH would eat
    jz .dot                     ; its high half on the very next pass
    cmp dl, '.'
    jne .next
    mov ax, si
    inc ax
.next:
    inc si
    loop .scan
.dot:
    or ax, ax
    jz .none
    mov si, ax
    mov cx, 3
.copy:
    mov al, [si]
    or al, al
    jz .done
    mov [di], al
    inc si
    inc di
    loop .copy
.done:
    mov di, assoc_extb
    pop si
    pop dx
    pop cx
    pop ax
    clc
    ret
.none:
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; assoc_stem_of - the 8.3 STEM of a staged display name (SPEC.md 54.2)
; in:  SI -> the NUL-padded display name
; out: DI -> 8 space-padded bytes in assoc_stemb (always succeeds)
; clobbers: DI (the output), flags
; -----------------------------------------------------------------------------
assoc_stem_of:
    push ax
    push cx
    push si
    push di
    mov di, assoc_stemb
    mov cx, 8
.pad:
    mov byte [di], ' '
    inc di
    loop .pad
    mov di, assoc_stemb
    mov cx, 8
.copy:
    mov al, [si]
    or al, al
    jz .done
    cmp al, '.'
    je .done
    mov [di], al
    inc si
    inc di
    loop .copy
.done:
    pop di
    pop si
    pop cx
    pop ax
    mov di, assoc_stemb
    ret

; -----------------------------------------------------------------------------
; assoc_find - the app an extension names (SPEC.md 54.2)
; in:  SI -> 3 extension bytes
; out: CF=0 and AL = app slot index; CF=1 = no association
; clobbers: AX (output), CF
; -----------------------------------------------------------------------------
assoc_find:
    push bx
    push cx
    push di
    mov bx, assoc_ext
    mov cx, ASSOC_NEXT
.row:
    cmp byte [bx], 0
    je .next                    ; a free slot never matches
    mov di, si
    mov al, [bx]
    cmp al, [di]
    jne .next
    mov al, [bx+1]
    cmp al, [di+1]
    jne .next
    mov al, [bx+2]
    cmp al, [di+2]
    je .hit
.next:
    add bx, 4
    loop .row
.miss:
    pop di
    pop cx
    pop bx
    stc
    ret
.hit:
    mov al, [bx+3]
    and al, ASSOC_IX
    cmp al, ASSOC_NAPP
    jae .miss                   ; a corrupt index names no app
    pop di
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; assoc_app_of - the app slot a stem names
; in:  SI -> 8 stem bytes
; out: CF=0 and AL = slot index; CF=1 = this stem is in no slot
; clobbers: AX (output), CF
; -----------------------------------------------------------------------------
assoc_app_of:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, assoc_stem
    xor dh, dh
.row:
    cmp dh, ASSOC_NAPP
    jae .miss
    cmp byte [bx], 0
    je .next                    ; a free slot never matches
    mov di, bx
    mov cx, 8
    push si
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .nomatch
    inc si
    inc di
    loop .cmp
    pop si
    mov al, dh                  ; all eight matched
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.nomatch:
    pop si
.next:
    add bx, 8
    inc dh
    jmp .row
.miss:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; assoc_reduce - a 16x16 icon's DATA plane -> the 8x8 glyph (SPEC.md 54.3)
; in:  ES:SI -> 16 data words (bit 15 = leftmost), DI -> 8 bytes in the
;      kernel segment
; out: nothing (all registers preserved)
;
; MAJORITY of each 2x2 block: it lights if two or more of its four source
; pixels are ink. OR-of-4 turns a typical 40%-ink icon into a near-solid blob
; and point-sampling drops every one-pixel stroke; majority is the one that
; keeps a silhouette at 8x8 on a 1bpp adapter. Byte-identical to reduce8() in
; tools/os88mini.py, which bakes the shipped glyphs - the two MUST agree, or a
; default and a harvested icon for the same program would not match.
;
; ES rather than DS because the source is usually LOW_SEG (the harvest's
; sector buffer), which SPEC.md 2.1 forbids reaching through DS.
; -----------------------------------------------------------------------------
assoc_reduce:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ch, 8                   ; output rows left
.row:
    mov ax, [es:si]             ; the two source rows this one reduces
    mov dx, [es:si+2]
    xor bl, bl                  ; the output byte, built MSB first
    mov cl, 8                   ; output columns left
.col:
    shl bl, 1
    xor bh, bh                  ; how many of the four are ink
    shl ax, 1
    adc bh, 0
    shl ax, 1
    adc bh, 0
    shl dx, 1
    adc bh, 0
    shl dx, 1
    adc bh, 0
    cmp bh, 2
    jb .dark
    or bl, 1
.dark:
    dec cl
    jnz .col
    mov [di], bl
    inc di
    add si, 4                   ; two source rows per output row
    dec ch
    jnz .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_compose - a document icon for one app slot (SPEC.md 54.3)
; in:  AL = app slot index, DI -> 64 bytes in the kernel segment
; out: the composed 16x16 body (icon_draw16's layout)
; clobbers: nothing (all registers preserved)
;
; The page frame, then the app's glyph OR'd into the interior the frame has
; already cleared to white. An UNRESOLVED glyph is eight zero bytes and
; composes to the bare page, which is a correct document icon and not a
; placeholder - the SPEC.md 50 degradation rule, applied to an icon.
; -----------------------------------------------------------------------------
assoc_compose:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es                      ; ES = DS = KERNEL_SEG for the frame copy
    mov bl, al
    xor bh, bh
    mov cl, 3
    shl bx, cl                  ; slot * 8
    push di
    mov si, assoc_page
    mov cx, 32
    cld
    rep movsw
    pop di
    mov si, assoc_glyph
    add si, bx
    add di, ASSOC_INSET
    mov cx, 8
.inset:
    mov al, [si]
    xor ah, ah
    push cx
    mov cl, ASSOC_INSHL
    shl ax, cl
    pop cx
    or [di], ax
    inc si
    inc di
    inc di
    loop .inset
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_docicon - compose a document icon for one directory entry
; in:  SI -> a staged 32-byte entry (SPEC.md 19.1)
; out: CF=0 and dsk_ico holds the 64-byte body; CF=1 = no association
; clobbers: CF (everything else preserved)
;
; dsk_ico is dsk_get_icon's staging buffer, borrowed here to save 64 bytes of
; footprint. Safe because nothing calls dsk_get_icon during a mount, which is
; the only place this runs - state the invariant if either ever moves.
; -----------------------------------------------------------------------------
assoc_docicon_x:
    push ax
    push si
    push di
    call assoc_ext_of           ; DI -> assoc_extb
    jc .no
    mov si, di
    call assoc_find             ; AL = the app slot
    jc .no
    mov di, dsk_ico
    call assoc_compose
    pop di
    pop si
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; assoc_declare - take the extensions a package's header declares (SPEC.md 54.6)
; in:  AL = its app slot; ES = LOW_SEG with dsk_secbuf holding its first
;      sector, header validated as a v3 .o88 whose flags bit 1 is set
; out: nothing (all registers preserved)
;
; The block is a count byte and up to five 3-byte extensions, and the harvest
; ALREADY READ this sector - so a folder holding a program and its documents
; teaches the OS the extension, the glyph and the location in one existing
; read. The program still has to be SEEN; it no longer has to be run.
;
; A declaration may NOT take an extension a runtime registration claimed
; (ASSOC_STICKY). It arrives on every mount of the folder, so without that
; guard it would silently undo the user's own choice every time they browsed.
; -----------------------------------------------------------------------------
assoc_declare:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dl, al                  ; DL = the app slot
    mov si, dsk_secbuf + 32     ; the block: after the header, and after the
    test byte [assoc_hflags], 1 ; icon when there is one
    jz .based
    add si, 64
.based:
    mov cl, [es:si]             ; the count
    cmp cl, ASSOC_DECLMAX
    ja .out                     ; a corrupt count declares nothing
    or cl, cl
    jz .out
    xor ch, ch
    inc si
.one:
    push cx
    mov di, assoc_extb          ; stage it: everything below reads through DS
    mov al, [es:si]
    mov [di], al
    mov al, [es:si+1]
    mov [di+1], al
    mov al, [es:si+2]
    mov [di+2], al
    add si, 3
    push si
    mov si, assoc_extb
    cmp byte [si], ' '          ; a blank extension names nothing
    jbe .next
    call assoc_find             ; already claimed?
    jc .fresh
    call assoc_ext_row          ; BX = that row
    test byte [bx+3], ASSOC_STICKY
    jnz .next                   ; a runtime claim outranks a declaration
    jmp short .point
.fresh:
    call assoc_ext_new
    jc .next                    ; the table is full: the rest are dropped
.point:
    mov [bx+3], dl              ; ...pointed here, sticky bit CLEAR
.next:
    pop si
    pop cx
    loop .one
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_note_app - record where a program is, and what it looks like
; in:  SI -> its staged 32-byte entry; ES = LOW_SEG with dsk_secbuf holding
;      its first sector, header validated as a v3 .o88 WITH an icon
; out: nothing (all registers preserved)
;
; Called from the harvest for every package it reads, so browsing the folder
; a program lives in teaches the OS its location and its glyph at no extra
; I/O - the read already happened.
; -----------------------------------------------------------------------------
assoc_note_app_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call assoc_stem_of          ; DI -> assoc_stemb; SI untouched
    mov dx, si                  ; bank the entry
    mov si, di
    call assoc_app_of
    mov si, dx
    jnc .haveslot
    test byte [assoc_hflags], 2 ; no slot yet - but a package that DECLARES
    jz .out                     ; extensions is asking for one (SPEC.md 54.6)
    call assoc_app_new          ; (assoc_stemb still holds its stem)
    jc .out                     ; the table is full: nothing to record
.haveslot:
    push ax
    test byte [assoc_hflags], 2
    jz .nodecl
    call assoc_declare          ; AL = the slot
.nodecl:
    pop ax
    mov bl, al
    xor bh, bh
    mov cl, [disk_drive]
    mov [assoc_drv+bx], cl      ; where it was seen...
    push bx
    shl bx, 1
    mov ax, [dsk_cwd]
    mov [assoc_clus+bx], ax
    pop bx
    test byte [assoc_hflags], 1 ; ...and what it looks like, IF it ships an
    jz .out                     ; icon at all - an iconless package leaves
    mov cl, 3                   ; the glyph unresolved and its documents draw
    shl bx, cl                  ; the bare page, which is honest
    mov di, assoc_glyph
    add di, bx
    mov si, dsk_secbuf+64       ; the DATA plane of the embedded icon
    call assoc_reduce           ; ES = LOW_SEG, per SPEC.md 2.1
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

section .bss
assoc_hflags: resb 1            ; the header flags of the package the harvest
                                ; is looking at, banked because dsk_get_dir
                                ; re-stages the directory entry over dsk_ent
assoc_extb:  resb 3             ; one staged extension (assoc_ext_of)
assoc_stemb: resb 8             ; one staged stem (assoc_stem_of)
section .text

; The pending document open. In .text with a real initialiser, not .bss: -f
; bin zeroes nothing and ui.inc reads this on the machine's very first idle
; pass, before anything has written it (the fdlg_win precedent).
assoc_dapp:  db 0xFF            ; the app slot to open it with, 0xFF = none
assoc_argp:  db 0               ; 1 = a document is waiting to be claimed by
                                ; the next OSAPI_ARG_FILE (SPEC.md 54.5)

section .cold                   ; ...and back to cold code

; =============================================================================
; The open path (SPEC.md 54.4) - double-clicking a document
; =============================================================================

; -----------------------------------------------------------------------------
; assoc_post - a document double-click, from under the gfx lock
; in:  SI -> the staged type-0 entry, DI = the poster's state block
; out: CF=0 = posted (ui.inc will run it once the lock drops); CF=1 = this
;      extension has no association, so the caller does what it always did
; clobbers: CF (everything else preserved)
;
; The document's LOCATION is deliberately not banked here. [dsk_cwd] is one
; global shared by every Disk window, and the window that posted this may not
; be the one currently mounted - so assoc_run reads the poster's own
; FS_DRV/FS_CWD instead (SPEC.md 54.9.1). That used to be a fmv_sync, a FULL
; mount, and all it was ever wanted for was those two words.
; -----------------------------------------------------------------------------
assoc_post_x:
    push ax
    push cx
    push si
    push di
    push si                     ; the entry, for the name copy below
    call assoc_ext_of
    jc .no
    push si
    mov si, di
    call assoc_find             ; AL = the app slot
    pop si
    jc .no
    mov [assoc_dapp], al
    pop si                      ; the entry again
    push si
    mov di, assoc_doc           ; stage the document's own name
    mov cx, 12
.copy:
    mov al, [si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.named:
    pop si
    pop di
    mov [assoc_pwin], di
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop si
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; assoc_fname - build '<STEM>.O88' for the app slot in AL
; in:  AL = app slot
; out: assoc_fnb holds the NUL name; all registers preserved
; -----------------------------------------------------------------------------
assoc_fname:
    push ax
    push bx
    push cx
    push si
    push di
    mov bl, al
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov si, assoc_stem
    add si, bx
    mov di, assoc_fnb
    mov cx, 8
.stem:
    mov al, [si]
    cmp al, ' '                 ; the stem is space-padded; the name is not
    je .ext
    mov [di], al
    inc si
    inc di
    loop .stem
.ext:
    mov word [di], '.O'
    mov word [di+2], '88'
    mov byte [di+4], 0
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_try - is the program in THIS directory? (one rung of SPEC.md 54.4)
; in:  DL = volume, AX = directory cluster (0 = the root)
; out: CF=0 and AX = its directory index, with that volume+directory CURRENT;
;      CF=1 = not here (and the mount may have failed, which is the same
;      answer to the caller)
; clobbers: AX (the output), CF
;
; The name is re-checked on arrival every single time, which is what makes a
; cluster HINT safe: a cluster means nothing on a disk that has been swapped,
; so the hint is only ever a place to look.
; -----------------------------------------------------------------------------
assoc_try:
    push ax
    push bx
    push cx
    push dx
    push si
    call dsk_chdir_q_x            ; QUIET (SPEC.md 18.9): the question here is
    jc .no                      ; whether a FILE is in this directory, and
                                ; dskw_stat walks the directory sectors to
                                ; answer it. A listing would be a scan, a sort
                                ; and one icon-harvest read per file, bought
                                ; entirely for an INDEX nobody needs now that
                                ; the loader takes a name (SPEC.md 21.4)
    mov si, assoc_fnb
    call dskw_stat_x              ; ...and since 18.4.3 it costs no re-read
    jc .no
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; assoc_tryfold - the same, one folder down from a volume's root
; in:  DL = volume, BL = 1 = APPS, 2 = GAMES (0 = nothing to try)
; out: as assoc_try
; -----------------------------------------------------------------------------
assoc_tryfold:
    push si
    push di
    push cx
    push dx
    or bl, bl
    jz .no
    mov si, assoc_s_apps
    cmp bl, 1
    je .named
    mov si, assoc_s_games
.named:
    push si
    xor ax, ax                  ; the volume's root first
    call dsk_chdir_q_x            ; quiet, for assoc_try's reason
    pop si
    jc .no
    call dskw_stat_x              ; AL = attribute, BX = the FOLDER's own first
    jc .no                      ; cluster - no listing, no index and no staged
                                ; entry, where this used to want all three
    test al, 0x10               ; ...and it must actually be a folder
    jz .no
    mov ax, bx
    pop dx                      ; the volume, which dskw_stat clobbered (it
    push dx                     ; answers DX:CX = size) - peeked back off our
                                ; own frame rather than kept in a spare
                                ; register, because there is not one
    call assoc_try
    jc .no
    pop dx
    pop cx
    pop di
    pop si
    clc
    ret
.no:
    pop dx
    pop cx
    pop di
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; assoc_tryvol - one whole volume: its root, then the folder a default names
;                (rungs 3 and 4 of SPEC.md 54.4.2)
; in:  DL = volume, DI = the app slot
; out: as assoc_try
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
assoc_tryvol:
    push bx
    push dx
    xor ax, ax
    call assoc_try              ; the volume's root
    jnc .out
    mov bx, di                  ; ...then APPS or GAMES, if a default names
    mov bl, [assoc_dfold+bx]    ; one for this program (0 = nothing to try)
    call assoc_tryfold
.out:
    pop dx                      ; a pop touches no flag, so the CF both rungs
    pop bx                      ; answer in is what reaches the caller
    ret

; -----------------------------------------------------------------------------
; assoc_locate - find the program, four rungs (SPEC.md 54.4.2)
; in:  AL = app slot; [assoc_ddrv]/[assoc_dclus] = where the document lives
; out: CF=0 = found, with its volume+directory CURRENT; CF=1 = not found on
;      any volume that would mount
; clobbers: AX, CF
;
; AX is NOT an output. It used to be documented as the program's directory
; index and never was one - assoc_try restores AX across its own frame, and
; SPEC.md 21.4 removed the last consumer when ld_run_name started resolving
; by name - which is what frees .found to spend it on the hint.
; -----------------------------------------------------------------------------
assoc_locate:
    push bx
    push cx
    push dx
    push si
    push di
    call assoc_fname            ; assoc_fnb = '<STEM>.O88'
    mov bl, al
    xor bh, bh
    mov di, bx                  ; DI = the slot, wanted after the shifts

    ; --- rung 1: the hint, if this program has ever been seen -------------
    mov dl, [assoc_drv+bx]
    cmp dl, 0xFF
    je .rung2
    shl bx, 1
    mov ax, [assoc_clus+bx]
    call assoc_try
    jnc .found

    ; --- rung 2: the document's own directory -----------------------------
.rung2:
    mov dl, [assoc_ddrv]
    mov ax, [assoc_dclus]
    call assoc_try
    jnc .found

    ; --- rungs 3 and 4: the root, then the folder a default names, on EVERY
    ; LIVE VOLUME - the document's own first (SPEC.md 54.4.2). This was the
    ; document's volume and then `xor dl, 1`, "A: <-> B:, the only other
    ; floppy there is" - true when it was written and false since SPEC.md 52,
    ; where [disk_drive] is a VOLUME INDEX and a disk driver mounts one per
    ; partition. A document on E: swept volumes 4 and 5 and never looked at
    ; the floppies at all, which is where every shipped program lives.
    mov dl, [assoc_ddrv]
    call assoc_tryvol           ; its own volume first: already mounted, and
    jnc .found                  ; skipped below so nothing is visited twice
    xor cl, cl                  ; CL = the volume index of the sweep
.vol:
    mov bl, cl
    cmp bl, [assoc_ddrv]
    je .volnext
    call dsk_vol_row_x            ; a FREE row is skipped for the TIME, not for
    jc .volnext                 ; correctness: mounting one fails anyway, but
                                ; that failure is the BIOS's retries and on a
                                ; 4.77MHz machine those are seconds
    mov dl, cl
    call assoc_tryvol
    jnc .found
.volnext:
    inc cl
    cmp cl, DVOL_MAX
    jb .vol
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
.found:
    call asc_use_x                ; the volume that ANSWERED (SPEC.md 54.7.2):
                                ; seed EVERY association on it, not just the
                                ; one we came for. assoc_try navigates with
                                ; dsk_chdir_q and asc_use hangs off the
                                ; harvest, which a quiet mount skips - so
                                ; nothing else on this path would ever load
                                ; this disk's ASSOC.DAT, and the next document
                                ; of a DIFFERENT type swept all over again.
                                ; ~2 int 13h calls, once per volume: the
                                ; asc_vol stamp makes every later call a
                                ; compare. Safe from a subdirectory (we are
                                ; standing in B:\APPS) because asc_root_find
                                ; walks the ROOT directly rather than through
                                ; dskw_stat, which is what it was written for
    mov bx, di                  ; ...then the AUTHORITATIVE hint for the one
    mov al, [disk_drive]        ; program we actually located - AFTER the seed
    mov [assoc_drv+bx], al      ; on purpose, so a stale cached cluster for
    shl bx, 1                   ; THIS program loses to where it really is
    mov ax, [dsk_cwd]
    mov [assoc_clus+bx], ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc                         ; explicit, not inherited: a slot is 0..11 so
    ret                         ; the shl above happens to clear CF, and that
                                ; is an accident of the table's size

; -----------------------------------------------------------------------------
; assoc_run - open the pending document (SPEC.md 54.4)
; in:  [assoc_dapp] != 0xFF; UI task, NO lock held
; out: nothing (all registers preserved)
;
; Structured like loader_run and ui_tm_open together: bank where the document
; lives, go and find the program, load it, and put the volume back where the
; user was. Where it lives comes off the POSTER's state block and not from a
; mount (SPEC.md 54.9.1). The document is handed to the package by PULL - it
; asks with OSAPI_ARG_FILE - so nothing here has to be standing in the
; document's directory when the entry proc runs.
; -----------------------------------------------------------------------------
assoc_run_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

    mov al, [assoc_dapp]
    mov byte [assoc_dapp], 0xFF ; consume before running, not after
    push ax

    mov di, [assoc_pwin]
    or di, di
    jz .curvol
    mov al, [di+FS_DRV]         ; the poster's OWN folder, read straight off
    mov [assoc_ddrv], al        ; its state block. This was a fmv_sync - a
    mov ax, [di+FS_CWD]         ; FULL mount - and all it was ever wanted for
    mov [assoc_dclus], ax       ; was these two words, which fmv_sync itself
    jmp short .located          ; takes from exactly here (SPEC.md 54.9.1).
                                ; Nothing downstream needs the globals to be
                                ; this folder: assoc_locate navigates itself,
                                ; assoc_back comes back to what is banked
                                ; here, and files_refresh paints from the
                                ; WINDOW's cache (SPEC.md 22.1)
.curvol:
    mov ax, [dsk_cwd]           ; no poster - a package's own request - so the
    mov [assoc_dclus], ax       ; volume the machine is on IS the home
    mov al, [disk_drive]
    mov [assoc_ddrv], al
.located:

    pop ax
    push ax
    call assoc_locate           ; AX = the program's index, and we are in its
    jc .nofile                  ; directory

    mov bx, [assoc_pwin]
    mov [ld_pwin], bx
    mov byte [ld_painted], 0
    mov byte [assoc_argp], 1    ; armed ACROSS the load and disarmed after,
    mov si, assoc_fnb           ; '<STEM>.O88' - assoc_locate left us in its
    call ld_run_name_x            ; directory and the name is what it matched
                                ; on, so no listing and no index (21.4)
    mov byte [assoc_argp], 0    ; a package launched later, by any route,
    mov [ld_status], al         ; must never inherit this document
    push bx                     ; ...and SAY it (SPEC.md 59/22.9)
    push cx
    mov bx, fm_stattab
    mov cx, LD_NSTAT
    call KERNEL_SEG:cw_toast_say
    pop cx
    pop bx
    push ds
    pop es                      ; ld_run_body's failure exits leave ES adrift
    call assoc_back             ; the user's volume FIRST, then the window
    cmp byte [ld_painted], 0
    jne .out                    ; ld_run_body's wm_show drew the new window
                                ; and the poster's own window was never
                                ; written on by this load at all (SPEC.md
                                ; 22.17), so nothing here is owed a repaint
    call files_refresh_x
    jmp short .out

.nofile:
    call assoc_back
    pop ax                      ; the app index, back off the stack and into
    push ax                     ; 'TRACKER.O88 - not on this disk', because
    call assoc_progname         ; "Program not found" alone leaves the user
    mov si, assoc_pnbuf         ; with no way to know WHICH program a document
    mov bx, assoc_s_lead        ; wanted - which is exactly the report that
    mov di, assoc_s_ttl         ; found this (SPEC.md 54.4.1)
    call KERNEL_SEG:cw_ui_note
    call files_refresh_x
.out:
    pop ax
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; assoc_back - put the user's volume back where the document is. A failed
; remount leaves that volume mount-failed at its root with the write gate
; shut, which is survivable and is exactly ui_tm_back's contract.
assoc_back:
    push ax
    push dx
    mov ax, [assoc_dclus]
    mov dl, [assoc_ddrv]
    call dsk_chdir_q_x            ; QUIET (SPEC.md 18.9/54.9): this restores the
                                ; volume so the app can READ ITS DOCUMENT, and
                                ; a read resolves by NAME through dskw_find's
                                ; directory walk - it never touches the global
                                ; listing. The scan, the sort and one icon
                                ; harvest per file were bought for nothing.
    pop dx
    pop ax
    ret

section .text                   ; DATA, so back to the kernel segment: cold
                                ; code addresses its data through DS, which is
                                ; still KERNEL_SEG (SPEC.md 2.6 rule 1)
assoc_s_apps:   db 'APPS', 0
assoc_s_games:  db 'GAMES', 0
assoc_s_ttl:    db 'Open', 0
assoc_s_lead:   db 'This document opens with', 0
assoc_s_ptail:  db '.O88 - not on this disk', 0
ASSOC_PTAIL     equ 24          ; ...NUL included, so one rep movsb ends it

section .cold                   ; ...and back to cold code

; -----------------------------------------------------------------------------
; assoc_progname - the file name of an app slot, for the notice
; in:  AL = the app index
; out: assoc_pnbuf = 'STEM.O88 - not on this disk', NUL
; clobbers: nothing (all registers preserved)
;
; The stem is space-padded to 8 (SPEC.md 54.2), so the copy stops at the
; first space rather than counting - a stem cannot contain one.
; -----------------------------------------------------------------------------
assoc_progname:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es                      ; the tail lands through ES:DI
    xor ah, ah
    mov cl, 3
    shl ax, cl                  ; the slot's 8-byte stride
    mov si, assoc_stem
    add si, ax
    mov di, assoc_pnbuf
    mov cx, 8
.stem:
    mov al, [si]
    cmp al, ' '
    je .tail
    mov [di], al
    inc si
    inc di
    loop .stem
.tail:
    mov si, assoc_s_ptail
    mov cx, ASSOC_PTAIL
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

section .text                   ; DATA, so back to the kernel segment
assoc_pnbuf:    times 8 + ASSOC_PTAIL db 0

section .bss
assoc_doc:   resb 13            ; the pending document's 8.3 name
assoc_fnb:   resb 13            ; '<STEM>.O88', built by assoc_fname
assoc_dclus: resw 1             ; ...and where it lives: directory cluster
assoc_ddrv:  resb 1             ; ...and volume
assoc_pwin:  resw 1             ; the posting Disk window's STATE BLOCK, not
                                ; its window record - FS_DRV/FS_CWD are read
                                ; off it (SPEC.md 54.9.1), as ld_pwin is
section .cold                   ; ...and back to cold code

; =============================================================================
; The API surface (SPEC.md 54.5)
; =============================================================================

; -----------------------------------------------------------------------------
; osapi_arg_file - the document this instance was launched to open (0x02E8)
; in:  nothing
; out: CF=1 = no document; CF=0 with SI -> its NUL 8.3 name IN THE KERNEL
;      SEGMENT (ES is already KERNEL_SEG on entry to a package proc, SPEC.md
;      20.2), DX = the directory cluster it lives in and BL = its volume -
;      the same pair OSAPI_FILE_HERE answers and OSAPI_FILE_GOTO takes
; clobbers: SI, DX, BL (the outputs), CF
;
; READ-AND-CLEAR, and that is the whole of why a second instance cannot
; inherit a document: the first proc to ask gets it and the word is spent.
;
; PULL rather than push, because ld_run_body reads the program's image out of
; the PROGRAM's directory and far-calls its entry as one unit - so the kernel
; cannot be standing in the document's directory when that entry runs. Handing
; over the locator instead moves the one dsk_chdir to where it can happen, and
; the app spends it with slots it already had.
; -----------------------------------------------------------------------------
osapi_arg_file_x:
    cmp byte [assoc_argp], 0
    je .none
    mov byte [assoc_argp], 0    ; spent
    mov si, assoc_doc
    mov dx, [assoc_dclus]
    mov bl, [assoc_ddrv]
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; osapi_assoc_set - claim an extension for a program (0x02F0, an X stub)
; in:  ES:SI -> 3 extension bytes then 8 stem bytes, both space-padded
; out: CF=1 = the tables are full and nothing was stored
; clobbers: CF
;
; The caller supplies its own file stem because the kernel cannot: I_NAME
; holds the 16-byte HEADER name ('SOLITAIRE'), not the 8.3 file name
; ('SOLITAIR'), and nothing in the instance record says where a package was
; loaded from. OS88_ASSOC in the SDK makes it one line.
;
; There is deliberately NO ownership model - "take over an existing one" was
; the ask, so a matching extension is repointed. Registering grants nothing:
; the worst outcome is that the wrong program opens a file.
;
; It sets the STICKY bit, which is what stops a package's header declaration
; (SPEC.md 54.6) quietly taking the extension back on the next mount of its
; folder.
; -----------------------------------------------------------------------------
osapi_assoc_set_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, assoc_extb          ; stage both halves into the kernel, the
    mov cx, 3                   ; dsk_get_dir idiom - everything below reads
.cext:                          ; through DS
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cext
    mov di, assoc_stemb
    mov cx, 8
.cstem:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cstem

    mov si, assoc_stemb         ; the app slot: reuse this program's own if
    call assoc_app_of           ; it already has one, so a program claiming
    jnc .haveapp                ; four extensions takes one slot and not four
    call assoc_app_new
    jc .full
.haveapp:
    mov dl, al                  ; DL = the app slot from here down
    mov si, assoc_extb
    call assoc_find             ; already claimed by someone?
    jc .newext
    call assoc_ext_row          ; BX = that row
    jmp short .point
.newext:
    call assoc_ext_new          ; BX = a free row, with the extension in it
    jc .full
.point:
    mov al, dl
    or al, ASSOC_STICKY         ; a runtime claim outranks a declaration
    mov [bx+3], al
    call assoc_self_glyph       ; DL = slot; take the caller's own icon
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.full:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; assoc_ext_row - the row assoc_find just matched. in: SI -> 3 ext bytes;
; out: BX = its row (the caller has already had CF=0 from assoc_find)
assoc_ext_row:
    push ax
    push cx
    push di
    mov bx, assoc_ext
    mov cx, ASSOC_NEXT
.row:
    mov di, si
    mov al, [bx]
    cmp al, [di]
    jne .next
    mov al, [bx+1]
    cmp al, [di+1]
    jne .next
    mov al, [bx+2]
    cmp al, [di+2]
    je .out
.next:
    add bx, 4
    loop .row
.out:
    pop di
    pop cx
    pop ax
    ret

; assoc_ext_new - claim a free extension row. in: SI -> 3 ext bytes;
; out: CF=0 and BX = the row with the extension written in; CF=1 = full
assoc_ext_new:
    push ax
    push cx
    mov bx, assoc_ext
    mov cx, ASSOC_NEXT
.row:
    cmp byte [bx], 0
    je .take
    add bx, 4
    loop .row
    pop cx
    pop ax
    stc
    ret
.take:
    mov al, [si]
    mov [bx], al
    mov al, [si+1]
    mov [bx+1], al
    mov al, [si+2]
    mov [bx+2], al
    pop cx
    pop ax
    clc
    ret

; assoc_app_new - claim a free app slot for the stem in assoc_stemb.
; out: CF=0 and AL = the slot, its stem written and its glyph cleared;
;      CF=1 = every slot is taken
assoc_app_new:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, assoc_stem
    xor dh, dh
.row:
    cmp dh, ASSOC_NAPP
    jae .full
    cmp byte [bx], 0
    je .take
    add bx, 8
    inc dh
    jmp .row
.take:
    mov si, assoc_stemb
    mov di, bx
    mov cx, 8
.copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .copy
    mov bl, dh                  ; clear the side state: a slot reused for a
    xor bh, bh                  ; different program must not inherit the
    mov byte [assoc_drv+bx], 0xFF   ; last one's hint, folder or glyph
    mov byte [assoc_dfold+bx], 0
    push bx
    shl bx, 1
    mov word [assoc_clus+bx], 0
    pop bx
    mov cl, 3
    shl bx, cl
    mov di, assoc_glyph
    add di, bx
    mov cx, 8
.zap:
    mov byte [di], 0
    inc di
    loop .zap
    mov al, dh
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.full:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret

; assoc_self_glyph - reduce the CALLING package's own embedded icon into an
; app slot. in: DL = the slot, ES = the caller's segment (the X stub put it
; there). Does nothing if the caller ships no icon - the document then draws
; the bare page, which is honest.
assoc_self_glyph:
    push ax
    push bx
    push cx
    push si
    push di
    cmp word [es:0], 0x384F     ; 'O','8' - a package header, not a stray
    jne .out
    test byte [es:3], 1         ; the embedded-icon flag (SPEC.md 20.2)
    jz .out
    mov bl, dl
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov di, assoc_glyph
    add di, bx
    mov si, 64                  ; the DATA plane of the embedded icon
    call assoc_reduce
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; ASSOC.DAT - the volume's icon and association cache (SPEC.md 54.7)
;
; The mount reads the first sector of EVERY package in a directory to harvest
; its icon, and mechanism D of docs/DISK-PERF-PLAN.md is the measurement that
; makes this worth having: opening APPS/ costs 20 sectors, EIGHT of which are
; those reads, and it is paid again on every re-entry. Sixteen rows of 80
; bytes is ONE file - three sectors, read once per volume per session - in
; place of one scattered read per package per mount.
;
; ONE SHARED BUFFER, not one per volume. DVOL_MAX is 6, so per-volume would be
; up to 12KB of heap and on the 128KB floor that is over a fifth of everything
; above the kernel. SPEC.md 18.8.1 made the same call for the FAT window and
; escalated to per-volume for a stated reason - a COPY alternates between two
; volumes - and that reason does not transfer: a copy never consults this
; cache, because dskw_find and fcp_scan walk directory sectors themselves and
; dsk_chdir_q skips the harvest outright (18.9). It is read only on a FULL
; mount, which is a user navigating.
;
; Three tiers, each degrading into the next (the SPEC.md 50 rule): the claim
; held and holding this volume = 0 reads; a REFUSED claim = the old behaviour,
; because a miss is what an empty cache answers to everything; no ASSOC.DAT at
; all = likewise. Nothing here can fail in a way the harvest notices.
; =============================================================================

ASC_HDR      equ 16             ; magic 6 + version + napp + next + 7 reserved
ASC_ROW      equ 80             ; stem 8 + size 2 + cluster 2 + 4 reserved + a
                                ; 64-byte icon
ASC_ROWCLUS  equ 10             ; the DIRECTORY the program lives in (0 = root),
                                ; taken out of the six bytes the row already
                                ; reserved - so SPEC.md 54.7.1's seeding costs
                                ; the file not one byte
ASC_ROWICO   equ 16             ; ...the icon's offset inside a row
ASC_NAPP     equ 32             ; was 16, and the shipped apps disk holds 15 -
ASC_NEXT     equ 24             ; one package of headroom, with no guard
ASC_KB       equ 3              ; 32*80 + 16 + 24*4 = 2,672 bytes

; -----------------------------------------------------------------------------
; asc_use - make the cache describe THIS volume (SPEC.md 54.7)
; in:  [disk_drive] = the volume being mounted; the BPB and FAT window are
;      loaded, so the root is readable
; out: nothing (all registers preserved). [asc_n] = 0 means "no cache", which
;      every lookup below answers as a miss
;
; Called once at the top of the harvest. A volume already loaded costs one
; compare; anything else reloads, and a failure is REMEMBERED (the volume is
; still stamped) so a disk with no ASSOC.DAT is not searched again on every
; directory change.
; -----------------------------------------------------------------------------
asc_use_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov al, [disk_drive]
    cmp al, [asc_vol]
    je .out                     ; already this volume's
    mov [asc_vol], al
    mov word [asc_n], 0         ; ...and it holds nothing until proven
    cmp word [asc_seg], 0
    jne .haveseg
    mov ax, ASC_KB
    mov bx, MEM_K_ASC
    call mem_claim_x
    jc .out                     ; refused: tier 2, every lookup misses
    mov [asc_seg], dx
    push ax                     ; ...and MOVABLE (SPEC.md 66.5.6). This is the
    mov ax, asc_reloc           ; claim docs/HEAP-CLAIMS.md measured acting as a
    mov bx, MEM_K_ASC           ; BARRIER: it is taken on a VOLUME SWITCH, so
    call mem_movable_x          ; on a machine that has been used it sits
    pop ax                      ; wherever the arena had room - 3KB holding
                                ; 40KB out of reach on the run that found it
.haveseg:
    call asc_root_find          ; CF=0, AX = first cluster, DX = sectors
    jc .out
    cmp dx, ASC_KB*2            ; ...and it must FIT (SPEC.md 54.7.1). Every
    ja .out                     ; byte off a disk is hostile (SPEC.md 18.2)
                                ; and dsk_read_chain reads the whole file into
                                ; a fixed claim - so a bigger ASSOC.DAT than
                                ; this build knows about is a heap overwrite,
                                ; not a bad cache. It was unbounded until the
                                ; row grew a cluster and made it worth asking
    push dx                     ; the sectors dsk_read_chain is about to fill:
                                ; the ONLY bound on what the rows below may say
    mov es, [asc_seg]
    xor bx, bx
    call dsk_read_chain_x         ; ES:BX = the claim
    pop dx                      ; (pop leaves CF alone)
    jc .out
    mov es, [asc_seg]
    cmp word [es:0], 'OS'       ; 'OS88AC' + a version we understand
    jne .out
    cmp word [es:2], '88'
    jne .out
    cmp word [es:4], 'AC'
    jne .out
    cmp byte [es:6], 1
    jne .out
    mov al, [es:7]
    cmp al, ASC_NAPP
    ja .out
    xor ah, ah
    mov [asc_n], ax
    mov al, [es:8]
    cmp al, ASC_NEXT
    ja .out
    xor ah, ah
    mov [asc_ne], ax

                                ; THE COUNTS MUST FIT THE BYTES WE ACTUALLY
                                ; READ. The caps above bound them against this
                                ; build's arrays, which keeps every derived
                                ; offset inside the claim - but mem_claim does
                                ; not clear the block, so a 512-byte file
                                ; declaring 32 apps and 24 extensions had
                                ; asc_merge_ext and asc_seed parsing ~2,160
                                ; bytes dsk_read_chain never wrote: uninitialised
                                ; RAM on the first mount and the PREVIOUS
                                ; disk's ASSOC.DAT on every one after it. That
                                ; is a cache claiming to know things about a
                                ; volume it has never seen (SPEC.md 18.2 - every
                                ; byte off a disk is hostile, and so is every
                                ; byte a disk merely implied)
    mov ax, [asc_n]
    mov bl, ASC_ROW
    mul bl                      ; AX = rows' bytes: napp <= 32, so <= 2,560 and
                                ; the 8-bit multiply cannot touch DX
    mov bx, ax
    mov ax, [asc_ne]
    shl ax, 1
    shl ax, 1                   ; the extension table, four bytes a row
    add bx, ax
    add bx, ASC_HDR             ; BX = what the header says the file contains
    mov ax, dx                  ; DX = sectors, and at most ASC_KB*2 = 6 of
    mov cl, 9                   ; them, so this cannot overflow
    shl ax, cl                  ; AX = what the file actually is
    cmp bx, ax
    jbe .parse
    mov word [asc_n], 0         ; short: the clean "no cache" answer, which
    mov word [asc_ne], 0        ; every lookup already handles as a miss
    jmp short .out
.parse:
    call asc_merge_ext          ; the declarations this volume knows about...
    call asc_seed               ; ...and WHERE each of them is (SPEC.md 54.7.1)
                                ; and what it LOOKS like (54.7.3). Once per
                                ; volume SWITCH, not per mount - the compare
                                ; at the top is the gate - so the reduce is
                                ; paid where a floppy is already turning
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
; asc_seed - teach the hints where this whole DISK's programs are, and what
;            they look like (SPEC.md 54.7.1 / 54.7.3)
; in:  ES = [asc_seg], [asc_n] rows valid, [disk_drive] = this volume
; out: nothing (all registers preserved)
;
; A row records everything assoc_note_app records - the location AND the
; glyph - and this took only the first half of it, which is SPEC.md 54.7.3.
;
; The harvest is per DIRECTORY - assoc_note_app fires once per type-1 entry of
; the one directory being listed - so browsing B: root taught the machine
; nothing, because every program on the shipped apps disk is one folder down
; (SPEC.md 19.2). ASSOC.DAT already covers the whole VOLUME, so one root mount
; can answer for all of it, and the only thing the row was missing was where.
;
; assoc_app_of and NOT asc_slot_of: find, never create. A slot only matters if
; some extension points at it, and the kernel's table is 12 rows against a disk
; that can hold 32 - creating one per package would evict the associations this
; exists to serve. Run AFTER asc_merge_ext so a DECLARING package (54.6) has
; already been given its slot and is found here like any other.
;
; A wrong hint costs one wasted mount and never a wrong file: assoc_try
; re-checks the NAME on arrival and falls through to the remaining rungs
; (SPEC.md 54.4.2). That is what makes a cache safe to seed from at all.
; -----------------------------------------------------------------------------
asc_seed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [asc_n]
    jcxz .out
    mov si, ASC_HDR             ; ES:SI walks the app rows
.row:
    push cx
    push si
    mov di, assoc_stemb         ; stage the stem: assoc_app_of reads through DS
    mov cx, 8
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    pop si
    push si
    mov di, si                  ; bank the row: assoc_app_of clobbers SI
    mov si, assoc_stemb
    call assoc_app_of           ; AL = the slot, CF=1 = this program has no
    jc .next                    ; association, so there is nothing to point
    mov bl, al
    xor bh, bh
    mov dh, al                  ; bank the slot: the glyph take below spends AX
    mov dl, [disk_drive]
    mov [assoc_drv+bx], dl
    mov ax, [es:di+ASC_ROWCLUS]
    shl bx, 1
    mov [assoc_clus+bx], ax
    ; ...and WHAT IT LOOKS LIKE (SPEC.md 54.7.3). The row carries the whole
    ; 64-byte body, so the glyph costs no read either - and without it a
    ; DECLARED extension (54.6) resolved to a slot whose glyph was still the
    ; unresolved sentinel, so its documents drew assoc_compose's BARE PAGE
    ; until the folder the program lives in had been browsed. Every program
    ; on the shipped apps disk is one folder down (19.2) and the documents
    ; are in MEDIA/, so that was every .HTM and .TEX on the disk.
    mov si, di
    add si, ASC_ROWICO + 32     ; the cached body's DATA plane
    mov cx, 16
    xor ax, ax
.ink:
    or ax, [es:si]              ; an all-zero body is the ICONLESS sentinel:
    inc si                      ; leave the glyph unresolved rather than
    inc si                      ; blanking one another volume resolved, which
    loop .ink                   ; is the rule assoc_note_app already follows
    or ax, ax
    jz .next
    sub si, 32                  ; SI walked the plane: back to its start
    mov bl, dh
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov di, assoc_glyph
    add di, bx
    call assoc_reduce           ; ES = [asc_seg], per this routine's contract
.next:
    pop si
    pop cx
    add si, ASC_ROW
    loop .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; asc_root_find - ASSOC.DAT in the volume's ROOT (module-internal)
; out: CF=0 with AX = its first cluster and DX = its size in sectors;
;      CF=1 = absent or unreadable
; clobbers: AX, DX (outputs), CF
;
; The ROOT specifically, and walked here rather than through dskw_stat, which
; resolves in the CURRENT directory - and a mount may be landing in a
; subdirectory, which is exactly when this runs.
; -----------------------------------------------------------------------------
asc_root_find:
    push bx
    push cx
    push si
    push di
    push es
    mov ax, LOW_SEG
    mov es, ax
    xor ax, ax                  ; 0 = the root
    call dsk_dirw_start_x
.sector:
    call dsk_dirw_get_x           ; either failure is "no cache here" (19.2.1)
    jc .no
    mov di, dsk_secbuf
    mov cx, 512/32
.entry:
    mov al, [es:di]
    or al, al
    jz .no                      ; 0x00 ends the directory entirely
    cmp al, 0xE5
    je .next
    push cx
    push di
    mov si, asc_s_name
    mov cx, 11
.cmp:
    mov al, [es:di]
    cmp al, [si]
    jne .nomatch
    inc si
    inc di
    loop .cmp
    pop di
    pop cx
    mov ax, [es:di+26]          ; FstClusLO
    cmp word [es:di+30], 0      ; size HIGH word: without this a 66,048-byte
    jne .no                     ; ASSOC.DAT presents as one sector and the
                                ; caller's fit test is answered about a file
                                ; sixty-four kilobytes smaller than the one on
                                ; the disk. A cache this big is a hostile file
                                ; either way, so it is refused rather than
                                ; truncated
    mov dx, [es:di+28]          ; size low word -> sectors
    add dx, 511
    mov cl, 9
    shr dx, cl
    or dx, dx
    jz .no
    pop es
    pop di
    pop si
    pop cx
    pop bx
    clc
    ret
.nomatch:
    pop di
    pop cx
.next:
    add di, 32
    loop .entry
    jmp short .sector
.no:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; asc_merge_ext - take the cached association rows (SPEC.md 54.7)
; in:  ES = [asc_seg], [asc_n]/[asc_ne] set
; out: nothing (all registers preserved)
;
; A cache HIT skips the package's header read, and the header is where a
; declaration lives (54.6) - so without this, warming the cache would silently
; cost every declaration on the disk. The rows are merged with the same rule a
; declaration has: a runtime claim (ASSOC_STICKY) outranks them.
; -----------------------------------------------------------------------------
asc_merge_ext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [asc_ne]
    jcxz .out
    mov di, [asc_n]             ; the ext rows begin past the app rows
    mov ax, ASC_ROW
    mul di
    add ax, ASC_HDR
    mov di, ax
.row:
    push cx
    mov al, [es:di+3]           ; the app row it names
    cmp al, [asc_n]
    jae .next
    push di
    call asc_slot_of            ; AL = a kernel app slot for that row
    pop di
    jc .next
    mov dl, al
    mov si, assoc_extb          ; stage the extension, then the usual rules
    mov al, [es:di]
    mov [si], al
    mov al, [es:di+1]
    mov [si+1], al
    mov al, [es:di+2]
    mov [si+2], al
    cmp byte [si], ' '
    jbe .next
    push di
    call assoc_find
    jc .fresh
    call assoc_ext_row
    test byte [bx+3], ASSOC_STICKY
    jnz .popnext                ; a runtime claim outranks a cached one
    jmp short .point
.fresh:
    call assoc_ext_new
    jc .popnext
.point:
    mov [bx+3], dl
.popnext:
    pop di
.next:
    pop cx
    add di, 4
    loop .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; asc_slot_of - the kernel app slot for a CACHE row (module-internal)
; in:  AL = cache row index, ES = [asc_seg]
; out: CF=0 and AL = a kernel app slot (created if the stem is new);
;      CF=1 = the kernel's table is full
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
asc_slot_of:
    push bx
    push cx
    push si
    push di
    xor ah, ah
    mov bx, ASC_ROW
    mul bx
    add ax, ASC_HDR
    mov si, ax                  ; ES:SI = the cache row
    mov di, assoc_stemb         ; stage its stem into the kernel
    mov cx, 8
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .cp
    mov si, assoc_stemb
    call assoc_app_of
    jnc .have
    call assoc_app_new
.have:
    pop di
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; asc_lookup - is this package's icon already cached? (SPEC.md 54.7)
; in:  SI -> its staged 32-byte directory entry
; out: CF=0 and DI = the offset of its 64-byte icon within [asc_seg];
;      CF=1 = a miss, so the caller reads the sector as it always did
; clobbers: DI (the output), CF
;
; Keyed on (stem, SIZE), which is why a modified program can never serve a
; stale icon: any edit changes the length, so it misses and is re-harvested.
; A false hit needs a different program with the same name AND byte-identical
; size, and costs a wrong ICON - never a wrong load, which SPEC.md 54.4's name
; re-check governs separately.
; -----------------------------------------------------------------------------
asc_lookup_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov cx, [asc_n]
    jcxz .no
    cmp word [asc_seg], 0
    je .no
    mov dx, [si+20]             ; the size the directory reports
    call assoc_stem_of          ; DI -> assoc_stemb
    mov es, [asc_seg]
    mov di, ASC_HDR
.row:
    push cx
    mov si, assoc_stemb
    mov cx, 8
    mov bx, di
.cmp:
    mov al, [es:bx]
    cmp al, [si]
    jne .next
    inc bx
    inc si
    loop .cmp
    cmp dx, [es:di+8]           ; the stem matches: the size must too
    jne .next
    pop cx
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    add di, ASC_ROWICO
    clc
    ret
.next:
    pop cx
    add di, ASC_ROW
    loop .row
.no:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; asc_take - spend a cache hit: the icon, the location and the glyph
; in:  DI = the icon's offset in [asc_seg], DX = the listing index,
;      SI -> the staged directory entry
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
asc_take_x:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    call asc_note               ; the program's location and glyph first,
                                ; while SI still points at its entry
    push dx                     ; the listing index
    mov dx, [asc_seg]           ; the cached body -> dsk_ico, in the kernel
    push ds                     ; segment, which is where dsk_put_icon_k
    pop ax                      ; takes its source from
    mov si, di
    mov di, dsk_ico
    mov cx, 64
    call dsk_copy_seg_x           ; DX = src seg, AX = dst seg
    pop ax                      ; ...the listing index again
    mov si, dsk_ico
    call dsk_put_icon_k_x
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; asc_note - a cache hit's half of assoc_note_app (module-internal)
; in:  SI -> the staged entry, DI = the icon's offset in [asc_seg]
; out: nothing (all registers preserved)
;
; The location comes from the DIRECTORY ENTRY, which a hit still has - which
; is why an ASSOC.DAT row carries no cluster and so depends on nothing about
; the disk's layout.
; -----------------------------------------------------------------------------
asc_note:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push di                     ; the icon offset: assoc_stem_of takes DI
    call assoc_stem_of
    mov si, di
    call assoc_app_of
    jnc .have
    call assoc_app_new
    jc .done
.have:
    mov bl, al
    xor bh, bh
    mov cl, [disk_drive]
    mov [assoc_drv+bx], cl
    push bx
    shl bx, 1
    mov ax, [dsk_cwd]
    mov [assoc_clus+bx], ax
    pop bx
    mov cl, 3
    shl bx, cl
    mov di, assoc_glyph
    add di, bx
    pop si
    push si
    add si, 32                  ; the cached body's DATA plane
    mov es, [asc_seg]
    call assoc_reduce
.done:
    pop di                      ; balance the banked icon offset
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Far entries (SPEC.md 2.6): the resident thunks in kernel.asm far-call these,
; and these near-call the bodies above - so every routine here keeps its
; ordinary near `ret`.
; =============================================================================
acf_assoc_post:     call assoc_post_x
                retf
acf_assoc_run:      call assoc_run_x
                retf
acf_assoc_docicon:  call assoc_docicon_x
                retf
acf_assoc_note_app: call assoc_note_app_x
                retf
acf_asc_use:        call asc_use_x
                retf
acf_asc_lookup:     call asc_lookup_x
                retf
acf_asc_take:       call asc_take_x
                retf
acf_osapi_arg_file: call osapi_arg_file_x
                retf
acf_osapi_assoc_set: call osapi_assoc_set_x
                retf

section .text                   ; DATA, so back to the kernel segment
asc_s_name: db 'ASSOC   DAT'    ; the RAW 8.3 form: this walks directory
                                ; sectors, not display names

; In .text with real initialisers, NOT .bss - the dsk_fatw0 precedent, and for
; the same reason: -f bin zeroes nothing, asc_use runs at the machine's very
; first mount, and garbage here is not a wrong answer but a live one. A stale
; asc_seg would have the harvest reading icons out of whatever segment those
; bytes name, and a stale asc_vol would tell the first mount its cache is
; already loaded.
; asc_reloc - the compactor moved the cache (SPEC.md 66.2). ONE WORD: every
; reader loads ES from [asc_seg] immediately before a short in-memory burst,
; and the only disk read that targets the cache is the one that FILLS it,
; which dsk_xfer pins for the duration (SPEC.md 66.3 rule 5).
asc_reloc:
    cmp bx, [asc_seg]
    jne .out
    mov [asc_seg], dx
.out:
    ret

asc_seg:  dw 0                  ; the claim, 0 = none (refused, or not yet)
asc_n:    dw 0                  ; app rows in it, 0 = no usable cache
asc_ne:   dw 0                  ; ...and association rows
asc_vol:  db 0xFF               ; whose cache it holds, 0xFF = nobody's
