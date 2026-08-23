; =============================================================================
; os8088 - tests/gfxbench/gfxbench.asm
;
; GFXBENCH: every drawing primitive the OS publishes, priced on the adapter it
; is booted on, plus the raw memory and framebuffer bandwidth underneath them.
; It is the harness PERFORMANCE.md Part 2's calibration table wants and did not
; have: two of its fifteen figures are measured (a glyph cell, and a
; framebuffer read-modify-write quoted at "~30 cycles") and the rest are
; estimated from them.
;
; ONE package for Hercules AND CGA, deliberately. Both are 1bpp banked
; framebuffers driven by the same software renderer (SPEC.md 39.3); what
; differs is four numbers, and those come from OSAPI_VIDEO at run time. Two
; sources would be two chances to drift, and the whole value of the exercise is
; that the Hercules column and the CGA column are the SAME measurement. It runs
; on VGA too, where it prices the planar path for contrast.
;
;   make bench
;   make test VIDEO=herc HERCSEG=0x7000 TESTAPPS=build/bench.img
;   make test VIDEO=cga                 TESTAPPS=build/bench.img
;
; The report is a FILE, and `tools/os88flush.py` is how it reaches the host -
; MartyPC keeps the guest's writes in RAM, so nothing lands on the image until
; something spends them. `os88flush.Flush(marty=m).volume(N).read(...)` from a
; driver script, or `os88flush.py <addr> get N <FILE> <out>` from the shell.
; DO NOT page it off the screen: tests/benchlib.inc's bl_save header has the
; recipe and docs/TESTING.md has the three traps in front of the RUN.
;
; ...but see PERFORMANCE.md Part 3 and Part 4 before quoting anything from a
; QEMU run: the microsecond column there is the HOST's speed. Under
; `-icount shift=3,sleep=off` the counts column is guest INSTRUCTIONS, which is
; reproducible and machine-independent and is not time. build/bench360.img on a
; real 4.77 MHz 8088 is where these numbers mean what they say.
;
; --- what it measures, and why in that shape ---------------------------------
;
; RAW BANDWIDTH comes first because everything above it is explained by it. The
; same loop shape - 32 rows of 64 bytes, 2,048 bytes an iteration - runs
; against plain RAM and against the framebuffer, so the ratio between the two
; rows IS the bus penalty, with the loop, the segment override and the
; addressing identical on both sides. Four accesses are priced (word write,
; byte write, word read, byte read-modify-write) because the kernel's inner
; loops use all four and on an 8-bit bus they are not proportional.
;
; PRIMITIVES are measured AT TWO SIZES wherever the cost has a per-call part
; and a per-pixel part - 8x8 against 64x64, 8 px against 256 px. One size
; cannot separate them, and pricing a rect this harness never drew needs both
; terms. The DERIVED block does that subtraction so the reader does not have
; to, and prints its inputs beside it so the reader can check that it did.
;
; TEXT is measured at the same ten characters as tests/fontbench, on purpose:
; fontbench's published figures (SPEC.md 6.1.1) are then a free cross-check on
; this harness, and PERFORMANCE.md Part 6 rule 7 is that a harness reporting
; one number per run is one you have to trust.
;
; --- what it will NOT do -----------------------------------------------------
;
; It never calls OSAPI_WM_SHOW/HIDE/FRONT. Everything here runs inside a window
; callback, which already holds the gfx lock (SPEC.md 12.3) and those three
; take it themselves - so from here they are a deadlock, not a slow row. The
; composite costs they would have measured (SPEC.md 11.90/11.91) are the
; kernel's own and belong to a counter in the kernel, not to a package.
;
; THE LOCK ITSELF IS MEASURED, and it is measured BACKWARDS. OSAPI_GFX_LOCK
; from here would deadlock, but UNLOCK-then-LOCK is the same two routines in
; the other order, and it is an idiom the kernel already uses inside a callback
; (fm_drag, SPEC.md 22.4). What is in them is not the mutex - that is one flag
; and six instructions - it is the MOUSE CURSOR: the lock erases it, and the
; unlock saves under it and draws it again, three passes over a cell that no
; drawing may be allowed to smear. A field log of Missile Command put the pair
; at 21.8% of a session with not one pixel of the game in it (PERFORMANCE.md
; Part 9 Set 4), which is why it is here.
;
; The two halves cannot be separated from a package - they must alternate - so
; the row is the PAIR. It is also the one row in this harness whose measured
; span is not interrupt-free: gfx_lock ends with `sti` by contract, so an IRQ
; raised during the unlock half is delivered inside the span rather than
; between iterations. The 8259 latches one, so the error is bounded and
; upward, and [bl_max] and the '!' flag are what to read it against.
;
; It draws only inside its own content rect and repaints it afterwards. The raw
; framebuffer rows write to the byte columns their own content occupies,
; computed with the same banked arithmetic gfx_rowbase uses - so a wrong
; adapter record scribbles this window and nothing else.
;
; Prefix gb_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'GFXBENCH', gb_entry

GB_BOXW     equ 256               ; the drawing sandbox, in pixels. Fixed and
GB_BOXH     equ 128               ; not derived from the window, so the same
                                  ; row on two adapters is the same work. It
                                  ; fits the CGA content (622 x 136), which is
                                  ; the smallest of the three
GB_BWROWS   equ 32                ; bandwidth: rows per iteration...
GB_BWCOLS   equ 64                ; ...and bytes per row. 2,048 bytes, which is
                                  ; a few milliseconds on the target and so
                                  ; nowhere near the 55 ms PIT wrap
GB_BWBYTES  equ GB_BWROWS * GB_BWCOLS
GB_BLITW    equ 64                ; the blit source, 4bpp packed
GB_BLITH    equ 64
GB_BLITS    equ GB_BLITW / 2      ; ...its stride in bytes
GB_BLITSZ   equ GB_BLITS * GB_BLITH

GB_VSMAX    equ 60000             ; retrace-poll bound. A dead status port must
                                  ; time out, never hang: the Linux-RTC bug
                                  ; SPEC.md 37.90 records, in miniature

; -----------------------------------------------------------------------------
; gb_entry - package entry (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment; gfx lock NOT held
; out: BX = window ptr, CF set = refused
; -----------------------------------------------------------------------------
gb_entry:
    push si
    call gb_facts                   ; the machine's own numbers, before there
    call gb_hint                    ; is a window to draw them in - and the
                                    ; invitation, so the first thing on screen
                                    ; is not a blank page
    mov si, gb_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [gb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP              ; EVERY adapter (SPEC.md 11.94 - it was
                                    ; mono only until VGA was measured and
                                    ; gained more) - and it PRESERVES FLAGS,
                                    ; so the CF this proc owes the loader
                                    ; survives it. The rows below are
                                    ; unaffected either way: this harness sets
                                    ; its own text x explicitly, which is what
                                    ; keeps `aligned` and `skewed` meaning the
                                    ; same thing on all three adapters
    mov si, gb_menus
    call OSAPI_MENU_SET
    mov si, gb_onabout
    call OSAPI_ABOUT_SET
    clc
.out:
    pop si
    ret                             ; NEAR: the kernel reaches every proc here
                                    ; through our own dispatcher (SPEC.md 20.1)

; -----------------------------------------------------------------------------
; gb_paint - W_PAINT: the report page. The content arrives white.
; in:  SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
gb_paint:
    call bl_paint
    ret

; -----------------------------------------------------------------------------
; gb_onkey - W_ONKEY: R runs, S saves, everything else pages
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
gb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [gb_win], si
    mov bl, al
    or bl, 0x20                     ; fold the case; a cursor key has AL = 0
    cmp bl, 'r'                     ; and matches neither of these
    je .run
    cmp bl, 's'
    je .save
    call bl_key                     ; AL/AH still as they arrived
    jc .out
    call bl_paint
    jmp short .out
.run:
    call gb_run
    call gb_repaint
    jmp short .out
.save:
    call gb_save
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_onclick - W_ONCLICK: a click pages down, so the harness can be driven
;              without a keyboard (the QMP mouse is the scripted path)
; in:  CX = x, DX = y, SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
gb_onclick:
    push ax
    push si
    mov [gb_win], si
    cmp byte [gb_ran], 0            ; never run: a click runs it, which is what
    jne .page                       ; a user who has just read the invitation
    push bx                         ; will try (tests/fontbench's idiom)
    push cx
    push dx
    push di
    call gb_run
    call gb_repaint
    pop di
    pop dx
    pop cx
    pop bx
    jmp short .out
.page:
    mov al, ' '
    xor ah, ah
    call bl_key
    jc .out
    call bl_paint
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_oncmd - the menu handler (SPEC.md 12.2): AL = item, AH = menu, SI = window
; -----------------------------------------------------------------------------
gb_oncmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [gb_win], si
    or ah, ah
    jnz .out
    cmp al, 0
    je .run
    cmp al, 1
    je .save
    cmp al, 2
    je .top
    jmp short .out
.run:
    call gb_run
    call gb_repaint
    jmp short .out
.save:
    call gb_save
    call bl_paint
    jmp short .out
.top:
    mov word [bl_top], 0
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gb_onabout - the About item just returns to the top of the report, which is
; where the provenance lines are. A callback must repaint itself.
gb_onabout:
    push si
    mov word [bl_top], 0
    call bl_paint
    pop si
    ret

; -----------------------------------------------------------------------------
; gb_save - write the report, under a name that says which adapter made it
; -----------------------------------------------------------------------------
gb_save:
    push ax
    push si
    mov si, gb_f_vga
    cmp byte [gb_kind], VID_HERC
    jne .cga
    mov si, gb_f_herc
    jmp short .go
.cga:
    cmp byte [gb_kind], VID_CGA
    jne .go
    mov si, gb_f_cga
.go:
    call bl_save
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_repaint - erase what the run scribbled, then redraw the page
; in:  [gb_win]; gfx lock held
; -----------------------------------------------------------------------------
gb_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, [gb_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, dx
    mov cx, ax
    add cx, [gb_cw]
    dec cx
    add dx, [gb_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [gb_win]
    call bl_paint
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; gb_facts - the machine's own numbers, banked at entry
; =============================================================================
gb_facts:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call OSAPI_VIDEO                ; AX = w, BX = h, CX = first dock row,
    mov [gb_vw], ax                 ; DL = kind, DH = bpp
    mov [gb_vh], bx
    mov [gb_dock], cx
    mov [gb_kind], dl
    mov [gb_bpp], dh
    call OSAPI_CPU_INFO             ; AL = tier, AH = feature bits
    mov [gb_cpu], ax
    call OSAPI_MEM_AVAIL            ; AX = largest run KB, BX = total free KB
    mov [gb_mlarge], ax
    mov [gb_mtotal], bx
    push ds
    pop es
    mov di, gb_syskb
    call OSAPI_SYS_KB
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; gb_run - the whole suite
; in:  [gb_win]; gfx lock HELD (this is a callback)
; =============================================================================
gb_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov word [bl_nrow], 0           ; a re-run replaces the report rather than
    mov word [bl_used], 0           ; appending to it
    mov word [bl_top], 0
    mov byte [bl_full], 0
    mov byte [gb_ran], 1

    call gb_geom                    ; the sandbox, from the LIVE window
    call gb_disp                    ; ...and WHICH CARD it is on (SPEC.md 39.19)
    call gb_mktab                   ; the adapter record and the row tables
    call gb_mkblit                  ; the two blit sources
    call bl_baseline                ; ...and the loop overhead every P row is
    mov [gb_bcnt], ax               ; reported net of
    mov [gb_bcnt+2], dx

    mov si, gb_p_head
    call bl_progress
    call gb_header
%ifndef GB_ONLYHEAD
    mov si, gb_p_bw
    call bl_progress
    call gb_bw
    mov si, gb_p_prim
    call bl_progress
    call gb_prims
    mov si, gb_p_text
    call bl_progress
    call gb_text
    mov si, gb_p_api
    call bl_progress
    call gb_api
    mov si, gb_p_comp
    call bl_progress
    call gb_composite
    mov si, gb_p_fs
    call bl_progress
    call gb_fs
    call gb_derived
    call bl_operator                ; ...and what the OPERATOR was doing
%endif
    call gb_trailer
    call gb_save                    ; SAVE IT, without being asked (below)

    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_geom - the sandbox rect, from the live window (SPEC.md 39.7: wm_fit has
;           already clamped the template onto whatever screen this is)
; -----------------------------------------------------------------------------
gb_geom:
    push ax
    push bx
    push cx
    push dx
    mov bx, [gb_win]
    call OSAPI_WM_GEOM              ; CX = content w, DX = content h
    mov [gb_cw], cx
    mov [gb_ch], dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [gb_cx], ax
    mov [gb_cy], dx
    mov [bl_cx], ax                 ; bl_progress draws before bl_paint ever
    mov [bl_cy], dx                 ; runs, so it cannot wait for these
    add ax, 7                       ; the aligned x: content left rounded UP,
    and ax, 0xFFF8                  ; so font_run's fast path and the byte
    mov [gb_x], ax                  ; column of the raw rows agree
    mov [gb_tx], ax
    mov [gb_y], dx
    mov ax, [gb_cw]                 ; the page geometry bl_paint would compute -
    mov cl, 3                       ; done here too, so the header block can
    shr ax, cl                      ; report it before the first repaint
    cmp ax, BL_MAXLINE
    jbe .c
    mov ax, BL_MAXLINE
.c:
    mov [bl_vcols], ax
    mov ax, [gb_ch]
    shr ax, cl
    mov [bl_vrows], ax
    or ax, ax
    jz .r
    dec ax
.r:
    mov [bl_prows], ax
    call gb_boxfull
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_disp - WHICH DISPLAY is the sandbox on? (SPEC.md 39.19, 57.4's 'VD')
;
; in:       gb_geom has run; out: [gb_dok] and the gb_d* block; [gb_kind]
;           becomes the SANDBOX's adapter rather than the machine's primary
; clobbers: flags
;
; This report used to name itself after `[vid_kind]`, and on a two-card machine
; that is the PRIMARY - so a run whose window was on the other card wrote
; GFXHERC.TXT full of CGA timings and had to be renamed by hand. Worse than the
; name: gb_mktab took the framebuffer segment, the stride and the bank shape
; from the same place, so the raw VRAM rows measured a card the sandbox was not
; on, at an offset derived from a VIRTUAL x that is past that card's width.
; That was luck rather than design every time it came out right.
;
; So the display is RESOLVED, from the point the drawing actually starts at,
; and everything else follows from it: the file name, the adapter line, the
; status port, the four framebuffer numbers, and the local origin the row
; tables are built from.
;
; A reader that cannot find its block says so and continues (SPEC.md 57 rule
; 2) - [gb_dok] stays 0, the origin stays (0,0) and every number is the
; one-display answer, which is exactly right on a one-display machine and on a
; kern_small kernel that has no such bytes at all.
;
; [gb_dstrad] is the row that changes what the rest of the report MEANS: a
; sandbox crossing a seam has primitives being split per display (39.14.1),
; refused (gfx_scroll, 39.14.7) or drawn per cell (font_run, 39.14.6), and
; those are different measurements from the same row names.
; -----------------------------------------------------------------------------
gb_disp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [gb_dok], 0            ; the one-display answer, and the fallback
    mov byte [gb_dn], 1             ; for a kernel that publishes nothing
    mov byte [gb_dix], 0xFF
    mov byte [gb_dstrad], 0
    mov word [gb_dvx], 0
    mov word [gb_dvy], 0
    mov ax, [gb_vw]
    mov [gb_dcw], ax
    mov ax, [gb_vh]
    mov [gb_dch], ax

    mov ax, DBG_TAG_VIDEO
    call bl_dbgfind
    jc .out
    mov si, bx
    mov bx, [es:si+6]               ; -> vid_ndisp, or 0 on a kern_small kernel
    or bx, bx
    jz .out                         ; single-display by CONSTRUCTION: nothing
    mov al, [es:bx]                 ; to report, rather than a 1 read out of a
    mov [gb_dn], al                 ; byte that is not there
    mov cx, [es:si+12]              ; VID_CTX_SZ
    mov si, [es:si+10]              ; ...and the records
    xor di, di
.scan:
    mov ax, [es:si+VCTX_VX]         ; is the sandbox's origin inside this one?
    cmp [gb_x], ax
    jb .next
    mov dx, ax
    add dx, [es:si+VCTX_CW]
    cmp [gb_x], dx
    jae .next
    mov ax, [es:si+VCTX_VY]
    cmp [gb_y], ax
    jb .next
    mov dx, ax
    add dx, [es:si+VCTX_CH]
    cmp [gb_y], dx
    jb .found
.next:
    add si, cx
    inc di
    mov al, [gb_dn]
    xor ah, ah
    cmp di, ax
    jb .scan
    jmp short .out                  ; the DEAD ZONE (39.2.1): no display claims
                                    ; it, so there is no record to report and
                                    ; the primary's numbers are the best guess
.found:
    mov [gb_dix], di
    mov al, [es:si+VCTX_KIND]
    mov [gb_dkind], al
    mov [gb_kind], al               ; ...and THIS is what renames the file
    mov ax, [es:si+VCTX_VX]
    mov [gb_dvx], ax
    mov ax, [es:si+VCTX_VY]
    mov [gb_dvy], ax
    mov ax, [es:si+VCTX_CW]
    mov [gb_dcw], ax
    mov ax, [es:si+VCTX_CH]
    mov [gb_dch], ax
    mov ax, [es:si+VCTX_SEG]        ; the four framebuffer numbers, from the
    mov [gb_fbseg], ax              ; live context rather than gb_vtab
    mov ax, [es:si+VCTX_STRIDE]
    mov [gb_stride], ax
    mov ax, [es:si+VCTX_BMASK]
    mov [gb_bmask], ax
    mov ax, [es:si+VCTX_BSHIFT]
    mov [gb_bshift], ax
    mov byte [gb_dok], 1

    mov ax, [gb_cx]                 ; ...and does the CONTENT BOX fit inside it?
    add ax, [gb_cw]                 ; (the sandbox's origin does by construction
    mov dx, [gb_dvx]                ; - that is how the display was chosen)
    add dx, [gb_dcw]
    cmp ax, dx
    ja .strad
    mov ax, [gb_cy]
    add ax, [gb_ch]
    mov dx, [gb_dvy]
    add dx, [gb_dch]
    cmp ax, dx
    jbe .out
.strad:
    mov byte [gb_dstrad], 1
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
; gb_rowbase - the framebuffer offset of scan line AX (SPEC.md 39.3)
; in:       AX = y
; out:      AX = byte offset
; clobbers: AX, CX, DX, flags
;
; gfx_rowbase's arithmetic, in the package: bank = y & bmask, and the row
; within that bank is y >> bshift. The kernel's copy is not reachable through
; the API and there is no slot that answers "where is scan line y", so a
; package that wants to touch the framebuffer has to carry this - which is
; exactly why the rows built from it are confined to this window's own columns.
; -----------------------------------------------------------------------------
gb_rowbase:
    push bx
    mov bx, ax
    and bx, [gb_bmask]              ; BX = the bank
    mov cl, 13
    shl bx, cl                      ; ...times 0x2000
    mov cl, [gb_bshift]
    shr ax, cl                      ; AX = the row inside it
    mul word [gb_stride]            ; DX:AX; DX is 0 at every geometry here
    add ax, bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; gb_mktab - pick the adapter record and fill the two bandwidth row tables
;
; The record comes from the DISPLAY the sandbox is on, not from the machine's
; primary (SPEC.md 39.14.7's report): gb_disp has already taken seg / stride /
; bmask / bshift out of that display's own context when there is more than one,
; and the static gb_vtab is the one-display answer. The rows are built from
; LOCAL coordinates for the same reason - gb_rowbase computes an offset into a
; framebuffer, and the sandbox's x is the VIRTUAL desktop's.
; -----------------------------------------------------------------------------
gb_mktab:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [gb_dok], 0
    jne .port                       ; gb_disp took them from the live context
    mov al, [gb_kind]               ; the adapter record: seg, stride, bmask,
    xor ah, ah                      ; bshift - vid_tab's four numbers, which a
    mov bx, 8                       ; package has no other way to learn
    mul bx
    mov si, gb_vtab
    add si, ax
    mov bx, [si]
    mov [gb_fbseg], bx
    mov bx, [si+2]
    mov [gb_stride], bx
    mov bx, [si+4]
    mov [gb_bmask], bx
    mov bx, [si+6]
    mov [gb_bshift], bx
.port:
    mov al, [gb_kind]               ; the status-port record is 4 bytes, and it
    xor ah, ah                      ; is still keyed on the KIND - which is the
    mov bx, 4                       ; sandbox's display's kind now
    mul bx
    mov si, gb_ptab
    add si, ax
    mov bx, [si]
    mov [gb_stat], bx
    mov bx, [si+2]
    mov [gb_vsbit], bx

    mov ax, [gb_x]                  ; the byte column our content starts at,
    sub ax, [gb_dvx]                ; on ITS OWN card (gb_dvx is 0 with one
    mov cl, 3                       ; display, so this is free there)
    shr ax, cl
    mov [gb_xbyte], ax

    mov di, gb_vrow                 ; --- the framebuffer table. SI is the row
    mov si, GB_BWROWS               ; counter and NOT cx, because gb_rowbase
    mov bx, [gb_y]                  ; loads two shift counts into CL: a `loop`
    sub bx, [gb_dvy]                ; ...and local rows, gb_xbyte's reason
.v:                                 ; here ran 65,536 times and wrote a word
    mov ax, bx                      ; every two bytes across the whole segment
    call gb_rowbase
    add ax, [gb_xbyte]
    mov [di], ax
    add di, 2
    inc bx
    dec si
    jnz .v

    mov di, gb_rrow                 ; --- and the RAM one, the same shape so
    mov cx, GB_BWROWS               ; the two only differ in the segment
    mov ax, gb_ram
.r:
    mov [di], ax
    add di, 2
    add ax, GB_BWCOLS
    loop .r
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; gb_mkblit - build the two 4bpp blit sources (SPEC.md 5.4)
;
; SOLID is one run per row: the best case gfx_blit4's run coalescing was
; written for. STRIPE is a new colour every four pixels - sixteen runs a row,
; which is what a picture costs. The PAIR is the point: PERFORMANCE.md Part 3
; item 4 is a version of this primitive that kept its shape and lost its
; substance, and one number cannot show that. Two can.
; -----------------------------------------------------------------------------
gb_mkblit:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    push ds
    pop es
    cld
    mov di, gb_bsolid
    mov cx, GB_BLITSZ
    mov al, 0xFF                    ; two pixels of colour 15 per byte
    rep stosb
    mov di, gb_bstripe
    mov cx, GB_BLITSZ
    xor bx, bx                      ; BX = the byte index
.s:
    mov ax, bx
    shr ax, 1                       ; two bytes - four pixels - per colour
    and ax, 15
    mov dl, 0x11                    ; ...and both nibbles that colour
    mul dl                          ; AX = colour * 0x11, always under 256
    stosb
    inc bx
    loop .s
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; the report blocks
; =============================================================================

; -----------------------------------------------------------------------------
; gb_hint - the report an unrun harness shows
;
; Built out of the same arena the results use, so it pages, saves and scrolls
; like anything else and costs no drawing code of its own. A run replaces it.
; -----------------------------------------------------------------------------
gb_hint:
    push si
    mov si, gb_s_ttl1
    call bl_sline
    mov si, gb_s_ttl2
    call bl_sline
    call bl_blank
    mov si, gb_h_1
    call bl_sline
    mov si, gb_h_2
    call bl_sline
    call bl_blank
    mov si, gb_h_3
    call bl_sline
    mov si, gb_h_4
    call bl_sline
    mov si, gb_h_5
    call bl_sline
    call bl_blank
    mov si, gb_h_6
    call bl_sline
    mov si, gb_h_6b
    call bl_sline
    mov si, gb_h_7
    call bl_sline
    pop si
    ret

gb_header:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, gb_s_ttl1
    call bl_sline
    mov si, gb_s_ttl2
    call bl_sline
    call bl_blank

    mov si, gb_l_adapter            ; the adapter, named
    mov di, gb_n_vga
    cmp byte [gb_kind], VID_HERC
    jne .c1
    mov di, gb_n_herc
.c1:
    cmp byte [gb_kind], VID_CGA
    jne .c2
    mov di, gb_n_cga
.c2:
    call bl_kvs
    mov si, gb_l_screen
    mov ax, [gb_vw]
    call gb_num
    mov si, gb_l_rows
    mov ax, [gb_vh]
    call gb_num
    cmp byte [gb_dn], 1             ; ...and on a two-card machine, WHICH card
    jbe .one                        ; the rows below were measured on: every
    mov si, gb_l_ndisp              ; number from here down is that display's,
    mov al, [gb_dn]                 ; and the screen pair above is the UNION
    xor ah, ah                      ; (SPEC.md 39.16)
    call gb_num
    mov si, gb_l_dix
    mov al, [gb_dix]
    xor ah, ah
    call gb_num
    mov si, gb_l_dorg
    mov ax, [gb_dvx]
    call gb_num
    mov si, gb_l_dorgy
    mov ax, [gb_dvy]
    call gb_num
    mov si, gb_l_dw
    mov ax, [gb_dcw]
    call gb_num
    mov si, gb_l_dh
    mov ax, [gb_dch]
    call gb_num
    mov si, gb_l_strad              ; THE row that changes what the rest means
    mov al, [gb_dstrad]
    xor ah, ah
    call gb_num
.one:
    mov si, gb_l_bpp
    mov al, [gb_bpp]
    xor ah, ah
    call gb_num
    mov si, gb_l_dock
    mov ax, [gb_dock]
    call gb_num
    mov si, gb_l_fbseg
    mov ax, [gb_fbseg]
    call gb_hex
    mov si, gb_l_stride
    mov ax, [gb_stride]
    call gb_num
    mov si, gb_l_banks
    mov ax, [gb_bmask]
    inc ax
    call gb_num
    mov si, gb_l_stat
    mov ax, [gb_stat]
    call gb_hex

    mov si, gb_l_conw               ; the window the numbers were taken in
    mov ax, [gb_cw]
    call gb_num
    mov si, gb_l_conh
    mov ax, [gb_ch]
    call gb_num
    mov si, gb_l_cells
    mov ax, [bl_vcols]
    call gb_num
    mov si, gb_l_crows
    mov ax, [bl_prows]
    call gb_num
    mov si, gb_l_boxx
    mov ax, [gb_x]
    call gb_num

    mov si, gb_l_cpu                ; and the machine
    mov al, [gb_cpu]
    xor ah, ah
    call gb_num
    mov si, gb_l_feat
    mov al, [gb_cpu+1]
    xor ah, ah
    call gb_hex
    mov si, gb_l_kern
    mov ax, [gb_syskb + SK_KERN]
    call gb_num
    mov si, gb_l_heap
    mov ax, [gb_syskb + SK_HEAP]
    call gb_num
    mov si, gb_l_mlarge
    mov ax, [gb_mlarge]
    call gb_num
    mov si, gb_l_mtotal
    mov ax, [gb_mtotal]
    call gb_num
    mov si, gb_l_xms
    mov ax, [gb_syskb + SK_XMS]
    call gb_num
    call OSAPI_BOOT_TICKS           ; the same machine facts sysbench prints,
    cmp ax, 0xFFFF                  ; so a gfxbench report alone still says
    je .noboot                      ; which machine and which boot it came off
    mov si, gb_l_boott              ; (SPEC.md 15.4; PERFORMANCE.md Part 9's
    call gb_num                     ; "what to record with the numbers")
    jmp short .snd
.noboot:
    mov si, gb_l_boott
    mov di, gb_n_nostamp
    call bl_kvs
.snd:
    call OSAPI_SND_CAPS             ; a sound driver attached, or not
    mov si, gb_l_snd
    call gb_hex
    call OSAPI_FILE_HERE            ; ...and which volume the report will
    add bl, 'A'                     ; land on, which is the commonest way to
    mov [gb_volch], bl              ; lose one. BL is the drive, not AL
    mov si, gb_l_vol
    mov di, gb_volch
    call bl_kvs

    call bl_blank
    mov si, gb_s_pit1
    call bl_sline
    mov si, gb_s_pit2
    call bl_sline
    mov si, gb_s_pit3
    call bl_sline
    mov si, gb_l_ovh
    mov ax, [gb_bcnt]
    mov dx, [gb_bcnt+2]
    mov cx, 9
    call bl_kv
    mov ax, [gb_bcnt]
    mov dx, [gb_bcnt+2]
    mov cx, BL_BASE_N
    call bl_us100
    mov si, gb_l_ovh1
    mov cx, 9
    call bl_kv
    call bl_blank
    mov si, gb_s_warn1
    call bl_sline
    mov si, gb_s_warn2
    call bl_sline
    mov si, gb_s_warn3
    call bl_sline
    mov si, gb_s_warn4
    call bl_sline
    mov si, gb_s_warn5
    call bl_sline
    mov si, gb_s_warn6
    call bl_sline
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gb_num - SI = label, AX = an unsigned word: one "label   N" line
gb_num:
    push cx
    push dx
    xor dx, dx
    mov cx, 9
    call bl_kv
    pop dx
    pop cx
    ret

; gb_hex - SI = label, AX = a word: one "label   NNNN" line
gb_hex:
    push di
    call bl_lclr
    xor di, di
    call bl_lput
    mov di, BL_C_N
    call bl_hex4
    call bl_lcommit
    pop di
    ret

; --- block 1: raw memory and framebuffer bandwidth ---------------------------
gb_bw:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, gb_s_h_bw
    call bl_sline
    call bl_head

    mov ax, ds                      ; --- RAM, through the loop below
    mov [gb_seg], ax
    mov word [gb_tab], gb_rrow
    mov word [bl_n], 8
    mov word [bl_body], gb_b_stosw
    mov si, gb_r_rw
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tram_w], ax
    mov [gb_tram_w+2], dx
    mov word [bl_body], gb_b_stosb
    mov si, gb_r_rb
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_read
    mov si, gb_r_rr
    xor al, al
    call bl_run
    mov word [bl_n], 4
    mov word [bl_body], gb_b_rmw
    mov si, gb_r_rm
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tram_m], ax
    mov [gb_tram_m+2], dx

    mov ax, [gb_fbseg]              ; --- and the framebuffer, through the same
    mov [gb_seg], ax
    mov word [gb_tab], gb_vrow
    mov word [bl_n], 8
    mov word [bl_body], gb_b_stosw
    mov si, gb_r_vw
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tvid_w], ax
    mov [gb_tvid_w+2], dx
    mov word [bl_body], gb_b_stosb
    mov si, gb_r_vb
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_read
    mov si, gb_r_vr
    xor al, al
    call bl_run
    mov word [bl_n], 4
    mov word [bl_body], gb_b_rmw
    mov si, gb_r_vm
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tvid_m], ax
    mov [gb_tvid_m+2], dx

    mov word [bl_n], 200            ; the ISA I/O cost itself, which is what
    mov word [bl_body], gb_b_inport ; makes a status-port poll expensive
    mov si, gb_r_in
    xor al, al
    call bl_run
    call gb_b_vsync                 ; ...and one whole vertical retrace period:
    mov word [bl_n], 12             ; the refresh rate, measured. The UNTIMED
    mov word [bl_body], gb_b_vsync  ; call first is the fix for a real bias:
                                    ; the body waits for the bit to fall and
                                    ; then rise, so it leaves the phase AT a
                                    ; rising edge and every later iteration is
                                    ; a whole frame - but the FIRST one starts
                                    ; wherever the suite happened to be and can
                                    ; return almost at once. At N = 4 that was
                                    ; up to a quarter of the answer: a CGA read
                                    ; 80.6 Hz where three of its four
                                    ; iterations were a clean 60.4
    mov si, gb_r_vs
    xor al, al
    call bl_run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 2: the drawing primitives -----------------------------------------
gb_prims:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, gb_s_h_prim
    call bl_sline
    call bl_head
    mov al, CBLACK
    call OSAPI_SET_COLOR

    mov word [bl_n], 300
    mov word [bl_body], gb_b_pixel
    mov si, gb_r_px
    xor al, al
    call bl_run

    call gb_box8                    ; hline at 8 px, then at GB_BOXW
    mov word [bl_n], 200
    mov word [bl_body], gb_b_hline
    mov si, gb_r_hl8
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_thl8], ax
    mov [gb_thl8+2], dx
    call gb_boxfull
    mov word [bl_n], 60
    mov word [bl_body], gb_b_hline
    mov si, gb_r_hlw
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_thlw], ax
    mov [gb_thlw+2], dx

    call gb_box8                    ; vline at 8 px, then at GB_BOXH
    mov word [bl_n], 200
    mov word [bl_body], gb_b_vline
    mov si, gb_r_vl8
    xor al, al
    call bl_run
    call gb_boxfull
    mov word [bl_n], 40
    mov word [bl_body], gb_b_vline
    mov si, gb_r_vlh
    xor al, al
    call bl_run

    call gb_box8                    ; fill: 8x8, 64x64, then the whole sandbox
    mov word [bl_n], 200
    mov word [bl_body], gb_b_fill
    mov si, gb_r_f8
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tf8], ax
    mov [gb_tf8+2], dx
    call gb_box64
    mov word [bl_n], 24
    mov word [bl_body], gb_b_fill
    mov si, gb_r_f64
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tf64], ax
    mov [gb_tf64+2], dx
    mov word [bl_body], gb_b_clipfill   ; the SAME fill under an armed clip
    mov word [bl_n], 24                 ; region (SPEC.md 11.3) - what every
    mov si, gb_r_f64c                   ; covered background window pays. Next
    xor al, al                          ; to its own unclipped row on purpose:
    call bl_run                         ; the gap is the region's cost, and the
    mov word [bl_body], gb_b_fill       ; API block prices SET+CLEAR alone

    ; --- gfx_line, four rows whose sizes are predicted in advance ------------
    ; SPEC.md 5.6.6 made a STEEP dilated line one Bresenham walk instead of
    ; three, and left "how much cheaper" unsettled: tests/linetest said
    ; 1.3x-1.9x, which is the spread of the SAME two builds measured four
    ; times, because it was taken as QEMU host time - the one thing Part 4
    ; says is not a measurement. These four rows settle it, in instructions
    ; under -icount and in microseconds on iron.
    ;
    ; The two geometries are the SAME LINE TRANSPOSED - 32x127 against 127x32,
    ; so 128 pixels each - which is what makes the comparison mean anything.
    ; Predicted, and a run that disagrees has found something: the two THIN
    ; rows should match; shallow fat should be about 3x its thin row, because
    ; it still walks three times; and steep fat should be well under 3x, which
    ; is the whole claim. The derived block prints both ratios.
    mov word [gb_lfat], 0
    mov word [bl_n], 24
    mov word [bl_body], gb_b_lsteep
    mov si, gb_r_lst
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tlst], ax
    mov [gb_tlst+2], dx
    mov word [bl_body], gb_b_lshal
    mov si, gb_r_lsh
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tlsh], ax
    mov [gb_tlsh+2], dx
    mov word [gb_lfat], 1
    mov word [bl_body], gb_b_lsteep
    mov si, gb_r_lstf
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tlstf], ax
    mov [gb_tlstf+2], dx
    mov word [bl_body], gb_b_lshal
    mov si, gb_r_lshf
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tlshf], ax
    mov [gb_tlshf+2], dx

    ; --- SPEC.md 79.5.6: the private mask, against the kernel's own line -------
    mov word [bl_body], gb_b_mline
    mov si, gb_r_mline
    xor al, al
    call bl_run
    call gb_maskfill                ; a realistic density for the row below
    mov word [bl_body], gb_b_xdiff
    mov si, gb_r_xdiff
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_blit1
    mov si, gb_r_blit1
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_mclr
    mov si, gb_r_mclr
    xor al, al
    call bl_run

    ; --- many walks, one arrival (SPEC.md 5.6.8) -----------------------------
    ; The resumable walk (5.6.7) gives a moving line a cheap PIXEL and does
    ; nothing about the ARRIVAL, which for a caller stepping eight live trails
    ; is the whole cost: 5.7 prices getting into a drawing call at ~756 us
    ; whatever it then draws. gfx_lstepv is CX of those calls with the pushes,
    ; the far call, the dispatch and the ink paid ONCE.
    ;
    ; So the two rows draw the IDENTICAL eight pixels and differ only in how
    ; many times they arrive - eight against one - and the ratio is the whole
    ; measurement. 5.6.8 argues its case from 5.7's floor rather than from a
    ; measurement of itself, and tests/linetest gates the PIXELS (0 differing
    ; of 236,160) without pricing them; this is the missing half.
    ;
    ; THIS ROW ALREADY CONTRADICTED ITS OWN PREDICTION, which is why it is
    ; here. The guess was "approaching 800, because 5.7 prices an arrival at
    ; ~756 us"; it measures 118 in instructions, and ~36 instructions removed
    ; per arrival rather than 5.7's 196. The reason is structural and worth
    ; knowing: gfx_lstep is NOT a rect primitive - it never goes near
    ; vga_rect_setup or sw_rect - so its arrival is the far-call cell and a
    ; prologue, not the rect machinery 5.7 measured. 5.6.8 borrowed a floor
    ; that does not apply to it.
    ;
    ; Instructions understate the clocks here (Part 9 measured the far-call
    ; cell at 46.7 us for about seven instructions), so the field ratio will
    ; be higher than 118 - but even charging every removed instruction at
    ; far-call rates only reaches about 160, against the 356 that 5.6.8's own
    ; field figures imply (570 us a pixel stepping one call per missile
    ; against 160 in the drain). That gap is unexplained, and settling it is
    ; what these two rows are for.
    call gb_lsinit                  ; walks are 126 px long and each row steps
    mov word [bl_n], 100            ; one pixel per iteration, so 100 cannot
    mov word [bl_body], gb_b_lstep8 ; run one off its end
    mov si, gb_r_ls8
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tls8], ax
    mov [gb_tls8+2], dx
    call gb_lsinit                  ; fresh walks: the row above spent 100 px
    mov word [bl_body], gb_b_lstepv8
    mov si, gb_r_lsv8
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tlsv8], ax
    mov [gb_tlsv8+2], dx
    call gb_boxfull
    mov word [bl_n], 6
    mov word [bl_body], gb_b_fill   ; RESTORE it: this used to be carried over
    mov si, gb_r_fbox               ; from the 64x64 fill twelve lines up, and
    xor al, al                      ; then the line and lstep blocks were
    call bl_run                     ; inserted between the two (see gb_boxrow)
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tfbox], ax
    mov [gb_tfbox+2], dx
    call gb_boxrow                  ; ...and ONE row of that same width. It is
    mov word [bl_n], 100            ; the third fill size because two could not
    mov word [bl_body], gb_b_fill   ; separate the per-CALL term from the
    mov si, gb_r_frow               ; per-ROW one: fitting c + a*rows + b*px to
    xor al, al
    call bl_run                     ; 8x8 / 64x64 / 256x128 gave a NEGATIVE c
    mov ax, [bl_last]               ; (PERFORMANCE.md Part 9 Set 1). Holding
    mov dx, [bl_last+2]             ; the width fixed and varying only the rows
    mov [gb_tfrow], ax              ; leaves nothing else in the subtraction
    mov [gb_tfrow+2], dx

    call gb_box64                   ; the rest of the rect family, all 64x64,
    mov word [bl_n], 24             ; so the column compares like for like
    mov word [bl_body], gb_b_frame
    mov si, gb_r_fr
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_gray
    mov si, gb_r_gy
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_pat
    mov si, gb_r_pt
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_xfill
    mov si, gb_r_xf
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_xrect
    mov si, gb_r_xr
    xor al, al
    call bl_run

    ; THE DRAG/ZOOM OUTLINE AT WINDOW SIZE, and one row of the same width to
    ; separate the two terms in it. An outline is four strips and the two
    ; VERTICALS are one framebuffer read-modify-write per scan line, so the
    ; cost of the transient overlay every window animation would be built on
    ; is dominated by its HEIGHT, not by its area - and the 64x64 row above
    ; is far too small to show that. (256x128 - gb_boxfull) minus (256x1 -
    ; gb_boxrow) is 252 vertical scan lines and nothing else.
    call gb_boxfull
    mov word [bl_n], 6
    mov word [bl_body], gb_b_xrect
    mov si, gb_r_xrb
    xor al, al
    call bl_run
    call gb_boxrow
    mov word [bl_n], 24
    mov word [bl_body], gb_b_xrect
    mov si, gb_r_xrr
    xor al, al
    call bl_run
    call gb_box64                   ; ...and put the sandbox back, or the blit
                                    ; below inherits a one-row rect

    mov word [gb_src], gb_bsolid    ; the blit, both ways round
    mov word [bl_n], 12
    mov word [bl_body], gb_b_blit
    mov si, gb_r_bs
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tbs], ax
    mov [gb_tbs+2], dx
    mov word [gb_src], gb_bstripe
    mov word [bl_n], 6
    mov word [bl_body], gb_b_blit
    mov si, gb_r_bn
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tbn], ax
    mov [gb_tbn+2], dx

    call gb_boxfull                 ; the blit's alternative: move the pixels
    mov word [bl_n], 16             ; you already have (SPEC.md 5.5)
    mov word [bl_body], gb_b_scroll
    mov si, gb_r_sc
    xor al, al
    call bl_run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 3: text -----------------------------------------------------------
gb_text:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, gb_s_h_text
    call bl_sline
    mov si, gb_s_h_text2
    call bl_sline
    call bl_head
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [gb_x]
    mov [gb_tx], ax

    mov word [bl_n], 40
    mov word [bl_body], gb_b_fchar
    mov si, gb_r_ch
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tcell], ax
    mov [gb_tcell+2], dx

    mov word [bl_n], 12
    mov word [bl_body], gb_b_fstr
    mov si, gb_r_st
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_pair
    mov si, gb_r_pa
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_frun
    mov si, gb_r_ru
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_trun], ax
    mov [gb_trun+2], dx

    ; SPEC.md 6.1.7's question: a run of 20, once as ordinary text and once
    ; SPACE-PADDED, which is what this system actually draws - 27.2 makes a
    ; Note Pad row's padding its ERASE, 12.9 composes the menu bar out to the
    ; clock, and the Task Manager's columns are largely spaces. The two are the
    ; same LENGTH so the pair isolates content from size.
    mov word [bl_body], gb_b_frun20
    mov si, gb_r_ru20
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_frunp
    mov si, gb_r_rup
    xor al, al
    call bl_run

    mov ax, [gb_x]                  ; ...and the same two five pixels right.
    add ax, 5                       ; NOT one pixel: the ROM font's rightmost
    mov [gb_tx], ax                 ; column is blank in every glyph, so a
                                    ; one-pixel shift spills nothing into the
                                    ; second byte and flatters the status quo
                                    ; (tests/fontbench, FB_SKEW). A dragged
                                    ; window lands here seven times in eight
    mov word [bl_body], gb_b_pair
    mov si, gb_r_pa5
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tpair5], ax
    mov [gb_tpair5+2], dx
    mov word [bl_body], gb_b_frun
    mov si, gb_r_ru5
    xor al, al
    call bl_run

    mov ax, [gb_x]
    mov [gb_tx], ax
    mov word [bl_n], 200
    mov word [bl_body], gb_b_fwidth
    mov si, gb_r_fw
    xor al, al
    call bl_run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 4: the API cells that draw nothing --------------------------------
gb_api:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, gb_s_h_api
    call bl_sline
    call bl_head
    mov word [bl_n], 300
    mov word [bl_body], gb_b_ticks
    mov si, gb_r_gt
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tapi], ax
    mov [gb_tapi+2], dx
    mov word [bl_body], gb_b_color
    mov si, gb_r_sc2
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_cont
    mov si, gb_r_wc
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_geom
    mov si, gb_r_wg
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_obsc
    mov si, gb_r_wo
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_cliptest
    mov si, gb_r_ct
    xor al, al
    call bl_run
    mov word [bl_n], 100
    mov word [bl_body], gb_b_clip
    mov si, gb_r_cs
    xor al, al
    call bl_run
    mov word [bl_n], 200
    mov word [bl_body], gb_b_mouse
    mov si, gb_r_mo
    xor al, al
    call bl_run
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 5: composite window work ------------------------------------------
gb_composite:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, gb_s_h_comp
    call bl_sline
    call bl_head
    mov word [bl_n], 100            ; the gfx lock, in the only order a
    mov word [bl_body], gb_b_lockpair   ; callback can reach it (see the
    mov si, gb_r_lock               ; header). About 11 ms a pair on the target
    xor al, al                      ; machine, so 100 of them is a second and
    call bl_run                     ; no single one comes near the 55 ms wrap
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tlock], ax
    mov [gb_tlock+2], dx
    mov word [bl_n], 4              ; one TITLE_H strip (SPEC.md 11.92)
    mov word [bl_body], gb_b_title
    xor al, al
    mov si, gb_r_ti
    call bl_run
    mov word [bl_n], 12             ; one full-width opaque text row
    mov word [bl_body], gb_b_row
    mov si, gb_r_rowdraw
    xor al, al
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_trow], ax
    mov [gb_trow+2], dx
    mov word [bl_n], 2              ; ...and the whole page of them. Method T:
    mov word [bl_body], gb_b_page   ; this is seconds on the target machine,
    mov si, gb_r_page               ; which IS the finding
    mov al, 1
    call bl_run
    mov ax, [bl_last]
    mov dx, [bl_last+2]
    mov [gb_tpage], ax
    mov [gb_tpage+2], dx
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- block 6: the same machine with no window around it ----------------------
;
; SPEC.md 11.2's fullscreen surface IS a real window - the frame becomes the
; whole screen and wm_draw_win draws no chrome at all - so every primitive
; below runs against identical code at a different place on the glass.
;
; THE PRIMITIVE ROWS ARE HERE TO BE BORING. If the per-call floor really is
; CPU-side setup (PERFORMANCE.md Part 2: two cards 13% apart at the bus priced
; GFX_PIXEL 0.008% apart) then where the sandbox sits cannot matter, and these
; rows should land on their windowed twins above. One that does NOT has found
; something position-dependent that nobody believed was - a bank boundary, an
; alignment, a clip - and that is worth a whole run to know. They carry the
; SAME labels as their twins so the two can be diffed by name.
;
; The COMPOSITE rows are here because they genuinely change: there is no title
; bar to redraw and the page is the whole screen tall rather than one window's
; content. WM_TITLE is deliberately NOT among them: under WF_FULL the frame is
; the content, so wm_title_set would letter a title bar over the app's own top
; 18 rows - a question about the kernel, not a measurement of it.
;
; A CAVEAT ON READING THE PRIMITIVE PAIRS USED TO LIVE HERE, and SPEC.md 32's
; removal retired it: [bb_mono] was a one-way flag that made every fullscreen
; row come in slightly under its twin whenever something drawn between the two
; passes used a colour other than 0 or 15. Neither the flag nor its check
; exists now, and the pairs are a clean A/B on every adapter. Figures in this
; file taken before that removal were measured while it did.
;
; And the entering and leaving is itself a measurement nothing else here can
; reach. wm_fullscreen is the ONE window-composition call a package may make
; while it holds the gfx lock - OSAPI_WM_RESIZE says "Do NOT hold the gfx
; lock" in as many words, and WM_SHOW/HIDE/FRONT take it themselves, so from
; inside a window callback they are a deadlock rather than a measurement.
; Its ENTER is a resize to the whole screen plus a repaint of this window; its
; EXIT is a restore plus a wm_paint_all - the whole-screen repaint Part 1
; calls a "visible redraw", that Part 5's budget table is entirely organised
; around avoiding, and that no field set has ever put a number on. What is in
; it: the desktop dither, the drive zones, the dock, the menu bar and every
; visible window's frame and W_PAINT - one of which is this report, whose own
; cost the `whole page of rows` row above prices separately.
gb_fs:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, gb_s_h_fs
    call bl_sline
    mov si, gb_s_h_fs2
    call bl_sline
    call bl_head

    mov word [bl_n], 4              ; in and out as one body: the enter cannot
    mov word [bl_body], gb_b_fspair ; be repeated without the exit. Method T -
    mov si, gb_r_fspair             ; a wm_paint_all is seconds on the target
    mov al, 1                       ; and would lap the PIT ten times over
    call bl_run

    mov al, 1                       ; ...and now stay there for the rows
    mov bx, [gb_win]
    call OSAPI_FULLSCREEN
    jc .refused
    call gb_geom                    ; the sandbox follows the content box

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov word [bl_n], 300
    mov word [bl_body], gb_b_pixel
    mov si, gb_r_px
    xor al, al
    call bl_run
    call gb_box64
    mov word [bl_n], 24
    mov word [bl_body], gb_b_fill
    mov si, gb_r_f64
    xor al, al
    call bl_run
    mov word [bl_n], 40
    mov word [bl_body], gb_b_fchar
    mov si, gb_r_ch
    xor al, al
    call bl_run
    mov word [bl_n], 12
    mov word [bl_body], gb_b_frun
    mov si, gb_r_ru
    xor al, al
    call bl_run
    mov word [bl_body], gb_b_row
    mov si, gb_r_rowdraw
    xor al, al
    call bl_run
    mov word [bl_n], 2
    mov word [bl_body], gb_b_page
    mov si, gb_r_page
    mov al, 1
    call bl_run

    mov al, 0
    mov bx, [gb_win]
    call OSAPI_FULLSCREEN           ; back to a window, and back to the
    call gb_geom                    ; sandbox the rest of the report used
    jmp short .out
.refused:
    mov si, gb_s_fsno               ; another window owns the screen: say so
    call bl_sline                   ; rather than leave a silent gap
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; gb_b_fspair - enter fullscreen and leave again. Idempotent by construction,
; which is what lets bl_run repeat it.
gb_b_fspair:
    push bx
    mov bx, [gb_win]
    mov al, 1
    call OSAPI_FULLSCREEN
    jc .out                         ; refused: the exit would be a no-op too
    mov bx, [gb_win]
    xor al, al
    call OSAPI_FULLSCREEN
.out:
    pop bx
    ret

; --- block 7: what the two-size rows imply -----------------------------------
;
; Every figure here is a subtraction or a division of rows printed above, so a
; wrong one is visible beside its inputs (PERFORMANCE.md Part 6 rule 7). The
; page prediction and the measured page are the deliberate redundancy: they are
; computed from different rows and must agree.
gb_derived:
    push ax
    push bx
    push cx
    push dx
    push si
    call bl_blank
    mov si, gb_s_h_der
    call bl_sline

    mov ax, [gb_tf64]               ; fill: (64x64 - 8x8) over the pixels
    mov dx, [gb_tf64+2]             ; between them. Both rows are scaled to one
    mov cx, 24                      ; call first, or the different N's dominate
    call gb_percall
    call gb_stash
    mov ax, [gb_tf8]
    mov dx, [gb_tf8+2]
    mov cx, 200
    call gb_percall
    call gb_sub                     ; DX:AX = stash - this
    mov cx, 4096 - 64
    call gb_nsper
    mov si, gb_d_fillpx
    call gb_num32

    mov ax, [gb_thlw]               ; hline: the same, over 248 pixels
    mov dx, [gb_thlw+2]
    mov cx, 60
    call gb_percall
    call gb_stash
    mov ax, [gb_thl8]
    mov dx, [gb_thl8+2]
    mov cx, 200
    call gb_percall
    call gb_sub
    mov cx, GB_BOXW - 8
    call gb_nsper
    mov si, gb_d_hlpx
    call gb_num32

    mov ax, [gb_tfbox]              ; the fill's SECOND slope, from 64x64 up to
    mov dx, [gb_tfbox+2]            ; the whole sandbox. If it disagrees with
    mov cx, 6                       ; the first, the cost is not linear in
    call gb_percall                 ; PIXELS - and on a 1bpp adapter it is not:
    call gb_stash                   ; it is ~156 us per ROW plus 0.32 per pixel,
    mov ax, [gb_tf64]               ; so a narrow rect is nearly all setup.
    mov dx, [gb_tf64+2]             ; One slope cannot show that; two can
    mov cx, 24
    call gb_percall
    call gb_sub
    mov cx, 32768 - 4096
    call gb_nsper
    mov si, gb_d_fillpx2
    call gb_num32

    mov ax, [gb_tlstf]              ; SPEC.md 5.6.6, settled: a dilated line is
    mov dx, [gb_tlstf+2]            ; three Bresenham walks unless it is STEEP,
    mov bx, [gb_tlst]               ; where one walk writing a three-bit mask
    mov cx, [gb_tlst+2]             ; is the identical pixel set. So the shallow
    call gb_ratio                   ; ratio is the control and should sit near
    mov si, gb_d_lsteep             ; 300; the steep one is the answer
    call gb_num32

    mov ax, [gb_tlshf]
    mov dx, [gb_tlshf+2]
    mov bx, [gb_tlsh]
    mov cx, [gb_tlsh+2]
    call gb_ratio
    mov si, gb_d_lshal
    call gb_num32

    mov ax, [gb_tls8]               ; SPEC.md 5.6.8: eight arrivals over one,
    mov dx, [gb_tls8+2]             ; for the same eight pixels. 118 in
    mov bx, [gb_tlsv8]              ; instructions under -icount; higher on
    mov cx, [gb_tlsv8+2]            ; iron, because what comes off is far-call
    call gb_ratio                   ; cells. 5.6.8's own field figures imply
    mov si, gb_d_lsv                ; 356 and nothing reconciles that yet
    call gb_num32

    ; ...and the two rows are two equations in two unknowns, so the walk comes
    ; apart with no extra rows at all. A = 100(8a + 8p), B = 100(a + 8p), so
    ; A - B = 700a and B - 100a = 800p. THIS is what settles 5.6.8: if a
    ; pixel dominates an arrival then batching cannot be where the cost is,
    ; whatever the ratio above says, and the field's 570 us a pixel becomes a
    ; number this report can be held against directly.
    ; BOTH SUBTRACTIONS GO THROUGH gb_sub, which floors at zero, and that is
    ; not defensive tidiness - it is a bug this row shipped with for one run.
    ; A raw `sub`/`sbb` of two measured totals UNDERFLOWS the moment the
    ; vector row measures larger than the scalar one, which is exactly what
    ; noise does when the two are close, and 4 billion divided by 700 is a
    ; large plausible-looking number in a report meant to be carried off a
    ; machine (PERFORMANCE.md Part 6 rule 3: the failure mode is a number that
    ; looks fine). Floored, an inverted pair reports an arrival of 0 and gives
    ; the whole cost to the pixel, which is what "the batching saved nothing
    ; measurable" honestly means.
    mov ax, [gb_tls8]               ; A - B = 700a
    mov dx, [gb_tls8+2]
    call gb_stash
    mov ax, [gb_tlsv8]
    mov dx, [gb_tlsv8+2]
    call gb_sub
    mov [gb_tlsa], ax               ; park it: bl_us100 consumes DX:AX
    mov [gb_tlsa+2], dx
    mov cx, 700
    call bl_us100
    mov si, gb_d_lsarr
    call gb_num32

    mov ax, [gb_tlsa]               ; 100a = (A - B) / 7
    mov dx, [gb_tlsa+2]
    mov cx, 1
    call bl_mul48
    mov cx, 7
    call bl_div48
    call bl_get32
    mov bx, ax
    mov cx, dx
    mov ax, [gb_tlsv8]              ; B - 100a = 800p
    mov dx, [gb_tlsv8+2]
    call gb_stash
    mov ax, bx
    mov dx, cx
    call gb_sub
    mov cx, 800
    call bl_us100
    mov si, gb_d_lspx
    call gb_num32

    mov ax, [gb_tfbox]              ; the per-ROW term, cleanly: 256x128 against
    mov dx, [gb_tfbox+2]            ; 256x1 differs by 127 rows and by NOTHING
    mov cx, 6                       ; else, so neither the per-call floor nor
    call gb_percall                 ; the per-pixel slope survives the
    call gb_stash                   ; subtraction. It should agree with the
    mov ax, [gb_tfrow]              ; two-point fit above; where they disagree,
    mov dx, [gb_tfrow+2]            ; this one is the measurement and that one
    mov cx, 100                     ; is the model
    call gb_percall
    call gb_sub
    mov cx, GB_BOXH - 1
    call gb_nsper
    mov si, gb_d_fillrow
    call gb_num32

    mov ax, [gb_tcell]              ; one glyph cell - PERFORMANCE.md Part 2's
    mov dx, [gb_tcell+2]            ; anchor number, and the one figure in that
    mov cx, 40                      ; table everything else is estimated from
    call bl_us100
    mov si, gb_d_cell
    call gb_num32

    mov ax, [gb_trun]               ; ...and one cell of a ten-cell run
    mov dx, [gb_trun+2]
    mov cx, 120
    call bl_us100
    mov si, gb_d_runcell
    call gb_num32

    mov ax, [gb_tpair5]             ; the fontbench ratio: the status quo (a
    mov dx, [gb_tpair5+2]           ; dragged window, so unaligned) over an
    mov bx, [gb_trun]               ; aligned run. SPEC.md 6.1.1 says 130 on a
    mov cx, [gb_trun+2]             ; real XT with a Hercules card
    call gb_ratio
    mov si, gb_d_runwin
    call gb_num32

    mov ax, [gb_tvid_w]             ; the bus penalty: framebuffer over RAM,
    mov dx, [gb_tvid_w+2]           ; through byte-identical loops
    mov bx, [gb_tram_w]
    mov cx, [gb_tram_w+2]
    call gb_ratio
    mov si, gb_d_busw
    call gb_num32
    mov ax, [gb_tvid_m]
    mov dx, [gb_tvid_m+2]
    mov bx, [gb_tram_m]
    mov cx, [gb_tram_m+2]
    call gb_ratio
    mov si, gb_d_busm
    call gb_num32

    mov ax, [gb_tvid_m]             ; ...and the read-modify-write in 8088
    mov dx, [gb_tvid_m+2]           ; CLOCKS, which is the "~30 cycles" SPEC.md
    mov cx, 4                       ; 39.5 quotes and nothing has re-measured
    call gb_percall
    mov cx, GB_BWBYTES
    call gb_clocks
    mov si, gb_d_rmwclk
    call gb_num32

    mov ax, [gb_tbn]                ; blit: striped over solid. A big ratio is
    mov dx, [gb_tbn+2]              ; the run coalescer earning its keep; near
    mov bx, [gb_tbs]                ; 100 would mean it is not coalescing at
    mov cx, [gb_tbs+2]              ; all, which is the Part 3 item 4 failure
    call gb_ratio
    mov si, gb_d_blit
    call gb_num32

    mov ax, [gb_tlock]              ; the gfx lock pair, in the field log's own
    mov dx, [gb_tlock+2]            ; currency (PERFORMANCE.md Part 9 Set 4):
    mov cx, 100                     ; microseconds, against the 55,000 that a
    call bl_us100                   ; frame at the tick rate has to spend
    mov si, gb_d_lockus
    call gb_num32

    mov ax, [gb_tf8]                ; ...and in this harness's own: per-call
    mov dx, [gb_tf8+2]              ; floors. The pair draws nothing the caller
    mov cx, 100                     ; asked for, so what it is WORTH in
    call gb_mul                     ; GFX_FILL 8x8 calls is what every frame
    call gb_stash                   ; pays to enter and leave the critical
    mov ax, [gb_tlock]              ; section rather than to draw.
    mov dx, [gb_tlock+2]
    mov cx, 200                     ; CROSS-MULTIPLIED, not two gb_percalls: a
    call gb_mul                     ; per-call figure here is a single-digit
    mov bx, [gb_ta]                 ; integer, so dividing first threw most of
    mov cx, [gb_ta+2]               ; the ratio away before it was taken - 1.58
    call gb_ratio                   ; counts a fill read as 1, and the answer
    mov si, gb_d_lockfill           ; came out 16.00 where it is 10.29
    call gb_num32

    mov ax, [gb_trow]               ; the whole page predicted from one row.
    mov dx, [gb_trow+2]             ; Print it beside the measured page: the
    mov cx, 12                      ; two come from different rows and must
    call gb_percall                 ; agree, which is the harness checking
    mov cx, [bl_prows]              ; itself
    call gb_mul
    mov cx, 1
    call bl_us100
    mov si, gb_d_pagepred
    call gb_num32
    mov ax, [gb_tpage]
    mov dx, [gb_tpage+2]
    mov cx, 2
    call bl_us100
    mov si, gb_d_pagereal
    call gb_num32

    mov ax, [gb_tf64]               ; a whole-screen fill from the same slope:
    mov dx, [gb_tf64+2]             ; where a wm_paint_all starts from
    mov cx, 24
    call gb_percall
    mov cx, [gb_vw]                 ; MULTIPLY BEFORE DIVIDING, and split the
    call gb_mul                     ; /4096 into two /64s: dividing first
    mov cx, 64                      ; truncated a 212-count fill to zero, and
    call gb_div                     ; doing it all at the end overflows the
    mov cx, [gb_vh]                 ; 32-bit accumulator on a slow machine,
    call gb_mul                     ; where the same fill is 3,800 counts
    mov cx, 64
    call gb_div
    mov cx, 1
    call bl_us100
    mov si, gb_d_scrfill
    call gb_num32
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

gb_trailer:
    push si
    call bl_blank
    mov si, gb_s_end1
    call bl_sline
    mov si, gb_s_end2
    call bl_sline
    mov si, gb_s_end3
    call bl_sline
    pop si
    ret

; =============================================================================
; derived-figure arithmetic. Every one of these is 32-bit in and 32-bit out,
; because a row on a 4.77 MHz machine is millions of counts and a 16-bit
; intermediate is PERFORMANCE.md Part 6 rule 3's whole subject.
; =============================================================================

; gb_num32 - SI = label, DX:AX = a 32-bit value: one report line
gb_num32:
    push cx
    mov cx, 12
    call bl_kv
    pop cx
    ret

; gb_stash / gb_sub - park DX:AX, then subtract the next value from it
gb_stash:
    mov [gb_ta], ax
    mov [gb_ta+2], dx
    ret
gb_sub:
    push bx
    mov bx, ax
    mov ax, [gb_ta]
    sub ax, bx
    mov bx, dx
    mov dx, [gb_ta+2]
    sbb dx, bx
    jnc .out
    xor ax, ax                      ; the bigger row measured smaller: noise
    xor dx, dx
.out:
    pop bx
    ret

; gb_mul / gb_div - DX:AX scaled by CX, saturating
gb_mul:
    call bl_mul48
    call bl_get32
    ret
gb_div:
    push cx
    push si
    mov si, cx
    mov cx, 1
    call bl_mul48                   ; load DX:AX into the 48-bit accumulator
    mov cx, si
    or cx, cx
    jnz .d
    mov cx, 1
.d:
    call bl_div48
    call bl_get32
    pop si
    pop cx
    ret

; gb_percall - DX:AX total counts over CX iterations -> counts per call
gb_percall:
    call gb_div
    ret

; gb_nsper - DX:AX counts over CX units -> nanoseconds per unit
gb_nsper:
    push cx
    push si
    mov si, cx
    mov cx, 838                     ; one PIT count is 838 ns
    call bl_mul48
    mov cx, si
    or cx, cx
    jnz .d
    mov cx, 1
.d:
    call bl_div48
    call bl_get32
    pop si
    pop cx
    ret

; gb_clocks - DX:AX counts over CX units -> 4.77 MHz CPU clocks per unit, x100
;
; One PIT count is EXACTLY four CPU clocks on a period machine: both divide the
; same 14.31818 MHz crystal, the PIT by 12 and the 8088 by 3. So this is not an
; approximation on the target - and on a turbo clone it is the ratio the
; sysbench CPU block reports, which is why that block exists.
gb_clocks:
    push cx
    push si
    mov si, cx
    mov cx, 400                     ; 4 clocks a count, x100 for two decimals
    call bl_mul48
    mov cx, si
    or cx, cx
    jnz .d
    mov cx, 1
.d:
    call bl_div48
    call bl_get32
    pop si
    pop cx
    ret

; gb_ratio - (DX:AX / CX:BX) * 100, both 32-bit
; Both are shifted right together until the denominator fits a word, which
; costs at most a bit of the ratio's last digit and cannot overflow.
gb_ratio:
    push bx
    push cx
    push si
.shift:
    or cx, cx
    jz .have
    shr cx, 1
    rcr bx, 1
    shr dx, 1
    rcr ax, 1
    jmp short .shift
.have:
    or bx, bx
    jnz .div
    mov bx, 1
.div:
    mov si, bx
    mov cx, 100
    call bl_mul48
    mov cx, si
    call bl_div48
    call bl_get32
    pop si
    pop cx
    pop bx
    ret

; =============================================================================
; the measured bodies. Each reads its parameters from memory, so bl_time's loop
; passes nothing and no setup is inside the measured span.
; =============================================================================

gb_box8:
    push ax
    mov ax, [gb_x]
    add ax, 7
    mov [gb_x2], ax
    mov ax, [gb_y]
    add ax, 7
    mov [gb_y2], ax
    pop ax
    ret

gb_box64:
    push ax
    mov ax, [gb_x]
    add ax, 63
    mov [gb_x2], ax
    mov ax, [gb_y]
    add ax, 63
    mov [gb_y2], ax
    pop ax
    ret

; ONE row of the full width: gb_boxfull's twin, and the other end of the
; per-row measurement (see the fill block).
gb_boxrow:
    push ax
    mov ax, [gb_x]
    add ax, GB_BOXW - 1
    mov [gb_x2], ax
    mov ax, [gb_y]
    mov [gb_y2], ax
    pop ax
    ret

gb_boxfull:
    push ax
    mov ax, [gb_x]
    add ax, GB_BOXW - 1
    mov [gb_x2], ax
    mov ax, [gb_y]
    add ax, GB_BOXH - 1
    mov [gb_y2], ax
    pop ax
    ret

gb_b_pixel:
    mov cx, [gb_x]
    mov dx, [gb_y]
    call OSAPI_GFX_PIXEL
    ret

gb_b_hline:
    mov ax, [gb_x]
    mov bx, [gb_x2]
    mov dx, [gb_y]
    call OSAPI_GFX_HLINE
    ret

gb_b_vline:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov dx, [gb_y2]
    call OSAPI_GFX_VLINE
    ret

; The fill again, with this window's own clip region armed. With nothing on
; top the region is one rectangle, so gfx_clip_run re-enters the raw body
; exactly once and the row measures the ARMING plus one fragment - which is
; the cheapest case, and the one a background painter pays on a quiet desktop.
; GB_NWALK resumable walks (SPEC.md 5.6.7) side by side in the sandbox, 126
; pixels long so a 100-iteration row cannot step one off its end, plus the
; descriptor array gfx_lstepv takes. Both are rebuilt from scratch before each
; row, untimed, because the rows consume the walks.
;
; Note what is NOT passed: these are X slots, so the stub puts the caller's DS
; in ES itself and a package hands over a bare offset in its own segment.
gb_lsinit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [gb_lsi], 0
    mov di, gb_lsblk
.next:
    mov ax, [gb_lsi]                ; walk i runs from (x + 8i, y) to
    mov cl, 3                       ; (x + 8i + 4, y + 126): steep, and clear
    shl ax, cl                      ; of its neighbours
    add ax, [gb_x]
    mov cx, ax
    add cx, 4
    mov bx, [gb_y]
    mov dx, bx
    add dx, 126
    call OSAPI_GFX_LINIT
    add di, GLS_SZ
    inc word [gb_lsi]
    cmp word [gb_lsi], GB_NWALK
    jb .next
    mov di, gb_lsdsc                ; (block, count) pairs, one pixel each
    mov ax, gb_lsblk
    mov cx, GB_NWALK
.d:
    mov [di], ax
    mov word [di+2], 1
    add ax, GLS_SZ
    add di, 4
    loop .d
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

gb_b_lstep8:                        ; eight arrivals for eight pixels
    mov di, gb_lsblk
    mov si, GB_NWALK
.next:
    mov cx, 1
    call OSAPI_GFX_LSTEP
    add di, GLS_SZ
    dec si
    jnz .next
    ret

gb_b_lstepv8:                       ; one arrival for the same eight
    mov di, gb_lsdsc
    mov cx, GB_NWALK
    call OSAPI_GFX_LSTEPV
    ret

; The two line geometries, transposed so the pixel counts match: 32 across by
; 127 down, and 127 across by 32 down. [gb_lfat] is SPEC.md 5.6.5's dilation,
; 0 for a draw and 1 for an erase-what-was-drawn-in-pieces.
gb_b_lsteep:
    mov ax, [gb_x]
    mov cx, ax
    add cx, 32
    mov bx, [gb_y]
    mov dx, bx
    add dx, 127
    mov si, [gb_lfat]
    call OSAPI_GFX_LINE
    ret

; --- SPEC.md 79.5.6's two candidates -------------------------------------------
; gb_b_mline rasterises ONE line into a private 1bpp mask, with no clipping, no
; ink, no dither table and no arrival - everything gfx_line does that a
; caller compositing its own figure does not need. Against `GFX_LINE shallow
; thin`, which draws the identical 127 x 32 line, the difference IS what the
; kernel's generality costs.
;
; gb_b_xdiff is the commit: the whole box walked a word at a time, XORing this
; frame's mask against last frame's, and writing only where they differ. It
; writes into gb_ram rather than the framebuffer, and that substitution is
; sound rather than convenient - Set 1 measured the framebuffer at 1.09x RAM
; for a read-modify-write, and only about one word in seven here is written at
; all. It does NOT clear as it goes, because bl_run runs it many times and a
; self-clearing loop would measure an empty buffer from the second iteration
; on; a real caller pays a `rep stosw` of GB_MSZ bytes on top, which Set 1
; prices at 1.76 us a byte.
gb_b_mline:
    push bp
    xor di, di                  ; DI = the byte, BL = the bit, at (0,0)
    mov bl, 80h
    mov si, GB_MDX + 1
    mov bp, GB_MDY * 2 - GB_MDX
.px:
    or [gb_maska + di], bl
    shr bl, 1                   ; ...one column on
    jnz .nx
    mov bl, 80h
    inc di
.nx:
    add bp, GB_MDY * 2          ; ...and Bresenham's minor axis
    jle .no
    sub bp, GB_MDX * 2
    add di, GB_MST
.no:
    dec si
    jnz .px
    pop bp
    ret

gb_b_xdiff:
    mov si, gb_maska
    mov di, gb_maskb
    mov bx, gb_ram
    mov cx, GB_MSZ / 2
.w:
    mov ax, [si]
    xor ax, [di]
    jz .skip                    ; ...and most words ARE the same
    xor [bx], ax
.skip:
    add si, 2
    add di, 2
    add bx, 2
    dec cx
    jnz .w
    ret

; gb_maskfill - GB_MNL lines into each mask, the second lot shifted one row, so
; the diff row above runs over a real density rather than an empty box. It is
; if anything DENSER than a cube: twelve 128-pixel lines put ink in about half
; the words, where a cube's twelve short edges reach nearer a sixth - so the
; xordiff figure is a pessimistic bound on that shape and not a typical one.
gb_maskfill:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov cx, GB_MNL
    xor dx, dx                  ; DX = the row this edge starts on
.one:
    push cx
    mov di, dx                  ; ...into mask A at row DX
    mov bl, 80h
    mov si, GB_MDX + 1
    mov bp, GB_MDY * 2 - GB_MDX
.pa:
    or [gb_maska + di], bl
    or [gb_maskb + di + GB_MST], bl     ; B is the same figure, one row down
    shr bl, 1
    jnz .na
    mov bl, 80h
    inc di
.na:
    add bp, GB_MDY * 2
    jle .noa
    sub bp, GB_MDX * 2
    add di, GB_MST
.noa:
    dec si
    jnz .pa
    add dx, GB_MST * 8          ; the next edge, eight rows down
    pop cx
    dec cx
    jnz .one
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ...and the third candidate, which needs no new kernel call at all:
; OSAPI_GFX_BLIT1 (SPEC.md 5.4.2) already puts a 1bpp band down in one arrival,
; byte-aligned, in final screen polarity. For a figure on a plain ground that
; IS the commit - the band replaces the box, so the old figure goes and the new
; one arrives in the same pass and no pixel is ever read. gb_b_mclr is the
; other half of that frame: the mask has to be wiped before it is drawn into.
gb_b_blit1:
    push bp
    push es
    push ds
    pop es
    mov si, gb_maska
    mov bp, GB_MST
    mov ax, [gb_x]
    and ax, 0FFF8h              ; BLIT1 refuses an x that is not a multiple of 8
    mov bx, [gb_y]
    mov cx, GB_MW
    mov dx, GB_MH
    call OSAPI_GFX_BLIT1
    pop es
    pop bp
    ret

gb_b_mclr:
    push es
    push ds
    pop es
    mov di, gb_maska
    mov cx, GB_MSZ / 2
    xor ax, ax
    cld
    rep stosw
    pop es
    ret

gb_b_lshal:
    mov ax, [gb_x]
    mov cx, ax
    add cx, 127
    mov bx, [gb_y]
    mov dx, bx
    add dx, 32
    mov si, [gb_lfat]
    call OSAPI_GFX_LINE
    ret

gb_b_clipfill:
    mov bx, [gb_win]
    call OSAPI_WM_CLIP_SET
    jc gb_b_fill                    ; refused (over 16 fragments): draw plain,
    call gb_b_fill                  ; so the row is never simply missing
    call OSAPI_WM_CLIP_CLEAR
    ret

gb_b_fill:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, [gb_x2]
    mov dx, [gb_y2]
    call OSAPI_GFX_FILL
    ret

gb_b_frame:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, [gb_x2]
    mov dx, [gb_y2]
    call OSAPI_GFX_FRAME
    ret

gb_b_gray:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, [gb_x2]
    mov dx, [gb_y2]
    call OSAPI_GFX_FILL_GRAY
    ret

gb_b_pat:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, [gb_x2]
    mov dx, [gb_y2]
    mov si, gb_pattern
    call OSAPI_GFX_FILL_PAT
    ret

gb_b_xfill:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, [gb_x2]
    mov dx, [gb_y2]
    call OSAPI_GFX_XOR_FILL
    ret

gb_b_xrect:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, [gb_x2]
    mov dx, [gb_y2]
    call OSAPI_GFX_XOR_RECT
    ret

gb_b_blit:
    push bp
    push es
    push ds
    pop es                          ; ES is OURS here: no stub is involved
    mov si, [gb_src]
    mov bp, GB_BLITS
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, GB_BLITW
    mov dx, GB_BLITH
    call OSAPI_GFX_BLIT4
    pop es
    pop bp
    ret

gb_b_scroll:
    mov ax, [gb_x]                  ; x1 and x2+1 are multiples of 8 by
    mov bx, [gb_y]                  ; construction - the blit is byte-column
    mov cx, [gb_x2]                 ; granular on every adapter
    mov dx, [gb_y2]
    mov si, 8
    call OSAPI_GFX_SCROLL
    ret

gb_b_fchar:
    mov cx, [gb_tx]
    mov dx, [gb_y]
    mov al, 'W'
    call OSAPI_FONT_CHAR
    ret

gb_b_fstr:
    mov cx, [gb_tx]
    mov dx, [gb_y]
    mov si, gb_s_test
    call OSAPI_FONT_STR
    ret

; The erase-and-letter PAIR: what every text element in this system wrote by
; hand before SPEC.md 6.1, and what the RUN row below it replaced.
gb_b_pair:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [gb_tx]
    mov bx, [gb_y]
    mov cx, ax
    add cx, 79
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov cx, [gb_tx]
    mov dx, [gb_y]
    mov si, gb_s_test
    call OSAPI_FONT_STR
    ret

gb_b_frun:
    mov cx, [gb_tx]
    mov dx, [gb_y]
    mov si, gb_s_test
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

gb_b_frun20:
    mov cx, [gb_tx]
    mov dx, [gb_y]
    mov si, gb_s_t20
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

gb_b_frunp:
    mov cx, [gb_tx]
    mov dx, [gb_y]
    mov si, gb_s_pad
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

gb_b_fwidth:
    mov si, gb_s_test
    call OSAPI_FONT_WIDTH
    ret

gb_b_ticks:
    call OSAPI_GET_TICKS
    ret

gb_b_color:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ret

gb_b_mouse:
    call OSAPI_MOUSE
    ret

gb_b_cont:
    mov bx, [gb_win]
    call OSAPI_WM_CONTENT
    ret

gb_b_geom:
    mov bx, [gb_win]
    call OSAPI_WM_GEOM
    ret

gb_b_obsc:
    mov bx, [gb_win]
    call OSAPI_WM_OBSCURED
    ret

gb_b_cliptest:
    mov ax, [gb_x]
    mov bx, [gb_y]
    mov cx, [gb_x2]
    mov dx, [gb_y2]
    call OSAPI_WM_CLIP_TEST
    ret

gb_b_clip:
    mov bx, [gb_win]
    call OSAPI_WM_CLIP_SET
    call OSAPI_WM_CLIP_CLEAR
    ret

; gb_b_lockpair - the drawing mutex, entered and left. UNLOCK first because
; this runs in a callback that already holds it, and the pair therefore leaves
; the lock exactly as it found it (see the header).
;
; The lock is free for the handful of instructions between the two calls, and
; bl_time's cli window covers them - so no other task can take it, and the
; mouse ISR, which draws the cursor itself whenever the lock is free (SPEC.md
; 7), cannot run either. The trailing `cli` is not decoration: gfx_lock returns
; with IF = 1 by contract and bl_pit's two-byte latch read requires IF = 0.
gb_b_lockpair:
    call OSAPI_GFX_UNLOCK
    call OSAPI_GFX_LOCK
    cli
    ret

gb_b_title:
    mov bx, [gb_win]
    xor ax, ax                      ; 0 = "the bytes W_TITLE names changed"
    call OSAPI_WM_TITLE
    ret

gb_b_row:
    mov si, gb_s_full               ; one full-width opaque row: the unit the
    call bl_pad                     ; page below is built out of - and it must
                                    ; be full of GLYPHS. Padded with spaces it
                                    ; measured 186 us a cell against the 915 a
                                    ; real page costs, because a blank cell on
                                    ; font_run's fast path is ~5x cheaper than
                                    ; a lettered one; the page prediction came
                                    ; out 5x low and the check beside it fired
                                    ; (PERFORMANCE.md Part 9)
    mov cx, [gb_cx]
    mov dx, [gb_y]
    mov si, bl_draw
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    ret

gb_b_page:
    mov si, [gb_win]
    call bl_paint
    ret

; --- the raw bandwidth bodies ------------------------------------------------
; GB_BWROWS rows of GB_BWCOLS bytes, walked through a table of row offsets, so
; the framebuffer's banked interleave and plain RAM go through the identical
; loop and the difference between the two rows is the bus and nothing else.

gb_b_stosw:
    push es
    mov es, [gb_seg]
    mov bx, [gb_tab]
    mov si, GB_BWROWS
    xor ax, ax
    cld
.r:
    mov di, [bx]
    mov cx, GB_BWCOLS / 2
    rep stosw
    add bx, 2
    dec si
    jnz .r
    pop es
    ret

gb_b_stosb:
    push es
    mov es, [gb_seg]
    mov bx, [gb_tab]
    mov si, GB_BWROWS
    xor al, al
    cld
.r:
    mov di, [bx]
    mov cx, GB_BWCOLS
    rep stosb
    add bx, 2
    dec si
    jnz .r
    pop es
    ret

; Read through ES rather than DS so that neither side has to juggle DS - the
; segment override is one byte and two clocks on both sides, so the RAM/VRAM
; comparison is unaffected.
gb_b_read:
    push es
    mov es, [gb_seg]
    mov bx, [gb_tab]
    mov si, GB_BWROWS
.r:
    mov di, [bx]
    mov cx, GB_BWCOLS / 2
.w:
    mov ax, [es:di]
    inc di
    inc di
    loop .w
    add bx, 2
    dec si
    jnz .r
    pop es
    ret

gb_b_rmw:
    push es
    mov es, [gb_seg]
    mov bx, [gb_tab]
    mov si, GB_BWROWS
.r:
    mov di, [bx]
    mov cx, GB_BWCOLS
.b:
    mov al, [es:di]                 ; the read-modify-write SPEC.md 39.5 prices
    or al, 0                        ; at ~30 clocks whether or not it changes a
    mov [es:di], al                 ; pixel - the mono renderer's inner step
    inc di
    loop .b
    add bx, 2
    dec si
    jnz .r
    pop es
    ret

gb_b_inport:
    mov dx, [gb_stat]
    in al, dx
    ret

; gb_b_vsync - one whole vertical retrace period, bounded
; The bit is high during retrace, so this waits for it to fall and then to rise
; again: one full frame, which is the refresh rate. A dead or absent status
; port exhausts GB_VSMAX and returns - a wrong number, never a hung machine.
gb_b_vsync:
    mov dx, [gb_stat]
    mov ah, [gb_vsbit]
    mov cx, GB_VSMAX
.low:
    in al, dx
    test al, ah
    jz .high
    loop .low
    ret
.high:
    mov cx, GB_VSMAX
.h:
    in al, dx
    test al, ah
    jnz .done
    loop .h
.done:
    ret

%include "benchlib.inc"

; =============================================================================
; data
; =============================================================================

; A window as big as the adapter allows: wm_fit clamps the template onto the
; live screen (SPEC.md 39.7), so ONE template gives 52 text rows on VGA, 35 on
; Hercules and 17 on CGA without this source knowing which it is on. The x is
; 7 because WF_SNAP wants the CONTENT origin - W_X + 1 - on a multiple of 8.
gb_tpl:
    dw 7, 22, 632, 448
    dw gb_ttl, gb_paint, gb_onkey, gb_onclick

gb_ttl:     db 'Gfx Bench', 0

; The measured string is tests/fontbench's, character for character, so its
; published figures (SPEC.md 6.1.1) cross-check this harness for free.
gb_s_test:  db 'C-2 01 A0F', 0

gb_pattern: db 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55

; A row shaped like the report's own: label, columns, digits, no runs of
; blanks. 78 characters, so bl_pad never has to pad it on any adapter.
gb_s_full:  db 'GFX_FILL_GRAY 64x64        24    356320    12442.99 us  a report row', 0

; seg, stride, bmask, bshift - vid_tab's four numbers, in VID_* order
gb_vtab:
    dw 0xA000, 80, 0, 0
    dw 0xB000, 90, 3, 2
    dw 0xB800, 80, 1, 1

; status port and its retrace bit: 3DAh bit 3 on VGA and CGA, 3BAh bit 7 on
; Hercules
gb_ptab:
    dw 0x03DA, 0x0008
    dw 0x03BA, 0x0080
    dw 0x03DA, 0x0008

gb_f_vga:   db 'GFXVGA.TXT', 0
gb_f_herc:  db 'GFXHERC.TXT', 0
gb_f_cga:   db 'GFXCGA.TXT', 0

gb_n_vga:   db 'VGA 640x480 planar', 0
gb_n_herc:  db 'HERCULES 720x348 mono', 0
gb_n_cga:   db 'CGA 640x200 mono', 0

gb_h_1:     db 'Every gfx_* and font_* slot the OS publishes, priced on the adapter', 0
gb_h_2:     db 'this machine actually has, plus the RAM and framebuffer bandwidth under', 0
gb_h_3:     db '   R  or the Bench menu   run it.  About 10 seconds on a 4.77MHz 8088,', 0
gb_h_4:     db '                          and the machine is FROZEN while it runs - the', 0
gb_h_5:     db '                          bottom line says which block it is on, and', 0
gb_h_6:     db '                          it SAVES ITSELF when it finishes.  S or the', 0
gb_h_6b:    db '                          Bench menu writes it again, after a disk swap.', 0
gb_h_7:     db '   Space PgDn PgUp Up Dn Home End   page through it afterwards.', 0

gb_s_h_fs:  db '-- fullscreen (SPEC.md 11.2): the same rows, no window around them --', 0
gb_s_h_fs2: db '   (the primitives SHOULD match their twins above; the rest should not)', 0
gb_s_fsno:  db 'FULLSCREEN refused - another window owns the screen. Rows skipped.', 0

gb_p_head:  db 'running: reading the machine...', 0
gb_p_bw:    db 'running: raw RAM and framebuffer bandwidth (1 of 6)', 0
gb_p_prim:  db 'running: drawing primitives (2 of 6)', 0
gb_p_text:  db 'running: text (3 of 6)', 0
gb_p_api:   db 'running: the API cells (4 of 6)', 0
gb_p_comp:  db 'running: composite window work - the slow one (5 of 6)', 0
gb_p_fs:    db 'running: fullscreen, and the screen repaint (6 of 6)', 0

gb_s_ttl1:  db 'os8088 GFXBENCH - drawing primitives priced on this adapter', 0
gb_s_ttl2:  db '===========================================================', 0

gb_l_adapter: db 'adapter', 0
gb_l_screen:  db 'screen width px', 0
gb_l_rows:    db 'screen height px', 0
; ...the desktop's UNION. These seven are the DISPLAY the sandbox is on, and
; they are printed only when there is more than one to choose between.
gb_l_ndisp:   db 'displays', 0
gb_l_dix:     db 'sandbox display', 0
gb_l_dorg:    db 'display origin x', 0
gb_l_dorgy:   db 'display origin y', 0
gb_l_dw:      db 'display width px', 0
gb_l_dh:      db 'display height px', 0
gb_l_strad:   db 'sandbox straddles', 0
gb_l_bpp:     db 'bits per pixel', 0
gb_l_dock:    db 'first dock row', 0
gb_l_fbseg:   db 'framebuffer seg', 0
gb_l_stride:  db 'bytes per row', 0
gb_l_banks:   db 'framebuffer banks', 0
gb_l_stat:    db 'status port', 0
gb_l_conw:    db 'content width px', 0
gb_l_conh:    db 'content height px', 0
gb_l_cells:   db 'content cells wide', 0
gb_l_crows:   db 'content cells tall', 0
gb_l_boxx:    db 'sandbox x (aligned)', 0
gb_l_cpu:     db 'cpu tier 0/1/2', 0
gb_l_feat:    db 'cpu feature bits', 0
gb_l_kern:    db 'kernel span KB', 0
gb_l_heap:    db 'claim heap KB', 0
gb_l_mlarge:  db 'largest free run KB', 0
gb_l_mtotal:  db 'total free KB', 0
gb_l_boott: db 'boot ticks', 0
gb_l_snd:   db 'sound caps word', 0
gb_l_vol:   db 'current volume', 0
gb_n_nostamp: db '  not stamped', 0
gb_volch:   db 'A', 0
gb_l_xms:     db 'pool above 1MB KB', 0
gb_l_ovh:     db 'loop overhead counts', 0
gb_l_ovh1:    db 'loop overhead usx100', 0

gb_s_pit1:  db 'One PIT count is 838 ns and four 4.77MHz CPU clocks. us/op is PER', 0
gb_s_pit2:  db 'ITERATION, net of the loop overhead. t = tick-timed, ! = near the wrap,', 0
gb_s_pit3:  db 'w = an iteration LAPPED the counter, so that row is tick-timed and coarse.', 0
gb_s_warn1: db 'CAUTION: under QEMU the us column is the HOST speed and means nothing.', 0
gb_s_warn2: db 'Boot -icount shift=3,sleep=off and read counts as guest INSTRUCTIONS.', 0
gb_s_warn3: db 'The VRAM rows assume the real framebuffer address, so a kernel built', 0
gb_s_warn4: db 'with HERCSEG= measures plain RAM in them and the bus ratio reads 100.', 0
gb_s_warn5: db 'The retrace row is meaningless on QEMU too - its status port toggles', 0
gb_s_warn6: db 'on every read so a poll always terminates. On iron it is the refresh.', 0

gb_s_h_bw:    db '-- raw bandwidth: 32 rows x 64 bytes = 2048 bytes an iteration --', 0
gb_s_h_prim:  db '-- primitives (two sizes wherever the cost has two terms) --', 0
gb_s_h_text:  db '-- text: the same 10 characters, aligned and skewed 5 px --', 0
gb_s_t20:  db 'C-2 01 A0FC-2 01 A0F', 0   ; 20 cells, no adjacent repeat
gb_s_pad:  db 'PAINT               ', 0   ; 5 + 15, a padded field (27.2/12.9)
gb_s_h_text2: db '   (tests/fontbench uses this string too, so the two harnesses check)', 0
gb_s_h_api:   db '-- API cells that draw nothing: the far-call floor --', 0
gb_s_h_comp:  db '-- composite: what a window operation costs --', 0
gb_s_h_der:   db '-- derived: subtractions of the rows above, printed to be checked --', 0

gb_r_rw:   db 'RAM  write word', 0
gb_r_rb:   db 'RAM  write byte', 0
gb_r_rr:   db 'RAM  read word', 0
gb_r_rm:   db 'RAM  read-mod-write', 0
gb_r_vw:   db 'VRAM write word', 0
gb_r_vb:   db 'VRAM write byte', 0
gb_r_vr:   db 'VRAM read word', 0
gb_r_vm:   db 'VRAM read-mod-write', 0
gb_r_in:   db 'ISA status port in', 0
gb_r_vs:   db 'one retrace period', 0

gb_r_px:   db 'GFX_PIXEL', 0
gb_r_hl8:  db 'GFX_HLINE 8px', 0
gb_r_hlw:  db 'GFX_HLINE 256px', 0
gb_r_vl8:  db 'GFX_VLINE 8px', 0
gb_r_vlh:  db 'GFX_VLINE 128px', 0
gb_r_f8:   db 'GFX_FILL 8x8', 0
gb_r_f64:  db 'GFX_FILL 64x64', 0
gb_r_fbox: db 'GFX_FILL 256x128', 0
gb_r_f64c: db 'GFX_FILL 64x64 clipped', 0
gb_r_lst:  db 'GFX_LINE steep thin', 0
gb_r_lstf: db 'GFX_LINE steep fat', 0
gb_r_lsh:  db 'GFX_LINE shallow thin', 0
gb_r_lshf: db 'GFX_LINE shallow fat', 0
gb_r_mline:db 'mask line 127x32', 0
gb_r_xdiff:db 'xordiff 128x128', 0
gb_r_blit1:db 'GFX_BLIT1 128x128', 0
gb_r_mclr: db 'clear mask 2048', 0
gb_r_ls8:  db 'GFX_LSTEP x8 (8 calls)', 0
gb_r_lsv8: db 'GFX_LSTEPV x8 (1 call)', 0
gb_r_frow: db 'GFX_FILL 256x1', 0
gb_r_fr:   db 'GFX_FRAME 64x64', 0
gb_r_gy:   db 'GFX_FILL_GRAY 64x64', 0
gb_r_pt:   db 'GFX_FILL_PAT 64x64', 0
gb_r_xf:   db 'GFX_XOR_FILL 64x64', 0
gb_r_xr:   db 'GFX_XOR_RECT 64x64', 0
gb_r_xrb:  db 'GFX_XOR_RECT 256x128', 0
gb_r_xrr:  db 'GFX_XOR_RECT 256x1', 0
gb_r_bs:   db 'GFX_BLIT4 solid', 0
gb_r_bn:   db 'GFX_BLIT4 4px runs', 0
gb_r_sc:   db 'GFX_SCROLL 256x128', 0

gb_r_ch:   db 'FONT_CHAR one cell', 0
gb_r_st:   db 'FONT_STR 10 aligned', 0
gb_r_pa:   db 'PAIR 10 aligned', 0
gb_r_ru:   db 'FONT_RUN 10 aligned', 0
gb_r_ru20: db 'FONT_RUN 20 text', 0
gb_r_rup:  db 'FONT_RUN 20 padded', 0
gb_r_pa5:  db 'PAIR 10 skewed 5', 0
gb_r_ru5:  db 'FONT_RUN 10 skewed', 0
gb_r_fw:   db 'FONT_WIDTH 10', 0

gb_r_gt:   db 'GET_TICKS', 0
gb_r_sc2:  db 'SET_COLOR', 0
gb_r_wc:   db 'WM_CONTENT', 0
gb_r_wg:   db 'WM_GEOM', 0
gb_r_wo:   db 'WM_OBSCURED', 0
gb_r_ct:   db 'WM_CLIP_TEST', 0
gb_r_cs:   db 'WM_CLIP_SET+CLEAR', 0
gb_r_mo:   db 'MOUSE', 0

gb_r_lock:    db 'GFX_UNLOCK+LOCK pair', 0
gb_r_ti:      db 'WM_TITLE strip', 0
gb_r_rowdraw: db 'one full-width row', 0
gb_r_page:    db 'whole page of rows', 0
gb_r_fspair:  db 'FULLSCREEN in+out', 0

gb_d_fillpx:   db 'fill ns/px 8-64', 0
gb_d_fillrow:  db 'fill ns per row', 0
gb_d_lsteep:   db 'line steep fat/thin', 0
gb_d_lshal:    db 'line shal fat/thin ~300', 0
gb_d_lsv:      db 'LSTEP8/LSTEPV8 icnt118', 0
gb_d_lsarr:    db 'lstep arrival us x100', 0
gb_d_lspx:     db 'lstep pixel us x100', 0
gb_d_fillpx2:  db 'fill ns/px 64-box', 0
gb_d_hlpx:     db 'hline ns per pixel', 0
gb_d_cell:     db 'FONT_CHAR us x100', 0
gb_d_runcell:  db 'RUN cell us x100', 0
gb_d_runwin:   db 'skewPAIR/RUN x100', 0
gb_d_busw:     db 'VRAM/RAM word x100', 0
gb_d_busm:     db 'VRAM/RAM rmw x100', 0
gb_d_rmwclk:   db 'VRAM rmw clocks x100', 0
gb_d_blit:     db 'blit runs/solid x100', 0
gb_d_lockus:   db 'lock pair us x100', 0
gb_d_lockfill: db 'lock pair/FILL8 x100', 0
gb_d_pagepred: db 'page predicted usx100', 0
gb_d_pagereal: db 'page measured usx100', 0
gb_d_scrfill:  db 'screen fill us x100', 0

gb_s_end1:  db 'End of report. R re-runs it, S saves it, Bench menu does both.', 0
gb_s_end2:  db 'A saved file lands in the CURRENT directory of the CURRENT volume', 0
gb_s_end3:  db '(SPEC.md 19.2): open a Disk window on the disk you want it on first.', 0

gb_about:   db 'Gfx Bench', 0

    OS88_MENUSET gb_menus, gb_about, gb_oncmd
        OS88_MENU gb_m_bench, gb_i_bench, 3
    OS88_MENUSET_END gb_menus

gb_m_bench: db 'Bench', 0
gb_i_bench: dw gb_it_run, gb_it_save, gb_it_top
gb_it_run:  db 'Run', 0
gb_it_save: db 'Save Report', 0
gb_it_top:  db 'Top of Report', 0

; The bss offsets past the scalars are DERIVED, not written down twice: the
; equ block after OS88_IMAGE_END uses these, and GB_BSS_OWN falls out of them.
; A hand-totalled figure that is too small is a package writing over
; benchlib's arena, which assembles cleanly and produces a report full of
; plausible nonsense.
; vid_ctx (SPEC.md 57.4's 'VD'): an 18-word run with vid_cw/vid_ch inside it,
; then the display's origin in the virtual desktop and its kind. Mirrored here
; and in tests/sysbench for the same reason - a test package reads kernel state
; through the registry and shipped software never does (SPEC.md 57).
VCTX_SEG    equ 0               ; vid_seg:    the framebuffer
VCTX_STRIDE equ 2               ; vid_stride: bytes from a row to the row one
                                ;             BANK down (SPEC.md 39.3)
VCTX_BMASK  equ 4               ; vid_bmask:  y & this = the bank
VCTX_BSHIFT equ 6               ; vid_bshift: y >> this = the row in that bank
VCTX_CW     equ 14              ; vid_cw / vid_ch: THIS DISPLAY's extent, not
VCTX_CH     equ 16              ; the desktop's (SPEC.md 39.2.1)
VCTX_VX     equ 36              ; ...and its origin in the virtual desktop
VCTX_VY     equ 38
VCTX_KIND   equ 40              ; ...and which adapter it is

GB_NWALK    equ 8               ; walks stepped together (SPEC.md 5.6.8)
GB_O_SCAL   equ 192             ; ...where the scalars below end
GB_O_SYSKB  equ GB_O_SCAL + GB_NWALK * (4 + GLS_SZ)   ; the scalars above end at
                                ; gb_lsblk, which is DERIVED - a hand-totalled
                                ; figure that is too small is a package writing
                                ; over benchlib's arena, and it assembles
GB_O_VROW   equ GB_O_SYSKB + SYSKB_SIZE
GB_O_RROW   equ GB_O_VROW + GB_BWROWS * 2
GB_O_RAM    equ GB_O_RROW + GB_BWROWS * 2
GB_O_SOLID  equ GB_O_RAM + GB_BWBYTES
GB_O_STRIPE equ GB_O_SOLID + GB_BLITSZ
; --- SPEC.md 79.5.6's candidate: a figure rasterised into a PRIVATE 1bpp mask,
; and the frame committed as the XOR of this mask against last frame's. The
; question these two rows answer is whether a wireframe can be moved by
; writing WORDS of difference instead of pixels of erase-and-draw.
GB_MW       equ 128             ; the mask, which is a cube's bounding box
GB_MH       equ 128
GB_MST      equ GB_MW / 8       ; 16 bytes a row
GB_MSZ      equ GB_MST * GB_MH  ; 2,048
GB_MDX      equ 127             ; ...and the same 127 x 32 line the GFX_LINE
GB_MDY      equ 32              ; rows draw, so the two are comparable
GB_MNL      equ 12              ; edges of a cube, for the diff row's density
GB_O_MASKA  equ GB_O_STRIPE + GB_BLITSZ
GB_O_MASKB  equ GB_O_MASKA + GB_MSZ
GB_BSS_OWN  equ ((GB_O_MASKB + GB_MSZ + 511) / 512) * 512   ; benchlib's base must be
                                        ; 512-ALIGNED: bl_out is an int 13h target

    align 512                   ; ...and os88_image_end likewise, which this
                                ; costs up to 511 bytes of image and buys the
                                ; alignment of every bss offset below
    OS88_BSS GB_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
gb_win      equ os88_image_end + 0     ; word: our window
gb_vw       equ os88_image_end + 2     ; word: OSAPI_VIDEO's answers
gb_vh       equ os88_image_end + 4
gb_dock     equ os88_image_end + 6
gb_kind     equ os88_image_end + 8     ; byte
gb_bpp      equ os88_image_end + 9     ; byte
gb_cpu      equ os88_image_end + 10    ; word: AL tier, AH feature bits
gb_mlarge   equ os88_image_end + 12    ; word
gb_mtotal   equ os88_image_end + 14    ; word
gb_cx       equ os88_image_end + 16    ; word: content origin and size
gb_cy       equ os88_image_end + 18
gb_cw       equ os88_image_end + 20
gb_ch       equ os88_image_end + 22
gb_x        equ os88_image_end + 24    ; word: the sandbox rect
gb_y        equ os88_image_end + 26
gb_x2       equ os88_image_end + 28
gb_y2       equ os88_image_end + 30
gb_tx       equ os88_image_end + 32    ; word: the text x, aligned or skewed
gb_fbseg    equ os88_image_end + 34    ; word: the adapter record
gb_stride   equ os88_image_end + 36
gb_bmask    equ os88_image_end + 38
gb_bshift   equ os88_image_end + 40
gb_xbyte    equ os88_image_end + 42    ; word: the sandbox's byte column
gb_stat     equ os88_image_end + 44    ; word: the status port
gb_vsbit    equ os88_image_end + 46    ; word: ...and its retrace bit
gb_seg      equ os88_image_end + 48    ; word: the bandwidth target segment
gb_tab      equ os88_image_end + 50    ; word: ...and its row table
gb_src      equ os88_image_end + 52    ; word: the blit source in use
gb_bcnt     equ os88_image_end + 54    ; dword: the baseline row's raw counts
gb_ta       equ os88_image_end + 58    ; dword: gb_stash/gb_sub's scratch
gb_tram_w   equ os88_image_end + 62    ; dword: the rows the derived block
gb_tram_m   equ os88_image_end + 66    ; subtracts and divides
gb_tvid_w   equ os88_image_end + 70
gb_tvid_m   equ os88_image_end + 74
gb_tf8      equ os88_image_end + 78
gb_tf64     equ os88_image_end + 82
gb_thl8     equ os88_image_end + 86
gb_thlw     equ os88_image_end + 90
gb_tcell    equ os88_image_end + 94
gb_trun     equ os88_image_end + 98
gb_tpair5   equ os88_image_end + 102
gb_tbs      equ os88_image_end + 106
gb_tbn      equ os88_image_end + 110
gb_trow     equ os88_image_end + 114
gb_tpage    equ os88_image_end + 118
gb_tfbox    equ os88_image_end + 130   ; dword: the 256x128 fill, for slope 2
gb_tfrow    equ os88_image_end + 136   ; dword: ...and the 256x1 one, for the
                                       ; per-row term it pins against
gb_lfat     equ os88_image_end + 140   ; word: SPEC.md 5.6.5's dilation flag
gb_tlst     equ os88_image_end + 142   ; dword: the four gfx_line rows, whose
gb_tlstf    equ os88_image_end + 146   ; two RATIOS are the finding rather
gb_tlsh     equ os88_image_end + 150   ; than any one of them (SPEC.md 5.6.6)
gb_tlshf    equ os88_image_end + 154
gb_tapi     equ os88_image_end + 122   ; dword: 122..125
gb_ran      equ os88_image_end + 126   ; byte: has the suite been run yet?
gb_tlock    equ os88_image_end + 158   ; dword: the gfx lock pair (SPEC.md 7)
gb_tls8     equ os88_image_end + 162   ; dword: eight arrivals (SPEC.md 5.6.8)
gb_tlsv8    equ os88_image_end + 166   ; dword: ...and one, for the same pixels
gb_tlsa     equ os88_image_end + 174   ; dword: A - B, parked across bl_us100
gb_lsi      equ os88_image_end + 170   ; word:  gb_lsinit's walk index
gb_dok      equ os88_image_end + 178   ; byte: the 'VD' block answered, and
gb_dn       equ os88_image_end + 179   ; byte: ...with this many displays
gb_dix      equ os88_image_end + 180   ; byte: which one the sandbox is on,
                                       ;       0xFF = none (the dead zone)
gb_dkind    equ os88_image_end + 181   ; byte: ...and its adapter
gb_dstrad   equ os88_image_end + 182   ; byte: the content box leaves it
gb_dvx      equ os88_image_end + 184   ; word: that display's origin in the
gb_dvy      equ os88_image_end + 186   ;       virtual desktop, and its own
gb_dcw      equ os88_image_end + 188   ;       extent - what every number from
gb_dch      equ os88_image_end + 190   ;       'framebuffer seg' down describes
gb_lsdsc    equ os88_image_end + GB_O_SCAL   ; GB_NWALK (block, count) pairs
gb_lsblk    equ os88_image_end + GB_O_SCAL + GB_NWALK * 4  ; ...and walk states
gb_syskb    equ os88_image_end + GB_O_SYSKB    ; SYSKB_SIZE bytes
gb_vrow     equ os88_image_end + GB_O_VROW     ; GB_BWROWS words: fb offsets
gb_rrow     equ os88_image_end + GB_O_RROW     ; ...and the RAM ones
gb_ram      equ os88_image_end + GB_O_RAM      ; the RAM bandwidth target
gb_bsolid   equ os88_image_end + GB_O_SOLID    ; the two blit sources
gb_bstripe  equ os88_image_end + GB_O_STRIPE
gb_maska    equ os88_image_end + GB_O_MASKA   ; the two figure masks
gb_maskb    equ os88_image_end + GB_O_MASKB

    BL_BSS os88_image_end + GB_BSS_OWN
