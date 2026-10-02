; =============================================================================
; os8088 - desksc.inc - desktop SHORTCUTS (SPEC.md 26.8)
;
; An entry dragged out of a Disk window onto the desktop becomes a shortcut:
; its picture and a caption on a grid cell, selected by a click, opened by a
; double-click or Enter exactly as the Disk window would open it, moved by a
; drag, removed by Delete, a right-click or Locator's File menu - and kept in
; SYSTEM.CFG, so it is there after a reboot. kern_big only (OS88_SHORTCUTS).
;
; WHERE EACH HALF LIVES (SPEC.md 26.8.7):
;
;   .cold    the record lookup, the open by name, the keys and the
;            right-click's popup - what a click needs AT ONCE. The popup was
;            CTRL.DRV's for a cycle and is not: an XT pays 2-3 s to load an
;            image, which is a menu that does not come up
;   .text    the claim's relocation proc, the strings, the badge
;   .modc    CTRL.DRV: CREATING, MOVING and REMOVING an item, a package's
;            OSAPI_DESK_ITEM link, and the SYSTEM.CFG trailer. Every one of
;            them ends in a SYSTEM.CFG write and the writer is already in that
;            image, so the gesture rides the load the save needed anyway
;   .ovl     the boot reader, which runs once and is gone
;
; THE TABLE IS A HEAP CLAIM, never .bss: one claim for every shortcut, grown
; and shrunk a KB at a time (eight records a KB), freed when the last one goes.
; A machine with no shortcuts pays this file's code and four bytes of .bss.
;
; A shortcut is zone index DESK_SC0 + its row, one more ITEM in desk.inc's
; grid (SPEC.md 26.9): its cell is its byte of [desk_zslot], PLACED, and the
; grid's routines draw, hit, highlight and repaint it with the drives. What is
; here is only what is a shortcut's own: the record, the open by name, and the
; gestures and the SYSTEM.CFG trailer CTRL.DRV carries.
; =============================================================================

%ifdef OS88_SHORTCUTS

; --- the record (SPEC.md 26.8.1) - the claim's row AND the file's, verbatim --
SC_R_VOL    equ 0               ;  1: the volume index (0 = A:), 0xFF = free row
SC_R_SLOT   equ 1               ;  1: its byte of [desk_zslot], for the FILE
                                ;     only: CTRL.DRV writes it from the table
                                ;     and the boot reader puts it back
SC_R_KIND   equ 2               ;  1: 0 document, 1 package, 2 folder - the
                                ;     SPEC.md 19.1 type word's low byte. 3 is
                                ;     spare
SC_R_PATH   equ 4               ; 45: the entry's path BELOW THE ROOT,
SC_PATHMAX  equ 45              ;     `APPS\CALC.O88`, NUL - dsk_path's 32-byte
                                ;     folder path less its leading `\`, a `\`
                                ;     and an 8.3 name
SC_R_CAP    equ 49              ; 13: the caption, NUL, decided at the drop
SC_CAPMAX   equ 13
SC_R_HDR    equ 62              ;  2: `db 1, 16`, an icon record's header, so
                                ;     header and body ARE a record and the
                                ;     picture draws straight out of the claim
SC_R_BODY   equ 64              ; 64: the 16x16 picture (SPEC.md 25 body form),
                                ;     as the Disk window had it
SC_REC      equ 128
SC_PERKB    equ 8               ; rows a KB: 8 * 128, exactly. SC_MAX, the
                                ; rows [sc_nrows] never exceeds, is desk.inc's
SC_VER      equ 2               ; the trailer's own version (SPEC.md 26.8.6):
                                ; 2 carries the placed drives' cells
%if SC_PERKB * SC_REC > 1024
  %error "desksc: SC_PERKB records must fit one KB of the claim"
%endif
%if SC_R_BODY - SC_R_HDR != 2 || SC_R_CAP + SC_CAPMAX != SC_R_HDR || SC_R_PATH + SC_PATHMAX != SC_R_CAP || SC_R_BODY + 64 != SC_REC
  %error "desksc: the record's fields must tile it - header against body"
%endif
%if SC_R_KIND != SC_R_SLOT + 1
  %error "desksc: ovl_sc_load reads SC_R_SLOT and SC_R_KIND as one word"
%endif

SC_OP_NEW   equ 0               ; CTRL.DRV's door - CPE_SC, the settings
SC_OP_MOVE  equ 1               ; core's one entry (SPEC.md 2.8.7) - takes
SC_OP_DEL   equ 2               ; these in AH (SPEC.md 26.8.7)
SC_OP_ADD   equ 3               ; a package's link, OSAPI_DESK_ITEM (26.9)
SC_OP_PDEL  equ 4               ; ...and its remove, named by the record

; =============================================================================
section .cold
; =============================================================================

; -----------------------------------------------------------------------------
; sc_row - a zone's record, and whether it is a shortcut that exists
; in:  AL = a shortcut zone (DESK_SC0..)
; out: CF = 0 it is, ES:BX = its record; CF = 1 no claim, past the claim or a
;      free row
; clobbers: ES, BX, flags
; -----------------------------------------------------------------------------
sc_row:
    push ax
    sub al, DESK_SC0
    cmp al, [sc_nrows]
    cmc                         ; CF = 1 iff past the claim
    jc .out
    mov ah, SC_REC
    mul ah
    mov bx, ax
    mov es, [sc_seg]
    cmp byte [es:bx+SC_R_VOL], 0xFF
    cmc                         ; CF = 1 iff the free mark
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_open - open what shortcut AL points at, as its Disk window would
;           (SPEC.md 26.8.5); NO lock held
;
; Banks the user's volume, walks the path from the root one component at a
; time - dskw_stat, then into it with fcp_goto - and does what the entry IS
; NOW: a folder is fm_choose's, a package OSAPI_PKG_START's by-name arm, a
; document that slot's document arm. Anything missing on the way - no disk, a
; component gone, a FILE where a folder was - is ONE toast naming the drive:
; to whoever double-clicked, those are the same thing.
; -----------------------------------------------------------------------------
sc_open:
    call sc_row                 ; ES:BX = the record
    mov ax, [es:bx+SC_R_VOL]    ; AL = the volume...
    mov ah, [es:bx+SC_R_KIND]   ; ...AH = the kind
    xchg ax, bp                 ; BP holds both for the walk
    lea di, [bx+SC_R_PATH]
    call drv_vol_bank
    xor bx, bx                  ; the volume's ROOT first
.dir:
    mov ax, bp
    call fcp_goto               ; CF = no disk, or no such folder
    jc .nf
    mov es, [sc_seg]            ; re-read: a mount may compact the heap
    mov si, assoc_doc
    mov cx, SC_CAPMAX
.cc:
    mov al, [es:di]
    inc di
    cmp al, '\'
    je .sep
    mov [si], al
    inc si
    or al, al
    loopnz .cc
    xor di, di                  ; the END: this component is the entry itself
.sep:
    mov byte [si], 0
    mov si, assoc_doc
    call dskw_stat_x            ; AL = attribute, BX = first cluster
    jc .nf
    test al, 0x10
    jz .file
    or di, di
    jnz .dir                    ; a folder on the way: into it
    call drv_vol_back           ; the ENTRY is a folder: a Disk window there,
    mov ax, bp                  ; fronted, opened, or the frontmost moved at
    mov dx, bx                  ; the cap (SPEC.md 22.1)
    jmp fm_choose
.file:
    or di, di
    jnz .nf                     ; a FILE where a folder was
    xor dx, dx
    mov es, dx                  ; a PACKAGE: SI = its name, nothing through
    mov ax, bp                  ; ES:DI (SPEC.md 21.5)
    cmp ah, 1
    je .run
    mov ax, [dsk_cwd]           ; a DOCUMENT: the locator is the name, the
    mov [assoc_dclus], ax       ; directory it is in and the volume
    mov ax, bp
    mov [assoc_ddrv], al
    push ds
    pop es
    mov di, assoc_doc
    mov si, sc_nul              ; ...and NO name: its extension picks the
.run:                           ; program, which is the double-click
    xor cx, cx
    call COLD_SEG:ldf_ld_pkg_start
    jnc .back
    call ld_say_status          ; the Disk window's verdicts (SPEC.md 22.9)
.back:
    jmp drv_vol_back
.nf:
    mov ax, bp                  ; `Shortcut not found (B:)`, the letter
    add al, 'A'                 ; patched in place
    mov [sc_s_nf+20], al
    call drv_vol_back           ; the volume FIRST, then the toast - putting
    push ds                     ; it back touches the floppy
    pop es
    mov si, sc_s_nf
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    ret

; -----------------------------------------------------------------------------
; sc_modcall - run one of CTRL.DRV's desktop operations (SPEC.md 26.8.7)
; in:  AH = SC_OP_*; AL, CX, DX = the operation's own arguments; the gfx
;      lock held
; out: AL = the operation's answer; a module that cannot be read says
;      `Needs Sys Disk A:` and leaves AL as it came
; clobbers: whatever the operation clobbers
;
; THE SETTINGS CORE, AND ONLY THE CORE (SPEC.md 2.8.7). Every operation is
; CPE_SC, which lives in CTRL.DRV's first MODS_SIZE bytes, so a desktop gesture
; reads those and not the panel behind them: MOD_SETS is the same file, cut
; short. A panel that is OPEN already holds the whole image, entry and all at
; the same offset, so then the gesture runs in that copy and leaves it loaded
; for the panel; otherwise the core comes in, runs, and goes.
; -----------------------------------------------------------------------------
sc_modcall:
    push bx                     ; mod_live answers a row in BX
    push ax
    mov al, MOD_CTRL
    call mod_live               ; CF = 0: a Control Panel is open
    pop ax
    jnc .full
    push ax
    mov al, MOD_SETS
    call mod_need
    pop ax
    jc .no
    call far [SETFP]            ; the core's one slot, CPE_SC
    push ax
    mov al, MOD_SETS
    call mod_drop
    pop ax
    pop bx
    ret
.full:
    call far [CPFP + CPE_SC * 4]
    pop bx
    ret
.no:
    pop bx
    jmp hbf_nodisk              ; the panel's own sentence (SPEC.md 2.8.4)

; -----------------------------------------------------------------------------
; desk_key - Enter and Delete on the desktop's selection (SPEC.md 26.8.5)
; in:  AX = the keystroke (AL = ASCII, AH = scan); no lock held
; out: CF = 1 the desktop took it; CF = 0 it goes where keys always went,
;      EVERY register as it came - AX included, which an earlier version
;      handed on to the front window holding the selection's zone
;
; Only while LOCATOR owns the bar - a click on the bare desktop is what hands
; it the bar (SPEC.md 12) - so a window the user went back to gets its keys.
; Enter opens ANY item, drives included - it is the double-click. Delete is
; Remove, which CTRL.DRV refuses for anything that is not a live shortcut;
; Locator's File > Remove Shortcut arrives here as a Delete (ui_cmd).
; -----------------------------------------------------------------------------
desk_key_x:
    cmp word [menu_win], 0
    jne .no
    xchg ax, bx                 ; BX = the key
    mov al, [desk_sel]
    cmp al, 0xFF
    je .pass
    cmp bl, 13
    je .enter
    cmp al, DESK_SC0            ; Delete has nothing to remove on a drive or
    jb .pass                    ; the service item: no module load to refuse
    cmp bh, 0x53
    jne .pass
    call ct_cw_gfx_lock
    jmp sc_rclick_x.del
.enter:
    call desk_open
    stc
    retf
.pass:
    xchg ax, bx
.no:
    clc
    retf

; -----------------------------------------------------------------------------
; sc_rclick - a right press on the bare desktop (ui_rdown, no lock held)
; in:  CX, DX = the press
; out: CF = 1; clobbers what ui_rdown may (AX..BP, its own header)
;
; On a shortcut: select it and pop its one command (SPEC.md 26.8.5) - which
; IS Locator's File item, kept current by desk_select, so the popup and the
; menu cannot disagree. A drive or the service item has no command to offer.
; -----------------------------------------------------------------------------
sc_rclick_x:
    call desk_hit               ; AL = the zone under it...
    jc .out
    cmp al, DESK_SC0            ; ...and only a SHORTCUT has the command
    jb .out
    call desk_select
    call ct_cw_gfx_lock
    push ax
    mov cx, di
    mov dx, si
    mov bx, menu_items_file + 2
    mov ax, 1
    call KERNEL_SEG:cw_menu_popup
    pop ax                      ; `pop` writes no flag: the pick's CF stands
    jc .u
.del:
    mov ah, SC_OP_DEL
    call sc_modcall
.u:
    call ct_cw_gfx_unlock
.out:
    stc
    retf

; =============================================================================
section .text
; =============================================================================

; sc_reloc - the compactor moved the table (DX = its new base). In .text,
; called through the kernel's segment (ico_reloc's note). No compare against
; the old base: the table is the one claim this proc is registered on - the
; trailer's claim (sc_m_ser) is made PINNED, with none.
sc_reloc:
    mov [sc_seg], dx
    ret

; --- the File menu's item and the right-click's, ONE string ------------------
; desk_select points item 1 at the MENU_DIS byte or past it, the Close Window
; idiom (menu.inc), so the greyed twin costs one byte and not a second string.
sc_s_remx   db MENU_DIS
sc_s_rem    db 'Remove Shortcut'    ; NO NUL OF ITS OWN: it ends on sc_nul's,
sc_nul      db 0                ; ...which is also an EMPTY name, for a
                                ; document's open. Nothing may go between
sc_s_nf     db 'Shortcut not found (?:)', 0   ; 23: exactly a toast
sc_bdg      db 1, 10            ; the badge: 10x10, one word across - a box
            times 10 dw 0xFFC0  ; the mask: the whole box white
            dw 0xFFC0, 0x8040, 0x8740, 0x8340, 0x8540   ; the box, and an arrow
            dw 0xB840, 0xA040, 0xA040, 0x8040, 0xFFC0   ; curving up and right

; =============================================================================
section .bss
; =============================================================================
sc_seg      resw 1              ; the table's claim, 0 = no shortcuts
sc_nrows    resb 1              ; rows the claim holds, 0..SC_MAX
sc_vol      resb 1              ; sc_open: the volume, across its walk

; =============================================================================
section .modc                   ; CTRL.DRV (SPEC.md 26.8.7): CS = the image,
; =============================================================================
; DS = KERNEL_SEG, out through far entries only - and the image's own data,
; strings and scratch alike, read through `cs:`. Every routine here runs with
; the gfx lock HELD (sc_modcall's contract).

modc_e_sc:                      ; CPE_SC (SPEC.md 2.8.7): AH = the operation
    push bp                     ; wm_cbd takes its routine in BP
    or ah, ah
    jz .new
    dec ah
    jz .move
    dec ah
    jz .del
    dec ah
    jnz .pdel
    call sc_m_add
    jmp short .out
.pdel:
    call sc_m_pdel
    jmp short .out
.del:
    call sc_m_del
    jmp short .out
.new:
    call sc_m_new
    jmp short .out
.move:
    call sc_m_move
.out:
    pop bp                      ; AL is the operation's answer (sc_m_add);
    retf                        ; sc_modcall decides whether the image goes

; sc_m_cold - call the kernel's cold routine at BP (wm_cbd), near from here:
; one far entry the kernel already has, where a resident stub per routine
; was four bytes each
sc_m_cold:
    call COLD_SEG:wm_cbd
    ret

; -----------------------------------------------------------------------------
; sc_m_new - SC_OP_NEW: a Disk window's entry dropped on the bare desktop
; in:  CX, DX = where the button was let go - fm_dgdrop's reading, taken
;      BEFORE this image was loaded: read here, after a floppy load, it was
;      wherever the hand had gone in the meantime, and the shortcut landed
;      there and not under the drop; and the drag's own state: [fm_dgwin]/
;      [fm_dgidx] the entry, [fcp_cb*] its identity (the Cut fm_drag armed,
;      which the caller has disarmed)
; out: nothing; a new shortcut on the nearest free cell and SYSTEM.CFG written,
;      or a toast saying why not
;
; THE RECORD IS FILLED BEFORE ANY I/O. dsk_ico is a shared scratch (disk.inc)
; and the claim may move across a mount, so the body is taken first and every
; access after the first far call re-reads [sc_seg] through sc_m_off.
; -----------------------------------------------------------------------------
sc_m_new:
    cmp dx, MBAR_H              ; not the bar, not the dock, not another
    jb .ret                     ; display: those are not the desktop, and a
    cmp dx, [vid_dock_y0]       ; drop there does what it always did -
    jae .ret                    ; nothing
    cmp cx, [vid_band_x0]
    jb .ret
    cmp cx, [vid_band_xe]
    jae .ret
    push cx
    push dx
    call sc_m_row               ; AL = a free zone
    pop dx
    pop cx
    jc .ret                     ; ...and the user has been told why not
    mov [cs:sc_mz], al
    mov bl, 0xFF                ; no row to leave out of the search
    call sc_m_cell              ; [cs:sc_bc] = the nearest free cell
    jc .noroom
    mov bx, [fm_dgwin]          ; the picture, out of the window the drag
    mov bp, fm_vp_set           ; began in - the one fm_drag's index is into
    call sc_m_cold
    mov ax, [fm_dgidx]
    mov bp, fmv_get_icon
    call sc_m_cold              ; dsk_ico = its 64-byte body
    mov al, [cs:sc_mz]
    call sc_m_off
    mov al, [fcp_cbtype]
    cmp al, 2
    jbe .kind
    mov al, 2
.kind:
    mov [es:di+SC_R_KIND], al
    push di
    add di, SC_R_CAP            ; CAPTION, HEADER and BODY are contiguous, so
    cld                         ; the three copies run on
    mov si, fcp_cbname
    mov cx, SC_CAPMAX
    rep movsb                   ; the name, as the caption until 26.8.4 says
    mov ax, 1 | (16 << 8)       ; otherwise...
    stosw                       ; ...the record header...
    mov si, dsk_ico
    mov cx, 32
    rep movsw                   ; ...and the picture
    pop di
    mov al, [fcp_cbdrv]
    mov [es:di+SC_R_VOL], al    ; LIVE from here: nothing can paint it before
                                ; this returns, the lock being ours

    ; --- the path, and a package's own name: standing in the entry's folder -
    call COLD_SEG:drvf_drv_vol_bank
    mov al, [fcp_cbdrv]
    mov bx, [fcp_cbcwd]
    call COLD_SEG:fcpf_fcp_goto
    jc .bad
    mov al, [cs:sc_mz]
    call sc_m_off
    add di, SC_R_PATH - 1       ; ONE BEFORE THE FIELD, so dsk_path's leading
    mov cx, SC_PATHMAX + 1 - SC_CAPMAX  ; `\` lands on the spare byte and
    call COLD_SEG:dsk_path_x    ; the field is the path BELOW THE ROOT. The
    jc .bad                     ; folder's REAL path, which a window's
    add di, cx                  ; FS_PATH is not when it was opened part-way
    cmp cx, 1                   ; down (it records navigation). CX = its
    je .nm                      ; length, so ES:DI = its NUL - and the root
                                ; answers `\`, which is no folder at all
    mov byte [es:di], '\'
    inc di
.nm:
    mov si, fcp_cbname
    mov cx, SC_CAPMAX
    rep movsb                   ; `APPS\CALC.O88` (SPEC.md 26.8.1)
    mov al, [cs:sc_mz]
    call sc_m_off
    cmp byte [es:di+SC_R_KIND], 1
    jne .capx
    mov si, fcp_cbname          ; a PACKAGE is captioned with its header name
    call COLD_SEG:dwf_dskw_stat ; when that fits the cell (SPEC.md 26.8.4):
    jc .stem                    ; one sector, inside a gesture that is about
    mov ax, bx                  ; to spend several on the write
    mov cx, 32
    mov bp, dsk_peek_x
    call sc_m_cold     ; ES:BX = the header, AX = what arrived
    jc .stem
    cmp ax, 32
    jb .stem
    cmp word [es:bx], 0x384F    ; 'O8': a package, and not a file that is
    jne .stem                   ; merely named like one
    lea si, [bx+16]
    xor cx, cx
.nlen:
    cmp byte [es:si], 0
    je .nend
    inc si
    inc cx
    cmp cx, 16
    jb .nlen
.nend:
    jcxz .stem
    cmp cx, SC_CAPMAX - 1
    ja .stem                    ; longer than the cell: the stem instead
    inc cx                      ; ...with its NUL
    lea si, [bx+16]
    mov al, [cs:sc_mz]
    push ds                     ; the kernel's DS, for after
    push es                     ; the header's segment
    call sc_m_off               ; ES:DI = the record
    pop ds                      ; DS = the header's segment
    add di, SC_R_CAP
    rep movsb
    pop ds
    jmp short .capx
.stem:
    mov al, [cs:sc_mz]          ; the 8.3 name up to its dot - CALC.O88 is
    call sc_m_off               ; CALC, which a desktop should say even where
    add di, SC_R_CAP            ; the Disk window's listing would not
.st:
    mov al, [es:di]
    or al, al
    jz .capx
    cmp al, '.'
    je .cut
    inc di
    jmp short .st
.cut:
    mov byte [es:di], 0
.capx:                          ; the caption is measured at paint, as every
    jmp short .done             ; item's is (SPEC.md 26.9)
.bad:
    mov al, [cs:sc_mz]          ; the folder is gone or its path will not fit
    call sc_m_off
    mov byte [es:di+SC_R_VOL], 0xFF
    call COLD_SEG:drvf_drv_vol_back
    mov si, sc_s_path
    jmp sc_m_say
.done:
    call COLD_SEG:drvf_drv_vol_back
    mov bl, [cs:sc_mz]          ; its cell, PLACED, in the one table (SPEC.md
    xor bh, bh                  ; 26.9) - and posted, so ui_task paints it
    mov al, [cs:sc_bc]
    mov bp, desk_cmark
    call sc_m_cold
    or al, DSL_PIN
    mov [bx+desk_zslot], al
    jmp sc_m_save
.noroom:
    mov si, sc_s_room
    jmp sc_m_say
.ret:
    ret

; -----------------------------------------------------------------------------
; sc_m_add - SC_OP_ADD: a PACKAGE's link, through OSAPI_DESK_ITEM (SPEC.md 26.9)
; in:  DX:CX = the caller's SC_REC record
; out: AL = its zone, or 0xFF refused (and said, where there is a sentence)
;
; THE RECORD IS HOSTILE: it is the caller's bytes. Both strings are
; terminated whatever it said, the kind is bounded, the picture's header is
; stamped, and a volume no row can have is refused - sc_open would otherwise
; walk a path on a drive that is not there, which says "not found", but the
; file would keep it for ever.
; -----------------------------------------------------------------------------
sc_m_add:
    push dx
    push cx
    call sc_m_row               ; AL = a free zone, the claim grown if need be
    pop si
    pop dx
    jc .no                      ; ...and the user has been told why not
    mov [cs:sc_mz], al
    call sc_m_off               ; ES:DI = the row
    push ds
    mov ds, dx
    mov cx, SC_REC / 2
    cld
    rep movsw
    pop ds
    sub di, SC_REC
    mov word [es:di+SC_R_HDR], 1 | (16 << 8)    ; the picture's header is ours
    mov byte [es:di+SC_R_PATH+SC_PATHMAX-1], 0
    mov byte [es:di+SC_R_CAP+SC_CAPMAX-1], 0
    cmp byte [es:di+SC_R_KIND], 2
    ja .bad
    cmp byte [es:di+SC_R_VOL], DVOL_MAX
    jae .bad
    mov bp, desk_free           ; a full desktop refuses, rather than keep
    call sc_m_cold              ; a link nobody can see
    jc .full
    mov bl, [cs:sc_mz]
    xor bh, bh
    mov al, [es:di+SC_R_SLOT]   ; THE CELL IT ASKED FOR, as a wish: the
    or al, DSL_PIN | DSL_GONE   ; reflow in sc_m_save gives it that cell if it
    mov [bx+desk_zslot], al     ; can and the first free one if not (SPEC.md
    push bx                     ; 26.9) - 0xFF stays 0xFF, "any"
    call sc_m_save
    pop ax                      ; AL = the zone
    ret
.full:
    mov si, sc_s_room
    call sc_m_say
.bad:
    mov al, [cs:sc_mz]          ; the row back, unused
    call sc_m_off
    mov byte [es:di+SC_R_VOL], 0xFF
.no:
    mov al, 0xFF
    ret

; sc_m_mem - the table's claims, one door: AX = KB (0 = free the claim at DX),
; DX = the claim (0 = a new one), CX = a new claim's relocation proc (0 = pin
; it). CF = 0 and DX = the claim now; CF = 1 refused, the old one as it was.
sc_m_mem:
    push ax
    push bx
    push bp
    mov bx, MEM_K_SC
    or ax, ax
    jz .free
    or dx, dx
    jnz .regrow
    call COLD_SEG:mmf_mem_claim
    jc .out
    jcxz .out                   ; CF is the claim's 0: a pinned claim
    mov ax, cx
    call COLD_SEG:mmf_mem_movable   ; a refusal only PINS it
    clc
    jmp short .out
.regrow:
    mov bp, mem_regrow
    call sc_m_cold
    jmp short .out
.free:
    call COLD_SEG:mmf_mem_free
.out:
    pop bp
    pop bx
    pop ax
    ret

; sc_m_say - toast one of THIS image's strings, at CS:SI
sc_m_say:
    push cs
    pop es
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    ret

; sc_m_off - AL = a zone -> ES:DI = its record. Preserves AX.
sc_m_off:
    push ax
    sub al, DESK_SC0
    mov ah, SC_REC
    mul ah
    mov di, ax
    mov es, [sc_seg]
    pop ax
    ret

; sc_m_save - SYSTEM.CFG, NOW: a shortcut is meant to survive a power-off, so
; it does not wait for the panel to close. cp_flush_cfg is the panel's own
; spend, the core's half of it - it writes, says so, and keeps [cp_wdirty] on
; a failure so the next close or Restart tries again (SPEC.md 59.6).
sc_m_save:
    mov bp, desk_reflow
    call sc_m_cold ; ...after the flowing items pack round what
    mov byte [cp_wdirty], 1     ; changed (SPEC.md 26.9)
    jmp cp_flush_cfg

; -----------------------------------------------------------------------------
; sc_m_kb - the table's claim, to AL KB (0 = none), with its bookkeeping
; out: CF = 0 [sc_seg]/[sc_nrows] describe it and the rows a grow added are
;      marked free; CF = 1 refused, nothing changed
; -----------------------------------------------------------------------------
sc_m_kb:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    xor ah, ah
    mov dx, [sc_seg]
    mov cx, sc_reloc            ; a NEW claim moves with its proc
    call sc_m_mem      
    jc .out
    or ax, ax
    jnz .live
    xor dx, dx                  ; freed
.live:
    mov [sc_seg], dx
    mov cl, 3
    shl ax, cl                  ; the rows it holds...
    cmp ax, SC_MAX
    jbe .cap
    mov al, SC_MAX              ; ...and never more than SC_MAX
.cap:
    mov ch, [sc_nrows]          ; CH = the first row a grow adds
    mov [sc_nrows], al
.mark:
    cmp ch, al
    jae .ok
    push ax
    mov al, ch
    add al, DESK_SC0
    call sc_m_off
    mov byte [es:di+SC_R_VOL], 0xFF
    pop ax
    inc ch
    jmp short .mark
.ok:
    clc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; sc_m_row - a free record, growing the claim a KB if every row is taken
; out: CF = 0 and AL = its zone; CF = 1 refused, and said
; -----------------------------------------------------------------------------
sc_m_row:
    mov al, DESK_SC0
.s:
    mov ah, al
    sub ah, DESK_SC0
    cmp ah, [sc_nrows]
    jae .grow
    call sc_m_off
    cmp byte [es:di+SC_R_VOL], 0xFF
    je .got                     ; equal: CF is already 0
    inc ax
    jmp short .s
.grow:
    cmp ah, SC_MAX
    jae .full
    push ax
    mov al, ah
    shr al, 1
    shr al, 1
    shr al, 1
    inc ax                      ; a KB more than the rows in use need
    call sc_m_kb
    pop ax
    jnc .s
    mov si, fm_s_nomem          ; the Disk window's own words
    push ds
    pop es
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    stc
    ret
.full:
    mov si, sc_s_many
    call sc_m_say
    stc
.got:
    ret

; -----------------------------------------------------------------------------
; sc_m_cell - the free cell NEAREST a point (SPEC.md 26.9)
; in:  CX, DX = the point; BL = a zone to treat as absent (its own cell is
;      free to it, which is what a move wants), 0xFF for none
; out: CF = 0 and [cs:sc_bc] = the cell; CF = 1 every cell is taken
; clobbers: AX, BX, CX, DX, flags
;
; Nearest by the cell's CENTRE, in city-block pixels: the user never has to
; aim at an edge, and a drop on a taken cell lands beside it. A cell a FLOWING
; drive is in is taken, like any other: the drive does not get pushed out of
; the way, it packs round whatever lands next to it.
; -----------------------------------------------------------------------------
sc_m_cell:
    mov [cs:sc_px], cx
    mov [cs:sc_py], dx
    mov [cs:sc_ex], bl
    mov word [cs:sc_best], 0xFFFF
    xor bx, bx                  ; BL = the cell
.cell:
    cmp bl, [desk_ncell]
    jae .end
    mov al, bl
    mov bp, desk_owner
    call sc_m_cold    ; CF = 1 free, else AH = the zone in it
    jc .free
    cmp ah, [cs:sc_ex]
    jne .next
.free:
    mov al, bl
    mov bp, desk_cell_xy
    call sc_m_cold    ; AX = left, DX = top
    add ax, DESK_CW / 2
    sub ax, [cs:sc_px]
    jns .xa
    neg ax
.xa:
    xchg ax, dx                 ; DX = the x distance, AX = the top
    mov cx, [desk_zh1]
    shr cx, 1
    add ax, cx
    sub ax, [cs:sc_py]
    jns .ya
    neg ax
.ya:
    add ax, dx
    cmp ax, [cs:sc_best]
    jae .next
    mov [cs:sc_best], ax
    mov [cs:sc_bc], bl
.next:
    inc bx
    jmp short .cell
.end:
    cmp word [cs:sc_best], 0xFFFF
    je .none
    clc
    ret
.none:
    stc
    ret

; -----------------------------------------------------------------------------
; sc_m_move - SC_OP_MOVE: ANY item dragged past the threshold (SPEC.md 26.9.5)
; in:  AL = its zone; the button still down
;
; fm_drag's tracking loop and its outline (fm_dgxor), the outline erased
; before the lock may drop, and the drop going to the nearest free cell under
; the pointer - its own cell counting as free, so a short drag stays put.
; -----------------------------------------------------------------------------
sc_m_move:
    mov [cs:sc_mz], al
    mov bp, fm_dgmove
    call sc_m_cold
    mov bp, fm_dgxor
    call sc_m_cold    ; the outline is up from here on
.track:
    mov di, ui_ev               ; THE RELEASE IS AN EVENT, and the mouse ISR
    call KERNEL_SEG:cw_evq_mup  ; queued it with the point it happened at -
    jnc .drop                   ; which matters most HERE, because the hand
                                ; drags WHILE this image is read off the
                                ; floppy and has often let go before the
                                ; first pass of this loop (fm_drag's .track
                                ; drains the same way, wake bookkeeping and
                                ; all, SPEC.md 74.1.1)
    test byte [mouse_btn], 1    ; ...and the level, should the event have
    jnz .held                   ; been lost: then the pointer NOW is all
    mov ax, [mouse_x]           ; there is
    mov [ui_ev+EV_A], ax
    mov ax, [mouse_y]
    mov [ui_ev+EV_B], ax
    jmp short .drop
.held:
    mov bp, fm_dgxor
    call sc_m_cold    ; erase at the old place...
    call KERNEL_SEG:cw_gfx_unlock   ; ...and only then may the lock drop
    call KERNEL_SEG:cw_task_yield
    call KERNEL_SEG:cw_gfx_lock
    mov bp, fm_dgmove
    call sc_m_cold
    mov bp, fm_dgxor
    call sc_m_cold
    mov ax, [ticks]             ; held with the outline lit (fm_drag's linger)
.l:
    call KERNEL_SEG:cw_task_yield
    cmp ax, [ticks]
    je .l
    jmp short .track
.drop:
    mov bp, fm_dgxor
    call sc_m_cold
    mov cx, [ui_ev+EV_A]        ; WHERE IT WAS LET GO, not where the pointer
    mov dx, [ui_ev+EV_B]        ; has wandered since (SPEC.md 26.9.5)
    mov bl, [cs:sc_mz]          ; its own cell is free to it
    call sc_m_cell
    jc .ret
    mov bl, [cs:sc_mz]
    xor bh, bh
    mov al, [bx+desk_zslot]
    and al, DSL_SLOT
    cmp al, [cs:sc_bc]
    je .ret                     ; it went nowhere: nothing to write
    mov bp, desk_cmark
    call sc_m_cold    ; the old cell, bare desktop or the next
    mov al, [cs:sc_bc]          ; drive to pack into it...
    mov bp, desk_cmark
    call sc_m_cold    ; ...and the new one
    or al, DSL_PIN              ; PLACED, whatever it is: a drive dragged is
    mov [bx+desk_zslot], al     ; where the user put it (SPEC.md 26.9)
    jmp sc_m_save
.ret:
    ret

; -----------------------------------------------------------------------------
; sc_m_pdel - SC_OP_PDEL: a PACKAGE's OSAPI_DESK_ITEM remove (SPEC.md 26.9.6)
; in:  AL = the zone its add answered; DX:CX = the caller's SC_REC record
; out: AL = 0 removed, 0xFF refused
;
; A ZONE IS NOT AN IDENTITY: the row a user's Delete frees is the next one a
; drag-out gets, and the boot reader compacts them. So the remove names the
; LINK - the row goes only while its volume and path are still the record's,
; and a shortcut the user made in that zone since is nobody's to take.
; -----------------------------------------------------------------------------
sc_m_pdel:
    mov ah, al
    sub ah, DESK_SC0
    cmp ah, [sc_nrows]
    jae .no
    call sc_m_off               ; ES:DI = the row; AL is kept
    push ds
    mov ds, dx
    mov si, cx                  ; DS:SI = the caller's record
    mov ah, [si+SC_R_VOL]
    cmp ah, [es:di+SC_R_VOL]
    jne .ne
    mov cx, SC_PATHMAX - 1      ; the row's own path ends by here
.c:
    mov ah, [si+SC_R_PATH]
    cmp ah, [es:di+SC_R_PATH]
    jne .ne
    inc si
    inc di
    or ah, ah
    loopnz .c                   ; to the NUL, or the last byte a row keeps
    xor cx, cx                  ; ZF = 1: either way, the same path
.ne:
    pop ds
    je sc_m_del
.no:
    mov al, 0xFF
    ret

; -----------------------------------------------------------------------------
; sc_m_del - SC_OP_DEL: Remove Shortcut from any of its three routes, and a
;            package's OSAPI_DESK_ITEM remove (sc_m_pdel's, once it matched)
; in:  AL = its zone
; out: AL = 0 removed, 0xFF there was no link in it (only sc_m_pdel reads it)
;
; The claim gives back a KB once the rows above the highest one in use fill
; it, and goes altogether with the last - so a machine whose user removed
; every shortcut holds nothing for them.
; -----------------------------------------------------------------------------
sc_m_del:
    mov ah, al                  ; THE ROUTES DO NOT CHECK, this does: the
    sub ah, DESK_SC0            ; Delete key and the menu hand over whatever
    cmp ah, [sc_nrows]          ; the desktop's selection is, a drive's zone
    jae .ret                    ; and "none" (0xFF) included
    call sc_m_off
    cmp byte [es:di+SC_R_VOL], 0xFF
    je .ret
    mov byte [es:di+SC_R_VOL], 0xFF
    cmp al, [desk_sel]
    jne .ns
    mov byte [desk_sel], 0xFF
    mov word [menu_items_file+2], sc_s_remx ; ...and Remove Shortcut greys
.ns:
    mov bl, al                  ; its cell goes, and is posted bare (SPEC.md
    xor bh, bh                  ; 26.9)
    mov al, 0xFF
    xchg al, [bx+desk_zslot]
    cmp al, DSL_GONE
    jae .kbz
    and al, DSL_SLOT
    mov bp, desk_cmark
    call sc_m_cold
.kbz:
    mov cl, [sc_nrows]          ; the highest row still in use decides the KB
.h:
    or cl, cl
    jz .kb
    dec cl
    mov al, cl
    add al, DESK_SC0
    call sc_m_off
    cmp byte [es:di+SC_R_VOL], 0xFF
    je .h
    inc cx                      ; CL = the rows still needed
.kb:
    mov al, cl
    call .kb8
    mov ah, al
    mov al, [sc_nrows]
    call .kb8
    cmp ah, al
    jae .save
    mov al, ah
    call sc_m_kb
.save:
    call sc_m_save
    xor al, al                  ; removed, whatever the write said: the
    ret                         ; desktop has it no more (SPEC.md 59.6)
.ret:
    mov al, 0xFF
    ret
.kb8:                           ; AL rows -> AL KB
    add al, SC_PERKB - 1
    shr al, 1
    shr al, 1
    shr al, 1
    ret

; -----------------------------------------------------------------------------
; sc_m_ser - the shortcut TRAILER, after cpc_ser's terminator (SPEC.md 26.8.6)
; in:  ES:BX = the settings cpc_ser just built (CTRL.DRV's own buffer), CX =
;      their length
; out: CF = 0 and ES:BX, CX = what to WRITE - unchanged when there are no
;      shortcuts and no PLACED drive, else a claim of its own holding the
;      settings, the live rows verbatim and compacted, DESK_SC0 bytes of the
;      placed drives' and service item's cells (SPEC.md 26.9), and a 4-byte
;      footer: 'SC', its version and the count. CF = 1: there was something to
;      keep and no room to compose it
; clobbers: ES, BX, CX (the outputs), flags
;
; The footer is read from the END, so the boot reader needs no walk of the
; records above it. NOTHING PLACED, NO TRAILER: the file is then byte for byte
; what it was before this feature - the "costs nothing in the file when there
; is none" the ask set.
;
; A CLAIM OF ITS OWN, PINNED, for the length of the write: CTRL.DRV's bss is
; the slack in its own KB rounding (kernel.asm's MODC_BSS assertion) and two
; KB of rows do not fit there, and a buffer the write is reading must not be
; moved by a compaction the write itself triggers. sc_m_serx gives it back.
; -----------------------------------------------------------------------------
sc_m_ser:
    mov word [cs:sc_tseg], 0
    push ax
    push dx
    push si
    push di
    push es
    xor dl, dl                  ; DL = the live rows
    mov dh, [sc_nrows]
    mov al, DESK_SC0
    or dh, dh
    jz .none
.c:
    call sc_m_off
    cmp byte [es:di+SC_R_VOL], 0xFF
    je .cn
    push bx                     ; the table's byte into the row: the FILE's
    mov bl, al                  ; copy of where it is (SPEC.md 26.9)
    xor bh, bh
    mov ah, [bx+desk_zslot]
    mov [es:di+SC_R_SLOT], ah
    pop bx
    inc dx
.cn:
    inc ax
    dec dh
    jnz .c
.none:
    pop es
    mov [cs:sc_tcnt], dl
    or dl, dl
    jnz .go
    push cx                     ; no shortcut - but a PLACED drive is kept
    mov si, desk_zslot          ; the same way
    mov cx, DESK_SC0
    cld
.anyp:
    mov al, [si]                ; the KERNEL's table, through DS
    inc si
    cmp al, 0xFF
    je .anyn
    test al, DSL_PIN
    jnz .anyy
.anyn:
    loop .anyp
    pop cx
    jmp .plain                  ; nothing placed at all
.anyy:
    pop cx
.go:
    mov al, dl
    mov ah, SC_REC
    mul ah
    add ax, cx
    add ax, DESK_SC0 + 4 + 1023
    push cx
    mov cl, 10
    shr ax, cl                  ; the KB the whole file needs
    xor cx, cx                  ; PINNED
    xor dx, dx
    call sc_m_mem            ; DX = the claim
    pop cx
    jc .out                     ; CF = 1: the caller refuses the write
    mov [cs:sc_tseg], dx
    push ds
    push es
    pop ds                      ; DS = the settings' segment
    mov es, dx
    mov si, bx
    xor di, di
    cld
    rep movsb                   ; the settings...
    pop ds
    mov ax, [sc_seg]
    mov dh, [sc_nrows]
    or dh, dh
    jz .rows                    ; no claim at all: only placed drives
    push ds
    mov ds, ax                  ; DS = the table: no kernel word below
    xor si, si
.r:
    cmp byte [si+SC_R_VOL], 0xFF
    je .skip
    mov cx, SC_REC
    rep movsb                   ; ...the rows...
    jmp short .nx
.skip:
    add si, SC_REC
.nx:
    dec dh
    jnz .r
    pop ds
.rows:
    mov si, desk_zslot          ; ...the placed drives' cells, the table's
    mov cx, DESK_SC0            ; own bytes, 0xFF for any that flows...
.pin:
    mov al, [si]
    inc si
    test al, DSL_PIN            ; placed - or 0xFF, which has the bit too -
    jnz .sp                     ; goes as it is, for the boot to read back

    mov al, 0xFF
.sp:
    stosb
    loop .pin
    mov ax, 'SC'                ; ...and the footer
    stosw
    mov al, SC_VER
    stosb
    mov al, [cs:sc_tcnt]
    stosb
    mov cx, di
    xor bx, bx                  ; ES:BX = the claim, CX = the file
.plain:
    clc
.out:
    pop di
    pop si
    pop dx
    pop ax
    ret

; sc_m_serx - give sc_m_ser's claim back. CF and AX survive: the write's
; answer is in them.
sc_m_serx:
    pushf
    push ax
    push dx
    mov dx, [cs:sc_tseg]
    or dx, dx
    jz .out
    xor ax, ax
    call sc_m_mem      
.out:
    pop dx
    pop ax
    popf
    ret

sc_s_room   db 'No room on the desktop', 0  ; the image's own strings: a toast
sc_s_many   db 'Too many shortcuts', 0      ; copies, and these cost the
sc_s_path   db 'Path too long', 0           ; kernel nothing (SPEC.md 2.8)

; =============================================================================
section .ovl                    ; the BOOT half (SPEC.md 2.5.1): runs once,
; =============================================================================
; from ovc_load, and costs no resident byte and no disk read - .ovl rides the
; fixed ten-sector blob (docs/plans/completed/DESKTOP-SHORTCUTS-PLAN.md 3.1).

; ovl_sc_load's EXITS, AHEAD OF ITS ENTRY. Behind the body they were ~250
; bytes past its six early refusals, so each assembled as a 5-byte jcc-over-
; jmp-near pair - 18 bytes of a blob that had ONE left on kern_big and was
; over on kern_emu and BOOTMARK=1 (SPEC.md 2.5.3.3). Here every one is short.
ovl_sc_drop:
    call ovl_sc_free
ovl_sc_plain:
    stc
ovl_sc_out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
ovl_sc_free:
    mov dx, [sc_seg]
    mov bx, MEM_K_SC
    call COLD_SEG:mmf_mem_free
    xor ax, ax
    mov [sc_seg], ax
    mov [sc_nrows], al
    ret

; -----------------------------------------------------------------------------
; ovl_sc_load - SYSTEM.CFG, when it carries shortcuts (SPEC.md 26.8.6)
; in:  the boot volume mounted at its root; ovc_load's read set up in BX, CX,
;      DX, SI and ES - all preserved, so a refusal falls through into it
; out: CF = 0: the file was read whole into the shortcut claim, its settings
;      head staged in ovc_buf and AX = that head's length; the rows, if a
;      valid trailer carried any, at the claim's base. CF = 1: take ovc_load's
;      own read - the file is too small to carry a trailer, or something here
;      was refused
;
; A FILE OF CFG_FBUF OR LESS NEVER GETS PAST THE FIRST COMPARE, so a machine
; with no shortcuts boots exactly as it did, plus one dskw_stat answered out
; of the directory the mount already holds. A file bigger than that with no
; valid trailer still has its settings read: the head is staged either way,
; so a future kernel's longer file costs the shortcuts and not the settings.
; -----------------------------------------------------------------------------
ovl_sc_load:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, drv_cfgname
    call COLD_SEG:dwf_dskw_stat ; DX:CX = its size
    jc ovl_sc_plain
    or dx, dx
    jnz ovl_sc_plain
    cmp cx, CFG_FBUF
    jbe ovl_sc_plain                  ; no room for a trailer: today's read
    cmp ch, 4096 >> 8
    jae ovl_sc_plain                  ; nothing this kernel writes is that big
    mov di, cx                  ; DI = the size
    lea ax, [di+1023]
    mov cl, 10
    shr ax, cl                  ; ...in KB: 3 at the most
    mov bx, MEM_K_SC
    call COLD_SEG:mmf_mem_claim ; DX = the claim, whole-file sized
    jc ovl_sc_plain
    mov ax, sc_reloc            ; ...and MOVABLE: made at boot, it would
    call COLD_SEG:mmf_mem_movable   ; otherwise be a wall low in the arena for
                                ; the whole session (HEAP-UNPIN-PLAN 2.0)
    mov [sc_seg], dx            ; [sc_nrows] stays 0 until the rows are in
    mov es, dx
    xor bx, bx
    mov cx, di
    xor dx, dx                  ; SI is still drv_cfgname: dskw_stat and the
                                ; heap's entries all keep it
    call COLD_SEG:dwf_dskw_read ; AX = the bytes read
    jc ovl_sc_drop
    mov dx, ax                  ; DX = where the head ends: the whole file...
    xor bx, bx                  ; BL = the rows, BH = 1 once a trailer is seen
    mov di, ax                  ; the footer is the last 4 bytes and the
    sub di, 4 + DESK_SC0        ; placed cells the DESK_SC0 before it
    jb .stage
    cmp word [es:di+DESK_SC0], 'SC'
    jne .stage
    cmp byte [es:di+DESK_SC0+2], SC_VER
    jne .stage
    mov al, [es:di+DESK_SC0+3]
    cmp al, SC_MAX              ; 0..SC_MAX: none is legal, the file may be
    ja .stage                   ; keeping only placed drives
    mov bl, al
    mov ah, SC_REC
    mul ah
    sub di, ax                  ; DI = where the rows start
    jb .stage
    cmp di, CFG_REC0 + 2
    jb .stage
    mov dx, di                  ; ...unless a trailer is there
    inc bh
.stage:
    mov cx, dx                  ; the settings head, into ovc_buf
    cmp cx, CFG_FBUF
    jbe .hc
    mov cx, CFG_FBUF
.hc:
    push cx                     ; ...which is ovc_deser's AX
    push ds
    push es                     ; the claim, back into ES after the copy
    push es
    pop ds                      ; DS = the claim
    push cs
    pop es                      ; ES = this overlay
    xor si, si
    mov di, ovc_buf
    cld
    rep movsb
    pop es                      ; ES = the claim
    pop ds
    or bh, bh
    jz .none
    mov al, bl                  ; the rows, down to the claim's base...
    mov ah, SC_REC
    mul ah
    mov cx, ax
    push es                     ; the claim, for the sanitise below
    push ds
    push es
    pop ds
    mov si, dx
    xor di, di
    rep movsb                   ; ...which leaves DS:SI on the placed cells
    pop es                      ; ES = the kernel
    push es
    mov di, desk_zslot          ; THE TABLE'S zones 0..DESK_SC0-1, verbatim
    mov cl, DESK_SC0            ; (SPEC.md 26.9): the writer put them in their
    rep movsb                   ; final form, and the boot's rowcalc drops any
    pop ds                      ; cell this screen does not have
    pop es                      ; ES = the claim
    mov [sc_nrows], bl
    mov cl, bl
    jcxz .none                  ; placed drives and no shortcut: no claim
    xor di, di                  ; every string terminated, whatever the file
    mov bx, desk_zslot + DESK_SC0   ; said, and each row's cell into the table
.san:
    mov byte [es:di+SC_R_PATH+SC_PATHMAX-1], 0
    mov byte [es:di+SC_R_CAP+SC_CAPMAX-1], 0
    mov ax, [es:di+SC_R_SLOT]   ; AL = the cell, AH = SC_R_KIND, the next
    cmp ah, 2                   ; byte - and a row sc_m_add would refuse is
    ja .kill                    ; a FREE row: a kind or a volume no open can
    cmp byte [es:di+SC_R_VOL], DVOL_MAX ; follow (SPEC.md 26.9.6)
    jb .keep
.kill:
    mov al, 0xFF
    mov [es:di+SC_R_VOL], al
.keep:
    mov [bx], al
    inc bx
    add di, SC_REC
    loop .san
    jmp short .done
.none:
    call ovl_sc_free            ; no trailer after all: give the claim back
.done:
    pop ax                      ; the head's length
    clc
    jmp ovl_sc_out

section .modc                   ; ...and the image's own scratch, IN the
sc_mz       db 0                ; image: a module may write its own bytes
sc_px       dw 0                ; (MODULE-SELFCONTAIN-PLAN), and its bss is
sc_py       dw 0                ; only the slack in its KB rounding, which
sc_ex       db 0                ; the settings buffer already spends
sc_best     dw 0                ; sc_m_cell: the point, the zone it leaves
sc_bc       db 0                ; out, the nearest distance so far and its
                                ; cell; sc_m_ser's claim and the rows it holds
sc_tseg     dw 0
sc_tcnt     db 0

section .text

%endif                          ; OS88_SHORTCUTS
