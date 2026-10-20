; =============================================================================
; os8088 - drivers/wirezone.inc
;
; THE WIRE'S DESKTOP ICON, and every byte of it is the driver's (SPEC.md 26.7,
; docs/WIRE-PLAN.md §2). The kernel has a generic desktop SERVICE zone a
; driver registers through OSAPI_DESK_ITEM and knows nothing else: not the
; picture, not the caption, not the file it launches, not the word Wire. Most
; XTs have no network card, and a machine without one must not carry a glyph
; it can never draw - so the glyph lives here, in RAM only while a driver that
; can actually reach the Wire is loaded.
;
; %included by any driver that can reach os8088.com. Define WZ_CLASS to your
; own DRVC_* class before the %include and call the two entry points:
;
;   wz_register   from DRVV_READY, once your services are published
;   wz_withdraw   from DRVV_DETACH, before the kernel frees your image
;
; A SECOND DRIVER REGISTERING IS REFUSED AND THAT IS FINE: there is one zone,
; whoever attaches first gets it, and both drivers reach the same Wire. The
; refusal is CF=1 and nothing else, so neither has anything to undo.
;
; --- WHY FOUR 16x16 RECORDS AND NOT ONE 32x32 -------------------------------
; OSAPI_ICON_DRAW refuses every record but 16 px wide and at most 16 rows
; (SPEC.md 25.6.1: it stages the record into a fixed cell and the bound is a
; refusal rather than a truncation). The kernel's own 32-wide desktop icons go
; through icon_draw_ix, which is KERNEL-INTERNAL and reachable by no slot. So
; the picture is drawn as quadrants through the slot a driver actually has -
; four calls for the 32-row form, two for the 14-row one. Four far calls at
; ~47 us each is 188 us on a 4.77 MHz 8088, against the 756 us any one drawing
; call costs there: it is inside the noise of the draw it is part of, and it
; happens on a desktop repaint and nowhere else.
; =============================================================================

WZV_PAINT   equ 0x40            ; the verb the kernel calls to paint the icon.
                                ; WELL CLEAR OF NETV_MAX (17) on purpose: it
                                ; shares a dispatcher with the socket verbs
                                ; and must never collide with one

; --- the record the kernel copies at registration (SPEC.md 26.7) -------------
; Fixed offsets, so the four fields are laid out with `times` padding and each
; one is asserted where it ends. The kernel copies DSV_R_SIZE bytes with one
; `rep movsb` and terminates the caption itself; everything else here is
; trusted the way a driver's DSV table is trusted.
;
; **AND ONCE IT IS COPIED IT IS SCRATCH** (SPEC.md 26.7.1): the same bytes are
; wz_buf, the stage wz_tile unpacks each quadrant's record into. That is safe
; for three facts and each is a fact about the tree rather than a hope:
; osapi_desk_item_x COPIES the record and never reads the driver's again (the
; caption, the file and the name all live in desk_svc_rec from then on);
; wz_register is called ONCE, from DRVV_READY, and the kernel sends that once
; per attach, every attach being a fresh image read off the disk; and the
; kernel calls the paint verb only on the driver whose registration it TOOK,
; so a refused second driver never paints and keeps its record intact. A
; driver that ever wants to register AGAIN on the same image - taking the zone
; over when the other driver leaves, say - must give wz_buf storage of its own
; first, or it hands the kernel a picture's rows as a caption.
wz_rec:
wz_buf:
    db 'Wire', 0                ; +0  the caption under the icon
    times 12 - ($ - wz_rec) db 0
%if ($ - wz_rec) != 12
  %error "wirezone: the caption field is 12 bytes (11 characters and a NUL)"
%endif
    db 'THEWIRE.O88', 0         ; +12 the file it launches out of SYSTEM/
    times 25 - ($ - wz_rec) db 0
%if ($ - wz_rec) != 25
  %error "wirezone: the launch file field is 13 bytes (an 8.3 name and a NUL)"
%endif
    db WZ_CLASS                 ; +25 our class, so the kernel can call back
    db WZV_PAINT                ; +26 ...and the verb that paints
    db 'The Wire', 0            ; +27 THE PACKAGE'S HEADER NAME, and it must be
    times 39 - ($ - wz_rec) db 0    ; what apps/thewire's OS88_HEADER says:
%if ($ - wz_rec) != 39              ; it is what makes the zone a SINGLETON,
  %error "wirezone: the header-name field is 12 bytes (11 characters and a NUL)"
%endif                              ; matched against the instance table
    db 0xFF                     ; +39 THE CELL: 0xFF is the next free one
                                ; after the drives, which is where the Wire
                                ; always went (OSAPI_DESK_ITEM, SPEC.md 26.9).
                                ; 40 is the WHOLE record (DSV_R_SIZE)
%if ($ - wz_rec) != 40
  %error "wirezone: the record is 40 bytes - the kernel copies DSV_R_SIZE"
%endif
WZ_BUF_SZ   equ 2 + 16 * 4      ; one quadrant's record at its tallest: the
    times WZ_BUF_SZ - ($ - wz_buf) db 0     ; header, 16 mask words and 16
                                ; data words - exactly what icon_draw_x stages

; -----------------------------------------------------------------------------
; wz_register / wz_withdraw - the zone appears and goes away with the driver
; in:  nothing; DS = CS = our segment
; out: nothing (CF is the slot's and nobody here acts on it)
; -----------------------------------------------------------------------------
wz_register:
    push ax
    push si
    mov si, wz_rec              ; ES is the slot's X stub's business: it puts
    mov al, 1                   ; our own DS there, which is where the record
    call OSAPI_DESK_ITEM         ; is
    pop si
    pop ax
    ret

wz_withdraw:
    push ax
    xor al, al
    call OSAPI_DESK_ITEM
    pop ax
    ret

; -----------------------------------------------------------------------------
; wz_paint - THE VERB: draw the zone's icon (SPEC.md 26.7)
; in:  CX = the icon's left column, DX = its top row; the gfx lock is HELD
;      (this is called from inside desk_paint), DS = CS = our segment
; out: nothing; every register comes back as it came in
;
; The adapter decides the shape, exactly as the kernel's volume icons do
; (SPEC.md 26.4): the CGA's pixels are 2.4:1 tall, so a 32-row glyph renders
; there as a stretched column and the 14-row form is what is square. It is
; asked of OSAPI_VIDEO rather than derived from the height, because the
; question is the PIXEL ASPECT and that is a property of the adapter's mode.
;
; IT MUST NOT TOUCH THE CARD. The kernel calls this under the gfx lock from a
; desktop repaint, which can happen while another task is inside a socket
; verb, so the dispatcher answers it ABOVE the card claim - like NETV_IDENT
; and for the same reason.
; -----------------------------------------------------------------------------
wz_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push cx                     ; OSAPI_VIDEO answers in AX, BX, CX and DX,
    push dx                     ; so the origin is banked across it
    call OSAPI_VIDEO            ; DL = VID_*
    mov bl, dl
    pop dx
    pop cx
    push ds
    pop es                      ; wz_tile's stosw writes our own wz_buf - and
                                ; the X stub hands ES back as it found it
    mov si, wz_l14
    cmp bl, VID_CGA
    je .last                    ; the CGA: one band of 14 rows
    mov si, wz_l32t             ; ...the rest: two bands of 16
    push cx
    call wz_pair
    pop cx
    add dx, 16
    mov si, wz_l32b
.last:
    call wz_pair
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
; wz_pair - one band of the picture: its left quadrant at CX, its right at
;           CX + 16, both out of the same run list
; wz_tile - unpack ONE quadrant's record into wz_buf, then draw it
; in:  SI -> a run list, CX/DX = where; ES = DS; wz_tile also BP -> the
;      half's row-word table
; out: SI and DX kept, CX + 16 after wz_pair; AX, BX, DI, BP spent
;
; The record comes out byte for byte what the hand-written one was - the
; header, `rows` mask words, `rows` data words - so OSAPI_ICON_DRAW is handed
; exactly what it was handed before, and the pixels cannot differ. The list
; is walked TWICE, because the data rows follow the mask rows in the record:
; once for the masks and once for the data, DI running straight on from one
; into the other. DF = 0 is the system contract (SPEC.md 1, 20.2), which
; icon_draw_x's own copy rests on along this very path.
; -----------------------------------------------------------------------------
wz_pair:
    mov bp, wz_tl
    call wz_tile
    add cx, 16
    mov bp, wz_tr
wz_tile:
    push si
    push cx
    push dx
    mov di, wz_buf
    mov al, 1                   ; one word a row...
    stosb
    movsb                       ; ...and the list's own row count
    mov dh, WZT_WIND            ; THE MASK PASS: a wound row's silhouette is
.pass:                          ; the drum's, so WIND reads EDGE's word here
    push si
.run:
    lodsb                       ; count << 4 | last << 3 | type
    mov bl, al
    and bx, 7                   ; BX = the type; BH = 0
    cmp bl, dh
    jne .word
    dec bx                      ; WIND -> EDGE, the mask pass only: in the
.word:                          ; data pass DH has bit 7 set and no type does
    shl bx, 1
    add bx, bp                  ; ...a word of this half's table
    mov cl, 4
    shr al, cl                  ; AL = the run's rows, and CF = the bit shifted
    cbw                         ; out last: THE LAST-RUN FLAG. cbw, xchg, mov
    xchg ax, cx                 ; and rep stosw move no flag, so it reaches the
    mov ax, [bx]                ; `jnc` below untouched
    rep stosw
    jnc .run
    pop si                      ; ...and round again for the data rows
    xor dh, 0x80                ; 0x03 -> 0x83: the data pass, which no
    js .pass                    ; type matches; 0x83 -> 0x03: done
    pop dx
    pop cx
    mov si, wz_buf
    call OSAPI_ICON_DRAW        ; CX and DX come back; ES and BP too, the X
    pop si                      ; stub restores both
    ret

; =============================================================================
; THE PICTURE: a reel of cable, side on, with a cord and a two-prong plug
;
; Black outline on a white body, the diskette's own mask/data convention
; (SPEC.md 25): the mask is the filled silhouette, so the desktop's 50% dither
; never shows through, and the data is what is drawn black over it.
;
; SOLID BLACK MASSES, not a 1px outline, and that is the whole difference
; between this and the first draft of it. Against the desktop dither a thin
; outline over a white silhouette reads as a pale BLOCK; the flanges and the
; plug are what give it a shape at 32 px. The flanges stand two rows proud of
; the drum at each end, which is what makes it a reel rather than a framed
; grille - the second draft had them flush and it read as a window with bars.
;
;   ..######................######..   1.. 2  flange caps
;   ..############################..   3      the drum's top edge
;   ..######.#..#..#..#..#..######..   4..12  ...and the cable wound on it
;   ..############################..  13      the drum's bottom edge
;   ..######................######..  14..15  flange caps
;   ..............####..............  16..19  the cord
;   ...........##########...........  20      the plug's shoulder
;   ........################........  21..25  ...its body
;   ...........###....###...........  26..30  ...and its two prongs
;
; PACKED (SPEC.md 26.7.1): the picture has EIGHT distinct rows and both forms
; are made of nothing else, so each half is a table of eight words and each
; band is a list of runs over it. A row's mask is its data - every shape here
; is solid - except a WOUND row's, which is the drum's silhouette under the
; winding's holes, and wz_tile substitutes that one in the mask pass. Six
; hand-written records were 380 bytes; the tables and the lists are 56.
; =============================================================================
WZT_BLANK   equ 0               ; ................
WZT_FLANGE  equ 1               ; ..######........  ........######..
WZT_EDGE    equ 2               ; ..##############  ##############..
WZT_WIND    equ 3               ; ..######.#..#..#  ..#..#..######..
WZT_CORD    equ 4               ; ..............##  ##..............
WZT_SHOULD  equ 5               ; ...........#####  #####...........
WZT_BODY    equ 6               ; ........########  ########........
WZT_PRONG   equ 7               ; ...........###..  ..###...........
%if WZT_EDGE != WZT_WIND - 1
  %error "wirezone: wz_tile turns WIND into EDGE with one `dec`"
%endif

wz_tl:      dw 0x0000, 0x3F00, 0x3FFF, 0x3F49, 0x0003, 0x001F, 0x00FF, 0x001C
wz_tr:      dw 0x0000, 0x00FC, 0xFFFC, 0x24FC, 0xC000, 0xF800, 0xFF00, 0x3800

; WZ_RUNS rows, count, type, count, type, ... - one band's run list: the row
; count (the record's header byte), then a byte per run, count << 4 | type, with
; bit 3 set on the last - where wz_tile's `shr al, cl` drops it into CF. The counts are checked against the rows HERE, so
; a list that would draw a record short or long does not assemble.
%macro WZ_RUNS 1-*
    db %1
  %assign %%rows %1
  %assign %%sum 0
  %assign %%left (%0 - 1) / 2
  %rotate 1
  %rep %%left
    %assign %%left %%left - 1
    %if (%1) < 1 || (%1) > 15
      %error "wirezone: a run is 1..15 rows - the count is a nibble"
    %endif
    %assign %%sum %%sum + (%1)
    db ((%1) << 4) | ((%%left == 0) << 3) | (%2)
    %rotate 2
  %endrep
  %if %%sum != %%rows
    %error "wirezone: a run list's counts must add up to its row count"
  %endif
%endmacro

; --- 32 rows: VGA, EGA and Hercules, as two bands of 16 ----------------------
wz_l32t:    WZ_RUNS 16, 1,WZT_BLANK, 2,WZT_FLANGE, 1,WZT_EDGE, 9,WZT_WIND, \
                        1,WZT_EDGE, 2,WZT_FLANGE
wz_l32b:    WZ_RUNS 16, 4,WZT_CORD, 1,WZT_SHOULD, 5,WZT_BODY, 5,WZT_PRONG, \
                        1,WZT_BLANK

; --- 14 rows: the CGA (SPEC.md 26.4) ----------------------------------------
; Drawn, not squashed, for ico_disk14's reason: OR-ing row pairs thickens
; every 1px feature into two, and the winding, the cord and the prongs are all
; single features. The reel keeps two rows of winding and the plug two of
; body, which is the proportion the 32-row form has.
;
;   ..######................######..   1      flange
;   ..############################..   2      the drum
;   ..######.#..#..#..#..#..######..   3.. 4  the winding
;   ..############################..   5
;   ..######................######..   6      flange
;   ..............####..............   7.. 8  the cord
;   ...........##########...........   9      the shoulder
;   ........################........  10..11  the body
;   ...........###....###...........  12..13  the prongs
wz_l14:     WZ_RUNS 14, 1,WZT_BLANK, 1,WZT_FLANGE, 1,WZT_EDGE, 2,WZT_WIND, \
                        1,WZT_EDGE, 1,WZT_FLANGE, 2,WZT_CORD, 1,WZT_SHOULD, \
                        2,WZT_BODY, 2,WZT_PRONG
