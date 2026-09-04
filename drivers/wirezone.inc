; =============================================================================
; os8088 - drivers/wirezone.inc
;
; THE WIRE'S DESKTOP ICON, and every byte of it is the driver's (SPEC.md 26.7,
; docs/WIRE-PLAN.md §2). The kernel has a generic desktop SERVICE zone a
; driver registers through OSAPI_DESK_SVC and knows nothing else: not the
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
                                ; WELL CLEAR OF NETV_MAX (14) on purpose: it
                                ; shares a dispatcher with the socket verbs
                                ; and must never collide with one

; --- the record the kernel copies at registration (SPEC.md 26.7) -------------
; Fixed offsets, so the four fields are laid out with `times` padding and each
; one is asserted where it ends. The kernel copies DSV_R_SIZE bytes with one
; `rep movsb` and terminates the caption itself; everything else here is
; trusted the way a driver's DSV table is trusted.
wz_rec:
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
    db 'Cannot open Wire:', 0   ; +39 the failure notice's lead line. Ours,
    times 65 - ($ - wz_rec) db 0    ; because it names our program - a kernel
%if ($ - wz_rec) != 65              ; that composed it would carry both the
  %error "wirezone: the record is 65 bytes (SPEC.md 26.7's DSV_R_SIZE)"
%endif

wz_x:       dw 0                ; the icon's top-left, banked across
wz_y:       dw 0                ; OSAPI_VIDEO, which answers in CX and DX

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
    call OSAPI_DESK_SVC         ; is
    pop si
    pop ax
    ret

wz_withdraw:
    push ax
    xor al, al
    call OSAPI_DESK_SVC
    pop ax
    ret

; -----------------------------------------------------------------------------
; wz_paint - THE VERB: draw the zone's icon (SPEC.md 26.7)
; in:  CX = the icon's left column, DX = its top row; the gfx lock is HELD
;      (this is called from inside desk_paint), DS = CS = our segment
; out: nothing
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
    mov [wz_x], cx
    mov [wz_y], dx
    call OSAPI_VIDEO            ; DL = VID_*; AX, BX, CX and DX are all its
    cmp dl, VID_CGA             ; answers, which is why the origin is banked
    je .small

    mov cx, [wz_x]
    mov dx, [wz_y]
    mov si, wz_32tl
    call OSAPI_ICON_DRAW
    add cx, 16
    mov si, wz_32tr
    call OSAPI_ICON_DRAW
    mov cx, [wz_x]
    mov dx, [wz_y]
    add dx, 16
    mov si, wz_32bl
    call OSAPI_ICON_DRAW
    add cx, 16
    mov si, wz_32br
    call OSAPI_ICON_DRAW
    jmp short .out
.small:
    mov cx, [wz_x]
    mov dx, [wz_y]
    mov si, wz_14l
    call OSAPI_ICON_DRAW
    add cx, 16
    mov si, wz_14r
    call OSAPI_ICON_DRAW
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
; Each quadrant is one 16 px column of it, `db 1, rows` then `rows` mask words
; then `rows` data words - the plain record OSAPI_ICON_DRAW takes, bit 15 =
; the leftmost pixel of the word.
; =============================================================================

; --- 32 rows: VGA, EGA and Hercules -----------------------------------------
wz_32tl:                        ; rows 0..15, columns 0..15
    db 1, 16
    dw 0x0000                   ;  0
    dw 0x3F00, 0x3F00           ;  1.. 2  flange
    dw 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF   ;  3..13 the drum's
    dw 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF           ;        silhouette
    dw 0x3F00, 0x3F00           ; 14..15  flange
    dw 0x0000                   ;  0
    dw 0x3F00, 0x3F00           ;  1.. 2  ..######................######..
    dw 0x3FFF                   ;  3      ..############################..
    dw 0x3F49, 0x3F49, 0x3F49, 0x3F49, 0x3F49           ;  4..12 the winding
    dw 0x3F49, 0x3F49, 0x3F49, 0x3F49
    dw 0x3FFF                   ; 13      ..############################..
    dw 0x3F00, 0x3F00           ; 14..15

wz_32tr:                        ; rows 0..15, columns 16..31
    db 1, 16
    dw 0x0000
    dw 0x00FC, 0x00FC
    dw 0xFFFC, 0xFFFC, 0xFFFC, 0xFFFC, 0xFFFC, 0xFFFC
    dw 0xFFFC, 0xFFFC, 0xFFFC, 0xFFFC, 0xFFFC
    dw 0x00FC, 0x00FC
    dw 0x0000
    dw 0x00FC, 0x00FC
    dw 0xFFFC
    dw 0x24FC, 0x24FC, 0x24FC, 0x24FC, 0x24FC
    dw 0x24FC, 0x24FC, 0x24FC, 0x24FC
    dw 0xFFFC
    dw 0x00FC, 0x00FC

; The cord, the plug and the prongs are SOLID, so their mask and their data
; are the same sixteen rows twice - which is what a filled shape looks like in
; this format and not a mistake.
wz_32bl:                        ; rows 16..31, columns 0..15
    db 1, 16
    dw 0x0003, 0x0003, 0x0003, 0x0003   ; 16..19  the cord
    dw 0x001F                           ; 20      the shoulder
    dw 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF   ; 21..25  the body
    dw 0x001C, 0x001C, 0x001C, 0x001C, 0x001C   ; 26..30  the prongs
    dw 0x0000                           ; 31
    dw 0x0003, 0x0003, 0x0003, 0x0003
    dw 0x001F
    dw 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF
    dw 0x001C, 0x001C, 0x001C, 0x001C, 0x001C
    dw 0x0000

wz_32br:                        ; rows 16..31, columns 16..31
    db 1, 16
    dw 0xC000, 0xC000, 0xC000, 0xC000
    dw 0xF800
    dw 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00
    dw 0x3800, 0x3800, 0x3800, 0x3800, 0x3800
    dw 0x0000
    dw 0xC000, 0xC000, 0xC000, 0xC000
    dw 0xF800
    dw 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00
    dw 0x3800, 0x3800, 0x3800, 0x3800, 0x3800
    dw 0x0000

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
wz_14l:                         ; columns 0..15
    db 1, 14
    dw 0x0000                   ;  0
    dw 0x3F00                   ;  1
    dw 0x3FFF, 0x3FFF, 0x3FFF, 0x3FFF   ;  2.. 5
    dw 0x3F00                   ;  6
    dw 0x0003, 0x0003           ;  7.. 8
    dw 0x001F                   ;  9
    dw 0x00FF, 0x00FF           ; 10..11
    dw 0x001C, 0x001C           ; 12..13
    dw 0x0000
    dw 0x3F00
    dw 0x3FFF
    dw 0x3F49, 0x3F49
    dw 0x3FFF
    dw 0x3F00
    dw 0x0003, 0x0003
    dw 0x001F
    dw 0x00FF, 0x00FF
    dw 0x001C, 0x001C

wz_14r:                         ; columns 16..31
    db 1, 14
    dw 0x0000
    dw 0x00FC
    dw 0xFFFC, 0xFFFC, 0xFFFC, 0xFFFC
    dw 0x00FC
    dw 0xC000, 0xC000
    dw 0xF800
    dw 0xFF00, 0xFF00
    dw 0x3800, 0x3800
    dw 0x0000
    dw 0x00FC
    dw 0xFFFC
    dw 0x24FC, 0x24FC
    dw 0xFFFC
    dw 0x00FC
    dw 0xC000, 0xC000
    dw 0xF800
    dw 0xFF00, 0xFF00
    dw 0x3800, 0x3800
