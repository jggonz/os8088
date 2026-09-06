; =============================================================================
; os8088 - apps/weave/weave.asm
;
; The assembly shim of WEAVE, the .WAB runtime (WEAVE-SPEC 1.2). It is
; the top-level nasm source: the .c files are never assembled on their own,
; because `nasm -f bin` has no notion of an external symbol, so the compiled C,
; the runtime, the hand-written cores and the 32-byte header are ONE assembly
; (SPEC.md 73.1).
;
; A shim does four things and nothing else belongs in it: name the package,
; declare which callbacks the C actually defines, include the runtime and then
; the compiled C in that order (the header has to be at file offset 0), and
; close the image. The two halves cannot drift silently - a %define with no C
; function behind it is an nasm external-reference error naming that function,
; and a C function with no %define is code the kernel never calls.
;
; IT IS NOT LOOM. LOOM.O88 is a separate package (WEAVE-SPEC 1.2) - the
; WORD/CWORD precedent, that two things may not answer to one name - and
; nothing here may reach a `loom` name.
; =============================================================================

; --- os88ui.inc's FEATURE DEFINES, AND THEY BELONG AT THE TOP ----------------
; Preprocessor tests are answered in FILE ORDER, and os88ui.inc's own include
; is at the very END of this file by its contract (see there). A %define placed
; beside that include is BELOW every %ifdef that reads it: the guarded bodies
; assemble anyway - the binary GROWS - while each of the file's own guarded
; blocks silently vanishes, which is 405 bytes and a thumb that does not move
; (SPEC.md 13.10.7.4). So they are here, at the top, and the include is at the
; bottom, and the distance between the two is the whole point.
;
; WEAVE-SPEC 6.1's component library needs two of the three features:
%define OS88UI_SCROLL               ; os88ui_sbar/sbhit/sbmove and the
                                    ; OS88UI_SB* answers - <list> and <grid>
%define OS88UI_ARM                  ; os88ui_arm/fire/armed - the press/release
                                    ; gesture every <button>, <check> and
                                    ; <radio> is armed through
%define OS88UI_ABOUT                ; ...the standard About card (SPEC.md
                                    ; 20.5.1.1), reached from C through
                                    ; os88_about_card()
%define OS88UI_ALERT                ; ...and SPEC.md 75.3's alert, which is
                                    ; WEAVE-SPEC 8.2's alert() and 4.11's
                                    ; runaway question. 607 bytes of OUR image
                                    ; and none of the kernel's, which is that
                                    ; file's whole argument applied to
                                    ; something bigger than a button.
                                    ;
                                    ; NOT %define OS88UI_SBDRAG: nothing drags
                                    ; a thumb yet, and the state it declares is
                                    ; six bytes of bss and a body. Add it here,
                                    ; not beside the include.

%define CC_PKG_NAME 'WEAVE'         ; <= 15 chars, and the stem of WEAVE.OVL:
                                    ; crt0.asm builds the module's file name
                                    ; out of this and '.OVL', so the disk and
                                    ; the loader cannot disagree about it

%define CC_HAS_ONKEY                ; void os88_onkey(int, int, void *)
%define CC_HAS_ONCLICK              ; void os88_onclick(int, int, void *)
%define CC_HAS_ONMOUSEUP            ; void os88_onmouseup(int, int, void *) -
                                    ; the RELEASE half of SPEC.md 13.7's
                                    ; gesture, and the only place a <button>
                                    ; fires (WEAVE-SPEC 6.5)
%define CC_HAS_ONWAKE               ; void os88_onwake(void *) - the one
                                    ; callback that runs WITHOUT the gfx lock,
                                    ; and therefore the only place a WVM slice
                                    ; may run (WEAVE-SPEC 4.10)
%define CC_HAS_ONTIMER              ; void os88_ontimer(void *) - 6.7's caret
                                    ; and 8.2's timer(), multiplexed over the
                                    ; one one-shot a window gets (SPEC.md 13.9)
%define CC_HAS_ONRESIZE             ; void os88_onresize(int, int, void *) -
                                    ; SPEC.md 11.98, and NOT the same thing as
                                    ; WM_ONSIZE: that one is asked BEFORE a
                                    ; resize and takes an answer, this one is
                                    ; told after and must not draw
%define CC_HAS_MENUS                ; void os88_oncmd(int, int, void *)
%define CC_HAS_ABOUT                ; void os88_about(void *)
%define CC_HAS_FDLG                 ; void os88_onfile(...) - File > Open, the
                                    ; second of WEAVE-SPEC 1.5's ways in
%define CC_HAS_OVL                  ; ...and WEAVE.OVL (SPEC.md 73.14), whose
                                    ; tenants are in apps/weave/wovl.c
%define CC_HAS_WORKER               ; void os88_worker(void *) - WEAVE-SPEC

%define CC_STACK_CLASS OS88_STACK_192 ; ...AND HOW MUCH STACK IT WANTS
                                    ; (SPEC.md 8.7). Declared rather
                                    ; than defaulted:
                                    ; os88_worker() is a three-instruction trampoline into WEAVE.WSM,
                                    ; whose own frame loop walks 22 more - about 62 all told, so 126
                                    ; over the floor and 192 gives 1.52x
                                    ; It is NOT the same number as
                                    ; crt0.asm's CC_STACK, which is
                                    ; the LARGEST class: this is what
                                    ; we ask for and that is the
                                    ; widest slice we could be given
                                    ; 6.10's canvas frame loop, hired at the
                                    ; first start() and parked between runs.
                                    ; The BODY is not in this image at all: it
                                    ; is WEAVE.WSM (1.2.2), a second RESIDENT
                                    ; segment, because SPEC.md 73.14's overlay
                                    ; refuses a worker at its first
                                    ; instruction and every byte of that loop
                                    ; runs on one. What os88_worker() does is
                                    ; far-call it - and the entry has to be
                                    ; HERE anyway, because OSAPI_TASK_SPAWN's
                                    ; ownership fence checks that it lies
                                    ; inside this instance's own region.
                                    ;
                                    ; It also arms cc_iswk: without
                                    ; CC_HAS_WORKER, cc_wksp is never set and
                                    ; every task reads as the UI task - which
                                    ; would let the WORKER load WEAVE.OVL, and
                                    ; SPEC.md 20.6 rule 7 is why it may not

%define CC_ICON  "weave/icon.inc"   ; 32 dw rows exactly: 16 mask, 16 data
%define CC_ASSOC "weave/wvassoc.inc" ; 'WAB' - so a bundle opens on the FIRST
                                    ; double-click, with no prior run and no
                                    ; search (SPEC.md 54.6, WEAVE-SPEC 1.5)

%include "cc/crt0.asm"              ; the sections, the 32-byte header, the
                                    ; icon and association blocks, the entry
                                    ; and callback trampolines, the overlay
                                    ; runtime and the whole API bridge

%include "weave.gen.asm"            ; the compiled C, found through -I build/

; --- THE SHARED CONTROL LIBRARY, BEFORE THE CORES THAT NAME ITS CONSTANTS ----
; os88ui.inc is the standard control (SPEC.md 20.5.1) and os88line.inc the
; one-line field behind every <input> (WEAVE-SPEC 6.7). Both are included HERE
; - after crt0.asm, before wdraw.inc - and the position is load-bearing in two
; directions at once.
;
; WHY THIS IS NOT THE VIOLATION IT LOOKS LIKE. os88ui.inc's header says "at the
; END of your code, not up beside os88api.inc", and the reason it gives is the
; icon: the 32-byte header and the OS88_ICON16 block are at FIXED OFFSETS
; (SPEC.md 20.2), so an assembly package that includes it early emits code
; between them and fails the icon macro's offset assertion. THAT HAZARD CANNOT
; REACH A C PACKAGE AT ALL. crt0.asm emits the header, the icon and the
; association block itself and carries all four assertions inside it
; (apps/cc/crt0.asm's `%if ($ - $$) != 32` / `!= 96` pairs), and they are
; decided during the %include above - so nothing placed after that line can
; violate them. What the "at the end" convention actually protects is
; narrower: your own %ifdef gates and any %assign naming an os88ui constant
; must sit below the include (apps/notepad/notepad.asm says this about
; OS88UI_AMAX). Neither binds our position relative to our own .inc files.
;
; NOT LATER, and this is the half that is easy to get wrong. wdraw.inc names
; two things that are not labels: OS88UI_SB* in a %if (its drift-guard against
; os88ui.inc renumbering the scroll bar's parts) and OS88LINE_SZ in a `resb`.
; nasm forward-resolves a LABEL - every `call os88ui_btn` in that file would
; have been fine either way - but it evaluates a %if where it stands and sizes
; a `resb` where it sees it. With -w+error=forward the second one is an error
; rather than a scratch block of the wrong size under every input the painter
; draws, which is the failure that would have shipped.
;
; os88line.inc after os88ui.inc: it calls into that file, and its header says
; so. apps/browser/browser.asm is the worked example of this exact pair.
%include "os88ui.inc"
%include "os88line.inc"

; --- the hand-written cores (SPEC.md 73.11: the inner loop is assembly) ------
; AFTER the compiled C, because the C names them and nasm resolves a forward
; reference to a label but not a section that has not been opened yet. Their
; own headers carry the segment rules; both leave DS, ES and DF as they found
; them, and both switch back to .text before they end (CLAUDE.md's section
; discipline - the next %include otherwise lands in the wrong section).
%include "weave/wblob.inc"          ; reading the bundle claim
%include "weave/wdraw.inc"          ; the paint core (WEAVE-SPEC 1.2's seam)
%include "weave/wui.inc"            ; the alert, the arm word, and the bridge
                                    ; the bytecode core leaves through. AFTER
                                    ; os88ui.inc, which it calls into.
%include "weave/wvm.inc"            ; ...and the WJS VM (WEAVE-SPEC 4), which
                                    ; names wui.inc's wvm_native and is
                                    ; %included UNCHANGED by
                                    ; apps/weave/hosttest/weavevm.asm - what
                                    ; that gate runs is this text and not a
                                    ; copy of it (WEAVE-SPEC 12.3)
%include "weave/wfx.inc"            ; the FX formula VM (WEAVE-SPEC 5), the
                                    ; second core the same boot sector runs
                                    ; unchanged (12.1.2)
%include "weave/wband.inc"          ; ...and the grid's band composer (6.9.1),
                                    ; apps/runcpm/rcband.inc's shape and
                                    ; PERFORMANCE.md Set 68's constants
%include "weave/wsmabi.inc"         ; the WEAVE.WSM contract (1.2.2) - the ONE
                                    ; file this assembly and apps/weave/
                                    ; wcanvas.asm share, and the reason a
                                    ; separate `nasm -f bin` job can be
                                    ; trusted to agree with this one about
                                    ; anything at all
%include "weave/wcv.inc"            ; ...and the three-routine seam to it. The
                                    ; module is NOT in this image: it is read
                                    ; once at open, into a claim of its own,
                                    ; and only when the bundle declares a
                                    ; <canvas>

; --- THE DRIFT GUARDS, and they are %if and not a comment --------------------
; weave.h carries a C copy of two of wvm.inc's own numbers, because a C file
; may not name an nasm equ. A copy that goes stale is the class of defect this
; whole tree writes guards for (os88.h's own count line was wrong by three),
; and here it would be silent: the native block's fields would be read at the
; wrong offsets and every GETP would answer nonsense.
%if WN_SIZE != 54
  %error "wvm.inc's WN_SIZE moved; weave.h's WN_SIZE and WNW_* must follow"
%endif
%if OS88LINE_SZ != 20
  %error "os88line.inc's block moved; wact.c's LNW_* must follow"
%endif
%if WG_STRIDE < 90
  %error "wband.inc's WG_STRIDE must cover the widest content grid (7.1.1)"
%endif
%if WFX_HDR != 16 || WFX_CELL != 4
  %error "wfx.inc's cell record moved; wgrid.c's WG_CHDR/WG_CELL must follow"
%endif

; ...and the same guard over WEAVE.WSM's ABI, which is the one contract in
; this package whose two readers are in DIFFERENT ASSEMBLIES. weave.h carries
; a C copy of the verb numbers, the parameter block and the state block's
; offsets, because a C file may not name an nasm equ; a copy that went stale
; here would read the frame counter out of the middle of the staging ring,
; assemble cleanly and run wrong.
%if WSMV_BIND != 0 || WSMV_SPRITE != 1 || WSMV_START != 2 || WSMV_STOP != 3
  %error "wsmabi.inc's verbs moved; weave.h's WSMV_* must follow"
%endif
%if WSMV_PAINT != 4 || WSMV_DRAIN != 5 || WSMV_UNBIND != 6 || WSMV_PLACE != 8
  %error "wsmabi.inc's verbs moved; weave.h's WSMV_* must follow"
%endif
%if WSMP_SIZE != 16 || WSMP_CID != 12 || WSMP_COLOR != 14
  %error "wsmabi.inc's BIND block moved; weave.h's WSMP_NW must follow"
%endif
%if WSMF_DESC != 7 || WSMF_NFRAME != 6 || WSMF_COLOR != 8
  %error "wsmabi.inc's sprite fields moved; weave.h's WSMF_* must follow"
%endif
%if WSS_RUN != 0 || WSS_FRAME != 6 || WSS_BLITS != 18 || WSS_FRAMES != 20
  %error "wsmabi.inc's state block moved; weave.h's WSS_* must follow"
%endif
%if WSS_OVF != 22 || WSM_MAXSPR != 16
  %error "wsmabi.inc's state block moved; weave.h's WSS_* must follow"
%endif

    CC_IMAGE_END                    ; cc_bss_end, cc_modc_end and cc_image_end
