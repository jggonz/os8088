; =============================================================================
; os8088 - apps/os88stikc.inc
;
; The CLIENT arm of apps/os88stik.inc (SPEC.md 110.15), which includes it for
; a package built `%define STIK_LIB`. You do not include it yourself.
;
; What a Sticky App on the shared library still carries: the descriptor's
; values (STIKD_*, assembled into its block by OS88_STIKCARD_END), the four
; strings and the caption the library asks for by address, a five-byte thunk
; per routine, the trampoline the library calls back through, and the bind -
; which is this file's stik_load. The routines' names, registers and
; contracts are os88stik.inc's, unchanged.
; =============================================================================

%if STIK_ICON != 32
  %error "os88stik: STIK_LIB wants the icon body at 32 (OS88_F_ICON), as the library reads it"
%endif
%ifndef STIK_RIGHT
  %error "os88stik: STIK_LIB wants STIK_RIGHT - the library places the face from it"
%endif
%if STIK_PAYVER > 255 || STIK_PAYVER < 0
  %error "os88stik: STIK_PAYVER is a byte, and +13 is its zero"
%endif

; --- the descriptor (os88api.inc's STKL_D_*) -----------------------------------
STIKD_FILE      equ stik_file
STIKD_PAYLEN    equ STIK_PAYLEN
STIKD_PAYVER    equ STIK_PAYVER
%assign stikd_fl 0
%ifdef STIK_DMGREPLACE
  %assign stikd_fl stikd_fl | STKF_DMGREPLACE
%endif
%ifdef STIK_PACK
  %assign stikd_fl stikd_fl | STKF_PACK
%endif
STIKD_FLAGS     equ stikd_fl
STIKD_TAGW      equ STIK_TAGW
STIKD_RIGHT     equ STIK_RIGHT
STIKD_LAYS      equ STIK_LAYOUTS
STIKD_BUF       equ GFXE_BAND_BUF
STIKD_BST       equ GFXE_BAND_ST
STIKD_BW        equ GFXE_BAND_W
STIKD_BROWS     equ GFXE_BAND_H
STIKD_DNAME     equ stik_dname
STIKD_CAP       equ stik_cap
STIKD_ISOPEN    equ stik_isopen
STIKD_TAG       equ stik_tags
STIKD_FW        equ STIK_FACE_W
STIKD_XCB       equ stik_xcb
STIKD_PAINTCARD equ STIK_PAINTCARD
; the package's optional procs: its proc, or 0 for none
%ifdef STIK_COMPOSE
STIKD_COMPOSE equ STIK_COMPOSE
%else
STIKD_COMPOSE equ 0
%endif
%ifdef STIK_ONCARD
STIKD_ONCARD equ STIK_ONCARD
%else
STIKD_ONCARD equ 0
%endif
%ifdef STIK_ONCARDUP
STIKD_ONCARDUP equ STIK_ONCARDUP
%else
STIKD_ONCARDUP equ 0
%endif
%ifdef STIK_ONFOCUS
STIKD_ONFOCUS equ STIK_ONFOCUS
%else
STIKD_ONFOCUS equ 0
%endif
%ifdef STIK_ONTICK
STIKD_ONTICK equ STIK_ONTICK
%else
STIKD_ONTICK equ 0
%endif
%ifdef STIK_ONCLOSE
STIKD_ONCLOSE equ STIK_ONCLOSE
%else
STIKD_ONCLOSE equ 0
%endif
%ifdef STIK_SIZEUP
STIKD_SIZEUP equ STIK_SIZEUP
%else
STIKD_SIZEUP equ 0
%endif
%ifdef STIK_PACK
STIKD_PACK equ STIK_PACK
%else
STIKD_PACK equ 0
%endif
%ifdef STIK_UNPACK
STIKD_UNPACK equ STIK_UNPACK
%else
STIKD_UNPACK equ 0
%endif
STIKD_CLICK     equ stik_click  ; the kernel's callbacks are our thunks: a
STIKD_MUP       equ stik_mup    ; callback is a near proc in the caller's
STIKD_TICK      equ stik_tick   ; segment (SPEC.md 20.2)
STIKD_CLOSE     equ stik_close
STIKD_WAKE      equ STIK_WAKE

; --- the thunks: os88stik.inc's routines, in DESK.DRV -------------------------
%macro STIK_THUNK 2
%1:
    call far [stik_le + 4 * %2]
    ret
%endmacro
    STIK_THUNK stik_made, STKE_MADE
    STIK_THUNK stik_touch, STKE_TOUCH
    STIK_THUNK stik_org, STKE_ORG
    STIK_THUNK stik_ctop, STKE_CTOP
    STIK_THUNK stik_slabs, STKE_SLABS
%ifdef STIK_REDRAW
    STIK_THUNK stik_redraw, STKE_REDRAW
%endif
    STIK_THUNK stik_redrawr, STKE_REDRAWR
%ifdef STIK_VARFACE
    STIK_THUNK stik_resize, STKE_RESIZE
%endif
    STIK_THUNK stik_quit, STKE_QUIT
    STIK_THUNK stik_desk, STKE_DESK
    STIK_THUNK stik_paint, STKE_PAINT
    STIK_THUNK stik_click, STKE_CLICK
    STIK_THUNK stik_mup, STKE_MUP
    STIK_THUNK stik_tick, STKE_TICK
    STIK_THUNK stik_close, STKE_CLOSE
    STIK_THUNK stik_wake, STKE_WAKE
%ifdef STIK_WALK                ; the walk to SYSTEM\APPDATA and its find, for
    STIK_THUNK stik_enter, STKE_ENTER   ; a package with files of its own
    STIK_THUNK stik_leave, STKE_LEAVE   ; there (os88stik.inc's contracts)
    STIK_THUNK stik_goqm, STKE_GOQM
    STIK_THUNK stik_seek, STKE_SEEK
%endif

; stik_xcb - the library's way back in (its stik_cb): a far call here runs
; the near proc it put in [stik_xt], which this reads before anything else
; can write it
stik_xcb:
    call [stik_xt]
    retf

; -----------------------------------------------------------------------------
; stik_load - the BIND, then the library's stik_load (os88stik.inc's contract:
; SI = the loader's name, DI = the template; CF = 1 refused and said, the
; entry proc returns CF = 1; every register kept). QUERY - the loader's step 5
; has normally loaded DESK.DRV for an OS88_F_DESK package already, so it
; touches no disk - then LIB, the library's version, and the segment into
; every far pointer of the block. Any refusal is the one a desk-only widget
; always said: `No desk layer (DESK.DRV)` (SPEC.md 110.7)
; -----------------------------------------------------------------------------
stik_load:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    mov al, 0
    call OSAPI_WM_DESK          ; QUERY
    jc .no
    mov al, 4
    call OSAPI_WM_DESK          ; LIB: DX = the library's paragraph
    jc .no
    mov es, dx
    cmp word [es:0], STIKLIB_MAGIC
    jne .no
    cmp word [es:2], STIKLIB_VER
    jne .no                     ; another kernel's library: refused
    mov di, stik_le + 2
    mov cx, STIKLIB_NENT
.b:
    mov [di], dx
    add di, 4
    loop .b
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    call far [stik_le + 4 * STKE_LOAD]
    ret
.no:
    push ds
    pop es
    mov si, stik_nodesk
    xor cx, cx
    call OSAPI_TOAST            ; the entry proc's own toast (SPEC.md 20.12.4)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    stc
    ret

; --- what the library asks for by address -------------------------------------
stik_tags   db STIK_TAG, 0
stik_dname  db STIK_DATNAME, 0
%ifdef STIK_ISOPEN
stik_isopen db STIK_ISOPEN, 0
%else
stik_isopen db 'Already open', 0
%endif
stik_nodesk db 'No desk layer (DESK.DRV)', 0
%strlen STIK_CAPLEN STIK_CAP
%if STIK_CAPLEN > 12
  %error "os88stik: STIK_CAP is at most 12 characters"
%endif
stik_cap    db STIK_CAP, 0
            times OSAPI_DI_BODY - OSAPI_DI_CAP - ($ - stik_cap) db 0
