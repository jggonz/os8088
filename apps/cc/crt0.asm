; =============================================================================
; os8088 - apps/cc/crt0.asm
;
; The prologue of a C package (SPEC.md 67.2, 67.4): the section layout, the
; 32-byte header with its dispatcher, the entry trampoline the loader calls,
; and one trampoline per window callback. It %includes apps/cc/os88thunk.asm,
; so including this file is the whole of "link against the C runtime".
;
; -----------------------------------------------------------------------------
; HOW A C PACKAGE IS PUT TOGETHER
; -----------------------------------------------------------------------------
; A C package is a .c file plus a ten-line .asm shim. The shim is the
; top-level nasm source; it declares what the package has, includes this file,
; includes the compiled C, and closes the image:
;
;     ; apps/cword/cword.asm
;     %define CC_PKG_NAME 'CWORD'         ; <= 15 chars, the Disk window's label
;     %define CC_HAS_ONKEY                ; ...and one line per callback the C
;     %define CC_HAS_ONCLICK              ;    actually defines
;     %define CC_HAS_MENUS
;     %include "cc/crt0.asm"              ; sections, header, trampolines, API
;     %include "cword.gen.asm"            ; the compiled C, via -I build/
;         CC_IMAGE_END                    ; the last thing in the file
;
; Then, from apps/cc/Makefile.inc:
;
;     cword.c --smlrcc--> cword.raw.asm --cc8086.py--> cword.gen.asm
;     cword.asm --nasm -f bin--> cword.bin --os88pkg.py--> cword.o88
;
; The shim exists because the two halves have to agree about which callbacks
; are real, and this is the cheapest place to say it: a %define with no C
; function behind it is an nasm external-reference error naming the function,
; and a C function with no %define is code the kernel simply never calls.
; Everything else about the package - its window, its layout, its data - is in
; the C.
;
; -----------------------------------------------------------------------------
; THE SECTIONS, AND THE SILENT TRAP THEY EXIST TO CLOSE (SPEC.md 67.2)
; -----------------------------------------------------------------------------
; An assembly package is one flat run of bytes from `org 0`, and
; apps/os88api.inc was written on that assumption: OS88_IMAGE_END computes
; `os88_image_end - $$`, and in nasm `$$` is the start of the CURRENT SECTION.
; SmallerC emits four sections and switches between them several times per
; function, so that macro lands wherever the compiler last left off and the
; header's image-size word becomes the size of one section. Measured on the
; first C package built: the word said 397 for a 478-byte file. Nothing about
; it is ill-formed, nasm says nothing, and the only thing in the tree that
; notices is os88pkg.py's "image size must equal file size" - which reads
; exactly like a truncated file.
;
; So a C package does not use OS88_HEADER / OS88_IMAGE_END. It uses the pair
; below, which have the same field layout, the same magic, the same version 3
; and the same dispatcher bytes, and differ in exactly one thing: the image
; size is an ABSOLUTE LABEL rather than a difference. With `.text start=0` and
; every other section chained to it by `follows=`, a label's value IS its
; offset from the head of the image, so `dw cc_image_end` is the file size and
; there is no `$$` anywhere to be wrong about.
;
; Two consequences worth stating, because both were found the hard way:
;
;   * THE BSS SIZE IS A DIFFERENCE TAKEN INSIDE ONE SECTION.
;     `cc_bss_end - cc_bss_beg`, both in .bss. It cannot be measured from
;     cc_image_end: nasm refuses a cross-section subtraction (`invalid operand
;     type`) because section placement is not resolved when the expression is
;     evaluated. Two labels in one section always resolve.
;
;   * .data IS NEVER EMPTY, because cc_tpl lives in it. An empty .data is not
;     harmless: nasm materializes a section's alignment padding only if the
;     section has content, so with nothing in .data the file ends at .rodata
;     while cc_image_end sits one byte past it - measured, a 37-byte file with
;     a 38 in its header, and os88pkg.py reporting a size mismatch that looks
;     like a bad disk.
;
;   * .data IS PADDED TO EVEN BEFORE cc_image_end IS TAKEN, and that one
;     `align 2` closes a silent memory bug rather than tidying a number.
;     .bss is `follows=.data align=2`, so nasm rounds its base UP: with an
;     odd-length .data - one `static char name[9] = "SAVE.DAT";` is enough -
;     .bss begins at cc_image_end + 1. The loader zeroes exactly
;     [image, image + bss_size) (kernel/loader.inc), so it clears one byte
;     BEFORE .bss and stops one byte short of its end, and the last byte of
;     .bss keeps whatever the previous tenant of that heap claim left there
;     (mem_claim_hi_x does not pre-zero - SPEC.md 15). Measured before the
;     fix on a package whose only change was the parity of one string:
;     cc_bss_beg=2290 against cc_image_end=2289, and the even sibling in
;     agreement at 2288. Nothing warns, and the symptom is a C global that is
;     occasionally not 0 on the second launch.
;
; There is no assembly-time APP_MAX_SIZE assertion here, and that is not an
; oversight: `%if` cannot compare a section-relative label to a constant
; (`operands differ by a non-scalar`). The 60KB ceiling is enforced twice
; downstream instead - tools/os88pkg.py on every build, and ld_check_hdr on
; every load (SPEC.md 21) - and the build rule prints the sizes.
;
; -----------------------------------------------------------------------------
; WHY EVERY CALLBACK GOES THROUGH A TRAMPOLINE (SPEC.md 67.4)
; -----------------------------------------------------------------------------
; The kernel reaches a package by far-calling <package>:12 with BP = the
; callback's near offset; the three bytes there are `call bp` / `retf`, so a
; callback is an ordinary near proc ending in a near `ret` and a package
; author never writes `retf`. A compiled C function is structurally exactly
; that, and it must still never be the dispatch target, for three reasons that
; are each fatal on their own:
;
;   1. THE ARGUMENTS ARRIVE IN REGISTERS. W_PAINT, W_ONKEY and W_ONCLICK all
;      come in with SI = the window record; a click adds CX/DX, a key adds the
;      code in AX. Compiled C reads none of that: it wants a cdecl frame.
;
;   2. THE KERNEL EXPECTS REGISTERS BACK AND C PRESERVES NOTHING. SI must
;      survive W_PAINT and W_ONCLICK - wm_draw_win does `mov bx, si` after the
;      dispatch returns (kernel/wm.inc:7875) - and SmallerC has no
;      callee-saved register at all. So each trampoline saves everything it
;      can rather than the minimum set per callback: the minimum set is a fact
;      about kernel code that this package cannot see and that may change
;      under it, and being wrong about it produces something that assembles
;      cleanly and runs wrong.
;
;   3. THE DIRECTION FLAG. Every callback must leave DF clear (SPEC.md 1,
;      20.2). SmallerC never emits `std`, so it would in fact survive - but a
;      guarantee resting on a compiler's habits is not a guarantee. One `cld`,
;      one byte, and it is local.
;
; AX is deliberately NOT saved: it is C's return register and the return
; register of every callback contract that answers with a value, so the two
; agree for free.
;
; -----------------------------------------------------------------------------
; THE ENVIRONMENT EVERY TRAMPOLINE RUNS IN
; -----------------------------------------------------------------------------
;   CS = DS = this package's segment (SPEC.md 20.1), so every static, string
;        literal and .rodata table is a plain DS-relative reference and the
;        compiled C needs no help.
;   SS = LOW_SEG, and SS != DS is a property of the whole operating system,
;        not a detail. `[bp+N]` reads the stack, which is right; the ADDRESS
;        of a local is wrong, which is why tools/cc8086.py refuses it (67.5).
;   ES = KERNEL_SEG on entry, because the window record and the file dialog's
;        name live there. The trampolines save and restore it; the routines
;        that actually read the kernel's segment load it themselves rather
;        than trust it (see CC_T_WFIELD in os88thunk.asm).
;   IF = 1, and for a window callback the gfx lock is HELD.
; =============================================================================

%include "os88api.inc"              ; the slot numbers, the record layout, the
                                    ; colours - all of it %define and equ, so
                                    ; including it emits nothing. Only the
                                    ; MACROS are unused here: OS88_HEADER
                                    ; would emit `org 0` and OS88_IMAGE_END
                                    ; the `$$` arithmetic above.

; %fatal rather than %error, and the difference is the whole point: without a
; name there is no header, and nasm goes on to report a `%strlen` complaint, an
; undefined `cc__namelen` and a non-constant TIMES - three messages, none of
; which says "you forgot the name". %fatal stops here with the one that does.
%ifndef CC_PKG_NAME
  %fatal "cc/crt0.asm: define CC_PKG_NAME (e.g. %define CC_PKG_NAME 'CWORD') before including this file"
%endif

; --- the layout, pinned by SPEC.md 67.2 ---------------------------------------
cpu 8086
bits 16
section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits

; =============================================================================
; THE 32-BYTE HEADER (SPEC.md 20.2)
;
; Byte for byte what OS88_HEADER emits, and it has to be: os88pkg.py validates
; it on every build and ld_check_hdr validates it again on every load (SPEC.md
; 21), neither of which knows a C package from an assembly one - which is the
; test of whether this was done right.
; =============================================================================
section .text
    db 'O', '8'                     ; +0  magic (word 0x384F)
    db 3                            ; +2  format version 3: segment-per-package
%ifdef CC_ICON
    db 1                            ; +3  flags bit 0: an embedded icon follows
%else
    db 0                            ; +3  flags
%endif
    dw 0                            ; +4  link base: 0, and the loader checks it
    dw cc_entry                     ; +6  entry offset - an ABSOLUTE label,
                                    ;     because .text starts at 0
    dw CC_IMAGE_SIZE                ; +8  image size = the whole file. An
                                    ;     ABSOLUTE LABEL, not `$-$$` (67.2)
    dw CC_BSS_SIZE                  ; +10 bytes the loader zeroes after it -
                                    ;     a difference taken INSIDE .bss
    db 0FFh, 0D5h                   ; +12 THE DISPATCHER: `call bp`...
    db 0CBh                         ; +14 ...`retf`. The loader CHECKS these
    db 0                            ; +15 three bytes (kernel/loader.inc:163
                                    ;     and :165), so they are not
                                    ;     decoration - a package whose header
                                    ;     says the right size and carries the
                                    ;     wrong dispatcher is rejected at load
    %strlen cc__namelen CC_PKG_NAME
    %if cc__namelen > 15
        %fatal "CC_PKG_NAME must be at most 15 characters (the field is 16, NUL-padded)"
    %endif
    db CC_PKG_NAME                  ; +16 program name...
    times 16 - cc__namelen db 0     ;     ...NUL-padded to 16 bytes

%ifdef CC_ICON
; The optional embedded 16x16 icon (SPEC.md 20.2/20.5), which must sit at
; file offset 32 and end at 96: 16 mask words (the white underlay) then 16
; data words (black pixels), bit 15 = leftmost, row-major. Without it the Disk
; window shows the built-in ico_app16, which is what HELLO does and is a
; perfectly good answer. The shim names the file:
;     %define CC_ICON "cword/cwicon.inc"
%if ($ - $$) != 32
    %error "the icon must immediately follow the header (file offset 32)"
%endif
%include CC_ICON
%if ($ - $$) != 96
    %error "CC_ICON must be exactly 32 dw rows (16 mask + 16 data)"
%endif
%endif

; =============================================================================
; THE ENTRY TRAMPOLINE (SPEC.md 20.2, 21 step 8)
;
; The loader reaches this the same way every other callback is reached -
; through the dispatcher at +12 - with DS = CS = this segment, ES = KERNEL_SEG,
; IF = 1 and the gfx lock NOT held. It owes the loader BX = the window it
; created and CF clear, or CF set to abort the launch.
;
; in:  nothing
; out: BX = the window, CF clear; CF set = abort (and the loader unwinds the
;      region, the instance record and anything os88_main() claimed)
; preserves: SI, DI, ES - not because the loader documents needing them, but
;      because it goes on to read a directory entry and copy an icon after
;      this returns, and a package that hands a kernel path a clobbered ES has
;      broken the one register CLAUDE.md says the kernel cannot shrug off.
; =============================================================================
cc_entry:
    push si
    push di
    push es
    cld                             ; DF is ours to guarantee, not to inherit
    call _os88_main                 ; C: void *os88_main(void). It creates the
                                    ; window with os88_wm_create() - which is
                                    ; lock-free - and returns it; 0 = abort.
                                    ; It must NOT show, draw or spawn: the
                                    ; loader shows the window, and the
                                    ; instance is not published yet, so
                                    ; OSAPI_TASK_SPAWN would simply refuse
    mov bx, ax                      ; BX = the window, which is the whole of
    mov [cc_win], ax                ; the answer. Banked too, for cc_worker
    pop es
    pop di
    pop si
    or bx, bx                       ; CF LAST, and after the pops: `pop`
    jz .abort                       ; writes no flags, so either order works -
    clc                             ; pinning one removes the question (67.4)
    ret
.abort:
    stc
    ret

; =============================================================================
; THE WINDOW CALLBACKS
;
; One trampoline each, all the same shape: save, clear DF, push the C
; arguments RIGHT TO LEFT, call, clean up (the CALLER cleans - SPEC.md 67.3),
; restore, near `ret`. Each is assembled only if the shim asked for it.
; =============================================================================

; -----------------------------------------------------------------------------
; cc_paint - W_PAINT (SPEC.md 11). Always present: a window that draws nothing
; is not a window.
; in:  SI = the window record; the gfx lock is ALREADY HELD
; out: nothing; every register the kernel cares about is preserved
; -----------------------------------------------------------------------------
cc_paint:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si                         ; arg 1: void *win
    call _os88_paint
    add sp, 2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret                             ; NEAR - the dispatcher owns the only retf

%ifdef CC_HAS_ONKEY
; -----------------------------------------------------------------------------
; cc_onkey - W_ONKEY (SPEC.md 11)
; in:  AL = the ASCII code, AH = the scan code, SI = the window; lock held
; out: nothing
; The two halves of AX are moved into BX first, because building the second
; C argument destroys the register the first one is still in.
; -----------------------------------------------------------------------------
cc_onkey:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    mov bx, ax                      ; BL = ascii, BH = scan
    push si                         ; arg 3: void *win
    mov al, bh
    mov ah, 0
    push ax                         ; arg 2: int scan
    mov al, bl
    mov ah, 0
    push ax                         ; arg 1: int ascii
    call _os88_onkey
    add sp, 6
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_ONCLICK
; -----------------------------------------------------------------------------
; cc_onclick - W_ONCLICK (SPEC.md 11)
; in:  CX = x, DX = y (absolute screen), SI = the window; lock held
; out: nothing. You may draw; you must not take the lock or block.
; -----------------------------------------------------------------------------
cc_onclick:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si                         ; arg 3: void *win
    push dx                         ; arg 2: int y
    push cx                         ; arg 1: int x
    call _os88_onclick
    add sp, 6
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_ONMOUSEUP
; -----------------------------------------------------------------------------
; cc_onmouseup - the RELEASE half of a content click (SPEC.md 13.7), installed
; by os88_wm_onmouseup(). Identical environment to W_ONCLICK.
; in:  CX = x, DX = y, SI = the window; lock held
; out: nothing. The release may land OUTSIDE your window and the coordinates
;      may go negative once you subtract your origin - that is the feature
;      (un-draw a pressed state and decline to act), so range-test it.
; -----------------------------------------------------------------------------
cc_onmouseup:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si
    push dx
    push cx
    call _os88_onmouseup
    add sp, 6
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_ONRESIZE
; -----------------------------------------------------------------------------
; cc_onresize - "your content box CHANGED and you did not ask" (SPEC.md 11.98),
; installed by os88_wm_onresize(). The adapter changed under you, or your
; window crossed onto a shorter display.
; in:  CX = the NEW content width, DX = the new height, SI = the window; lock
;      held, and nothing is drawn at either size
; out: nothing - and DO NOT DRAW. A full repaint follows immediately; this is
;      your chance to be laid out correctly before it, not a frame later.
; -----------------------------------------------------------------------------
cc_onresize:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si                         ; arg 3: void *win
    push dx                         ; arg 2: int h
    push cx                         ; arg 1: int w
    call _os88_onresize
    add sp, 6
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_MENUS
; -----------------------------------------------------------------------------
; cc_oncmd - AM_ONCMD, a pick from one of your menus (SPEC.md 12.2)
; in:  AL = the item index, AH = the menu index, SI = the owning window,
;      BX = the menu set; on the UI task, under the gfx lock
; out: nothing. Same rules as a click - you may draw and may call the file
;      slots, you must never take the lock, and THE KERNEL DOES NOT REPAINT
;      AFTER A COMMAND, so the redraw is yours.
; -----------------------------------------------------------------------------
cc_oncmd:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    mov bx, ax                      ; BL = item, BH = menu
    push si                         ; arg 3: void *win
    mov al, bh
    mov ah, 0
    push ax                         ; arg 2: int menu
    mov al, bl
    mov ah, 0
    push ax                         ; arg 1: int item
    call _os88_oncmd
    add sp, 6
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_ABOUT
; -----------------------------------------------------------------------------
; cc_about - the 'About <Name>' item the kernel adds above 'Close' in your own
; pull-down (SPEC.md 12.2), installed by os88_about_set().
; in:  SI = the window; UI task, gfx lock held, no selection to decode
; out: nothing; W_ONCLICK's rules exactly.
; -----------------------------------------------------------------------------
cc_about:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si
    call _os88_about
    add sp, 2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_FDLG
; -----------------------------------------------------------------------------
; cc_onfile - the Standard File dialog's completion proc (SPEC.md 38.6),
; installed by os88_file_dlg(). Called long after the call that opened the
; dialog, on the UI task with the gfx lock held and the dialog window gone.
; Cancel calls nothing at all.
;
; in:  AL = the mode it ran in, SI = your window, ES:DI = the chosen name,
;      DX:CX = that file's size in bytes (0 = this listing has no such file:
;      a typed Save name, a folder)
; out: nothing
;
; THE NAME IS IN THE KERNEL'S SEGMENT and is valid for the duration of the
; call only, so the first thing this does is copy it into cc_fname - which is
; the whole reason a C program can be told about it at all. ES is loaded here
; rather than taken on trust: the callback contract does put KERNEL_SEG in it,
; but two instructions make that a fact instead of an assumption, and this
; routine is the one place in the package that reads another segment before
; anything else has run.
;
; The size arrives as 32 bits and C has no such type (SPEC.md 67.7), so it is
; handed over as two words. USE IT TO REFUSE BEFORE TOUCHING THE DISK: it is
; free, and reading 116KB off a floppy to then say "wrong format" is about ten
; seconds of motor the user cannot tell from a load that works.
; -----------------------------------------------------------------------------
cc_onfile:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push ax                         ; the mode, across the copy
    push cx                         ; ...and the size, both halves
    push dx
    mov ax, KERNEL_SEG
    mov es, ax                      ; ES = where the name lives, stated
    mov bx, 0
.cp:
    mov al, [es:bx+di]              ; [bx+di] - a legal 8086 base+index, and
    mov [cc_fname+bx], al           ; the store is DS: ours
    or al, al
    jz .copied
    inc bx
    cmp bx, 12
    jb .cp
    mov byte [cc_fname+12], 0       ; 12 characters and no NUL: terminate it
.copied:
    pop dx
    pop cx
    pop ax                          ; AL = the mode again
    push si                         ; arg 5: void *win
    push dx                         ; arg 4: unsigned size_hi
    push cx                         ; arg 3: unsigned size_lo
    mov bx, cc_fname
    push bx                         ; arg 2: const char *name
    mov ah, 0
    push ax                         ; arg 1: int mode
    call _os88_onfile
    add sp, 10
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_WORKER
; -----------------------------------------------------------------------------
; cc_worker - your one background task (SPEC.md 20.6), started by
; os88_task_spawn() from a callback. NOT a callback and not dispatched: the
; scheduler irets straight to this offset.
;
; in:  DS = CS = this segment, ES = KERNEL_SEG, DX = the instance index,
;      IF = 1, the gfx lock free, SP at the top of a 256-byte slice in LOW_SEG
; out: it does not have one - see below
;
; It saves nothing, because there is nobody to give anything back to.
;
; THE PARK LOOP IS NOT DECORATION. SPEC.md 20.6 rule 2: a worker that falls
; off the end and executes `ret` jumps to a near-random offset, and one that
; exits cleanly leaves the kernel holding a dead task slot - the close box
; then hides the window but never frees the instance, and the region leaks for
; the rest of the session. A C author is one missing `while` away from both.
; So if os88_worker() ever returns, this parks: sleep a tick, ask
; OSAPI_TASK_ALIVE whether the instance is still wanted, repeat. TASK_ALIVE
; never returns once the close box is clicked - the kernel destroys the
; window, frees the instance and ends the task inside that call - so the loop
; is how the task dies properly rather than a spin.
;
; The gfx lock must NOT be held across TASK_ALIVE (rule 4: it is not
; reentrant, and calling it under the lock deadlocks the machine and nothing
; ever draws again). Nothing here takes it.
; -----------------------------------------------------------------------------
cc_worker:
    cld
    push word [cc_win]              ; arg 1: void *win, banked by cc_entry
    call _os88_worker               ; C: it must never return
    add sp, 2                       ; ...but if it does:
.park:
    mov ax, 1
    call OSAPI_TASK_SLEEP           ; one tick, so the park costs nothing
    mov bx, [cc_win]
    call OSAPI_TASK_ALIVE           ; never returns once we are being closed
    jmp short .park
%endif

; =============================================================================
; THE API BRIDGE
; =============================================================================
%include "cc/os88thunk.asm"

; =============================================================================
; DATA
; =============================================================================
section .data

; The wm_create template (SPEC.md 11: 16 bytes, 8 words). The first five words
; are the C's, written by os88_wm_create(); the last three are this file's,
; and they are why a C program never has to name an assembly label. A
; callback the shim did not ask for is a 0 here, which is exactly how the
; window record says "I have no key handler".
;
; It also keeps .data non-empty, which is what keeps the image-size word equal
; to the file size - see the note at the top of this file.
cc_tpl:
    dw 0, 0, 0, 0                   ; WT_X, WT_Y, WT_W, WT_H
    dw 0                            ; WT_TITLE
    dw cc_paint                     ; WT_PAINT
%ifdef CC_HAS_ONKEY
    dw cc_onkey                     ; WT_ONKEY
%else
    dw 0
%endif
%ifdef CC_HAS_ONCLICK
    dw cc_onclick                   ; WT_ONCLICK
%else
    dw 0
%endif

; =============================================================================
; BSS - zeroed by the loader (SPEC.md 21 step 8), so every one of these starts
; at 0 and nothing here needs an initialiser. cc_bss_beg must be the FIRST
; label in .bss and this file is included before the compiled C, so it is.
; =============================================================================
section .bss
cc_bss_beg:
cc_win:     resw 1                  ; the window os88_main() returned. Banked
                                    ; because cc_worker has no argument of its
                                    ; own and OSAPI_TASK_ALIVE needs one
cc_ferr:    resw 1                  ; the FERR_* of the last file call, read
                                    ; back by os88_ferr()
cc_fname:   resb 13                 ; the file dialog's chosen name, copied out
                                    ; of KERNEL_SEG before the C sees it
cc_assoc:   resb 11                 ; os88_assoc_set()'s space-padded block:
                                    ; 3 extension bytes then 8 stem bytes

section .text                       ; leave the assembler in .text - the next
                                    ; %include is the compiled C, and it opens
                                    ; its own sections but must not INHERIT
                                    ; one (CLAUDE.md, section discipline)

; -----------------------------------------------------------------------------
; CC_IMAGE_END - close the image. The last line of the package's shim, after
; the compiled C, and the sibling of OS88_IMAGE_END.
;
; It defines the labels the header referred to forwards: cc_bss_end at the end
; of all bss (the runtime's above, then the C's), and cc_image_end at the end
; of .data, which with `.text start=0` IS the file's size. The two `equ`s are
; the names SPEC.md 67.2 pins, and they resolve because each is a difference
; or a label inside ONE section - `cc_bss_end - cc_image_end` would be a
; cross-section subtraction, which nasm refuses in an equ as `invalid operand
; type`. Leaving the assembler in .text afterwards costs nothing and keeps the
; rule the rest of the tree follows.
;
; The `align 2` is the whole of the fix described at the head of this file: it
; is taken BEFORE cc_image_end so that the label the header reports as the
; image size is the same address `.bss follows=.data align=2` rounds up to.
; Without it an odd-length .data leaves the last byte of .bss un-zeroed, and
; `align` must be here rather than in the shim because this is the only place
; that is guaranteed to run after the compiled C's last `.data` byte.
; -----------------------------------------------------------------------------
%macro CC_IMAGE_END 0
section .bss
cc_bss_end:
section .data
align 2, db 0                       ; see above - NOT cosmetic
cc_image_end:
CC_IMAGE_SIZE equ cc_image_end
CC_BSS_SIZE   equ cc_bss_end - cc_bss_beg
section .text
%endmacro
