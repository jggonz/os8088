; =============================================================================
; os8088 - apps/stiklib.asm
;
; THE STICKY APP LIBRARY (SPEC.md 110.15): apps/os88stik.inc and the
; GFXE_TEXT / GFXE_BITS half of apps/os88gfx.inc, assembled ONCE. It is not a
; package. kernel/deskmod.inc carries it at the end of DESK.DRV's image
; (`incbin`), so it is on the system disk once, on the heap once - in the
; desk layer's claim, held from the first QUERY to the restart - and every
; Sticky App built `%define STIK_LIB` far-calls this copy instead of carrying
; its own. OSAPI_WM_DESK's LIB verb answers its paragraph.
;
; ITS CONVENTIONS ARE A PACKAGE'S, ONE LEVEL REMOVED. DS is the CALLING
; package's on every instruction - so its OSAPI calls are that package's,
; which is how the kernel knows its caller (SPEC.md 50.3) - and everything it
; knows about the package is in the package's Sticky App block at
; OS88_STIKLIB_AT: the descriptor, the far pointers, the state. Its own image
; holds code and two tables, read through CS. A call into the package (a card
; painter, a tick, a callback the kernel installs) goes through the package's
; trampoline (stik_cb), and the kernel's callbacks are the package's thunks.
;
; The layout: STIKLIB_MAGIC, STIKLIB_VER, then STIKLIB_NENT entries of four
; bytes - `call` the routine, `retf` - in apps/os88api.inc's STKE_* order.
; A package checks both words before it binds a single pointer.
; =============================================================================

%define STIK_LIBRARY
%define STIK_WIDGET
%define STIK_DESKONLY
%define STIK_VARFACE            ; the face's width is always [stik_fw]
%define GFXE_TEXT
%define GFXE_BITS
%define GFXE_SCALE              ; every style a package may ask for

%include "os88api.inc"

os88_stiklib equ OS88_STIKLIB_AT ; the CALLER's block, in the caller's DS
STIK_ICON   equ 32              ; every Sticky App's icon body (os88stikc.inc
                                ; refuses one built otherwise)

    cpu 8086
    bits 16
    org 0

    dw STIKLIB_MAGIC, STIKLIB_VER

%macro STIKLIB_ENT 2
  %if ($ - $$) != STIKLIB_JT + 4 * %2
    %error "stiklib: the entry table is out of STKE_* order"
  %endif
    call %1
    retf
%endmacro
    STIKLIB_ENT stik_load, STKE_LOAD
    STIKLIB_ENT stik_made, STKE_MADE
    STIKLIB_ENT stik_touch, STKE_TOUCH
    STIKLIB_ENT stik_org, STKE_ORG
    STIKLIB_ENT stik_ctop, STKE_CTOP
    STIKLIB_ENT stik_slabs, STKE_SLABS
    STIKLIB_ENT stik_redraw, STKE_REDRAW
    STIKLIB_ENT stik_redrawr, STKE_REDRAWR
    STIKLIB_ENT stik_resize, STKE_RESIZE
    STIKLIB_ENT stik_quit, STKE_QUIT
    STIKLIB_ENT stik_desk, STKE_DESK
    STIKLIB_ENT stik_paint, STKE_PAINT
    STIKLIB_ENT stik_click, STKE_CLICK
    STIKLIB_ENT stik_mup, STKE_MUP
    STIKLIB_ENT stik_tick, STKE_TICK
    STIKLIB_ENT stik_close, STKE_CLOSE
    STIKLIB_ENT stik_wake, STKE_WAKE
    STIKLIB_ENT gfxe_text, STKE_TEXT
    STIKLIB_ENT gfxe_glyph, STKE_GLYPH
    STIKLIB_ENT gfxe_bfill, STKE_BFILL
    STIKLIB_ENT gfxe_bits, STKE_BITS
    STIKLIB_ENT gfxe_bclear, STKE_BCLEAR
    STIKLIB_ENT stik_enter, STKE_ENTER
    STIKLIB_ENT stik_leave, STKE_LEAVE
    STIKLIB_ENT stik_goqm, STKE_GOQM
    STIKLIB_ENT stik_seek, STKE_SEEK
%if ($ - $$) != STIKLIB_JT + 4 * STIKLIB_NENT
  %error "stiklib: STIKLIB_NENT entries, no more and no fewer"
%endif

%include "os88gfx.inc"
%include "os88stik.inc"
