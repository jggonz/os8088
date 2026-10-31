; =============================================================================
; os8088 - apps/win16/win16.asm
;
; WIN16 (SPEC.md 124): a Windows 3.x program in an os8088 window. A Windows
; program is 8086 machine code for the processor already running, and Windows
; 1.x-3.0 ran such programs in real mode, so this package emulates no CPU: it
; loads the NE image, points the program's imports at code of its own, and
; runs it as a COROUTINE of the UI task (SPEC.md 124.2) - the program has the
; machine from a resume until it next waits, which is Windows 3.x's own
; cooperative contract.
;
; Launched with a document: DOS.O88 owns .EXE (SPEC.md 96.1) and hands an NE
; file to WIN16.O88 through OSAPI_PKG_START's document arm (SPEC.md 124.3).
; Launched empty, it says how to use it.
;
; S2 (docs/plans/WIN16-PLAN.md) is LOAD OR REFUSE: the loader (w16load.inc),
; the gate and the coroutine (w16core.inc), DOS's int 21h as the MS C runtime
; calls it (w16i21.inc), and the first KERNEL and USER functions (w16api.inc).
;
; LICENCE: apps/win16/ is GPL-2.0-or-later (SPEC.md 124.1, WIN16-PLAN.md D7):
; the loader follows winevdm's krnl386/ne_segment.c and task.c, which are
; Wine's (LGPL-2.1-or-later) and winevdm's (GPL-2). See COPYING.
; =============================================================================

%include "os88api.inc"
%include "doscall.inc"              ; where the DOS core goes (SPEC.md 124.21.1)
%include "win16/w16dos.inc"         ; ...and WIN16's host above it

    OS88_HEADER 'WIN16', w16_entry, OS88_F_ICON | OS88_F_PARTS

; --- the icon (SPEC.md 20.2, 124.18.3): a window with a title bar and the
; four-pane flag in it, 1bpp, the same on every adapter. It is the dock's tile
; for every program WIN16 runs, and the Disk window's for WIN16.O88; the
; program's own icons are §124.18.4's, drawn inside its window
    OS88_ICON16
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0000, 0x0000
    dw 0xFFFF, 0xFFFF, 0x8001, 0x8001, 0x9EF1, 0x9EF1, 0x9EF1, 0x8001
    dw 0x9EF1, 0x9EF1, 0x9EF1, 0x8001, 0x8001, 0xFFFF, 0x0000, 0x0000
    OS88_ICON16_END

; --- the states -------------------------------------------------------------
W16S_IDLE    equ 0              ; launched empty
W16S_LOAD    equ 1              ; a document is waiting for the wake handler
W16S_REFUSED equ 2              ; [w16_l1]/[w16_l2] say why, and it stays up
W16S_RUN     equ 3              ; the program is running (or waiting)
W16S_ENDED   equ 4              ; it called int 21h AH=4Ch; the window closes

W16_MAXSEG   equ 253            ; segments a program may have: NE's own limit,
                                ; a byte with 0FEh/0FFh taken (SPEC.md 124.20.2;
                                ; Windows Help 3.1 has 102). 10 bytes a segment
W16_FCACHE   equ 8              ; the loader's cluster cache, KB (124.20.1)

; --- sizes the bss below is cut from (w16user.inc, w16gdi.inc) ---------------
W16_MAXCLS   equ 8              ; classes (SPEC.md 124.8)
W16_MAXWND   equ 80             ; windows: a dialog's controls are windows
                                ;   (WinMine's Preferences is 20, the PIF
                                ;   Editor's Advanced 54 beside 23 open)
W16_QLEN     equ 32             ; posted messages (a power of two)
W16_MAXTMR   equ 4              ; timers
W16C_SIZE    equ 30             ; a class record
W16W_SIZE    equ 102            ; a window record
W16_MAXTOP   equ 4              ; top-level windows in os8088 windows of
                                ;   their own (dialogs, SPEC.md 124.8)
W16_TTLSZ    equ 50
W16D_SBUF    equ 256            ; the dialog part's text buffer
W16D_NBUF    equ 16             ; ...and its number's             ; ...each one's title buffer: its os8088
                                ;   window, then up to 47 characters
W16T_SIZE    equ 14             ; a timer record
MSG_SIZE     equ 18             ; a Windows MSG
W16_NSCAN    equ 0x59           ; scan codes with a virtual key
W16_NPTS     equ 32             ; LineTo's point buffer
W16_TXTMAX   equ 80             ; TextOut's longest run
W16_DTMAX    equ 255            ; DrawText's longest string (w16draw.inc)
W16_NOBJ     equ 16             ; brushes and pens made at once
W16_NBMP     equ 16             ; bitmaps at once (SPEC.md 124.15.2)
W16_NMDC     equ 4              ; memory DCs at once
W16_DCMAX    equ 3              ; window DCs open under the current one
W16_SVMAX    equ 2              ; SaveDC's states at once (SPEC.md 124.22.11)
W16S_QN      equ 32             ; SOUND's queue, in notes (SPEC.md 124.15.1:
W16S_QE      equ 6              ;   Windows' 192 bytes), and a note's bytes
W16_BS       equ 96             ; the text band's stride: 720 px, the guard,
                                ;   the spill, to even
W16_GBUF     equ W16_BS * 20    ; the band (20 rows: a menu item is the
                                ;   System font's rows + 2), and
                                ;   SetDIBitsToDevice's 4bpp block, never
                                ;   both at once
W16_RRMAX    equ 64             ; RoundRect's largest corner radius
W16_NMISS    equ 64             ; different imports WIN16 lacks a program may
                                ;   import before the load refuses it
W16_MTXT     equ 24             ; ...and each one's name, "USER.287"
W16_NBIND    equ 384            ; bind slots: the distinct imports a program
                                ;   may bind, each a stub (SPEC.md 124.19.1).
                                ;   Paintbrush 3.1's 270 is the most measured
W16_NPNMAX   equ 8              ; GetProcAddress's names at most (w16api.inc)
W16_NFIXED   equ 4              ; ...slots 0-3 are the image's own:
W16_CTLROW   equ 0              ;   a control's window procedure,
W16_CBROW    equ 1              ;   the callee's way back (SPEC.md 124.2.2),
W16_X87ROW   equ 2              ;   the emulator's missing instruction and
W16_DDPROW   equ 3              ;   DefDlgProc, a dialog class's procedure
W16B_SIZE    equ 4              ; a slot: the argument bytes, the part, the
W16B_ARGB    equ 0              ;   handler's offset in it
W16B_PART    equ 1
W16B_PROC    equ 2
W16B_MISS    equ 0x7F           ; W16B_PART: an import WIN16 lacks, PROC its
                                ;   name's slot in w16_mtxt
W16B_NORET   equ 0x80           ; ...ORed in: it never returns (the census)
W16_NARENA   equ 6              ; global heap arenas at once (w16mem.inc)
W16_NGH      equ 64             ; moveable global handles
W16_MDEPTH   equ 4              ; popups open at once (w16menu.inc)
W16_MLAB     equ 64             ; ...a menu label's characters, and an
W16_MACC     equ 24             ;   accelerator text's
MP_SIZE      equ 16             ; ...an open popup's record

; --- the parts (SPEC.md 20.12, 124.7.1): the System font, one face a display
W16P_SYS16   equ 0              ; VGA
W16P_SYS12   equ 1              ; EGA and Hercules
W16P_SYS08   equ 2              ; CGA
W16P_DLG     equ 3              ; THE LIBRARY PART (SPEC.md 124.19): the
W16P_LIB     equ W16P_DLG       ;   dialog manager and controls (124.13) and
                                ;   the handlers moved out of the image -
                                ;   code, far-called both ways
W16P_FPU     equ 4              ; the 8087 emulator (SPEC.md 124.5.1): code,
                                ;   reached by the program's int 34h-3Dh
W16P_LOAD    equ 5              ; the loader and the census (SPEC.md 124.18.7):
                                ;   code, far-called once a program
W16P_ICONS   equ 6              ; WIN16's own system icons (SPEC.md 124.18.4):
                                ;   tools/w16icon.py's, from apps/win16/icons/
W16P_DCORE   equ 7              ; WIN16'S DOS (SPEC.md 124.21.1): the DOS
W16P_DHOST   equ 8              ;   core DOS.O88 ships, and the host part above
                                ;   it, copied into one claim at first use

; --- the dialog part's two directions (SPEC.md 124.13). DS = ours on both
; sides of either, so a vector is one word written and one far call, and a
; nested call reads its own vector before anything can write the next.
%macro DCALL 1                  ; image -> part: %1 is a part routine that
    mov word [w16_pfv], %1      ;   ends in retf
    call far [w16_pfv]
%endmacro
%macro ICALL 1                  ; part -> image: %1 is an image routine that
    mov word [w16_ifn], %1      ;   ends in ret, near-called by w16_ibgate
    call far [w16_ibv]
%endmacro
%macro LCALL 1                  ; image -> library part (SPEC.md 124.19.2):
    mov word [w16_lfn], %1      ;   %1 is a NEAR routine there, ending in
    call w16_lcall              ;   ret, near-called by w16_lbgate. The part
%endmacro                       ;   is fetched first if it is not here
%macro LCONT 1                  ; in the library part: %1 is a CONTINUATION
section .text                   ;   (SPEC.md 124.2.2) that is a NEAR routine,
%1_c:                           ;   handed out as %1_c - its trampoline in the
    LCALL %1                    ;   image, which w16_cbdone's `jmp si` can
    ret                         ;   reach (SPEC.md 124.19.3)
section .w16dlg
%1:
%endmacro
%macro W16LIB 1                 ; in the library part: %1 is a HANDLER that
%define w16lib_%1 1             ;   a row names, near, ending in ret - the
%1:                             ;   row's part says so (SPEC.md 124.19.1)
%endmacro
%macro DCONT 1                  ; in the part: %1 is a CONTINUATION
section .text                   ;   (SPEC.md 124.2.2), which the gate enters
%1_c:                           ;   with a near jump in OUR segment - so SI
    call w16_dlgin              ;   names its trampoline %1_c, and %1 ends in
    DCALL %1                    ;   retf. The part is fetched again if it was
    ret                         ;   dropped while the call it waits on ran
section .w16dlg
%1:
%endmacro

; --- bss (SPEC.md 21 step 5: zeroed by the loader) ---------------------------
%assign W16B 0
%macro W16BSS 2                 ; after the parts standard's OP_BSS (os88parts.inc)
%1 equ os88_image_end + OP_BSS + W16B
%assign W16B W16B + (%2)
%endmacro
    W16BSS w16_win, 2           ; our window
    W16BSS w16_state, 1         ; W16S_*
    W16BSS w16_locked, 2        ; 1 = the callback that resumed holds the lock
                                ;   (a word: saved and put back whole)
    W16BSS w16_name, 14         ; the program's 8.3 name, NUL-terminated
    W16BSS w16_dir, 2           ; ...the folder it is in
    W16BSS w16_vol, 1           ; ...and the volume
    W16BSS w16_l1, 48           ; the two lines the window shows
    W16BSS w16_l2, 48
    W16BSS w16_fseg, 2          ; the NE header and its tables, then every
                                ;   segment's relocations (SPEC.md 124.20.1;
                                ;   freed before the run)
    W16BSS w16_hpos, 2          ; ...the bytes of it in use
    W16BSS w16_hkb, 2           ; ...and the KB claimed
    W16BSS w16_rtoff, 2 * W16_MAXSEG    ; each segment's relocations in it
    W16BSS w16_cseg, 2          ; the cluster cache's claim (0 = none)
    W16BSS w16_csz, 2           ; ...its bytes
    W16BSS w16_cbase, 4         ; ...the file offset it holds
    W16BSS w16_clen, 2          ; ...and the bytes of it valid
    W16BSS w16_soff, 4          ; a segment's offset in the file
    W16BSS w16_hdr, 64          ; the MZ header, then the NE header
    W16BSS w16_pseg, 2          ; the program's ONE claim
    W16BSS w16_ne, 2            ; the NE header's offset in [w16_fseg]: 0
    W16BSS w16_nseg, 2          ; segments
    W16BSS w16_sbase, 2 * W16_MAXSEG    ; each segment's paragraph
    W16BSS w16_slen, 2 * W16_MAXSEG     ; ...and its size in bytes (0 = 64KB)
    W16BSS w16_dgroup, 2        ; DGROUP's paragraph, which is hInstance
    W16BSS w16_dgsize, 2        ; ...its size in bytes (0 = 64KB)
    W16BSS w16_lbase, 2         ; ...where its local heap starts: past the
                                ;   static data and the stack (SPEC.md 124.9)
    W16BSS w16_dgfull, 1        ; 1 = lay DGROUP out at 64KB (w16_layout)
    W16BSS w16_psp, 2           ; the PSP-shaped block's paragraph
    W16BSS w16_ptop, 2          ; the paragraph past the claim
    W16BSS w16_nmod, 2          ; the program's module-reference count
    W16BSS w16_modid, 2 * 16    ; ...each one's W16M_* (0xFF = not ours)
    W16BSS w16_cpu186, 1        ; 1 = this CPU runs 186 code (SPEC.md 124.3)
    W16BSS w16_cbm, 2 * W16_MAXSEG      ; the census: each code segment's bitmap
    W16BSS w16_clast, 2 * W16_MAXSEG    ; ...and its last offset (w16cens.inc)
    ; --- the coroutine (w16core.inc) ---
    W16BSS w16_hss, 2           ; the UI task's SS (LOW_SEG) ...
    W16BSS w16_hsp, 2           ; ...and its SP at the resume
    W16BSS w16_hflags, 2        ; ...and its FLAGS, for the way back
    W16BSS w16_pss, 2           ; the program's SS:SP while it waits
    W16BSS w16_psp_, 2
    W16BSS w16_rax, 2           ; the register bank the epilogue loads
    W16BSS w16_rdx, 2
    W16BSS w16_rbx, 2
    W16BSS w16_rcx, 2
    W16BSS w16_res, 2
    W16BSS w16_rsi, 2           ; ...and SI/DI, written into the frame when
    W16BSS w16_rdi, 2           ;   [w16_rflags] says so (InitTask)
    W16BSS w16_rflags, 1
    W16BSS w16_pend, 1          ; 1 = the handler is waiting: yield
    W16BSS w16_nargb, 2         ; bytes the finishing call pops (-2: InitTask)
    W16BSS w16_idx, 2           ; the bind slot the gate is serving
    W16BSS w16_old21, 4         ; the vector int 21h had before the resume
    W16BSS w16_vecs, 5 * 8      ; int 21h AH=25h's vectors: number, far ptr
    ; --- S16: the DOS (SPEC.md 124.21) ---
    W16BSS w16_dhseg, 2         ; the DOS's claim, 0 = not fetched yet
    W16BSS w16_dhtmp, 2         ; ...while it is being made
    W16BSS w16_dhwin, 2         ; ...and its read window's
    W16BSS w16_dhent, 4         ; the far entry: W16DH_I21 (or _INIT)
    W16BSS w16_i21ss, 2         ; the program's stack, across a fetch
    W16BSS w16_i21sp, 2
    W16BSS w16f_style, 2        ; OpenFile's wStyle
    W16BSS w16f_ofs, 4          ; ...its OFSTRUCT
    W16BSS w16f_num, 2          ; GetTempFileName: where the digits go
    W16BSS w16f_unq, 2          ; ...and the number
    W16BSS w16_dces, 2          ; the ES the DOS's last call answered
    W16BSS w16f_ddl, 2          ; DlgDirList's wFiletype
    W16BSS w16f_spec, 2         ; ...the file part it searches with
    W16BSS w16f_odta, 4         ; ...the program's DTA, while it uses its own
    W16BSS w16f_dta, 44         ; ...which is this one
    W16BSS w16f_item, 144       ; ...and one item's text
    W16BSS w16f_name, 128       ; a name as the program gave it
    W16BSS w16f_path, 144       ; ...and as a full path
    W16BSS w16_exit, 1          ; AH=4Ch's AL
    W16BSS w16_mbtext, 64       ; MessageBox's text, one line, in OUR segment
    W16BSS w16_scratch, 16
    ; --- calls into the program (w16core.inc, SPEC.md 124.2.2) ---
    W16BSS w16_cbax, 2          ; the callee's answer, DX:AX
    W16BSS w16_cbdx, 2
    W16BSS w16_cbgo, 1          ; 1 = the gate enters the callee
    ; --- USER (w16user.inc, SPEC.md 124.8) ---
    W16BSS w16_inget, 1         ; 1 = waiting in GetMessage
    W16BSS w16_inpeek, 1        ; 1 = ...in a PeekMessage, for the wake
    W16BSS w16_mvcx, 2          ; MoveWindow's size and window
    W16BSS w16_mvcy, 2
    W16BSS w16_mvhwnd, 2
    W16BSS w16_inpaint, 1       ; 1 = resumed inside our W_PAINT
    W16BSS w16_ltook, 1         ; 1 = w16_lock took the gfx lock
    W16BSS w16_top, 1           ; the top-level window's slot + 1, 0 = none
    W16BSS w16_erase, 1
    W16BSS w16_take, 1
    W16BSS w16_titleptr, 2      ; what our title is when no alert is up
    W16BSS w16_wtitle, 48       ; ...the program window's text
    W16BSS w16_namebuf, 16      ; a class name, folded
    W16BSS w16_cwargs, 2        ; CreateWindow's arguments, on its stack
    W16BSS w16_cwslot, 2
    W16BSS w16_cls, W16_MAXCLS * W16C_SIZE
    W16BSS w16_wnd, W16_MAXWND * W16W_SIZE
    W16BSS w16_q, W16_QLEN * MSG_SIZE
    W16BSS w16_qhead, 1
    W16BSS w16_qcount, 1
    W16BSS w16_quit, 1          ; 1 = PostQuitMessage was called
    W16BSS w16_quitcode, 2
    W16BSS w16_tmr, W16_MAXTMR * W16T_SIZE
    W16BSS w16_tproc, 4         ; the TimerProc DispatchMessage calls
    W16BSS w16_msg, MSG_SIZE    ; the message being delivered
    W16BSS w16_mx, 2            ; the last pointer position, screen
    W16BSS w16_my, 2
    W16BSS w16_kascii, W16_NSCAN    ; each scan code's last ASCII
    W16BSS w16_capture, 2
    W16BSS w16_car, 16          ; the caret (w16car.inc, SPEC.md 124.22.9)
    W16BSS w16_sbtrk, 2         ; a scroll bar's press (SPEC.md 124.22.1):
    W16BSS w16_sbbar, 1         ;   its window, 0 none; SB_HORZ or SB_VERT;
    W16BSS w16_sbcode, 1        ;   the SB_* it sends; the tick it repeats at;
    W16BSS w16_sbdue, 2         ;   the thumb's position as it is dragged and
    W16BSS w16_sbdpos, 2        ;   the press's offset inside the thumb
    W16BSS w16_sbgrab, 2
    W16BSS w16_sbg, 16          ; a bar's geometry, w16_sbgeom's (w16sb.inc)
    W16BSS w16_sm_hwnd, 2       ; the message being sent or posted
    W16BSS w16_sm_msg, 2
    W16BSS w16_sm_wp, 2
    W16BSS w16_sm_lp, 4
    W16BSS w16_dmg, 8           ; W_PAINT's damage, screen, inclusive
    ; --- GDI (w16gdi.inc) ---
    W16BSS w16_dcon, 1          ; 1 = a DC is open (w16_dcdep more under it)
    W16BSS w16_dctook, 1        ; 1 = the DC took the gfx lock
    W16BSS w16_dcclip, 1        ; 1 = ...and armed the kernel's clip
    W16BSS w16_dcdep, 1         ; DCs open UNDER the current one (SPEC.md
    W16BSS w16_gcol, 1          ;   124.20.4: a GetDC inside a GetDC)
    W16BSS w16_objk, W16_NOBJ   ; each made object's W16K_*, 0 = free
    W16BSS w16_objc, W16_NOBJ   ; ...and its colour
    ; --- one DC's state, saved whole when another opens over it ---
    W16BSS w16_dchide, 1        ; 1 = nothing of the window is visible
    W16BSS w16_dcpaint, 1
    W16BSS w16_dcwnd, 2
    W16BSS w16_ox, 2            ; the client's origin, screen
    W16BSS w16_oy, 2
    W16BSS w16_cl, 2            ; WIN16's clip, screen, inclusive
    W16BSS w16_ct, 2
    W16BSS w16_cr, 2
    W16BSS w16_cb, 2
    W16BSS w16_prc, 8           ; rcPaint, client
    W16BSS w16_pen, 2
    W16BSS w16_brush, 2
    W16BSS w16_font, 2
    W16BSS w16_curx, 2
    W16BSS w16_cury, 2
    W16BSS w16_rop, 1           ; SetROP2's mode
    W16BSS w16_tcol, 1          ; the text colour, and its COLORREF
    W16BSS w16_trgb, 4
    W16BSS w16_brgb, 4          ; SetBkColor's COLORREF (w16_tpap its colour)
    W16BSS w16_bkmode, 1        ; SetBkMode's
    W16BSS w16_tal, 1           ; SetTextAlign's
    W16BSS w16_tpap, 1          ; text's paper colour (w16_txtdraw)
    W16BSS w16_brorg, 4         ; SetBrushOrg's
W16_DCSZ equ os88_image_end + OP_BSS + W16B - w16_dchide
    W16BSS w16_dcstk, W16_DCMAX * W16_DCSZ ; the DCs open under it, innermost
                                ;   last
W16_TRN equ 2048                   ; the trace ring's entries, a power of two
%ifdef W16_TRACE                   ; a debugging knob: the last W16_TRN gate rows
    W16BSS w16_trn, 2           ;   and int 21h calls (80h:AH), which
    W16BSS w16_trc, 2 * W16_TRN ;   tools/w16trace.py reads (SPEC.md 124.17)
    W16BSS w16_trow, 2 * W16_NBIND ; ...each bind slot's export row, FFFFh
                                ;   for none (a missing import, a fixed slot)
%endif
    W16BSS w16_ckhm, 2          ; int 21h 2Ch's anchor: the kernel's hour:minute
    W16BSS w16_cks, 1           ; ...second
    W16BSS w16_ckt, 2           ; ...and the BIOS tick it was seen at
    W16BSS w16_rgb, 4
    W16BSS w16_lx1, 2
    W16BSS w16_ly1, 2
    W16BSS w16_ldx, 2
    W16BSS w16_ldy, 2
    W16BSS w16_lsx, 2
    W16BSS w16_lsy, 2
    W16BSS w16_npts, 2
    W16BSS w16_pts, W16_NPTS * 4
    W16BSS w16_rl, 2
    W16BSS w16_rt, 2
    W16BSS w16_rr, 2
    W16BSS w16_rb, 2
    W16BSS w16_tx, 2
    W16BSS w16_ty, 2
    W16BSS w16_txt, W16_TXTMAX + 2
    W16BSS w16_tt, 22           ; TabbedTextOut's walk (w16acc.inc): the
                                ;   string, the count left, the pen, the
                                ;   tab origin, the default stop, the stops,
                                ;   their count and whether it draws
    ; --- S4: the display, the face, text, RoundRect, DIBs ---
    W16BSS w16_disp, 1          ; the window's VID_* kind (VESA reads as VGA)
    W16BSS w16_mono, 1          ; 1 = a 1bpp display
    W16BSS w16_scrw, 2          ; ...its size
    W16BSS w16_scrh, 2
    W16BSS w16_capn, 2          ; SM_CYCAPTION - 1: what CreateWindow takes off
    W16BSS w16_home, 2          ; the folder WIN16.O88 is in, and its volume
    W16BSS w16_homev, 1
    W16BSS w16_face, 2          ; the System font part's segment, 0 = none
    W16BSS w16_fpart, 1         ; ...which part, + 1
    W16BSS w16_tn, 2            ; TextOut's characters
    W16BSS w16_tnb, 2           ; ...the band's bytes a row
    W16BSS w16_tb0, 2           ; ...the screen x of its bit 0
    W16BSS w16_r0, 2            ; ...the glyph rows drawn
    W16BSS w16_r1, 2
    W16BSS w16_gbpr, 2          ; ...a glyph's bytes a row (the word's high
                                ;   byte stays 0)
    W16BSS w16_son, 1           ; w16_span's growing rectangle: 1 = one is
    W16BSS w16_sx1, 2           ;   open, and its corners
    W16BSS w16_sx2, 2
    W16BSS w16_sy1, 2
    W16BSS w16_sy2, 2
    W16BSS w16_rrx, 2           ; RoundRect's radii, its pen, its insets
    W16BSS w16_rry, 2
    W16BSS w16_rpen, 1
    W16BSS w16_rrin, W16_RRMAX
    W16BSS w16_dres, 2          ; SetDIBitsToDevice: the rows drawn
    W16BSS w16_dwid, 2          ; ...the DIB's width, height, depth, palette
    W16BSS w16_dhgt, 2          ;   entry size and row stride
    W16BSS w16_dbpp, 1
    W16BSS w16_dpal, 2
    W16BSS w16_dstride, 2
    W16BSS w16_dx0, 2           ; ...the destination's top left, screen
    W16BSS w16_dy0, 2
    W16BSS w16_dsx, 2           ; ...the source column at its left, and row
    W16BSS w16_dtop, 2          ;   at its top (bottom up)
    W16BSS w16_dstart, 2        ; ...the rows lpBits holds
    W16BSS w16_dlines, 2
    W16BSS w16_dbits, 4
    W16BSS w16_dvw, 2           ; ...the visible width, its 4bpp bytes, the
    W16BSS w16_d4s, 2           ;   rows a block holds and the block's top
    W16BSS w16_dband, 2
    W16BSS w16_dby, 2
    W16BSS w16_dmap, 16         ; ...its palette, as os8088 colours
    W16BSS w16_gbuf, W16_GBUF
    ; --- S5: ending, memory, resources, menus ---
    W16BSS w16_nmiss, 2         ; the missing imports bound (SPEC.md 124.12)
    W16BSS w16_missi, 2         ; ...the one called
    W16BSS w16_endmsg, 1        ; 1 = it ended with [w16_l1]/[w16_l2] to show
    W16BSS w16_mtxt, W16_NMISS * W16_MTXT
    W16BSS w16_heap, 2          ; ne_heap: InitTask's local heap
    W16BSS w16_arena, 2 * W16_NARENA    ; the global heap (w16mem.inc)
    W16BSS w16_arenae, 2 * W16_NARENA
    W16BSS w16_ghseg, 2 * W16_NGH
    W16BSS w16_ghf, 2 * W16_NGH
    W16BSS w16_gflg, 1
    W16BSS w16_gdisc, 1
    W16BSS w16_gnp, 2
    W16BSS w16_gold, 2
    W16BSS w16_lflg, 1
    W16BSS w16_lh, 2
    W16BSS w16_clus, 2          ; the program's volume's cluster, bytes
    W16BSS w16_rtab, 2          ; the kept resource table (w16res.inc)
    W16BSS w16_rtlen, 2
    W16BSS w16_rtk, 2           ; FindResource's keys and strings
    W16BSS w16_rnk, 2
    W16BSS w16_rtypb, 16
    W16BSS w16_rnamb, 16
    W16BSS w16_rlen, 2          ; w16_rread's resource and span
    W16BSS w16_rpre, 2
    W16BSS w16_roff, 4
    W16BSS w16_rspanb, 4
    W16BSS w16_rh, 2
    W16BSS w16_rdat, 2
    W16BSS w16_rback, 2
    W16BSS w16_rbackv, 1
    W16BSS w16_rdst, 4          ; LoadString's buffer, its size, its ID
    W16BSS w16_rmax, 2
    W16BSS w16_rsid, 2
    ; --- S5: menus and accelerators (w16menu.inc) ---
    W16BSS w16_mseg, 2          ; the USER heap's segment, and its handle
    W16BSS w16_mhh, 2
    W16BSS w16_mstr, 4          ; an item's string, far
    W16BSS w16_mtp, 2           ; a template being parsed
    W16BSS w16_mtps, 2
    W16BSS w16_mlab, W16_MLAB + 2   ; an item's label, prepared
    W16BSS w16_mlabn, 2
    W16BSS w16_macc, W16_MACC + 2   ; ...its accelerator text
    W16BSS w16_maccn, 2
    W16BSS w16_mmn, 1           ; ...its mnemonic's index, and the letter
    W16BSS w16_mmch, 1
    W16BSS w16_mhelp, 1
    W16BSS w16_mlw, 2           ; layout: the width, a line's height
    W16BSS w16_mlh, 2
    W16BSS w16_msys, 2          ; GetSystemMenu's
    W16BSS w16_mclip, 1         ; w16_mgon: the clip armed, the lock taken
    W16BSS w16_mtook, 1
    W16BSS w16_mcx0, 2          ; ...the content's origin
    W16BSS w16_mcy0, 2
    W16BSS w16_mbh, 2           ; the band's rows, ink and paper
    W16BSS w16_mink, 1
    W16BSS w16_mpap, 1
    W16BSS w16_mbfb, 2
    W16BSS w16_mblb, 2
    W16BSS w16_mpidx, 1         ; w16_mpitem's item, the selection, places
    W16BSS w16_mpsel, 1
    W16BSS w16_mpcl, 2
    W16BSS w16_mpct, 2
    W16BSS w16_mx0, 2           ; a popup's frame on the screen
    W16BSS w16_my0, 2
    W16BSS w16_mx1, 2
    W16BSS w16_my1, 2
    W16BSS w16_mrec, 2
    W16BSS w16_mpl, W16_MDEPTH * MP_SIZE    ; the open popups
    W16BSS w16_mlev, 1          ; ...how many
    W16BSS w16_mtrk, 1          ; 1 = the menu loop runs
    W16BSS w16_mkbd, 1          ; ...entered from the keyboard, with this
    W16BSS w16_mkch, 1
    W16BSS w16_mbsel, 1         ; ...the title selected, 0FFh none
    W16BSS w16_mstale, 1        ; ...a W_PAINT came: the banks are stale
    W16BSS w16_meat, 1          ; 1 = this press is the menu's
    W16BSS w16_inwp, 1          ; 1 = inside w16_ppaint
    W16BSS w16_mhbar, 2         ; ...the bar, the window
    W16BSS w16_mhwnd, 2
    W16BSS w16_midx, 2          ; ...the DefWindowProc it waits in
    W16BSS w16_mnargb, 2
    W16BSS w16_mpsp, 2
    W16BSS w16_mnmenu, 2        ; the popup to open next, its place, where
    W16BSS w16_mnpos, 2
    W16BSS w16_mnx, 2
    W16BSS w16_mny, 2
    W16BSS w16_mptx, 2          ; the press that began it, screen
    W16BSS w16_mpty, 2
    W16BSS w16_mhl, 1           ; w16_mhit's
    W16BSS w16_mhx, 2
    W16BSS w16_mhy, 2
    W16BSS w16_ahwnd, 2         ; TranslateAccelerator's
    W16BSS w16_aacc, 2
    W16BSS w16_acmd, 2
    W16BSS w16_asub, 2
    W16BSS w16_apos, 2
    W16BSS w16_arec, 2
    W16BSS w16_ahm, 2
    W16BSS w16_kmods, W16_NSCAN ; each scan code's shift state when pressed
    W16BSS w16_closeq, 1        ; 1 = a WM_CLOSE of ours is not taken yet
    W16BSS w16_ctop, 2          ; w16_ppaint: the client's top, screen
    ; --- S6: the dialog part (w16dlg.inc, SPEC.md 124.13) ---
    W16BSS w16_dseg, 2          ; its segment, 0 = not fetched
    ; --- S9: floating point (w16x87.inc, SPEC.md 124.5) ---
    W16BSS w16_fpm, 1           ; W16FP_*: none, the emulator, the coprocessor
    W16BSS w16_nfix, 2          ; pass 0: OS fixups and __FPMATH imports
    W16BSS w16_fpseg, 2         ; the emulator part's segment, 0 = not here
    W16BSS w16x_new, 40         ; its ten vectors, int 34h..3Dh
    W16BSS w16x_old, 44         ; the machine's 34h..3Dh and 2
    W16BSS w16x_hand, 4         ; the runtime's handler (__FPMATH 3), native
    W16BSS w16x_q, 1            ; a code int 2 kept for int 3Dh
    W16BSS w16x_kept, 2         ; __FPMATH 8's exceptions
    W16BSS w16x_ref, 2          ; __FPMATH 0/2's count
    W16BSS w16x_sw, 2           ; the coprocessor's words, read
    W16BSS w16x_cw, 2
    W16BSS w16x_cw2, 2
    W16BSS w16x_env, 14         ; fnstenv's, and fistp's dword
    W16BSS w16d_menu, 4         ; a dialog template's menu (w16d_ownwin)
    W16BSS w16_clpopen, 1       ; the clipboard: open (SPEC.md 124.16.1)
    W16BSS w16_clpown, 2        ; ...by this window
    W16BSS w16_clph, 2          ; ...GetClipboardData's block, which it owns
    W16BSS w16_scase, 1         ; lstrcmp: 1 case decides a tie
    W16BSS w16_cursor, 2        ; SetCursor's, answered by the next
    W16BSS w16_curcnt, 2        ; ShowCursor's count
    W16BSS w16_pfv, 4           ; DCALL's vector: a routine in it, its segment
    W16BSS w16_ibv, 4           ; ICALL's: w16_ibgate and our CS
    W16BSS w16_ifn, 2           ; ...and the image routine w16_ibgate calls
    W16BSS w16_lbv, 4           ; LCALL's: w16_lbgate and the library part's
    W16BSS w16_lfn, 2           ;   segment, and the routine it calls
    ; --- S14: the bind table (SPEC.md 124.19.1) ---
    W16BSS w16_nbind, 2         ; slots given out
    W16BSS w16_pnslot, 2 * W16_NPNMAX ; ...GetProcAddress's names', 0 none yet
    W16BSS w16_bind, W16B_SIZE * W16_NBIND
    ; --- S6: the window tree (w16wnd.inc, SPEC.md 124.8) ---
    W16BSS w16_focus, 2         ; the window with the keyboard focus, 0 none
    ; --- S17: focus notifications sent from SetFocus (SPEC.md 124.22.6) ---
    W16BSS w16_nfdefer, 1       ; 1: inside a program's SetFocus
    W16BSS w16_nfn, 1           ; the notifications it raised
    W16BSS w16_nfi, 1           ; ...and the next one to send
    W16BSS w16_nfq, 8 * 2       ; each its parent, wParam and lParam
    ; --- S17: SaveDC's states (SPEC.md 124.22.11) ---
    W16BSS w16_svn, 1           ; how many are saved
    W16BSS w16_svdc, W16_SVMAX * W16_DCSZ
    ; --- w16draw.inc (SPEC.md 124.20.4) ---
    W16BSS w16_dtr, 8           ; DrawText's rectangle (IntersectRect's,
                                ;   FrameRect's scratch too)
    W16BSS w16_dtfmt, 2         ; ...its DT_* flags
    W16BSS w16_dtn, 2           ; ...the characters kept
    W16BSS w16_dth, 2           ; ...the height drawn so far
    W16BSS w16_dtw, 2           ; ...the widest line
    W16BSS w16_dtfh, 2          ; ...the font's height
    W16BSS w16_dty, 2           ; ...the next line's y, client
    W16BSS w16_dtclip, 8        ; ...the DC's clip, banked
    W16BSS w16_dtdraw, 1        ; ...1 = drawing, not DT_CALCRECT
    W16BSS w16_dtc, 2           ; ...one character measured
    W16BSS w16_dtbuf, W16_DTMAX + 1     ; ...the string, its prefixes out
    W16BSS w16_ddx, 2           ; LineDDA's point
    W16BSS w16_ddy, 2
    W16BSS w16_ddxa, 2          ; ...its steps, +1 or -1
    W16BSS w16_ddya, 2
    W16BSS w16_dderr, 2         ; ...Bresenham's error and its two adds
    W16BSS w16_ddea, 2
    W16BSS w16_ddeb, 2
    W16BSS w16_ddcnt, 2         ; ...the points still to call
    W16BSS w16_ddmaj, 1         ; ...1 = x is the major axis
    W16BSS w16_ddfn, 4          ; ...the callback
    W16BSS w16_ddat, 4          ; ...and its lpData
    W16BSS w16_gpix, 4          ; GetPixel's one screen pixel, a byte a plane
    W16BSS w16_kl, 2            ; w16_wclip's answer, screen, inclusive
    W16BSS w16_kt, 2
    W16BSS w16_kr, 2
    W16BSS w16_kb, 2
    W16BSS w16_hx, 2            ; w16_hittest's point, and the level's
    W16BSS w16_hy, 2            ;   client origin and HWND
    W16BSS w16_hox, 2
    W16BSS w16_hoy, 2
    W16BSS w16_hpar, 2
    W16BSS w16_ivb, 2           ; w16_invaltree's child border
    W16BSS w16_fdx, 2           ; os8088's frame round a content box
    W16BSS w16_fdy, 2
    W16BSS w16_fox, 2
    W16BSS w16_foy, 2
    W16BSS w16_nwx, 2           ; w16_newosw's client, screen
    W16BSS w16_nwy, 2
    W16BSS w16_nwcx, 2
    W16BSS w16_nwcy, 2
    W16BSS w16_otpl, 16         ; ...and its os8088 window template
    W16BSS w16_ttab, W16_MAXTOP * W16_TTLSZ ; second windows' titles
    W16BSS w16_pfosw, 2         ; the os8088 window being painted (w16_ppaint)
    W16BSS w16_inmodal, 1       ; the innermost modal dialog's slot + 1
    W16BSS w16_seq, 2           ; the next window's W16W_SEQ
    W16BSS w16_inprog, 1        ; 1 = between a resume and its return
    ; --- the dialog part's (w16dlg.inc): it has no bss of its own
    W16BSS w16d_sbuf, W16D_SBUF ; a control's text, as drawn
    W16BSS w16d_pbuf, 32        ; a picture, copied into our segment
    W16BSS w16d_nbuf, W16D_NBUF ; SetDlgItemInt's / GetDlgItemInt's digits
    W16BSS w16d_func, 4         ; the dialog being made: its procedure
    W16BSS w16d_param, 4        ;   ...WM_INITDIALOG's lParam
    W16BSS w16d_dsty, 4         ;   ...its style
    W16BSS w16d_cls, 4          ;   ...its class, far
    W16BSS w16d_ul, 2
    W16BSS w16d_mcnt, 2
    W16BSS w16d_mcw, 2
    W16BSS w16d_ulx, 2
    W16BSS w16d_n, 2
    W16BSS w16d_sty, 2
    W16BSS w16d_l, 2
    W16BSS w16d_t, 2
    W16BSS w16d_r, 2
    W16BSS w16d_b, 2
    W16BSS w16d_w, 2
    W16BSS w16d_h, 2
    W16BSS w16d_rec, 2
    W16BSS w16d_il, 2
    W16BSS w16d_it, 2
    W16BSS w16d_ir, 2
    W16BSS w16d_ib, 2
    W16BSS w16d_lb, 2
    W16BSS w16d_sh, 2
    W16BSS w16d_pic, 2
    W16BSS w16d_bh, 2
    W16BSS w16d_by, 2
    W16BSS w16d_bxx, 2
    W16BSS w16d_e0, 2
    W16BSS w16d_e1, 2
    W16BSS w16d_bar, 1          ; w16sb.inc's: the bar being drawn, whether
    W16BSS w16d_sbonly, 1       ;   only its thumb moved, the window's
    W16BSS w16d_wl, 2           ;   rectangle on the screen
    W16BSS w16d_wt, 2
    W16BSS w16d_wr, 2
    W16BSS w16d_wb, 2
    W16BSS w16e_wtab, 256       ; w16edml.inc's: the System font's advances,
    W16BSS w16e_wface, 2        ;   the face they are of, a tab stop, a row,
    W16BSS w16e_tabw, 2         ;   the text's offset in its segment
    W16BSS w16e_rowh, 2
    W16BSS w16e_base, 2
    W16BSS w16e_ww, 2           ; ...word wrap's width, 0 none (124.22.3)
    W16BSS w16e_lst, 2          ; ...w16e_wline's line
    W16BSS w16e_y, 2            ; ...painting: the line's top, the selection
    W16BSS w16e_s0, 2           ;   shown, the line's start and end, the
    W16BSS w16e_s1, 2           ;   view's left, the pen, the next box's left
    W16BSS w16e_ls, 2           ;   and whether the run is selected
    W16BSS w16e_le, 2
    W16BSS w16e_xo, 2
    W16BSS w16e_x, 2
    W16BSS w16e_bx0, 2
    W16BSS w16e_rsel, 1
    W16BSS w16e_ext, 1          ; ...the caret carries the selection
    W16BSS w16e_noundo, 1       ; ...w16e_repl: keep no step; a typed key
    W16BSS w16e_typing, 1
    W16BSS w16e_src, 4          ;   what goes in, and how much
    W16BSS w16e_cnt, 2
    W16BSS w16e_a, 2            ;   the selection it replaces
    W16BSS w16e_b, 2
    W16BSS w16e_nlen, 2         ;   the length after it
    W16BSS w16e_als, 2          ;   the first changed line's start and index
    W16BSS w16e_aln, 2
    W16BSS w16e_dlf, 2          ;   the line feeds that went and came
    W16BSS w16e_ilf, 2
    W16BSS w16e_ch, 2           ; ...a typed character, or CR LF
    W16BSS w16e_ublk, 2         ; ...EM_UNDO's step, taken off the record
    W16BSS w16_srl, 2           ; ScrollWindow's rectangle, client
    W16BSS w16_srt, 2
    W16BSS w16_srr, 2
    W16BSS w16_srb, 2
    W16BSS w16d_hdlg, 2
    W16BSS w16d_frec, 2
    W16BSS w16d_code, 2
    W16BSS w16d_mch, 2
    W16BSS w16d_dlg, 2
    W16BSS w16d_tseg, 2
    W16BSS w16d_tp, 2
    W16BSS w16d_rent, 2
    W16BSS w16d_par, 2
    W16BSS w16d_modal, 2
    W16BSS w16d_cnt, 2
    W16BSS w16d_tabq, 2
    W16BSS w16d_mbt, 2
    W16BSS w16d_mbk, 2
    W16BSS w16d_mbn, 2
    W16BSS w16d_mbrw, 2
    W16BSS w16d_mbd, 2
    W16BSS w16d_mbh, 2
    W16BSS w16d_mtw, 2
    W16BSS w16d_mtl, 2
    W16BSS w16d_mth, 2
    W16BSS w16d_icon, 2         ; an SS_ICON static's icon, or a message
                                ;   box's (SPEC.md 124.18.4); 0 none
    W16BSS w16d_tx, 2           ; a message box's text: where it starts
    W16BSS w16d_ty, 2
    W16BSS w16d_mby, 2          ; ...and its buttons' top
    W16BSS w16_cwkind, 1        ; w16_mkwnd's arguments (SPEC.md 124.8)
    W16BSS w16_cwcls, 1
    W16BSS w16_cwproc, 4
    W16BSS w16_cwhbr, 2
    W16BSS w16_cwstyle, 4
    W16BSS w16_cwx, 2
    W16BSS w16_cwy, 2
    W16BSS w16_cwcx, 2
    W16BSS w16_cwcy, 2
    W16BSS w16_cwpar, 2
    W16BSS w16_cwid, 2
    W16BSS w16_cwtext, 4
    W16BSS w16_mvx, 2           ; MoveWindow's place, and SWP_NOSIZE/NOMOVE
    W16BSS w16_mvy, 2
    W16BSS w16_mvfl, 1
    W16BSS w16_bmpp, 2          ; w16_bmp's picture
    W16BSS w16_bxl, 2           ; w16_tbox's box, screen, inclusive
    W16BSS w16_bxt, 2
    W16BSS w16_bxr, 2
    W16BSS w16_bxb, 2
    ; --- profiles (SPEC.md 124.14, w16prof.inc) ---------------------------------
    W16BSS w16pf_tab, 104       ; W16PF_N rows of W16PF_SZ (4 x 26)
    W16BSS w16pf_clock, 1       ; the use stamp's clock
    W16BSS w16pf_row, 2         ; the row being worked on
    W16BSS w16pf_end, 2         ;   and its length
    W16BSS w16pf_nm, 13         ; the file's 8.3 name, zero-filled
    W16BSS w16pf_app, 256       ; the section, the key and the value, as the
    W16BSS w16pf_key, 256       ;   program passed them (W16PF_STR)
    W16BSS w16pf_val, 256
    W16BSS w16pf_ins, 784       ; text being composed for the file
    W16BSS w16pf_def, 2         ; GetProfileInt's default
    W16BSS w16pf_fr, 2          ; the frame (a far pointer: these two stay
    W16BSS w16pf_frs, 2         ;   together)
    W16BSS w16pf_dst, 4         ; the program's buffer
    W16BSS w16pf_dn, 2          ;   its size
    W16BSS w16pf_cnt, 2         ;   bytes written to it
    W16BSS w16pf_cut, 1         ;   1 = a list was cut
    W16BSS w16pf_nokey, 1       ; the write's NULLs
    W16BSS w16pf_noval, 1
    W16BSS w16pf_hdrp, 2        ; the section found: its header line, its
    W16BSS w16pf_body, 2        ;   first line, its end and where a new key
    W16BSS w16pf_send, 2        ;   goes
    W16BSS w16pf_last, 2
    W16BSS w16pf_kln, 2         ; the key line found: its start, the key,
    W16BSS w16pf_ks, 2          ;   the key's end, the value, the text's end
    W16BSS w16pf_ke, 2          ;   and the next line
    W16BSS w16pf_kend, 2
    W16BSS w16pf_vs, 2
    W16BSS w16pf_ve, 2
    W16BSS w16pf_lend, 2
    W16BSS w16pf_lnx, 2
    W16BSS w16pf_sp, 2          ; a splice: where, deleted, inserted
    W16BSS w16pf_sd, 2
    W16BSS w16pf_si, 2
    W16BSS w16pf_kb, 2          ;   and a grown claim's KB
    W16BSS w16pf_cs, 2          ; w16pf_cpy's copy, for w16pf_strim
    ; --- wsprintf (w16fmt.inc) ----------------------------------------------
    W16BSS w16w_out, 4          ; the output, the format and the arguments:
    W16BSS w16w_fmt, 4          ;   far pointers that move as they are used
    W16BSS w16w_arg, 4
    W16BSS w16w_sp, 4           ; a %s argument
    W16BSS w16w_cnt, 2          ; characters written
    W16BSS w16w_wid, 2          ; the field: its width, its precision
    W16BSS w16w_prec, 2         ;   (FFFFh = none) and its text's length
    W16BSS w16w_n, 2
    W16BSS w16w_base, 2         ; 10 or 16
    W16BSS w16w_left, 1         ; the flags: '-', '#', 'l', the pad
    W16BSS w16w_alt, 1
    W16BSS w16w_long, 1
    W16BSS w16w_pad, 1
    W16BSS w16w_neg, 1          ; 1 = a signed type, and 1 = it is below 0
    W16BSS w16w_minus, 1
    W16BSS w16w_hexa, 1         ; 'a' - 10 or 'A' - 10
    W16BSS w16w_tch, 1          ; the type's letter
    W16BSS w16w_buf, 12         ; a number's digits, built backwards
    ; --- SOUND (w16snd.inc, SPEC.md 124.15.1)
    W16BSS w16s_q, W16S_QN * W16S_QE    ; the queue: Hz, sounding units, units
    W16BSS w16s_qh, 2           ; ...its head, tail and count
    W16BSS w16s_qt, 2
    W16BSS w16s_qn, 2
    W16BSS w16s_due, 2          ; the tick the next note starts at
    W16BSS w16s_frac, 2         ; ...and the 1/2000 of a tick carried
    W16BSS w16s_until, 2        ; the tick our last tone ends at
    W16BSS w16s_rnd, 2          ; 1000 or 2000: w16s_tick's rounding
    W16BSS w16s_on, 1           ; OpenSound has it
    W16BSS w16s_play, 1         ; StartSound has started the queue
    W16BSS w16s_sounding, 1     ; a tone of ours may still be sounding
    W16BSS w16s_tempo, 1        ; SetVoiceAccent's
    W16BSS w16s_mode, 1
    W16BSS w16s_pitch, 1
    ; --- bitmaps and memory DCs (w16bmp.inc, SPEC.md 124.15.2)
    W16BSS w16b_seg, 2 * W16_NBMP       ; each bitmap's claim, 0 = free
    W16BSS w16b_w, 2 * W16_NBMP         ; ...its width and height
    W16BSS w16b_h, 2 * W16_NBMP
    W16BSS w16b_str, 2 * W16_NBMP       ; ...its 4bpp bytes a row
    W16BSS w16b_sel, 2 * W16_NBMP       ; ...1 = a memory DC holds it
    W16BSS w16b_mono, 2 * W16_NBMP      ; ...1 = made 1 plane, 1 bit a pixel
    W16BSS w16m_bmp, 2 * W16_NMDC       ; each memory DC's bitmap
    W16BSS w16m_on, W16_NMDC            ; ...1 = it exists
    W16BSS w16m_brush, 2 * W16_NMDC     ; ...the brush selected into it
    W16BSS w16b_rec, 14         ; GetObject's BITMAP
    W16BSS w16b_bm, 2           ; the blit's: the bitmap's slot * 2
    W16BSS w16b_src, 2          ; ...the source DC
    W16BSS w16b_x, 2            ; ...the destination, client
    W16BSS w16b_y, 2
    W16BSS w16b_cx, 2
    W16BSS w16b_cy, 2
    W16BSS w16b_xs, 2           ; ...the source
    W16BSS w16b_ys, 2
    W16BSS w16b_cxs, 2
    W16BSS w16b_cys, 2
    W16BSS w16b_dx0, 2          ; ...the destination's corner, screen, uncut
    W16BSS w16b_dy0, 2
    W16BSS w16b_sx, 2           ; ...the source column, the cut corner's
    W16BSS w16b_sy, 2
    W16BSS w16b_sx0, 2          ; ...the DDA's first column and error
    W16BSS w16b_err0, 2
    W16BSS w16b_err, 2
    W16BSS w16b_n, 2            ; ...pixels a row (bytes a row, filling)
    W16BSS w16b_ow, 2           ; ...the band's stride
    W16BSS w16b_band, 2         ; ...rows a band
    W16BSS w16b_by, 2           ; ...the band's first screen row
    W16BSS w16b_rs, 2           ; LoadBitmap's resource, its data segment
    W16BSS w16b_dbm, 2          ; a blit INTO a memory DC: its bitmap * 2
    W16BSS w16b_dfc, 1          ; ...a fill's colour, or W16_XOR to invert
    W16BSS w16b_fpat, 1         ; ...and in both halves of a byte
    W16BSS w16b_mbr, 2          ; ...the memory DC's brush
    W16BSS w16b_ropb, 1         ; ...a source ROP's index byte
    W16BSS w16_ropt, 256        ; ...its answer, source * 16 + screen
    W16BSS w16x_rec, 66         ; TRANSPARENT text: a chunk's ICON_DRAW
    W16BSS w16x_sv, 8           ; ...a cut row read back (four planes of 2)
    W16BSS w16x_nib, 8          ; ...and as colours
    W16BSS w16x_l, 2            ; ...the run's columns
    W16BSS w16x_r, 2
    W16BSS w16x_r0, 2           ; ...the record's first band row
    W16BSS w16x_n, 2            ; ...its rows
    W16BSS w16x_b, 2            ; ...the chunk's band byte
    W16BSS w16x_c, 2            ; ...its first screen column
    W16BSS w16x_i, 2            ; ...a cut chunk's row
    W16BSS w16x_ink, 1          ; ...the ink
    W16BSS w16b_pl, 1           ; ...the screen's planes, read from it
    W16BSS w16b_bw, 2           ; ...the screen bytes a row a plane
    W16BSS w16b_sxs, 2          ; ...the screen rect read, inclusive
    W16BSS w16b_sxe, 2
    ; --- icons (SPEC.md 124.18.4, w16ico.inc)
    W16BSS w16_icseg, 2         ; part 6's segment, 0 = not here
    W16BSS w16_icx, 2           ; w16_icodraw: where, screen
    W16BSS w16_icy, 2
    W16BSS w16_icw, 2           ; ...the image's width and height
    W16BSS w16_ich, 2
    W16BSS w16_icbpp, 1         ; ...its bits a pixel
    W16BSS w16_icmap, 16        ; ...its colours, each the nearest of ours
    W16BSS w16_icxp, 2          ; ...where its colours' rows and its mask's
    W16BSS w16_icxs, 2          ;   rows start, and their strides
    W16BSS w16_icap, 2
    W16BSS w16_icas, 2
    W16BSS w16_icrc, 1          ; ...the run being gathered: its colour
    W16BSS w16_icrs, 2          ;   (W16_NOCOL none) and its first column
    W16BSS w16_icgnd, 1         ; ...what a see-through pixel is: W16_NOCOL,
                                ;   or a static's ground colour
    ; --- FindWindow (SPEC.md 124.15.3)
    W16BSS w16_fwt, 4           ; the title asked for, far
    W16BSS w16_fwcls, 1         ; 1 = a class was asked for
    OS88_BSS OP_BSS + W16B
W16_BSS_SIZE equ W16B

; =============================================================================
; w16_entry - package entry point (SPEC.md 20.2)
; in:  DS = ours, ES = KERNEL_SEG, gfx lock NOT held
; out: BX = window ptr, CF clear; CF set = abort
; =============================================================================
w16_entry:
    push es
    call op_load                    ; FIRST (os88parts.inc rule 1): our file's
    pop es                          ;   name, which the faces are read out of;
                                    ;   every row is lazy, so it cannot refuse
    call w16_pmcheck                ; SPEC.md 124.1, D6: real mode only
    jnc .real
    ret                             ; CF=1: the launch is torn down
.real:
    push si
    push di
    call OSAPI_FILE_HERE            ; the folder WIN16.O88 was launched from,
    mov [w16_home], dx              ;   banked before anything navigates
    mov [w16_homev], bl
    mov word [w16_capn], 19         ; VGA's, until the display is read
    mov word [w16_ibv], w16_ibgate  ; the dialog part's way back (SPEC.md
    mov [w16_ibv + 2], cs           ;   124.13)
    mov si, w16_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr
    jc .out
    mov [w16_win], bx
    mov ax, w16_wake
    call OSAPI_WM_ONWAKE
    mov ax, w16_onclose
    call OSAPI_WM_ONCLOSE
    mov ax, w16_onup                ; the rest of SPEC.md 124.8's input table
    call OSAPI_WM_ONMOUSEUP
    mov ax, w16_ondrag
    call OSAPI_WM_ONDRAG
    mov ax, w16_onrclick
    call OSAPI_WM_ONRCLICK
    mov ax, w16_ontimer
    call OSAPI_WM_ONTIMER
    mov word [w16_titleptr], w16_name
    call w16_cputest                ; [w16_cpu186]: once, before anything runs
    call OSAPI_ARG_FILE             ; CF=1: launched empty
    jc .idle
    mov [w16_dir], dx
    mov [w16_vol], bl
    mov di, w16_name                ; copy the name out of KERNEL_SEG before
    mov cx, 13                      ; the next call can move it
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    call w16_ownname                ; the instance is named after it
    mov si, w16_name
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_s_loading
    mov di, w16_l2
    call w16_strcpy
    mov byte [w16_state], W16S_LOAD
    mov bx, [w16_win]
    mov ax, w16_name                ; the title is the program's file name
    call OSAPI_WM_TITLE             ; (hidden yet, so it draws nothing)
    call OSAPI_WM_WAKE              ; ...and load it from the wake handler,
    jmp short .ok                   ;   the one callback without the gfx lock
.idle:
    call w16_idlelines
.ok:
    mov bx, [w16_win]
    clc
.out:
    pop di
    pop si
    ret

; =============================================================================
; w16_wake - the OSAPI_WM_ONWAKE handler (SPEC.md 74.1)
; in:  SI = our window, UI task, gfx lock NOT held
;
; A wake is a kick and a stale one is possible, so the state moves before the
; work: a second wake finds W16S_RUN or W16S_REFUSED and does nothing.
; =============================================================================
w16_wake:
    cmp byte [w16_state], W16S_ENDED
    je w16_after                    ; it ended inside a W_PAINT (w16_ppaint)
    cmp byte [w16_inpeek], 0
    jne w16_peekwake                ; a PeekMessage that found nothing
    cmp byte [w16_state], W16S_RUN
    je .kick
    cmp byte [w16_state], W16S_LOAD
    jne .out
    mov byte [w16_state], W16S_RUN
    call w16_ldin                   ; CF=1: refused, [w16_l1]/[w16_l2] say why
    jc .refused
    call w16_facein                 ; the System font for this display
    jnc .run
.refused:
    mov byte [w16_state], W16S_REFUSED
    call w16_freeprog               ; a face that failed leaves a loaded image
    call w16_repaint
    ret
.run:
    mov si, w16_s_running
    mov di, w16_l2
    call w16_strcpy
    call w16_repaint
    mov byte [w16_locked], 0        ; a wake holds no lock
    xor ax, ax
    xor dx, dx
    call w16_resume                 ; the program's first instruction
    call w16_after
.out:
    ret
.kick:
    mov byte [w16_locked], 0        ; whatever the program waits for, if it
    call w16_kick                   ;   is here now
    jmp short w16_after

; -----------------------------------------------------------------------------
; w16_after - what every resume's caller does when the resume comes back
; in:  anything a callback has; out: nothing
; The program is waiting (nothing to do) or it has ended, and an ended
; program takes its window with it, as a Windows program's did.
; -----------------------------------------------------------------------------
w16_after:
    cmp byte [w16_state], W16S_ENDED
    jne .trim
    cmp byte [w16_endmsg], 0
    jne w16_endshow
    call w16_freeprog               ; the program's memory, now: the close is
    mov bx, [w16_win]               ;   deferred to the next UI pass
    call OSAPI_WM_CLOSE
    ret
.trim:                              ; the library part stays until the
    ret                             ;   program ends (SPEC.md 124.19.2)

; w16_afterl - w16_after from a callback that holds the gfx lock: marked
; held while it runs, so its clean-up does not ask for the lock again
w16_afterl:
    push word [w16_locked]
    mov byte [w16_locked], 1
    call w16_after
    pop word [w16_locked]
    ret

; w16_endshow - the program has ended with something to say (SPEC.md 124.12:
; an import it called that WIN16 lacks): its windows, memory and face go, and
; WIN16's window stays, its own again, with [w16_l1]/[w16_l2]
w16_endshow:
    mov byte [w16_endmsg], 0
    mov bx, w16_wnd
    mov cx, W16_MAXWND
.w:
    test byte [bx + W16W_FLAGS], W16WF_USED
    jz .wn
    push cx
    call w16_wndfree
    pop cx
.wn:
    add bx, W16W_SIZE
    loop .w
    mov byte [w16_qcount], 0
    call w16_freeprog
    mov byte [w16_state], W16S_REFUSED
    mov bx, [w16_win]
    mov ax, w16_name
    call OSAPI_WM_TITLE
    call OSAPI_WM_GEOM              ; CX/DX = the content now
    mov ax, KERNEL_SEG
    mov es, ax
    mov ax, [es:bx + W_W]           ; the frame's cost, onto ours
    sub ax, cx
    mov si, [es:bx + W_H]
    sub si, dx
    mov cx, W16_W
    add cx, ax
    mov dx, W16_H
    add dx, si
    call OSAPI_WM_RESIZE            ; (it repaints everything)
    ret

; w16_freeprog - give back the program's claim, its global heap, the face
; and the dialog part, once
w16_freeprog:
    cmp word [w16_dseg], 0          ; the library part's own (SPEC.md
    je .nolib                       ;   124.19.3): a program that never fetched
    LCALL w16pf_endprog             ;   it opened no profile, played no note,
    LCALL w16s_endprog              ;   made no bitmap and loaded no icon.
    LCALL w16_bmend                 ;   Profiles written (124.14), sound
    LCALL w16_icend                 ;   stopped (124.15.1), bitmaps freed
.nolib:                             ;   (124.15.2), icons dropped (124.18.4)
    call w16_killwins               ; its second windows, which are ours
    mov byte [w16_inmodal], 0
    mov word [w16_focus], 0
    mov word [w16_capture], 0
    call w16_dlgout
    call w16x_end                   ; its floating point (SPEC.md 124.5)
    call w16_dosend                 ; its files closed, the DOS freed (124.21)
    call w16_gfreeall
    mov word [w16_rtlen], 0
    push ax
    xor ax, ax                      ; the USER heap went with the arenas
    mov [w16_mseg], ax
    mov [w16_msys], ax
    mov [w16_mtrk], al
    mov [w16_mlev], al
    mov [w16_closeq], al
    pop ax
    mov dx, [w16_pseg]
    or dx, dx
    jz .face
    call OSAPI_MEM_FREE
    mov word [w16_pseg], 0
.face:
    mov al, [w16_fpart]
    or al, al
    jz .out
    dec al
    call op_drop
    mov byte [w16_fpart], 0
    mov word [w16_face], 0
.out:
    ret

; -----------------------------------------------------------------------------
; w16_facein - read the display the window is on and fetch the System font
; Windows showed there (SPEC.md 124.7): a part of WIN16.O88, read from the
; folder it was launched from. CF=1: refused, with the reason in [w16_l2].
; UI task, no lock (the wake handler).
; -----------------------------------------------------------------------------
w16_facein:
    call w16_dispinit
    call w16_facepart               ; AL = the part
    push ax
    call OSAPI_FILE_HERE            ; where the user is, to be put back
    push dx
    push bx
    mov dx, [w16_home]
    mov bl, [w16_homev]
    call OSAPI_FILE_GOTO_QM         ; quietly, as w16_load stands in a folder
    pop bx
    pop dx
    pop ax
    push dx
    push bx
    push ax
    call op_fetch                   ; claims, reads, expands; a toast if not
    pop ax
    pop bx
    pop dx
    pushf
    push ax
    call OSAPI_FILE_GOTO_QM
    pop ax
    popf
    jc .no
    inc al
    mov [w16_fpart], al
    dec al
    call op_seg
    mov [w16_face], ax
    push es
    mov es, ax
    mov al, [es:W16F_ROWS]
    pop es
    xor ah, ah
    add ax, 3
    mov [w16_capn], ax              ; SM_CYCAPTION - 1: rows + 3
    clc
    ret
.no:
    mov si, w16_s_noface
    mov di, w16_l2
    call w16_strcpy
    stc
    ret

; -----------------------------------------------------------------------------
; w16_pfetch - part AL, fetched from WIN16's own folder (SPEC.md 20.12):
; out CF=0 and AX = its segment; CF=1 it could not be, and op_fetch has said
; why. Keeps every other register.
; -----------------------------------------------------------------------------
w16_pfetch:
    push bx
    push dx
    push ax
    call OSAPI_FILE_HERE            ; where the user is, to be put back
    push dx
    push bx
    mov dx, [w16_home]
    mov bl, [w16_homev]
    call OSAPI_FILE_GOTO_QM
    mov bx, sp
    mov al, [ss:bx + 4]             ; the part
    call op_fetch                   ; claims, reads, expands; a toast if not
    pop bx
    pop dx
    pushf
    call OSAPI_FILE_GOTO_QM
    popf
    pop ax
    jc .out
    call op_seg                     ; AX = it
.out:
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; w16_ldin - the load, from part 5 (SPEC.md 124.18.7): fetched, far-called,
; and dropped whatever w16_load answered. Its contract is w16_load's: CF=1
; refused, with [w16_l1]/[w16_l2] saying why.
; -----------------------------------------------------------------------------
w16_ldin:
    mov al, W16P_LOAD
    call w16_pfetch
    jnc .have
    mov si, w16_name
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_s_noload
    mov di, w16_l2
    call w16_strcpy
    stc
    ret
.have:
    mov word [w16_pfv], w16_loadf   ; DCALL's vector, idle at a load: no
    mov [w16_pfv + 2], ax           ;   dialog is up before the program runs
    call far [w16_pfv]
    pushf
    mov ax, [w16_dseg]              ; the dialog part's segment back, for
    mov [w16_pfv + 2], ax           ;   the next DCALL
    mov al, W16P_LOAD
    call op_drop
    popf
    ret

; -----------------------------------------------------------------------------
; w16_dlgin - the library part (SPEC.md 124.13, 124.19), fetched from WIN16's
; own folder if it is not here. CF=1: it could not be, and op_fetch has said why.
; Keeps every register.
; -----------------------------------------------------------------------------
w16_dlgin:
    cmp word [w16_dseg], 0
    je .fetch
    clc
    ret
.fetch:
    push ax
    push bx
    push dx
    mov al, W16P_DLG
    call w16_pfetch
    jc .out
    mov [w16_dseg], ax
    mov [w16_pfv + 2], ax
    mov [w16_lbv + 2], ax           ; LCALL's and the gate's (SPEC.md 124.19.2)
    mov word [w16_lbv], w16_lbgate
    clc
.out:
    pop dx
    pop bx
    pop ax
    ret

; w16_dlgout - drop the dialog part if it is here. Keeps every register.
w16_dlgout:
    cmp word [w16_dseg], 0
    je .out
    push ax
    mov al, W16P_DLG
    call op_drop
    mov word [w16_dseg], 0
    pop ax
.out:
    ret

; w16_ibgate - the dialog part's one way back into the image: a far call
; here near-calls [w16_ifn] (ICALL). CS = DS = ours. Every register and the
; flags pass through both ways.
w16_ibgate:
    call [cs:w16_ifn]
    retf

; -----------------------------------------------------------------------------
; w16_onclose - the close negotiator (SPEC.md 75.1)
; in:  SI = our window, gfx lock held; out: CF=0 the close goes ahead
; A program with a window is asked, as Windows asks it: WM_CLOSE, and the
; close is refused until the program has ended (w16_close, SPEC.md 124.8).
; One without a window is all on its own stack and in its own claim, which
; the kernel frees with the instance; int 21h is never ours between resumes
; (SPEC.md 124.2.1). The kernel holds the gfx lock for us, so it is marked
; held throughout: a program that ends in here frees its windows, and that
; must not ask for the lock again.
; -----------------------------------------------------------------------------
w16_onclose:
    push word [w16_locked]
    mov byte [w16_locked], 1
    call w16_close
    pop word [w16_locked]           ; (CF, the answer, is kept)
    ret

; =============================================================================
; The window: two lines on white
; =============================================================================
W16_W   equ 320
W16_H   equ 64

; w16_paint - W_PAINT; in: SI = window, lock held; preserves everything.
; Once the program has a window, the window is the program's (w16_ppaint).
w16_paint:
    cmp byte [w16_top], 0
    je .lines
    jmp w16_ppaint
.lines:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 10
    mov si, w16_l1
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    add dx, 14
    mov si, w16_l2
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_repaint - the content, under a lock WE take (a wake has none). DOS.O88's
; dos_repaint shape. Preserves everything.
w16_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [w16_locked], 0
    jne .held
    call OSAPI_GFX_LOCK
.held:
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov bx, [w16_win]
    call OSAPI_WM_CONTENT
    push ax
    push dx
    mov bx, [w16_win]
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    pop bx
    pop ax
    jc .nofill
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
.nofill:
    mov si, [w16_win]
    call w16_paint
    cmp byte [w16_locked], 0
    jne .out
    call OSAPI_GFX_UNLOCK
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Small helpers
; =============================================================================

; w16_strcpy - SI -> DI, NUL included; out: DI at the NUL; SI past it
w16_strcpy:
    push ax
.l:
    lodsb
    mov [di], al
    or al, al
    jz .z
    inc di
    jmp short .l
.z:
    pop ax
    ret

; w16_ownname - the program's stem into OUR header's name (SPEC.md 20.2 +16,
; 124.18.3): the loader copies that field into I_NAME after this entry proc
; returns, so the Task Manager, the dock and the menu bar call the instance
; WINMINE rather than WIN16. The field is 16 bytes, NUL-padded. Keeps every
; register.
w16_ownname:
    push ax
    push cx
    push si
    push di
    mov si, w16_name
    mov di, 16
    mov cx, 16
.c:
    lodsb
    cmp al, '.'
    jne .k
    xor al, al                      ; the stem ends at the dot
.k:
    or al, al
    jnz .w
    dec si                          ; NUL: pad from here on
.w:
    mov [di], al
    inc di
    loop .c
    pop di
    pop si
    pop cx
    pop ax
    ret

; w16_pmcheck - CF=1 under protected mode, having said so (SPEC.md 124.1, D6)
; Its own routine so that w16_entry, which loads the idle lines, is not a
; toasting procedure to tests/unit/t_toast.py. Not os88mega.inc's refusal:
; its words send the user to a MEGA apps disk, and WIN16 has no arm there.
w16_pmcheck:
    push ax
    call OSAPI_CPU_INFO             ; AH = CPU_F_*
    test ah, CPU_F_PM
    jz .real
    push cx
    push si
    push es
    push ds
    pop es
    mov si, w16_s_pm
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop si
    pop cx
    pop ax
    stc
    ret
.real:
    pop ax
    clc
    ret

; w16_idlelines - the window's two lines when it was opened with no program.
; Window text, never a toast - its own routine for w16_pmcheck's reason.
w16_idlelines:
    mov si, w16_s_idle1
    mov di, w16_l1
    call w16_strcpy
    mov si, w16_s_idle2
    mov di, w16_l2
    jmp w16_strcpy

; w16_putdec - AX as decimal at DI, NUL-terminated; out: DI at the NUL
w16_putdec:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.put:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .put
    mov byte [di], 0
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; w16_cputest - does this CPU run 186 code? (SPEC.md 124.3)
; out: [w16_cpu186]; preserves everything but the flags
; 6Ah is `push imm8` on a 186 and a V20, and an alias of `jp rel8` on an
; 8088 and an 8086. With PF clear, the jump is not taken and the operand is a
; zero displacement anyway, so either way the next instruction runs: the one
; difference is whether SP moved. That is the exact question the loader has.
w16_cputest:
    push ax
    push bx
    call OSAPI_CPU_INFO
    mov byte [w16_cpu186], 1
    cmp al, CPU_8086
    jne .out                        ; a 286 or a 386 runs everything
    mov al, 1
    or al, al                       ; one bit set: odd parity, PF = 0
    mov bx, sp
    db 0x6A, 0x00                   ; push 0 / jp $+2
    cmp bx, sp
    je .no
    inc sp                          ; the 186's pushed word
    inc sp
    jmp short .out
.no:
    mov byte [w16_cpu186], 0
.out:
    pop bx
    pop ax
    ret

%include "win16/w16core.inc"
%include "win16/w16load.inc"
%include "win16/w16i21.inc"
%include "win16/w16api.inc"
%include "win16/w16mem.inc"
%include "win16/w16res.inc"
%include "win16/w16user.inc"
%include "win16/w16wnd.inc"
%include "win16/w16menu.inc"
%include "win16/w16gdi.inc"
%include "win16/w16cens.inc"
%include "win16/w16optab.inc"
%include "win16/w16dlg.inc"     ; part 3's code, and its trampolines here
%include "win16/w16prof.inc"    ; WIN.INI and private profiles
%include "win16/w16fmt.inc"     ; wsprintf, wvsprintf (the library part's)
%include "win16/w16snd.inc"     ; SOUND's voice queue (SPEC.md 124.15.1)
%include "win16/w16bmp.inc"     ; bitmaps and memory DCs (SPEC.md 124.15.2)
%include "win16/w16clip.inc"    ; the clipboard, strings (SPEC.md 124.16)
%include "win16/w16acc.inc"     ; what CLOCK and REVERSI call (SPEC.md 124.17)
%include "win16/w16ico.inc"     ; icons (SPEC.md 124.18.4)
%include "win16/w16draw.inc"    ; what Solitaire calls (SPEC.md 124.20.4)
%include "win16/w16sys.inc"     ; what the Control Panel calls (SPEC.md 124.20.5)
%include "win16/w16file.inc"    ; files, through DOS.O88's core (SPEC.md 124.21)
%include "win16/w16sb.inc"      ; scroll bars and ScrollWindow (SPEC.md 124.22)
%include "win16/w16car.inc"     ; the caret (SPEC.md 124.22.9)
%include "win16/w16edml.inc"    ; the multi-line EDIT (SPEC.md 124.22.3)
%include "win16/w16x87.inc"     ; floating point, and part 4 (SPEC.md 124.5)

; --- the export rows and the fixed slots, in the loader part (SPEC.md
; 124.19.1). Emitted HERE, after every handler is defined, because a row's
; part is whether its handler was declared W16LIB - which nasm can only know
; once the library part's files have been read.
section .w16ld
w16_exports:
%assign k 0
%rep W16NX
    db w16x_m_%[k]
    dw w16x_o_%[k]
    db w16x_a_%[k]
  %ifdef w16lib_%[w16x_p_%[k]]
    %assign w16pt W16P_LIB
  %else
    %assign w16pt 0
  %endif
  %if w16x_m_%[k] = W16M_KERNEL
    %if w16x_o_%[k] = 1 || w16x_o_%[k] = 137
      %assign w16pt w16pt | W16B_NORET  ; FatalExit, FatalAppExit
    %endif
  %endif
    db w16pt
    dw w16x_p_%[k]
%assign k k + 1
%endrep
; GetProcAddress's names (w16api.inc's W16PROCNAME): each its module and
; ordinal, its export row's binding in w16_bind's layout, then the name as a
; pascal string; 0FFh ends them. The library part's, read through cs:
section .w16dlg
w16_pnames:
%if W16NPN > W16_NPNMAX
  %error "W16PROCNAME: more names than W16_NPNMAX"
%endif
%assign j 0
%rep W16NPN
  %assign w16f -1
  %assign k 0
  %rep W16NX
    %if w16x_m_%[k] = w16pn_m_%[j] && w16x_o_%[k] = w16pn_o_%[j]
      %assign w16f k
    %endif
    %assign k k + 1
  %endrep
  %if w16f < 0
    %error "W16PROCNAME: no export row for that module and ordinal"
  %endif
    db w16pn_m_%[j]
    dw w16pn_o_%[j]
    db w16x_a_%[w16f]
  %ifdef w16lib_%[w16x_p_%[w16f]]
    db W16P_LIB
  %else
    db 0
  %endif
    dw w16x_p_%[w16f]
  %xdefine w16pns w16pn_s_%[j]
  %strlen w16pnl w16pns
    db w16pnl, w16pns
%assign j j + 1
%endrep
    db 0xFF
section .w16ld
w16_fixed:                      ; W16_NFIXED slots, in w16_bind's own layout
%assign k 0
%rep W16NF
    db w16f_a_%[k]
  %ifdef w16lib_%[w16f_p_%[k]]
    db W16P_LIB
  %else
    db 0
  %endif
    dw w16f_p_%[k]
%assign k k + 1
%endrep
w16l_rowslot:  times W16NX dw 0     ; the bind slot each row has, 0 = none yet
w16l_missslot: times W16_NMISS dw 0 ; ...and each missing name's
section .text
%define OD_TVOL w16_homev       ; SYSTEM\APPDATA on WIN16.O88's own volume
%include "os88data.inc"

; --- the window template (SPEC.md 11) ----------------------------------------
w16_tpl:
    dw 160, 120, W16_W, W16_H
    dw w16_title, w16_paint, w16_onkey, w16_onclick

w16_title:      db 'WIN16', 0
w16_s_pm:       db 'WIN16 needs real mode', 0
w16_s_idle1:    db 'WIN16 runs Windows 3.x programs.', 0
w16_s_idle2:    db 'Double-click one in a Disk window.', 0
w16_s_loading:  db 'Loading...', 0
w16_s_running:  db 'Running', 0
w16_s_noface:   db 'Its System font could not be read.', 0
w16_s_noload:   db 'WIN16.O88 could not be read.', 0

; --- the alert (SPEC.md 75.3): MessageBox's face until S6 --------------------
%define OS88UI_ALERT 1
%define OS88UI_AEXTRA           ; OK / Cancel (MB_OKCANCEL)
%define OS88UI_NOGLYPH
%include "os88ui.inc"

; --- the parts (SPEC.md 20.12, 124.7.1): the System font, one face a display,
; each fetched for the display the program's window is on and dropped when it
; ends. tools/w16font.py builds them from apps/win16/fonts/
%include "os88parts.inc"
    OS88_PARTS_BEGIN 9
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_SYS16: sys16.txt
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_SYS12: sys12.txt
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_SYS08: sys08.txt
      OS88_PART OP_SEG, OP_LAZY | OP_COMP     ; W16P_DLG: section .w16dlg
      OS88_PART OP_SEG, OP_LAZY | OP_COMP     ; W16P_FPU: section .w16fp
      OS88_PART OP_SEG, OP_LAZY | OP_COMP     ; W16P_LOAD: section .w16ld
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_ICONS: apps/win16/icons/
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_DCORE: build/doscore.bin
      OS88_PART OP_ASSET, OP_LAZY | OP_COMP   ; W16P_DHOST: w16dosh.asm
    OS88_PARTS_END

    OS88_IMAGE_END
