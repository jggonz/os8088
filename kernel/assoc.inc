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
;
; KERN_BIG ONLY (SPEC.md 54.0). The gate is HERE rather than around
; kernel.asm's %include for tools/kernsize.py's reason, which that line
; records: a whole file behind one %ifdef reports as a zero row instead of
; vanishing, and the per-module pass keeps assembling.
%ifdef OS88_ASSOC

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
;
; GENERATED, not stored (SPEC.md 54.3): 24 of the 32 words are one of two
; constants, so the image carries the SEVEN irregular ones and assoc_compose
; lays the frame down as two runs off this seed and two runs of a constant.
; 14 bytes of .text against 64, for 25 of .cold.
;
;   mask   3FE0 3FF0 3FF8         then 13 x ASSOC_PGMASK
;   data   3FE0 2030 2028 203C    then 11 x ASSOC_PGBODY   then ASSOC_PGMASK
assoc_pg_seed:
    dw 0x3FE0, 0x3FF0, 0x3FF8           ; the cut corner, in the mask...
    dw 0x3FE0, 0x2030, 0x2028, 0x203C   ; ...and the dog-ear, in the body

ASSOC_PGMASK equ 0x3FFC         ; a full-width row: both side edges, ink
ASSOC_PGBODY equ 0x2004         ; ...and the same row with a white interior

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
    db 'DOS     '                ; SPEC.md 96: the handler for .COM, .EXE and
                                 ; .LNK (96.21),
                                 ; and it has to be a BUILT-IN rather than a
                                 ; declaration on the disk - see assoc_ext
    times (ASSOC_NAPP - 5) * 8 db 0

; The four defaults' glyphs are BAKED AT BUILD TIME by tools/os88mini.py out
; of each package's own .o88, so the kernel ships knowing what its own
; applications look like and a document icon costs no disk read on the first
; boot of any machine. Generated and never hand-pasted: pasted bytes go stale
; in silence when an app's icon changes, where the make dependency cannot.
assoc_glyph:
%include "associco.inc"
    times (ASSOC_NAPP - 5) * 8 db 0

; --- the extension slots ---------------------------------------------------
assoc_ext:
    db 'BMP', 0                 ; -> PAINT
    db 'GIF', 0                 ; -> PAINT
    db 'TXT', 1                 ; -> NOTEPAD
    db 'MOD', 2                 ; -> TRACKER
    db 'MD ', 3                 ; -> ARTFUL
    db 'COM', 4                 ; -> DOS (SPEC.md 96)
    db 'EXE', 4                 ; -> DOS
    db 'LNK', 4                 ; -> DOS: a SHORTCUT (SPEC.md 96.21), and
                                ; built in for the same reason as the two
                                ; above and more so - a shortcut's whole point
                                ; is that it sits where the USER put it
    times (ASSOC_NEXT - 8) * 4 db 0

; WHY .COM AND .EXE ARE BUILT IN rather than declared in DOS.O88's own header
; the way TRACKER declares .MOD (SPEC.md 54.6). A declaration is harvested at
; a FULL mount, and the boot mount is a quiet one - disk.inc says `asc_use_x`
; IS DELIBERATELY NOT CALLED there - so a declared association is not known
; until the user has opened a Disk window on the volume the PROGRAM is on.
; That is invisible for every other handler, because a document and its
; program ride the SAME disk: the apps floppy carries BEVERLY.MOD and
; TRACKER.O88 together, so mounting it teaches the machine both at once.
;
; DOS is the first handler whose documents are on somebody ELSE's disk - a
; floppy of .COM files, with DOS.O88 back on the system disk - and that is
; exactly the case the harvest cannot reach. **.LNK JOINED THEM AND HIT THE
; IDENTICAL WALL**, which is worth recording because the second time cost a
; debugging session the first had already paid for: a shortcut declared and
; not built in opened as `Bad package` on the floppy it was saved to, and the
; symptom was indistinguishable from a writer that had emitted rubbish.
; Measured: with DOS declared and
; not built in, a double-click on B:\FOO.COM answers `Bad package`, and the
; SAME double-click works after opening A: once, because that full mount is
; what finally read A:'s ASSOC.DAT.
;
; It costs NO kernel bytes: both tables are `times`-filled to ASSOC_NAPP and
; ASSOC_NEXT already, so a row that was padding becomes a row that is used.


; --- per-app side arrays ---------------------------------------------------
; Parallel rather than inside the slot, so the 8-byte strides survive. The
; hint is a place to LOOK and never an answer: a cluster means nothing on a
; disk that has been swapped, so the open path re-checks the name on arrival.
assoc_clus:  times ASSOC_NAPP dw 0     ; the directory it was last SEEN in
assoc_drv:   times ASSOC_NAPP db 0xFF  ; ...on which volume (0xFF = never seen)
assoc_dfold: db 1, 1, 1, 1, 1          ; build-time folder: 0 root, 1 APPS,
             times ASSOC_NAPP - 5 db 0 ; 2 GAMES. The one part of a default
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
; assoc_stage - CX bytes of ES:SI into the kernel at DI (SPEC.md 20.2's
;               staging idiom, written once)
; in:  ES:SI -> the source, DI -> the kernel destination, CX = the count
; out: SI and DI past the copy, CX = 0, AL = the last byte copied
;
; CX MUST NOT BE 0 ON ENTRY - the body ends in `loop`, so a zero count copies
; 65,536 bytes. Every caller passes a literal: 3 for an extension, 8 for a
; stem, and 16 for the whole of OSAPI_PKG_START's document locator (SPEC.md
; 21.5.3.3), which the three cells it lands in are laid out to be.
;
; IT DOES NOT FOLD CASE and must not: that 16-byte caller's last three bytes
; are a directory cluster WORD and a volume BYTE, and a cluster of 0x6C6C
; would quietly become 0x4C4C. Everything else reaching here is already
; uppercase, being FAT-derived or a declared block.
; -----------------------------------------------------------------------------
assoc_stage:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop assoc_stage
    ret

; -----------------------------------------------------------------------------
; asc_inkck - is this run of words ENTIRELY ZERO? (module internal)
; in:  ES:SI -> the words, CX = how many (never 0 - the body ends in `loop`)
; out: ZF = 1 = every word was zero; all registers preserved
;
; SPEC.md 54.2's UNRESOLVED SENTINEL and SPEC.md 54.7.3's ICONLESS one are the
; same question asked of different lengths - four words of a glyph, sixteen of
; an icon's data plane - and it was written out three times: in
; assoc_glyph_take, in asc_body_glyph and again in the absorb, where the third
; copy reads the same sixteen words as the second. `pop` writes no flag, so
; the answer survives the unwind.
; -----------------------------------------------------------------------------
asc_inkck:
    push ax
    push cx
    push si
    xor ax, ax
.w:
    or ax, [es:si]
    inc si
    inc si
    loop .w
    or ax, ax
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_glyph_di - where an app slot's 8x8 glyph lives (SPEC.md 54.3)
; in:  BL = the app slot (BH is zeroed here, so BL alone is enough)
; out: DI -> its eight glyph bytes. BX and CX preserved; flags are not
; -----------------------------------------------------------------------------
assoc_glyph_di:
    push bx
    push cx
    xor bh, bh
    mov cl, 3
    shl bx, cl                  ; slot * 8
    mov di, assoc_glyph
    add di, bx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; assoc_hint_here - this app slot was just SEEN here (SPEC.md 54.4.2 rung 1)
; in:  BL = the app slot, BH = 0
; out: nothing. BX preserved; AX clobbered, which every caller already spent
;
; The hint is [disk_drive] and [dsk_cwd] - the volume and directory that are
; CURRENT at the moment of the call - so it is only ever correct where the
; program was actually found or read.
; -----------------------------------------------------------------------------
assoc_hint_here:
    push bx
    mov al, [disk_drive]
    mov [assoc_drv+bx], al
    shl bx, 1
    mov ax, [dsk_cwd]
    mov [assoc_clus+bx], ax
    pop bx
    ret

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
    mov word [di], '  '         ; a short extension pads itself, so a two-
    mov byte [di+2], ' '        ; letter 'MD' matches a declared 'MD '
    xor dx, dx                  ; DX = the byte after the LAST dot, 0 = none
    mov cx, 16                  ; seen
    cld
.scan:
    lodsb                       ; ...which is SI itself, lodsb having already
    or al, al                   ; stepped past the dot - so no `inc`
    jz .dot
    cmp al, '.'
    jne .next
    mov dx, si
.next:
    loop .scan
.dot:
    or dx, dx
    jz .none
    mov si, dx
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
    clc
    jmp short .out
.none:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop ax
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
    mov di, assoc_stemb
    mov cx, 8
.copy:                          ; the copy first, then the PAD over what is
    mov al, [si]                ; left of the SAME counter - one walk, not two
    or al, al
    jz .pad
    cmp al, '.'
    je .pad
    mov [di], al
    inc si
    inc di
    loop .copy
    jmp short .done             ; exactly 8: `loop` falls through at CX = 0 and
.pad:                           ; entering .pad there would pad 65,536 bytes
    mov byte [di], ' '
    inc di
    loop .pad
.done:
    pop si
    pop cx
    pop ax
    mov di, assoc_stemb
    ret

; -----------------------------------------------------------------------------
; assoc_find - the app an extension names (SPEC.md 54.2)
; in:  SI -> 3 extension bytes
; out: CF=0 and AL = app slot index, BX -> the matched row; CF=1 = no
;      association, and BX IS UNDEFINED (the corrupt-index refusal at .hit
;      aborts the scan with BX on a row that matched the extension)
; clobbers: AX and BX (both outputs), CF
;
; BX is an output because assoc_ext_row used to be a second walk of this same
; table, run immediately after a successful call to re-find the row this one
; had already found. Every caller that wants the row takes it from here; the
; two published entries (assoc_docicon, assoc_post) bank BX themselves so
; their "everything else preserved" contracts still hold.
; -----------------------------------------------------------------------------
assoc_find:
    push cx
    mov bx, assoc_ext
    mov cx, ASSOC_NEXT
.row:
    cmp byte [bx], 0
    je .next                    ; a free slot never matches
    mov al, [si]                ; the key off SI and the row off BX - no third
    cmp al, [bx]                ; register, so no DI to bank
    jne .next
    mov al, [si+1]
    cmp al, [bx+1]
    jne .next
    mov al, [si+2]
    cmp al, [bx+2]
    je .hit
.next:
    add bx, 4
    loop .row
.miss:
    stc
    jmp short .out
.hit:
    mov al, [bx+3]
    and al, ASSOC_IX
    cmp al, ASSOC_NAPP
    jae .miss                   ; a corrupt index names no app
    clc
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; assoc_slot_of - the app slot that claims a staged display name's EXTENSION
; in:  SI -> a NUL-padded 16-byte display name (the SPEC.md 19.1 staged shape)
; out: CF=0 and AL = app slot index, BX -> the matched row; CF=1 = the name
;      carries no extension, or nothing claims it
; clobbers: AX and BX (both outputs), DI, CF.  SI IS PRESERVED
;
; assoc_ext_of and then assoc_find, which is one question - *what opens this?*
; - and was written out THREE times: assoc_docicon_x, assoc_post_x and
; ld_pkg_byname's .byext arm, the last two banking SI round it by hand. The
; extension buffer is the only thing that stands between the two calls, so
; naming the pair costs nothing and the shuffle comes out of every caller.
;
; `pop` writes no flag, so the CF assoc_find answered rides through it.
; -----------------------------------------------------------------------------
assoc_slot_of:
    call assoc_ext_of           ; DI -> assoc_extb
    jc .no
    push si
    mov si, di
    call assoc_find             ; AL = the app slot, BX the row
    pop si
.no:
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
    clc
    jmp short .out
.nomatch:
    pop si
.next:
    add bx, 8
    inc dh
    jmp .row
.miss:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
    jmp kretc_di

; -----------------------------------------------------------------------------
; assoc_glyph_take - a SHIPPED glyph into a slot, unless it is blank
;                    (SPEC.md 54.3.2)
; in:  ES:SI -> eight glyph bytes, DI -> the slot's eight in the kernel
; out: CF=0 = copied; CF=1 = they were all zero and nothing was written
;      (all registers preserved)
;
; All-zero is the UNRESOLVED sentinel (54.2) on both sides: in an image the
; tool refuses it, and in a cache row it is how a package that ships no glyph
; says so - so the caller reduces the icon instead. Two loops rather than one
; copy-and-OR, because a zero copied over a glyph another volume already
; resolved would un-resolve it (54.7.3's rule).
; -----------------------------------------------------------------------------
assoc_glyph_take:
    push ax
    push cx
    push si
    push di
    mov cx, 4                   ; NEITHER LOOP IS THIS ROUTINE'S OWN: the
    call asc_inkck              ; blank test is asc_inkck above and the copy
    stc                         ; is SPEC.md 20.2's staging idiom, which is
    jz .out                     ; ES:SI -> DS:DI and exactly what a glyph in a
    mov cx, 8                   ; cache row, a store row or an image needs.
    call assoc_stage            ; Both advance SI and this routine banks it
    clc                         ; anyway
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_img_glyph - a slot's glyph out of a package IMAGE (SPEC.md 54.3.2)
; in:  ES:SI -> the image's header (a first sector in LOW_SEG, or a package's
;      own segment at 0), DI -> the slot's eight glyph bytes
; out: nothing (all registers preserved)
;
; The block the package SHIPS when its flags say so, else the icon's data
; plane REDUCED (54.3). Both image-shaped writers - the harvest and a runtime
; claim - come through here, so they cannot disagree. The tool requires the
; icon and the association block in front of the glyph, which is what fixes
; it at LD_H_GLYPH; a foreign file that sets the bit without them wears eight
; bytes of its own code, which is a wrong picture and nothing worse.
; -----------------------------------------------------------------------------
assoc_img_glyph:
    push si
    test byte [es:si+LD_H_FLAGS], LD_HF_GLYPH
    jz .reduce
    add si, LD_H_GLYPH
    call assoc_glyph_take
    jnc .out
    sub si, LD_H_GLYPH          ; blank after all: as if it shipped none
.reduce:
    add si, LD_HDR_SIZE + 32    ; the DATA plane of the embedded icon
    call assoc_reduce
.out:
    pop si
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
    mov bl, al                  ; bank the slot: the .inset loop below spends AX
    push di
    mov si, assoc_pg_seed       ; the page frame, composed rather than copied
    cld                         ; (SPEC.md 54.3) - and the runs spend AX, which
    mov cx, 3                   ; is why the slot is banked in BL two lines up
    rep movsw
    mov ax, ASSOC_PGMASK
    mov cx, 13
    rep stosw
    mov cx, 4
    rep movsw
    mov ax, ASSOC_PGBODY
    mov cx, 11
    rep stosw
    mov ax, ASSOC_PGMASK        ; the second run left ASSOC_PGBODY in AX
    stosw                       ; ...and the bottom edge is solid
    call assoc_glyph_di         ; the app's glyph, addressed through DI...
    mov si, di
    pop di                      ; ...and the page frame we just laid down
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
; dsk_ico is dsk_get_icon's staging buffer and has FOUR tenants: dsk_get_icon
; (fdlg_icon), fmv_get_icon (fm_draw_icon16), assoc_docicon (mount pass 4b)
; and asc_take (mount pass 4a). All four run on task 0 - one on the repaint
; path, two inside disk_mount, one in the modal dialog - and NO tenant's live
; range contains a yield, a claim or a drawing call that reaches another.
; disk_mount_x in particular DRAWS NOTHING AT ALL, which is what makes "no
; tile is composed during a mount" structural rather than hoped for. A fifth
; tenant must state which task it runs on and prove it cannot be entered from
; inside another's stage-to-consume window - which is exactly why
; inst_icon_ptr (dock_draw_tile) is NOT one: wm_destroy reaches it on a dying
; package's worker, and osapi_file_goto reaches a mount with no lock held, so
; it stages into inst_icobuf of its own (instance.inc).
; -----------------------------------------------------------------------------
assoc_docicon_x:
    push ax
    push bx                     ; assoc_find answers the matched row in BX
    push si
    push di
    call assoc_slot_of          ; AL = the app slot the extension names
    jc .no
    mov di, dsk_ico
    call assoc_compose
    mov [assoc_docslot], al     ; ...AND WHICH SLOT IT CAME FROM (SPEC.md
                                ; 25.9). The body is a pure function of the
                                ; slot's glyph, so every document of one
                                ; association composes the same 64 bytes -
                                ; which is the whole reason the store keys
                                ; them by slot instead of by name. Banked in
                                ; a cell rather than returned in AL because
                                ; this proc's contract preserves every
                                ; register and four callers rely on it
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

section .text                   ; DATA, so back to the kernel segment
                                ; (SPEC.md 2.6 rule 1)
assoc_docslot: db 0xFF          ; the slot assoc_docicon last composed from
section .cold

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
    mov cx, 3
    call assoc_stage            ; ...and SI is left past the extension
    push si
    mov si, assoc_extb
    cmp byte [si], ' '          ; a blank extension names nothing
    jbe .next
    call assoc_point            ; ...and the table being full drops the rest
.next:
    pop si
    pop cx
    loop .one
.out:
    jmp kretc_di

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
    call assoc_hint_here        ; where it was seen...
    test byte [assoc_hflags], 1 ; ...and what it looks like, IF it ships an
    jz .out                     ; icon at all - an iconless package leaves
    call assoc_glyph_di         ; the glyph unresolved and its documents draw
    mov si, dsk_secbuf          ; the bare page, which is honest. SI = the
                                ; image's header, in the sector buffer
    call assoc_img_glyph        ; shipped or reduced (SPEC.md 54.3.2); ES =
                                ; LOW_SEG, per SPEC.md 2.1
.out:
    jmp kretc_di

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
    push bx                     ; assoc_find answers the matched row in BX
    push cx
    push si
    push di
    push si                     ; the entry, for the name copy below. NOTE the
                                ; FIFTH push: the epilogues below pop si three
                                ; times, and the new pop bx belongs after the
                                ; LAST of them, not before
    call assoc_slot_of          ; AL = the app slot (SI preserved: the entry
    jc .no                      ; is wanted again for the name copy below)
    mov [assoc_dapp], al
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
    pop si                      ; the entry again
    push si
    mov di, assoc_doc           ; stage the document's own name - files.inc's
    mov cx, 12                  ; bounded, always-terminated copy (SPEC.md 4),
    call fm_ncpy                ; into a 13-byte buffer with CX = 12
    pop si
    pop di
    mov [assoc_pwin], di
    clc
    jmp short .out
.no:
    pop si
    pop di
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_fname - build '<STEM>.O88' for the app slot in AL
; in:  AL = app slot
; out: assoc_fnb holds the NUL name; all registers preserved
; -----------------------------------------------------------------------------
assoc_fname:
    push di
    mov di, assoc_fnb
    call assoc_stemcpy
    mov word [di], '.O'
    mov word [di+2], '88'
    mov byte [di+4], 0
    pop di
    ret

; -----------------------------------------------------------------------------
; assoc_stemcpy - an app slot's stem, unpadded, at DI (SPEC.md 54.2)
; in:  AL = the app slot, DI = where it goes
; out: DI past the bytes written; everything else preserved
;
; The stem is space-padded to 8, so the copy stops at the first space rather
; than counting - a stem cannot contain one. DI is an in/out on purpose: both
; callers go straight on to append a tail where it now points.
; -----------------------------------------------------------------------------
assoc_stemcpy:
    push ax
    push bx
    push cx
    push si
    mov bl, al
    xor bh, bh
    mov cl, 3
    shl bx, cl
    mov si, assoc_stem
    add si, bx
    mov cx, 8
.l:
    mov al, [si]
    cmp al, ' '
    je .o
    mov [di], al
    inc si
    inc di
    loop .l
.o:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; assoc_try - is the program in THIS directory? (one rung of SPEC.md 54.4)
; in:  DL = volume, AX = directory cluster (0 = the root)
; out: CF=0 = yes, with that volume+directory CURRENT; CF=1 = not here (and
;      the mount may have failed, which is the same answer to the caller)
; clobbers: CF only - AX IS NOT AN OUTPUT and is restored across both exits.
;      It used to be documented as the directory index; assoc_locate's own
;      header forty lines below already said so, and two headers disagreeing
;      about one routine is worse than either being wrong alone
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
    jc .out                     ; whether a FILE is in this directory, and
                                ; dskw_stat walks the directory sectors to
                                ; answer it. A listing would be a scan, a sort
                                ; and one icon-harvest read per file, bought
                                ; entirely for an INDEX nobody needs now that
                                ; the loader takes a name (SPEC.md 21.4)
    mov si, assoc_fnb
    call dskw_stat_x              ; ...and since 18.4.3 it costs no re-read
.out:                           ; ...whose CF IS this routine's answer: both
    jmp kretc_si

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
    call assoc_try              ; ...and its CF is this routine's answer too
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop di
    pop si
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
    stc
    jmp short .out
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
    call assoc_hint_here        ; program we actually located - AFTER the seed
                                ; on purpose, so a stale cached cluster for
                                ; THIS program loses to where it really is
    clc                         ; explicit, not inherited: the helper's last
                                ; store leaves CF as the `shl` left it, which
                                ; is an accident of the table's size
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

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
    mov dx, [di+FS_CWD]         ; its state block. This was a fmv_sync - a
    jmp short .located          ; FULL mount - and all it was ever wanted for
                                ; was these two words, which fmv_sync itself
                                ; takes from exactly here (SPEC.md 54.9.1).
                                ; Nothing downstream needs the globals to be
                                ; this folder: assoc_locate navigates itself,
                                ; assoc_back comes back to what is banked
                                ; here, and files_refresh paints from the
                                ; WINDOW's cache (SPEC.md 22.1)
.curvol:
    mov dx, [dsk_cwd]           ; no poster - a package's own request - so the
    mov al, [disk_drive]        ; volume the machine is on IS the home
.located:
    mov [assoc_ddrv], al        ; ...banked once, whichever rung answered
    mov [assoc_dclus], dx

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
    mov byte [assoc_argp], 0    ; a package launched later, by any route, must
                                ; never inherit this document
    call ld_say_status          ; record the verdict and SAY it (SPEC.md
                                ; 59/22.9) - loader.inc's, `.cold` like this,
                                ; so the call is near, and it preserves every
                                ; register because toast_say does
    push ds
    pop es                      ; ld_run_body's failure exits leave ES adrift
    call assoc_back             ; the user's volume FIRST, then the window
    cmp byte [ld_painted], 0
    je .refresh                 ; ld_run_body's wm_show drew the new window
                                ; and the poster's own window was never
                                ; written on by this load at all (SPEC.md
                                ; 22.17), so nothing here is owed a repaint
    call assoc_handover         ; ...and the DOCUMENT is a second phase, now
    jmp short .out              ; that there is something to look at while it
                                ; loads (SPEC.md 54.10)

.nofile:
    call assoc_back
    pop ax                      ; the app index, back off the stack and into
    push ax                     ; 'TRACKER.O88 - not on this disk', because
    call assoc_progname         ; "Program not found" alone leaves the user
    mov si, assoc_pnbuf         ; with no way to know WHICH program a document
    mov bx, assoc_s_lead        ; wanted - which is exactly the report that
    mov di, assoc_s_ttl         ; found this (SPEC.md 54.4.1)
    mov bp, ui_note
    call KERNEL_SEG:cw_mem_disp
.refresh:                       ; ...and both routes end at the one repaint
    call files_refresh_x
.out:
    pop ax                      ; the extra word this routine banked itself...
    jmp kretfc_es               ; ...and then the canonical eight (kernel.asm)

; -----------------------------------------------------------------------------
; assoc_handover - the window is up; tell the package to spend its document
; in:  the load succeeded ([ld_painted] != 0), so [ld_rec] is the instance
;      ld_run_body published and its I_WIN is the window wm_show just drew;
;      the UI task, gfx lock NOT held
; out: nothing
; clobbers: EVERYTHING. It is called from ONE place, in assoc_run_x, where the
;      next instruction is `jmp short .out` - so no register is live across it.
;      A SECOND CALLER, OR MOVING THIS CALL, MAKES THAT FALSE. "One caller
;      which banks everything at entry" is NOT the invariant and would not be
;      enough: a caller can bank at entry and still hold a live value in a
;      register at the call site.
;
; **THE SECOND PHASE OF A DOCUMENT LAUNCH** (SPEC.md 54.10). The first is the
; PROGRAM: locate it, read it, run its entry proc, show its window. Everything
; a package used to do with the document happened inside that - in the entry
; proc, or in the first W_PAINT, which is inside wm_show's repaint - so the
; machine was frozen at the disk with the desktop half redrawn and the
; application not on the glass at all. A double-click on a picture bought
; several seconds of a screen that had nothing on it but the file-activity
; widget, and PERFORMANCE.md's "a redraw is priced in calls" has nothing to
; say about it: the cost is one int 13h at a time and the defect is that
; nobody can see anything while it is paid.
;
; So the document waits for HERE: wm_show has drawn the new window and every
; other window with it, assoc_back has put the user's volume back, and this
; runs on the UI task with no lock held - which is exactly the environment
; SPEC.md 74.1 gives a W_ONWAKE handler. The package's own handler is
; therefore what is called, with nothing delivered: the pending flag it set
; from OSAPI_ARG_FILE in its entry proc is what tells it this wake is the
; document's, and a package that installs no handler is the machine it was
; before this existed.
;
; It is a CALL and not an OSAPI_WM_WAKE post, deliberately. A post can be
; refused (a full ring), and a refusal here loses the document silently -
; where the whole of what the ring buys, ordering against the mouse, is
; already settled by standing at this instruction.
; -----------------------------------------------------------------------------
assoc_handover:
    mov di, [ld_rec]            ; the record .reg published, and I_WIN is the
    or di, di                   ; window the entry proc answered with
    jz .out
    mov si, [di+I_WIN]
    or si, si
    jz .out
    mov bp, wm_wake_call        ; assoc_run's second phase (SPEC.md 54.10): the
    call KERNEL_SEG:cw_mem_disp ; launched window is on the glass, so the
                                ; package may now spend the document it was
                                ; handed
.out:
    ret

; assoc_back - put the user's volume back where the document is. A failed
; remount leaves that volume mount-failed at its root with the write gate
; shut, which is survivable and is exactly ui_tm_back's contract.
;
; clobbers AX and DX. Two callers, both in assoc_run_x, and NEITHER is
; followed by the epilogue - so this one is argued site by site rather than by
; assoc_handover's rule: AX and DX are dead at both. After the first, the only
; instructions before .out are `cmp byte [ld_painted],0` and a call to
; assoc_handover (no register inputs) or files_refresh_x (takes none, clobbers
; nothing). After the second, `pop ax` kills AX before any read, and
; assoc_progname takes AL and preserves everything; DX is unread throughout.
assoc_back:
    mov ax, [assoc_dclus]
    mov dl, [assoc_ddrv]
    jmp dsk_chdir_q_x           ; QUIET (SPEC.md 18.9/54.9): this restores the
                                ; volume so the app can READ ITS DOCUMENT, and
                                ; a read resolves by NAME through dskw_find's
                                ; directory walk - it never touches the global
                                ; listing. The scan, the sort and one icon
                                ; harvest per file were bought for nothing.
                                ; A TAIL CALL: its ret is this routine's, and
                                ; disk.inc's `.cold` is this file's, so near

section .text                   ; DATA, so back to the kernel segment: cold
                                ; code addresses its data through DS, which is
                                ; still KERNEL_SEG (SPEC.md 2.6 rule 1)
assoc_s_apps:   db 'APPS', 0
assoc_s_games:  db 'GAMES', 0
assoc_s_ttl     equ fdlg_s_topen    ; the Standard File dialog's own title
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
; The stem copy is assoc_stemcpy's, which assoc_fname shares.
; -----------------------------------------------------------------------------
assoc_progname:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es                      ; the tail lands through ES:DI
    mov di, assoc_pnbuf
    call assoc_stemcpy
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
; **THESE THREE CELLS ARE THE PUBLISHED LOCATOR, IN ITS OWN ORDER** (SPEC.md
; 21.5.3): 13 NUL name bytes, the directory WORD, the volume BYTE - which is
; exactly what osapi_arg_file answers with and exactly what OSAPI_PKG_START is
; handed. They are ONE 16-byte block with `equ` names rather than three `resb`
; lines because assoc_fnb used to sit in the MIDDLE of them, and a block
; cannot have a line inserted into it: ld_pkg_byname copies the locator in a
; single assoc_stage of 16 (SPEC.md 21.5.3.3) and that is legal only while
; this holds.
assoc_dblk:  resb 16            ; the locator, one block
assoc_doc    equ assoc_dblk     ; 13: the pending document's 8.3 name
assoc_dclus  equ assoc_dblk+13  ; ...and where it lives: directory cluster
assoc_ddrv   equ assoc_dblk+15  ; ...and volume
assoc_fnb:   resb 13            ; '<STEM>.O88', built by assoc_fname
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
    retf
.none:
    stc
    retf

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
    call assoc_stage            ; through DS
    mov di, assoc_stemb
    mov cx, 8
    call assoc_stage

    mov si, assoc_stemb         ; the app slot: reuse this program's own if
    call assoc_app_get          ; it already has one, so a program claiming
    jc .full                    ; four extensions takes one slot and not four
    mov dl, al                  ; DL = the app slot from here down
    mov si, assoc_extb
    call assoc_find             ; already claimed by someone?  BX = that row
    jnc .point
    call assoc_ext_new          ; BX = a free row, with the extension in it
    jc .full
.point:
    mov al, dl
    or al, ASSOC_STICKY         ; a runtime claim outranks a declaration
    mov [bx+3], al
    call assoc_self_glyph       ; DL = slot; take the caller's own icon
    clc
    jmp short .out
.full:
    stc
.out:
    jmp kretfc_di

; -----------------------------------------------------------------------------
; assoc_point - point one extension at one app slot (SPEC.md 54.6's rule)
; in:  SI -> 3 ext bytes, DL = the app slot
; out: nothing (BX = the row, and AX and CF are assoc_find's or
;      assoc_ext_new's - no caller reads either)
;
; A DECLARATION and a CACHED row are both outranked by a runtime registration
; (ASSOC_STICKY), and a full table drops the extension silently - which is
; what both callers already did, spelled out twice.
;
; osapi_assoc_set is deliberately NOT folded in: it SETS the sticky bit and
; skips the sticky test, which is the whole difference between a runtime claim
; and a declaration.
; -----------------------------------------------------------------------------
assoc_point:
    call assoc_find             ; already claimed?  BX = that row
    jc .fresh
    test byte [bx+3], ASSOC_STICKY
    jnz .out
    jmp short .set
.fresh:
    call assoc_ext_new
    jc .out
.set:
    mov [bx+3], dl              ; ...pointed here, sticky bit CLEAR
.out:
    ret

; assoc_ext_new - claim a free extension row. in: SI -> 3 ext bytes;
; out: CF=0 and BX = the row with the extension written in; CF=1 = full
assoc_ext_new:
    push cx
    mov bx, assoc_ext
    mov cx, ASSOC_NEXT
.row:
    cmp byte [bx], 0
    je .take
    add bx, 4
    loop .row
    stc
    jmp short .out
.take:
    push di                     ; disk.inc's counted copy (SPEC.md 4), which
    mov di, bx                  ; walks DI - and this routine does not
    mov cx, 3                   ; otherwise touch it
    call dsk_ncopy
    pop di
    clc
.out:
    pop cx
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
    call dsk_ncopy              ; disk.inc's counted copy (SPEC.md 4)
    mov bl, dh                  ; clear the side state: a slot reused for a
    xor bh, bh                  ; different program must not inherit the
    mov byte [assoc_drv+bx], 0xFF   ; last one's hint, folder or glyph
    mov byte [assoc_dfold+bx], 0
    push bx
    shl bx, 1
    mov word [assoc_clus+bx], 0
    pop bx
    call assoc_glyph_di
    mov cx, 8
.zap:
    mov byte [di], 0
    inc di
    loop .zap
    mov al, dh
    clc
    jmp short .out
.full:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; assoc_app_get - the app slot a stem names, CREATING one if it has none
; in:  SI -> 8 stem bytes (and assoc_stemb holds them, which assoc_app_new
;      requires - at all three call sites SI IS assoc_stemb)
; out: CF=0 and AL = the slot; CF=1 = every slot is taken
; clobbers: AX (the output), CF - which is both callees' contract unchanged
;
; asc_seed deliberately does NOT come here: it calls assoc_app_of alone, for
; the reason its own comment gives - twelve kernel slots against a thirty-two
; row cache, so seeding must never spend a slot on a program the user has not
; met.  That is the one distinction this entry point must not erase.
assoc_app_get:
    call assoc_app_of
    jnc .out
    call assoc_app_new
.out:
    ret

; assoc_self_glyph - reduce the CALLING package's own embedded icon into an
; app slot. in: DL = the slot, ES = the caller's segment (the X stub put it
; there). Does nothing if the caller ships no icon - the document then draws
; the bare page, which is honest.
assoc_self_glyph:
    push bx
    push si
    push di
    cmp word [es:0], 0x384F     ; 'O','8' - a package header, not a stray
    jne .out
    test byte [es:3], 1         ; the embedded-icon flag (SPEC.md 20.2)
    jz .out
    mov bl, dl
    call assoc_glyph_di
    xor si, si                  ; the image's header, at 0 of its own segment
    call assoc_img_glyph        ; shipped or reduced (SPEC.md 54.3.2)
.out:
    pop di
    pop si
    pop bx
    ret

; asc_use_out - asc_use_x's two refusals share this one near jump: -3.  A global
; rather than a `.local` for the reason its placement forces - it sits after the
; routine above's `ret`, where a NASM local would belong to THAT routine.
asc_use_out:
    jmp asc_use_x.out

; =============================================================================
; ASSOC.DAT - the volume's icon and association cache (SPEC.md 54.7)
;
; The mount reads the first sector of EVERY package in a directory to harvest
; its icon, and mechanism D of docs/plans/completed/DISK-PERF-PLAN.md is the measurement that
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
ASC_ROW      equ 88             ; a VERSION 2 row: stem 8 + size 2 + cluster 2
                                ; + 4 reserved + a 64-byte icon + the 8-byte
                                ; document glyph (SPEC.md 54.3.2)
ASC_ROW1     equ 80             ; ...and a version 1 row, the same without the
                                ; glyph: READ, because an installed volume's
                                ; cache was written once at install and a
                                ; refusal would send that machine back to the
                                ; pre-54.7 harvest in silence; never written
ASC_ROWGLY   equ 80             ; the glyph's offset inside a v2 row. All zero
                                ; = the package ships none: reduce its icon
ASC_ROWCLUS  equ 10             ; the DIRECTORY the program lives in (0 = root),
                                ; taken out of the six bytes the row already
                                ; reserved - so SPEC.md 54.7.1's seeding costs
                                ; the file not one byte
ASC_ROWICO   equ 16             ; ...the icon's offset inside a row
ASC_NAPP     equ 32             ; was 16, and the shipped apps disk holds 15 -
ASC_NEXT     equ 24             ; one package of headroom, with no guard
ASC_KB       equ 3              ; 32*88 + 16 + 24*4 = 2,928 bytes
; ...and that sum is now checked rather than kept up to date by hand. The ext
; rows are FOUR bytes, not ASC_ROW: asc_merge_ext walks them with `add di, 4`,
; which is the one number here that a reader is likely to get wrong, ASC_ROW
; being the app row's stride two lines above. The counts are bounded at load
; (asc_use caps [asc_n] at ASC_NAPP and [asc_ne] at ASC_NEXT), so this is what
; keeps every offset those caps allow inside the claim asc_use asks for.
%if ASC_HDR + ASC_NAPP * ASC_ROW + ASC_NEXT * 4 > ASC_KB * 1024
  %error "the association cache no longer fits its own claim: asc_use claims ASC_KB and mem_claim does not clear the block, so a row past the end is parsed out of whatever the heap handed out last"
%endif
%if ASC_ROW > 255 || ASC_NAPP > 255 || (((ASC_NAPP + ASC_NEXT) * ASC_ROW) + ASC_HDR) > 0xFFFF
  %error "asc_slot_of and asc_merge_ext use the 8-BIT `mul bl` so DX survives (asc_slot_of's header says `clobbers AX, CF`): both operands must fit a byte, and the whole cache must fit one segment"
%endif

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
    jne .load
    cmp byte [ico_n], 0         ; ...AND THE STORE STILL HOLDS IT (SPEC.md
    jne asc_use_out             ; 54.7.4). The stamp used to mean "the claim
                                ; holds this volume's cache" and the claim was
                                ; ours to keep; it means "this volume's bodies
                                ; are in the STORE" now, and the store is
                                ; PURGEABLE - so a shed can empty it while the
                                ; stamp still vouches for it, after which every
                                ; package on the volume goes back to a sector
                                ; read apiece (~400 ms of int 13h each on the
                                ; target machine) against one three-sector
                                ; re-read. Here and not on the shed's own path
                                ; because mem_shed_one's whole protocol is that
                                ; the holder's existing refusal is the notice
                                ; (SPEC.md 50.6), and this IS that refusal
.load:
    mov [asc_vol], al
    xor ax, ax
    mov [asc_n], ax             ; ...and it holds nothing until proven
    mov ax, ASC_KB              ; ALWAYS a fresh claim, and the `cmp [asc_seg],
    mov bx, MEM_K_ASC           ; 0 / jne .haveseg` that used to stand here has
    call mem_claim_x            ; gone with the session-long buffer it guarded:
    jc asc_use_out              ; every exit below runs asc_drop (SPEC.md
    mov [asc_seg], dx           ; 54.7.4), so this word is 0 whenever anything
                                ; reaches here. A refusal is tier 2 - every
                                ; lookup misses and the harvest reads sectors
    push ax                     ; ...and MOVABLE (SPEC.md 66.5.6), which is now
    mov ax, asc_reloc           ; a WITHIN-CALL requirement rather than the
    mov bx, MEM_K_ASC           ; session-long one docs/HEAP-CLAIMS.md measured
    call mem_movable_x          ; acting as a BARRIER: dsk_read_chain reaches
    pop ax                      ; the sector cache's own claims and those can
                                ; compact (SPEC.md 18.95), so the block can
                                ; move between the claim and the free even
                                ; though nothing outside this routine will
                                ; ever see it again
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
    mov si, asc_s_magic         ; 'OS88AC' - the dskw_cmp idiom (diskw.inc):
    xor di, di                  ; cmpsb is DS:SI against ES:DI, and ES is
    mov cx, 6                   ; already the claim
    cld
    repe cmpsb
    jne .out
    mov al, [es:6]              ; THE VERSION SETS THE ROW STRIDE (SPEC.md
                                ; 54.3.2; its DEFERRED TRIM is this whole
                                ; version-1 ladder, dropped when no field
                                ; volume can still carry a v1 cache)
    mov cx, ASC_ROW1            ; 54.3.2): 1 is the 80-byte row an installed
    dec al                      ; volume's cache still has, 2 the 88-byte one
    jz .ver                     ; with the glyph column. Anything else is a
    mov cx, ASC_ROW             ; cache this build does not understand, and
    dec al                      ; that is the 'no cache' answer below
    jnz .out
.ver:
    mov [asc_rowsz], cx
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
    mov bl, [asc_rowsz]
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
    xor ax, ax                  ; short: the clean "no cache" answer, which
    mov [asc_n], ax             ; every lookup already handles as a miss
    mov [asc_ne], ax
    jmp short .out
.parse:
    call asc_merge_ext          ; the declarations this volume knows about...
    call asc_seed               ; ...and WHERE each of them is (SPEC.md 54.7.1),
                                ; what it LOOKS like (54.7.3) and its BODY into
                                ; the machine-wide store (54.7.4), which is the
                                ; last thing in this file anything above still
                                ; wanted. ONE WALK of the rows: it was this
                                ; call and an `asc_absorb` beside it, two loops
                                ; over the same table back to back. Once per
                                ; volume SWITCH, not per mount - the compare
                                ; at the top is the gate - so the reduce is
                                ; paid where a floppy is already turning
.out:
    call asc_drop               ; THE CLAIM IS A FILE BUFFER (SPEC.md 54.7.4)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; asc_seed - teach the hints where this whole DISK's programs are, what they
;            look like, and put their BODIES in the store
;            (SPEC.md 54.7.1 / 54.7.3 / 54.7.4)
; in:  ES = [asc_seg], [asc_n] rows valid, [disk_drive] = this volume
; out: nothing (all registers preserved)
;
; **ONE WALK OF THE ROWS, not two.** `asc_use` called this and then
; `asc_absorb`, two loops over the same table at the same stride from the same
; base, back to back, each with its own frame and row pointer. `asc_row_take`
; below is the absorb's body and this loop calls it; the two halves never read
; what the other writes, so the interleave is free.
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
    call ico_need               ; **THE STORE'S CLAIM, BEFORE THE WALK** (SPEC.md
    mov es, [asc_seg]           ; 54.7.4): `mem_claim` COMPACTS, and this loop
                                ; holds a row OFFSET against a segment a
                                ; compaction moves - so the one call that can
                                ; claim is made with no row pointer live. Its
                                ; carry is deliberately not tested: a refused
                                ; store still has to be SEEDED, and
                                ; `asc_row_take` reads [ico_seg] itself rather
                                ; than retrying the claim per row. `ico_need`
                                ; answers ES = the STORE, so the cache's own
                                ; segment goes straight back
    mov si, ASC_HDR             ; ES:SI walks the app rows
.row:
    push cx
    push si
    call asc_row_take           ; ...THE BODY FIRST, and unconditionally: this
                                ; walk skips a row with no association slot
                                ; and the store must not (SPEC.md 54.7.4)
    mov di, assoc_stemb         ; stage the stem: assoc_app_of reads through DS
    mov cx, 8
    call assoc_stage
    pop di                      ; the row start, banked on the frame above and
    push di                     ; wanted in DI (assoc_app_of clobbers SI)
    mov si, assoc_stemb
    call assoc_app_of           ; AL = the slot, CF=1 = this program has no
    jc .next                    ; association, so there is nothing to point
    mov bl, al
    xor bh, bh
    mov dl, [disk_drive]
    mov [assoc_drv+bx], dl
    mov ax, [es:di+ASC_ROWCLUS]
    shl bx, 1
    mov [assoc_clus+bx], ax
    ; ...and WHAT IT LOOKS LIKE (SPEC.md 54.7.3). The row carries the whole
    ; 64-byte body - and, in a version 2 cache, the glyph the package SHIPS
    ; (54.3.2) - so the glyph costs no read either; without it a DECLARED
    ; extension (54.6) resolved to a slot whose glyph was still the
    ; unresolved sentinel, so its documents drew assoc_compose's BARE PAGE
    ; until the folder the program lives in had been browsed. Every program
    ; on the shipped apps disk is one folder down (19.2) and the documents
    ; are in MEDIA/, so that was every .HTM and .TEX on the disk.
    shr bx, 1                   ; the slot back out of BX, which the `shl`
                                ; above left as a word index and nothing
                                ; since has written
    mov si, di
    add si, ASC_ROWICO          ; the cached body
    call asc_row_glyph          ; ES = [asc_seg], per this routine's contract
.next:
    pop si
    pop cx
    add si, [asc_rowsz]
    loop .row
.out:
    jmp kretc_di

; -----------------------------------------------------------------------------
; asc_row_take - ONE row's body into the machine-wide store (SPEC.md 54.7.4)
; in:  ES:SI -> the row, [asc_rowsz] the stride
; out: nothing (all registers preserved)
;
; **IT WAS A SECOND WALK OF THE SAME TABLE.** `asc_use` called `asc_seed` and
; then `asc_absorb`, two loops over `[asc_n]` rows at `[asc_rowsz]` from
; `ASC_HDR`, back to back, with a whole frame and a row pointer each. They
; are one walk now and this is its second half; the ordering is safe because
; neither half reads what the other writes - the seed puts a row's glyph in
; `assoc_glyph` and this puts the row's own bytes in the store.
;
; **THE CLAIM IS STILL MADE BEFORE THE WALK** and that is load-bearing, not
; tidiness: `mem_claim` compacts on its refusal path, the cache's claim is
; movable, and this walk holds a row OFFSET against the segment a compaction
; moves. `asc_seed` makes it once with no row pointer live; a refusal then
; stays made, because this routine reads `[ico_seg]` rather than asking
; `ico_need` again per row - which would retry the claim, and compact,
; exactly where it must not.
;
; An all-zero data plane is the ICONLESS sentinel (`asc_body_glyph` reads the
; same sixteen words the same way) and spending a row on it would buy the
; generic icon a miss that already draws it.
; -----------------------------------------------------------------------------
asc_row_take:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [ico_seg], 0       ; NO STORE, so nothing to absorb into - and
    je .out                     ; asking `ico_need` again would RETRY the
                                ; claim, which compacts, under a walk holding
                                ; a row offset. The caller made that call once
                                ; with nothing live; this is how a refusal
                                ; stays made
    mov di, si                  ; the ROW, banked: ico_key_stem wants SI on
    add si, ASC_ROWICO + 32     ; the stem and the sentinel test wants the
    mov cx, 16                  ; body's DATA plane
    call asc_inkck
    jz .out
    mov dx, [es:di+8]           ; the size, and the stem beside it, are the two
    mov si, di                  ; halves of the key the LISTING will ask with
    call ico_key_stem
    mov dx, [asc_seg]           ; ...and the body, out of the claim it is
    mov bx, di                  ; still sitting in
    add bx, ASC_ROWICO
    call ico_add                ; a hit is idempotent, so a package on two
    jc .out                     ; mounted volumes takes one row
    cmp byte [asc_rowsz], ASC_ROW1
    je .out                     ; a version 1 row ships no glyph (SPEC.md
    mov si, di                  ; 54.3.2), so the store keeps ico_add's zeros
    add si, ASC_ROWGLY          ; - the unresolved sentinel, which is true
    mov dx, [asc_seg]           ; rather than convenient
    call ico_glyph_put          ; ...and a v2 row's goes WITH the body, which
.out:                           ; is what lets a later hit answer as well as
    jmp kretc_di                ; the seed did (54.7.4)

; -----------------------------------------------------------------------------
; asc_drop - give the file buffer back (module-internal)
; out: nothing (all registers preserved)
;
; Idempotent, and it has to be: .out is reached by the refused claim, the bad
; magic, the version it does not understand, the short file and the "already
; this volume" compare as well as by a clean load.
; -----------------------------------------------------------------------------
asc_drop:
    push ax
    push bx
    push dx
    mov dx, [asc_seg]
    or dx, dx
    jz .out
    xor ax, ax
    mov [asc_seg], ax           ; FIRST, and asc_reloc's own header is why: a
    mov [asc_n], ax             ; segment word naming a block that is gone is
    mov [asc_ne], ax            ; not a missing answer, it is a live wrong one.
                                ; The counts go with it - nothing reads them
                                ; past this point, and leaving a row count
                                ; beside a null segment is a trap for whoever
                                ; adds the next reader
    mov bx, MEM_K_ASC
    call mem_free_x
.out:
    pop dx
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
    push ss                     ; ES = LOW_SEG: dsk_secbuf is .lowbss (SPEC.md
    pop es                      ; 2.1), which is exactly what SS names
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
    cld
    repe cmpsb                  ; the dskw_cmp idiom; pop leaves ZF alone
    pop di
    pop cx
    jne .next
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
    clc
    jmp short .out
.next:
    add di, 32
    loop .entry
    jmp short .sector
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
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
    mov ax, [asc_n]             ; the ext rows begin past the app rows - the
    mov bl, [asc_rowsz]             ; accumulator form and the same 8-bit multiply
    mul bl
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
    push di                     ; stage the extension, then the usual rules -
    mov si, di                  ; the row is in DI here, so it goes in as the
    mov di, assoc_extb          ; SOURCE and the row pointer is banked
    mov cx, 3
    call assoc_stage
    pop di
    mov si, assoc_extb
    cmp byte [si], ' '
    jbe .next
    push di
    call assoc_point            ; a runtime claim outranks a cached one
    pop di
.next:
    pop cx
    add di, 4
    loop .row
.out:
    jmp kretc_di

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
    mov bl, [asc_rowsz]             ; the 8-BIT form: `mul bl` reads AL and BL
    mul bl                      ; only, so AH needs no clearing and DX SURVIVES
    add ax, ASC_HDR             ; - this routine's header says it clobbers AX
    mov si, ax                  ; and CF, and `mul bx` clobbered DX. The row
                                ; index is < ASC_NAPP, so AL*88 <= 2,728 and
                                ; the product cannot leave AX. ES:SI = the row
    mov di, assoc_stemb         ; stage its stem into the kernel
    mov cx, 8
    call assoc_stage
    mov si, assoc_stemb
    call assoc_app_get          ; a full table leaves AL alone and CF set, and
    pop di                      ; this routine's caller reads neither
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; asc_row_glyph - a slot's glyph out of a CACHE ROW (SPEC.md 54.3.2)
; in:  ES:SI -> the row's cached 64-byte icon body (ASC_ROWICO in), BL = the
;      slot, BH = 0
; out: nothing (all registers preserved)
;
; A version 2 row carries the glyph the package SHIPS right after the body,
; and all-zero there means it ships none - so the body is reduced, unless THAT
; is all zero too: the ICONLESS sentinel, on which the glyph is left alone
; rather than un-resolving one another volume resolved (54.7.3). A version 1
; row has no glyph column and goes straight to the body. Both row-shaped
; writers - the seed and a hit - come through here, so they cannot disagree.
; -----------------------------------------------------------------------------
asc_row_glyph:
    cmp byte [asc_rowsz], ASC_ROW1
    jne asc_gly_pair            ; a version 2 row has the pair; a version 1 one
    jmp asc_body_glyph          ; has no glyph column at all, so the body is
                                ; the whole of what is there. Two tail jumps
                                ; and no frame: both callees preserve every
                                ; register and return near

; -----------------------------------------------------------------------------
; asc_gly_pair - a slot's glyph out of a BODY with the SHIPPED eight behind it
; in:  ES:SI -> the 64-byte icon body, BL = the slot, BH = 0
; out: nothing (all registers preserved)
;
; **ONE WALKER FOR TWO STORES.** The shipped glyph, else the body reduced
; (SPEC.md 54.3.2) - and that ladder was written twice, here for an ASSOC.DAT
; row and again in asc_take for SPEC.md 25.9's store row, whose own comment
; said *"asc_row_glyph one routine along does exactly this out of a CACHE
; row"*. It is the same code because it is the same layout: ASC_ROWGLY -
; ASC_ROWICO and ICO_R_GLYPH - ICO_R_BODY are both 64, which disk.inc asserts
; at the point both constants exist.
;
; Getting it wrong is not a missing icon but a WORSE one: the reduction of a
; CRT icon is an empty block, so an arm that reduced where a glyph was shipped
; would DOWNGRADE a slot the seed had already resolved.
; -----------------------------------------------------------------------------
asc_gly_pair:
    push ax
    push cx
    push si
    push di
    call assoc_glyph_di         ; DI -> the slot's glyph
    add si, ASC_ROWGLY - ASC_ROWICO
    call assoc_glyph_take
    jnc .out
    sub si, ASC_ROWGLY - ASC_ROWICO
    call asc_body_glyph         ; ...reduced instead (SPEC.md 54.7.3)
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; asc_body_glyph - reduce a 64-byte icon BODY into a slot's glyph
; in:  ES:SI -> the body, BL = the slot, BH = 0
; out: nothing (all registers preserved)
;
; Split out of asc_row_glyph above so a STORE HIT can reach it. Since SPEC.md
; 54.7.4 the cache's claim is gone by the time a listing asks, and what a hit
; has is the body in SPEC.md 25.9's store - which carries no glyph column, so
; the reduce is the whole of what is there. Nothing is lost for a volume that
; HAS an ASSOC.DAT: asc_seed spends every row's shipped glyph while the claim
; is still up, and it covers the whole VOLUME rather than the folder being
; listed. One body of code either way, so a seeded glyph and a hit's cannot
; disagree about what the ICONLESS sentinel is.
; -----------------------------------------------------------------------------
asc_body_glyph:
    push ax
    push cx
    push si
    push di
    call assoc_glyph_di         ; DI -> the slot's glyph
    add si, 32                  ; the DATA plane
    mov cx, 16
    call asc_inkck              ; an all-zero body is the ICONLESS sentinel:
    jz .out                     ; leave the glyph alone rather than blanking
    call assoc_reduce           ; one another volume resolved
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; asc_take - spend a store hit: the program's location and its glyph
; in:  AL = the store row holding its body, SI -> the staged directory entry
; out: nothing (all registers preserved)
;
; WHAT THE SLOW PATH WOULD HAVE DONE, minus the sector read. A package reached
; through .h_read runs assoc_note_app, which records where it is, merges what
; it declares and takes its icon into its slot's glyph; a package whose body
; is already in the store never reads that sector, so this is the half of it
; that does not need one. The DECLARATION half genuinely does - it lives in
; the header - and is not lost: it was merged the first time this package was
; harvested, or by asc_merge_ext out of ASSOC.DAT, and assoc_ext is resident.
;
; The location comes from the DIRECTORY ENTRY and not from any cache, which is
; what makes a hit safe on a disk that has been swapped: the body is keyed on
; (name, size) and says nothing about where anything is.
;
; IT WAS `asc_lookup` AND `asc_take`, AND THE LOOKUP HAS GONE UNGATED. It read
; the volume's ASSOC.DAT claim for an offset into it; there is no claim to read
; now (SPEC.md 54.7.4), so the question "is this package's body already in RAM"
; is the store's and is asked by ico_have in disk.inc - on both kernels,
; because the store is on both and kern_small's harvest was reading a sector
; per package for a body it already had.
; -----------------------------------------------------------------------------
asc_take_x:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ax                     ; the store row: assoc_stem_of answers in DI
    call assoc_stem_of          ; and assoc_app_get in AL, so it cannot stay
    mov si, di                  ; where it is
    call assoc_app_get          ; AL = the slot, CF=1 = no association, so
    pop cx                      ; there is nothing to point at the glyph
    jc .done
    mov bl, al
    xor bh, bh
    call assoc_hint_here        ; ...it is HERE (SPEC.md 54.4.2 rung 1)
    mov al, cl
    call ico_body               ; ES:DI -> the 64-byte body, with the eight
    jc .done                    ; bytes the package SHIPS behind it. THE STORE
    mov si, di                  ; ROW IS THE CACHE ROW'S LAYOUT, so the shipped
    call asc_gly_pair           ; glyph and its reduce fallback are one walker
.done:                          ; (SPEC.md 54.3.2) and not a second copy here
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Far entries (SPEC.md 2.6): the resident thunks in kernel.asm far-call these,
; and these near-call the bodies above - so every routine here keeps its
; ordinary near `ret`.
; =============================================================================

section .text                   ; DATA, so back to the kernel segment
asc_s_magic: db 'OS88AC'        ; the cache's magic (SPEC.md 54.7). The
                                ; version byte after it used to ride in this
                                ; string; since 54.3.2 asc_use reads it and
                                ; sets [asc_rowsz] from it - 1 and 2 are both
                                ; understood, at different row strides
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
asc_rowsz: dw ASC_ROW           ; the app-row stride of the cache it holds -
                                ; set from the version byte at load (54.3.2)

%endif                          ; OS88_ASSOC - SPEC.md 54.0

section .text                   ; SECTION DISCIPLINE (CLAUDE.md's hard rules):
                                ; the file must leave .text selected on BOTH
                                ; arms, and the gated-out arm switches section
                                ; nowhere - so this is the one line that has
                                ; to sit outside the %ifdef
