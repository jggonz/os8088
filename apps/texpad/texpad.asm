; =============================================================================
; TeXPad - black-and-white TeX pad for os8088
;
; Source on the left, typeset preview on the right. Letter default, other
; sizes on the Page menu. Facing/opposing, gutter, units, page numbers.
; System 8x8 monofont, text + tables. Export is PDF 1.4 / PostScript Level 1.
; =============================================================================

%include "os88api.inc"

; SPEC.md 13.10.5's thumb GESTURE, and it SHIPS (13.10.7);
; `make SBDRAGOFF=1` compiles it out.
; AT THE TOP, not beside the %include that pulls os88ui.inc in (13.10.7.4).
%ifndef SBDRAGOFF
%define OS88UI_SBDRAG
%ifndef SB_RATE
%define SB_RATE 0               ; RATE 0 (13.10.5.4): both panes repaint whole
%endif
TP_SBRATE   equ SB_RATE
; ...AND A 286 GETS 2 (13.10.5.4.1). Two panes repainting whole is the
; heaviest commit any package here makes, and BOTH bars share one gesture
; record (13.10.7.2), so one number serves them.
%ifndef SB_RATE286
%define SB_RATE286 2
%endif
TP_SBRATE286 equ SB_RATE286
; ...AND THE PAUSE COMMIT (13.10.5.4.2): a one-shot re-armed on every movement
; fires only after this many ticks of stillness. No tier pair - half a second
; is half a second on an 8088 and on a 286 alike.
%ifndef SB_IDLE
%define SB_IDLE 9
%endif
TP_SBIDLE   equ SB_IDLE
%endif

    OS88_HEADER 'TEXPAD', tp_entry, 3

    OS88_ICON16
    ; mask: portrait page
    dw 0x3FFC
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x3FFC
    ; data: page + T + three text rules
    dw 0x0000
    dw 0x3FFC
    dw 0x2004
    dw 0x27E4
    dw 0x2124
    dw 0x2124
    dw 0x2004
    dw 0x27E4
    dw 0x2004
    dw 0x27E4
    dw 0x2004
    dw 0x27E4
    dw 0x2004
    dw 0x2004
    dw 0x3FFC
    dw 0x0000
    OS88_ICON16_END

    OS88_ASSOC16
    db 1
    OS88_ASSOC_EXT 'TEX'
    OS88_ASSOC16_END

TP_WIN_W      equ 628
TP_WIN_H      equ 400
TP_BAR_H      equ 22
TP_STAT_H     equ 14
TP_SPLIT0     equ 200
TP_SPLIT_MIN  equ 120           ; the narrowest the SOURCE pane is worth
TP_PREV_MIN   equ 140           ; ...and the PREVIEW, which is a page
TP_TEXT_MIN   equ 48            ; six rows of source under the bar
; The smallest FRAME the two-pane layout still means anything in (SPEC.md
; 11.100.2), derived from tp_clamp_split's own two numbers so they cannot
; drift apart. Without it WMIN_W lets the grow box take this window to 96:
; measured, the content is then 94 wide, tp_clamp_split pins the split at 120
; - WIDER THAN THE WINDOW - and the preview pane comes out **-26 pixels**, so
; the source pane runs off its own right edge and the preview is a rect with a
; negative width.
TP_MIN_W      equ TP_SPLIT_MIN + TP_PREV_MIN + 2
TP_MIN_H      equ TP_BAR_H + TP_STAT_H + TP_TEXT_MIN + TITLE_H + 1
TP_SB_W       equ 14            ; SPEC.md 13.10: the shared bar is 14 wide
TP_WRAP_MARK  equ '>'           ; the last column of a row that CONTINUES
TP_SB_STEP    equ 4             ; ...and an arrow cell steps FOUR rows. One is
                                ; what the kernel's own bars do and it is four
                                ; clicks a line of TeX here, where a row is an
                                ; 8px cell rather than a file-list entry. The
                                ; preview scrolls in the same step, times the
                                ; 8px a body line occupies there (SPEC.md
                                ; 13.10.1: the policy is the caller's)
TP_SRC_KB     equ 8             ; the most the source claim ever grows to...
TP_SRC_MAX    equ 8190          ; ...for a document of at most this
TP_SRC_KB0    equ 1             ; ...from this at launch (tp_claim_src)
TP_EXP_STEP   equ 4             ; the export claim grows this many KB at a time
TP_EXP_MAX    equ 24576         ; ...to this, and a document past it is refused
TP_MAX_RUNS   equ 480
TP_MAX_BOXES  equ 64
TP_MAX_PAGES  equ 16
TP_TEXT_MAX   equ 5120
; A RUN is 7 bytes: +0 its page in the low nibble with TP_RUN_BOLD and
; TP_RUN_UNDER above it, +1 x, +3 y, +5 size, +6 length. Its text is in tp_text
; in run order, so it carries no offset: every reader walks the runs from the
; first and adds each length as it goes. A BOX is 7: +0 page, +1 x, +3 y,
; +5 w - and no height, every box being one point tall (tp_tab_grid has why;
; SPEC.md 69.3).
TP_RUN_SZ     equ 7
TP_BOX_SZ     equ 7
%if TP_RUN_SZ != 7 || TP_BOX_SZ != 7
    %error "tp_put_run_at and tp_emit_box multiply by 8 - 1"
%endif
TP_RUN_PAGE   equ 0x0F          ; TP_MAX_PAGES must fit it
TP_RUN_BOLD   equ 0x10
TP_RUN_UNDER  equ 0x20
%if TP_MAX_PAGES > TP_RUN_PAGE + 1
    %error "a run's page nibble cannot hold TP_MAX_PAGES"
%endif
TP_ARG_MAX    equ 180
TP_LINE_MAX   equ 148
TP_CLINE_MAX  equ 120
TP_STY_MAX    equ 8
TP_ENV_MAX    equ 6
TP_COL_MAX    equ 6

; -----------------------------------------------------------------------------
tp_entry:                       ; (an entry proc answers BX = its window and
    cld                         ; CF, and owes the loader nothing else,
    push si                     ; SPEC.md 20.2)
    mov si, tp_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov si, tp_menus
    call OSAPI_MENU_SET
    mov [tp_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    ; The proc is a bare `ret` of ours: nothing here holds a segment of
    ; its own region.
    OS88_REGION_MOVABLE tp_seg_ok
    mov ax, tp_onup             ; SPEC.md 13.7/13.8.1: the bar's seven fire on
    call OSAPI_WM_ONMOUSEUP     ; the RELEASE and follow the pointer between
    mov ax, tp_ondrag           ; the edges. Not template words, so they are
    call OSAPI_WM_ONDRAG        ; set after wm_create like MENU_SET above
%ifdef OS88UI_SBDRAG
    sbb al, al                  ; CF = 1 on kern_small (SPEC.md 13.10.7.1): no
    mov [tp_nodrag], al         ; tracking edge, so no thumb gesture either -
    mov ax, tp_ontimer          ; read off OSAPI_WM_ONDRAG's own CF, so nothing
    call OSAPI_WM_ONTIMER       ; goes between. Then 13.10.5.4.2's PAUSE commit
%endif
    mov al, 1
    call OSAPI_WM_SIZABLE
    mov al, 1
    call OSAPI_WM_SNAP
    mov al, OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP
    call OSAPI_WM_SAVEU         ; ...AND IT PROMISES, WITH NOTHING TO REMEMBER
                                ; (SPEC.md 11.96.1). This app has NO WORKER at
                                ; all - tp_onwake is a one-shot the kernel
                                ; calls once the window is on the glass, not a
                                ; background painter - and the window ABI has
                                ; no periodic hook, so its content cannot
                                ; change while it is not drawing. There is no
                                ; mode to toggle and no br_promise to write.
                                ;
                                ; ...AND IT IS TWO COLOURS (SPEC.md 11.96.17),
                                ; which is not a claim to make lightly and is
                                ; simply true here: CBLACK and CWHITE and
                                ; nothing else in 43 uses, this being a
                                ; black-and-white TeX pad. That is what gets
                                ; it a cache AT ALL on a VGA - 628x400 is the
                                ; largest window in the system and its content
                                ; is 123,458 bytes at four planes, past
                                ; wm_su_kb's 64,512 ceiling and refused;
                                ; 30,875 at one, and taken. The repaint it
                                ; skips is source text beside a typeset
                                ; preview, which is more lettering than any
                                ; other window has
    mov cx, TP_MIN_W            ; ...AND A FLOOR UNDER THE GROW BOX (SPEC.md
    mov dx, TP_MIN_H            ; 11.100.2). Two panes with minimums cannot be
    call OSAPI_WM_MINSIZE       ; expressed by WMIN_W, which is one number for
                                ; the whole machine and is 96
    mov si, tp_pref             ; ...and the HERCULES is 720 columns wide
    call OSAPI_WM_PREFER        ; (SPEC.md 11.100.1). A two-pane editor is the
                                ; one window in the tree with somewhere to put
                                ; them: the split stays where it is and every
                                ; extra column goes to the page
    mov si, tp_onabout
    call OSAPI_ABOUT_SET
    call tp_rgn_all             ; the preview region is bss, and bss is ZERO -
                                ; which as a region means "nothing is
                                ; drawable". Every painter sets it, but this
                                ; is the one that costs nothing to be sure of
    ; The page options (bss is zero, so Letter, single-sided and inches are
    ; already set): 18pt gutter, normal padding, normal margins, footer
    ; numbers, 12pt - three words over tp_gutter's run of adjacent bytes
    mov word [tp_gutter], 1 | (1 << 8)          ; gutter, pad
    mov word [tp_margin], 1                     ; margin, units
    mov word [tp_pstyle], 1 | (12 << 8)         ; pstyle, fsize
    mov word [tp_split], TP_SPLIT0
    call tp_claim_src
    jc .out
    ; RAM seed only when not opened on a .TEX. No floppy, no typeset
    ; under the loader lock (that froze the desktop). A .TEX's load is
    ; tp_onwake's, into a document still empty.
    call tp_note_arg
    cmp byte [tp_needld], 0
    jne .ready
    call tp_seed
.ready:
    mov byte [tp_needttl], 1
    mov byte [tp_needset], 1
    mov bx, [tp_win]            ; SPEC.md 54.10: the kernel calls this once our
    mov ax, tp_onwake           ; window is on the glass, and the launch .TEX
    call OSAPI_WM_ONWAKE        ; loads in front of it rather than inside the
                                ; repaint that is still putting it there
    clc                         ; entry CF=1 means launch failed
.out:
    pop si
tp_seg_ok:                      ; (and the region's relocation proc: a ret)
    ret

; Copy ARG_FILE name only. Do not READ or typeset here.
tp_note_arg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_ARG_FILE
    jc .o
    mov [tp_dir], dx
    mov [tp_drv], bl
    mov di, tp_fname
    call tp_kname
    call tp_is_tex
    jc .o
    mov byte [tp_needld], 1
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; tp_onwake - W_ONWAKE: typeset the launch document (SPEC.md 54.10/74.1)
; in:  SI = our window; the UI task, gfx lock NOT held
; out: nothing; no register need be preserved
;
; The kernel calls this once assoc_run has shown our window. It takes the lock
; for the burst SPEC.md 74.1 lets it state - one file read, one typeset, one
; repaint - and then spends [tp_ldfull] itself, because the handler that used
; to inherit that debt is no longer the one that ran the load.
tp_onwake:
    cmp byte [tp_needld], 0     ; not the document's wake, or it is already in
    je .out
    call OSAPI_GFX_LOCK
    call tp_deferred_ld
    mov byte [tp_ldfull], 0     ; the redraw below IS the full repaint the load
                                ; asks the next handler for, so nothing
                                ; inherits it
    call tp_redraw              ; ...and it reloads SI from [tp_win] itself
    call OSAPI_GFX_UNLOCK
.out:
    ret

; Load a pending ARG_FILE. Call from tp_onwake, or a key/click, never paint.
tp_deferred_ld:
    cmp byte [tp_needld], 0
    je .o
    mov byte [tp_needld], 0
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov dx, [tp_dir]                ; the DOCUMENT's folder, stood in QUIETLY
    mov bl, [tp_drv]                ; and the instance with it, so tp_load's
    call OSAPI_FILE_GOTO_QM         ; by-name read resolves there. Nothing here
                                    ; lists the folder, so the remount's scan,
                                    ; sort and icon harvest were ~0.5 s of
                                    ; floppy for no reader (docs/plans/
                                    ; NAV-COST-PLAN.md, SPEC.md 19.9.1). CX, SI
                                    ; and DI are banked for a redirected
                                    ; volume's FSV_CHDIR (SPEC.md 62.9.1)
    jnc .there
    mov si, tp_s_err                ; CF WAS IGNORED HERE, and with the old
    call tp_toast                   ; GOTO that was safe by accident: its
    stc                             ; failure left the root with the write gate
    jmp short .moved                ; shut and tp_load's read refused. QM's
                                    ; failure moves NOTHING, so the read would
                                    ; find tp_fname in TeXPad's own folder - a
                                    ; same-named stranger loaded as this
                                    ; document. Refuse in tp_load's own words
.there:
    call tp_load
.moved:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    jnc .set
    jmp .ttl
.set:
    call tp_typeset
.ttl:
    mov byte [tp_needttl], 1
    call tp_retitle
    ; A load changes the WHOLE window - the byte count in the status strip and
    ; the page count in the top bar as much as the text - and the key handler
    ; that got us here is on its way to repainting the source pane alone. Say
    ; so, and let it repaint everything ONCE instead: launched on a .TEX, the
    ; first keypress otherwise left "0/8190" under a document plainly not
    ; empty, until something unrelated forced a full repaint.
    mov byte [tp_ldfull], 1
.o:
    ret

; THE SOURCE CLAIM IS THE DOCUMENT'S SIZE, NOT THE CEILING'S (SPEC.md 69.4).
; It is TP_SRC_KB0 at launch, grows a KB at a time as the document does
; (tp_srcfit, from tp_open_gap - the one door every insert goes through) and
; is cut back to what a newly loaded or a new document needs (tp_srctrim), so
; an open TeXPad holds its document's KB and not 8 of them. TP_SRC_MAX is
; still the document's ceiling and the status strip still says so.
tp_claim_src:
    mov ax, TP_SRC_KB0
    call OSAPI_MEM_CLAIM
    jc .bad
    mov [tp_srcseg], dx
    mov word [tp_srccap], TP_SRC_KB0 * 1024
.bad:
    ret

; AX = the bytes the document is about to need: CF=0 once the claim holds
; them, growing it to the KB they round up to (OSAPI_MEM_REGROW, SPEC.md
; 50.3.1, which brings the bytes with it when it has to move); CF=1 when the
; heap says no, and the claim is untouched. Preserves every register.
tp_srcfit:
    cmp [tp_srccap], ax
    jae .o                      ; (CF=0)
    push ax
    add ax, 1023
    mov al, ah
    shr al, 1
    shr al, 1
    xor ah, ah                  ; the KB, at most TP_SRC_KB
    call tp_srckb
    pop ax
.o:
    ret

; The claim cut back to the document it holds - [tp_srclen] rounded up to a
; KB, and never under one. A shrink is always granted in place. Preserves
; every register.
tp_srctrim:
    push ax
    mov ax, [tp_srclen]
    add ax, 1023
    mov al, ah
    shr al, 1
    shr al, 1
    xor ah, ah
    or al, al
    jnz .k
    inc ax
.k:
    call tp_srckb
    pop ax
    ret

; AX = KB: the source claim regrown to it, [tp_srcseg] and [tp_srccap]
; following. CF=1 refused. Preserves every register.
tp_srckb:
    push ax
    push dx
    mov dx, [tp_srcseg]
    call OSAPI_MEM_REGROW
    jc .o
    mov [tp_srcseg], dx
    mov ah, al
    xor al, al
    shl ax, 1
    shl ax, 1                   ; KB * 1024
    mov [tp_srccap], ax
    clc
.o:
    pop dx
    pop ax
    ret

tp_is_tex:
    push ax
    push si
    mov si, tp_fname
.l:
    mov al, [si]
    or al, al
    jz .no
    cmp al, '.'
    je .dot
    inc si
    jmp .l
.dot:
    inc si
    mov al, [si]
    or al, 0x20
    cmp al, 't'
    jne .no
    mov al, [si+1]
    or al, 0x20
    cmp al, 'e'
    jne .no
    mov al, [si+2]
    or al, 0x20
    cmp al, 'x'
    jne .no
    pop si
    pop ax
    clc
    ret
.no:
    pop si
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
tp_paint:
    cmp byte [tp_needttl], 0
    je .go
    mov byte [tp_needttl], 0
    call tp_retitle
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tp_wingeo
    ; NO tp_deferred_ld HERE. It was read from the FIRST PAINT between SPEC.md
    ; 69.6 and 54.10 - never from the entry proc, where a floppy read under the
    ; loader lock froze the desktop - and the first paint is itself inside
    ; wm_show's repaint, so the read still happened with the desktop half
    ; redrawn and this window's panes still blank. tp_onwake is after all of
    ; that, and it is the handler that spends [tp_ldfull] now.
    call tp_fill
    mov byte [tp_caret_on], 0
    mov byte [tp_dselon], 0
    cmp byte [tp_abouton], 0
    je .ui
    call tp_draw_about
    jmp .grow
.ui:
    call tp_layout
    call tp_draw_bar
    call tp_draw_source
    call tp_draw_preview
    call tp_draw_stat
.grow:
    mov bx, [tp_win]
    or bx, bx
    jz .out
    call OSAPI_WM_GROW
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; SI = the window: where its content is and how big, from the WM - every
; time, never banked (apps/os88api.inc's rule: a MOVE replays pixels and calls
; no paint) - and the split clamped to it. Preserves every register.
tp_wingeo:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [tp_ox], ax
    mov [tp_oy], dx
    call OSAPI_WM_GEOM
    mov [tp_cw], cx
    mov [tp_ch], dx
    call tp_clamp_split
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_fill:
    push ax
    push bx
    push cx
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_ox]
    mov bx, [tp_oy]
    mov cx, ax
    add cx, [tp_cw]
    dec cx
    mov dx, bx
    add dx, [tp_ch]
    dec dx
    call OSAPI_GFX_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret
; -----------------------------------------------------------------------------
; The one thing this window has to say about an adapter (SPEC.md 11.100.1).
;
; VGA and CGA get a PAIR OF ZEROS - "nothing to say", so the template stands
; and wm_fit clamps it exactly as it always did: 628x400 on a VGA, and 628x155
; on a CGA where the desktop band is all there is. The template is already
; nearly the whole width of both.
;
; THE HERCULES IS THE ENTRY, and it is the only window in the tree with an
; obvious use for those 720 columns: the source pane keeps its split and every
; extra column goes to the page. The height is the template's and is clamped to
; that adapter's 303-row band, which is 11.100.1's "a real width and a generous
; height" - only the width here is a decision.
; -----------------------------------------------------------------------------
    OS88_PREFER tp_pref, 0, 0,  712, TP_WIN_H,  0, 0


tp_clamp_split:
    push ax
    push bx
    mov ax, [tp_split]
    cmp ax, TP_SPLIT_MIN
    jae .lo
    mov ax, TP_SPLIT_MIN
.lo:
    mov bx, [tp_cw]
    sub bx, TP_PREV_MIN
    cmp bx, TP_SPLIT_MIN
    jge .hi
    mov bx, TP_SPLIT_MIN        ; the floor OSAPI_WM_MINSIZE now makes
.hi:                            ; unreachable - kept because this routine is
                                ; also reached from tp_paint, which runs on
                                ; whatever box the record holds
    cmp ax, bx
    jbe .ok
    mov ax, bx
.ok:
    mov [tp_split], ax
    pop bx
    pop ax
    ret

; The bar's seven rects, left to right with 4px between, one width each.
tp_layout:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, [tp_ox]
    add ax, 4
    mov bx, [tp_oy]
    add bx, 2
    mov dx, bx
    add dx, 16
    mov di, tp_r_set
    mov si, tp_bwid
.l:
    mov cl, [si]
    inc si
    xor ch, ch
    add cx, ax
    mov [di], ax
    mov [di+2], bx
    mov [di+4], cx
    mov [di+6], dx
    add di, 8
    mov ax, cx
    add ax, 4
    cmp si, tp_bwid + TP_NBTN
    jb .l
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

TP_NBTN     equ 7
tp_bwid:    db 60               ; 'Typeset' is seven cells; the four cyclers
            db 32, 32, 32, 32   ; after it hold three or four and stay at 32,
            db 24, 24           ; and the two page arrows one

; Paper size in points from tp_psize: Letter / Legal / A4 / A5.
tp_pagesz:
    push ax
    push bx
    mov bl, [tp_psize]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [tp_pgtab+bx]
    mov [tp_pw], ax
    mov ax, [tp_pgtab+2+bx]
    mov [tp_ph], ax
    pop bx
    pop ax
    ret

tp_pgtab:   dw 612, 792, 612, 1008, 595, 842, 420, 595
tp_margtab: db 54, 72, 108      ; the outer margin, narrow / normal / wide

; AL = a page number: NZ when it is a VERSO - the gutter on its right. Facing
; pages put it inner-left on odd (recto) pages; opposing flips that.
tp_verso:
    cmp byte [tp_bind], 2
    jne .t
    inc ax
.t:
    test al, 1
    ret

; Left / right / top / bot / text width for [tp_page].
; Single keeps equal outer margins; facing or opposing add the gutter on the
; inner side (tp_verso).
tp_geom:
    push ax
    push bx
    push cx
    push dx
    call tp_pagesz
    mov bl, [tp_margin]
    xor bh, bh
    mov cl, [tp_margtab+bx]
    xor ch, ch                  ; CX = the outer margin
    mov al, [tp_gutter]
    mov ah, 18
    mul ah
    xchg ax, bx                 ; BX = the gutter, 0 / 18 / 36pt
    mov ax, cx                  ; AX = left
    mov dx, [tp_pw]
    sub dx, cx                  ; DX = right
    cmp byte [tp_bind], 0
    je .tw
    push ax
    mov al, [tp_page]
    call tp_verso
    pop ax
    jnz .verso
    add ax, bx
    jmp short .tw
.verso:
    sub dx, bx
.tw:
    mov [tp_left], ax
    mov [tp_tleft], ax
    mov [tp_right], dx
    sub dx, ax
    mov [tp_tw], dx
    mov ax, [tp_ph]
    sub ax, cx
    cmp byte [tp_pstyle], 2     ; a header takes 14pt off the top...
    jne .th
    sub ax, 14
.th:
    mov [tp_ttop], ax
    mov ax, cx
    add ax, 18
    test byte [tp_pstyle], 1    ; ...and a footer (1, or 3 outer) 8 off the foot
    jz .tb
    add ax, 8
.tb:
    mov [tp_tbot], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Fit the sheet so 8 screen pixels = [tp_fsize] points (12pt default).
; Shrinking the whole Letter page into the pane left 8px glyphs looking
; twice as big as printed 12pt and only ~20 characters on a line.
tp_fit_sheet:
    push ax
    push bx
    push cx
    push dx
    mov ax, [tp_ppage]
    mov [tp_page], ax
    call tp_geom
    mov bl, [tp_fsize]
    or bl, bl
    jnz .fs
    mov bl, 12
.fs:
    xor bh, bh
    mov cx, bx                  ; CX = pt size
    mov ax, [tp_pw]
    call .px
    xchg ax, bx                 ; BX = sheet w
    mov ax, [tp_ph]
    call .px
    xchg ax, cx                 ; CX = sheet h
    ; horizontal: center if it fits, else flush left
    mov ax, [tp_px2]
    sub ax, [tp_px1]
    sub ax, 8
    cmp bx, ax
    jae .hfull
    sub ax, bx
    sar ax, 1
    add ax, [tp_px1]
    jmp .hx
.hfull:
    mov ax, [tp_px1]
    add ax, 4
.hx:
    mov [tp_sx1], ax
    add ax, bx
    mov [tp_sx2], ax
    ; vertical: top of pane, then scroll
    mov ax, [tp_py1]
    add ax, 4
    sub ax, [tp_pscroll]
    mov [tp_sy1], ax
    add ax, cx
    mov [tp_sy2], ax
    mov ax, [tp_left]           ; the text area's top-left, where "F5
    call tp_mapx                ; typesets" goes on an unset page
    mov [tp_tx1], ax
    mov ax, [tp_ttop]
    call tp_mapy
    mov [tp_ty1], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.px:                            ; AX points -> AX pixels at CX points a cell
    shl ax, 1
    shl ax, 1
    shl ax, 1
    xor dx, dx
    div cx
    ret

; AX = PDF x -> AX = screen x
tp_mapx:
    push bx
    push cx
    push dx
    mov cx, [tp_sx2]
    sub cx, [tp_sx1]
    mul cx
    mov bx, [tp_pw]
    or bx, bx
    jnz .d
    mov bx, 612
.d:
    div bx
    add ax, [tp_sx1]
    pop dx
    pop cx
    pop bx
    ret

; AX = PDF y (from bottom) -> AX = screen y
tp_mapy:
    push bx
    push cx
    push dx
    mov bx, [tp_ph]
    sub bx, ax
    mov ax, bx
    mov cx, [tp_sy2]
    sub cx, [tp_sy1]
    mul cx
    mov bx, [tp_ph]
    or bx, bx
    jnz .d
    mov bx, 792
.d:
    div bx
    add ax, [tp_sy1]
    pop dx
    pop cx
    pop bx
    ret

; Page, gutter band, text-area frame.
tp_draw_sheet:
    push ax
    push bx
    push cx
    push dx
    ; desk
    mov ax, [tp_px1]
    inc ax
    mov bx, [tp_py1]
    inc bx
    mov cx, [tp_px2]
    dec cx
    mov dx, [tp_py2]
    dec dx
    call tp_clip_pane           ; ...to the REGION as well, so a band redraw
    jc .gutter                  ; greys the band and not the pane
    call OSAPI_GFX_FILL_GRAY
    ; paper, clipped to the preview pane
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_sx1]
    mov bx, [tp_sy1]
    mov cx, [tp_sx2]
    mov dx, [tp_sy2]
    call tp_clip_pane
    jc .gutter
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    call tp_sheet_edges
.gutter:
    ; gutter
    cmp byte [tp_bind], 0
    je .txt
    mov al, [tp_gutter]
    or al, al
    jz .txt
    mov ah, 18
    mul ah                      ; the gutter, 18 or 36pt
    mov bx, [tp_sx2]
    sub bx, [tp_sx1]
    mul bx
    mov bx, [tp_pw]
    or bx, bx
    jnz .gd
    mov bx, 612
.gd:
    div bx
    cmp ax, 4
    jae .gok
    mov ax, 4
.gok:
    xchg ax, cx                 ; gutter px
    mov al, [tp_ppage]
    call tp_verso
    jnz .rg
    mov ax, [tp_sx1]            ; left (recto / inner)
    inc ax
    add cx, ax
    dec cx
    jmp short .gf
.rg:
    mov ax, [tp_sx2]            ; right (verso)
    sub ax, cx
    mov cx, [tp_sx2]
    dec cx
.gf:
    mov bx, [tp_sy1]
    inc bx
    mov dx, [tp_sy2]
    dec dx
    call tp_clip_pane
    jc .txt
    call OSAPI_GFX_FILL_GRAY
.txt:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The sheet's four edges, each clipped on its own. GFX_FRAME cannot be used
; here: clamped to a band it draws the two sides the clamp INVENTED, so a
; scrolled page came out ruled across the middle.
tp_sheet_edges:
    push ax
    push bx
    push cx
    push dx
    mov ax, [tp_sx1]            ; top
    mov bx, [tp_sy1]
    mov cx, [tp_sx2]
    mov dx, bx
    call tp_seg
    mov ax, [tp_sx1]            ; bottom
    mov bx, [tp_sy2]
    mov cx, [tp_sx2]
    mov dx, bx
    call tp_seg
    mov ax, [tp_sx1]            ; left
    mov bx, [tp_sy1]
    mov cx, ax
    mov dx, [tp_sy2]
    call tp_seg
    mov ax, [tp_sx2]            ; right
    mov bx, [tp_sy1]
    mov cx, ax
    mov dx, [tp_sy2]
    call tp_seg
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX..DX = a rect on the preview, inclusive: clipped to the pane and the
; region (tp_clip_pane), and drawn in the pen as what it is - a row is a
; horizontal line, a column a vertical one, anything else a frame. Clobbers
; AX..DX.
tp_seg:
    call tp_clip_pane
    jc .o
    cmp bx, dx
    jne .nh
    mov bx, cx                  ; hline: AX=x1, BX=x2, DX=y
    call OSAPI_GFX_HLINE
    ret
.nh:
    cmp ax, cx
    jne .fr
    call OSAPI_GFX_VLINE        ; AX=x, BX=y1, DX=y2, which is what the clip
    ret                         ; already left behind
.fr:
    call OSAPI_GFX_FRAME
.o:
    ret

; -----------------------------------------------------------------------------
; THE REGION - what part of the preview pane is being drawn right now
;
; The preview is painted by one set of routines whatever the reason, and a
; SCROLL needs the same picture cut to a band. There is no clip region a
; package can ask for - OSAPI_WM_CLIP_SET is the window manager's, about what
; is covering you - so this is the app's own, honoured by tp_clip_pane below
; and by the two painters' y tests. Everything that draws the preview goes
; through one of those, so a region set here reaches all of it.
;
; It is set to the whole pane by tp_rgn_all and NEVER left narrow: every
; entry that paints the preview opens with tp_rgn_all.
; -----------------------------------------------------------------------------
tp_rgn_all:
    push ax
    xor ax, ax
    mov [tp_rgx1], ax
    mov [tp_rgy1], ax
    mov ax, 0x7FFF              ; SIGNED-large, and that is the whole of it:
    mov [tp_rgx2], ax           ; tp_clip_pane compares with jge/jle because a
    mov [tp_rgy2], ax           ; mapped sheet coordinate is legitimately
    pop ax                      ; NEGATIVE (a page scrolled above the pane),
    ret                         ; so 0xFFFF as "no restriction" is -1 and
                                ; every rect clipped against it came out
                                ; EMPTY - measured as a preview with no desk,
                                ; no paper and no page edges at all, on every
                                ; full repaint

; AX..DX become the region (inclusive).
tp_rgn_set:
    mov [tp_rgx1], ax
    mov [tp_rgy1], bx
    mov [tp_rgx2], cx
    mov [tp_rgy2], dx
    ret

; Clip AX/BX/CX/DX to the preview pane AND the region. CF=1 if empty.
tp_clip_pane:
    push si
    mov si, [tp_rgx1]
    cmp ax, si
    jge .r1
    mov ax, si
.r1:
    mov si, [tp_rgy1]
    cmp bx, si
    jge .r2
    mov bx, si
.r2:
    mov si, [tp_rgx2]
    cmp cx, si
    jle .r3
    mov cx, si
.r3:
    mov si, [tp_rgy2]
    cmp dx, si
    jle .r4
    mov dx, si
.r4:
    mov si, [tp_px1]
    inc si
    cmp ax, si
    jge .x1
    mov ax, si
.x1:
    mov si, [tp_px2]
    dec si
    cmp cx, si
    jle .x2
    mov cx, si
.x2:
    cmp cx, ax
    jl .empty
    mov si, [tp_py1]
    inc si
    cmp bx, si
    jge .y1
    mov bx, si
.y1:
    mov si, [tp_py2]
    dec si
    cmp dx, si
    jle .y2
    mov dx, si
.y2:
    cmp dx, bx
    jl .empty
    pop si
    clc
    ret
.empty:
    pop si
    stc
    ret

; Status: size, margin, bind, gutter, numbers, body size, source used.
; DI = where to write it (ES is set to DS); out DI = its terminator, and
; [tp_statcnt] = where the byte count starts, the only part of the line a
; keystroke moves (tp_stat_touch).
tp_fmt_stat:
    push ax
    push si
    push es
    push ds
    pop es
    mov al, [tp_psize]
    mov si, tp_l_paper
    call tp_statf
    mov al, [tp_units]          ; the margin and the gutter come in inches
    neg al                      ; or in millimetres: units 1 is three strings
    and al, 3                   ; further on
    push ax
    add al, [tp_margin]
    mov si, tp_l_marg
    call tp_statf
    mov al, [tp_bind]
    mov si, tp_l_bind
    call tp_statf
    pop ax
    add al, [tp_gutter]
    mov si, tp_l_gut
    call tp_statf
    mov al, [tp_pstyle]
    mov si, tp_l_num
    call tp_statf
    mov al, [tp_fsize]
    xor ah, ah
    call tp_u16_di
    mov ax, 'pt'
    stosw
    mov al, ' '
    stosb
    mov [tp_statcnt], di
    call tp_stat_cnt
    pop es
    pop si
    pop ax
    ret

; The AL-th of the strings at SI, then a space, at ES:DI.
tp_statf:
    call tp_pick
    call tp_cpat
    mov al, ' '
    stosb
    ret

; "used/8190" at DI (ES = DS), and its NUL; DI is left ON the NUL.
tp_stat_cnt:
    mov ax, [tp_srclen]
    call tp_u16_di
    mov al, '/'
    stosb
    mov ax, TP_SRC_MAX
    call tp_u16_di
    mov byte [di], 0
    ret

; SI = the AL-th of the NUL-terminated strings that start at SI. Preserves
; every other register. A walk of a few bytes where a table of pointers was.
tp_pick:
    push ax
    push cx
    mov cl, al
    xor ch, ch
    jcxz .o
.n:
    lodsb
    or al, al
    jnz .n
    loop .n
.o:
    pop cx
    pop ax
    ret

; --- SPEC.md 13.8: the top row acts on the RELEASE and draws itself down ----
; The seven rects were already one description shared by the drawing and
; os88ui_bhit; what was missing is that the press ACTED. None of these has a
; safe prefix action - Setup opens a window, the four cyclers change a
; document's page setup, and the arrows move a page - so all seven want the
; release (SPEC.md 13.6).
;
; tp_btn1 is the per-control painter the down state needs: redrawing the whole
; bar on every press and every slide would re-letter the status line beside it
; and is PERFORMANCE.md's double-draw flash.
tp_bdown:   db 0                ; which of the seven is DRAWN pressed, 0 = none

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; One button at a time, the record pointed at whichever rect the caller staged
; - which is how its DOWN look stays this package's own [tp_bdown].
tp_btlbl: dw 0
tp_btflg: dw 0
    OS88UI_BTNREC tp_btrec, 0, tp_btlbl, tp_btflg, 1

; AL = 1..7 -> BX = that button's rect. The seven are adjacent in button order.
tp_brect:
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, tp_r_set - 8
    ret

; AL = 1..7: draw that button. Preserves every register.
tp_btn1:
    push ax
    push bx
    push si
    push di
    mov di, OS88UI_FILL
    cmp al, [tp_bdown]
    jne .nodn
    or di, OS88UI_DOWN
.nodn:
    call tp_brect               ; BX = its rect, before AL means anything else
    mov si, tp_it_set           ; 1: 'Typeset'
    cmp al, 1
    je .draw
    cmp al, 5
    ja .arrow
    push bx                     ; 2..5: what the option it cycles is set to,
    mov bl, al                  ; [tp_psize] onwards in button order
    xor bh, bh
    mov si, bx
    add si, si
    mov si, [tp_blists-4+si]
    mov al, [tp_psize-2+bx]
    pop bx
    call tp_pick
    jmp short .draw
.arrow:
    ; The two page arrows are the only ones here that can have nothing to do,
    ; and on page 1 of 1 that is both of them. SPEC.md 47: grey a FACT - the
    ; page number and the page count are both on the bar beside them, so this
    ; is not a guess - and let os88ui_btn dither the frame and the label,
    ; which is what makes it read as disabled on the 1bpp adapters too
    ; (SPEC.md 39.4, where grey rounds to black).
    push ax
    sub al, 6
    mov si, tp_s_lt
    call tp_pick
    pop ax
    call tp_bdis
    jnc .draw
    or di, OS88UI_DIS           ; ...and DISABLED wins over DOWN inside
                                ; os88ui_btn, so there is nothing to clear
.draw:
    mov [tp_btlbl], si          ; THE ONE CONTROL (SPEC.md 20.5.1.3): the
    mov [tp_btflg], di          ; record takes this rect, label and flags
                                ; and the picture is identical
    mov [tp_btrec+OS88UI_BT_RECTS], bx
    mov bx, tp_btrec
    mov al, 1
    call os88ui_btn
    pop di
    pop si
    pop bx
    pop ax
    ret

tp_blists:  dw tp_l_bpap, tp_l_bind, tp_l_bgut, tp_l_bpad

; -----------------------------------------------------------------------------
; tp_bdis - is button AL (1..7) DISABLED? CF=1 yes. Preserves every register.
;
; One description, read by the painter and by the press: a button drawn grey
; that still arms and depresses is worse than one that was never greyed, and
; the two would be separate opinions about tp_ppage otherwise. Back is grey on
; the first page, and Next on the last - or with nothing typeset at all.
; -----------------------------------------------------------------------------
tp_bdis:
    push ax
    cmp al, 6
    mov ax, [tp_ppage]
    jb .no
    je .prev
    inc ax
    cmp ax, [tp_npages]
    jae .yes
.no:
    clc
    pop ax
    ret
.prev:
    cmp ax, 1                   ; CF = 1 exactly on page 0
    pop ax
    ret
.yes:
    stc
    pop ax
    ret

; -----------------------------------------------------------------------------
; tp_bhit - which of the seven is this SCREEN point in? AX = 1..7, 0 = none
; -----------------------------------------------------------------------------
tp_bhit:
    push bx
    mov ax, 1
.next:
    call tp_brect
    call os88ui_bhit
    jnc .out
    inc ax
    cmp al, TP_NBTN
    jbe .next
    xor ax, ax
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; tp_setdown - AL becomes the button drawn pressed (0 = none), and it DRAWS
; -----------------------------------------------------------------------------
tp_setdown:
    push ax
    cmp al, [tp_bdown]
    je .out
    push ax
    xchg al, [tp_bdown]         ; the new one is DOWN from here...
    or al, al
    jz .take
    call tp_btn1                ; ...so the old one draws upright
.take:
    pop ax
    or al, al
    jz .out
    call tp_btn1
.out:
    pop ax
    ret

tp_draw_bar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, 1
.each:
    call tp_btn1
    inc al
    cmp al, TP_NBTN
    jbe .each
    call tp_fmt_bar
    mov cx, [tp_r_next+4]
    add cx, 8
    mov dx, [tp_oy]
    add dx, 6
    mov si, tp_status
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
    mov al, CBLACK
    call OSAPI_SET_COLOR
    ; OSAPI_GFX_HLINE is AX = x1, BX = x2, DX = y - not the four corners the
    ; fills take. Written as a rect, this drew from the left edge to whatever
    ; the Y happened to be: the bar's rule stopped at x = 62 and the status
    ; strip's at x = 407, both of them a coincidence of the window's height.
    mov ax, [tp_ox]
    mov dx, [tp_oy]
    add dx, TP_BAR_H
    dec dx                      ; DX = the rule's row
    mov bx, [tp_ox]
    add bx, [tp_cw]
    dec bx                      ; BX = its right end
    call OSAPI_GFX_HLINE
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_fmt_bar:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    mov di, tp_status
    mov si, tp_fname
    cmp byte [si], 0
    jne .n
    mov si, tp_s_noload
.n:
    call tp_cpat
    cmp byte [tp_dirty], 0
    je .pg
    mov al, '*'
    stosb
.pg:
    mov al, ' '
    stosb
    mov ax, [tp_ppage]
    inc ax
    call tp_u16_di
    mov al, '/'
    stosb
    mov ax, [tp_npages]
    or ax, ax
    jnz .np
    inc ax
.np:
    call tp_u16_di
    cmp byte [tp_needset], 0
    je .z
    mov al, ' '
    stosb
    mov si, tp_s_stale
    call tp_cpat
.z:
    mov byte [di], 0
    call tp_barsig              ; what this string was composed from, for
    mov [tp_barsig_v], al       ; tp_stat_touch's comparison
    pop es
    pop di
    pop si
    pop ax
    ret

tp_draw_stat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ox]             ; ...and the status strip's, the same way
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, TP_STAT_H
    mov bx, [tp_ox]
    add bx, [tp_cw]
    dec bx
    call OSAPI_GFX_HLINE
    mov di, tp_nbuf
    push di
    call tp_fmt_stat
    pop si
    mov cx, [tp_ox]
    add cx, 6
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, 11
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_draw_about:
    push ax
    push cx
    push dx
    push si
    mov cx, [tp_ox]
    add cx, 16
    mov dx, [tp_oy]
    add dx, 16
    mov si, tp_s_a1             ; thirteen lines, one string after another
.l:
    mov ax, CBLACK | (CWHITE << 8)
    push si
    call OSAPI_FONT_RUN
    pop si
.z:
    lodsb
    or al, al
    jnz .z
    add dx, 14
    cmp si, tp_s_aend
    jb .l
    pop si
    pop dx
    pop cx
    pop ax
    ret

; The internal "paint the whole window" entry, as against tp_paint, which is
; the WM's callback and takes the window in SI the way the WM passes it.
;
; SI IS RELOADED FROM [tp_win] HERE, and that is the whole reason this exists
; as more than a jmp: an event handler reaches a redraw through the editing
; routines, and tp_go_left / tp_up / tp_down / tp_sol / tp_home / tp_end all
; RETURN a source offset in SI. So SI at the point of the call is a byte
; offset into the document, not a window - and tp_paint asks WM_CONTENT where
; that "window" is and paints the answer, which put a second set of chrome
; over the menu bar. Every caller was correct about its own registers; the
; contract was the thing that could not survive being called from two places.
tp_redraw:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [tp_win]
    or si, si
    jz .o                       ; before WM_CREATE there is nothing to paint
    call tp_paint
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_onabout:
    xor byte [tp_abouton], 1
    call tp_redraw
    ret

; ---- panes -----------------------------------------------------------------
tp_edit_box:
    ; out: tp_ex1/ey1/ex2/ey2 inclusive
    push ax
    mov ax, [tp_ox]
    inc ax
    mov [tp_ex1], ax
    add ax, [tp_split]
    dec ax
    mov [tp_r_ssb+4], ax        ; sbar x2
    sub ax, TP_SB_W - 1
    mov [tp_r_ssb], ax          ; sbar x1 (inclusive width = TP_SB_W)
    dec ax
    mov [tp_ex2], ax            ; text right of sbar
    mov ax, [tp_oy]
    add ax, TP_BAR_H
    inc ax
    mov [tp_ey1], ax
    mov [tp_r_ssb+2], ax
    mov ax, [tp_oy]
    add ax, [tp_ch]
    sub ax, TP_STAT_H
    dec ax
    mov [tp_ey2], ax
    mov [tp_r_ssb+6], ax
    pop ax
    ret

; The preview pane's frame, and its INTERIOR ON BYTE COLUMNS.
;
; A gfx_scroll is byte-column granular (SPEC.md 5.5) and what is immediately
; outside this pane on both sides is a SCROLL BAR - the source's on the left,
; its own on the right - so a blit may not round either edge outward, and
; rounding inward leaves columns it cannot move. Here that is not a strip to
; repaint but a defect: the sheet is wider than the pane at 12pt, so the
; right-hand columns carry TEXT, and repainting them means re-lettering every
; row in the pane, which is the full repaint this exists to avoid.
;
; So the pane is placed where the arithmetic works instead: x1 rounded UP to
; the next byte column and x2+1 rounded DOWN to one, which costs at most
; seven pixels of page at each side and makes [px1+1, px2-1] exactly what
; gfx_scroll can move. The gaps left over are window background and tp_fill
; paints them.
tp_prev_box:
    push ax
    mov ax, [tp_ox]
    add ax, [tp_split]
    add ax, 5
    add ax, 7                   ; px1+1 up to a byte column, so the interior
    and ax, 0xFFF8              ; starts on one...
    dec ax
    mov [tp_px1], ax
    mov ax, [tp_ox]
    add ax, [tp_cw]
    dec ax
    dec ax
    mov [tp_r_psb+4], ax        ; preview sbar x2
    sub ax, TP_SB_W - 1
    mov [tp_r_psb], ax
    dec ax
    and ax, 0xFFF8              ; ...and px2 = the interior's x2+1, down to one
    mov [tp_px2], ax
    mov ax, [tp_oy]
    add ax, TP_BAR_H
    inc ax
    mov [tp_py1], ax
    mov [tp_r_psb+2], ax
    mov ax, [tp_oy]
    add ax, [tp_ch]
    sub ax, TP_STAT_H
    dec ax
    mov [tp_py2], ax
    mov [tp_r_psb+6], ax
    pop ax
    ret

tp_draw_source:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tp_edit_box
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ex1]
    mov bx, [tp_ey1]
    mov cx, [tp_r_ssb+4]
    mov dx, [tp_ey2]
    call OSAPI_GFX_FRAME
    cmp byte [tp_focus], 0
    jne .nf
    dec ax
    dec bx
    inc cx
    inc dx
    call OSAPI_GFX_FRAME
.nf:
    call tp_src_metrics
    jc .out
    call tp_ssb_draw
    xor di, di
.row:
    cmp di, [tp_erows]
    jae .marks
    call tp_draw_srow
    inc di
    jmp .row
.marks:
    mov byte [tp_caret_on], 0
    mov byte [tp_dselon], 0
    call tp_sel_show
    call tp_caret_show
    mov ax, [tp_vscroll]
    mov [tp_oldvs], ax
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
; THE ROW ENGINE - what one line of the source pane shows (SPEC.md 69.10)
;
; THERE IS NO HORIZONTAL SCROLLING. A line longer than the pane WRAPS, broken
; at the last space that fits - hard at the width when one word is longer than
; the pane - and a row that continues carries TP_WRAP_MARK in its last column.
; What that replaces is arrowing sideways to read a document, and the reason
; is as much about drawing as about the reading: every row's text changes when
; the view slides one column, so a horizontal scroll is a whole-pane repaint
; on every keystroke and cannot be made cheap.
;
; A ROW is therefore the unit here. The caret, the selection, the scroll bar,
; the page keys and every drawing path count in rows; a LINE exists inside
; this section, in tp_cut_line, and in the file on disk.
;
; The walk starts at the top of the document, as the line walk it replaces
; already did - a row cannot know where it begins without reading everything
; before it. What makes that affordable is the one-entry CACHE below: every
; drawing path asks for rows in order, so the second row of a repaint starts
; where the first one ended and a pane repaint is ONE pass over the document
; instead of forty-four.
; =============================================================================

; AX = the wrap width in cells: the pane's columns less the one the mark sits
; in, and never less than one.
tp_row_w:
    mov ax, [tp_ecols]
    or ax, ax
    jz .one
    dec ax
    or ax, ax
    jnz .o
.one:
    mov ax, 1
.o:
    ret

; The cache is a guess about where a row starts, so the only way to be wrong
; is to be stale: any edit, and any change of width. The row COUNT is a guess
; about the same thing, so it goes at the same moments.
tp_row_inval:
    mov word [tp_rc_row], 0xFFFF
    mov byte [tp_nrowsok], 0
    ret

; SI = a row's start offset, ES = tp_srcseg. Out: CX = its length in cells,
; DI = where the NEXT row starts, [tp_rowcont] = 1 when the break is a WRAP
; rather than a newline. CF = 1 when SI is past the end of the document.
tp_row_ext:
    push ax
    push bx
    push dx
    mov byte [tp_rowcont], 0
    mov bx, [tp_srclen]
    cmp si, bx
    ja .none
    call tp_row_w               ; AX = the width
    xor cx, cx
    mov di, si
.scan:
    cmp di, bx
    jae .eol
    mov dl, [es:di]
    cmp dl, 10
    je .eol
    cmp dl, 13
    je .eol
    inc di
    inc cx
    cmp cx, ax
    jbe .scan                   ; stop one PAST the width - that cell is the
    jmp short .wrap             ; one that does not fit
.eol:
    mov cx, di
    sub cx, si                  ; the row is the rest of the line
    cmp di, bx
    jb .nl
    inc di                      ; at the END of the document the next row must
    jmp short .o                ; not start where this one did, or the walk
.nl:                            ; never terminates
    mov dl, [es:di]
    inc di
    cmp dl, 13
    jne .o
    cmp di, bx
    jae .o
    cmp byte [es:di], 10
    jne .o
    inc di
    jmp short .o
.wrap:
    mov byte [tp_rowcont], 1
    mov di, si
    add di, ax                  ; the first cell that did not fit
.back:
    cmp di, si
    jbe .hard                   ; one word, longer than the pane
    cmp byte [es:di], ' '
    je .space
    dec di
    jmp short .back
.space:
    mov cx, di
    sub cx, si                  ; the row ends BEFORE the space...
    inc di                      ; ...and the next one starts after it
    jmp short .o
.hard:
    mov cx, ax
    mov di, si
    add di, ax
.o:
    pop dx
    pop bx
    pop ax
    clc
    ret
.none:
    pop dx
    pop bx
    pop ax
    stc
    ret

; AX = a row index. Out: CF=1 past the end, else SI = its start, CX = its
; length, [tp_rowcont] set. Every other register preserved.
tp_row_at:
    push ax
    push bx
    push dx
    push di
    push es
    mov [tp_rc_want], ax
    mov es, [tp_srcseg]
    call tp_row_w
    cmp ax, [tp_rc_w]
    je .w
    mov [tp_rc_w], ax           ; a resize is a different set of rows
    call tp_row_inval
.w:
    mov ax, [tp_rc_want]
    mov bx, [tp_rc_row]
    cmp bx, 0xFFFF
    je .top
    cmp ax, bx
    jb .top                     ; the cache is BELOW us: start over
    mov si, [tp_rc_off]
    mov dx, bx
    jmp short .walk
.top:
    xor si, si
    xor dx, dx
.walk:
    cmp dx, ax
    je .here
    call tp_row_ext
    jc .miss
    mov si, di
    inc dx
    jmp short .walk
.here:
    call tp_row_ext             ; the row's own length and continuation
    jc .miss
    mov [tp_rc_row], ax
    mov [tp_rc_off], si
    pop es
    pop di
    pop dx
    pop bx
    pop ax
    clc
    ret
.miss:
    pop es
    pop di
    pop dx
    pop bx
    pop ax
    stc
    ret

; AX = the row containing offset SI. Preserves everything else.
;
; It reads the same cache tp_row_at keeps, for the same reason: this runs on
; every editing keystroke (tp_edmark) and the offset it is asked about is
; almost always inside the pane, which is where the cache already points. A
; walk from the top of an 8KB document is ~26ms on the target machine and it
; would be paid per character typed.
tp_row_of:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov dx, si
    xor si, si
    xor ax, ax
    mov bx, [tp_rc_row]
    cmp bx, 0xFFFF
    je .lp
    mov cx, [tp_rc_off]
    cmp cx, dx
    ja .lp                      ; the cache is PAST the offset: start at the top
    mov si, cx
    mov ax, bx
.lp:
    call tp_row_ext
    jc .o
    cmp dx, di
    jb .o                       ; the offset is inside this row
    inc ax
    mov si, di
    jmp short .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; AX = how many rows START in [SI, DI). Preserves everything else.
tp_rows_between:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov dx, di
    xor ax, ax
.lp:
    cmp si, dx
    jae .o
    call tp_row_ext
    jc .o
    inc ax
    mov si, di
    jmp short .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; SI = the start of the logical LINE containing SI (ES = tp_srcseg).
tp_lstart:
    push ax
    push bx
    mov bx, [tp_srclen]
    cmp si, bx
    jbe .ok
    mov si, bx
.ok:
    or si, si
    jz .o
.lp:
    dec si
    mov al, [es:si]
    cmp al, 10
    je .aft
    cmp al, 13
    je .aft
    or si, si
    jnz .lp
    jmp short .o
.aft:
    inc si
.o:
    pop bx
    pop ax
    ret

; SI = the start of the logical line AFTER the one containing SI, or the end
; of the document (ES = tp_srcseg).
tp_lnext:
    push ax
    push bx
    mov bx, [tp_srclen]
.lp:
    cmp si, bx
    jae .o
    mov al, [es:si]
    inc si
    cmp al, 10
    je .o
    cmp al, 13
    jne .lp
    cmp si, bx
    jae .o
    cmp byte [es:si], 10
    jne .o
    inc si
.o:
    pop bx
    pop ax
    ret

; AX = how many ROWS the document has - the scroll bar's `total`.
;
; The walk reads every character of the document, and tp_ssb_sync asks for it
; on every keystroke, every scroll step and every mouse report of a live thumb
; drag - ~26ms per 8KB on the target machine, per report. So the answer is
; KEPT, and thrown away where the row cache is (tp_row_inval: every edit) and
; when the wrap width moves under it, which is a different set of rows.
tp_count_rows:
    push cx
    push dx
    push si
    push di
    push es
    call tp_row_w               ; AX = the width the count would be taken at
    cmp byte [tp_nrowsok], 0
    je .walk
    cmp ax, [tp_nrowsw]
    jne .walk
    mov ax, [tp_nrows]
    jmp short .o
.walk:
    mov [tp_nrowsw], ax
    mov es, [tp_srcseg]
    xor si, si
    xor ax, ax
.lp:
    call tp_row_ext
    jc .done
    inc ax
    mov si, di
    jmp short .lp
.done:
    mov [tp_nrows], ax
    mov byte [tp_nrowsok], 1
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; caret -> tp_cxp/tp_cyp. CF=1 off screen
tp_caret_xy:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    call tp_cur_lc
    ; AX=line BX=col
    mov dx, ax
    sub dx, [tp_vscroll]
    js .off
    cmp dx, [tp_erows]
    jae .off
    mov cx, bx
    cmp cx, [tp_ecols]
    ja .off
    mov al, 8
    mul dl
    mov dx, ax
    add dx, [tp_ey1]
    add dx, 3
    mov [tp_cyp], dx
    mov ax, cx
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3
    mov [tp_cxp], ax
    clc
    jmp .o
.off:
    stc
.o:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; out AX = the caret's ROW, BX = its column in that row.
;
; A caret at the offset where one row ends and the next begins belongs to the
; EARLIER row when the break is a wrapped space - the space is at the end of
; the first row and that is where the caret is drawn - and to the LATER row
; when the break is hard, where the offset is the next row's first cell and
; the earlier row has no column left to draw it in.
tp_cur_lc:
    push cx
    push dx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    mov dx, [tp_cur]
    cmp dx, [tp_srclen]
    jbe .ok
    mov dx, [tp_srclen]
.ok:
    xor si, si
    xor ax, ax
    xor bx, bx
    mov cx, [tp_rc_row]         ; the same cache, and this is the hottest
    cmp cx, 0xFFFF              ; reader of it: the caret's row is asked for
    je .lp                      ; on every keystroke and every repaint
    mov bx, [tp_rc_off]
    cmp bx, dx
    ja .rst
    mov si, bx
    mov ax, cx
.rst:
    xor bx, bx
.lp:
    call tp_row_ext
    jc .o
    mov bx, si
    add bx, cx                  ; BX = one past the row's last cell
    cmp dx, bx
    ja .next
    jb .in
    cmp di, bx
    je .next                    ; a hard break: this offset opens the next row
.in:
    mov bx, dx
    sub bx, si
    jmp short .o
.next:
    mov si, di
    inc ax
    xor bx, bx
    jmp short .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; White-fill the source pane and redraw it. Fallback when gfx_scroll
; refuses, or when the view jumped too far for a blit.
tp_redraw_src:
    push ax
    push bx
    push cx
    push dx
    call tp_edit_box
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_ex1]
    mov bx, [tp_ey1]
    mov cx, [tp_ex2]
    mov dx, [tp_ey2]
    call OSAPI_GFX_FILL
    mov byte [tp_caret_on], 0
    mov byte [tp_dselon], 0
    call tp_draw_source
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; erows/ecols from the edit box. CF=1 if the pane is too small to letter.
tp_src_metrics:
    push ax
    push bx
    call tp_edit_box
    mov ax, [tp_ey2]
    sub ax, [tp_ey1]
    sub ax, 4
    js .bad
    mov bl, 8
    div bl
    xor ah, ah
    mov [tp_erows], ax
    or ax, ax
    jz .bad
    mov ax, [tp_ex2]
    sub ax, [tp_ex1]
    sub ax, 6
    js .bad
    mov bl, 8
    div bl
    xor ah, ah
    mov [tp_ecols], ax
    pop bx
    pop ax
    clc
    ret
.bad:
    pop bx
    pop ax
    stc
    ret

; Fill the 7-word source bar block (SPEC.md 13.10).
tp_ssb_sync:
    push ax
    call tp_count_rows
    mov [tp_r_ssb+8], ax
    mov ax, [tp_erows]
    or ax, ax
    jnz .f
    mov ax, 8
.f:
    mov [tp_r_ssb+10], ax
    mov ax, [tp_vscroll]
    mov [tp_r_ssb+12], ax
%ifdef OS88UI_SBDRAG
    push bx
    mov bx, tp_r_ssb
    call os88ui_sbfix
    pop bx
%endif
    pop ax
    ret

; Draw the source bar whole, and remember what it was drawn from.
tp_ssb_draw:
    push bx
    call tp_ssb_sync
    mov bx, tp_r_ssb
    call os88ui_sbupdw
    pop bx
    ret

; The thumb ALONE - three drawing calls against the sixteen a whole bar costs
; (SPEC.md 13.10.3) - which is the difference on every arrow key, when total
; and fit are what the bar on the glass was drawn from; a line added or
; deleted since the last paint is a whole bar, once. os88ui_sbupd makes that
; choice and keeps what it drew in the three words behind the block
; (SPEC.md 13.10.3.2).
tp_ssb_move:
    push bx
    call tp_ssb_sync
    mov bx, tp_r_ssb
    call os88ui_sbupd
    pop bx
    ret

; DI = visible row. White-fill the 8px band and letter it. Preserves DI.
tp_draw_srow:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, di
    mov bl, 8
    mul bl
    mov dx, ax
    add dx, [tp_ey1]
    add dx, 3
    push dx
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_ex1]
    inc ax
    mov bx, dx
    mov cx, [tp_ex2]
    add dx, 7
    call OSAPI_GFX_FILL
    pop dx
    mov es, [tp_srcseg]
    mov ax, [tp_vscroll]
    add ax, di
    call tp_row_at              ; SI = its start, CX = its length, and a row
    jc .o                       ; never needs clipping: it IS what fits
    push dx
    push di
    push es
    push si
    push cx
    push ds
    pop es
    mov di, tp_cline
    pop cx
    pop si
    push cx
    push ds
    mov ax, [tp_srcseg]
    mov ds, ax
    jcxz .nul
    cld
    rep movsb
.nul:
    pop ds
    pop cx
    pop es
    mov bx, cx
    mov byte [tp_cline+bx], 0
    pop di
    pop dx
    mov cx, [tp_ex1]
    add cx, 3
    mov si, tp_cline
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
    call tp_draw_mark           ; ...and the wrap mark, if this row continues
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; DI = a visible row, DX = its top. Letter TP_WRAP_MARK in the column kept
; for it when [tp_rowcont] says the row is continued by the next one.
;
; It is a FONT_RUN of its own rather than a padded line, and that is the whole
; point: padding the row out to the mark with spaces would letter forty cells
; to draw one, at PERFORMANCE.md's ~900us each.
tp_draw_mark:
    cmp byte [tp_rowcont], 0
    je .o
    push ax
    push bx
    push cx
    push dx
    push si
    call tp_row_w               ; AX = the width, which IS the mark's column
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3
    mov cx, ax
    mov si, tp_s_wrap
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.o:
    ret

; XOR the 8x8 cell at tp_cxp/tp_cyp.
tp_xor_caret:
    push ax
    push bx
    push cx
    push dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_cxp]
    mov bx, [tp_cyp]
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call OSAPI_GFX_XOR_FILL
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_caret_hide:
    cmp byte [tp_caret_on], 0
    je .o
    call tp_xor_caret
    mov byte [tp_caret_on], 0
.o:
    ret

tp_caret_show:
    cmp byte [tp_focus], 0
    jne .o
    cmp byte [tp_selon], 0
    jne .o
    call tp_caret_xy
    jc .o
    call tp_xor_caret
    mov byte [tp_caret_on], 1
.o:
    ret

; AX=start DX=end exclusive, any order. Invert the selected cells on screen.
tp_xor_span:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp ax, dx
    je .out
    jbe .ord
    xchg ax, dx
.ord:
    mov [tp_tmp], ax            ; start
    mov [tp_tmp2], dx           ; end
    xor di, di
.row:
    cmp di, [tp_erows]
    jae .out
    mov ax, [tp_vscroll]
    add ax, di
    call tp_row_at
    jc .out
    mov bx, si                  ; the row's start
    add si, cx
    mov dx, si                  ; line end
    mov ax, [tp_tmp]
    cmp ax, dx
    jae .n
    mov ax, [tp_tmp2]
    cmp ax, bx
    jbe .n
    mov ax, [tp_tmp]
    cmp ax, bx
    jae .s0
    mov ax, bx
.s0:
    mov cx, [tp_tmp2]
    cmp cx, dx
    jbe .s1
    mov cx, dx
.s1:
    cmp ax, cx
    jae .n
    sub ax, bx                  ; start col
    sub cx, bx                  ; end col
    cmp ax, 0
    jge .c0
    xor ax, ax
.c0:
    cmp cx, 0
    jle .n
    cmp ax, [tp_ecols]
    jae .n
    cmp cx, [tp_ecols]
    jbe .c1
    mov cx, [tp_ecols]
.c1:
    cmp ax, cx
    jae .n
    push di
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3                   ; x1
    push ax
    mov ax, cx
    mov bl, 8
    mul bl
    add ax, [tp_ex1]
    add ax, 3
    dec ax
    mov cx, ax                  ; x2
    pop ax
    mov dx, di
    mov bl, 8
    xchg ax, dx
    mul bl
    add ax, [tp_ey1]
    add ax, 3
    xchg ax, dx                 ; DX=y, AX=x1
    mov bx, dx
    add dx, 7
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_XOR_FILL
    pop di
.n:
    inc di
    jmp .row
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_sel_hide:
    push ax                     ; the callers hold live values in AX/DX across
    push dx                     ; this - tp_ondrag its anchor and caret pair,
    cmp byte [tp_dselon], 0     ; tp_src_scroll the rows the view moved
    je .o
    mov ax, [tp_dsel0]
    mov dx, [tp_dsel1]
    call tp_xor_span
    mov byte [tp_dselon], 0
.o:
    pop dx
    pop ax
    ret

tp_sel_show:
    cmp byte [tp_selon], 0
    je .o
    mov ax, [tp_sel0]
    mov dx, [tp_sel1]
    cmp ax, dx
    je .o
    call tp_xor_span
    mov ax, [tp_sel0]
    mov [tp_dsel0], ax
    mov ax, [tp_sel1]
    mov [tp_dsel1], ax
    mov byte [tp_dselon], 1
.o:
    ret

tp_sel_clear:
    call tp_sel_hide
    mov byte [tp_selon], 0
    ret

; Delete the live selection. CF=1 there was none.
tp_sel_del:
    push ax
    push bx
    cmp byte [tp_selon], 0
    je .none
    call tp_sel_hide
    mov ax, [tp_sel0]
    mov [tp_cur], ax
    mov bx, [tp_sel1]
    sub bx, ax
    mov [tp_tmp3], bx
    mov bx, ax
    call tp_del_range2
    mov byte [tp_selon], 0
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
    mov byte [tp_edkind], 0
    pop bx
    pop ax
    clc
    ret
.none:
    pop bx
    pop ax
    stc
    ret

; AX and DX become the live selection (either order). Empty = none.
tp_sel_set:
    push ax
    push dx
    cmp ax, dx
    jbe .ord
    xchg ax, dx
.ord:
    cmp ax, dx
    je .none
    mov [tp_sel0], ax
    mov [tp_sel1], dx
    mov byte [tp_selon], 1
    jmp short .o
.none:
    mov byte [tp_selon], 0
.o:
    pop dx
    pop ax
    ret

; Caret / selection on glass, after the document positions changed and
; the view did not scroll. One XOR off, one XOR on; not a line.
tp_marks_sync:
    call tp_caret_hide
    call tp_sel_hide
    call tp_sel_show
    call tp_caret_show
    ret

; Focus rings only - selecting a pane does not redraw its contents.
tp_draw_frames:
    push ax
    push bx
    push cx
    push dx
    call tp_edit_box
    call tp_prev_box
    mov ax, [tp_ex1]
    mov bx, [tp_ey1]
    mov cx, [tp_r_ssb+4]
    mov dx, [tp_ey2]
    cmp byte [tp_focus], 0
    call tp_ring
    mov ax, [tp_px1]
    mov bx, [tp_py1]
    mov cx, [tp_px2]
    mov dx, [tp_py2]
    cmp byte [tp_focus], 1
    call tp_ring
    ; A FOCUS RING IS DRAWN ONE PIXEL OUTSIDE THE PANE, so the ring that is
    ; ERASED (white, the pane that just lost focus) takes with it whatever
    ; else lives on those four lines: the SPLITTER down the right of the
    ; source pane, the PREVIEW BAR's left frame column, and the status strip's
    ; RULE along the bottom of both. None of them belongs to this routine and
    ; all three have to be put back, or a click on the other pane leaves a
    ; white gap where they were.
    ;
    ; THE THIRD ONE IS THERE BECAUSE THE PANES ARE NOT SYMMETRIC. The source
    ; frame's right edge is [tp_r_ssb+4], its own bar's x2, so the source ring
    ; falls one pixel BEYOND that bar - on the splitter. The preview frame's
    ; is [tp_px2], which stops SHORT of its bar, so the preview ring falls ON
    ; [tp_r_psb], the bar's left frame column, for the bar's whole height.
    ; tests/tpdraw.py measured it as 344 pixels at x 617 down y 63..406, and
    ; only ever on a preview -> source change: the ring that GAINS focus is
    ; black, which is what the bar wanted on that column anyway.
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_ox]
    add ax, [tp_split]
    inc ax
    mov bx, [tp_py1]
    mov dx, [tp_py2]
    call OSAPI_GFX_VLINE
    mov ax, [tp_r_psb]          ; the preview bar's left frame column, which
    mov bx, [tp_r_psb+2]        ; the ring above lands on rather than beyond
    mov dx, [tp_r_psb+6]
    call OSAPI_GFX_VLINE
    mov ax, [tp_ox]
    mov bx, ax
    add bx, [tp_cw]
    dec bx                      ; hline: AX=x1, BX=x2, DX=y
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, TP_STAT_H
    call OSAPI_GFX_HLINE
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX..DX = a pane's frame: the frame in black, and the focus ring one pixel
; outside it - black when ZF=1 (this pane has the focus), white when not.
tp_ring:
    pushf
    push ax
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FRAME
    dec ax
    dec bx
    inc cx
    inc dx
    popf
    je .on
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
.on:
    call OSAPI_GFX_FRAME
    ret

; AX = the FIRST visible row the blit may move, SI = signed pixel dy (positive
; scrolls the content UP). CF = gfx_scroll's answer. Preserves AX.
;
; BOTH EDGES ROUND DOWN, and they are not the same decision.
;
; gfx_scroll is byte-column granular (SPEC.md 5.5), so x1 and x2+1 have to be
; multiples of 8 and the rect can only be widened or narrowed by whole cells.
; The text pen is at [tp_ex1]+3, which is NOT on a byte boundary, so a rect
; that starts at the next boundary UP leaves the first four columns of the
; first character cell behind - measured: four stale pixel columns down every
; row a Return pushed, the whole height of the pane. So x1 goes DOWN, to the
; content origin, which SPEC.md 11.94 keeps on a multiple of 8 for us: what
; that takes in with it is the pane's own left frame and padding, ours and
; uniform, and a vertical line moved vertically is the same line.
;
; The RIGHT edge has the source scroll bar immediately beyond it, so x2+1
; goes down too - away from it. Rounding that one up instead sheared up to
; seven columns of the bar by 8px on every Return, which is what a blit is
; least for. Nothing is lost: [tp_ex2] is padding past the last whole cell.
tp_src_blit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bl, 8
    mul bl
    add ax, [tp_ey1]
    add ax, 3
    mov bx, ax                  ; y1 = that row's top
    mov ax, [tp_ex1]
    inc ax
    and ax, 0xFFF8              ; x1, down to a byte column...
    cmp ax, [tp_ox]
    jae .x1
    mov ax, [tp_ox]             ; ...but never past the content's own left
.x1:
    mov cx, [tp_ex2]
    inc cx
    and cx, 0xFFF8
    dec cx                      ; x2, x2+1 down to a byte column
    cmp cx, ax
    jb .no
    mov dx, [tp_erows]
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, [tp_ey1]
    add dx, 3
    dec dx
    cmp dx, [tp_ey2]
    jb .yok
    mov dx, [tp_ey2]
    dec dx
.yok:
    cmp bx, dx
    ja .no
    call OSAPI_GFX_SCROLL
    jmp .o
.no:
    stc
.o:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; View moved to [tp_vscroll]. Blit + new rows, or a full pane if that fails.
tp_src_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tp_src_metrics
    jc .full
    mov ax, [tp_vscroll]
    mov bx, [tp_oldvs]
    sub ax, bx                  ; AX = d rows (new - old)
    jz .marks
    mov dx, ax
    or dx, dx
    jns .abs
    neg dx
.abs:
    cmp dx, [tp_erows]
    jae .full
    call tp_caret_hide
    call tp_sel_hide
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    push ax
    xor ax, ax                  ; the whole pane moves: from visible row 0
    call tp_src_blit
    pop ax                      ; (pop leaves gfx_scroll's CF alone)
    jc .full
    or ax, ax
    js .up
    mov di, [tp_erows]
    sub di, ax
.dn:
    cmp di, [tp_erows]
    jae .after
    call tp_draw_srow
    inc di
    jmp .dn
.up:
    neg ax
    xor di, di
.upl:
    cmp di, ax
    jae .after
    call tp_draw_srow
    inc di
    jmp .upl
.after:
    call tp_sel_show
    call tp_caret_show
    call tp_ssb_move
    mov ax, [tp_vscroll]
    mov [tp_oldvs], ax
    jmp .o
.marks:
    call tp_marks_sync
    jmp .o
.full:
    call tp_redraw_src
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; After a caret key: scroll the pane if needed, else XOR the caret.
tp_after_caret:
    push ax
    push bx
    mov ax, [tp_vscroll]
    call tp_keep_caret
    cmp ax, [tp_vscroll]
    jne .sc
    call tp_marks_sync
    jmp .o
.sc:
    call tp_src_scroll          ; ...which owns the whole-pane fallback itself
.o:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; tp_edmark - what the edit about to happen is, in ROWS
;
; Wrapping makes an edit's reach a question that cannot be answered after the
; fact: typing one character can push a word onto a new row and move every row
; below it, or pull one back and take a row away. So the shape is measured
; HERE, before the edit, as a span of the document [tp_edls, tp_edtail) and
; the number of rows it occupied; tp_after_edit measures the same span again
; and the difference is how far everything below has moved.
;
; The span starts at the caret's own LINE - or the one before it, because a
; Backspace at a line start joins the two - and ends two lines on, because a
; Delete at a line's end joins the next one in.
; -----------------------------------------------------------------------------
tp_edmark:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov byte [tp_edkind], 0
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    call tp_lstart
    cmp si, [tp_cur]
    jne .ls
    or si, si
    jz .ls
    dec si
    call tp_lstart              ; a join takes the line above with it
.ls:
    mov [tp_edls], si
    call tp_row_of
    mov [tp_edrf], ax
    mov si, [tp_cur]
    call tp_lnext
    call tp_lnext
    mov [tp_edtail], si
    mov di, si
    mov si, [tp_edls]
    call tp_rows_between
    mov [tp_edkb], ax
    mov ax, [tp_srclen]
    mov [tp_edlen], ax
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; [tp_edka] = what that span spans NOW. Every offset after the edit moved by
; the bytes the document gained, the tail included.
tp_ed_ka:
    push ax
    push si
    push di
    mov ax, [tp_srclen]
    sub ax, [tp_edlen]
    add ax, [tp_edtail]
    mov di, ax
    mov si, [tp_edls]
    call tp_rows_between
    mov [tp_edka], ax
    pop di
    pop si
    pop ax
    ret

; DI = the first visible row, CX = how many. Draw them, stopping at the pane.
tp_rows_redraw:
    push ax
    push cx
    push di
.lp:
    or cx, cx
    jz .o
    cmp di, [tp_erows]
    jae .o
    call tp_draw_srow
    inc di
    dec cx
    jmp short .lp
.o:
    pop di
    pop cx
    pop ax
    ret

; After an edit at the caret. edkind: 1=ins char 2=ins nl 3=del char 4=del nl
; 0 = something else, full pane.
tp_after_edit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tp_caret_hide
    call tp_sel_hide
    mov ax, [tp_vscroll]
    call tp_keep_caret
    mov bx, [tp_vscroll]
    cmp ax, bx
    je .same
    ; THE VIEW MOVED AND THE DOCUMENT CHANGED, which tp_src_scroll cannot do:
    ; it blits the old pixels by the rows the view moved and letters the band
    ; that came in, and the rows the EDIT moved are inside that blit.
    ; Measured with a two-line selection typed over: every row below the
    ; deletion stayed where it was, two lines stale.
    ;
    ; ONE SHAPE IS STILL A BLIT, and it is the one a person meets constantly:
    ; typing at the BOTTOM of the pane. The view goes down exactly one row,
    ; and an insert or a delete inside a line only changed the caret's line -
    ; which is now the last row, the one the blit vacates anyway. A Return
    ; owes the line it broke as well, one row above. Everything else falls
    ; through to the whole pane.
    sub bx, ax                  ; BX = rows the view moved
    cmp bx, 1
    jne .full
    mov al, [tp_edkind]
    or al, al
    jz .full
    call tp_ed_ka               ; ...and the edit must not have RESHAPED the
    mov ax, [tp_edka]           ; rows under it, or one blit cannot describe
    cmp ax, [tp_edkb]           ; what moved (wrapping makes that ordinary:
    jne .full                   ; one typed character can push a word down)
.sc1:
    call tp_src_metrics
    jc .full
    mov si, 8                   ; the pane's content up one row
    xor ax, ax
    call tp_src_blit
    jc .full
    mov di, [tp_edrf]
    sub di, [tp_vscroll]
    js .full
    mov cx, [tp_edka]
    call tp_rows_redraw         ; the span, wherever the scroll left it
    mov ax, [tp_vscroll]
    mov [tp_oldvs], ax
    call tp_caret_show
    call tp_ssb_move            ; the thumb moved, and a Return moved `total`
    jmp .strip
.full:
    call tp_redraw_src
    jmp .stat
.same:
    ; THE VIEW DID NOT MOVE, so what is on screen changed only where the edit
    ; reached: the rows of the span tp_edmark measured, plus - if that span
    ; now takes a different number of rows - everything below it, which moves
    ; by the difference and is a BLIT rather than a repaint.
    cmp byte [tp_edkind], 0
    je .full                    ; an edit nobody measured (a selection went)
    call tp_ed_ka
    mov di, [tp_edrf]
    sub di, [tp_vscroll]
    js .full                    ; the span starts above the pane
    cmp di, [tp_erows]
    jae .stat                   ; ...or below it: nothing on screen moved
    mov ax, [tp_edka]
    cmp ax, [tp_edkb]
    je .blk                     ; the rows below did not move
    mov ax, di
    add ax, [tp_edkb]           ; the first row below the OLD span - the
    cmp ax, [tp_erows]          ; blit's SOURCE start, and the gate: off the
    jae .blk                    ; pane, the bottom redraw below owns it whole
    mov ax, [tp_edka]           ; ...but the RECT anchors at the DESTINATION,
    cmp ax, [tp_edkb]           ; min(edka, edkb): gfx_scroll puts the dest
    jb .anc                     ; at the rect TOP going up and the BOTTOM
    mov ax, [tp_edkb]           ; coming down, so anchoring a shrink at
.anc:                           ; di+edkb left rows [di+edka, di+edkb) never
    add ax, di                  ; written - one row duplicated, one dropped,
                                ; on every mid-pane join
    mov si, [tp_edkb]
    sub si, [tp_edka]           ; rows the tail moves UP...
    shl si, 1
    shl si, 1
    shl si, 1                   ; ...times 8, which is gfx_scroll's dy
    push di
    call tp_src_blit
    pop di
    jc .full
.blk:
    mov cx, [tp_edka]
    call tp_rows_redraw
    mov ax, [tp_edkb]
    cmp ax, [tp_edka]
    jbe .caret
    sub ax, [tp_edka]           ; the span SHRANK: the rows it gave back are
    mov cx, ax                  ; stale at the bottom of the pane
    mov di, [tp_erows]
    sub di, cx
    js .caret
    call tp_rows_redraw
.caret:
    call tp_caret_show
    call tp_ssb_move
    jmp .strip
.stat:
    ; A Return or a join changed the LINE COUNT, which is the scroll bar's
    ; `total`: the thumb is a different size and in a different place, and
    ; nothing else on this path would have said so until the next scroll.
    ; tp_ssb_move draws the whole bar when total moved and three calls when
    ; it did not, so this is the cheap version of "keep the bar honest".
    mov al, [tp_edkind]
    cmp al, 2
    je .bar
    cmp al, 4
    jne .strip
.bar:
    call tp_ssb_move
.strip:
    call tp_stat_touch
    mov byte [tp_edkind], 0
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The BYTE COUNT in the status strip, and NOTHING else on the window.
;
; Everything to the left of it - the paper, the margin, the binding, the
; gutter, the numbering, the body size - can only be changed from a menu or a
; bar button, and every one of those repaints the window. A keystroke moves
; the count and the count only.
;
; So this formats the count ALONE, at [tp_statcnt]: everything before it was
; composed by the last full paint and nothing a keystroke does can move it, so
; recomposing the five option names on every character was work for nothing.
;
; And it is ONE font_run of about eleven cells, and deliberately not a fill
; and a run: font_run is erase-and-letter in one decision per cell (SPEC.md
; 6.1), so the cells it letters need no clearing first - and the two SPACES
; appended below cover the one case that would otherwise leave a stale cell,
; a count losing a digit and pulling '/8190' left with it. What that buys is
; not the 756us of the fill: it is that nothing here touches the right-hand
; end of the strip any more, so the GROW BOX is not erased and needs no
; OSAPI_WM_GROW to put it back, and the top bar is not re-lettered.
tp_stat_touch:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov di, [tp_statcnt]        ; where the last full paint put the count -
    or di, di                   ; the rest of the line cannot have moved since
    jz .bar                     ; (never painted: nothing to touch)
    push ds
    pop es
    call tp_stat_cnt            ; leaves DI on the terminator
    mov word [di], '  '         ; ...and a count that SHRANK pulls the rest of
    mov byte [di+2], 0          ; the field left, so letter two cells of slack
    mov ax, [tp_statcnt]
    sub ax, tp_nbuf             ; AX = the count's column in the line
    mov bl, 8
    mul bl
    add ax, [tp_ox]
    add ax, 6                   ; ...and its x, from tp_draw_stat's pen
    mov cx, [tp_ox]
    add cx, [tp_cw]
    dec cx
    cmp cx, ax
    jb .bar                     ; the strip is narrower than the count's column
    mov cx, ax
    mov dx, [tp_oy]
    add dx, [tp_ch]
    sub dx, 11
    mov si, [tp_statcnt]
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
.bar:
    ; The top bar carries the file name, the * and the (edit) marker, and a
    ; keystroke can change the last two - ONCE. Re-lettering that strip on
    ; every character typed is ~30 more cells for a picture that is already
    ; right, so this compares what the bar was drawn from against what it
    ; would be drawn from now, and draws only on the edge.
    call tp_barsig
    cmp al, [tp_barsig_v]
    je .o
    call tp_bar_status          ; ...which re-signs it through tp_fmt_bar
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AL = what the top bar's TEXT depends on and a keystroke can move. The page
; number and the file name are on it too and neither can change without a
; full repaint (a page turn, a load, a typeset).
tp_barsig:
    xor al, al
    cmp byte [tp_dirty], 0
    je .ns
    or al, 1
.ns:
    cmp byte [tp_needset], 0
    je .o
    or al, 2
.o:
    ret

; Redraw the filename/page text beside the buttons, not the buttons.
tp_bar_status:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_r_next+4]
    add ax, 4
    mov bx, [tp_oy]
    inc bx
    mov cx, [tp_ox]
    add cx, [tp_cw]
    dec cx
    mov dx, [tp_oy]
    add dx, TP_BAR_H
    sub dx, 2
    call OSAPI_GFX_FILL
    call tp_fmt_bar
    mov cx, [tp_r_next+4]
    add cx, 8
    mov dx, [tp_oy]
    add dx, 6
    mov si, tp_status
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Preview: blit the pane and letter only the uncovered band.
tp_psb_sync:
    push ax
    push bx
    call tp_fit_sheet
    mov ax, [tp_sy2]
    sub ax, [tp_sy1]
    mov [tp_r_psb+8], ax
    mov ax, [tp_py2]
    sub ax, [tp_py1]
    sub ax, 8
    cmp ax, 16
    jae .f
    mov ax, 16
.f:
    mov [tp_r_psb+10], ax
    mov ax, [tp_pscroll]
    mov [tp_r_psb+12], ax
%ifdef OS88UI_SBDRAG
    mov bx, tp_r_psb            ; SPEC.md 13.10.5.6, and 13.10.5.10 decides
    call os88ui_sbfix           ; whether it is THIS bar's gesture
%endif
    pop bx
    pop ax
    ret

tp_clamp_ps:
    push ax
    push bx
    ; AN EVEN SCROLL, ALWAYS. The desk is a 50% dither and its phase is a
    ; function of the absolute (x, y) of each pixel, so a blit by an ODD
    ; number of rows moves the checkerboard onto the wrong parity and the
    ; strip beside the page comes out inverted against everything a full
    ; repaint draws. One pixel of preview scroll is meaningless at this
    ; scale; the phase is not.
    and word [tp_pscroll], 0xFFFE
    call tp_fit_sheet
    mov ax, [tp_sy2]
    sub ax, [tp_sy1]
    mov bx, [tp_py2]
    sub bx, [tp_py1]
    sub bx, 8
    cmp bx, 16
    jae .ph
    mov bx, 16
.ph:
    sub ax, bx
    jnc .ok
    xor ax, ax
.ok:
    and ax, 0xFFFE              ; the max EVENED, like the scroll itself was
                                ; at entry: sheet_height - fit is ODD for A4
                                ; at 12pt (561), and a clamp landing there
                                ; handed tp_prev_scroll an odd dy - the 50%
                                ; desk dither blitted onto inverted parity
                                ; beside the sheet until a full repaint
    cmp [tp_pscroll], ax
    jbe .o
    mov [tp_pscroll], ax
.o:
    pop bx
    pop ax
    ret

tp_redraw_prev:
    push ax
    push bx
    push cx
    push dx
    call tp_prev_box
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [tp_px1]
    inc ax
    mov bx, [tp_py1]
    inc bx
    mov cx, [tp_px2]
    dec cx
    mov dx, [tp_py2]
    dec dx
    call OSAPI_GFX_FILL
    call tp_draw_preview
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; THE PREVIEW SCROLLS BY BLITTING TOO, and it is the pane where that matters
; most: a page of set text is forty-odd FONT_RUNs of fifty cells, which is
; PERFORMANCE.md's ~900us a cell - the better part of two seconds on the
; target machine for a scroll of four lines.
;
; The reason it did not, before, is written into the routine it replaces: a
; blit that letters only the vacated strip misses the gutter, the rules, the
; sheet's own edges and any run whose 8-row cell straddles the band. So the
; band is not lettered here - it is DRAWN, by the same routines that draw the
; whole pane, cut to the band by tp_rgn_set. A run whose cell straddles the
; edge is drawn WHOLE, and the rows of it that land outside the band are the
; same pixels the blit just put there.
; -----------------------------------------------------------------------------
tp_prev_scroll:
    push ax
    push bx
    push cx
    push dx
    push si
    call tp_clamp_ps
    mov ax, [tp_pscroll]
    mov si, ax
    sub si, [tp_oldps]          ; SI = dy, positive = the page moves UP
    jz .out
    mov cx, [tp_py2]
    sub cx, [tp_py1]            ; the pane's height
    mov dx, si
    or dx, dx
    jns .abs
    neg dx
.abs:
    cmp dx, cx
    jae .full                   ; further than the pane: nothing to keep
    call tp_prev_box
    call tp_fit_sheet           ; the sheet at the NEW scroll position
    mov ax, [tp_px1]
    inc ax                      ; the interior, both edges on byte columns by
    mov bx, [tp_py1]            ; construction (tp_prev_box)
    inc bx
    mov cx, [tp_px2]
    dec cx
    mov dx, [tp_py2]
    dec dx
    call OSAPI_GFX_SCROLL
    jc .full
    ; the band it vacated: the bottom |dy| rows going up, the top going down
    mov ax, [tp_px1]
    inc ax
    mov cx, [tp_px2]
    dec cx
    or si, si
    js .up
    mov dx, [tp_py2]
    dec dx
    mov bx, dx
    sub bx, si
    inc bx
    jmp short .band
.up:
    mov bx, [tp_py1]
    inc bx
    mov dx, bx
    sub dx, si                  ; SI is negative here
    dec dx
.band:
    call tp_prev_band           ; the rows the blit vacated...
    ; ...AND EIGHT ROWS AT THE OTHER END. A run whose 8-row cell straddles
    ; the pane's edge is not drawn at all (a glyph cannot be cut), so the
    ; edge holds a HALF cell that the blit then moves into full view - where
    ; a full repaint draws nothing. Repainting a cell's worth at the far edge
    ; is what makes the two agree.
    mov ax, [tp_px1]
    inc ax
    mov cx, [tp_px2]
    dec cx
    or si, si
    js .fup
    mov bx, [tp_py1]
    inc bx
    mov dx, bx
    add dx, 7
    jmp short .far
.fup:
    mov dx, [tp_py2]
    dec dx
    mov bx, dx
    sub bx, 7
.far:
    call tp_prev_band
.done:
    call tp_rgn_all
    call tp_psb_move            ; the thumb, not the bar
    mov ax, [tp_pscroll]
    mov [tp_oldps], ax
    jmp short .out
.full:
    call tp_rgn_all
    call tp_redraw_prev
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX..DX = a band of the preview pane; paint everything the pane has in it.
; The sheet first, because the desk and the paper are what the rest is drawn
; ON, then the page itself. A run whose cell straddles the band is drawn
; WHOLE and the rows of it outside are the same pixels that are already
; there, so nothing has to be cut per pixel - which is just as well, because
; a package has no clip region to do it with (OSAPI_WM_CLIP_SET is the window
; manager's, about what covers you).
tp_prev_band:
    push ax
    push bx
    push cx
    push dx
    call tp_rgn_set
    call tp_draw_sheet
    cmp byte [tp_setok], 0
    je .o
    call tp_paint_runs
    call tp_paint_boxes
    call tp_draw_folio
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The preview bar's thumb alone, on tp_ssb_move's rule: total and fit here are
; the sheet's height and the pane's, and a scroll moves neither.
tp_psb_move:
    push bx
    call tp_psb_sync
    mov bx, tp_r_psb
    call os88ui_sbupd
    pop bx
    ret

; ...and the whole bar, as tp_draw_preview draws it
tp_psb_full:
    push bx
    call tp_psb_sync
    mov bx, tp_r_psb
    call os88ui_sbupdw
    pop bx
    ret

tp_draw_preview:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call tp_rgn_all             ; this entry is the WHOLE pane, always
    call tp_prev_box
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [tp_px1]
    mov bx, [tp_py1]
    mov cx, [tp_px2]
    mov dx, [tp_py2]
    call OSAPI_GFX_FRAME
    cmp byte [tp_focus], 1
    jne .nf
    dec ax
    dec bx
    inc cx
    inc dx
    call OSAPI_GFX_FRAME
.nf:
    ; splitter
    mov ax, [tp_ox]
    add ax, [tp_split]
    inc ax
    mov bx, [tp_py1]
    mov dx, [tp_py2]
    call OSAPI_GFX_VLINE
    call tp_fit_sheet
    call tp_draw_sheet
    call tp_psb_full            ; ...and what it was drawn from, for
    mov ax, [tp_pscroll]        ; tp_psb_move's comparison
    mov [tp_oldps], ax
    cmp byte [tp_setok], 0
    je .empty
    call tp_paint_runs
    call tp_paint_boxes
    jmp short .fol
.empty:
    mov cx, [tp_tx1]
    add cx, 4
    mov dx, [tp_ty1]
    add dx, 8
    mov si, tp_s_noset
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
.fol:
    call tp_draw_folio
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Page number on the sheet. Default (style 1) is bottom center.
tp_draw_folio:
    cmp byte [tp_pstyle], 0
    je .o
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    pop es
    mov di, tp_nbuf
    mov ax, [tp_ppage]
    inc ax
    call tp_u16_di
    mov byte [di], 0
    lea bx, [di-tp_nbuf]        ; its length...
    mov cl, 3
    shl bx, cl                  ; ...and its width on the screen
    ; Y: bottom center (default), or header
    mov dx, [tp_sy2]
    sub dx, 18
    mov al, [tp_pstyle]
    cmp al, 2
    jne .yok
    mov dx, [tp_sy1]
    add dx, 4
.yok:
    cmp al, 3
    je .outer
    ; center on the sheet
    mov ax, [tp_sx1]
    add ax, [tp_sx2]
    sub ax, bx
    sar ax, 1
    jmp short .put
.outer:
    test byte [tp_ppage], 1
    jnz .ol
    mov ax, [tp_sx2]
    sub ax, bx
    sub ax, 6
    jmp short .put
.ol:
    mov ax, [tp_sx1]
    add ax, 6
.put:
    cmp dx, [tp_py1]
    jb .done
    mov cx, dx                  ; A CELL IS EIGHT ROWS and this is its TOP: a
    add cx, 7                   ; run one row above the pane's bottom frame
    cmp cx, [tp_py2]            ; letters seven rows THROUGH it, and what is
    jae .done                   ; under it is the status strip. Measured as
    cmp cx, [tp_rgy1]           ; two rows of page text in the strip, which
    jb .done                    ; only ever showed up as a full repaint
    cmp dx, [tp_rgy2]           ; disagreeing with an incremental one. The
    ja .done                    ; region tests are the run painter's
    add ax, 7
    and al, 0xF8
    xchg ax, cx
    mov si, tp_nbuf
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.o:
    ret

tp_paint_runs:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; Place each run at its typeset (x,y). The sheet is already scaled
    ; so 8px = body point size; packing into a fitted pane made 12pt
    ; look huge and crushed the measure to ~20 characters.
    ;
    ; The runs are WALKED, BX the record and SI its text, which starts where
    ; the run before it ended (SPEC.md 69.3) - a running sum in place of the
    ; multiply per run this used to index with.
    mov bx, tp_runs
    mov si, tp_text
    mov di, [tp_nruns]
.lp:
    or di, di
    jz .o
    mov al, [bx]
    and al, TP_RUN_PAGE
    cmp al, [tp_ppage]          ; (a page number is a nibble: the word's high
    jne .n                      ; byte is 0)
    cmp byte [bx+6], 0
    je .n
    cmp word [bx+3], 40
    jb .n
    mov ax, [bx+1]
    call tp_mapx
    mov cx, ax
    mov ax, [bx+3]
    call tp_mapy
    mov dx, ax
    cmp dx, [tp_py1]
    jb .n
    add ax, 7                   ; ...the same eight rows as the folio above
    cmp ax, [tp_py2]
    jae .n
    cmp ax, [tp_rgy1]           ; does the CELL reach the region at all? A run
    jb .n                       ; that straddles its edge is drawn whole - the
    cmp dx, [tp_rgy2]           ; rows outside are the same pixels the blit
    ja .n                       ; put there
    cmp cx, [tp_px2]
    jae .n
    cmp cx, [tp_px1]
    jae .xok
    mov cx, [tp_px1]
    add cx, 2
.xok:
    push si
    push di
    push cx
    push ds
    pop es
    mov di, tp_cline
    mov cl, [bx+6]
    xor ch, ch
    cmp cl, 80
    jbe .cp
    mov cl, 80
.cp:
    rep movsb
    mov [di], ch                ; the NUL: CH is 0
    pop cx
    mov si, tp_cline
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_RUN
    test byte [bx], TP_RUN_BOLD
    jnz .bd
    cmp byte [bx+5], 14
    jb .un
.bd:
    inc cx
    mov si, tp_cline
    mov ax, CBLACK | (CWHITE << 8)
    call OSAPI_FONT_STR_XPARENT
.un:
    pop di
    pop si
.n:
    mov al, [bx+6]
    xor ah, ah
    add si, ax
    add bx, TP_RUN_SZ
    dec di
    jmp .lp
.o:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_paint_boxes:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, tp_boxes
    mov di, [tp_nboxes]
.lp:
    or di, di
    jz .o
    mov bx, si
    mov al, [bx]
    cmp al, [tp_ppage]
    jne .n
    push si
    push di
    ; top-left: (x, y+h)
    mov ax, [bx+1]
    call tp_mapx
    xchg ax, di
    mov ax, [bx+3]
    push ax
    inc ax                      ; ...a box is one point tall
    call tp_mapy
    xchg ax, si
    ; bottom-right: (x+w, y)
    mov ax, [bx+1]
    add ax, [bx+5]
    call tp_mapx
    xchg ax, cx
    pop ax
    call tp_mapy
    xchg ax, dx
    xchg ax, di
    mov bx, si
    cmp ax, cx
    jle .xok
    xchg ax, cx
.xok:
    cmp bx, dx
    jle .yok
    xchg bx, dx
.yok:
    call tp_seg                 ; (the pen is black from the top of the
.sk:                            ; routine and nothing in the loop moves it)
    pop di
    pop si
.n:
    add si, TP_BOX_SZ
    dec di
    jmp .lp
.o:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; A WINDOW CALLBACK MAY CLOBBER AX, BX, CX, DX, SI, DI AND ES (SPEC.md 12.2,
; 38.6): ui_bill, which dispatches every one of them, clobbers the first five
; itself. So the handlers below bank SI at most - W_ONCLICK must give it back
; (SPEC.md 13.7) - and nothing else.
tp_onkey:
    call tp_deferred_ld
    mov bx, ax
    cmp byte [tp_abouton], 0
    je .k
    mov byte [tp_abouton], 0
    jmp .draw
.k:
    cmp bh, 0x3F                ; F5
    je .kset
    mov al, bl
    cmp byte [tp_focus], 0
    jne .nsrc
    mov ah, bh                  ; THE KEYPAD FIRST, by scan code - its keys
    sub ah, 0x47                ; carry a digit in AL with NumLock on and have
    cmp ah, 0x53 - 0x47         ; always moved the caret here, not typed it.
    jbe .pad                    ; Off the keypad that is one compare
.chr:
    cmp al, 127                 ; A PRINTABLE CHARACTER IS THE HOT PATH: two
    jae .ned                    ; compares from here and it is an insert
    cmp al, 32
    jae .kchar
.ned:
    mov si, tp_ked              ; Backspace, Delete, Return, Tab - and the
    call tp_klook               ; table runs on into the Ctrl keys'
    jnc .jmp
    jmp .out
.pad:
    ; THE KEYPAD IS A TABLE INDEXED BY SCAN CODE, not a search: a caret key
    ; is the next hot path after a character, and searching the Ctrl keys'
    ; table and then the caret keys' cost an arrow ~500 cycles over the
    ; compare chain it replaced. A 0 entry is a key that types (NumLock).
    mov al, ah
    cmp al, 0x53 - 0x47         ; Delete
    je .kdel
    cbw
    mov di, .ecaret             ; a caret key redraws the caret, a page key
    cmp al, 0x49 - 0x47         ; scrolls
    je .pg
    cmp al, 0x51 - 0x47
    jne .pc
.pg:
    mov di, .escroll
.pc:
    xchg ax, si
    shl si, 1
    mov si, [tp_kpad+si]
    mov al, bl
    or si, si
    jz .chr
    push di
    call tp_sel_clear
    call si
    pop di
    jmp di
.jmp:
    jmp si
.nsrc:
    cmp al, 32
    jae .pv
    mov si, tp_kctl             ; Esc and the Ctrl keys, in the preview too
    call tp_klook
    jnc .jmp
.pv:
    ; '[' AND ']' TURN PAGES ONLY WITH THE PREVIEW FOCUSED. They were tested
    ; ahead of the focus, so in the SOURCE they turned a page instead of
    ; going in - and a .TEX file needs them: \documentclass[12pt] could not
    ; be typed.
    mov si, tp_kpva
    call tp_klook
    jnc .jmp
    mov al, bh
    mov si, tp_kpvs
    call tp_klook
    jnc .jmp
    jmp .out
.kset:
    call tp_typeset
    jmp .draw
.kesc:
    call tp_typeset
    mov byte [tp_focus], 1
    jmp .draw
.ksave:
    call tp_save
    jmp .draw
.kopen:
    call tp_open_dlg
    jmp .out
.knew:
    call tp_new                 ; as File > New
    jmp .draw
.kcopy:
    call tp_copy_all
    jmp .draw
.kpaste:
    call tp_paste
    jmp .draw
.kcut:
    call tp_cut_line
    jmp .draw
.kprev:
    call tp_prev_page
    jmp .draw
.knext:
    call tp_next_page
    jmp .draw
.kchar:
    ; A SELECTION THAT WENT IS NOT AN INSERT (edkind 0 = the whole pane).
    ; tp_sel_del leaves edkind 0 and CF=1 when there was nothing selected;
    ; claiming "one character into one line" over the top of it drew the
    ; caret's line and left every row a multi-line selection had moved.
    call tp_edmark              ; where this lands, in rows, BEFORE it does
    call tp_sel_del
    mov al, bl
    jnc .kins
    mov byte [tp_edkind], 1
.kins:
    call tp_ins
    jmp .etype
.kbs:
    call tp_edmark
    call tp_sel_del
    jc .kbs1
    mov byte [tp_edkind], 0
    jmp .etype
.kbs1:
    call tp_backsp
    jmp .etype
.kdel:
    call tp_edmark
    call tp_sel_del
    jc .kdel1
    mov byte [tp_edkind], 0
    jmp .etype
.kdel1:
    call tp_delete
    jmp .etype
.knl:
    call tp_edmark
    call tp_sel_del             ; ...and the same for a Return and a Tab
    mov al, 10
    jnc .knl1
    mov byte [tp_edkind], 2
.knl1:
    call tp_ins
    jmp .etype
.ktab:
    call tp_edmark
    call tp_sel_del
    mov al, ' '
    jnc .ktab1
    mov byte [tp_edkind], 1
.ktab1:
    call tp_ins
    call tp_ins
    jmp .etype
.etype:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_after_edit
    jmp .out
.ecaret:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_after_caret
    jmp .out
.escroll:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_src_scroll
    jmp .out
.psu:
    cmp word [tp_pscroll], 0
    je .out
    sub word [tp_pscroll], 24
    jnc .psc
    mov word [tp_pscroll], 0
    jmp short .psc
.psd:
    add word [tp_pscroll], 24
.psc:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_prev_scroll
    jmp .out
.draw:
    mov byte [tp_ldfull], 0
    call tp_keep_caret
    call tp_redraw
    jmp .done
.out:
    ; a key that changed nothing still owes a repaint if a load came in under
    ; it - an unhandled key is the one path here that draws nothing at all.
    cmp byte [tp_ldfull], 0
    jne .draw
.done:
    ret

; AL = a key, SI = a table of (key, where) - a byte and a word - ending in a
; 0 key: CF=0 and SI = where, or CF=1. Preserves everything else.
tp_klook:
    cmp byte [si], 0
    je .no
    cmp al, [si]
    je .hit
    add si, 3
    jmp tp_klook
.hit:
    mov si, [si+1]              ; (CF is 0: the compare was equal)
    ret
.no:
    stc
    ret

; the key tables: ASCII in BL is looked up in some, the scan code in BH in
; the rest (tp_onkey says which)
tp_ked:     db 8                    ; (runs on into tp_kctl: the source
            dw tp_onkey.kbs         ; pane looks the two up as one)
            db 127
            dw tp_onkey.kdel
            db 13
            dw tp_onkey.knl
            db 9
            dw tp_onkey.ktab
tp_kctl:    db 27
            dw tp_onkey.kesc
            db 19                   ; Ctrl+S
            dw tp_onkey.ksave
            db 15                   ; Ctrl+O
            dw tp_onkey.kopen
            db 14                   ; Ctrl+N
            dw tp_onkey.knew
            db 3                    ; Ctrl+C
            dw tp_onkey.kcopy
            db 22                   ; Ctrl+V
            dw tp_onkey.kpaste
            db 24                   ; Ctrl+X
            dw tp_onkey.kcut
            db 0
tp_kpad:    dw tp_home              ; 47
            dw tp_up                ; 48
            dw tp_pgup              ; 49
            dw 0                    ; 4A -
            dw tp_go_left           ; 4B
            dw 0                    ; 4C 5
            dw tp_go_right          ; 4D
            dw 0                    ; 4E +
            dw tp_end               ; 4F
            dw tp_down              ; 50
            dw tp_pgdn              ; 51
            dw 0                    ; 52 Ins
tp_kpva:    db '['
            dw tp_onkey.kprev
            db ']'
            dw tp_onkey.knext
            db 0
tp_kpvs:    db 0x48
            dw tp_onkey.psu
            db 0x50
            dw tp_onkey.psd
            db 0x49
            dw tp_onkey.kprev
            db 0x51
            dw tp_onkey.knext
            db 0x4B
            dw tp_onkey.kprev
            db 0x4D
            dw tp_onkey.knext
            db 0

; -----------------------------------------------------------------------------
; tp_bact - do what bar button AL says (SPEC.md 13.8)
; in:  AL = 1..7; the gfx lock is held
; out: CF = 1 if the caller owes a full redraw
;
; ONE body, lifted out of tp_onclick's ladder so the press and the release
; cannot come to hold different opinions about what a button means.
; -----------------------------------------------------------------------------
tp_bact:
    cmp al, 1
    je .set
    cmp al, 6
    je .prev
    ja .next
    mov bl, al                  ; 2..5 cycle [tp_psize] onwards, in button
    xor bh, bh                  ; order: four paper sizes, then three of each
    mov ah, 3                   ; of the others
    cmp al, 2
    jne .lim
    inc ah
.lim:
    mov al, [tp_psize-2+bx]
    inc ax
    cmp al, ah
    jb .ok
    xor al, al
.ok:
    mov [tp_psize-2+bx], al
    mov byte [tp_needset], 1
    stc
    ret
.set:
    call tp_typeset
    stc
    ret
.prev:
    call tp_prev_page
    stc
    ret
.next:
    call tp_next_page
    stc
    ret
; -----------------------------------------------------------------------------
; tp_onup - W_ONMOUSEUP (SPEC.md 13.7): a bar button fires HERE
; in:  CX = x, DX = y (SCREEN), SI = the window; gfx lock held
; -----------------------------------------------------------------------------
%ifdef OS88UI_SBDRAG
tp_ontimer:                     ; the thumb has been STILL for TP_SBIDLE ticks
    push si                     ; (13.9 disarms before this runs and this does
    call tp_sbd_which           ; not re-arm, so a pause is ONE commit). WHICH
    jc .tout                    ; bar, same as the other two edges
    call os88ui_sbowed          ; ...and NOT os88ui_sbdrop: a pause is not the
    jc .tout                    ; end of the gesture
    call tp_sbd_commit
.tout:
    pop si
    ret
%endif

tp_onup:
    push si
    mov byte [tp_seldrag], 0
%ifdef OS88UI_SBDRAG
    call tp_sbd_which           ; SPEC.md 13.10.5: the release COMMITS, and
    jc .btn                     ; unconditionally
    push bx                     ; the pause timer must not outlive the gesture
    mov bx, [tp_win]            ; it belongs to (13.10.5.4.2)
    xor ax, ax
    call OSAPI_WM_TIMER
    pop bx
    call os88ui_sbdrop
    jc .btn
    call tp_sbd_commit
    jmp .out
.btn:
%endif
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov di, ax
    call tp_bhit                ; still on it?
    xor ah, ah
    cmp ax, di
    je .fire
    xor al, al                  ; no: thought better of, and nothing fires
    call tp_setdown
    jmp short .out
.fire:
    mov byte [tp_bdown], 0      ; NOT tp_setdown: tp_redraw below draws the
                                ; whole window, this button upright with it,
                                ; and drawing it upright first is
                                ; PERFORMANCE.md's double-draw
    call tp_bact
    jnc .out
    mov byte [tp_ldfull], 0
    call tp_redraw
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; tp_ondrag - W_ONDRAG (SPEC.md 13.8.1): the pointer moved, press still down
; -----------------------------------------------------------------------------
tp_ondrag:
    push si
    cmp byte [tp_seldrag], 0
    je .bar
    ; extend the source selection to the pointer; XOR only the cells that
    ; changed, never the pane.
    call tp_click_caret
    mov ax, [tp_anchor]
    mov dx, [tp_cur]
    cmp ax, dx
    jne .sel
    call tp_sel_hide
    mov byte [tp_selon], 0
    call tp_caret_show
    jmp .out
.sel:
    call tp_caret_hide
    call tp_sel_hide
    call tp_sel_set
    call tp_sel_show
    jmp .out
.bar:
%ifdef OS88UI_SBDRAG
    call tp_sbd_which           ; SPEC.md 13.10.5: a live thumb drag owns this
    jc .btn                     ; movement whole
    push bx                     ; 13.10.5.4.2: every movement pushes the
    mov bx, [tp_win]            ; one-shot out. BX is the BLOCK here and the
    mov ax, TP_SBIDLE           ; timer wants the WINDOW, so it is banked -
    call OSAPI_WM_TIMER         ; tp_sbd_which chose which of the two bars and
    pop bx                      ; that answer must survive
    call os88ui_sbtrack         ; CF = 1: nothing owed - the rate, or the same
    jc .out                     ; step
    call tp_sbd_commit
    jmp .out
.btn:
%endif
    call os88ui_armed           ; PEEK: the arm is the release's to spend
    or ax, ax
    jz .out
    mov di, ax
    call tp_bhit
    xor ah, ah
    cmp ax, di
    je .set
    xor al, al                  ; off it: nothing is down
.set:
    call tp_setdown             ; ...which draws only if that CHANGED
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
tp_onclick:
    call tp_deferred_ld
    push si
    call tp_wingeo
    call tp_layout
    call tp_edit_box
    call tp_prev_box
%ifdef OS88UI_SBDRAG
    call tp_sbd_which           ; A PRESS CANNOT ARRIVE DURING A LIVE DRAG, so
    jc .nostale                 ; one that does means the release never came
    push cx                     ; (SPEC.md 13.10.5.7). AFTER tp_layout, so the
    push dx                     ; two rects the gesture is matched against are
    call os88ui_sbdrop          ; the ones on screen
    pop dx
    pop cx
.nostale:
%endif
    ; A CLICK DISMISSES THE ABOUT BOX AND DOES NOTHING ELSE, and it owes the
    ; window a repaint on the way out - the box is drawn OVER the two panes
    ; and the bar, so every incremental path below would leave it standing.
    ; Clearing the flag and then routing the click was the bug: a click on
    ; the source pane put a caret under a paragraph of About text, and a
    ; click on nothing left the box up for good. The key handler already
    ; treats a keystroke this way.
    cmp byte [tp_abouton], 0
    je .live
    mov byte [tp_abouton], 0
    jmp .draw
.live:
    ; SPEC.md 13.6/13.8: the seven bar buttons only ARM and DRAW here, and
    ; tp_onup acts. The two scroll-bar rects below keep the press - a scroll
    ; is repeatable and reversible, which is the definition of a safe prefix
    ; action, and the arrow cells are the part that would want a down state.
    call tp_bhit
    or al, al
    jz .sbars
    call tp_bdis                ; grey: it does not arm and it does not go down
    jc .out
    xor ah, ah
    call os88ui_arm
    call tp_setdown
    jmp .out
.sbars:
    mov bx, tp_r_ssb
    call os88ui_bhit
    jnc .ssb
    mov bx, tp_r_psb
    call os88ui_bhit
    jnc .psb
    ; panes
    cmp dx, [tp_ey1]
    jb .out
    cmp dx, [tp_ey2]
    ja .out
    cmp cx, [tp_ex2]
    jbe .src
    cmp cx, [tp_px1]
    jae .prv
    jmp .out
.src:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_focus_src
    call tp_caret_hide
    call tp_sel_hide
    mov byte [tp_selon], 0
    call tp_click_caret
    mov ax, [tp_cur]
    mov [tp_anchor], ax
    mov byte [tp_seldrag], 1
    call tp_caret_show
    jmp .done
.prv:
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_focus_prv
    jmp .done
.ssb:
    call tp_focus_src
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_caret_hide          ; the marks come off at the scroll they were
    call tp_sel_hide            ; DRAWN at - tp_xor_span walks rows from
                                ; [tp_vscroll], which tp_ssb_click is about to
                                ; move (tp_after_edit hides ahead of the move
                                ; for the same reason)
    call tp_ssb_click
    call tp_src_scroll
    jmp .done
.psb:
    call tp_focus_prv
    cmp byte [tp_ldfull], 0
    jne .draw
    call tp_psb_click
    call tp_prev_scroll
    jmp .done
.draw:
    mov byte [tp_ldfull], 0     ; this IS the full repaint the flag asks for
    call tp_redraw
    jmp .done
.out:
    cmp byte [tp_ldfull], 0     ; a click on nothing, with a load under it
    jne .draw
.done:
    pop si
    ret

; Focus to the source pane, or to the preview: a ring redrawn only when it
; MOVES, and the caret hidden when it leaves the source.
tp_focus_src:
    cmp byte [tp_focus], 0
    je .o
    mov byte [tp_focus], 0
    jmp tp_draw_frames
.o:
    ret

tp_focus_prv:
    cmp byte [tp_focus], 1
    je tp_focus_src.o
    mov byte [tp_focus], 1
    call tp_caret_hide
    jmp tp_draw_frames

tp_click_caret:
    push ax
    push bx
    push cx
    push dx
    ; row
    mov ax, dx
    sub ax, [tp_ey1]
    sub ax, 3
    jns .r
    xor ax, ax
.r:
    mov bl, 8
    div bl
    xor ah, ah
    add ax, [tp_vscroll]
    mov [tp_tline], ax
    ; col
    mov ax, cx
    sub ax, [tp_ex1]
    sub ax, 3
    jns .c
    xor ax, ax
.c:
    mov bl, 8
    div bl
    xor ah, ah
    mov [tp_wantcol], ax
    mov ax, [tp_tline]
    call tp_goto_lc
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AX = a ROW, [tp_wantcol] = a column in it -> tp_cur.
tp_goto_lc:
    push ax
    push bx
    push cx
    push si
    call tp_row_at
    jc .eof
    ; SI start CX len
    mov ax, [tp_wantcol]
    cmp ax, cx
    jbe .ok
    mov ax, cx
.ok:
    add si, ax
    mov [tp_cur], si
    jmp .o
.eof:
    mov ax, [tp_srclen]
    mov [tp_cur], ax
.o:
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_prev_page:
    cmp word [tp_ppage], 0
    je .o
    dec word [tp_ppage]
    mov word [tp_pscroll], 0
.o:
    ret

tp_next_page:
    mov ax, [tp_npages]
    or ax, ax
    jz .o
    dec ax
    cmp [tp_ppage], ax
    jae .o
    inc word [tp_ppage]
    mov word [tp_pscroll], 0
.o:
    ret

; -----------------------------------------------------------------------------
tp_oncmd:
    call tp_deferred_ld
    cmp ah, 2
    jb .fe
    je .lay
    cmp ah, 3
    je .page
    cmp ah, 4
    jne .o
    ; View
    or al, al
    jz .set
    cmp al, 2
    jb .vp
    je .vn
    cmp al, 4
    ja .o
    mov ax, 24                  ; Wider Source / Wider Preview: the split
    je .wp                      ; (the flags are still cmp al, 4's)
    add [tp_split], ax
    jmp short .ws
.wp:
    sub [tp_split], ax
.ws:
    call tp_clamp_split
    jmp tp_redraw
.set:
    call tp_typeset
    jmp tp_redraw
.vp:
    call tp_prev_page
    jmp tp_redraw
.vn:
    call tp_next_page
    jmp tp_redraw
.fe:
    or ah, ah
    jnz .edit
    or al, al                   ; File
    jz .new
    cmp al, 2
    jb tp_open_dlg              ; 1 Open...
    je .save
    cmp al, 5
    ja .o
    sub al, 2                   ; 3 Save As..., 4 Export PDF..., 5 Export
    mov [tp_dlgmode], al        ; PS... are tp_ondlg's modes 1, 2 and 3
    mov al, FDLG_SAVE
    jmp tp_dlg
.new:
    call tp_new                 ; a document that REPLACES this one is set on
    jmp tp_redraw               ; arrival - File > Open and a double-clicked
                                ; .TEX already were, and a blank preview
                                ; beside a fresh document was the odd one out
                                ; (SPEC.md 69.1)
.save:
    call tp_save
    jmp tp_redraw
.edit:
    or al, al
    jz .copy
    cmp al, 2
    jb .paste
    ja .o
    call tp_cut_line
    jmp tp_redraw
.copy:
    call tp_copy_all
    jmp tp_redraw
.paste:
    call tp_paste
    jmp tp_redraw
.lay:
    cmp al, 1                   ; Layout: Article, Book, then the codes
    jb .art
    je .book
    cmp al, 10
    ja .o
    mov bx, tp_mlay - 2
    jmp short .code
.art:
    mov byte [tp_bind], 0
    jmp short .rely
.book:
    cmp byte [tp_bind], 2
    je .rely
    mov byte [tp_bind], 1
    jmp short .rely
.page:
    cmp al, 12
    ja .o
    mov bx, tp_mpage
.code:
    xlatb                       ; AL = (an offset from tp_psize) << 4 | value
    mov bl, al
    and al, 0x0F
    mov cl, 4
    shr bl, cl
    xor bh, bh
    mov [tp_psize+bx], al
.rely:
    mov byte [tp_needset], 1
    jmp tp_redraw
.o:
    ret

; Every Layout item after Article and Book, and every Page item, sets ONE of
; the page options to ONE value: offset from tp_psize in the high nibble, the
; value in the low (tp_psize's map in the bss has the order)
tp_mlay:    db 0x40, 0x41, 0x42         ; Narrow, Normal, Wide Margins
            db 0x30, 0x31, 0x32         ; Compact, Normal, Loose Padding
            db 0x10, 0x11, 0x12         ; Single, Facing, Opposing Pages
tp_mpage:   db 0x00, 0x01, 0x02, 0x03   ; Letter, Legal, A4, A5
            db 0x20, 0x21, 0x22         ; Gutter Off, Narrow, Wide
            db 0x50, 0x51               ; Inches, Millimetres
            db 0x60, 0x61, 0x62, 0x63   ; Numbers Off, Bottom, Header, Outer

; File > Open and Ctrl+O
tp_open_dlg:
    mov byte [tp_dlgmode], 0
    mov al, FDLG_OPEN
    jmp short tp_dlg

; File > New and Ctrl+N: the seed document, set
tp_new:
    call tp_seed
    jmp tp_typeset

tp_dlg:
    push bx
    push si
    push di
    mov bx, [tp_win]
    or bx, bx
    jnz .w
    mov bx, si
.w:
    mov di, tp_ondlg
    mov si, tp_fname
    cmp byte [si], 0
    jne .nm
    mov si, tp_s_hello
.nm:
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

tp_ondlg:                       ; (the completion proc owes nothing back,
    mov si, di                  ; SPEC.md 38.6)
    mov di, tp_dlgname
    call tp_kname
    call OSAPI_FILE_HERE
    mov [tp_dir], dx
    mov [tp_drv], bl
    mov al, [tp_dlgmode]
    mov si, tp_dlgname
    cmp al, 2
    jb .file
    je .pdf
    cmp al, 3
    jne .fin
    call tp_export_ps
    jmp short .fin
.pdf:
    call tp_export_pdf
    jmp short .fin
.file:                          ; 0 Open and 1 Save As name the document
    mov di, tp_fname
    call tp_cpat
    mov byte [di], 0
    or al, al
    jnz .save
    call tp_load
    jc .fin
    call tp_typeset
    jmp short .fin
.save:
    call tp_save
.fin:
    call tp_retitle
    jmp tp_redraw               ; SI is tp_redraw's own business now

; SI = a name in the KERNEL's segment (an ARG_FILE or a chooser's answer):
; at most 13 bytes of it to DI, and the NUL. ES is ours after.
tp_kname:
    push ax
    push cx
    mov ax, KERNEL_SEG
    mov es, ax
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .cp
    mov byte [di], 0
.named:
    push ds
    pop es
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
tp_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    xor bx, bx                  ; THE SIZE PROBE (SPEC.md 18.4.1): a read with
    xor cx, cx                  ; no room answers FERR_BIG and DX = the KB the
    xor dx, dx                  ; read needs, off the directory entry and with
    mov si, tp_fname            ; no data I/O - or CF=0 and nothing for an
    call OSAPI_FILE_READ        ; empty file
    jnc .sized
    cmp ax, FERR_BIG
    jne .err
    xchg ax, dx                 ; AX = the KB it needs
    cmp ax, TP_SRC_KB
    ja .err                     ; past the ceiling: what the 8190-byte read
                                ; always answered, in the same words
    mov cl, 10
    shl ax, cl
.sized:
    call tp_srcfit              ; ONLY EVER A GROW here: the document is still
    jc .nomem                   ; in the claim and a read that fails leaves it
    mov cx, [tp_srccap]         ; there. tp_srctrim gives the rest back after
    cmp cx, TP_SRC_MAX
    jbe .cap
    mov cx, TP_SRC_MAX
.cap:
    mov es, [tp_srcseg]
    xor bx, bx
    xor dx, dx
    mov si, tp_fname
    call OSAPI_FILE_READ
    jc .err
    cmp ax, TP_SRC_MAX
    jbe .ok
    mov ax, TP_SRC_MAX
.ok:
    mov [tp_srclen], ax
    call tp_crlf_fix
    call tp_srctrim
    call tp_row_inval
    mov word [tp_cur], 0
    mov word [tp_vscroll], 0
    mov byte [tp_selon], 0      ; the selection is a pair of offsets into the
                                ; document that just went (the same for the
                                ; new document below, a cut and a paste)
    mov byte [tp_dirty], 0
    mov byte [tp_needset], 1
    mov si, tp_s_loaded
    call tp_toast
    clc
    jmp .out
.nomem:
    mov si, tp_s_nomem
    jmp short .say
.err:
    mov si, tp_s_err
.say:
    call tp_toast
    stc
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_crlf_fix:
    ; collapse CR LF -> LF, lone CR -> LF
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [tp_srcseg]
    xor si, si
    xor di, di
    mov cx, [tp_srclen]
    jcxz .z
.lp:
    mov al, [es:si]
    inc si
    cmp al, 13
    jne .st
    mov al, 10
    cmp si, [tp_srclen]
    jae .st
    cmp byte [es:si], 10
    jne .st
    inc si
.st:
    mov [es:di], al
    inc di
    cmp si, [tp_srclen]
    jb .lp
    mov [tp_srclen], di
.z:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es
    cmp byte [tp_fname], 0
    jne .nm
    mov si, tp_s_paper
    mov di, tp_fname
    call tp_cpat
    mov byte [di], 0
.nm:
    mov es, [tp_srcseg]
    xor bx, bx
    mov cx, [tp_srclen]
    cmp cx, TP_SRC_MAX
    jbe .len
    mov cx, TP_SRC_MAX
.len:
    xor dx, dx
    mov si, tp_fname
    call OSAPI_FILE_WRITE
    jc .err
    mov byte [tp_dirty], 0
    call tp_retitle
    mov si, tp_s_saved
    call tp_toast
    jmp .out
.err:
    mov si, tp_s_werr
    call tp_toast
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

tp_seed:
    push ax
    push cx
    push si
    push di
    push es
    mov es, [tp_srcseg]         ; the seed is under TP_SRC_KB0, so it fits
    mov si, tp_seed_s
    xor di, di
.cp:
    lodsb
    mov [es:di], al
    inc di
    or al, al
    jnz .cp
    dec di
    mov [tp_srclen], di
    call tp_srctrim             ; ...and a document that was bigger gives its
    call tp_row_inval           ; KB back
    mov word [tp_cur], 0
    mov word [tp_vscroll], 0
    mov byte [tp_selon], 0
    mov si, tp_s_untitled
    mov di, tp_fname
    call tp_cpat
    mov byte [di], 0
    mov byte [tp_dirty], 0
    mov byte [tp_needset], 1
.o:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

tp_copy_all:
    push ax
    push cx
    push si
    push es
    mov es, [tp_srcseg]
    xor si, si
    mov cx, [tp_srclen]
    call OSAPI_CLIP_PUT
    mov si, tp_s_copied
    call tp_toast
.o:
    pop es
    pop si
    pop cx
    pop ax
    ret

tp_paste:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call tp_clamp_cur
    call OSAPI_CLIP_SIZE
    jc .o
    or ax, ax
    jz .o
    mov dx, ax                  ; paste len
    mov bx, [tp_cur]
    call tp_open_gap            ; BX=cur DX=len; CF=1 and SI = why not
    jc .say
    mov es, [tp_srcseg]         ; ...re-read: the gap may have grown it
    mov di, [tp_cur]
    mov cx, dx
    call OSAPI_CLIP_GET
    ; CX is what CLIP_GET actually COPIED, not what CLIP_SIZE promised. The
    ; clipboard belongs to the desktop and this app is pre-empted between the
    ; two calls (SPEC.md 20.6), so another program may have replaced it with a
    ; shorter one in between. Advancing by DX would splice that many bytes of
    ; whatever was already in the claim into the document.
    add [tp_cur], cx
    add [tp_srclen], cx
    call tp_crlf_fix
    ; crlf_fix SHRINKS srclen by one byte per CR LF pair it collapses and knows
    ; nothing about the caret, so a paste of CRLF text at the end of the buffer
    ; leaves tp_cur PAST tp_srclen. The next tp_ins then hands tp_open_gap an
    ; offset beyond the end - see the guard there for what that used to do.
    call tp_row_inval           ; every other mutation invalidates the row
                                ; cache (tp_ins, tp_del_range2, tp_load,
                                ; tp_seed) - a paste ABOVE the cached offset
                                ; that skipped this left tp_cur_lc walking
                                ; rows from mid-pasted-text: wrong caret row,
                                ; wrong vscroll, until the next edit healed it
    call tp_clamp_cur
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
    jmp short .o
.say:
    call tp_toast
.o:
    mov byte [tp_selon], 0      ; every offset after the caret moved
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; The caret is an offset into the document and every path that shortens the
; document has to bring it back inside, or the next edit works from an offset
; that is past the end. Cheap enough to call unconditionally.
tp_clamp_cur:
    push ax
    mov ax, [tp_srclen]
    cmp [tp_cur], ax
    jbe .o
    mov [tp_cur], ax
.o:
    pop ax
    ret

; BX = offset, DX = bytes to open; the claim grows to hold them first
; (tp_srcfit). CF=1 no room, and SI = what to say: 'Document full (8K)' past
; TP_SRC_MAX, 'Need more RAM' when the heap would not grow the claim.
tp_open_gap:
    push ax
    push cx
    push di
    push ds
    push es
    mov ax, [tp_srclen]
    ; BX PAST the end is the caller's bug, and refusing is the only safe answer
    ; to it: the tail length below is srclen - BX, which UNDERFLOWS to ~64KB
    ; when BX is bigger, and the std/rep movsb under it then walks backwards
    ; over the whole 64KB segment the claim sits in - the claim heap above it
    ; included (SPEC.md 50.3). Every caller is checked, but this is the one
    ; place the damage is unbounded, so the check lives here too.
    cmp bx, ax
    ja .full
    add ax, dx
    jc .full                    ; (a clipboard bigger than the segment)
    cmp ax, TP_SRC_MAX
    ja .full
    call tp_srcfit
    jc .ram
    mov cx, [tp_srclen]
    sub cx, bx
    jz .ok
    push si
    mov si, [tp_srclen]
    dec si
    mov di, si
    add di, dx
    mov ax, [tp_srcseg]
    mov ds, ax
    mov es, ax
    std
    rep movsb
    cld
    pop si
.ok:
    clc
    jmp short .o
.ram:
    mov si, tp_s_nomem
    jmp short .bad
.full:
    mov si, tp_s_full
.bad:
    stc
.o:
    pop es
    pop ds
    pop di
    pop cx
    pop ax
    ret

tp_cut_line:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    ; THE LOGICAL LINE, not the row: this is the Edit menu's 'Cut Line' and
    ; what it takes out has to be a line of the FILE, newline and all.
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    call tp_lstart
    mov bx, si                  ; BX = where it starts
    call tp_lnext               ; SI = where the next one does
    mov cx, si
    sub cx, bx                  ; CX = its length, the newline included
.cut:
    push bx
    mov si, bx
    call OSAPI_CLIP_PUT
    pop bx
    mov [tp_tmp3], cx
    call tp_del_range2
    mov [tp_cur], bx
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
.o:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_del_range2:
    ; BX=off, [tp_tmp3]=count
    call tp_row_inval           ; every row after this one starts somewhere
    push ax                     ; else now
    push cx
    push si
    push di
    push ds
    push es
    mov cx, [tp_srclen]
    cmp bx, cx
    jae .none
    mov ax, bx
    add ax, [tp_tmp3]
    jnc .sum
    mov ax, cx
.sum:
    cmp ax, cx
    jbe .ok
    mov ax, cx
.ok:
    sub ax, bx
    mov [tp_tmp3], ax           ; clamped count
    or ax, ax
    jz .none
    mov si, bx
    add si, ax                  ; first kept byte
    mov di, bx
    mov cx, [tp_srclen]
    sub cx, si                  ; tail
    jnc .tail
    xor cx, cx
.tail:
    mov ax, [tp_srcseg]
    mov ds, ax
    mov es, ax
    cld
    jcxz .z
    rep movsb
.z:
    pop es
    pop ds
    mov ax, [tp_srclen]
    sub ax, [tp_tmp3]
    mov [tp_srclen], ax
    pop di
    pop si
    pop cx
    pop ax
    ret
.none:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret

; AL = char to insert
tp_ins:
    push bx
    push dx
    push si
    push di
    push es
    call tp_clamp_cur
    mov dx, 1
    mov bx, [tp_cur]
    call tp_open_gap            ; (keeps AL)
    jc .no
    mov es, [tp_srcseg]
    mov di, [tp_cur]
    mov [es:di], al
    inc word [tp_cur]
    inc word [tp_srclen]
    call tp_row_inval
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
    jmp short .o
.no:
    call tp_toast               ; SI = why
.o:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    ret

tp_backsp:
    cmp word [tp_cur], 0
    je .o
    dec word [tp_cur]
    call tp_delete
.o:
    ret

tp_delete:
    push ax
    push bx
    push cx
    push si
    push es
    mov ax, [tp_cur]
    cmp ax, [tp_srclen]
    jae .o
    mov bx, ax
    mov word [tp_tmp3], 1
    mov es, [tp_srcseg]
    mov si, bx
    mov al, [es:si]
    mov byte [tp_edkind], 3
    cmp al, 10
    je .nl
    cmp al, 13
    jne .one
.nl:
    mov byte [tp_edkind], 4
    inc si
    cmp si, [tp_srclen]
    jae .one
    cmp byte [es:si], 10
    jne .one
    mov word [tp_tmp3], 2
.one:
    call tp_del_range2
    mov byte [tp_dirty], 1
    mov byte [tp_needset], 1
.o:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_go_left:
    cmp word [tp_cur], 0
    je .o
    dec word [tp_cur]
    push es
    mov es, [tp_srcseg]
    mov si, [tp_cur]
    cmp byte [es:si], 10
    jne .d
    or si, si
    jz .d
    dec si
    cmp byte [es:si], 13
    jne .d
    mov [tp_cur], si
.d:
    pop es
    call tp_cur_lc
    mov [tp_wantcol], bx
.o:
    ret

tp_go_right:
    mov ax, [tp_cur]
    cmp ax, [tp_srclen]
    jae .o
    push es
    mov es, [tp_srcseg]
    mov si, ax
    mov al, [es:si]
    inc si
    cmp al, 13
    jne .d
    cmp si, [tp_srclen]
    jae .d
    cmp byte [es:si], 10
    jne .d
    inc si
.d:
    mov [tp_cur], si
    pop es
    call tp_cur_lc
    mov [tp_wantcol], bx
.o:
    ret

; A ROW up and a row down, not a line: on a wrapped line the two are not the
; same move, and the row is the one the reader is looking at.
tp_up:
    push ax
    push bx
    call tp_cur_lc
    or ax, ax
    jz .o
    mov [tp_wantcol], bx
    dec ax
    call tp_goto_lc
.o:
    pop bx
    pop ax
    ret

tp_down:
    push ax
    push bx
    push cx
    push si
    call tp_cur_lc
    mov [tp_wantcol], bx
    inc ax
    push ax
    call tp_row_at              ; is there a row below at all?
    pop ax
    jc .o
    call tp_goto_lc
.o:
    pop si
    pop cx
    pop bx
    pop ax
    ret

tp_home:
    push ax
    call tp_cur_lc
    mov word [tp_wantcol], 0
    call tp_goto_lc
    pop ax
    ret

tp_end:
    push ax
    push cx
    push si
    call tp_cur_lc
    push ax
    call tp_row_at
    pop ax
    jc .o
    add si, cx
    mov [tp_cur], si
    mov word [tp_wantcol], 255
.o:
    pop si
    pop cx
    pop ax
    ret

; AX = the rows a page moves: the pane's, or 8 before it has been laid out
tp_prows:
    mov ax, [tp_erows]
    or ax, ax
    jnz .o
    mov al, 8
.o:
    ret

; [BX] -= AX, stopping at 0 (the source's and the preview's scrolls)
tp_subz:
    sub [bx], ax
    jnc .o
    and word [bx], 0
.o:
    ret

tp_clamp_vs:
    push ax
    push bx
    call tp_prows
    xchg ax, bx
    call tp_count_rows          ; (keeps BX)
    sub ax, bx
    jnc .ok
    xor ax, ax
.ok:
    cmp [tp_vscroll], ax
    jbe .o
    mov [tp_vscroll], ax
.o:
    pop bx
    pop ax
    ret

tp_pgup:
    call tp_prows
    mov bx, tp_vscroll
    call tp_subz
    call tp_clamp_vs
    call tp_cur_lc
    mov ax, [tp_vscroll]
    jmp tp_goto_lc

tp_pgdn:
    call tp_prows
    add [tp_vscroll], ax
    call tp_clamp_vs
    call tp_cur_lc
    mov ax, [tp_vscroll]
    add ax, [tp_erows]
    dec ax
    jns .g
    xor ax, ax
.g:
    jmp tp_goto_lc

%ifdef OS88UI_SBDRAG
tp_nodrag:  db 0                ; 0xFF = this kernel has no tracking edge
tp_sbd_bar: db 0                ; which bar the live gesture is on: 0 source,
                                ; 1 preview

; -----------------------------------------------------------------------------
; tp_sbd_which - which of the two bars is the live gesture on? (SPEC.md
;                13.10.7.2)
; out: CF = 1 = none of ours; CF = 0, BX = that bar's block and [tp_sbd_bar]
;      set. AX preserved.
;
; THIS APP IS WHY os88ui_sbmine EXISTS AT ALL (13.10.5.10). One gesture record,
; two bars - the source's counting lines and the preview's counting pixels -
; and the record is asked which it belongs to rather than the caller guessing
; from the pointer, because the pointer may be a long way off the bar by then
; (13.10.5.2) and a drop on the wrong bar SPENDS somebody else's gesture.
; -----------------------------------------------------------------------------
tp_sbd_which:
    call os88ui_sbdragging
    jc .no
    push ax
    call tp_src_metrics
    call tp_ssb_sync
    mov bx, tp_r_ssb
    call os88ui_sbmine
    jnc .src
    call tp_psb_sync
    mov bx, tp_r_psb
    call os88ui_sbmine
    jc .nope
    mov byte [tp_sbd_bar], 1
    jmp short .yes
.src:
    mov byte [tp_sbd_bar], 0
.yes:
    pop ax
    clc
    ret
.nope:
    pop ax
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; tp_sbd_commit - put the dragged pos into the bar it belongs to, and draw
; in:  AX = the pos, [tp_sbd_bar] = which; gfx lock held
; out: nothing much; both tails redraw their own pane
;
; The two clamps are not interchangeable: tp_clamp_ps forces an EVEN scroll
; because the desk beside the sheet is a 50% dither whose phase is a function
; of absolute y, and tp_clamp_vs bounds a line count. The one-pixel
; disagreement an odd pos leaves between the thumb and the sheet is below
; anything a reader can see at preview scale.
; -----------------------------------------------------------------------------
tp_sbd_commit:
    cmp byte [tp_sbd_bar], 0
    jne .prev
    call tp_caret_hide          ; ...at the scroll they were drawn at, before
    call tp_sel_hide            ; the view moves out from under them
    mov [tp_vscroll], ax
    call tp_clamp_vs
    jmp tp_src_scroll
.prev:
    mov [tp_pscroll], ax
    call tp_clamp_ps
    jmp tp_prev_scroll
%endif

tp_psb_click:
    push ax
    push bx
    call tp_psb_sync
    mov bx, tp_r_psb
    call os88ui_sbhit
    mov bx, tp_pscroll
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .dn
    cmp al, OS88UI_SBPGUP
    je .pu
    cmp al, OS88UI_SBPGDN
    je .pd
    mov bx, tp_r_psb            ; (the bar, which the grab takes in BX)
    call tp_sbthumb
    jmp short .o
.up:
    mov ax, TP_SB_STEP * 8
    jmp short .sub
.pu:
    mov ax, [tp_r_psb+10]
.sub:
    call tp_subz
    jmp short .cl
.dn:
    mov ax, TP_SB_STEP * 8
    jmp short .add
.pd:
    mov ax, [tp_r_psb+10]
.add:
    add [bx], ax
.cl:
    call tp_clamp_ps
.o:
    pop bx
    pop ax
    ret

; AL = what the click hit, BX = the bar: the THUMB grabs a drag where this kernel can track
; one (SPEC.md 13.10.5), anything else is nothing. The element does not care
; whether the bar counts lines or pixels (13.10.7.2).
tp_sbthumb:
%ifdef OS88UI_SBDRAG
    cmp al, OS88UI_SBTHUMB
    jne .o
    cmp byte [tp_nodrag], 0
    jne .o
    mov ax, TP_SBRATE | (TP_SBRATE286 << 8)
    call os88ui_sbrate          ; the rate THIS machine can afford
    jmp os88ui_sbgrab           ; (SPEC.md 13.10.5.4.1)
.o:
%endif
    ret

; Arrow cells step a line, the track pages, the thumb drags (SPEC.md 13.10.1).
tp_ssb_click:
    push ax
    push bx
    call tp_src_metrics
    call tp_ssb_sync
    mov bx, tp_r_ssb
    call os88ui_sbhit
    mov bx, tp_vscroll
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .dn
    cmp al, OS88UI_SBPGUP
    je .pu
    cmp al, OS88UI_SBPGDN
    je .pd
    mov bx, tp_r_ssb            ; (the bar, which the grab takes in BX)
    call tp_sbthumb
    jmp short .o
.up:
    mov ax, TP_SB_STEP
    jmp short .sub
.pu:
    call tp_prows
.sub:
    call tp_subz
    jmp short .cl
.dn:
    mov ax, TP_SB_STEP
    jmp short .add
.pd:
    call tp_prows
.add:
    add [bx], ax
.cl:
    call tp_clamp_vs
.o:
    pop bx
    pop ax
    ret

; The caret is on screen or the view moves to put it there. It is only ever
; VERTICAL now: a row is the whole width of the pane by construction, so
; there is nothing to scroll sideways and [tp_hscroll] is gone with the
; horizontal arrowing it existed for.
tp_keep_caret:
    push ax
    push bx
    push dx
    call tp_cur_lc              ; AX = row
    cmp ax, [tp_vscroll]
    jae .dn
    mov [tp_vscroll], ax
    jmp short .o
.dn:
    mov dx, [tp_erows]
    or dx, dx
    jnz .er
    mov dx, 8
.er:
    add dx, [tp_vscroll]
    or dx, dx
    jz .o
    dec dx
    cmp ax, dx
    jbe .o
    sub ax, [tp_erows]
    inc ax
    jns .sv
    xor ax, ax
.sv:
    mov [tp_vscroll], ax
.o:
    pop dx
    pop bx
    pop ax
    ret

tp_retitle:
    push ax
    push si
    push di
    push es
    push ds
    pop es
    mov di, tp_ttlbuf
    mov si, tp_s_name
    call tp_cpat
    mov al, ' '
    stosb
    mov al, '-'
    stosb
    mov al, ' '
    stosb
    mov si, tp_fname
    cmp byte [si], 0
    jne .n
    mov si, tp_s_noload
.n:
    call tp_cpat
    cmp byte [tp_dirty], 0
    je .z
    mov al, '*'
    stosb
.z:
    mov byte [di], 0
    mov bx, [tp_win]
    or bx, bx
    jz .o
    mov ax, tp_ttlbuf
    call OSAPI_WM_TITLE
.o:
    pop es
    pop di
    pop si
    pop ax
    ret

tp_toast:
    push ax
    push cx
    push es
    push ds
    pop es
    mov cx, 54
    call OSAPI_TOAST
    pop es
    pop cx
    pop ax
    ret

tp_cpat:
    push ax
    push ds
    pop es
.l:
    lodsb
    or al, al
    jz .d
    stosb
    jmp .l
.d:
    pop ax
    ret

; tp_cpat with a ceiling: CX = the most bytes to copy. Out CX = what is left of
; it, so several of these can fill one buffer between them, and DI stops where
; the copy stopped. The NUL is the caller's, as with tp_cpat.
;
; Every destination here is a fixed bss field and most of the sources are
; DOCUMENT text, which is as long as whoever wrote the file made it - so a
; plain tp_cpat into a 48-byte field is a buffer overrun with the length under
; the file's control. This is what those call sites use instead.
tp_cpatn:
    push ax
.l:
    jcxz .d
    lodsb
    or al, al
    jz .d
    stosb
    dec cx
    jmp .l
.d:
    pop ax
    ret

tp_u16_di:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    push ds
    pop es
    mov bx, 10
    xor cx, cx
    or ax, ax
    jnz .d
    mov al, '0'
    stosb
    jmp .o
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.p:
    pop ax
    add al, '0'
    stosb
    loop .p
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

    %include "texpad/tpparse.inc"
    %include "texpad/tpexport.inc"

; -----------------------------------------------------------------------------
tp_tpl:
    dw 6, 22, TP_WIN_W, TP_WIN_H
    dw tp_s_name, tp_paint, tp_onkey, tp_onclick

    OS88_MENUSET tp_menus, tp_s_name, tp_oncmd
        OS88_MENU tp_m_file, tp_i_file, 6
        OS88_MENU tp_m_edit, tp_i_edit, 3
        OS88_MENU tp_m_lay, tp_i_lay, 11
        OS88_MENU tp_m_page, tp_i_page, 13
        OS88_MENU tp_m_view, tp_i_view, 5
    OS88_MENUSET_END tp_menus

tp_s_name:      db 'TeXPad', 0
tp_m_file:      db 'File', 0
tp_m_edit:      db 'Edit', 0
tp_m_lay:       db 'Layout', 0
tp_m_page:      db 'Page', 0
tp_m_view:      db 'View', 0
tp_i_file:      dw tp_it_new, tp_it_open, tp_it_save, tp_it_saveas, tp_it_pdf, tp_it_ps
tp_i_edit:      dw tp_it_copy, tp_it_paste, tp_it_cut
tp_i_lay:       dw tp_it_art, tp_it_book, tp_it_mn, tp_it_m1, tp_it_mw
                dw tp_it_pc, tp_it_pn, tp_it_pl
                dw tp_it_sgl, tp_it_fac, tp_it_opp
tp_i_page:      dw tp_it_ltr, tp_it_leg, tp_it_a4, tp_it_a5
                dw tp_it_g0, tp_it_g1, tp_it_g2
                dw tp_it_uin, tp_it_umm
                dw tp_it_n0, tp_it_n1, tp_it_n2, tp_it_n3
tp_i_view:      dw tp_it_set, tp_it_vp, tp_it_vn, tp_it_ws, tp_it_wp
tp_it_new:      db 'New', 0
tp_it_open:     db 'Open...', 0
tp_it_save:     db 'Save', 0
tp_it_saveas:   db 'Save As...', 0
tp_it_pdf:      db 'Export PDF...', 0
tp_it_ps:       db 'Export PS...', 0
tp_it_copy:     db 'Copy All', 0
tp_it_paste:    db 'Paste', 0
tp_it_cut:      db 'Cut Line', 0
tp_it_art:      db 'Article', 0
tp_it_book:     db 'Book', 0
tp_it_mn:       db 'Narrow Margins', 0
tp_it_m1:       db 'Normal Margins', 0
tp_it_mw:       db 'Wide Margins', 0
tp_it_pc:       db 'Compact Padding', 0
tp_it_pn:       db 'Normal Padding', 0
tp_it_pl:       db 'Loose Padding', 0
tp_it_sgl:      db 'Single Pages', 0
tp_it_fac:      db 'Facing Pages', 0
tp_it_opp:      db 'Opposing Pages', 0
tp_it_ltr:      db 'Letter 8.5 x 11 in', 0
tp_it_leg:      db 'Legal 8.5 x 14 in', 0
tp_it_a4:       db 'A4 210 x 297 mm', 0
tp_it_a5:       db 'A5 148 x 210 mm', 0
tp_it_g0:       db 'Gutter Off', 0
tp_it_g1:       db 'Gutter Narrow', 0
tp_it_g2:       db 'Gutter Wide', 0
tp_it_uin:      db 'Inches', 0
tp_it_umm:      db 'Millimetres', 0
tp_it_n0:       db 'Numbers Off', 0
tp_it_n1:       db 'Numbers Bottom Center', 0
tp_it_n2:       db 'Numbers Header', 0
tp_it_n3:       db 'Numbers Outer', 0
tp_it_set:      db 'Typeset', 0
tp_it_vp:       db 'Prev Page', 0
tp_it_vn:       db 'Next Page', 0
tp_it_ws:       db 'Wider Source', 0
tp_it_wp:       db 'Wider Preview', 0

tp_s_hello:     db 'HELLO.TEX', 0
tp_s_paper:     db 'PAPER.TEX', 0
tp_s_untitled:  db 'UNTITLED.TEX', 0
tp_s_noload:    db '(new)', 0
tp_s_lt:        db '<', 0           ; the page arrows, in button order
                db '>', 0
tp_s_stale:     db '(edit)', 0
tp_s_wrap:      db TP_WRAP_MARK, 0      ; the last column of a continued row
; Each option's names IN ORDER, one string after another, for tp_pick: the
; status strip's (tp_l_*) and the bar's shorter ones (tp_l_b*)
tp_l_paper:     db 'Letter', 0, 'Legal', 0, 'A4', 0, 'A5', 0
tp_l_marg:      db '.75in', 0, '1in', 0, '1.5in', 0         ; units 0
                db '19mm', 0, '25mm', 0, '38mm', 0          ; units 1
tp_l_bind:      db '1pg', 0, 'Fac', 0, 'Opp', 0
tp_l_gut:       db 'G0', 0, 'G.25in', 0, 'G.5in', 0
                db 'G0', 0, 'G6mm', 0, 'G13mm', 0
tp_l_num:       db 'No#', 0, 'Ft', 0, 'Hd', 0, 'Out', 0
tp_l_bpap:      db 'Ltr', 0, 'Leg', 0, 'A4', 0, 'A5', 0
tp_l_bgut:      db 'G0', 0, 'G+', 0, 'G++', 0
tp_l_bpad:      db 'Pad-', 0, 'Pad', 0, 'Pad+', 0
tp_s_noset:     db 'F5 typesets the preview', 0
tp_s_loaded:    db 'Loaded', 0
tp_s_saved:     db 'Saved', 0
tp_s_copied:    db 'Copied', 0
tp_s_err:       db 'Read error: new document', 0
tp_s_werr:      db 'Write error', 0
tp_s_nomem:     db 'Need more RAM', 0
tp_s_full:      db 'Document full (8K)', 0
tp_s_xok:       db 'Exported', 0
tp_s_xerr:      db 'Export failed', 0
tp_s_xbig:      db 'Export too large', 0
; The About card, in the shape apps/paint and apps/modplug use it: what the
; program is, who contributed it, and one honest line about what is NOT in
; here. A reader who has met TeX should not be left thinking this is TeX.
tp_s_a1:        db 'TeXPad for os8088', 0       ; tp_draw_about walks these
                db 'a TeX pad for the 8086', 0  ; as a run, one line each
                db 0
                db 'contributed by Jason Page', 0
                db 'amfile.org', 0
                db 0
                db 'A paper-oriented SUBSET of the markup, set in', 0
                db 'the kernel 8x8 monofont. No math engine, no', 0
                db 'figures, no colour - see SPEC.md 69.2.', 0
                db 0
                db 'F5 typesets.  File exports PDF 1.4 and PostScript.', 0
                db 0
                db 'Long lines WRAP; a row that continues is marked >', 0
tp_s_aend:

tp_seed_s:
    db '\documentclass[letterpaper,12pt]{article}', 10
    db '\title{Untitled}', 10
    db '\author{}', 10
    db '\date{}', 10
    db '\begin{document}', 10
    db '\maketitle', 10
    db '\section{Introduction}', 10
    db 10
    db 'Type on the left. F5 typesets the preview.', 10
    db 10
    db '\end{document}', 10
    db 0

%define OS88UI_SCROLL           ; SPEC.md 13.10: the shared scroll bar
%define OS88UI_SBUPD            ; ...and its whole-or-thumb choice, for both bars (13.10.3.2)
    %define OS88UI_NOGLYPH          ; no check box or radio drawn here (SPEC.md 13.8.9)
    %define OS88UI_NOGEST           ; presses routed by hand: no gesture install (SPEC.md 20.5.1.3.4)
    %define OS88UI_NOBFIND          ; hit-tests its own rects: no os88ui_bfind (SPEC.md 20.5.1.3.4)
    %include "os88ui.inc"

; ---- bss --------------------------------------------------------------------
; EVERY FIELD IS ONE TPB LINE AND ITS OFFSET IS COUNTED, NOT TYPED (SPEC.md
; 69.4). The map used to be a hand-computed column of `equ`s with nothing
; checking them against each other, so a field that grew moved everything under
; it and two fields could OVERLAP without a word from the assembler. Here the
; next offset is the last one plus its size, so two fields cannot share a byte
; unless a TPB_UNION says they may - and the block is exactly as big as what is
; in it: TP_BSS_TOTAL is TPB_AT, not a round number with slack under it.
;
; A union is for buffers that are NEVER LIVE AT THE SAME TIME, and the one
; below is the typesetter's scratch against the exporter's and the top bar's.
; tp_typeset runs to completion inside one handler; an export calls it first
; and only then writes; the bar string is composed and drawn in one call.
%assign TPB_AT 0
%macro TPB 2
%1 equ os88_image_end + TPB_AT
%assign TPB_AT TPB_AT + (%2)
%endmacro
%macro TPB_UNION 0
%assign TPB_U0 TPB_AT
%assign TPB_UMAX TPB_AT
%endmacro
%macro TPB_ARM 0                ; the next arm starts where the union did
%if TPB_AT > TPB_UMAX
%assign TPB_UMAX TPB_AT
%endif
%assign TPB_AT TPB_U0
%endmacro
%macro TPB_UNION_END 0
%if TPB_AT < TPB_UMAX
%assign TPB_AT TPB_UMAX
%endif
%endmacro

    TPB tp_win,      2
    TPB tp_ox,       2
    TPB tp_oy,       2
    TPB tp_cw,       2
    TPB tp_ch,       2
    TPB tp_srcseg,   2
    TPB tp_srclen,   2
    TPB tp_srccap,   2          ; the source claim's size in BYTES (tp_srcfit)
    TPB tp_expseg,   2          ; the export claim, 0 between exports
    TPB tp_explen,   2
    TPB tp_expcap,   2          ; ...its size in bytes (tp_xgrow)
    TPB tp_dir,      2
    TPB tp_drv,      2
    TPB tp_cur,      2
    TPB tp_wantcol,  2
    TPB tp_vscroll,  2
    TPB tp_pscroll,  2
    TPB tp_ppage,    2
    TPB tp_npages,   2
    TPB tp_split,    2
    TPB tp_focus,    1
    TPB tp_dirty,    1
    TPB tp_needset,  1
    TPB tp_setok,    1
    TPB tp_needld,   1
    TPB tp_abouton,  1
    TPB tp_dlgmode,  1
; THE PAGE OPTIONS, in this order and adjacent: the bar's buttons 2..5 cycle
; the first four (tp_bact), and every Layout and Page menu item is one byte
; naming an offset from tp_psize and a value (tp_mcode)
    TPB tp_psize,    1          ; 0 Letter 1 Legal 2 A4 3 A5
    TPB tp_bind,     1          ; 0 single 1 facing 2 opposing
    TPB tp_gutter,   1          ; 0 off 1 18pt 2 36pt
    TPB tp_pad,      1          ; 0 compact 1 normal 2 loose
    TPB tp_margin,   1          ; 0 narrow 1 normal 2 wide
    TPB tp_units,    1          ; 0 in 1 mm
    TPB tp_pstyle,   1          ; 0 none 1 foot 2 head 3 outer
    TPB tp_fsize,    1
    TPB tp_xkind,    1          ; the export being written: 0 PDF, 1 PS
    TPB tp_xdrop,    1          ; ...and a byte tp_xw_al had no room for
    TPB tp_xpage,    1          ; ...and the page its writer is on
    TPB tp_ex1,      2
    TPB tp_ey1,      2
    TPB tp_ex2,      2
    TPB tp_ey2,      2
    TPB tp_px1,      2
    TPB tp_py1,      2
    TPB tp_px2,      2
    TPB tp_py2,      2
    TPB tp_erows,    2
    TPB tp_ecols,    2
    TPB tp_tline,    2
    TPB tp_cxp,      2
    TPB tp_cyp,      2
    TPB tp_tmp,      2
    TPB tp_tmp2,     2
    TPB tp_tmp3,     2
    TPB tp_tmp4,     2
    TPB tp_tmp5,     2
; typesetter
    TPB tp_page,     2
    TPB tp_x,        2
    TPB tp_y,        2
    TPB tp_left,     2
    TPB tp_right,    2
    TPB tp_tleft,    2
    TPB tp_ttop,     2
    TPB tp_tbot,     2
    TPB tp_parind,   2          ; the four tp_padtab sets, in its order
    TPB tp_parskip,  2
    TPB tp_tlead,    2
    TPB tp_tabsep,   2
    TPB tp_indent,   2
    TPB tp_tw,       2
    TPB tp_nruns,    2
    TPB tp_nboxes,   2
    TPB tp_tused,    2
    TPB tp_si,       2
    TPB tp_se,       2
    TPB tp_pw,       2
    TPB tp_ph,       2
    TPB tp_nobj,     2
    TPB tp_scanat,   2
    TPB tp_budget,   2
    TPB tp_tabtop,   2
    TPB tp_tabbot,   2
    TPB tp_tabw,     2
    TPB tp_tabn,     2
    TPB tp_rowh,     2
    TPB tp_tsize,    1
    TPB tp_csize,    1
    TPB tp_inpara,   1
    TPB tp_needind,  1
    TPB tp_bold,     1
    TPB tp_under,    1
    TPB tp_align,    1
    TPB tp_chno,     1
    TPB tp_secno,    1
    TPB tp_subno,    1
    TPB tp_itemn,    1
    TPB tp_made,     1
    TPB tp_sty_n,    1
    TPB tp_env_n,    1
    TPB tp_clen,     1
    TPB tp_onpage,   1
    TPB tp_cmdn,     1          ; tp_cmd's length (tp_ts_cmd)
    TPB tp_fname,    14
    TPB tp_dlgname,  14
    TPB tp_ttlbuf,   32         ; the window's title: OSAPI_WM_TITLE keeps the
                                ; pointer and reads it on every repaint
    TPB tp_nbuf,     48
    TPB_UNION                   ; ...the typesetter's own scratch
    TPB tp_cmd,      32
    TPB tp_arg,      182
    TPB tp_wbuf,     82
    TPB tp_cline,    122        ; a centred line, a table's cell - AND the
                                ; panes' row text at PAINT time, which is never
                                ; inside tp_typeset (89 columns + NUL at most)
    TPB tp_title,    80
    TPB tp_author,   80
    TPB tp_date,     40
    TPB tp_sty,      32
    TPB tp_env,      24
    TPB tp_colw,     12
    TPB tp_colk,     6
    TPB_ARM                     ; ...the exporter's, after tp_typeset is done
    TPB tp_objoff,   80         ; 4 + 2 * TP_MAX_PAGES objects
    TPB_ARM                     ; ...and the top bar's string
    TPB tp_status,   80
    TPB_UNION_END
; the bar's seven buttons, IN BUTTON ORDER: rect N is tp_r_set + 8*(N-1)
    TPB tp_r_set,    8
    TPB tp_r_cls,    8
    TPB tp_r_mar,    8
    TPB tp_r_gut,    8
    TPB tp_r_pad,    8
    TPB tp_r_prev,   8
    TPB tp_r_next,   8
    TPB tp_sx1,      2
    TPB tp_sy1,      2
    TPB tp_sx2,      2
    TPB tp_sy2,      2
    TPB tp_tx1,      2
    TPB tp_ty1,      2
    TPB tp_r_ssb,    20         ; x1 y1 x2 y2 total fit pos, and the
    TPB tp_r_psb,    20         ; total fit pos the bar was DRAWN from
                                ; (os88ui_sbupd, SPEC.md 13.10.3.2)
    TPB tp_needttl,  1
    TPB tp_ldfull,   1          ; a deferred load landed: the next handler owes
                                ; a FULL repaint, not a source-pane one
                                ; (tp_deferred_ld)
    TPB tp_selon,    1
    TPB tp_dselon,   1
    TPB tp_seldrag,  1
    TPB tp_caret_on, 1
    TPB tp_edkind,   1
    TPB tp_rowcont,  1          ; the row tp_row_ext just measured is
                                ; CONTINUED by the next one
    TPB tp_nrowsok,  1
    TPB tp_sel0,     2
    TPB tp_sel1,     2
    TPB tp_anchor,   2
    TPB tp_dsel0,    2
    TPB tp_dsel1,    2
    TPB tp_statcnt,  2          ; where the byte count starts inside tp_nbuf
                                ; (tp_stat_touch)
    TPB tp_oldvs,    2
    TPB tp_oldps,    2
    TPB tp_barsig_v, 2          ; what the TOP bar's text was
    TPB tp_rgx1,     2          ; the preview REGION being drawn
    TPB tp_rgy1,     2
    TPB tp_rgx2,     2
    TPB tp_rgy2,     2
    TPB tp_rc_row,   2          ; the row cache: a row index, where it starts,
    TPB tp_rc_off,   2          ; and the width both were measured at
    TPB tp_rc_w,     2
    TPB tp_rc_want,  2
    TPB tp_edls,     2          ; the span an edit reaches (tp_edmark)
    TPB tp_edrf,     2
    TPB tp_edtail,   2
    TPB tp_edkb,     2
    TPB tp_edka,     2
    TPB tp_edlen,    2
    TPB tp_nrows,    2          ; the row COUNT cache: how many rows the
    TPB tp_nrowsw,   2          ; document had, and the width it was counted at
; the typesetter's OUTPUT (SPEC.md 69.3) - the run record is TP_RUN_SZ bytes,
; the box TP_BOX_SZ, and the text pool is the runs' text in run order: a run's
; text starts where the one before it ended, so no run carries an offset
    TPB tp_runs,     TP_MAX_RUNS * TP_RUN_SZ
    TPB tp_boxes,    TP_MAX_BOXES * TP_BOX_SZ
    TPB tp_text,     TP_TEXT_MAX

TP_BSS_TOTAL equ TPB_AT
; ...and the three places that walk fields as a block (tp_ts_init)
    OS88_ADJACENT tp_nruns, tp_nboxes
    OS88_ADJACENT tp_nboxes, tp_tused
    OS88_ADJACENT tp_parind, tp_parskip
    OS88_ADJACENT tp_parskip, tp_tlead
    OS88_ADJACENT tp_tlead, tp_tabsep
    OS88_ADJACENT tp_psize, tp_bind, 1
    OS88_ADJACENT tp_bind, tp_gutter, 1
    OS88_ADJACENT tp_gutter, tp_pad, 1
    OS88_ADJACENT tp_pad, tp_margin, 1
    OS88_ADJACENT tp_margin, tp_units, 1
    OS88_ADJACENT tp_units, tp_pstyle, 1
    OS88_ADJACENT tp_pstyle, tp_fsize, 1
    OS88_BSS TP_BSS_TOTAL
    OS88_IMAGE_END
