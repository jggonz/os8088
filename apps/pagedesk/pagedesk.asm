; =============================================================================
; os8088 - apps/pagedesk/pagedesk.asm     PAGEDESK.O88's shim (PAGEDESK-SPEC 2)
;
; The top-level nasm source of PageDesk's PRIMARY segment: the package header,
; the parts table, the compiled C (pagedesk.c and the pdu*.c it #includes),
; the shared control library and the hand-written seam - ONE assembly, because
; `nasm -f bin` has no notion of an external symbol (SPEC.md 73.1). LOOM's
; shim (apps/loom/loom.asm) is the shape and its notes are the argument for
; every ordering below; what is said here is what differs.
;
; WHAT THE ONE FILE CARRIES (PAGEDESK-SPEC 2.2):
;
;   part 0  PDENG   eager  the engine: file layer, objects, xref, pages
;   part 1  PDREND  eager  the renderer: interpreter, bands, glyphs
;   part 2  PDWRT   lazy   the writer: an occupant of the SLOT (20.22.2),
;                          read by offset at a save, freed after
;   part 3  PDRES   lazy   the font bank, widths and art - OP_SELF: read by
;   part 4                 OFFSET out of PAGEDESK.O88 itself (pdumod.c), never
;                          fetched whole by the standard. TWO ROWS: PDRES.BIN
;                          is 77,828 bytes and a part's ceiling is 65,024
;                          (os88pkg.py, SPEC.md 20.12.11), so the Makefile
;                          cuts it there - a multiple of 16, so os88pkg's
;                          paragraph layout puts row 4's bytes straight after
;                          row 3's and every PDRES file offset (D3) is still
;                          part_off + offset. pdu_bank_load checks it.
;   part 5  PDXK    lazy   the slot's occupants (PAGEDESK-SPEC 20.22.2.3):
;   part 6  PDXG           OP_SELF | OP_COMP, read by offset and expanded
;   part 7  PDXF           onto claim S's base by pduslot.c, one at a time:
;   part 8  PDXI           key derivation, colour/shading, font programs,
;   part 9  PDXJ           DCT/CCITT/JBIG2, JPEG 2000
;   part 10 the overlay    lazy, last (SPEC.md 73.14.2): pdovl.c's ovl_*
;
; Every module part is the module's image FOLLOWED BY ITS .bss AS ZEROS (the
; Makefile's pd*.part rules, apps/loom's lmpvpart shape), stored OP_COMP: the
; trailing zeros pack to a few bytes, and the carve (or op_fetch's claim,
; sized from the row's `len`) then covers the bss the module zeroes on its
; first entry (PAGEDESK-SPEC 3.1). Not OP_ZFILL: that cuts round an INTERIOR
; run of zeros and refuses a part whose only zeros are its tail.
; =============================================================================

; --- os88ui.inc's FEATURE DEFINES, at the TOP (apps/loom/loom.asm says why) --
%define OS88UI_SCROLL               ; the canvas's vertical bar
%define OS88UI_SBDRAG               ; nav: its thumb drags (SPEC.md 13.10.5)
%define OS88UI_ABOUT                ; the standard About card (SPEC.md 20.5.1.1)
%define OS88UI_NOGEST               ; no button gesture install: PageDesk's
                                    ; bars hit-test their own rects

%define CC_PKG_NAME 'PAGEDESK'
%define CC_STACK_CLASS OS88_STACK_128 | OS88_HIGH | OS88_HIGHMEM
                                    ; no worker (PAGEDESK-SPEC 15.1), so the
                                    ; worker class is the smallest. HIGH and
                                    ; HIGHMEM: no arithmetic on our own
                                    ; segments or on a claim's - the modules
                                    ; are reached through OS88_CODESEL'd
                                    ; vectors (pdui.inc) and every claim is
                                    ; handed down as an opaque segment

%define CC_HAS_ONKEY                ; PAGEDESK-SPEC 17.5's keys
%define CC_HAS_ONCLICK              ; bars, panels, canvas, scroll bar
%define CC_HAS_ONRESIZE             ; 17.1: the layout is the live content box
%define CC_HAS_MENUS                ; File Edit View Document Help
%define CC_HAS_ABOUT                ; About PageDesk (the INDEX story)
%define CC_HAS_FDLG                 ; File > Open / Save As (SPEC.md 38)
%define CC_HAS_ONWAKE               ; THE JOB SCHEDULER (PAGEDESK-SPEC 15.1):
                                    ; one slice a wake, re-posted
%define CC_HAS_ONWHEEL              ; the wheel scrolls the canvas
%define CC_HAS_ONMOUSEUP            ; nav: the release that ends a drag...
%define CC_HAS_ONTIMER              ; ...and the poll that tracks one (pdunav.c)
%define CC_HAS_ONCLOSE              ; a save in flight deletes its temp file
%define CC_HAS_OVL                  ; pdovl.c's ovl_* (PAGEDESK-SPEC 2.2)
%define CC_OVL_PART                 ; ...a PART of PAGEDESK.O88
%define CC_OVL_LAZY                 ; ...fetched on the first call into it
%define OP_NO_LAZYOK                ; op_fetch's own refusal is the answer

%define CC_ICON     "pdart_appicon.inc"         ; tools/os88pdart.py, art/app.art
%define CC_ASSOC    "pagedesk/pduassoc.inc"     ; .PDF (SPEC.md 54.6)
%define CC_DOCGLYPH "pdart_docglyph.inc"        ; the 8x8 a .PDF wears (same art)

%include "cc/crt0.asm"

; --- THE PARTS TABLE (SPEC.md 20.12, PAGEDESK-SPEC 2.2) -----------------------
; Eager rows first, lazy after, the overlay last (crt0.asm's CC_OVL_ROW rule).
; pdu.h's PDU_PART_* name these rows and the %if at the foot holds them there.
    CC_PARTS_BEGIN 12
      %assign PD_PART_ENG_ASM OS88_PART_N
      OS88_PART OP_SEG,   OP_COMP                  ; 0 PDENG
      %assign PD_PART_REND_ASM OS88_PART_N
      OS88_PART OP_SEG,   OP_COMP                  ; 1 PDREND
      %assign PD_PART_WRT_ASM OS88_PART_N
      OS88_PART OP_SEG,   OP_LAZY | OP_SELF | OP_COMP  ; 2 PDWRT (the slot's)
      %assign PD_PART_RES_ASM OS88_PART_N
      OS88_PART OP_ASSET, OP_LAZY | OP_SELF        ; 3 PDRES [0, 65024)
      %assign PD_PART_RES2_ASM OS88_PART_N
      OS88_PART OP_ASSET, OP_LAZY | OP_SELF        ; 4 PDRES [65024, end)
      %assign PD_PART_XK_ASM OS88_PART_N
      OS88_PART OP_SEG,   OP_LAZY | OP_SELF | OP_COMP  ; 5 PDXK
      OS88_PART OP_SEG,   OP_LAZY | OP_SELF | OP_COMP  ; 6 PDXG
      OS88_PART OP_SEG,   OP_LAZY | OP_SELF | OP_COMP  ; 7 PDXF
      OS88_PART OP_SEG,   OP_LAZY | OP_SELF | OP_COMP  ; 8 PDXI
      %assign PD_PART_XJ_ASM OS88_PART_N
      OS88_PART OP_SEG,   OP_LAZY | OP_SELF | OP_COMP  ; 9 PDXJ
      %assign PD_PART_X8_ASM OS88_PART_N
      OS88_PART OP_SEG,   OP_LAZY | OP_SELF | OP_COMP  ; 10 PDX8 (the X8 region's)
      CC_OVL_ROW                                   ; 11 the overlay
    CC_PARTS_END

%include "pagedesk.gen.asm"         ; the compiled C, found through -I build/

%define OS88UI_NOGLYPH              ; no check box or radio drawn here
%define OS88UI_NOBFIND              ; hit-tests its own rects
%include "os88ui.inc"

%include "pagedesk/pdui.inc"        ; the seam: module vectors, part rows,
                                    ; the read by offset, the scroll bar
%include "pagedesk/pdkit.inc"       ; the face's composer: one band, one
                                    ; BLIT1 a control (PAGEDESK-SPEC 20.21.3)
%include "pagedesk/pdstk.inc"       ; pdu_mcall on the private module stack
                                    ; (PAGEDESK-SPEC 20.15, D23)
%include "pagedesk/pduslot.inc"     ; the slot, its cache, the regions and
                                    ; the ladder (PAGEDESK-SPEC 20.22.2)
; ...and the C SDK thunks only pduslot.inc calls (cc8086.py's CC_U_ list sees
; the C's calls alone - its own note says to keep them here)
%define CC_U__os88_mem_regrow_p
%define CC_U__os88_mem_largest_p
%define CC_U__os88_mem_claim_p
%define CC_U__os88_mem_free
%define CC_U__os88_seg_sub

; THE ARITHMETIC (PAGEDESK-SPEC 4.2, 4.3). pdmath.inc is the math agent's;
; until it lands the Makefile passes -DPD_MATH_STUB and the few routines the
; primary calls come from a stand-in with the same names.
%ifdef PD_MATH_STUB
  %include "pagedesk/stubs/pdmstub.inc"
%else
  %include "pagedesk/pdmath.inc"
%endif

; --- THE DRIFT GUARDS --------------------------------------------------------
%if PD_PART_ENG_ASM != 0 || PD_PART_REND_ASM != 1 || PD_PART_WRT_ASM != 2
  %error "the parts table moved; pdu.h's PDU_PART_* must follow"
%endif
%if PD_PART_XK_ASM != 5 || PD_PART_XJ_ASM != 9 || PD_PART_X8_ASM != PDU_PART_X8_A
  %error "the parts table moved; pdu.h's PDU_PART_X* must follow"
%endif
%if PD_PART_RES_ASM != 3 || PD_PART_RES2_ASM != 4 || cc_ovidx != 11
  %error "the parts table moved; pdu.h's PDU_PART_RES / the overlay row must follow"
%endif
%if OS88UI_SBNONE != 0 || OS88UI_SBUP != 1 || OS88UI_SBDOWN != 2
  %error "os88ui.inc's OS88UI_SB* renumbered; pdu.h's PDU_SB_* must follow"
%endif
%if OS88UI_SBPGUP != 3 || OS88UI_SBPGDN != 4 || OS88UI_SBTHUMB != 5
  %error "os88ui.inc's OS88UI_SB* renumbered; pdu.h's PDU_SB_* must follow"
%endif

    CC_IMAGE_END
