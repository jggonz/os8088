; =============================================================================
; kernel/hbmark.inc - A TRACE OF THE RESUME, ON THE GLASS (DOSRMARK=1)
;
; The resume is the one path on this machine that nothing can watch: by the
; time the stub is running there is no kernel, no task, no debugger hook and no
; serial port - just a blob in the text framebuffer reading the machine back
; over itself.  When it stops, every stage looks the same from outside, which
; is one still photograph and no way to tell the disk read from the decoder
; from the jump.  docs/FIELD-NOTES.md 45 is what that cost.
;
; So each stage stamps ONE CHARACTER onto row 7 of the page it is standing in,
; left to right, and the photograph becomes a trace.
;
; **IT IS NOT `int 10h` AND THAT IS THE WHOLE DESIGN.**  The ROM's teletype
; SCROLLS, and the page it scrolls IS the staging area - so one marker past the
; bottom line would move the stub, the extent table and the IVT copy up by a
; row underneath the code reading them.  `hbstub.inc`'s own `.fail` dodges this
; by homing to row 8 and never printing a fourth line; a trace cannot, so it
; stores directly and never moves anything.
;
; **ROW 7 IS THE ONLY FREE ROW IN THE MAP** (kernel/hbstage.inc): rows 0-6 are
; HS_CODE and the cells above it, and HS_IVT begins at 0x0500, which is row 8
; to the byte.  Row 7 is 0x0460..0x04FF and nothing else claims it.  The trace
; CLAMPS at HM_WIDTH rather than wrapping, because the cell after the row's
; last is HS_IVT's first and a diagnostic that corrupts the image it is
; diagnosing is worse than one that stops reporting.
;
; Everything here is CS-relative: the stub's CS is the staging segment and its
; DS is not always (the IVT staging borrows DS = 0x0060), so an override costs
; a byte and removes a question.
; =============================================================================
%ifndef HBMARK_INC
%define HBMARK_INC 1

HM_ROW      equ 0x0460          ; row 7 of an 80-column page, in bytes
HM_WIDTH    equ 64              ; ...of which the trace may use this many
HM_KCOL     equ 66              ; and HS_K lives here, live, in hex
HM_WCOL     equ 78              ; hbm_wake's own mark, past everything above:
                                ; the trace APPENDS from column 1, so a fixed
                                ; column inside its run would be overwritten by
                                ; the stub's first mark or overwrite it
HM_NXCOL    equ 72              ; ...with the extent COUNT beside it, once.
                                ; It is on the row and not in the teletype
                                ; line because the walk that produces it now
                                ; runs AFTER the last print (SPEC.md 96.49.4)
HM_CUR      equ 0x044A          ; dw the trace's next byte offset within the
                                ; row. Above HS_TICK0, which is a **dd** at
                                ; 0x0446 and so owns 0x0446..0x0449 - this was
                                ; 0x0448 and aliased its high word, so the stub
                                ; wrote 2 into the staged tick on every resume
                                ; and hb_wake woke the clock ~22 hours out.
                                ; Below HS_IVT, in the gap the map leaves
%if HM_CUR < HS_TICK0 + 4
  %error "hbmark: HM_CUR overlaps HS_TICK0 - the instrument would corrupt the tick it watches"
%endif

; --- HBMARK - one character, appended -----------------------------------
%macro HBMARK 1
    push bx
    mov bx, [cs:HM_CUR]
    cmp bx, HM_WIDTH * 2
    jae %%full
    mov byte [cs:HM_ROW + bx], %1
    mov byte [cs:HM_ROW + bx + 1], 0x07
    add bx, 2
    mov [cs:HM_CUR], bx
%%full:
    pop bx
%endmacro

; --- HBMARKK - HS_K, rewritten in place ---------------------------------
; The trace clamps and a 640KB image is ~75 reads, so the dots alone would run
; out before the image did. This is the number that does not: a machine frozen
; mid-restore leaves the exact file sector it was on standing on the glass.
%macro HBMARKK 0
    push ax
    push bx
    push cx
    push dx
    mov dx, [cs:HS_K]
    mov bx, HM_ROW + HM_KCOL * 2
    mov cx, 4
%%n:
    rol dx, 1
    rol dx, 1
    rol dx, 1
    rol dx, 1
    mov al, dl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe %%dig
    add al, 7
%%dig:
    mov [cs:bx], al
    mov byte [cs:bx + 1], 0x07
    inc bx
    inc bx
    loop %%n
    pop dx
    pop cx
    pop bx
    pop ax
%endmacro

; --- HBMARKAT - one character at a column, into a segment in ES ---------
; For the two markers that are NOT in the stub: `kd_resume`'s last breath
; before the far jump, and `hbm_wake`'s first after it.  Neither can use
; HBMARK - the first is not running in the staging segment and the second is
; not sure which segment that is, which is the very thing being asked.
%macro HBMARKAT 2               ; %1 = column, %2 = character
    mov byte [es:HM_ROW + (%1) * 2], %2
    mov byte [es:HM_ROW + (%1) * 2 + 1], 0x07
%endmacro

%endif                          ; HBMARK_INC
