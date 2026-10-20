; =============================================================================
; os8088 - tests/romfont/romfont.asm
;
; ROMFONT: does reading the 8x8 glyph table out of the MACHINE'S ROM cost
; anything against reading a RAM copy of it? SPEC.md 6 took the kernel's
; 768-byte .lowbss copy away and has every renderer read the table where
; the BIOS keeps it, on the argument that on the target - an IBM 5150 - ROM
; and RAM are both zero-wait-state on the planar and DMA refresh steals the
; bus from both alike. MartyPC models exactly that (bus/memory.rs answers 0
; wait states for any address that is not a device window, and installs
; every ROM through the same copy that has no wait cost), so the emulator
; CANNOT disagree with the argument - it is the same assumption twice. This
; is the instrument that can: the same loop, against the ROM table and
; against a copy of it in this package's own segment, PIT-timed on whatever
; machine it is booted on.
;
; Nothing here ships: `make romfont` builds build/romfont360.img (and the
; package rides the two bench disks as well), and `all` does not.
;
;   make romfont               # build/romfont360.img, ROMFONT.O88 at its root
;   ...boot the system disk, put this one in B:, open ROMFONT.O88, click the
;   window (or press R), photograph it. The photograph is the result.
;
; THE ROWS, and why each. There are TWO ROM tables on most machines, and the
; kernel's choice between them is SPEC.md 6.0.1's rule, so both are timed:
;
;   BIOS read 768B    `rep lodsb` over the table int 10h AX=1130h BH=3 names
;                     (glyphs 32..127) - on an EGA or VGA the card's OPTION
;                     ROM. Pure read traffic: the row most sensitive to a
;                     wait state, so the one that shows a ROM slower than RAM
;   RAM read 768B     ...the identical loop over a copy in this segment
;   BIOS glyph rows   96 cells x 8 rows of a renderer-shaped inner loop - load
;                     the glyph row, invert it, AND it into a RAM canvas, step
;                     a stride. One table read per two canvas accesses, which
;                     is the ratio the kernel's 1bpp cell writer has
;   RAM glyph rows    ...the identical loop over the copy
;   planar read/rows  ...and the same two over F000:FA6E, the system board's
;                     own set, which the kernel reads instead whenever its
;                     glyphs are the BIOS table's
;   FONT_RUN 40       the kernel's own opaque run of 40 cells, aligned - the
;                     whole cost a caller sees, from whichever table the
;                     "kernel reads" line names
;
; and four derived rows, table/RAM x1000: 1000 is no difference, 1250 is where
; a copy would have been worth a kilobyte of heap.
;
; The header lines say WHAT THE KERNEL DECIDED: the BIOS table's seg:off,
; whether the planar set carries the same glyphs (the kernel's test), and the
; seg:off OSAPI_FONT_GLYPHS answers with its kind - planar ROM, option ROM,
; or HEAP COPY (the 1KB MEM_K_FONT claim, SPEC.md 6.0.1) - and the kernel's
; OWN clock: font_init times the candidates and RAM at boot and leaves the
; counts at 0040:00F8, so the photograph shows the ratio it decided on
; beside the rows below that check it.
;
; PREDICTION (MartyPC, genuine 27 OCT 82 BIOS, CGA or Hercules): kernel reads
; F000:FB6E "planar ROM, in place", every table/RAM row 1000 +- 1.
; =============================================================================

%include "os88api.inc"

BL_ARENA_BYTES equ 3000             ; a ~20-line report (benchlib.inc)

    OS88_HEADER 'ROMFONT', rf_entry

section .bss align=1 follows=.text
section .text

RF_PLANAR   equ 0xFA6E + 32 * 8     ; F000: the planar ROM's glyph 32
RF_TAB      equ 96 * 8              ; glyphs 32..127, the kernel's FONT_BYTES
RF_STRIDE   equ 96                  ; the canvas: one byte column per glyph
RF_N        equ 32                  ; iterations a table row
RF_NRUN     equ 16                  ; ...and a FONT_RUN row
RF_RUNLEN   equ 40

; -----------------------------------------------------------------------------
; rf_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
rf_entry:
    push si
    mov ax, rf_bss_end              ; every bss byte inside what the header
    cmp ax, os88_image_end + RF_BSS_TOTAL   ; asked the loader to zero
    ja .refuse
    call rf_probe                   ; find the ROM table, copy it, compare
    call rf_hint
    mov si, rf_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [rf_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; rf_probe - where is the ROM table, what does the kernel answer, and a copy
; out: [rf_rseg]:[rf_roff] = the ROM's glyph 32, [rf_kseg]:[rf_koff] = the
;      kernel's, rf_copy = the 768 bytes, [rf_same] = 1 when they match
; preserves every register
; -----------------------------------------------------------------------------
rf_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    xor ax, ax                      ; font_init's own probe (kernel/font.inc):
    mov es, ax                      ; a pre-EGA BIOS leaves ES:BP untouched,
    xor bp, bp                      ; so zero it first
    mov ax, 0x1130
    mov bh, 0x03                    ; ROM 8x8, characters 0..127
    int 0x10
    mov ax, es
    or ax, bp
    jnz .have
    mov ax, 0xF000                  ; the IBM PC/XT ROM's own 8x8 set
    mov es, ax
    mov bp, 0xFA6E
.have:
    add bp, 32 * 8
    mov [rf_rseg], es
    mov [rf_roff], bp
    pop es

    call OSAPI_FONT_GLYPHS          ; DX:SI, AL = first code
    xor ah, ah
    sub al, 32                      ; normalise to glyph 32 (the kernel's
    mov cl, 3                       ; table starts there today; say so if not)
    shl ax, cl
    sub si, ax
    mov [rf_kseg], dx
    mov [rf_koff], si

    push ds                         ; the copy, in this package's own segment
    push es
    push ds
    pop es
    mov di, rf_copy
    mov si, [rf_roff]
    mov ds, [rf_rseg]
    mov cx, RF_TAB
    rep movsb
    pop es
    pop ds

    push ds                         ; and check it: the RAM rows must read the
    push es                         ; same bytes the ROM rows do
    push ds
    pop es
    mov di, rf_copy
    mov si, [rf_roff]
    mov ds, [rf_rseg]
    mov cx, RF_TAB
    repe cmpsb
    pop es
    pop ds
    mov byte [rf_same], 0
    jne .diff
    mov byte [rf_same], 1
.diff:
    push ds                         ; ...and the kernel's own test 1 (SPEC.md
    push es                         ; 6.0.1): does the PLANAR set at F000:FA6E
    mov ax, 0xF000                  ; carry the same 95 glyphs as the BIOS's
    mov es, ax                      ; answer? Then the kernel reads it there
    mov di, RF_PLANAR
    mov si, [rf_roff]
    mov ds, [rf_rseg]
    mov cx, 95 * 8
    repe cmpsb
    pop es
    pop ds
    mov byte [rf_psame], 0
    jne .pdiff
    mov byte [rf_psame], 1
.pdiff:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rf_hint:
    push si
    call bl_blank
    mov si, rf_s_title
    call bl_sline
    call rf_where
    mov si, rf_s_hint
    call bl_sline
    pop si
    ret

; rf_where - the two seg:off lines and the copy check. Preserves every register.
rf_where:
    push ax
    push di
    push si
    call bl_lclr
    mov si, rf_r_rom
    xor di, di
    call bl_lput
    mov ax, [rf_rseg]
    mov di, BL_C_N
    call bl_hex4
    mov byte [bl_lscr + BL_C_N + 4], ':'
    mov ax, [rf_roff]
    mov di, BL_C_N + 5
    call bl_hex4
    call bl_lcommit

    call bl_lclr
    mov si, rf_r_kern
    xor di, di
    call bl_lput
    mov ax, [rf_kseg]
    mov di, BL_C_N
    call bl_hex4
    mov byte [bl_lscr + BL_C_N + 4], ':'
    mov ax, [rf_koff]
    mov di, BL_C_N + 5
    call bl_hex4
    mov ax, [rf_kseg]               ; ...and WHICH KIND, which is the
    mov si, rf_s_isplan             ; kernel's verdict (SPEC.md 6.0.1)
    cmp ax, 0xF000
    je .say
    mov si, rf_s_isopt
    cmp ax, 0xC000
    jae .say
    mov si, rf_s_isheap             ; below the ROMs: a copy in RAM - the
    cmp word [rf_koff], 0           ; 1KB MEM_K_FONT claim starts at offset 0,
    je .say                         ; a BAKED_FONT face in .lowbss does not
    mov si, rf_s_isbake
.say:
    mov di, BL_C_N + 11
    call bl_lput
    call bl_lcommit

    mov si, rf_r_same
    mov di, rf_s_yes
    cmp byte [rf_same], 1
    je .same
    mov di, rf_s_no
.same:
    call bl_kvs
    mov si, rf_r_psame
    mov di, rf_s_yes
    cmp byte [rf_psame], 1
    je .psame
    mov di, rf_s_pno
.psame:
    call bl_kvs
    call rf_kclock                  ; ...and what the kernel's clock said
    pop si
    pop di
    pop ax
    ret

; rf_kclock - the kernel's own measurement (SPEC.md 6.0.1): font_init leaves
; three PIT counts at 0040:00F8 behind 'FP' - the table it chose, RAM, and the
; BIOS's answer - and this prints them with the ratio it decided on. A copy
; is taken when the chosen/RAM ratio passes 1250. Preserves every register.
rf_kclock:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, 0x0040
    mov es, ax
    cmp word [es:0x00F8], 'FP'
    jne .none
    mov si, rf_r_kbest
    mov ax, [es:0x00FA]
    call .one
    mov si, rf_r_kram
    mov ax, [es:0x00FC]
    call .one
    mov si, rf_r_kbios
    mov ax, [es:0x00FE]
    call .one
    mov ax, [es:0x00FA]             ; chosen x 1000 / RAM
    mov cx, 1000
    mul cx
    mov cx, [es:0x00FC]
    jcxz .out
    cmp dx, cx
    jae .out                        ; would not fit a word: say nothing
    div cx
    xor dx, dx
    mov si, rf_r_kratio
    mov cx, 9
    call bl_kv
    jmp short .out
.none:
    mov si, rf_r_knone
    call bl_sline
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.one:
    xor dx, dx
    mov cx, 9
    call bl_kv
    ret

rf_paint:
    call bl_paint
    ret

rf_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [rf_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 'r'
    je .run
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.run:
    call rf_run
    call rf_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rf_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [rf_win], si
    call rf_run
    call rf_repaint
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

rf_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [rf_win]
    call OSAPI_WM_CONTENT
    mov [rf_cx], ax
    mov [rf_cy], dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [rf_cx]
    mov bx, [rf_cy]
    mov cx, ax
    add cx, [rf_cw]
    dec cx
    mov dx, bx
    add dx, [rf_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [rf_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE TIMED BODIES. Each pair is ONE body run twice with [rf_tseg]:[rf_toff]
; pointing at the ROM and then at the copy, so the two rows cannot differ in
; anything but where the table is.
; =============================================================================

; --- the whole table, read and discarded -------------------------------------
rf_b_read:
    push ds
    mov si, [rf_toff]
    mov ds, [rf_tseg]
    mov cx, RF_TAB
    rep lodsb
    pop ds
    ret

; --- 96 cells x 8 rows: load the glyph row, invert, AND into the canvas ------
rf_b_rows:
    push ds
    push es
    push ds
    pop es                          ; ES = the canvas (our segment)
    mov si, [rf_toff]
    mov ds, [rf_tseg]               ; DS = the table
    mov di, rf_canvas
    mov dx, 96
.cell:
    mov bx, 8
    push di
.row:
    lodsb                           ; the glyph row
    not al
    and [es:di], al                 ; ...into the canvas, one read and one
    add di, RF_STRIDE               ; write, as a 1bpp cell writer does
    dec bx
    jnz .row
    pop di
    inc di
    dec dx
    jnz .cell
    pop es
    pop ds
    ret

; --- the kernel's opaque run, 40 cells aligned --------------------------------
rf_b_run:
    mov cx, [rf_bx]
    mov dx, [rf_by]
    mov si, rf_str
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

; rf_rom / rf_ram - point the bodies at one table or the other
rf_rom:
    push ax
    mov ax, [rf_rseg]
    mov [rf_tseg], ax
    mov ax, [rf_roff]
    mov [rf_toff], ax
    pop ax
    ret

rf_planar:
    mov word [rf_tseg], 0xF000
    mov word [rf_toff], RF_PLANAR
    ret

rf_ram:
    mov [rf_tseg], ds
    mov word [rf_toff], rf_copy
    ret

; =============================================================================
; rf_run - the suite
; =============================================================================
rf_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, [rf_win]
    call OSAPI_WM_CONTENT
    mov [rf_cx], ax
    mov [rf_cy], dx
    mov bx, [rf_win]
    call OSAPI_WM_GEOM
    mov [rf_cw], cx
    mov [rf_ch], dx
    mov ax, [rf_cx]
    add ax, 7
    and ax, 0xFFF8
    mov [rf_bx], ax
    mov ax, [rf_cy]
    add ax, [rf_ch]
    sub ax, 13
    mov [rf_by], ax

    mov word [bl_nrow], 0           ; a re-run replaces the report rather than
    mov word [bl_used], 0           ; appending to it (gfxbench's reset)
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov byte [rf_done], 0
    mov si, rf_s_title
    call bl_sline
    call rf_where
    call bl_head
    call bl_baseline

    mov word [bl_n], RF_N
    call rf_rom
    mov word [bl_body], rf_b_read
    mov si, rf_r_romrd
    xor al, al
    call bl_run
    mov di, rf_res + 0
    call rf_bank

    call rf_ram
    mov word [bl_body], rf_b_read
    mov si, rf_r_ramrd
    xor al, al
    call bl_run
    mov di, rf_res + 4
    call rf_bank

    call rf_rom
    mov word [bl_body], rf_b_rows
    mov si, rf_r_romrw
    xor al, al
    call bl_run
    mov di, rf_res + 8
    call rf_bank

    call rf_ram
    mov word [bl_body], rf_b_rows
    mov si, rf_r_ramrw
    xor al, al
    call bl_run
    mov di, rf_res + 12
    call rf_bank

    call rf_planar
    mov word [bl_body], rf_b_read
    mov si, rf_r_plrd
    xor al, al
    call bl_run
    mov di, rf_res + 20
    call rf_bank

    call rf_planar
    mov word [bl_body], rf_b_rows
    mov si, rf_r_plrw
    xor al, al
    call bl_run
    mov di, rf_res + 24
    call rf_bank

    mov word [bl_n], RF_NRUN
    mov word [bl_body], rf_b_run
    mov si, rf_r_run
    xor al, al
    call bl_run
    mov di, rf_res + 16
    call rf_bank

    call bl_blank
    mov si, rf_s_ratio
    call bl_sline
    mov si, rf_r_xrd
    mov bx, rf_res + 0
    mov di, rf_res + 4
    call rf_ratio
    mov si, rf_r_xrw
    mov bx, rf_res + 8
    mov di, rf_res + 12
    call rf_ratio
    mov si, rf_r_xprd
    mov bx, rf_res + 20
    mov di, rf_res + 4
    call rf_ratio
    mov si, rf_r_xprw
    mov bx, rf_res + 24
    mov di, rf_res + 12
    call rf_ratio
    mov byte [rf_done], 1

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rf_bank - [bl_lastus] -> the dword at DI (the result block a harness reads)
rf_bank:
    push ax
    mov ax, [bl_lastus]
    mov [di], ax
    mov ax, [bl_lastus+2]
    mov [di+2], ax
    pop ax
    ret

; rf_ratio - one "ROM/RAM x1000" line: SI = label, BX -> the table's dword,
; DI -> RAM's. Preserves every register.
rf_ratio:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [bx]
    mov dx, [bx+2]
    mov cx, [di+2]                  ; CX:DI = the RAM figure (the divisor)
    mov di, [di]
.fit:
    or cx, cx                       ; fit the divisor into 16 bits, the
    jz .fitted                      ; dividend with it - a ratio survives
    shr dx, 1                       ; the same shift on both
    rcr ax, 1
    shr cx, 1
    rcr di, 1
    jmp short .fit
.fitted:
    mov cx, di
    or cx, cx
    jz .out
    push si
    mov si, cx
    mov cx, 1000
    call bl_mul48
    mov cx, si
    call bl_div48
    call bl_get32
    pop si
    mov cx, 9
    call bl_kv
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

rf_tpl:
    dw 7, 22, 632, 448              ; x = 7 so WF_SNAP wants W_X + 1 on a
    dw rf_ttl, rf_paint, rf_onkey, rf_onclick   ; multiple of 8

rf_ttl:     db 'ROM Font Bench', 0

rf_s_title: db 'ROMFONT - the 8x8 table read from ROM against a RAM copy', 0
rf_s_hint:  db 'Click the window, or press R, to run.', 0
rf_s_ratio: db '-- ROM/RAM x1000 (1000 = no difference) --', 0

rf_r_rom:   db 'BIOS table (glyph 32)', 0
rf_r_kern:  db 'kernel reads', 0
rf_r_same:  db 'copy matches BIOS', 0
rf_r_psame: db 'planar = BIOS glyphs', 0
rf_s_pno:   db 'no - kernel keeps BIOS', 0
rf_s_isplan: db 'planar ROM, in place', 0
rf_s_isopt: db 'option ROM, in place', 0
rf_s_isheap: db 'HEAP COPY (1KB claim)', 0
rf_s_isbake: db 'baked face (.lowbss)', 0
rf_s_yes:   db 'yes', 0
rf_s_no:    db 'NO - rows invalid', 0

rf_r_kbest: db 'kernel: chosen counts', 0
rf_r_kram:  db 'kernel: RAM counts', 0
rf_r_kbios: db 'kernel: BIOS counts', 0
rf_r_kratio: db 'kernel: chosen/RAM x1000', 0
rf_r_knone: db 'kernel left no clock at 0040:00F8', 0
rf_r_romrd: db 'BIOS read 768B', 0
rf_r_ramrd: db 'RAM read 768B', 0
rf_r_romrw: db 'BIOS glyph rows 96', 0
rf_r_ramrw: db 'RAM glyph rows 96', 0
rf_r_plrd:  db 'planar read 768B', 0
rf_r_plrw:  db 'planar glyph rows 96', 0
rf_r_run:   db 'FONT_RUN 40 aligned', 0
rf_r_xrd:   db 'BIOS read 768B', 0
rf_r_xrw:   db 'BIOS glyph rows', 0
rf_r_xprd:  db 'planar read 768B', 0
rf_r_xprw:  db 'planar glyph rows', 0

rf_str:     db 'The quick brown fox jumps over 13 dogs. ', 0

; THE RESULT BLOCK: a harness finds 'RFNTRES' in guest memory and reads the
; seven dwords after it (BIOS read, RAM read, BIOS rows, RAM rows, FONT_RUN,
; planar read, planar rows) - hundredths of a microsecond per iteration, in row
; order - and [rf_done] after those. Kept in the IMAGE so it is at a fixed
; offset from the magic.
rf_magic:   db 'RFNTRES'
rf_res:     dd 0, 0, 0, 0, 0, 0, 0
rf_done:    db 0

rf_win:     dw 0
rf_cx:      dw 0
rf_cy:      dw 0
rf_cw:      dw 0
rf_ch:      dw 0
rf_bx:      dw 0
rf_by:      dw 0
rf_rseg:    dw 0
rf_roff:    dw 0
rf_kseg:    dw 0
rf_koff:    dw 0
rf_tseg:    dw 0
rf_toff:    dw 0
rf_same:    db 0
rf_psame:   db 0

RF_BSS_OWN  equ RF_TAB + RF_STRIDE * 8
RF_BSS_TOTAL equ RF_BSS_OWN + BL_BSS_SIZE + 16
    OS88_BSS RF_BSS_TOTAL
    OS88_IMAGE_END

section .bss
rf_copy:    resb RF_TAB             ; the RAM copy, in this package's segment
rf_canvas:  resb RF_STRIDE * 8      ; the rows loops' 1bpp target
rf_bl:      resb BL_BSS_SIZE
rf_bss_end:
section .text

    BL_BSS rf_bl
