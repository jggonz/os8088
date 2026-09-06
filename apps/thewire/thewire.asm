; =============================================================================
; os8088 - apps/thewire/thewire.asm
;
; THE WIRE - the online software library (SPEC.md 88).
;
; A window listing every program the project publishes, fetched over
; ETHER.DRV from os8088.com, with a picture, a description, a
; recommended-machine filter and two actions: Load Program runs it now out of
; memory, Add to Disk... writes it and its sidecars to a floppy. A networked
; XT with one 360KB drive reaches the whole collection without ever seeing a
; second disk.
;
; --- apps/wire/ IS WIREFRAME AND IS NOT RENAMED ------------------------------
; SPEC.md 78's instrument had the directory first. This is apps/thewire/,
; THEWIRE.O88, header name 'The Wire'; nothing the user sees carries the file
; name.
;
; --- THE DIVISION IS FORCED (SPEC.md 20.6 rule 7, 88.5) ----------------------
; A worker may not call OSAPI_MEM_*, the file slots or the dialog, and every
; NETV_* verb is non-blocking. So the UI task claims and commits, the worker
; opens, polls, sends and drains, and between them is ONE BYTE: the worker
; writes everything the handler will read and writes [wr_wake] LAST; the
; OSAPI_WM_ONWAKE handler banks the code, clears the flag and only then acts.
; apps/ftpd (SPEC.md 77) is the shape.
;
; **AND A GENERATION COUNTER CHECKED INSIDE THE STORE LOOP** (SPEC.md 71.11),
; which is in wrhttp.inc and is the one thing about this file that is not
; obvious: a selection change landing mid-picture would otherwise put the rest
; of that chunk into a claim the UI task has already freed.
;
; --- EVERYTHING OFF THE WIRE IS HOSTILE --------------------------------------
; The catalog is checked field by field before one byte of it is drawn
; (wr_catck) and a refusal leaves the window usable with `Catalog not
; understood` in the status cell. The only thing a server is trusted about is
; how many bytes it just handed over, and that is bounded by the capacity
; passed to NETV_RECV.
;
; --- THE PEN IS A WORD AND NOT A REGISTER ------------------------------------
; The detail pane's painters walk DOWN the pane, and each one wants to say
; "and the next line goes here". [wr_peny] is that, in the bss, because the
; alternative is a y carried through six routines in a register that every one
; of them also needs for something else - which is how the first version of
; this file went wrong.
;
; Every proc here is a near proc with a near `ret`: the kernel reaches a
; callback through the dispatcher in the package's own header (SPEC.md 20.1),
; and ES is KERNEL_SEG on the way in - so a window record is [es:bx+W_*] and
; never [bx+W_*].
; =============================================================================

%include "os88api.inc"
%include "netpkg.inc"               ; THE DRIVER'S OWN HEADER, the same file
                                    ; drivers/ether/ether.asm includes, so the
                                    ; two ends cannot drift (SPEC.md 20.11)
%include "wcat.inc"                 ; the catalog format (SPEC.md 88.2)
%include "warc.inc"                 ; ...and the archive's (SPEC.md 88.13)
%include "rdabi.inc"                ; RD_STEPKB, the granule RDPV_MOUNT rounds
                                    ; a size to - the RAM disk's own constant
                                    ; rather than a second spelling of 8 here
%include "rdpkg.inc"                ; RAMDISK.DRV's package verbs, THE DRIVER'S
                                    ; OWN HEADER again (SPEC.md 20.11): the two
                                    ; RAM-disk answers Load Program needs
                                    ; before it moves a byte (SPEC.md 62.9.16)

; OSAPI_PKG_RUN is apps/os88api.inc's (SPEC.md 21.5). While the two halves of
; this feature were being built on separate branches there was an %ifndef here
; that defined the slot and four LD_* codes locally, so the package half could
; assemble and be reviewed before the kernel half landed. It is gone with the
; merge, and the LD_* half of it was worth deleting on its own: the codes it
; guessed (1..4) are not the ones the slot answers (2 the header is not a
; package's or it carries PARTS, 3 too large, 4 the entry proc declined, 5 no
; region or no instance record), so what it left behind was a set of names
; nothing used and a reader could believe. wr_pkgrun prints the NUMBER and
; cites SPEC.md 21.4, which is the list.

    OS88_HEADER 'The Wire', wr_entry, OS88_F_ICON, OS88_STACK_192
                                ; **OS88_STACK_192, ARGUED THE WAY
                                ; apps/wire/wire.asm ARGUES ITS CLASS**
                                ; (SPEC.md 8.7): the worker's own deepest
                                ; chain is wr_nstep -> wr_take, four pushes
                                ; and a call, or wr_nshow -> wr_dstat ->
                                ; OSAPI_FONT_RUN, about 40 bytes. What
                                ; dominates is ETHER.DRV's socket verbs -
                                ; ~126 bytes on OUR stack, the driver owning
                                ; no task of its own - and over SPEC.md 8.5's
                                ; 64-byte interrupt floor that is ~190. 192 is
                                ; the class above the sum and is the MODAL
                                ; one: this is a transport poll, which is what
                                ; the class is described for. Telnet takes 256
                                ; because its worker DRAWS A SCREEN through
                                ; te_scroll; this one draws a status cell and
                                ; a picture, each one call deep.
    OS88_ICON16
    ; A two-pin plug with its cord: 16 mask rows (the white underlay) then 16
    ; of ink. A plug rather than a handset because the desktop caption is
    ; `Wire` and a handset reads as a telephone application - and because at
    ; 16x16 a cord is three pixels of zig-zag that survive the CGA's 2.4:1
    ; pixels (SPEC.md 26.4), where a handset's long diagonal does not.
    dw 0x0C30
    dw 0x0C30
    dw 0x0C30
    dw 0x3FFC
    dw 0x3FFC
    dw 0x3FFC
    dw 0x3FFC
    dw 0x07E0
    dw 0x07E0
    dw 0x07E0
    dw 0x0180
    dw 0x0300
    dw 0x0600
    dw 0x0300
    dw 0x0180
    dw 0x0080
    dw 0x0C30
    dw 0x0C30
    dw 0x0C30
    dw 0x3FFC
    dw 0x2004
    dw 0x2004
    dw 0x3FFC
    dw 0x07E0
    dw 0x0420
    dw 0x07E0
    dw 0x0180
    dw 0x0300
    dw 0x0600
    dw 0x0300
    dw 0x0180
    dw 0x0080
    OS88_ICON16_END

; =============================================================================
; GEOMETRY (SPEC.md 88.6) - content-relative throughout
;
; The content origin is 8-aligned by SPEC.md 11.94, so every constant here
; that is a multiple of 8 gives an 8-aligned SCREEN pen: font_run's
; single-store path and gfx_blit1's only path both want that.
; =============================================================================
WR_CW       equ 384             ; content width, fixed on every adapter
WR_FRW      equ WR_CW + 2       ; ...and the frame's, 1px border each side

WR_FY       equ 2               ; the filter row: 12px glyphs
WR_FTY      equ 4               ; ...and its labels' pen
WR_PY       equ 18              ; the two panes' top
WR_LX2      equ 143             ; the list pane's right edge
WR_SBX1     equ 129             ; ...with the scroll bar inside it
WR_SBX2     equ 142
WR_ICX      equ 3               ; the row icon
WR_TXX      equ 24              ; ...and the row text, 8-ALIGNED
WR_TXN      equ 13              ; 13 cells of it: 24 + 104 = 128
WR_DX1      equ 152             ; the detail pane
WR_DX2      equ WR_CW - 1
WR_DTX      equ 160             ; ...its text pen, 8-ALIGNED
                                ; **THE PANE IS SIZED FROM THE FORMAT AND THE
                                ; LIST GETS WHAT IS LEFT**, which is the one
                                ; place the shipped window departs from
                                ; docs/WIRE-PLAN.md's mock. That mock has the
                                ; detail pane start at 176 - 208px - and a
                                ; 27-column description in it, and 27 cells is
                                ; 216px: the first build drew the last two
                                ; words of every description off the
                                ; right-hand edge. The 27 is the CATALOG
                                ; FORMAT (SPEC.md 88.2) and the website
                                ; implements it too, so the pane is what
                                ; moved: a pen at 160 ends at 375 with the
                                ; frame at 383, eight pixels clear.
                                ;
                                ; What it costs is three cells off a list row
                                ; - 13 rather than 16 - and that is the right
                                ; side to lose them on: a list row is an
                                ; INDEX and the detail pane is where the
                                ; title is shown in full, which is what a
                                ; master-and-detail window is for
WR_PICX     equ 208             ; the picture, 8-ALIGNED and centred in the
                                ; pane (152 + (232-128)/2 = 204, snapped down
                                ; to the byte grid gfx_blit1 requires)
WR_ROWH     equ 16              ; a row is an icon tall
WR_DESCC    equ (WR_DX2 - WR_DTX) / 8   ; 27 cells: a pen at WR_DTX with 27
                                ; cells ends at WR_DX2 - 8, which leaves the
                                ; pane's own frame column clear. It is DERIVED
                                ; so that moving either edge moves the ceiling
                                ; with it, and wrtxt.inc's WR_PANE macro
                                ; asserts every hand-written line against it
%if WR_DESCC < WC_DESCW - 1
  %error "the detail pane is narrower than a catalog description line (SPEC.md 88.2)"
%endif
WR_BX1      equ 156             ; the two buttons, STACKED - see SPEC.md 88.6:
WR_BX2      equ 379             ; side by side they want 242px of a 232px pane
WR_BH       equ 17
WR_LNH      equ 10              ; a description line's pitch: an 8-row face
                                ; and two of air, which is what gets five of
                                ; them under the buttons on a VGA
WR_STATN    equ 48              ; the status cell: 48 cells of 8 = the 384

WR_PICMIN   equ 170             ; the pane height at which the picture fits
                                ; beside a title, a tier line and a
                                ; description (SPEC.md 88.6). A CGA's 108-row
                                ; pane fails it and gets the sentences

; --- the two adapters' frames, for OS88_PREFER --------------------------------
WR_HVGA     equ 243             ; content 224 -> 12 rows
WR_HCGA     equ 147             ; content 128 ->  6 rows

; --- the state machine (wrhttp.inc) ------------------------------------------
; **THE ORDER IS LOAD-BEARING**: WS_OPEN..WS_PAUSE is a CONTIGUOUS RANGE and it
; is the whole definition of "a transfer is in flight", which the predicate and
; wr_start both test as a range. The states either side are finished ones.
; brnet.inc carries the scar this copies the shape from - a test of "not IDLE"
; refused every fetch after the first for the life of the window.
;
; **WS_PAUSE IS INSIDE THE RANGE AND HAD TO GO IN IT** (SPEC.md 88.14): an
; archive's worker spends most of the transfer parked at an entry boundary
; waiting for the UI task to write a file, and a pause the predicate read as
; `finished` would offer Load Program on a record whose tree is half on the
; disk. It sits between WS_BODY and WS_DONE so the range stays one compare
; each side and nothing else in this file had to learn about it.
WS_IDLE     equ 0
WS_OPEN     equ 1               ; --- in flight from here...
WS_WAIT     equ 2
WS_SEND     equ 3
WS_HEAD     equ 4
WS_BODY     equ 5
WS_PAUSE    equ 6               ; ...the unpacker is at a boundary and the
                                ; handler owes us the next WS_BODY
WS_DONE     equ 7               ; --- ...to here
WS_FAIL     equ 8

; --- what a transfer is FOR --------------------------------------------------
WK_CAT      equ 0
WK_PIC      equ 1
WK_FILE     equ 2
WK_ARC      equ 3               ; an ARCHIVE (88.13): one stream, a folder
                                ; tree, and the FIRST body this package reads
                                ; that may be over 64KB - which is why
                                ; [wr_got] and [wr_clen] are dwords

; --- what the worker asks the UI task to do (SPEC.md 88.5's one byte) --------
WW_NONE     equ 0
WW_DONE     equ 1
WW_FAIL     equ 2
WW_HDR      equ 3               ; the archive's 32-byte header has arrived and
                                ; the ONE claim is sized from it (88.14)
WW_ENTRY    equ 4               ; ...and one entry's bytes are decoded and
                                ; waiting to be written

WR_RAMSLACK equ 16              ; the KB asked for OVER the tree when a store
                                ; is mounted for it (88.14 step 3): room for
                                ; what the program will save. A machine that
                                ; cannot spare it gets a store sized to the
                                ; tree alone rather than a refusal

; --- and what the UI task is in the middle of (the Add chain) ----------------
WJ_NONE     equ 0
WJ_LOAD     equ 1
WJ_ADD      equ 2

WR_HOSTMAX  equ 48
WR_PFXMAX   equ 32
WR_PATHMAX  equ WR_PFXMAX + 16  ; a prefix, then 'pkg/' and an 8.3 name
WR_REQFIX   equ 96              ; wr_s_get 4 + wr_s_http 17 + wr_s_tail 52 =
                                ; 73, COUNTED and not guessed, with 23 spare
WR_REQMAX   equ WR_REQFIX + WR_HOSTMAX + WR_PATHMAX
                                ; DERIVED FROM THE PIECES, brnet.inc's
                                ; BR_REQMAX and its reason - and the first
                                ; spelling of this got the derivation itself
                                ; wrong at 64, which is nine bytes short of
                                ; the three fixed strings. wr_rputs is
                                ; BX-bounded so the overrun was a CUT, and
                                ; what it cut was the terminating blank line:
                                ; a long host on a long prefix would have sent
                                ; a request no server ever answers
WR_CHUNK    equ 1024            ; one NETV_RECV's landing ground, IN OUR OWN
                                ; SEGMENT (netpkg.inc: ES is yours, so a
                                ; buffer in a heap claim would be read out of
                                ; our own image - SPEC.md 77.10)
WR_HLINE    equ 32              ; one reply header line, clipped
WR_LINEN    equ 64              ; the compose buffer

; =============================================================================
; wr_entry - the package entry point (SPEC.md 20.2)
; in:  DS = ES = our segment, IF = 1, the gfx lock NOT held
; out: BX = window ptr, CF clear
;
; It creates the window, declares its sizes, installs the side-table hooks -
; and then KICKS ITSELF (OSAPI_WM_WAKE, callable from any context). Everything
; that reads a disk or opens a socket happens in the wake handler instead,
; where the window is already on the glass: the entry proc runs inside the
; loader with nothing for the user to look at, and SPEC.md 74.1 names that
; handler as the place for exactly this.
; =============================================================================
wr_entry:
    push si
    mov si, wr_tpl
    call OSAPI_WM_CREATE                ; BX = window ptr, CF on table full
    jc .out
    mov [wr_win], bx
    mov word [wr_sel], 0xFFFF
    mov si, wr_sizes
    call OSAPI_WM_PREFER                ; ...registers AND applies, and
                                        ; preserves the flags (SPEC.md 11.100.1)
    mov cx, WR_FRW
    mov dx, WR_HCGA
    call OSAPI_WM_MINSIZE               ; the CGA frame is the floor: below it
                                        ; the six-row list stops being a list
    mov si, wr_menus
    call OSAPI_MENU_SET
    mov si, wr_about
    call OSAPI_ABOUT_SET                ; SPEC.md 12.2 - every package has one,
                                        ; and CLAUDE.md names the two that
                                        ; shipped without
    mov bx, [wr_win]                    ; BX RE-LOADED BEFORE EACH HOOK. Every
    mov ax, wr_onwake                   ; slot here documents BX = the window
    call OSAPI_WM_ONWAKE                ; and most preserve it, but "most" is
    mov bx, [wr_win]                    ; not a contract to build five installs
    mov ax, wr_onclose                  ; on - and a hook installed against a
    call OSAPI_WM_ONCLOSE               ; stale BX is refused in silence
    mov bx, [wr_win]
    mov ax, wr_onup
    call OSAPI_WM_ONMOUSEUP             ; the thumb's release...
    mov bx, [wr_win]
    mov ax, wr_ondrag
    call OSAPI_WM_ONDRAG                ; ...and its movement. CF = 1 on the
                                        ; 128KB kernel, which has no W_ONDRAG
                                        ; at all; the bar then pages and does
                                        ; not drag, which is what both kernel
                                        ; bars did before SPEC.md 13.10.5
    mov bx, [wr_win]
    call OSAPI_WM_WAKE
    clc
.out:
    pop si
    ret

; --- the window template and the three sizes ---------------------------------
; y = 24 rather than 40 so the CGA frame clears the dock without the fit
; having to move it: 24 + WR_HCGA - 1 = 170, and the dock owns row 176.
wr_tpl:
    dw 60, 24, WR_FRW, WR_HVGA
    dw wr_ttl, wr_paint, wr_onkey, wr_onclick

    OS88_PREFER wr_sizes, WR_FRW, WR_HVGA,  WR_FRW, WR_HVGA,  WR_FRW, WR_HCGA

; =============================================================================
; wr_geom - where everything is, this paint
; out: CF = 1 the window is not visible and nothing was stored
;
; Called at the top of every painter and every hit test, because a window
; moves and an adapter changes. It is the ONE place the layout arithmetic
; lives, so a painter and a hit tester cannot hold different opinions about
; where a row is - os88ui.inc's "GEOMETRY IS A POINTER" discipline applied to
; a layout that is computed rather than declared.
; =============================================================================
wr_geom:
    push ax
    push bx
    push cx
    push dx
    mov bx, [wr_win]
    or bx, bx
    jz .no
    call OSAPI_WM_GEOM                  ; CX = content w, DX = content h
    jc .no
    mov [wr_cw], cx
    mov [wr_ch], dx
    mov bx, [wr_win]
    call OSAPI_WM_CONTENT               ; AX = left, DX = top
    mov [wr_ox], ax
    mov [wr_oy], dx

    mov ax, [wr_ch]                     ; the panes: y2 = ch - 12, so the
    sub ax, 12                          ; status cell's band is clear of them
    mov [wr_y2], ax
    sub ax, WR_PY - 1                   ; AX = the pane height PH
    mov [wr_ph], ax
    sub ax, 2                           ; ...less the two border rows
    mov cl, 4
    shr ax, cl                          ; / WR_ROWH
    or ax, ax
    jnz .rok
    inc ax                              ; a floor of one: MINSIZE makes this
.rok:                                   ; unreachable, and a zero here would
    mov [wr_rows], ax                   ; divide the scroll bar by nothing

    mov ax, [wr_y2]                     ; --- the two buttons, bottom-anchored
    sub ax, WR_BH + 1
    mov [wr_rb+2], ax
    add ax, WR_BH - 1
    mov [wr_rb+6], ax
    mov ax, [wr_rb+2]
    sub ax, WR_BH + 1
    mov [wr_ra+2], ax
    add ax, WR_BH - 1
    mov [wr_ra+6], ax
    mov ax, [wr_ra+2]                   ; ...and what the pane may write above
    sub ax, 3                           ; them, which every text painter clips
    mov [wr_ylim], ax                   ; against

    mov ax, [wr_oy]                     ; the y pairs are content-relative
    add [wr_ra+2], ax                   ; until here, one add each
    add [wr_ra+6], ax
    add [wr_rb+2], ax
    add [wr_rb+6], ax
    mov ax, [wr_ox]
    add ax, WR_BX1
    mov [wr_ra+0], ax
    mov [wr_rb+0], ax
    add ax, WR_BX2 - WR_BX1
    mov [wr_ra+4], ax
    mov [wr_rb+4], ax
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_ax / wr_dy - a content-relative coordinate to a screen one -----------
; Two one-liners rather than an add at ninety call sites, and they are why the
; constants above read as a layout instead of as arithmetic.
wr_ax:                                  ; in/out AX
    add ax, [wr_ox]
    ret
wr_dy:                                  ; in/out DX
    add dx, [wr_oy]
    ret

; --- wr_clip - arm the visible region for a BACKGROUND draw (SPEC.md 11.3) --
; in:  the gfx lock held, in a context that is not W_PAINT and not a click
; out: [wr_hid] = 1 when nothing of the window can be seen
;
; THE WAKE HANDLER IS A BACKGROUND PAINTER. It draws the list, the pane, the
; buttons and the status cell after a transfer lands - and after `Load
; Program` has just put a NEW WINDOW ON TOP OF OURS, which is exactly when it
; redraws the two buttons. Drawn at absolute coordinates with no region
; armed, they landed inside the launched program's content and stayed there
; until that window was next repainted. So every lock hold in wr_onwake arms
; the region first, and each painter below tests its own rect against it
; (OSAPI_WM_CLIP_TEST) before it erases anything - the whole-unit gate of
; 11.3's granularity rule, since each of them fills and then draws glyphs.
; CF = 1 arms NOTHING (the window is hidden or wholly covered), so the state
; work still runs and the drawing is skipped by the flag, not by the region.
; -----------------------------------------------------------------------------
wr_clip:
    push bx
    mov byte [wr_hid], 0
    mov bx, [wr_win]
    call OSAPI_WM_CLIP_SET
    jnc .out
    mov byte [wr_hid], 1
.out:
    pop bx
    ret

; --- wr_lockarm - take the lock, arm the region, re-measure ------------------
; The wake handler and wrarc.inc hold the lock nine times between them and
; every one of those holds owes the same three calls: SPEC.md 11.3's region
; before anything is drawn at absolute coordinates, and wr_geom because a
; window moves. Written once, so a hold that forgot the arm cannot be added by
; hand - which is the defect 88.6.1 exists about.
wr_lockarm:
    call OSAPI_GFX_LOCK
    call wr_clip
    call wr_geom
    ret

; --- wr_dall - the whole content, from a state change ------------------------
; The menu follows the buttons, the list follows the catalog and the pane and
; the cell follow the selection. Three call sites want exactly this set and
; they wanted it in three different orders once.
wr_dall:
    call wr_menufix
    call wr_dlist
    call wr_ddet
    call wr_dstat
    ret

; --- wr_dend - what every finished chain redraws -----------------------------
wr_dend:
    call wr_menufix
    call wr_dstat
    call wr_dbtns
    ret

; =============================================================================
; THE PAINTERS
; =============================================================================

; -----------------------------------------------------------------------------
; wr_paint - W_PAINT: the whole content, which arrives white-filled
; in:  SI = window ptr; the gfx lock is held
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
wr_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_geom
    jc .out
    call wr_hire                        ; the worker, retried from every paint:
                                        ; a full task table is a NORMAL,
                                        ; TRANSIENT outcome (SPEC.md 20.6) and
                                        ; latching only on success is br_hire's
                                        ; shape
    call wr_dfilter
    call wr_dlist
    call wr_ddet
    call wr_dstat
    cmp byte [wr_abon], 0               ; ...and the About card LAST, over the
    je .out                             ; window it is opaque about (20.5.1)
    mov bx, si
    mov si, wr_ablines
    call os88ui_about_d                 ; _d: this paint's region is armed
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_dfilter - the five radios and their labels
;
; The row is redrawn WHOLE only from W_PAINT; a click redraws the two glyphs
; that changed (wr_dglyph), because os88ui_glyph is 35-50 ms on the field
; machine and five of them is a quarter of a second for a change of one.
; -----------------------------------------------------------------------------
wr_dfilter:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [wr_ox]
    mov dx, WR_FTY
    call wr_dy
    mov si, wr_s_show
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    xor di, di                          ; DI = which radio
.l:
    call wr_dglyph
    mov bx, di                          ; ...and its label
    shl bx, 1
    mov si, [wr_filtxt + bx]
    mov cx, [wr_filx + bx]
    add cx, OS88UI_GW + 4               ; 8-ALIGNED by construction: every
                                        ; wr_filx entry is a multiple of 8 and
                                        ; so is 16
    add cx, [wr_ox]
    mov dx, WR_FTY
    call wr_dy
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    inc di
    cmp di, 5
    jb .l
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_dglyph - one radio, DI = its index -----------------------------------
wr_dglyph:
    push ax
    push bx
    push cx
    push dx
    mov bx, di
    shl bx, 1
    mov cx, [wr_filx + bx]
    add cx, [wr_ox]
    mov dx, WR_FY
    call wr_dy
    mov al, OS88UI_GRADIO
    mov bl, [wr_filter]
    xor bh, bh
    cmp bx, di
    jne .off
    or al, OS88UI_GON
.off:
    xor ah, ah                          ; never disabled: an empty catalog
                                        ; still filters to nothing, which is a
                                        ; true answer and not a refusal
    call os88ui_glyph
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_dlist - the list pane: its frame, every visible row, and the scroll bar
; -----------------------------------------------------------------------------
wr_dlist:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [wr_ox]
    mov bx, WR_PY
    add bx, [wr_oy]
    mov cx, WR_LX2
    add cx, [wr_ox]
    mov dx, [wr_y2]
    call wr_dy
    cmp byte [wr_hid], 0
    jne .out
    call OSAPI_WM_CLIP_TEST             ; the whole pane or none of it
    jc .out                             ; (SPEC.md 11.3, wr_clip)
    call OSAPI_GFX_FRAME
    mov word [wr_selrow], 0xFFFF        ; wr_drow re-answers it below
    xor di, di
.l:
    call wr_drow
    inc di
    cmp di, [wr_rows]
    jb .l
    call wr_dscroll
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_drow - one list row, DI = its index on screen (0..[wr_rows]-1)
; in:  DI; the gfx lock held, wr_geom already run
; out: nothing; every register preserved
;
; ONE OSAPI_FONT_RUN per row (SPEC.md 6.1) - the title, its padding and the
; NEW tag are composed into one 16-cell buffer and put down opaquely, so the
; row is never momentarily blank. The white fill before it is for the FOUR
; rows an 8-pixel run does not cover in a 16-pixel one and for the ground
; around a mask-shaped icon; it is one call, and a row is only redrawn when
; the selection moves or the view scrolls, which are both human events.
; -----------------------------------------------------------------------------
wr_drow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, di                          ; --- the row's y, banked
    mov cl, 4
    shl ax, cl
    add ax, WR_PY + 1
    mov [wr_rowy], ax

    mov al, CWHITE                      ; --- erase the row band
    call OSAPI_SET_COLOR
    call wr_rowrect
    call OSAPI_GFX_FILL

    mov ax, [wr_top]                    ; --- which record is this?
    add ax, di
    call wr_nth
    jc .out                             ; past the end: the erase was the row
    mov [wr_rowrec], ax

    call wr_recs                        ; --- the icon, into a record with the
    add si, WC_ICON                     ; two-byte prefix OSAPI_ICON_DRAW wants
    call wr_icocopy                     ; (the catalog carries the 64 rows and
                                        ; not the header, SPEC.md 88.2)
    mov cx, WR_ICX
    add cx, [wr_ox]
    mov dx, [wr_rowy]
    call wr_dy
    mov si, wr_icobuf
    call OSAPI_ICON_DRAW

    mov ax, [wr_rowrec]                 ; --- the text, one run
    call wr_rowtext
    mov cx, WR_TXX
    add cx, [wr_ox]
    mov dx, [wr_rowy]
    add dx, 4
    call wr_dy
    mov si, wr_line
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN

    mov ax, [wr_rowrec]                 ; --- and the selection, XOR-ed over it
    cmp ax, [wr_sel]
    jne .out
    mov [wr_selrow], di
    call wr_rowrect
    call OSAPI_GFX_XOR_FILL
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_rowrect - AX/BX/CX/DX = the row band's screen rect -------------------
wr_rowrect:
    mov ax, 1
    call wr_ax
    mov bx, [wr_rowy]
    add bx, [wr_oy]
    mov cx, WR_SBX1 - 1
    add cx, [wr_ox]
    mov dx, [wr_rowy]
    add dx, WR_ROWH - 1
    call wr_dy
    ret

; --- wr_icocopy - 64 bytes from the catalog claim into wr_icobuf + 2 ---------
; in:  ES:SI = the record's WC_ICON; DS = ours
; The one place this package reads a far pointer by hand. `rep movsw` wants
; DS:SI and ES:DI, and here the SOURCE is the far one, so the loop reads with
; an override and stores near.
wr_icocopy:
    push ax
    push cx
    push di
    push si
    mov word [wr_icobuf], (16 << 8) | 1 ; **THE RECORD'S OWN HEADER**, and the
                                        ; loader-zeroed bss does NOT supply it:
                                        ; icon_draw_x refuses ww != 1 outright,
                                        ; so without this every row icon is
                                        ; refused and the list draws text over
                                        ; a blank square (SPEC.md 25.6)
    mov di, wr_icobuf + 2
    mov cx, 32
.l:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    loop .l
    pop si
    pop di
    pop cx
    pop ax
    ret

; --- wr_rowtext - record AX's title into wr_line, 16 cells, NEW right-aligned
; The tag is written INTO the padded field rather than drawn as a second run,
; which is what keeps a row at one OSAPI_FONT_RUN.
wr_rowtext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ax
    mov di, wr_line                     ; the field, blanked
    mov cx, WR_TXN
    mov al, ' '
    push ds
    pop es
    rep stosb
    mov byte [wr_line + WR_TXN], 0
    pop ax
    call wr_recs
    mov dl, [es:si+WC_FLAGS]
    mov cx, WR_TXN                      ; ...12 cells when a NEW tag is coming
    test dl, WF_NEW
    jz .n
    mov cx, WR_TXN - 4
.n:
    add si, WC_TITLE
    mov di, wr_line
.c:
    mov al, [es:si]
    cmp al, ' '
    jb .pad                             ; NUL, or a control byte off the wire
    mov [di], al
    inc si
    inc di
    loop .c
.pad:
    test dl, WF_NEW
    jz .done
    push ds
    pop es
    mov di, wr_line + WR_TXN - 3
    mov si, wr_s_newtag
    mov cx, 3
    rep movsb
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_dscroll - the block, then os88ui_sbar --------------------------------
wr_dscroll:
    push ax
    push bx
    mov ax, WR_SBX1
    call wr_ax
    mov [wr_sb+0], ax
    mov ax, WR_SBX2
    call wr_ax
    mov [wr_sb+4], ax
    mov ax, WR_PY
    add ax, [wr_oy]
    mov [wr_sb+2], ax
    mov ax, [wr_y2]
    add ax, [wr_oy]
    mov [wr_sb+6], ax
    call wr_count
    mov [wr_sb+8], ax
    mov ax, [wr_rows]
    mov [wr_sb+10], ax
    mov ax, [wr_top]
    mov [wr_sb+12], ax
    mov bx, wr_sb
    call os88ui_sbar
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_ddet - the detail pane
;
; Three states, and the FIRST is the one that matters: with no driver the pane
; carries four lines a person can act on, which is SPEC.md 47 rule 3 at the
; scale of a whole window. Never a modal alert for an absent card.
; -----------------------------------------------------------------------------
wr_ddet:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, WR_DX1
    call wr_ax
    mov bx, WR_PY
    add bx, [wr_oy]
    mov cx, WR_DX2
    add cx, [wr_ox]
    mov dx, [wr_y2]
    call wr_dy
    cmp byte [wr_hid], 0
    jne .out
    call OSAPI_WM_CLIP_TEST             ; the whole pane or none of it
    jc .out                             ; (SPEC.md 11.3, wr_clip)
    call OSAPI_GFX_FRAME
    mov al, CWHITE                      ; the INTERIOR, so an incremental
    call OSAPI_SET_COLOR                ; repaint has a ground of its own
    mov ax, WR_DX1 + 1
    call wr_ax
    mov bx, WR_PY + 1
    add bx, [wr_oy]
    mov cx, WR_DX2 - 1
    add cx, [wr_ox]
    mov dx, [wr_y2]
    dec dx
    call wr_dy
    call OSAPI_GFX_FILL

    mov word [wr_peny], WR_PY + 3
    cmp byte [wr_nodrv], 0
    je .have
    mov si, wr_nodrvl                   ; --- no driver, no link
.nl:
    mov ax, [si]
    or ax, ax
    jz .btns
    push si
    mov si, ax
    call wr_dline
    pop si
    add si, 2
    add word [wr_peny], 12
    jmp short .nl

.have:
    cmp word [wr_sel], 0xFFFF
    jne .rec
    mov si, wr_s_pick                   ; --- nothing chosen yet
    call wr_dline
    jmp short .btns

.rec:
    call wr_dpic                        ; --- the picture, when the pane can
    call wr_dinfo                       ; hold one, then the facts, then the
    call wr_ddesc                       ; five pre-wrapped lines
.btns:
    call wr_dbtns
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_dline - the string at SI at [wr_peny], and the pen does NOT move -----
; Clipped against [wr_ylim], which is the top of the upper button: a short
; pane simply stops drawing rather than lettering over a control.
wr_dline:
    push ax
    push cx
    push dx
    mov dx, [wr_peny]
    add dx, 8
    cmp dx, [wr_ylim]
    ja .out
    mov cx, WR_DTX
    add cx, [wr_ox]
    mov dx, [wr_peny]
    call wr_dy
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_dpic - the picture, or 'No picture', or neither
;
; THE TEST IS THE PANE'S OWN HEIGHT and not a VID_* compare (SPEC.md 39): a
; 108-row CGA pane cannot hold 64 rows of picture, a title, a tier line and any
; description at all, so it gets the sentences, which are what a person acts
; on. An EGA at 640x350 gets the picture on the same arithmetic without being
; named anywhere.
; -----------------------------------------------------------------------------
wr_dpic:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    cmp word [wr_ph], WR_PICMIN
    jb .out
    call wr_selrec
    test byte [es:si+WC_FLAGS], WF_PIC
    jz .none
    cmp byte [wr_picok], 0
    je .none                            ; asked for and not here yet: the pane
                                        ; says 'No picture' rather than leaving
                                        ; the last program's on the glass
    call wr_blitpic
    jmp short .after
.none:
    add word [wr_peny], 28
    mov si, wr_s_nopic
    call wr_dline
    sub word [wr_peny], 28
.after:
    add word [wr_peny], WIRE_PICH + 3   ; three rows of air, and they are
                                        ; counted: 5 description lines at
                                        ; WR_LNH have to clear the upper
                                        ; button, and at +5 the fifth does not
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_blitpic - the 1,024-byte band onto the glass (SPEC.md 88.3)
;
; The buffer was INVERTED as it arrived, so a set bit is a paper pixel - which
; is what gfx_blit1 draws with its default pen on a VGA and what a 1bpp
; adapter draws whatever the pen says. One band, one call, no adapter branch.
;
; CF = 1 is a kern_small kernel, which carries the slot and not the body. The
; fallback expands ONE ROW at a time into a 64-byte scratch and calls
; OSAPI_GFX_BLIT4 with DX = 1: the whole picture as packed 4bpp would want
; 4,096 bytes of bss for a path that runs on one kernel.
; -----------------------------------------------------------------------------
wr_blitpic:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds
    pop es
    mov si, wr_pic
    mov bp, WIRE_PICB
    mov ax, WR_PICX
    call wr_ax
    mov bx, [wr_peny]
    add bx, [wr_oy]
    mov cx, WIRE_PICW
    mov dx, WIRE_PICH
    call OSAPI_GFX_BLIT1
    jnc .out
    mov si, wr_pic                      ; --- the fallback, a row at a time
    mov bx, [wr_peny]
    add bx, [wr_oy]
    mov di, WIRE_PICH
.row:
    call wr_expand                      ; wr_prow = 64 packed 4bpp bytes
    push si
    push bx
    mov si, wr_prow
    mov bp, WIRE_PICW / 2
    mov ax, WR_PICX
    call wr_ax
    mov cx, WIRE_PICW
    mov dx, 1
    call OSAPI_GFX_BLIT4
    pop bx
    pop si
    add si, WIRE_PICB
    inc bx
    dec di
    jnz .row
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

; --- wr_expand - one 16-byte row at SI into 64 packed 4bpp bytes -------------
; A set bit is paper (see above), so it expands to CWHITE and a clear one to
; CBLACK. Two pixels a byte, high nibble leftmost.
wr_expand:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, wr_prow
    mov cx, WIRE_PICB
.b:
    mov bl, [si]
    inc si
    mov dh, 4                           ; four PAIRS of pixels a source byte
.p:
    xor al, al
    shl bl, 1
    jnc .p0
    mov al, CWHITE << 4
.p0:
    shl bl, 1
    jnc .p1
    or al, CWHITE
.p1:
    mov [di], al
    inc di
    dec dh
    jnz .p
    loop .b
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_dinfo - the title, the tier and size line, and the machine note
;
; THE MACHINE'S OWN TIER IS INFORMATION AND NEVER A REFUSAL (SPEC.md 60.2). It
; is drawn only when it is BELOW the program's, because "This machine: 386"
; under a program recommended for an 8088 is a fact nobody needed.
; -----------------------------------------------------------------------------
wr_dinfo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call wr_selrec
    mov di, wr_line                     ; --- the title
    push si
    add si, WC_TITLE
    mov cx, 24
    call wr_sputn
    pop si
    mov byte [di], 0
    push si
    mov si, wr_line
    call wr_dline
    pop si
    add word [wr_peny], 12

    mov di, wr_line                     ; --- '8088/8086     15K'
    mov bl, [es:si+WC_TIER]
    and bl, 3
    xor bh, bh
    shl bx, 1
    push si
    mov si, [wr_tiers + bx]
    call wr_sput
    pop si
    mov cx, wr_line + 12
    sub cx, di
    jbe .noshim
.shim:
    mov byte [di], ' '
    inc di
    loop .shim
.noshim:
    mov ax, [es:si+WC_SIZE]
    mov dx, [es:si+WC_SIZE+2]
    call wr_kfig
    mov byte [di], 0
    push si
    mov si, wr_line
    call wr_dline
    pop si
    add word [wr_peny], 12

    mov bl, [es:si+WC_TIER]             ; --- the machine note, drawn ONLY
    and bl, 3                           ; when this machine is BELOW what the
    call OSAPI_CPU_INFO                 ; program wants (SPEC.md 60.2: the tier
    cmp al, bl                          ; is information and never a refusal -
    jae .out                            ; 'This machine: 386' under a program
    mov bl, al                          ; recommended for an 8088 is a fact
    xor bh, bh                          ; nobody needed)
    shl bx, 1
    mov di, wr_line
    mov si, wr_s_mach
    call wr_sput
    mov si, [wr_tiers + bx]
    call wr_sput
    mov byte [di], 0
    mov si, wr_line
    call wr_dline
    add word [wr_peny], 12
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
; wr_ddesc - the five PRE-WRAPPED lines (SPEC.md 88.2)
;
; The machine wraps nothing: the writer laid them out at 27 columns, which is
; the whole reason the description is five fixed fields and not one string.
; wr_dline clips against the buttons, so a short pane draws as many as fit.
; -----------------------------------------------------------------------------
wr_ddesc:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp                             ; the counter, and BP is the kernel
    push es                             ; dispatcher's register (SPEC.md 20.1)
    call wr_selrec
    add si, WC_DESC
    mov bp, WC_DESCN
.l:
    push si
    mov di, wr_line
    mov cx, WC_DESCW - 1
    call wr_sputn
    mov byte [di], 0
    pop si
    cmp byte [wr_line], 0
    je .next                            ; an all-NUL line is an absent one
    push si
    mov si, wr_line
    call wr_dline
    pop si
    add word [wr_peny], WR_LNH
.next:
    add si, WC_DESCW
    dec bp
    jnz .l
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
; wr_dbtns - the two buttons, greyed from THE SAME PREDICATE the click reads
; (SPEC.md 47, 88.7). OS88UI_FILL because the greying MOVES: without the
; erase the second draw's checkerboard caption lands on top of the first
; draw's solid one and a disabled label comes out pixel-identical to a live
; one (os88ui.inc's own note).
; -----------------------------------------------------------------------------
wr_dbtns:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_menufix                     ; THE MENU FOLLOWS THE BUTTONS, and it
                                        ; is hung here rather than at each of
                                        ; the eight places that change the
                                        ; state: every one of them ends by
                                        ; drawing the buttons, so this is the
                                        ; one edge that cannot be forgotten
    mov byte [wr_grey], 0               ; ...and so does [wr_grey], which is
                                        ; SPEC.md 13.8's rule rather than an
                                        ; instrument: "keep which control is
                                        ; down in a variable your W_PAINT
                                        ; reads". What is kept here is which
                                        ; control is GREYED, written where it
                                        ; is drawn, so the drawn state and the
                                        ; recorded one cannot disagree - and
                                        ; it is what makes 47's greying
                                        ; assertable by a gate at all
                                        ; (tests/thewire.py), where the pixels
                                        ; of a checkerboard caption are not
    xor al, al                          ; Load Program
    call wr_may
    mov di, OS88UI_FILL
    jnc .a
    or di, OS88UI_DIS
    or byte [wr_grey], 1
.a:
    mov bx, wr_ra
    call wr_btnok                       ; THE STATE IS RECORDED WHETHER OR NOT
    jc .a2                              ; THE BUTTON IS DRAWN: a covered button
    mov si, wr_s_run                    ; still answers a click and the gate
    call os88ui_btn                     ; still reads [wr_grey]
.a2:
    mov al, 1                           ; Add to Disk...
    call wr_may
    mov di, OS88UI_FILL
    jnc .b
    or di, OS88UI_DIS
    or byte [wr_grey], 2
.b:
    mov bx, wr_rb
    call wr_btnok
    jc .b2
    mov si, wr_s_add
    call os88ui_btn
.b2:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_btnok - may the button whose rect is at BX be drawn? out CF = 1 no --
; The rect is the four screen words wr_geom left there. Every register but
; the flags is preserved, which the two call sites above lean on.
wr_btnok:
    cmp byte [wr_hid], 0
    jne .no
    push ax
    push bx
    push cx
    push dx
    mov ax, [bx]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov bx, [bx+2]
    call OSAPI_WM_CLIP_TEST
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; wr_dstat - the status cell: ONE opaque run, space-padded to a fixed width
;
; SPEC.md 6.1. It changes on every state edge of a transfer and a
; fill-then-letter pair would blank it each time; x is [wr_ox], which is
; 8-aligned, so the whole 48 cells are a single store a row.
; -----------------------------------------------------------------------------
wr_dstat:
    cmp byte [wr_hid], 0
    jne .hid
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_statbuild
    mov cx, [wr_ox]
    mov dx, [wr_ch]
    sub dx, 10
    call wr_dy
    mov si, wr_sline
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.hid:
    ret

; --- wr_statbuild - what the status cell says, padded to WR_STATN cells ------
wr_statbuild:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, wr_sline
    mov al, [wr_state]
    cmp al, WS_OPEN
    jb .settled
    cmp al, WS_DONE
    jae .settled
    cmp al, WS_SEND                     ; --- in flight
    jae .reading
    mov si, wr_s_conn
    call wr_sput
    jmp .pad
.reading:
    mov si, wr_s_load
    cmp word [wr_msg], 0                ; ...or WHICH FILE OF THE SET, which is
    je .rsay                            ; the thing a person watching a
    mov si, [wr_msg]                    ; four-file Add - or a fifty-nine-file
                                        ; archive - wants to know. The
                                        ; kilobyte figure still follows it, and
                                        ; a plain Load leaves [wr_msg] at zero
                                        ; (wr_start clears it) so it says
                                        ; `Loading from the Wire...` as before
.rsay:
    call wr_sput
    mov al, [wr_state]
    cmp al, WS_BODY
    je .fig
    cmp al, WS_PAUSE                    ; an archive spends most of a transfer
    jne .pad                            ; parked here (88.14), and a figure
.fig:                                   ; that vanished at every entry would
                                        ; read as a stall
    mov ax, [wr_clen]
    or ax, [wr_clen+2]
    jz .pad
    mov byte [di], ' '
    inc di
    mov ax, [wr_got]                    ; ' NNK of MMK', over 32 bits: an
    mov dx, [wr_got+2]                  ; archive is the one body that passes
    call wr_kfig                        ; 64KB and a word here would show it
    mov si, wr_s_of                     ; counting from zero again
    call wr_sput
    mov ax, [wr_clen]
    mov dx, [wr_clen+2]
    call wr_kfig
    jmp short .pad
.settled:
    cmp word [wr_msg], 0                ; A MESSAGE BELONGS TO A FINISHED
    je .idle                            ; STATE and not only to a failed one -
    mov si, [wr_msg]                    ; brnet.inc's scar: a successful write
    call wr_sput                        ; had to claim to have failed to say so
    jmp short .pad
.idle:
    cmp word [wr_n], 0
    je .none
    mov si, wr_s_avail
    call wr_sput
    mov ax, [wr_n]
    call wr_snum
    mov si, wr_s_progs
    cmp word [wr_n], 1
    jne .plural
    mov si, wr_s_prog1
.plural:
    call wr_sput
    jmp short .pad
.none:
    mov si, wr_s_nodrv
    call wr_sput
.pad:
    mov cx, wr_sline + WR_STATN
    sub cx, di
    jbe .term
.sp:
    mov byte [di], ' '
    inc di
    loop .sp
.term:
    mov byte [wr_sline + WR_STATN], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; COMPOSING - the four string helpers everything above uses
; Every one takes DI as the write pointer and advances it; none of them can
; run past wr_line's end, because every caller's total is bounded by
; WR_LINEN and the two longest (a 24-char title, a 27-char description) are
; declared with their own limits.
; =============================================================================

; --- wr_sput - the NUL string at DS:SI to DS:DI ------------------------------
wr_sput:
    push ax
    push si
.c:
    mov al, [si]
    or al, al
    jz .out
    mov [di], al
    inc di
    inc si
    jmp short .c
.out:
    pop si
    pop ax
    ret

; --- wr_sputn - at most CX bytes of the NUL string at ES:SI to DS:DI ---------
; ES because every caller of this one is copying out of the CATALOG CLAIM, and
; a byte below 0x20 ends the field: everything off the wire is hostile and the
; font has no glyph for a control byte (SPEC.md 88.2's ASCII rule, enforced by
; the reader as well as by the writer).
wr_sputn:
    push ax
    push cx
    push si
    jcxz .out
.c:
    mov al, [es:si]
    cmp al, ' '
    jb .out
    cmp al, 0x7F
    jae .out
    mov [di], al
    inc di
    inc si
    loop .c
.out:
    pop si
    pop cx
    pop ax
    ret

; --- wr_snum - AX as decimal to DS:DI ----------------------------------------
wr_snum:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.e:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .e
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_kfig - DX:AX bytes as 'NNK' to DS:DI, rounded UP ---------------------
; Rounded up because 0K next to a program that is plainly there reads as an
; empty file; 1K is the smallest true thing a cluster-sized number can say.
wr_kfig:
    push ax
    push bx
    push cx
    push dx
    push si                             ; ...AND SI, which the tail below
                                        ; spends on wr_s_k: wr_dinfo is holding
                                        ; the RECORD in it across this call
    add ax, 1023
    adc dx, 0
    mov cl, 10
.s:
    shr dx, 1
    rcr ax, 1
    dec cl
    jnz .s
    or dx, dx                           ; over 64MB: not a thing this format
    jz .ok                              ; can hold, and wr_catck refused it
    mov ax, 0xFFFF                      ; long before here
.ok:
    call wr_snum
    mov si, wr_s_k
    call wr_sput
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE CATALOG
; =============================================================================

; --- wr_recs - ES:SI = record AX's base --------------------------------------
; in:  AX = a record index below [wr_n]; out ES:SI, AX preserved
; WIRE_CATMAX bounds [wr_n] at 63, so WIRE_HDR + WIRE_REC*i cannot leave 16
; bits - which is the reason that ceiling is a reader's rule and not only the
; claim's size (SPEC.md 88.2).
wr_recs:
    push ax
    push cx
    mov es, [wr_catseg]
    mov cl, 8
    shl ax, cl
    add ax, WIRE_HDR
    mov si, ax
    pop cx
    pop ax
    ret

; --- wr_selrec - ES:SI = the SELECTED record's base --------------------------
; Fifteen call sites wanted `mov ax, [wr_sel]` and then wr_recs, and one of
; them is in the worker (wrhttp.inc's path composer): [wr_catseg] and [wr_sel]
; are both stable words, so reading a record from either task is safe and the
; two spellings of it may as well be one.
wr_selrec:
    push ax
    mov ax, [wr_sel]
    call wr_recs
    pop ax
    ret

; --- wr_pass - is a record of tier AL shown under the current filter? --------
; out: CF = 0 shown. Filter 0 is All; filter f shows tier <= f-1.
wr_pass:
    push bx
    mov bl, [wr_filter]
    or bl, bl
    jz .yes
    dec bl
    cmp al, bl
    ja .no
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx
    ret

; --- wr_nth - the AXth record the filter shows -------------------------------
; in:  AX = a visible index; out CF = 0 and AX = the record index
;
; A LINEAR SCAN AND NOT AN INDEX ARRAY, deliberately: 63 records is 63 byte
; compares, twelve rows of that is 756, and the array it replaces would be 255
; bytes of bss that has to be rebuilt whenever the filter moves. On the field
; machine the whole scan for a full list is well under one drawing call.
wr_nth:
    push bx
    push cx
    push dx
    push si
    push es
    mov dx, ax
    xor bx, bx
.l:
    cmp bx, [wr_n]
    jae .no
    mov ax, bx
    call wr_recs
    mov al, [es:si+WC_TIER]
    call wr_pass
    jc .next
    or dx, dx
    jz .yes
    dec dx
.next:
    inc bx
    jmp short .l
.yes:
    mov ax, bx
    clc
    jmp short .out
.no:
    stc
.out:
    pop es                              ; ONE epilogue: `pop` leaves the flags
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- wr_count - how many records the filter shows ----------------------------
wr_count:
    push bx
    push si
    push es
    xor ax, ax
    xor bx, bx
.l:
    cmp bx, [wr_n]
    jae .out
    push ax
    mov ax, bx
    call wr_recs
    mov al, [es:si+WC_TIER]
    call wr_pass
    pop ax
    jc .next
    inc ax
.next:
    inc bx
    jmp short .l
.out:
    pop es
    pop si
    pop bx
    ret

; --- wr_vispos - where record AX sits in the filtered order ------------------
; out: CF = 0 and AX = its visible index; CF = 1 = the filter hides it
wr_vispos:
    push bx
    push cx
    push dx
    push si
    push es
    mov dx, ax
    xor bx, bx
    xor cx, cx
.l:
    cmp bx, [wr_n]
    jae .no
    mov ax, bx
    call wr_recs
    mov al, [es:si+WC_TIER]
    call wr_pass
    jc .next
    cmp bx, dx
    je .yes
    inc cx
.next:
    inc bx
    jmp short .l
.yes:
    mov ax, cx
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wr_catck - is what arrived a catalog? (SPEC.md 88.2)
; in:  the claim is [wr_catseg], [wr_catlen] bytes of it filled
; out: CF = 1 refused, and NOTHING was stored: [wr_n] stays 0
;
; EVERY FIELD, BEFORE ONE BYTE IS DRAWN. A refusal is an ordinary path: the
; window stays usable and the status cell says `Catalog not understood`, which
; is what a person can act on. A reader that trusted the count and the offsets
; would address anywhere in the 64KB the claim sits in.
; -----------------------------------------------------------------------------
wr_catck:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov es, [wr_catseg]
    mov ax, [wr_catlen]
    cmp ax, WIRE_HDR
    jb .no
    cmp ax, WIRE_CATMAX
    ja .no
    cmp word [es:WC_MAGIC], 'WI'        ; 'WIRE', as two words
    jne .no
    cmp word [es:WC_MAGIC+2], 'RE'
    jne .no
    cmp byte [es:WC_VER], WIRE_VER
    jne .no
    cmp byte [es:WC_RSZ], WIRE_REC / 16
    jne .no
    cmp word [es:WC_HSZ], WIRE_HDR
    jne .no
    mov cx, [es:WC_N]
    or cx, cx
    jz .no
    cmp cx, WIRE_NMAX
    ja .no
    mov ax, cx                          ; the record array must END inside the
    push cx                             ; file. N <= 255 and 255 * 256 is
    mov cl, 8                           ; 0xFF00, so the shift cannot wrap and
    shl ax, cl                          ; the add is what has to be checked
    pop cx
    add ax, WIRE_HDR
    jc .no
    cmp ax, [wr_catlen]
    ja .no
    mov bx, [es:WC_SCN]                 ; ...and so must the sidecar table
    or bx, bx
    jz .sok
    mov ax, bx
    mov bx, WIRE_SC
    mul bx
    or dx, dx
    jnz .no
    add ax, [es:WC_SCOFF]
    jc .no
    cmp ax, [wr_catlen]
    ja .no
.sok:
    mov [wr_n], cx                      ; ...and only now is it a catalog
    mov si, WC_DATE                     ; the About card names WHICH catalog
    mov di, wr_abdate + WR_ABDATEO      ; is on the glass rather than which
    mov cx, 8                           ; build is under it
.date:
    mov al, [es:si]
    cmp al, '0'                         ; hostile: anything but eight digits
    jb .nodate                          ; leaves the question marks standing
    cmp al, '9'
    ja .nodate
    mov [di], al
    inc si
    inc di
    loop .date
.nodate:
    mov cx, [wr_n]
    xor bx, bx                          ; --- every record's own fields
.rec:
    mov ax, bx
    push cx
    call wr_recs
    pop cx
    test byte [es:si+WC_FLAGS], WF_ARC
    jnz .arcrec                         ; an ARCHIVE is bounded differently and
                                        ; by different fields (below)
    cmp byte [es:si+WC_NSIDE], WIRE_SCMAX
    ja .no
    mov ax, [es:si+WC_SIZE+2]           ; a size that needs more than 16 bits
    or ax, ax                           ; is over WIRE_FILEMAX by inspection
    jnz .no
    mov ax, [es:si+WC_SIZE]
    or ax, ax
    jz .no
    cmp ax, WIRE_FILEMAX
    ja .no
    mov bp, ax                          ; ...and WC_TOTAL is the .O88 PLUS its
    mov ax, [es:si+WC_TOTAL+2]          ; sidecars, so it can never be smaller.
    or ax, ax                           ; It is what the free-space check is
    jnz .totok                          ; decided on, so a hostile 0 there
    mov ax, [es:si+WC_TOTAL]            ; would make that check trivial
    cmp ax, bp
    jb .no
.totok:
    mov al, [es:si+WC_NSIDE]            ; the sidecar span must be in the table
    xor ah, ah
    add ax, [es:si+WC_SIDE0]
    jc .no
    cmp ax, [es:WC_SCN]
    ja .no
.nextrec:
    inc bx
    loop .rec
    clc
    jmp short .out
.no:
    mov word [wr_n], 0
    stc
.out:
    pop es                              ; ONE epilogue: `pop` leaves the flags,
    pop bp                              ; so the answer set above survives it
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.arcrec:
    ; --- AN ARCHIVE'S RECORD (SPEC.md 88.2, 88.13) --------------------------
    ; **NONE OF THE THREE RULES ABOVE IS TRUE OF ONE**, and the site's real
    ; catalog is what said so: RunCPM's WC_SIZE is 231,463, which the 16-bit
    ; bound refused outright and took the whole catalog down with it.
    ;
    ;   WC_SIZE is the bytes the TRANSFER carries, and there is never a claim
    ;   for the whole of it - one entry at a time into a claim sized from
    ;   WA_MAXENT - so the bound is WIRE_ARCMAX, a sanity figure, and it is a
    ;   dword.
    ;   WC_TOTAL is what the tree UNPACKS to and may be SMALLER than the
    ;   stream: a tree of incompressible files is stored, and the container
    ;   adds 32 bytes of header plus 64 an entry on top of it. So it is
    ;   checked for being there and not against WC_SIZE.
    ;   n is 0 on every archive record (88.2), so there is no sidecar span to
    ;   be in range and WC_SIDE0 is meaningless.
    cmp byte [es:si+WC_NSIDE], 0
    jne .no
    mov ax, [es:si+WC_SIZE+2]
    cmp ax, WIRE_ARCMAX >> 16
    jae .no                             ; ...and WIRE_ARCMAX is the first size
    or ax, [es:si+WC_SIZE]              ; refused, so one compare does it
    jz .no                              ; a dword zero is no file at all
    mov ax, [es:si+WC_TOTAL]
    or ax, [es:si+WC_TOTAL+2]
    jz .no                              ; ...and neither is a tree of nothing
    jmp short .nextrec

; --- wr_side - ES:SI = sidecar entry AX ---------------------------------------
wr_side:
    push ax
    push cx
    push dx
    mov es, [wr_catseg]
    mov cx, WIRE_SC
    mul cx                              ; AX = index * 16; the table is at most
    add ax, [es:WC_SCOFF]               ; WIRE_CATMAX along, so DX is 0 by
    mov si, ax                          ; wr_catck's own bound
    pop dx
    pop cx
    pop ax
    ret

; =============================================================================
; THE PREDICATE - one routine, three consumers (SPEC.md 47, 88.7)
; in:  AL = 0 "may Load?" / 1 "may Add?"
; out: CF = 0 allowed; CF = 1 refused and SI = the reason, a NUL string
;      Every other register preserved.
;
; The painter greys on it, the click refuses on it and says the reason in the
; status cell, and wr_menufix turns it into the File menu's MENU_DIS prefixes.
; Three readers and one answer is the whole of rule 1: a control that greys
; itself and a click that refuses cannot disagree.
; =============================================================================
wr_may:
    push ax
    push bx
    push si                             ; SI is an OUTPUT on the refusal and
    push es                             ; must be untouched on the other arm
    mov bl, al                          ; **BL, NOT AH.** The question has to
                                        ; survive `mov ax, [wr_sel]` below, and
                                        ; in AH it did not: every Add to Disk
                                        ; came back refused with Load
                                        ; Program's reason, which greys the
                                        ; one button a WF_DISK record exists
                                        ; to be used with. tests/thewire.py
                                        ; read wr_grey = 3 and found it
    mov al, [wr_state]
    cmp al, WS_OPEN
    jb .notbusy
    cmp al, WS_DONE
    jb .busy
.notbusy:
    cmp byte [wr_job], WJ_NONE
    jne .busy                           ; a chain between transfers is busy too
    cmp word [wr_n], 0
    je .nocat
    cmp word [wr_sel], 0xFFFF
    je .nosel
    call wr_selrec
    mov al, [es:si+WC_FLAGS]
    test al, WF_ARC                     ; **AN ARCHIVE IGNORES WF_FLOPPY**, and
    jnz .arc                            ; the writer sets that bit WITH WF_ARC
                                        ; on purpose (SPEC.md 88.2): a Wire
                                        ; from before 88.13 does not know bit 4
                                        ; and greys both buttons with the
                                        ; floppy reason rather than fetching a
                                        ; .O88 that is not there. This reader
                                        ; knows bit 4, so it tests it first
    test al, WF_FLOPPY
    jnz .flop
    or bl, bl
    jnz .yes                            ; Add works for a WF_DISK record: that
    test al, WF_DISK                    ; is what Add is FOR
    jnz .disk
    jmp short .yes
.arc:
    or bl, bl
    jnz .yes                            ; Add to Disk unpacks wherever the
                                        ; dialog points, so an archive is
                                        ; always addable (88.8)
    xor al, al                          ; Load Program: the RAM disk decision,
    call wr_ramck                       ; ASKED and not acted on - this runs
    jc .no                              ; from the painter (88.14 steps 1..3,
                                        ; and SI is the reason)
.yes:
    pop es
    pop si
    pop bx
    pop ax
    clc
    ret
.busy:
    mov si, wr_r_busy
    jmp short .no
.nocat:
    mov si, wr_r_nocat
    jmp short .no
.nosel:
    mov si, wr_r_nosel
    jmp short .no
.flop:
    mov si, wr_r_flop
    jmp short .no
.disk:
    mov si, wr_r_disk
.no:
    pop es
    add sp, 2                           ; ...and DISCARDED on this one: SI is
    pop bx                              ; the reason we are answering with
    pop ax
    stc
    ret

; --- wr_menufix - the File menu's two items, from the same predicate ---------
; MENU_DIS is a PREFIX BYTE on the item string (SPEC.md 12.2), so switching is
; pointing the item at the other spelling. Called from every repaint, which is
; every state change, so the pull-down is never stale.
wr_menufix:
    push ax
    push si
    xor al, al
    call wr_may
    mov word [wr_i_file + 2], wr_it_run
    jnc .a
    mov word [wr_i_file + 2], wr_it_run0
.a:
    mov al, 1
    call wr_may
    mov word [wr_i_file + 4], wr_it_add
    jnc .b
    mov word [wr_i_file + 4], wr_it_add0
.b:
    pop si
    pop ax
    ret

; =============================================================================
; INPUT
; =============================================================================

; -----------------------------------------------------------------------------
; wr_onclick - W_ONCLICK: CX = x, DX = y (absolute), SI = window; lock held
; -----------------------------------------------------------------------------
wr_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_abdismiss                   ; a card the user cannot click away is
    jc .out                             ; not a card
    call wr_geom
    jc .out
    sub cx, [wr_ox]                     ; --- content-relative from here
    sub dx, [wr_oy]

    cmp dx, WR_FY                       ; --- the filter row
    jb .out
    cmp dx, WR_FY + OS88UI_GW - 1
    ja .list
    call wr_filhit
    jmp short .out

.list:
    cmp cx, WR_SBX1                     ; --- the scroll bar
    jb .rows
    cmp cx, WR_SBX2
    ja .det
    add cx, [wr_ox]
    add dx, [wr_oy]
    call wr_sbclick
    jmp short .out

.rows:
    cmp cx, 1
    jb .out
    cmp dx, WR_PY + 1
    jb .out
    mov ax, dx
    sub ax, WR_PY + 1
    mov cl, 4
    shr ax, cl                          ; / WR_ROWH
    cmp ax, [wr_rows]
    jae .out
    call wr_rowclick
    jmp short .out

.det:
    cmp cx, WR_DX1
    jb .out
    add cx, [wr_ox]                     ; the buttons want absolute again
    add dx, [wr_oy]
    mov bx, wr_ra
    call os88ui_bhit
    jnc .runbtn
    mov bx, wr_rb
    call os88ui_bhit
    jnc .addbtn
    jmp short .out
.runbtn:
    xor al, al
    call wr_do
    jmp short .out
.addbtn:
    mov al, 1
    call wr_do
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_filhit - a click in the filter row, CX = content-relative x ----------
wr_filhit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di
.l:
    mov bx, di
    shl bx, 1
    mov ax, [wr_filx + bx]
    cmp cx, ax
    jb .next
    mov bx, di
    mov bl, [wr_filwid + bx]
    xor bh, bh
    add ax, bx
    cmp cx, ax
    jae .next
    mov al, [wr_filter]                 ; --- this one. Is it already set?
    xor ah, ah
    cmp ax, di
    je .out                             ; nothing moved, so nothing is redrawn
    mov dx, di                          ; DX = the new one, AX = the old
    mov [wr_filter], dl                 ; **THE BYTE MOVES FIRST**: wr_dglyph
                                        ; decides lit-or-not by comparing DI
                                        ; against it, so redrawing the old one
                                        ; before the store draws it STILL LIT
                                        ; and leaves two rings filled until the
                                        ; next full repaint
    mov di, ax
    call wr_dglyph                      ; ...and ONLY THE TWO THAT CHANGED:
    mov di, dx                          ; os88ui_glyph is 35-50 ms on the field
    call wr_dglyph                      ; machine, so five of them is a quarter
    call wr_refilter                    ; of a second for a change of one
    jmp short .out
.next:
    inc di
    cmp di, 5
    jb .l
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_refilter - the filter moved: the view, and a selection it may hide ---
; A SELECTION THE NEW FILTER DOES NOT SHOW IS DROPPED, and it has to be: every
; consumer of [wr_sel] - the detail pane, the predicate, both actions - would
; otherwise be about a record that is not on the list, which is a Load Program
; that runs a program the user cannot see.
wr_refilter:
    push ax
    mov word [wr_top], 0
    cmp word [wr_sel], 0xFFFF
    je .draw
    mov ax, [wr_sel]
    call wr_vispos
    jnc .draw
    mov word [wr_sel], 0xFFFF
    mov byte [wr_picok], 0
.draw:
    call wr_dall
    pop ax
    ret

; --- wr_rowclick - AX = the row on screen ------------------------------------
wr_rowclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    add ax, [wr_top]
    call wr_nth
    jc .out                             ; an empty row: the selection stands
    cmp ax, [wr_sel]
    je .out
    mov [wr_sel], ax
    mov di, [wr_selrow]                 ; ...the OLD row, if it is on screen
    cmp di, 0xFFFF
    je .new
    call wr_drow
.new:
    call wr_vispos
    jc .rest
    sub ax, [wr_top]
    mov di, ax
    call wr_drow
.rest:
    call wr_selchg
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_selchg - the selection moved: the detail pane, the status, a picture -
wr_selchg:
    push ax
    call wr_menufix
    mov byte [wr_picok], 0
    call wr_ddet
    call wr_dstat
    call wr_picwant
    pop ax
    ret

; --- wr_sbclick - CX/DX absolute, inside the bar -----------------------------
wr_sbclick:
    push ax
    push bx
    push cx
    push dx
    call wr_dscroll                     ; the block is what sbhit reads
    mov bx, wr_sb
    call os88ui_sbhit
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .down
    cmp al, OS88UI_SBPGUP
    je .pgup
    cmp al, OS88UI_SBPGDN
    je .pgdn
    cmp al, OS88UI_SBTHUMB
    jne .out
    mov bx, wr_sb
    mov al, 2                           ; the view follows every 2 ticks: a
    call os88ui_sbgrab                  ; 12-row repaint is 36 drawing calls
    jmp short .out                      ; and the field machine cannot do that
.up:                                    ; per mouse report (SPEC.md 13.10.5.4)
    mov ax, -1
    jmp short .by
.down:
    mov ax, 1
    jmp short .by
.pgup:
    mov ax, [wr_rows]
    neg ax
    jmp short .by
.pgdn:
    mov ax, [wr_rows]
.by:
    call wr_scroll
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_scroll - move the view by AX rows, clamped, and repaint if it moved --
wr_scroll:
    push ax
    push bx
    push cx
    push dx
    add ax, [wr_top]
    jns .lo
    xor ax, ax
.lo:
    mov bx, ax
    call wr_count
    sub ax, [wr_rows]
    jns .hi
    xor ax, ax
.hi:
    cmp bx, ax
    jbe .set
    mov bx, ax
.set:
    cmp bx, [wr_top]
    je .out
    mov [wr_top], bx
    call wr_dlist
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_ondrag / wr_onup - the thumb (SPEC.md 13.10.5) -----------------------
wr_ondrag:
    push ax
    push bx
    push cx
    push dx
    call wr_geom
    jc .out
    call wr_dscroll
    mov bx, wr_sb
    call os88ui_sbtrack
    jc .out
    cmp ax, [wr_top]
    je .out
    mov [wr_top], ax
    call wr_dlist
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

wr_onup:
    push ax
    push bx
    push cx
    push dx
    call wr_geom
    jc .out
    call wr_dscroll
    mov bx, wr_sb
    call os88ui_sbdrop
    jc .out
    cmp ax, [wr_top]
    je .draw
    mov [wr_top], ax
.draw:
    call wr_dlist
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_onkey - W_ONKEY: AL = ascii, AH = scan, SI = window; lock held
; -----------------------------------------------------------------------------
wr_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call wr_abdismiss
    jc .out
    call wr_geom
    jc .out
    cmp ah, KSC_UP
    je .up
    cmp ah, KSC_DOWN
    je .down
    cmp ah, 0x49                        ; PgUp
    je .pgup
    cmp ah, 0x51                        ; PgDn
    je .pgdn
    cmp ah, 0x47                        ; Home
    je .home
    cmp ah, 0x4F                        ; End
    je .end
    cmp ah, KSC_ENTER
    je .run
    jmp short .out
.up:
    mov ax, -1
    jmp short .move
.down:
    mov ax, 1
    jmp short .move
.pgup:
    mov ax, [wr_rows]
    neg ax
    jmp short .move
.pgdn:
    mov ax, [wr_rows]
    jmp short .move
.home:
    mov ax, -32000
    jmp short .move
.end:
    mov ax, 32000
.move:
    call wr_selby
    jmp short .out
.run:
    xor al, al
    call wr_do
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_selby - move the selection AX visible rows, scrolling to keep it -----
wr_selby:
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, ax
    call wr_count
    or ax, ax
    jz .out
    mov cx, ax                          ; CX = how many are visible in all
    mov ax, [wr_sel]
    cmp ax, 0xFFFF
    je .first
    call wr_vispos
    jnc .have
.first:
    xor ax, ax                          ; no selection, or one the filter now
    xor bx, bx                          ; hides: the first row
.have:
    add ax, bx
    jns .lo
    xor ax, ax
.lo:
    cmp ax, cx
    jb .hi
    mov ax, cx
    dec ax
.hi:
    push ax                             ; --- scroll it into view first
    sub ax, [wr_top]
    js .sup
    cmp ax, [wr_rows]
    jb .sok
    sub ax, [wr_rows]
    inc ax
    jmp short .sby
.sup:
    ; AX is already the negative distance
.sby:
    call wr_scroll
.sok:
    pop ax
    call wr_nth                         ; ...then take the record
    jc .out
    mov [wr_sel], ax
    call wr_dlist                       ; the view may have moved under it, so
    call wr_selchg                      ; this is the whole pane and not two
.out:                                   ; rows - a key repeat is the one path
    pop di                              ; that scrolls AND selects at once
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE MENU AND THE ABOUT CARD
; =============================================================================
; --- wr_oncmd - AM_ONCMD: AL = item, AH = menu, SI = window; lock held -------
wr_oncmd:
    push bx
    push cx
    push dx
    push si
    push di
    call wr_abdismiss
    call wr_geom
    jc .out
    cmp al, 0
    je .refresh
    cmp al, 1
    je .run
    cmp al, 2
    je .add
    cmp al, 3
    je .close
    jmp short .out
.refresh:
    call wr_refresh
    jmp short .out
.run:
    xor al, al
    call wr_do
    jmp short .out
.add:
    mov al, 1
    call wr_do
    jmp short .out
.close:
    mov bx, [wr_win]
    call OSAPI_WM_CLOSE
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wr_onclose - the close negotiator (SPEC.md 75.1)
; in:  SI = our window; the UI task, the gfx lock HELD
; out: CF = 0 always - this window never refuses a close
;
; IT EXISTS TO GIVE THE SOCKET BACK. The worker parks in OSAPI_TASK_ALIVE and
; never returns from it once the close is taken, so a transfer in flight would
; leave a handle held for the rest of the session - and NET_SOCKS is four
; (netpkg.inc), so four closed Wires are a machine that cannot open a
; connection at all. Nothing in the driver reaps by owner, so the owner has to.
;
; The generation is bumped as well as the handle closed, which costs nothing
; and means that if the worker is somehow inside a pass when this runs, that
; pass stores nothing.
; -----------------------------------------------------------------------------
wr_onclose:
    push ax
    inc byte [wr_gen]
    mov byte [wr_state], WS_IDLE
    call wr_hclose
    pop ax
    clc
    ret

; --- wr_about - the OSAPI_ABOUT_SET handler (SPEC.md 12.2, 20.5.1) -----------
wr_about:
    push bx
    push si
    mov byte [wr_abon], 1
    mov bx, si
    mov si, wr_ablines
    call os88ui_about                   ; arms the clip itself: a menu dispatch
    pop si                              ; arrives without one (SPEC.md 11.3)
    pop bx
    ret

; --- wr_abdismiss - take the card down if it is up ---------------------------
; out: CF = 1 the click or key was spent doing it
wr_abdismiss:
    cmp byte [wr_abon], 0
    je .none
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [wr_abon], 0
    mov bx, [wr_win]
    call OSAPI_WM_CLIP_SET              ; nothing has armed a region for a
    jc .gone                            ; click or a menu pick (SPEC.md 11.3)
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call wr_geom
    jc .gone
    mov ax, [wr_ox]
    mov bx, [wr_oy]
    mov cx, ax
    add cx, [wr_cw]
    dec cx
    mov dx, bx
    add dx, [wr_ch]
    dec dx
    call OSAPI_GFX_FILL
    mov si, [wr_win]
    call wr_paint
.gone:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.none:
    clc
    ret

; =============================================================================
; THE TWO ACTIONS (SPEC.md 88.8)
; =============================================================================

; -----------------------------------------------------------------------------
; wr_do - AL = 0 Load Program / 1 Add to Disk...
; in:  the UI task, the gfx lock HELD (a click, a key or a menu pick)
;
; THE PREDICATE ANSWERS FIRST AND ITS REASON GOES IN THE STATUS CELL. A greyed
; button that does nothing at all when clicked is SPEC.md 47's failure mode:
; the grey says THAT it will refuse and the cell is the only place that can
; say WHY.
; -----------------------------------------------------------------------------
wr_do:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ax
    call wr_may
    pop ax
    jnc .go
.say:
    mov [wr_msg], si
    call wr_dstat
    jmp .out
.go:
    mov byte [wr_ramjob], 0
    or al, al
    jnz .add
    push ax
    call wr_selrec
    test byte [es:si+WC_FLAGS], WF_ARC
    pop ax
    jnz .arc
    mov byte [wr_job], WJ_LOAD          ; --- Load Program: one file, and the
    mov word [wr_chain], 0              ; wake runs it
    call wr_fetchnext
    jmp short .out
.arc:
    mov al, 1                           ; --- Load Program on an ARCHIVE: the
    call wr_ramck                       ; SECOND ask, and the one that MOUNTS
    jc .say                             ; (SPEC.md 88.14). The predicate has
                                        ; already answered, so a refusal here
                                        ; is a store that went away between
                                        ; the paint and the click
    xor dx, dx                          ; ...and the store's ROOT, which the
    call OSAPI_FILE_GOTO_QM             ; instance MOVES to: OSAPI_PKG_RUN's
    jc .nordo                           ; new instance inherits our directory
                                        ; (SPEC.md 19.2.1), and the whole point
                                        ; of unpacking a tree is that the
                                        ; program's own files are beside it
    mov byte [wr_ramjob], 1
    mov byte [wr_job], WJ_LOAD
    call wr_arcstart
    jmp short .out
.nordo:
    mov si, wr_r_nord                   ; a store that is mounted and cannot be
    jmp short .say                      ; stood on is a driver that is not
                                        ; usable, which is what that says
.add:
    mov di, wr_savename                 ; --- Add to Disk...: the dialog first
    mov si, wr_x_o88
    call wr_stemput
    mov al, FDLG_SAVE
    mov bx, [wr_win]
    mov di, wr_saved
    mov si, wr_savename
    call OSAPI_FILE_DLG                 ; SPEC.md 38.6: it does NOT block - the
    jnc .out                            ; dialog is up when this returns and
    mov word [wr_msg], wr_r_busy        ; wr_saved is called much later. CF = 1
    call wr_dstat                       ; is one already up, or no room, and a
                                        ; click that does nothing at all and
                                        ; says nothing is the failure SPEC.md
                                        ; 47 is about
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_stemput - '<STEM><ext>' into DS:DI, for the selected record ----------
; in:  DI = the buffer, SI = a NUL extension (wr_x_o88, wr_x_wpk); DI advanced
;
; Three call sites want the same eight-and-three: the chain's first file, the
; Save dialog's default name and an archive's own .WPK. They had it three
; times over and one of the three banked a register the other two did not.
wr_stemput:
    push ax
    push cx
    push si
    push es
    push si                             ; the extension, banked past wr_recs
    call wr_selrec                  ; ES:SI = the record
    add si, WC_STEM
    mov cx, 8
    call wr_stemcopy
    pop si
    call wr_sput
    mov byte [di], 0
    pop es
    pop si
    pop cx
    pop ax
    ret

; --- wr_stemcopy - CX bytes of the space-padded stem at ES:SI to DS:DI -------
; A stem is 8 bytes padded with SPACES, and a space is where the name ends.
wr_stemcopy:
    push ax
.c:
    mov al, [es:si]
    cmp al, ' '
    jbe .out
    cmp al, 0x7F
    jae .out
    mov [di], al
    inc di
    inc si
    loop .c
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_saved - the file dialog's completion (SPEC.md 38.6)
; in:  AL = the mode, SI = our window, DI = the chosen name, DX:CX = its size
;      the UI task, the gfx lock HELD
;
; THE FREE-SPACE CHECK IS HERE AND ONLY HERE, and it is asked ONCE. This is
; the first moment a volume and a folder exist to ask about - the dialog has
; navigated to them - and SPEC.md 77.40 is what asking per file would cost:
; OSAPI_FILE_DFREE walks the whole resident FAT, about 105 ms on a 20MB disk,
; and apps/ftpd spent 44% of an upload in it.
; -----------------------------------------------------------------------------
wr_saved:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, di                          ; **THE NAME IS IN THE KERNEL'S
    mov di, wr_savename                 ; SEGMENT**, which is where ES still
    mov cx, 13                          ; points on the way in (SPEC.md 20.1),
.c:                                     ; and the buffer is valid for THIS CALL
    mov al, [es:si]                     ; only - so it is copied here, through
    mov [di], al                        ; an override, BEFORE ES is turned
    inc si                              ; round below. Without the override
    inc di                              ; this reads our own image at that
    or al, al                           ; offset: it assembles, it runs, and
    jz .named                           ; what reaches OSAPI_FILE_WRITE is four
    loop .c                             ; bytes of garbage under a status cell
    mov byte [di-1], 0                  ; reading `Could not write ?)33`
.named:
    push ds
    pop es
    call wr_geom
    jc .out
    call wr_selrec
    mov bx, [es:si+WC_TOTAL]            ; what the whole set will take, BANKED:
    mov [wr_need], bx                   ; **OSAPI_FILE_DFREE WRITES BX** (it is
    mov bx, [es:si+WC_TOTAL+2]          ; the sectors-per-cluster output), so a
    mov [wr_need+2], bx                 ; total held there is compared against
    push ds                             ; 1, 2, 4 or 8 and the check never
    pop es                              ; fires - which is exactly the
    call OSAPI_FILE_DFREE               ; nearly-full disk it exists for
    jc .noroom                          ; DX:AX = free bytes
    cmp dx, [wr_need+2]
    ja .room
    jb .noroom
    cmp ax, [wr_need]
    jb .noroom
.room:
    mov byte [wr_job], WJ_ADD
    mov byte [wr_ramjob], 0
    call wr_selrec
    test byte [es:si+WC_FLAGS], WF_ARC
    jz .chain
    call wr_arcstart                    ; --- an ARCHIVE unpacks where the
    jmp short .out                      ; dialog pointed, and THE TYPED NAME IS
                                        ; IGNORED (88.8): the archive names its
                                        ; own files, so there is one name the
                                        ; user could have typed and fifty-nine
                                        ; the machine is about to write
.chain:
    mov word [wr_chain], 0
    call wr_fetchnext
    jmp short .out
.noroom:
    mov word [wr_msg], wr_r_space
    call wr_dstat
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
; wr_fetchnext - claim and start the chain's [wr_chain]th file
; in:  [wr_job], [wr_chain]; the UI task
; out: nothing; a refusal is said in the status cell
;
; Chain item 0 is the .O88 itself; 1..n are the record's sidecars, in table
; order. The whole of "Add to Disk" is this routine called again from the wake
; handler with [wr_chain] one higher.
; -----------------------------------------------------------------------------
wr_fetchnext:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call wr_selrec
    mov ax, [wr_chain]
    or ax, ax
    jnz .side
    mov cx, [es:si+WC_SIZE]             ; --- the package itself
    mov [wr_flen], cx
    mov di, wr_fname
    mov si, wr_x_o88
    call wr_stemput
    jmp short .claim
.side:
    dec ax
    mov bl, [es:si+WC_NSIDE]
    xor bh, bh
    cmp ax, bx
    jae .done                           ; the chain is finished
    add ax, [es:si+WC_SIDE0]
    call wr_side                        ; ES:SI = the entry
    mov cx, [es:si+WC_SCSIZE]
    mov [wr_flen], cx
    mov dx, [es:si+WC_SCSIZE+2]
    or dx, dx
    jnz .toobig
    or cx, cx
    jz .toobig
    cmp cx, WIRE_FILEMAX
    ja .toobig
    mov di, wr_fname
    mov cx, 12
    call wr_sputn                       ; the 8.3 name, hostile-safe
    mov byte [di], 0
.claim:
    mov ax, [wr_flen]                   ; --- one claim, the EXACT size
    add ax, 1023
    mov cl, 10
    shr ax, cl
    or ax, ax
    jnz .kok
    inc ax
.kok:
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [wr_fseg], dx
    mov ax, [wr_fseg]                   ; the transfer's destination
    mov bx, 0
    mov cx, [wr_flen]
    mov dx, WK_FILE
    call wr_start
    cmp byte [wr_job], WJ_ADD           ; ...and the chain says WHERE IT IS,
    jne .said                           ; composed here rather than after the
    call wr_selrec                  ; write: wr_start has just cleared
    mov bl, [es:si+WC_NSIDE]
    xor bh, bh
    inc bx                              ; the .O88 and its sidecars
    mov [wr_ptot], bx
    mov ax, [wr_chain]
    inc ax                              ; [wr_chain] is 0-based and a person
    mov [wr_pnum], ax                   ; counts from one
    call wr_addprog
.said:                                  ; [wr_msg], and a line written after
    call wr_dstat                       ; the last file is a line nothing draws
    jmp short .out
.done:
    call wr_chaindone
    jmp short .out
.nomem:
    mov word [wr_msg], wr_s_nomem
    call wr_abandon
    jmp short .out
.toobig:
    mov word [wr_msg], wr_s_toobig
    call wr_abandon
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_abandon - the chain is over and it did not work ----------------------
; A FAILURE MID-CHAIN LEAVES WHAT WAS WRITTEN (SPEC.md 22): there is no undo
; on this system, and deleting a file the user can see would be a worse
; surprise than a folder with two of three files in it. The cell says which.
wr_abandon:
    push ax
    cmp byte [wr_state], WS_PAUSE       ; **A PAUSED WORKER IS STILL IN FLIGHT**
    jne .st                             ; and the predicate reads that as a
    inc byte [wr_gen]                   ; range: an archive abandoned at an
    call wr_hclose                      ; entry boundary would otherwise leave
    mov byte [wr_state], WS_FAIL        ; both buttons greyed with `A transfer
.st:                                    ; is already running` for the life of
                                        ; the window, and the socket held with
                                        ; them
    mov byte [wr_job], WJ_NONE
    call wr_freefile
    call wr_dend
    pop ax
    ret

; --- wr_freefile - give the transfer claim back ------------------------------
wr_freefile:
    push ax
    push dx
    cmp word [wr_fseg], 0
    je .out
    mov dx, [wr_fseg]
    call OSAPI_MEM_FREE
    mov word [wr_fseg], 0
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_chaindone - every file in the set has landed
; -----------------------------------------------------------------------------
wr_chaindone:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [wr_job], WJ_NONE
    call wr_freefile
    mov di, wr_omsg
    call wr_selrec
    push si
    add si, WC_TITLE
    mov cx, 23
    call wr_sputn
    pop si
    push ds
    pop es
    mov si, wr_s_added
    cmp byte [wr_ramjob], 0             ; ...and WHERE it landed, which for an
    je .say                             ; archive run from RAM is not a disk
    mov si, wr_s_addram                 ; the user can put in a drawer (88.8)
.say:
    call wr_sput
    mov byte [di], 0
    mov si, wr_omsg
    xor cx, cx
    call OSAPI_TOAST                    ; ES:SI, and ES is ours (SPEC.md 59)
    mov word [wr_msg], wr_omsg
    call wr_dend
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_refresh - re-fetch the catalog (File > Refresh)
; -----------------------------------------------------------------------------
wr_refresh:
    push ax
    push bx
    push cx
    push dx
    mov byte [wr_job], WJ_NONE
    call wr_freefile
    mov word [wr_n], 0
    mov word [wr_sel], 0xFFFF
    mov word [wr_top], 0
    mov byte [wr_picok], 0
    call wr_netcheck
    jc .say
    cmp word [wr_catseg], 0
    jne .go
    mov ax, WIRE_CATMAX / 1024
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [wr_catseg], dx
.go:
    mov ax, [wr_catseg]
    xor bx, bx
    mov cx, WIRE_CATMAX
    mov dx, WK_CAT
    call wr_start
    jmp short .say
.nomem:
    mov word [wr_msg], wr_s_nomem
.say:
    call wr_dall
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_netcheck - is there a stack at all? ----------------------------------
; out: CF = 1 no, [wr_nodrv] set and [wr_msg] said
; net_find is worker-safe AND UI-safe, so asking here costs the worker nothing
; and lets the window open already saying what is wrong.
wr_netcheck:
    push ax
    push bx
    mov byte [wr_nodrv], 0
    call net_find
    jc .no
    mov bh, NET_CLASS
    mov bl, NETV_STATE
    call OSAPI_DRV_CALL
    jc .no
    test al, NSTF_SOCK
    jz .no
    mov word [wr_msg], 0
    pop bx
    pop ax
    clc
    ret
.no:
    mov byte [wr_nodrv], 1
    mov word [wr_msg], wr_s_nodrv
    pop bx
    pop ax
    stc
    ret

; --- wr_picwant - fetch the selected record's picture, if the worker is free -
wr_picwant:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    cmp byte [wr_nodrv], 0
    jne .out
    cmp byte [wr_job], WJ_NONE
    jne .out                            ; a chain owns the socket
    mov al, [wr_state]
    cmp al, WS_OPEN
    jb .free
    cmp al, WS_DONE
    jb .out                             ; in flight: the picture can wait, and
.free:                                  ; a selection change bumps the
    cmp word [wr_sel], 0xFFFF           ; generation anyway
    je .out
    call wr_selrec
    test byte [es:si+WC_FLAGS], WF_PIC
    jz .out
    push ds
    pop es
    mov ax, ds                          ; the destination is OUR OWN bss
    mov bx, wr_pic
    mov cx, WIRE_PICSZ
    mov dx, WK_PIC
    call wr_start
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE WAKE HANDLER - the only place a claim is freed, a file written or a
; program run (SPEC.md 88.5)
; in:  SI = our window, the UI task, NO GFX LOCK
; =============================================================================
wr_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp byte [wr_ready], 0
    jne .wake
    mov byte [wr_ready], 1              ; --- the first kick: this is the boot
    call wr_cfgload                     ; (a file read, which is why it is not
    call OSAPI_GFX_LOCK                 ; in the entry proc)
    call wr_geom
    call wr_refresh
    call OSAPI_GFX_UNLOCK
    jmp .out
.wake:
    mov al, [wr_wake]
    or al, al
    jz .out
    mov [wr_wake0], al                  ; BANK IT, then CLEAR, then act: acting
    mov byte [wr_wake], WW_NONE         ; is what starts the NEXT transfer, and
                                        ; a flag cleared after that would clear
                                        ; the new one's wake (SPEC.md 88.5)
    call OSAPI_GFX_LOCK
    call wr_geom
    call OSAPI_GFX_UNLOCK
    ; EVERY HOLD BELOW ARMS THE REGION FIRST (wr_clip): this handler runs
    ; from the UI task's wake, not from W_PAINT, and the window may be
    ; behind the one Load Program has just opened (SPEC.md 11.3)
    mov al, [wr_wake0]
    cmp al, WW_HDR                      ; --- the archive's two PAUSES, where
    je .ahdr                            ; the worker has stopped and the next
    cmp al, WW_ENTRY                    ; thing to do is a claim or a file, and
    je .aent                            ; a worker may do neither (88.14)
    cmp al, WW_DONE
    jne .failed
    mov al, [wr_wkind]
    cmp al, WK_CAT
    je .cat
    cmp al, WK_PIC
    je .pic
    jmp .file
.failed:
    call OSAPI_GFX_LOCK
    call wr_clip
    cmp byte [wr_wkind], WK_PIC
    jne .fhard
    mov byte [wr_picok], 0              ; a picture that did not arrive is not
    call wr_ddet                        ; a failure of anything the user asked
    call OSAPI_GFX_UNLOCK               ; for: the pane says 'No picture'
    jmp .out
.fhard:
    call wr_abandon
    call OSAPI_GFX_UNLOCK
    jmp .out

.cat:
    call wr_catck
    jnc .catok
    mov word [wr_msg], wr_s_badcat
.catok:
    call wr_lockarm
    mov word [wr_top], 0
    call wr_dall
    call OSAPI_GFX_UNLOCK
    jmp .out

.pic:
    mov byte [wr_picok], 1
    call wr_lockarm
    call wr_ddet
    call OSAPI_GFX_UNLOCK
    jmp .out

.ahdr:
    call wr_arcbase                     ; --- the ONE claim, sized from the
    jc .wfail                           ; header, and `home` made and entered
    call OSAPI_GFX_LOCK                 ; (88.14). NO LOCK for either: a claim
    call wr_clip                        ; and a folder are not drawing
    call wr_geom
    call wr_dstat
    call OSAPI_GFX_UNLOCK
    jmp .out

.aent:
    call wr_arcwrite                    ; --- one entry: its folders, then the
    jc .wfail                           ; file, and NO LOCK for the same reason
    mov ax, [wr_adone]
    cmp ax, [wr_an]
    jb .amore
    call wr_arcend                      ; the last one: the launch or the toast
    jmp .out
.amore:
    mov ax, [wr_adone]
    inc ax
    mov [wr_pnum], ax
    mov ax, [wr_an]
    mov [wr_ptot], ax
    mov byte [wr_state], WS_BODY        ; **AND ONLY NOW MAY THE WORKER RUN**:
                                        ; it overwrites [wr_ehdr] with the next
                                        ; entry's header the moment it does,
                                        ; and wr_arcwrite has just been reading
                                        ; it
    call wr_lockarm
    call wr_addprog
    call wr_dstat
    call OSAPI_GFX_UNLOCK
    jmp .out

.file:
    cmp byte [wr_wkind], WK_ARC         ; A WW_DONE ON AN ARCHIVE is the stream
    jne .fnorm                          ; meeting its Content-Length with fewer
    mov word [wr_msg], wr_s_lost        ; than N entries decoded (88.14), which
    jmp short .wfail                    ; is a short answer and not a small one
.fnorm:
    cmp byte [wr_job], WJ_LOAD
    je .run
    call wr_write                       ; --- Add: write it, then the next
    jc .wfail
    call wr_freefile
    inc word [wr_chain]
    call wr_lockarm
    call wr_fetchnext
    call OSAPI_GFX_UNLOCK
    jmp short .out
.wfail:
    call wr_lockarm
    call wr_abandon
    call OSAPI_GFX_UNLOCK
    jmp short .out
.run:
    mov ax, [wr_got]                    ; what ARRIVED - wr_write's reason
    mov [wr_rlen], ax
    call wr_pkgrun                      ; --- Load: hand the image to the
    call OSAPI_GFX_LOCK                 ; loader's back half
    call wr_clip                        ; ...which has just put ITS window on
    call wr_geom                        ; top of ours
    mov byte [wr_job], WJ_NONE
    call wr_freefile
    call wr_dend
    call OSAPI_GFX_UNLOCK
.out:
    mov byte [wr_hid], 0                ; the flag is this handler's alone
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_write - the claim to [wr_fname] (or the chosen name, for item 0)
; out: CF = 1 and [wr_msg] said
; -----------------------------------------------------------------------------
wr_write:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, wr_fname
    cmp word [wr_chain], 0
    jne .name
    mov si, wr_savename                 ; ITEM 0 TAKES THE USER'S NAME. The
                                        ; sidecars cannot: an overlay is found
                                        ; by the name its package looks for
.name:
    mov [wr_wname], si                  ; BANKED: on a DVK_FILE volume CX, SI
                                        ; and DI are the DRIVER's across this
                                        ; call (SPEC.md 62.9), and the failure
                                        ; path below wants the name back
    mov es, [wr_fseg]
    xor bx, bx
    mov cx, [wr_got]                    ; **WHAT ARRIVED, not what was
                                        ; claimed**: wr_hdrdone refuses a
                                        ; Content-Length that disagrees with
                                        ; the catalog, so these are equal - and
                                        ; writing [wr_flen] would put the
                                        ; claim's uninitialised tail on the
                                        ; user's disk the day that check moves
    xor dx, dx
    call OSAPI_FILE_WRITE
    jnc .ok
    call wr_wrfail
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.ok:
    call wr_addprog
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; --- wr_wrfail - 'Could not write <name>', for the file at [wr_wname] --------
; Shared by the sidecar chain and by the archive's tree (wrarc.inc), because a
; failure mid-set and a failure mid-tree are the same sentence about the same
; kind of thing: SPEC.md 22 leaves what was written and the cell says which
; file, which is the only part of it a person can act on.
wr_wrfail:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, wr_omsg
    mov si, wr_s_wrote
    call wr_sput
    mov si, [wr_wname]
    mov cx, 12
    call wr_sputn
    mov byte [di], 0
    mov word [wr_msg], wr_omsg
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- wr_addprog - 'Adding <title>... N of M files' ---------------------------
; **THE TWO NUMBERS ARE PASSED IN [wr_pnum] AND [wr_ptot]** rather than derived
; here, because there are two chains now and they count different things: the
; Add chain counts the .O88 and its sidecars (WC_NSIDE + 1) and an archive
; counts the entries in its header (SPEC.md 88.14). One composer, two callers,
; and the sentence a person reads is the same either way.
wr_addprog:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, wr_omsg
    mov si, wr_s_adding
    call wr_sput
    call wr_selrec
    add si, WC_TITLE
    mov cx, 18
    call wr_sputn
    push ds
    pop es
    mov byte [di], ' '
    inc di
    mov ax, [wr_pnum]
    call wr_snum
    mov si, wr_s_of
    call wr_sput
    mov ax, [wr_ptot]
    call wr_snum
    mov si, wr_s_files
    cmp word [wr_ptot], 1
    jne .pl
    mov si, wr_s_file
.pl:
    call wr_sput
    mov byte [di], 0
    mov word [wr_msg], wr_omsg
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wr_pkgrun - the image in the claim, into a running instance
; in:  [wr_fseg], [wr_rlen], [wr_fname]; the UI task, NO LOCK
;
; [wr_rlen] IS THE LENGTH AND IT IS A WORD THE CALLER WRITES: for a plain Load
; Program it is [wr_got], what arrived (see wr_write's reason); for an archive
; it is the last entry's unpacked size, and the claim holding it is the same
; claim the whole tree was decoded through (SPEC.md 88.14).
;
; SPEC.md 21.x. The new instance's current directory is OURS (SPEC.md 19.2.1),
; which is why a WF_DISK record is refused by the predicate rather than
; launched into a folder where its overlay is not.
; -----------------------------------------------------------------------------
wr_pkgrun:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [wr_fseg]
    xor si, si
    mov cx, [wr_rlen]
    xor dx, dx
    mov di, wr_fname
    call OSAPI_PKG_RUN
    jc .bad
    push ds
    pop es
    mov di, wr_omsg                     ; 'Loaded <title> from the Wire'
    mov si, wr_s_loaded
    call wr_sput
    call wr_selrec
    add si, WC_TITLE
    mov cx, 23
    call wr_sputn
    push ds
    pop es
    mov si, wr_s_fromw
    call wr_sput
    mov byte [di], 0
    mov word [wr_msg], wr_omsg
    mov si, wr_omsg
    xor cx, cx
    call OSAPI_TOAST
    jmp short .out
.bad:
    push ds
    pop es
    xor ah, ah
    push ax
    mov di, wr_omsg                     ; the LD_* code in words, because
    mov si, wr_s_ldfail                 ; 'it did not start' with no number is
    call wr_sput                        ; a bug report nobody can act on
    pop ax
    call wr_snum
    mov byte [di], ')'
    inc di
    mov byte [di], 0
    mov word [wr_msg], wr_omsg
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
; WIRE.CFG (SPEC.md 19.9, 88.4)
;
; SYSTEM/APPDATA on OUR OWN volume, and it is OSAPI_FILE_GOTO_QM and not the
; quiet twin: GOTO_Q moves the GLOBAL cwd and deliberately not the INSTANCE's,
; while OSAPI_FILE_FIND and _READ both resolve in the instance's folder, so a
; quiet move is undone by the very next call. apps/ftpd carries the finding
; and SPEC.md 19.9's own prose says the opposite.
;
; A DISK WITHOUT THE FOLDER IS NOT AN ERROR: the defaults stand and nothing is
; said, which is what every shipped disk gets.
; =============================================================================
wr_cfgload:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov si, wr_def_host                 ; the defaults FIRST, so every exit
    mov di, wr_host                     ; below leaves a working configuration
    call wr_sput
    mov byte [di], 0
    mov si, wr_def_pfx
    mov di, wr_pfx
    call wr_sput
    mov byte [di], 0
    mov word [wr_port], 80

    call OSAPI_FILE_HERE                ; DX = our cwd, BL = our drive
    mov [wr_dbclus], dx
    mov [wr_dbdrv], bl
    xor dx, dx
    call OSAPI_FILE_GOTO_QM             ; the ROOT of that same volume
    jc .home
    mov si, wr_d_system
    call wr_dive
    jc .home
    mov si, wr_d_appdat
    call wr_dive
    jc .home
    mov si, wr_cfg_name
    mov bx, wr_cfgb
    mov cx, WR_HOSTMAX + WR_PFXMAX
    xor dx, dx
    call OSAPI_FILE_READ                ; DX:AX = the size
    jc .home
    or dx, dx
    jnz .home                           ; longer than the buffer: not ours
    or ax, ax
    jz .home
    mov [wr_cfgn], ax
    call wr_cfgparse
.home:
    mov dx, [wr_dbclus]
    mov bl, [wr_dbdrv]
    call OSAPI_FILE_GOTO_QM
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- wr_dive - step into the folder named at DS:SI. CF = 1 = no such folder --
wr_dive:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [wr_dvname], si             ; **BANKED, AND NOT ON THE STACK**: this
                                    ; walk now runs on a RAM DISK as well as
                                    ; on a floppy (SPEC.md 88.14), and on a
                                    ; DVK_FILE volume CX, SI and DI are the
                                    ; DRIVER's across every file cell (SPEC.md
                                    ; 62.9). SI held the name we are looking
                                    ; for, so on a redirected volume the
                                    ; comparison below was against whatever
                                    ; the driver left there
    xor cx, cx
.l:
    push ds                         ; ES too, for the same reason: it is the
    pop es                          ; find buffer's segment and an input to
    mov di, wr_fbuf                 ; every call
    call OSAPI_FILE_FIND
    jc .no
    cmp word [wr_fbuf+14], OSAPI_FT_DIR
    jne .l
    push cx
    mov si, [wr_dvname]
    mov di, wr_fbuf
    call wr_ieq
    pop cx
    jc .l
    mov dx, [wr_fbuf+16]
    call OSAPI_FILE_HERE                ; BL = the drive; DX is spent by it...
    mov dx, [wr_fbuf+16]                ; ...so the cluster goes back
    call OSAPI_FILE_GOTO_QM
    jc .no
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

; --- wr_ieq - the NUL strings at DS:SI and DS:DI, case-blind. CF = 0 equal ---
wr_ieq:
    push ax
    push bx
.c:
    mov al, [si]
    mov bl, [di]
    call wr_upper
    xchg al, bl
    call wr_upper
    xchg al, bl
    cmp al, bl
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .c
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx
    pop ax
    ret

wr_upper:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret

; --- wr_cfgparse - `host[:port][/prefix/]`, one line -------------------------
; The parse is deliberately forgiving about what follows the line - a CR, an
; LF or the end of the file all end it - and deliberately strict about the
; PREFIX, which is forced to start and end with a slash: a request path is
; composed by concatenation and a prefix without its slashes composes a URL
; that is wrong in a way the server answers 404 to rather than refusing.
wr_cfgparse:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, wr_cfgb
    mov cx, [wr_cfgn]
    mov di, wr_host                     ; --- the host
    mov bx, WR_HOSTMAX - 1
.h:
    jcxz .hend
    mov al, [si]
    cmp al, ' '
    jbe .hend
    cmp al, ':'
    je .colon
    cmp al, '/'
    je .hend
    or bx, bx
    jz .hskip
    mov [di], al
    inc di
    dec bx
.hskip:
    inc si
    dec cx
    jmp short .h
.colon:
    mov byte [di], 0
    inc si
    dec cx
    mov word [wr_port], 0
.p:
    jcxz .pend
    mov al, [si]
    cmp al, '0'
    jb .pend
    cmp al, '9'
    ja .pend
    push cx
    sub al, '0'
    mov bl, al
    xor bh, bh
    mov ax, [wr_port]
    mov cx, 10
    mul cx
    add ax, bx
    mov [wr_port], ax
    pop cx
    inc si
    dec cx
    jmp short .p
.pend:
    cmp word [wr_port], 0
    jne .path
    mov word [wr_port], 80
    jmp short .path
.hend:
    mov byte [di], 0
.path:
    cmp byte [wr_host], 0
    jne .hok
    mov di, wr_host                     ; an empty host is no configuration at
    push si                             ; all: keep the default
    mov si, wr_def_host
    call wr_sput
    mov byte [di], 0
    pop si
.hok:
    jcxz .out
    cmp byte [si], '/'
    jne .out                            ; no prefix given: the default stands
    mov di, wr_pfx
    mov bx, WR_PFXMAX - 2
.x:
    jcxz .xend
    mov al, [si]
    cmp al, ' '
    jbe .xend
    or bx, bx
    jz .xend
    mov [di], al
    inc di
    dec bx
    inc si
    dec cx
    jmp short .x
.xend:
    cmp byte [di-1], '/'                ; ...and it ends with a slash, always
    je .term
    mov byte [di], '/'
    inc di
.term:
    mov byte [di], 0
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The app menu set (SPEC.md 12.2)
; =============================================================================
    OS88_MENUSET wr_menus, wr_ttl, wr_oncmd
        OS88_MENU wr_m_file, wr_i_file, 4
    OS88_MENUSET_END wr_menus

wr_i_file:  dw wr_it_refr, wr_it_run, wr_it_add, wr_it_close

; =============================================================================
%include "wrhttp.inc"                   ; the client and the worker (88.4)
%include "wrarc.inc"                    ; the unpacker and the RAM disk (88.13,
                                        ; 88.14)
%include "wrtxt.inc"                    ; every string (docs/WIRE-PLAN.md 7)

%define OS88UI_SCROLL                   ; the list's bar...
%define OS88UI_SBDRAG                   ; ...and its thumb (SPEC.md 13.10.5)
%define OS88UI_ABOUT                    ; the standard card (SPEC.md 20.5.1)
%include "os88ui.inc"                   ; buttons, radios, bar, card
%include "os88sock.inc"                 ; net_find (SPEC.md 72, 20.11.1)

    OS88_BSS WR_BSS
    OS88_IMAGE_END

; =============================================================================
; loader-zeroed bss (SPEC.md 21 step 5)
;
; ZERO IS A WORKING STATE for all of it but [wr_sel], which wr_entry sets to
; 0xFFFF before anything reads it: record 0 is a real record and a window that
; opened with it selected would be claiming a choice the user never made.
; =============================================================================
wr_win      equ os88_image_end + 0      ; word
wr_hired    equ os88_image_end + 2      ; byte
wr_ready    equ os88_image_end + 3      ; byte: the first wake has run
wr_abon     equ os88_image_end + 4      ; byte: the About card is up
wr_nodrv    equ os88_image_end + 5      ; byte: no stack (SPEC.md 88.9)
wr_picok    equ os88_image_end + 6      ; byte: [wr_pic] holds this selection's
wr_filter   equ os88_image_end + 7      ; byte: 0 All, 1..4 tier <= n-1
wr_job      equ os88_image_end + 8      ; byte: WJ_*
wr_wake     equ os88_image_end + 9      ; byte: WW_*, THE ONE BYTE (88.5)
wr_wake0    equ os88_image_end + 10     ; byte: ...banked by the handler
wr_wkind    equ os88_image_end + 11     ; byte: what that wake was about
wr_gen      equ os88_image_end + 12     ; byte: the generation (SPEC.md 71.11)
wr_gen0     equ os88_image_end + 13     ; byte: ...banked by the worker's pass
wr_state    equ os88_image_end + 14     ; byte: WS_*
wr_kind     equ os88_image_end + 15     ; byte: WK_*
wr_hnd      equ os88_image_end + 16     ; byte: the socket handle, 0 = none
wr_hstate   equ os88_image_end + 17     ; byte: the header scan's line state
wr_dbdrv    equ os88_image_end + 18     ; byte: the banked drive (19.9)
wr_grey     equ os88_image_end + 19     ; byte: bit 0 = Load Program is greyed,
                                        ; bit 1 = Add to Disk... is - written
                                        ; by the painter that greys them

wr_ox       equ os88_image_end + 20     ; word: the content origin...
wr_oy       equ os88_image_end + 22
wr_cw       equ os88_image_end + 24     ; ...its size...
wr_ch       equ os88_image_end + 26
wr_y2       equ os88_image_end + 28     ; ...and everything wr_geom derives
wr_ph       equ os88_image_end + 30
wr_rows     equ os88_image_end + 32
wr_ylim     equ os88_image_end + 34
wr_peny     equ os88_image_end + 36
wr_rowy     equ os88_image_end + 38
wr_rowrec   equ os88_image_end + 40
wr_selrow   equ os88_image_end + 42

wr_catseg   equ os88_image_end + 44     ; word: the 16KB catalog claim
wr_catlen   equ os88_image_end + 46
wr_n        equ os88_image_end + 48     ; word: records, 0 = no catalog
wr_sel      equ os88_image_end + 50     ; word: the selected RECORD, 0xFFFF
wr_top      equ os88_image_end + 52     ; word: the first visible row
wr_msg      equ os88_image_end + 54     ; word: what the status cell says
wr_chain    equ os88_image_end + 56     ; word: which file of the set
wr_fseg     equ os88_image_end + 58     ; word: the transfer claim
wr_flen     equ os88_image_end + 60
wr_dseg     equ os88_image_end + 62     ; word: where the body is going...
wr_dbase    equ os88_image_end + 64
wr_dmax     equ os88_image_end + 66
wr_got      equ os88_image_end + 68     ; DWORD: ...and how much has
wr_clen     equ os88_image_end + 72     ; DWORD: Content-Length
                                        ; **BOTH ARE DWORDS BECAUSE OF THE
                                        ; ARCHIVE** (88.13): a .WPK is the
                                        ; first body here that may pass 64KB -
                                        ; the RunCPM master disk is 196KB
                                        ; compressed - and a 16-bit count laps
                                        ; into a small plausible number rather
                                        ; than failing (PERFORMANCE.md rule 3).
                                        ; The high halves are zero for every
                                        ; other kind and the per-byte store
                                        ; loop only touches them on WK_ARC, so
                                        ; a package and a picture cost exactly
                                        ; what they cost before
wr_code     equ os88_image_end + 76     ; word: the status code
wr_hline    equ os88_image_end + 78     ; word: which reply line
wr_hlen     equ os88_image_end + 80     ; word: how much of it is buffered
wr_sent     equ os88_image_end + 82     ; word: request bytes on the wire
wr_reqn     equ os88_image_end + 84
wr_port     equ os88_image_end + 86
wr_cfgn     equ os88_image_end + 88
wr_dbclus   equ os88_image_end + 90     ; word: the banked cwd (19.9)
wr_shown    equ os88_image_end + 92     ; word: the K figure last drawn
wr_wname    equ os88_image_end + 94     ; word: the name OSAPI_FILE_WRITE was
                                        ; given, banked because a DVK_FILE
                                        ; volume owns SI across that call
wr_need     equ os88_image_end + 96     ; dword: WC_TOTAL, banked because
                                        ; OSAPI_FILE_DFREE WRITES BX
wr_hid      equ os88_image_end + 100    ; byte: the wake handler is drawing
                                        ; with NOTHING of the window visible
                                        ; (wr_clip's CF), so every painter it
                                        ; reaches returns at once. Set and
                                        ; cleared inside wr_onwake alone

WR_B0       equ 102
wr_sb       equ os88_image_end + WR_B0              ; 7 words (13.10)
wr_ra       equ os88_image_end + WR_B0 + 14         ; 4 words: Load Program
wr_rb       equ os88_image_end + WR_B0 + 22         ; 4 words: Add to Disk...
wr_line     equ os88_image_end + WR_B0 + 30         ; WR_LINEN, the PAINTERS'
wr_sline    equ os88_image_end + WR_B0 + 30 + WR_LINEN
WR_OMSGO    equ WR_B0 + 30 + WR_LINEN + WR_STATN + 1
wr_omsg     equ os88_image_end + WR_OMSGO           ; 48: and the OUTCOME's,
                                                    ; which may NOT be wr_line.
                                                    ; [wr_msg] points at it and
                                                    ; is read by the status
                                                    ; cell on every later
                                                    ; repaint - while wr_line
                                                    ; is rewritten by every row
                                                    ; and every description
                                                    ; line, so the cell would
                                                    ; show the last thing
                                                    ; DRAWN rather than the
                                                    ; last thing that HAPPENED
WR_B1       equ WR_OMSGO + 48
wr_icobuf   equ os88_image_end + WR_B1              ; 2 + 64: the ICON_DRAW
                                                    ; record the catalog's 64
                                                    ; rows are copied into
wr_hbuf     equ os88_image_end + WR_B1 + 66         ; WR_HLINE
wr_host     equ os88_image_end + WR_B1 + 66 + WR_HLINE
wr_pfx      equ os88_image_end + WR_B1 + 66 + WR_HLINE + WR_HOSTMAX
WR_B2       equ WR_B1 + 66 + WR_HLINE + WR_HOSTMAX + WR_PFXMAX
wr_fname    equ os88_image_end + WR_B2              ; 14: the file being moved
wr_savename equ os88_image_end + WR_B2 + 14         ; 14: the user's chosen name
wr_fbuf     equ os88_image_end + WR_B2 + 28         ; OSAPI_FIND_SZ
wr_cfgb     equ os88_image_end + WR_B2 + 28 + OSAPI_FIND_SZ
WR_B2A      equ WR_B2 + 28 + OSAPI_FIND_SZ + WR_HOSTMAX + WR_PFXMAX
wr_path     equ os88_image_end + WR_B2A             ; the request path
wr_nmsg     equ os88_image_end + WR_B2A + WR_PFXMAX + 16
                                                    ; ...and the WORKER's own
                                                    ; message buffer, which may
                                                    ; not be wr_line: the two
                                                    ; tasks compose at the same
                                                    ; time and one buffer is a
                                                    ; status cell that reads as
                                                    ; half of each
WR_B3       equ WR_B2A + WR_PFXMAX + 16 + 48
wr_reqb     equ os88_image_end + WR_B3              ; WR_REQMAX
wr_prow     equ os88_image_end + WR_B3 + WR_REQMAX  ; 64: the BLIT4 fallback
WR_B4       equ WR_B3 + WR_REQMAX + 64
wr_pic      equ os88_image_end + WR_B4              ; WIRE_PICSZ, INVERTED
wr_rxb      equ os88_image_end + WR_B4 + WIRE_PICSZ ; WR_CHUNK, and it is in
                                                    ; OUR SEGMENT because
                                                    ; NETV_RECV takes ES:DI
                                                    ; and ES is ours (77.10)

; --- the archive (SPEC.md 88.13, 88.14; wrarc.inc is the code) ---------------
; **THE BIG BUFFER IS THE HEAP CLAIM AND NOT ANY OF THIS.** One entry is
; decoded at a time into a claim sized from the header's WA_MAXENT, so what
; the package's own bss owes the feature is the two headers it is reading, the
; folder it last stood in, and about thirty bytes of counters.
WR_B5       equ WR_B4 + WIRE_PICSZ + WR_CHUNK
wr_ahdr     equ os88_image_end + WR_B5              ; WARC_HDR: the file header
wr_ehdr     equ os88_image_end + WR_B5 + WARC_HDR   ; WARC_ENT: one entry's
wr_lpath    equ os88_image_end + WR_B5 + WARC_HDR + WARC_ENT
WR_LPATHN   equ WARC_DEPTH * WARC_SLOT              ; 36: the FOLDERS of the
                                                    ; entry last written, so an
                                                    ; entry in the same folder
                                                    ; is written without moving
WR_B6       equ WR_B5 + WARC_HDR + WARC_ENT + WR_LPATHN
WR_RMSGN    equ 40
wr_rmsg     equ os88_image_end + WR_B6              ; WR_RMSGN: the PREDICATE's
                                                    ; composed reason, and it
                                                    ; may not be wr_omsg -
                                                    ; wrarc.inc's wr_rreason
                                                    ; says why
WR_B7       equ WR_B6 + WR_RMSGN
wr_aph      equ os88_image_end + WR_B7 + 0          ; byte: WAP_*
wr_apw      equ os88_image_end + WR_B7 + 1          ; byte: which wake the pause
                                                    ; is to post
wr_ldep     equ os88_image_end + WR_B7 + 2          ; byte: the banked depth,
                                                    ; 0xFF = nothing banked
wr_aflags   equ os88_image_end + WR_B7 + 3          ; byte: WA_FLAGS
wr_ramjob   equ os88_image_end + WR_B7 + 4          ; byte: this tree is going
                                                    ; to a RAM disk, which is
                                                    ; what the toast says
wr_rdmnt    equ os88_image_end + WR_B7 + 5          ; byte: wr_ramck may mount
wr_lph      equ os88_image_end + WR_B7 + 6          ; byte: the decoder is
                                                    ; waiting on a pair's
                                                    ; second byte
wr_lflag    equ os88_image_end + WR_B7 + 7          ; byte: the group's flags
wr_lbits    equ os88_image_end + WR_B7 + 8          ; byte: ...items left in it
wr_lb0      equ os88_image_end + WR_B7 + 9          ; byte: a pair's first byte
wr_bdrv     equ os88_image_end + WR_B7 + 10         ; byte: the base folder's
                                                    ; drive
wr_acnt     equ os88_image_end + WR_B7 + 12         ; word: header bytes so far
wr_an       equ os88_image_end + WR_B7 + 14         ; word: the entry count
wr_adone    equ os88_image_end + WR_B7 + 16         ; word: entries complete
wr_asize    equ os88_image_end + WR_B7 + 18         ; word: this entry unpacked
wr_asto     equ os88_image_end + WR_B7 + 20         ; word: ...and stored
wr_aout     equ os88_image_end + WR_B7 + 22         ; word: bytes decoded
wr_ain      equ os88_image_end + WR_B7 + 24         ; word: stored bytes fed
wr_bclus    equ os88_image_end + WR_B7 + 26         ; word: the base folder
wr_rxp      equ os88_image_end + WR_B7 + 28         ; word: the read position in
                                                    ; wr_rxb, so a pause keeps
                                                    ; the rest of the chunk
wr_rxn      equ os88_image_end + WR_B7 + 30         ; word: ...and what is left
wr_pnum     equ os88_image_end + WR_B7 + 32         ; word: 'N of M', the N...
wr_ptot     equ os88_image_end + WR_B7 + 34         ; word: ...and the M
wr_rlen     equ os88_image_end + WR_B7 + 36         ; word: the bytes handed to
                                                    ; OSAPI_PKG_RUN
wr_needkb   equ os88_image_end + WR_B7 + 38         ; word: WC_NEEDKB, banked
                                                    ; because OSAPI_DRV_CALL
                                                    ; owns every register
wr_dvname   equ os88_image_end + WR_B7 + 40         ; word: the folder name
                                                    ; wr_dive is looking for,
                                                    ; banked past a file cell
                                                    ; that owns SI
wr_anb      equ os88_image_end + WR_B7 + 42         ; 13: one path slot, copied
                                                    ; and TERMINATED. SPEC.md
                                                    ; 88.13's twelve-character
                                                    ; name fills its slot with
                                                    ; no NUL, and every kernel
                                                    ; cell reads to one
WR_BSS      equ WR_B7 + 55
