; =============================================================================
; os8088 - HDDTOOL.DRV / iassoc.inc
;
; The installed volume's OWN ASSOC.DAT (SPEC.md 52.10.14).
;
; ASSOC.DAT (SPEC.md 54.7) is a per-VOLUME cache, and since SPEC.md 54.7.1 its
; app rows carry the CLUSTER of the folder each program lives in - so it is
; the one file on a system disk that cannot be copied anywhere. An install
; copied it anyway: the SYSTEM phase's job is exactly the hidden + system
; files, and SPEC.md 52.10.12's species test - the one whose comment names
; this hazard in as many words - runs on the APPS phase, which was never going
; to carry it.
;
; What that costs is not a wasted read. The rows survive and point at the
; FLOPPY's folders, so the machine knows `.HTM` opens with BROWSER and looks
; for it at a cluster that means nothing on C:, and SPEC.md 54.4.2's rung 4 -
; the one that would have tried APPS by name - never runs, because
; assoc_dfold is a build-time byte carried by the kernel's own four stems
; alone and a slot created by asc_merge_ext has 0, which assoc_tryfold reads
; as "nothing to try". The user sees `BROWSER.O88 - not on this disk` with
; BROWSER.O88 sitting in C:\APPS.
;
; So the copy skips it (hd_icopy_tree, in the root, beside the KERNEL.SYS
; skip) and this builds the volume its own from what is actually on it.
;
; IT READS THE DESTINATION AND NEVER THE SOURCES. A cache assembled from the
; two floppies' own caches would have to map every source folder cluster onto
; the destination's, and would inherit whatever each source dropped at its own
; cap. Walking C: is one question - what is on this volume, and where - and
; the answer is the file.
;
; SILENT. It runs after the MBR (SPEC.md 52.10.10), so a refused claim or a
; full volume may not turn a finished install into an error: the machine is
; left with NO ASSOC.DAT, which is SPEC.md 54.7's third tier - an absent cache
; answers "miss" to everything and the harvest runs as it did before the file
; existed. That is strictly better than what shipped, the failure being
; removed a cache that is present and WRONG, which those tiers have no row
; for. INSTBNCH.TXT (SPEC.md 52.10.9) set the precedent.
; =============================================================================

; --- the file format, MIRRORED from kernel/assoc.inc (SPEC.md 54.7) ----------
; A driver cannot %include a kernel header, so these are typed out a second
; time - the drivers/saver and drivers/xmem shape - and tests/unit/t_mirror.py
; carries this file for that reason. A drifted ASC_ROW here writes rows the
; kernel parses at the wrong stride, which is a cache that reads as garbage
; rather than an error.
ASC_HDR      equ 16             ; magic 6 + version + napp + next + 7 reserved
ASC_ROW      equ 88             ; stem 8 + size 2 + cluster 2 + 4 + icon 64 +
                                ; the 8-byte document glyph (a VERSION 2 row,
                                ; SPEC.md 54.3.2 - the kernel reads the
                                ; 80-byte version 1 too; this writes only 2)
ASC_ROWCLUS  equ 10             ; the DIRECTORY the program lives in
ASC_ROWICO   equ 16             ; ...the icon's offset inside a row
ASC_ROWGLY   equ 80             ; ...and the glyph's: all zero = ships none
ASC_NAPP     equ 32
ASC_NEXT     equ 24
ASC_KB       equ 3

; --- and this module's own -------------------------------------------------
HIA_EXT      equ ASC_HDR + ASC_NAPP * ASC_ROW  ; the ext rows are STAGED where
                                ; they would land if every app row were used,
                                ; and moved down once the count is known: a
                                ; walk cannot know it in advance, and the move
                                ; is always DOWNWARD so one forward rep movsb
                                ; serves
HIA_SEC      equ ASC_KB * 1024  ; ...and the package-header scratch, past the
                                ; whole body (2,672 bytes at its largest)
HIA_MAXSPC   equ 64             ; sectors per cluster we will read: 32KB, the
                                ; largest any FAT volume this kernel mounts
                                ; uses. Bigger is refused rather than clamped -
                                ; OSAPI_FILE_READ_AT's capacity must be a whole
                                ; number of CLUSTERS (SPEC.md 18.4.4), so there
                                ; is no smaller legal read to fall back to
HIA_DECLMAX  equ 5              ; the header's declaration block (SPEC.md 54.6)
HIA_MINHDR   equ 32             ; the package header itself
HIA_ICOEND   equ 96             ; ...and past its embedded icon
HIA_GLYEND   equ 128            ; ...and past its document glyph (SPEC.md
                                ; 54.3.2), which sits at 112 after the
                                ; association block

; -----------------------------------------------------------------------------
; hd_iassoc - build the destination volume's ASSOC.DAT (SPEC.md 52.10.14)
; in:  [hd_idrv] = the destination volume; it is mounted and the install has
;      finished writing to it
; out: nothing. Every register preserved, CF clear: this CANNOT fail an
;      install (see the header above)
;
; The volume is banked and put back (SPEC.md 51.5.2's rule): the walk below
; moves the machine's current volume and directory a few dozen times, and the
; user was somewhere.
; -----------------------------------------------------------------------------
hd_iassoc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call hd_ivol_bankv          ; ...the V form: hd_isrcdrv is the COPY's and
                                ; this is not a copy
    call hd_ia_build
    call hd_ivol_back
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; hd_ia_build - the body of it, with the volume already banked (internal)
; -----------------------------------------------------------------------------
hd_ia_build:
    mov word [hd_iaseg], 0
    call hd_ia_root
    jc .out                     ; the destination will not answer: nothing to
                                ; do and nothing to say

    ; --- the cluster size, which is what sizes both the read and the claim ---
    ; OSAPI_FILE_READ_AT refuses a capacity that is not a whole number of
    ; clusters (SPEC.md 18.4.4), and OSAPI_FILE_DFREE has always answered the
    ; sectors per cluster - which is the pair the slot's own contract names.
    call OSAPI_FILE_DFREE
    jc .out
    or bx, bx
    jz .out
    cmp bx, HIA_MAXSPC
    ja .out
    mov ax, bx
    mov cl, 9
    shl ax, cl                  ; sectors -> bytes. `shl reg, cl` is the 8086
    mov [hd_iaclb], ax          ; form; `shl reg, imm` other than 1 is 186+

    inc bx                      ; ...and the claim is the body plus one cluster,
    shr bx, 1                   ; rounded up to the KB the slot asks in
    mov ax, bx
    add ax, ASC_KB
    call OSAPI_MEM_CLAIM        ; AX = KB in, DX = the segment out (SPEC.md
    jc .out                     ; 50.3) - and a refusal is a normal path
    mov [hd_iaseg], dx

    call hd_ia_zero
    call hd_ia_walk             ; ...every package on the volume
    call hd_ia_close            ; ...compacted, headed and written
    mov dx, [hd_iaseg]
    call OSAPI_MEM_FREE
    mov word [hd_iaseg], 0
.out:
    clc
    ret

; -----------------------------------------------------------------------------
; hd_ia_root - stand in the DESTINATION's root (module internal)
; out: CF=1 = it would not answer
; clobbers: flags
; -----------------------------------------------------------------------------
hd_ia_root:
    push ax
    push bx
    push dx
    xor dx, dx                  ; cluster 0 IS the root (SPEC.md 19.2.2)
    mov bl, [hd_idrv]
    call OSAPI_FILE_GOTO_Q      ; QUIET: no listing, no harvest (SPEC.md 18.9)
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ia_zero - clear the body and the counters (module internal)
; -----------------------------------------------------------------------------
hd_ia_zero:
    push ax
    push cx
    push di
    push es
    mov es, [hd_iaseg]
    xor di, di
    xor ax, ax
    mov cx, (HIA_EXT + ASC_NEXT * 4 + 1) / 2
    cld
    rep stosw
    mov word [hd_ianf], 0       ; the FRONT index: declaring packages, up
    mov word [hd_ianb], ASC_NAPP ; ...and the BACK one, everything else, down
    mov word [hd_iane], 0
    pop es
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ia_walk - every package in the root and one level of folders (internal)
;
; The root and ONE level, which is the shape tools/os88disk.py's build_assoc
; indexes on the disks this project ships - its group keys are APPS:, GAMES:,
; SYSTEM: and MEDIA: - so a floppy and an installed volume cache the same set.
; Deliberately NOT hd_icopy_tree's full descent: that walk's level stack
; belongs to the copy, no disk in this tree carries a package two folders down,
; and a cache that misses one costs a harvest rather than a wrong answer.
;
; OSAPI_FILE_FIND is STATELESS and the ordinal is all of its state (SPEC.md
; 19.7.1), which is what lets this leave the root mid-walk to go down a folder
; and come back to the next entry with nothing banked but CX.
; -----------------------------------------------------------------------------
hd_ia_walk:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    xor cx, cx
.next:
    push cx
    call hd_ia_root             ; the copy and the descent below both moved us
    pop cx
    jc .out
    push es
    push cs
    pop es
    mov di, hd_iafind
    call OSAPI_FILE_FIND_RAW    ; RAW, and that is the whole of hd_ia_row's
    pop es                      ; key being right - see there
    jc .out                     ; FERR_NOENT: the end of the root
    mov ax, [hd_iafind+14]
    cmp ax, OSAPI_FT_PKG
    je .pkg
    cmp ax, OSAPI_FT_DIR        ; ...and the synthesized '..' is FT_UP, which
    jne .next                   ; is neither
    mov ax, [hd_iafind+16]
    push cx
    call hd_ia_folder
    pop cx
    jmp short .next
.pkg:
    xor dx, dx                  ; a package in the root: cluster 0 is where it
    call hd_ia_row              ; lives, and that is what the row records
    jmp short .next
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
; hd_ia_folder - every package in ONE folder of the destination (internal)
; in:  AX = its first cluster
; clobbers: flags
; -----------------------------------------------------------------------------
hd_ia_folder:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov [hd_iafc], ax
    xor cx, cx
.next:
    push cx
    mov dx, [hd_iafc]
    mov bl, [hd_idrv]
    call OSAPI_FILE_GOTO_Q
    pop cx
    jc .out
    push es
    push cs
    pop es
    mov di, hd_iafind
    call OSAPI_FILE_FIND_RAW
    pop es
    jc .out
    cmp word [hd_iafind+14], OSAPI_FT_PKG
    jne .next
    mov dx, [hd_iafc]
    call hd_ia_row
    jmp short .next
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ia_row - one package: read its header, take a row, take its declarations
; in:  DX = the cluster of the folder it is in (0 = the root); hd_iafind holds
;      its entry, and the machine is STANDING in that folder
; clobbers: flags
; -----------------------------------------------------------------------------
hd_ia_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [hd_iarc], dx           ; banked: OSAPI_FILE_READ_AT answers in DX:AX

    mov si, hd_iafind           ; type 1 is a PACKAGE and not "a file" (SPEC.md
    call hd_io88                ; 19.1), and this is the same second opinion
    jnc .out                    ; hd_iapps_at takes on the name

    ; --- its first cluster, into the scratch --------------------------------
    mov bx, HIA_SEC
    mov cx, [hd_iaclb]
    xor ax, ax
    xor dx, dx
    mov si, hd_iafind
    push es
    mov es, [hd_iaseg]
    call OSAPI_FILE_READ_AT     ; DX:AX = the bytes delivered
    pop es
    jc .out
    or dx, dx
    jz .word
    mov ax, 0xFFFF              ; over 64KB delivered: clamp, because every
    jmp short .got              ; bound below is a word compare
.word:
    cmp ax, HIA_MINHDR
    jb .out                     ; too short to be a header at all
.got:
    mov [hd_iatake], ax

    ; --- is it a package at all? --------------------------------------------
    ; The same three tests build_assoc makes (tools/os88disk.py): 'O8', version
    ; PKG_FMT. Every byte off a disk is hostile (SPEC.md 18.2), and a type-1 entry is
    ; only the loader's opinion of the NAME and the size.
    push es
    mov es, [hd_iaseg]
    cmp word [es:HIA_SEC], 'O8'
    jne .outes
    cmp byte [es:HIA_SEC+2], PKG_FMT
    jne .outes
    mov al, [es:HIA_SEC+3]
    pop es
    mov [hd_iaflg], al

    ; --- front or back? (SPEC.md 52.10.14) ----------------------------------
    ; A DECLARING package goes to the front, because its row is the only thing
    ; that can locate it: the kernel's own four stems carry assoc_dfold and are
    ; found by rung 4 with no row at all, so what THEY lose at the cap is a
    ; cached icon and one quiet mount.
    call hd_ia_ndecl            ; AL = usable declared extensions
    or al, al
    jz .back
    call hd_ia_front
    jmp short .have
.back:
    call hd_ia_back
.have:
    jc .out                     ; the table is full and this one is dropped
    mov [hd_iaix], al

    call hd_ia_emit             ; the 88 bytes
    call hd_ia_decl             ; ...and the extensions that point at them
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.outes:
    pop es
    jmp short .out

; -----------------------------------------------------------------------------
; hd_ia_front / hd_ia_back - take a row index (module internal)
; out: CF=0 and AL = the index; CF=1 = the table is full
; clobbers: AX (the output), CF
;
; The array is filled from BOTH ENDS in one pass, because a driver walking a
; disk cannot sort what it has not read yet and a second pass would be a second
; int 13h per package - ~400 ms each on the field machine (PERFORMANCE.md Part
; 2) against an install's measured 271 ticks (SPEC.md 52.10.9).
;
; When they meet, a further plain package is DROPPED and a further declaring
; one OVERWRITES the topmost plain row. That is safe because no ext row can
; point at a plain row - which is the property the whole scheme rests on, and
; the same one that lets hd_ia_close slide the plain rows down at the end
; without moving any index anything refers to.
; -----------------------------------------------------------------------------
hd_ia_front:
    push bx
    mov ax, [hd_ianf]
    mov bx, [hd_ianb]
    cmp ax, bx
    jb .take
    cmp bx, ASC_NAPP            ; full, and no plain row to evict
    jae .no
    inc bx                      ; evict the lowest plain row: its index IS the
    mov [hd_ianb], bx           ; one this front row is about to take
.take:
    inc word [hd_ianf]
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

hd_ia_back:
    push bx
    mov ax, [hd_ianf]
    mov bx, [hd_ianb]
    cmp ax, bx
    jae .no
    dec bx
    mov [hd_ianb], bx
    mov ax, bx
    pop bx
    clc
    ret
.no:
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; hd_ia_emit - write row [hd_iaix]: stem, size, cluster, icon (internal)
;
; ALL EIGHTY BYTES, every time, because an evicted plain row is not zero.
;
; The size is OSAPI_FILE_FIND_RAW's +18 - what the file OCCUPIES. asc_lookup
; keys on (stem, SIZE) and compares against SPEC.md 19.1's +20, the raw
; directory entry's size; OSAPI_FILE_FIND reports what a compressed file
; EXPANDS to (SPEC.md 20.14.3), and a row carrying that number misses on every
; mount for ever. No shipped .o88 is CZ-wrapped today, so the two cells agree
; and the defect would be invisible - it is the copier's cell for the copier's
; reason.
;
; AN ICONLESS PACKAGE STILL GETS A ROW, holding 64 zero bytes: that is SPEC.md
; 54.7's sentinel, so caching the ABSENCE saves that read too, and asc_seed's
; own ink test leaves such a slot's glyph unresolved rather than blanking one
; another volume resolved.
; -----------------------------------------------------------------------------
hd_ia_emit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [hd_iaseg]
    mov al, [hd_iaix]
    call hd_ia_at               ; DI = the row
    mov si, hd_iafind           ; --- the stem, space-padded to 8
    mov cx, 8
    cld
.stem:
    lodsb
    or al, al
    jz .pad
    cmp al, '.'
    je .pad
    stosb
    loop .stem
    jmp short .size
.pad:
    mov al, ' '
    rep stosb
.size:
    mov al, [hd_iaix]
    call hd_ia_at
    mov ax, [hd_iafind+18]      ; the OCCUPIED size - see the header above
    mov [es:di+8], ax
    mov ax, [hd_iarc]
    mov [es:di+ASC_ROWCLUS], ax
    xor ax, ax
    mov [es:di+12], ax          ; the four bytes still reserved
    mov [es:di+14], ax

    ; --- the icon, or the all-zero sentinel ---------------------------------
    add di, ASC_ROWICO
    mov cx, 32                  ; 64 bytes, as words
    test byte [hd_iaflg], 1
    jz .noico
    cmp word [hd_iatake], HIA_ICOEND
    jb .noico                   ; the header promised one and the file is not
                                ; long enough to hold it: SPEC.md 18.2
    push ds                     ; the claim to itself: scratch -> row
    mov si, HIA_SEC + HIA_MINHDR
    push es
    pop ds
    rep movsw
    pop ds
    jmp short .glyph
.noico:
    xor ax, ax
    rep stosw

    ; --- and the glyph it SHIPS, or zero (SPEC.md 54.3.2) -------------------
    ; DI is past the icon on either arm, which is where a version 2 row keeps
    ; it. Zero is 'ships none': the kernel then reduces the icon, as it did
    ; before the column existed.
.glyph:
    mov cx, 4                   ; 8 bytes, as words
    test byte [hd_iaflg], 0x20
    jz .nogly
    cmp word [hd_iatake], HIA_GLYEND
    jb .nogly                   ; promised and not delivered: SPEC.md 18.2
    push ds
    mov si, HIA_SEC + HIA_ICOEND + 16   ; past the association block
    push es
    pop ds
    rep movsw
    pop ds
    jmp short .out
.nogly:
    xor ax, ax
    rep stosw
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
; hd_ia_at - ES:DI -> row AL of the body (module internal)
; in:  AL = the row, ES = [hd_iaseg]
; out: DI. AX clobbered, every other register preserved
;
; The 8-BIT `mul bl`, asc_slot_of's form and for its reason: the index is under
; ASC_NAPP so AL*88 <= 2,728 and the product cannot leave AX, which leaves DX
; alone.
; -----------------------------------------------------------------------------
hd_ia_at:
    push bx
    mov bl, ASC_ROW
    mul bl
    add ax, ASC_HDR
    mov di, ax
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_ia_dbase - SI = the declaration block's offset in the scratch (internal)
; out: SI; CF=1 = there is no readable block. Every other register preserved
;
; After the header, and after the icon when there is one - build_assoc's own
; arithmetic, and assoc_declare's. The 16 is that routine's "long enough to
; hold a count and something to count", which is the bound a hostile file has
; to clear before anything here indexes past it.
; -----------------------------------------------------------------------------
hd_ia_dbase:
    push ax
    mov si, HIA_SEC + HIA_MINHDR
    mov ax, HIA_MINHDR
    test byte [hd_iaflg], 1
    jz .based
    mov si, HIA_SEC + HIA_ICOEND
    mov ax, HIA_ICOEND
.based:
    test byte [hd_iaflg], 2     ; it does not declare at all
    jz .no
    add ax, 16
    cmp [hd_iatake], ax
    jb .no
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_ia_ndecl - how many USABLE extensions does this package declare?
; out: AL. Every other register preserved
;
; Usable is build_assoc's test: a count inside HIA_DECLMAX, and an entry that
; is neither blank nor 'O88' - the last being what stops a package claiming
; the package extension itself.
; -----------------------------------------------------------------------------
hd_ia_ndecl:
    push bx
    push cx
    push dx
    push si
    push es
    xor dl, dl
    mov es, [hd_iaseg]
    call hd_ia_dbase
    jc .out
    mov cl, [es:si]
    cmp cl, HIA_DECLMAX
    ja .out                     ; a corrupt count declares nothing
    or cl, cl
    jz .out
    xor ch, ch
    inc si
.one:
    call hd_ia_dok
    jc .skip
    inc dl
.skip:
    add si, 3
    loop .one
.out:
    mov al, dl
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; hd_ia_dok - is the 3-byte extension at ES:SI usable? (module internal)
; out: CF=0 = yes. Every register preserved
; -----------------------------------------------------------------------------
hd_ia_dok:
    push ax
    cmp byte [es:si], ' '       ; a blank extension names nothing
    jbe .no
    cmp word [es:si], 'O8'      ; ...and '.O88' is not an association
    jne .ok
    cmp byte [es:si+2], '8'
    je .no
.ok:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; hd_ia_decl - stage this package's ext rows (SPEC.md 54.6, internal)
;
; Staged at HIA_EXT rather than written in place: the ext rows follow the app
; rows and the app COUNT is not known until the walk is over.
; -----------------------------------------------------------------------------
hd_ia_decl:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [hd_iaseg]
    call hd_ia_dbase
    jc .out
    mov cl, [es:si]
    cmp cl, HIA_DECLMAX
    ja .out
    or cl, cl
    jz .out
    xor ch, ch
    inc si
.one:
    cmp word [hd_iane], ASC_NEXT
    jae .out                    ; the ext table is full: the rest are dropped,
                                ; which is assoc_declare's own rule
    call hd_ia_dok
    jc .skip
    mov ax, [hd_iane]
    mov bl, 4
    mul bl
    add ax, HIA_EXT
    mov di, ax
    mov al, [es:si]             ; ES to ES: the scratch to the staging area
    mov [es:di], al
    mov al, [es:si+1]
    mov [es:di+1], al
    mov al, [es:si+2]
    mov [es:di+2], al
    mov al, [hd_iaix]
    mov [es:di+3], al
    inc word [hd_iane]
.skip:
    add si, 3
    loop .one
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hd_ia_close - compact, head, and write the file (module internal)
;
; The plain rows slide DOWN against the declaring ones, which moves no index
; anything refers to (see hd_ia_front), and the staged ext rows follow them.
; Both moves are downward, so one forward rep movsb serves each.
; -----------------------------------------------------------------------------
hd_ia_close:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [hd_iaseg]

    ; --- the file's shape, worked out with DS STILL OURS --------------------
    ; Every name in this driver is DS-relative, so the two moves below borrow
    ; DS inside hd_ia_move and nothing outside them reads the claim through it.
    ; That is not fastidiousness: the first draft of this routine computed
    ; these offsets with DS already pointing at the claim, and read six
    ; counters out of the cache it was building.
    mov ax, ASC_NAPP
    sub ax, [hd_ianb]           ; AX = how many PLAIN rows there are
    mov [hd_ianpl], ax
    add ax, [hd_ianf]
    mov [hd_ianp], ax           ; ...and the app row count is the two halves

    mov bl, ASC_ROW
    mul bl                      ; the 8-bit form: napp <= 32, so 32*88 = 2,816
    add ax, ASC_HDR             ; cannot leave AX (hd_ia_at's reason)
    mov [hd_iaend], ax          ; where the ext rows belong
    mov cx, [hd_iane]
    shl cx, 1
    shl cx, 1
    add ax, cx
    mov [hd_iasz], ax           ; ...and the whole file's size

    cmp word [hd_ianp], 0
    je .out                     ; a volume with no packages on it needs no
                                ; cache, and the format (SPEC.md 52.10.4 step
                                ; 1) means there is never a stale one to clear

    ; --- the plain rows down against the declaring ones ---------------------
    mov cx, [hd_ianpl]
    jcxz .exts
    mov ax, cx
    mov bl, ASC_ROW
    mul bl
    mov cx, ax                  ; the bytes to move
    mov ax, [hd_ianb]
    mul bl
    add ax, ASC_HDR
    mov si, ax
    mov ax, [hd_ianf]
    mul bl
    add ax, ASC_HDR
    mov di, ax
    cmp di, si
    je .exts                    ; they already sit together
    call hd_ia_move
.exts:
    mov cx, [hd_iane]
    shl cx, 1
    shl cx, 1
    jcxz .head
    mov si, HIA_EXT
    mov di, [hd_iaend]
    cmp di, si
    je .head
    call hd_ia_move

    ; --- the header ---------------------------------------------------------
.head:
    xor di, di
    mov si, hd_s_iamagic
    mov cx, 6
    call hd_ia_put
    mov byte [es:6], 2          ; version 2: the rows carry the glyph column
                                ; (SPEC.md 54.3.2)
    mov ax, [hd_ianp]
    mov [es:7], al
    mov ax, [hd_iane]
    mov [es:8], al

    ; --- and onto the volume, hidden + system as it is on a floppy ----------
    call hd_ia_root
    jc .out
    mov si, hd_s_assoc
    xor bx, bx
    mov cx, [hd_iasz]
    xor dx, dx
    call OSAPI_FILE_WRITE_SYS   ; SPEC.md 19.6.1's one door, and this image
                                ; already goes through it for KERNEL.SYS
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
; hd_ia_move - CX bytes from ES:SI to ES:DI, inside the claim (internal)
;
; DS is BORROWED and put straight back, so no caller has to think about it.
; Both of hd_ia_close's moves are DOWNWARD (DI <= SI), which is what makes one
; forward rep movsb correct for each.
; -----------------------------------------------------------------------------
hd_ia_move:
    push ds
    push es
    pop ds
    cld
    rep movsb
    pop ds
    ret

; -----------------------------------------------------------------------------
; hd_ia_put - CX bytes from DS:SI to ES:DI (module internal)
; -----------------------------------------------------------------------------
hd_ia_put:
    push ax
    push cx
    push si
    push di
    cld
.one:
    lodsb
    stosb
    loop .one
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- this module's data, in .text: a driver ships its bss inside its image ---
hd_s_iamagic: db 'OS88AC'       ; ...and the NAME is hd_s_assoc, inst.inc's:
                                ; one flat namespace (CLAUDE.md), and the copy
                                ; skip and the write must not be able to
                                ; disagree about which file this is

hd_iaseg:     dw 0              ; the claim, 0 = none held
hd_iaclb:     dw 0              ; bytes per cluster on the destination
hd_ianf:      dw 0              ; the next FRONT row index (declaring packages)
hd_ianb:      dw ASC_NAPP       ; ...and the next BACK one, filling downwards
hd_ianpl:     dw 0              ; how many of them are PLAIN rows
hd_ianp:      dw 0              ; the app rows the file ends up with
hd_iaend:     dw 0              ; where the ext rows belong once napp is known
hd_iasz:      dw 0              ; ...and the whole file's size
hd_iane:      dw 0              ; ...and the ext rows
hd_iaix:      db 0              ; the row this package took
hd_iaflg:     db 0              ; its header flags
hd_iatake:    dw 0              ; how many bytes of it were delivered
hd_iarc:      dw 0              ; the cluster of the folder it is in
hd_iafc:      dw 0              ; ...the folder hd_ia_folder is walking
hd_iafind:    times OSAPI_FIND_SZ db 0
