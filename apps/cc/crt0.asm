; =============================================================================
; os8088 - apps/cc/crt0.asm
;
; The prologue of a C package (SPEC.md 73.2, 67.4): the section layout, the
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
; THE SECTIONS, AND THE SILENT TRAP THEY EXIST TO CLOSE (SPEC.md 73.2)
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
; WHY EVERY CALLBACK GOES THROUGH A TRAMPOLINE (SPEC.md 73.4)
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
;        of a local is wrong, which is why tools/cc8086.py refuses it (73.5).
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
; THE LARGEST SLOT CLASS, and it must be the largest rather than the class this
; package declares. cc_iswk decides "am I the worker" by testing SP against a
; window one slice deep below the banked top, and a window that is too SHORT
; FAILS THE GATE OPEN: a worker deeper than this reads as the UI task and
; cc_ovneed goes on to claim and read a floppy from it (SPEC.md 20.6 rule 7).
;
; Since SPEC.md 8.7 a package ASKS for a class and is given the smallest free
; slice at least that big - which can be BIGGER than it asked for, because the
; small slices may all be taken. So a package that declares 128 can legitimately
; be running on 384, and a CC_STACK cut to its declared class would be short by
; 256 bytes exactly when the machine is busy. The maximum is the only safe
; answer, and being too LONG costs nothing: the only other stack in reach is
; task 0's, which lives above .lowbss and nowhere near a slice.
;
; It was the literal 384 with nothing comparing it to the kernel's SCH_STACK -
; docs/STACK-SLOTS-PLAN.md 12.4 found it while surveying the packages. Taking
; the SDK's copy puts it under tests/unit/t_mirror.py, which compares every
; name defined in more than one file and needed nobody to remember it.
;
; CC_OVDEPTH is the depth of cc_ovthunk's return stash in WORDS (SPEC.md 73.14).
CC_STACK    equ SCH_STACK
CC_OVDEPTH  equ 16

%ifndef CC_PKG_NAME
  %fatal "cc/crt0.asm: define CC_PKG_NAME (e.g. %define CC_PKG_NAME 'CWORD') before including this file"
%endif

; --- the layout, pinned by SPEC.md 73.2 ---------------------------------------
cpu 8086
bits 16
section .text   start=0
section .rodata follows=.text   align=2
section .data   follows=.rodata align=2
section .bss    follows=.data   align=2 nobits
%ifdef CC_HAS_OVL
; The overlay's code (SPEC.md 73.14), which ships as <NAME>.OVL beside the
; package instead of inside it. It FOLLOWS .data - the same section .bss
; follows - and the two do not collide because .bss is `nobits` and writes no
; file byte: .bss's base and .modc's first byte are both cc_image_end, which is
; exactly right. .bss is addressed there at run time and .modc is CUT there at
; build time, by tools/os88ovl.py, reading the image-size word the header
; already carries.
;
; vstart=0 because it is read into a claim and runs at offset 0 of it, so its
; labels must be numbered from there; align=1 because a bin section aligns to
; 4 by default and the pad would land between the recorded image size and the
; section's real start, so the cut would take the pad with it (SPEC.md 68.10 -
; it was one byte, and the module ran off the end of its own jump table).
section .modc   follows=.data   align=1 vstart=0
%endif

; --- the header flags byte, assembled from what the shim declared ------------
; Two independent bits (SPEC.md 54.6, and tools/os88pkg.py checks both):
;   bit 0  an embedded 16x16 icon follows the header, at offset 32..95
;   bit 1  a 16-byte association block follows the icon (offset 96) or, with
;          no icon, the header (offset 32)
; Computed here rather than written as a literal in the header, because with
; two optional blocks a literal is four cases and one of them is wrong: 54.6
; records that gating the DECLARATION on the icon bit is exactly the mistake
; that shipped once, and an iconless package declaring an extension is the
; case the block was wanted for first.
%assign CC_FLAGS 0
%ifdef CC_ICON
  %assign CC_FLAGS CC_FLAGS | 1
%endif
%ifdef CC_ASSOC
  %assign CC_FLAGS CC_FLAGS | 2
%endif
%ifdef CC_HAS_PARTS
  %assign CC_FLAGS CC_FLAGS | 4     ; bit 2: the FILE is longer than the image
%endif                              ; on purpose (SPEC.md 20.12.1)

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
    db CC_FLAGS                     ; +3  flags: bit 0 an embedded icon
                                    ;     follows, bit 1 a 16-byte association
                                    ;     block follows it (SPEC.md 54.6).
                                    ;     Computed above as the OR of the two,
                                    ;     because they are independent - an
                                    ;     ICONLESS package declaring an
                                    ;     extension is the case 54.6 says the
                                    ;     block ships on first
    dw 0                            ; +4  link base: 0, and the loader checks it
    dw cc_entry                     ; +6  entry offset - an ABSOLUTE label,
                                    ;     because .text starts at 0
    dw CC_IMAGE_SIZE                ; +8  image size = the whole file. An
                                    ;     ABSOLUTE LABEL, not `$-$$` (73.2)
    dw CC_BSS_SIZE                  ; +10 bytes the loader zeroes after it -
                                    ;     a difference taken INSIDE .bss
    db 0FFh, 0D5h                   ; +12 THE DISPATCHER: `call bp`...
    db 0CBh                         ; +14 ...`retf`. The loader CHECKS these
                                    ;     three bytes (kernel/loader.inc), so
                                    ;     they are not decoration - a package
                                    ;     whose header says the right size and
                                    ;     carries the wrong dispatcher is
                                    ;     rejected at load
%ifdef CC_STACK_CLASS
    db CC_STACK_CLASS               ; +15 the worker's stack class (SPEC.md
%else                               ;     8.7), an OS88_STACK_* from the SDK.
    db 0                            ;     Say nothing and the byte is 0, which
%endif                              ;     the kernel reads as "no opinion" and
                                    ;     answers with the largest class - the
                                    ;     stack this package had before the
                                    ;     field existed. NOT the same number as
                                    ;     CC_STACK above, and deliberately: one
                                    ;     is what we ASK for and the other is
                                    ;     the widest slice we could be GIVEN
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

%ifdef CC_ASSOC
; The optional association block (SPEC.md 54.6): "a document wearing one of
; these extensions is MINE". A fixed 16 bytes - a count byte then up to five
; 3-byte uppercase space-padded extensions - sitting immediately after the
; icon (offset 96) or, with no icon, after the header (offset 32). Declaring
; costs nothing at run time: the mount's icon harvest already reads this
; sector, so a document beside the program opens on the FIRST double-click,
; with no prior run and no search.
;
; The shim names the file, exactly as it names the icon:
;     %define CC_ASSOC "weave/wvassoc.inc"
; and that file writes the count byte and one OS88_ASSOC_EXT per extension -
; the same two lines an assembly package writes (apps/frotz/frotz.asm is the
; worked example). The BRACKET is here rather than in the shim's file so that
; the offset assertions and the pad-to-16 cannot be forgotten, and it is
; os88api.inc's own macro pair rather than a copy of it, so there is one
; authority for what the block looks like.
;
; The offsets both macros assert are `$ - $$` inside .text, which is the one
; section this file gives `start=0` - so `$$` is 0 and the arithmetic is the
; file offset, the same reason the icon's two assertions above work.
;
; This is ALSO what os88_assoc_set() (SPEC.md 54.5, os88thunk.asm) does at run
; time, and the two are not alternatives: a declaration is free and is read
; before the program has ever run, while a runtime claim is what a program
; makes about a STEM it just saved. A package may want both.
    OS88_ASSOC16                    ; asserts offset 96 (icon) or 32 (none)
%include CC_ASSOC                   ; the count byte, then OS88_ASSOC_EXT xN
    OS88_ASSOC16_END                ; asserts <= 5 and pads to exactly 16
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
%ifdef CC_HAS_PARTS
    call op_load                    ; ...FIRST, and before any C runs: SI is
    jc .partsno                     ; still the kernel's name pointer and the
                                    ; buffer it points into is reused on the
                                    ; next launch (SPEC.md 20.2, rule 1). A
                                    ; refusal has already toasted its reason,
                                    ; and the loader unwinds the region
%endif
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
    clc                             ; pinning one removes the question (73.4)
    ret
.abort:
    stc
    ret
%ifdef CC_HAS_PARTS
.partsno:
    pop es                          ; op_load refused; the C never ran, so
    pop di                          ; there is nothing of ours to unwind and
    pop si                          ; the loader gives the region back
    stc
    ret
%endif

; =============================================================================
; THE WINDOW CALLBACKS
;
; One trampoline each, all the same shape: save, clear DF, push the C
; arguments RIGHT TO LEFT, call, clean up (the CALLER cleans - SPEC.md 73.3),
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

%ifdef CC_HAS_ONTIMER
; -----------------------------------------------------------------------------
; cc_ontimer - W_ONTIMER (SPEC.md 13.9), installed by os88_wm_ontimer() and
; fired by os88_wm_timer(). ONE-SHOT, and it disarms itself BEFORE this runs -
; so re-arm inside the handler for a repeat (a blinking caret) and do nothing
; for a single shot.
;
; It is W_ONCLICK's environment exactly: the UI task, the gfx lock HELD, billed
; to the instance - which is why it may draw. It fires while the window is
; MINIMIZED too; ask os88_wm_geom() if that matters.
;
; NOTHING ARRIVES WITH IT. CX and DX are 0 going in (a timer has no point), so
; the C signature takes the window and nothing else. The first C package to
; want either of these is WEAVE, whose <input> blinks a caret (WEAVE-SPEC 6.7);
; the pair had no C path before that, which is why it is here and not older.
; in:  SI = the window; the gfx lock IS held
; out: nothing; every register the kernel cares about is preserved
; -----------------------------------------------------------------------------
cc_ontimer:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si                         ; arg 1: void *win
    call _os88_ontimer
    add sp, 2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
%endif

%ifdef CC_HAS_ONCLOSE
; -----------------------------------------------------------------------------
; cc_onclose - THE CLOSE NEGOTIATOR (SPEC.md 75.1), installed by
; os88_wm_onclose(). The kernel asks the window before it closes it, through
; EVERY door there is - the close box, the app-name menu's Close, the dock
; tile's context menu - and the window may refuse.
;
; It is W_ONCLICK's environment: the UI task, the gfx lock HELD, SI = the
; window. So the C may draw, may call the file slots, may raise an alert - and
; must not take the lock and must not take long.
;
; THE ANSWER RIDES IN CF, which is why this is not a copy of cc_ontimer:
; `wm_ask_close` calls us and branches on the carry with nothing in between
; that touches the flags (SPEC.md 75.1), so the flags this `ret` leaves ARE
; the answer. SPEC.md 73.4 pins the shape - "pops, then `or ax, ax`, then
; stc/clc, then ret", because a `pop` does not write FLAGS and AX is the one
; register a thunk deliberately does not save.
;
; The C returns non-zero to LET IT CLOSE and zero to REFUSE. A refusal now
; owes the user a way out: put up an alert and call os88_wm_close() when it is
; answered. A refusal with nothing behind it is a window that cannot be
; closed, and real mode has no way to take that back.
;
; in:  SI = the window; the gfx lock IS held
; out: CF = 0 close it, CF = 1 refused; every register but AX preserved
; -----------------------------------------------------------------------------
cc_onclose:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si                         ; arg 1: void *win
    call _os88_onclose
    add sp, 2
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    or ax, ax                       ; ...and only now, with nothing left to
    jz .refuse                      ; restore, is FLAGS the answer
    clc
    ret
.refuse:
    stc
    ret
%endif

%ifdef CC_HAS_ONWAKE
; -----------------------------------------------------------------------------
; cc_onwake - W_ONWAKE, the package's own kick (SPEC.md 74.1), installed by
; os88_wm_onwake(). THE ONE CALLBACK THAT RUNS WITHOUT THE GFX LOCK: the UI
; task popped an EVT_WAKE that OSAPI_WM_WAKE posted (from a callback, from the
; handler itself, from a worker) and dispatches it here, billed to the
; instance, lock free. So the C may call the file slots and may take the lock
; itself with os88_gfx_lock() for a burst it can state - and must not draw
; before it has. Nothing arrives with it: it is a kick, and one after the
; window's slot was reused is possible, so the handler must be indifferent to
; being called with nothing to do.
; in:  SI = the window; lock NOT held
; out: nothing; every register the kernel cares about is preserved
; -----------------------------------------------------------------------------
cc_onwake:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    push si                         ; arg 1: void *win
    call _os88_onwake
    add sp, 2
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
; The size arrives as 32 bits and C has no such type (SPEC.md 73.7), so it is
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
;      IF = 1, the gfx lock free, SP at the top of a 384-byte slice in LOW_SEG
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
    mov [cc_wksp], sp               ; the top of THIS task's 384-byte slice,
                                    ; which is how cc_iswk answers "am I the
                                    ; worker" without a kernel call - see it
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

; -----------------------------------------------------------------------------
; cc_iswk - is this the worker task?
; out: CF=1 on the worker, CF=0 on the UI task. Preserves every register.
;
; SPEC.md 20.6 rule 7 forbids a whole family of calls on a worker - claims,
; file I/O, anything that touches the FAT - and the kernel has no slot that
; answers "which task am I", so the runtime works it out from SP. Every task
; stack is a 384-byte slice of LOW_SEG (kernel/sched.inc, SCH_STACK) and every
; task runs with SS = LOW_SEG, so SP alone identifies the slice: cc_worker
; banks its own stack top on entry, and the worker is the task whose SP is
; inside that slice. Exact rather than approximate - the UI task's SP cannot be
; in the worker's slice, because the slices do not overlap.
;
; A FLAG WOULD NOT DO, and getting this wrong is easy to miss: a byte raised
; when cc_worker starts is still raised while the UI TASK runs, because the
; worker never exits. Every UI-task overlay load would then be refused, which
; presents as a feature that works until the app spawns a worker and never
; again.
; -----------------------------------------------------------------------------
cc_iswk:
    push ax
    mov ax, [cc_wksp]
    or ax, ax                       ; no worker at all: this is the UI task,
    jz .no                          ; and it is the only task there is
    cmp sp, ax                      ; SP is BELOW the banked top (we pushed AX,
    ja .no                          ; and the worker pushed its own frames)
    sub ax, CC_STACK                ; ...and no further below than one slice
    cmp sp, ax
    jbe .no
    pop ax
    stc
    ret
.no:
    pop ax
    clc
    ret

%ifdef CC_HAS_OVL
; =============================================================================
; THE OVERLAY (SPEC.md 73.14)
;
; A package links at org 0 and addresses itself with 16-bit offsets (SPEC.md
; 33), so image + bss can never reach 64KB whatever the heap has free - and C
; spends that ceiling two to three times faster than assembly does (73.9). A
; MODULE HAS A SEGMENT OF ITS OWN, so it does not spend the package's.
;
; It is kernel SPEC.md 2.8's shape and SPEC.md 68.10's, not a driver's: the
; module keeps DS = THE PACKAGE'S SEGMENT and reaches every global, every
; string literal and every .bss byte through it exactly as resident code does.
; That is what makes moving a subsystem out a matter of moving its text - and
; in C it is what makes it possible at all, because a compiled function names
; its data with plain DS-relative references and there is no way to tell the
; compiler otherwise.
;
; tools/cc8086.py does the moving: a C function whose name begins with `ovl_`
; is emitted into .modc instead of .text, every call TO one becomes a far call
; through a vector here, and every call FROM one to resident code becomes a far
; call through a shim. This file owns only what cannot be generated - the load,
; the stamp check, the binding and the refusal.
;
; THE FOUR WORDS OF STATE, and why the stamp is two of them: WORD.OVL and
; WORD.O88 are separate files on a floppy, so a rebuilt package beside a stale
; module is a thing a user can produce with a file copy. The module carries the
; two sizes it was built with; both change under almost any edit to either
; half; and the check costs four bytes and one compare. It catches a stale
; PAIR, not every stale pair - two builds whose resident image and module are
; both the same size would pass it - and that is worth stating rather than
; implying.
; =============================================================================

section .modc
; +0 of the module, because tools/os88ovl.py cuts here and the loader below
; reads these two words before it believes anything else in the file.
cc_modc_beg:
    dw cc_modc_end                  ; the module's own size...
    dw cc_image_end                 ; ...and the resident image it was built
                                    ;    with. Both are absolute labels: .modc
                                    ;    is vstart=0 so the first IS a size,
                                    ;    and .text starts at 0 so the second is
                                    ;    the resident file's length.
section .text

; -----------------------------------------------------------------------------
; cc_ovneed - make sure the module is in memory
;
; out: CF=0 and [cc_ovseg] is the claim holding it;
;      CF=1, AX=0 and the toast already says why.
; preserves: every register but AX (which is the C return register, and is
;      dead at the call site this is generated in front of - see 67.14)
;
; UI TASK ONLY. It claims and it reads a floppy, both of which SPEC.md 20.6
; rule 7 forbids on a worker, so a worker asking for an overlay feature is
; refused rather than allowed to corrupt the FAT. cc_worker raises [cc_inwk]
; before it calls the C, which is the whole of the gate.
; -----------------------------------------------------------------------------
cc_ovneed:
    call cc_iswk                    ; THE WORKER NEVER ENTERS THE MODULE, and
    jc .nowk                        ; not only because loading it is forbidden
                                    ; out there: cc_ovthunk's return stash is
                                    ; one LIFO, and one LIFO is only correct
                                    ; for one task. The rule is the simple one
                                    ; - overlay code is UI-task code - and it
                                    ; is enforced HERE, at the only door in
    cmp word [cc_ovseg], 0
    je .load
    clc                             ; already in: the common path
    ret
.load:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [cc_ovsize]             ; KB = ceil(size / 1024), computed here
    add ax, 1023                    ; rather than by the assembler: nasm will
    mov cl, 10                      ; not divide a section-relative label, and
    shr ax, cl                      ; a build that fails on arithmetic is a
    mov di, ax                      ; worse trade than four instructions
    call OSAPI_MEM_CLAIM            ; AX = KB; out DX = the segment
    jc .nomem
    mov [cc_ovseg], dx
    mov es, dx
    xor bx, bx                      ; ES:BX = the claim, from its first byte
    mov cx, di                      ; CX = the capacity, in bytes
    mov ax, 1024
    mul cx                          ; DX:AX - the claim is under 64KB by
    mov cx, ax                      ; construction (it is a segment), so DX is
    xor dx, dx                      ; 0 and the count is CX alone
    mov si, cc_ovfile               ; the name resolves in THIS INSTANCE's
    call OSAPI_FILE_READ            ; directory - the folder it was launched
    jc .noread                      ; from (SPEC.md 19.2.1), so no visit is
                                    ; needed and none is made
    mov es, [cc_ovseg]
    cmp word [es:0], cc_modc_end    ; the stamp: the module's size...
    jne .stale
    cmp word [es:2], cc_image_end   ; ...and the image it was built beside
    jne .stale
    call cc_ovbind                  ; the vectors are addresses until this runs
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.stale:
    mov ax, cc_ovm_stale
    jmp short .free
.noread:
    mov ax, cc_ovm_gone
.free:
    push ax                         ; a half-loaded module is worse than none
    mov dx, [cc_ovseg]
    call OSAPI_MEM_FREE
    mov word [cc_ovseg], 0
    pop ax
    jmp short .say
.nomem:
    mov ax, cc_ovm_mem
.say:
    mov si, ax                      ; the kernel COPIES the string (SPEC.md
    push ds                         ; 59), so nothing here has a lifetime
    pop es
    xor cx, cx                      ; CX = 0: the default ~3s
    call OSAPI_TOAST
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
.nowk:
    xor ax, ax                      ; the refused call's return value: 0. A C
    stc                             ; function that can be refused answers a
    ret                             ; status, and 0 is "it did not happen"

; -----------------------------------------------------------------------------
; cc_ovthunk - the module calling resident code: the ONE place the two calling
; conventions meet, reached by a six-byte stub per resident routine.
;
; in:  BX = the resident near proc; the stack is [ret IP][ret CS][arguments]
;      as the module's `call far` left it
; out: whatever the routine answered, in AX, back in the module
;
; WHY THIS IS NOT `call _f` / `retf`, which is what SPEC.md 68.10's assembly
; module uses and is right there to copy. A far call pushes FOUR bytes, and a
; shim that then makes a near call of its own pushes two more, so the routine's
; first argument sits at [bp+8] while every compiled reference to it says
; [bp+4]. In assembly that costs nothing, because a shimmed routine takes its
; arguments in REGISTERS and never looks at the stack. In C it is fatal and it
; is silent: the routine reads the module's return offset as its first
; argument. Measured on tests/covl before this existed - a function told 10 saw
; 49, which is exactly where the far call had come from in a 110-byte module.
;
; So the four bytes come OFF, and the routine is entered with its arguments
; where it expects them:
;
;   [IP][CS][args]   --pop--pop-->   [args]   --call-->   [ret][args]
;
; The segment is not stashed because it is [cc_ovseg] by construction - there
; is one module - so only the offset needs somewhere to live across the call.
;
; THE STASH IS A GLOBAL LIFO, AND THAT IS SAFE FOR EXACTLY ONE REASON: overlay
; code is UI-task code, enforced in cc_ovneed at the only door in. Two tasks
; sharing one LIFO would not be a race that a `cli` could fix - the entries
; are LIFO per task and interleave across tasks - so the fix is the rule, not
; a critical section. Nesting is real (module -> resident -> module -> resident
; happens the moment a resident routine reached from the module calls another
; overlay function), which is why it is a stack at all; CC_OVDEPTH of them is
; far more than the UI task's ~700 bytes of headroom can hold C frames for, so
; the task stack is the limit that binds and this one cannot be reached first.
; -----------------------------------------------------------------------------
cc_ovthunk:
    pop ax                          ; the module's return offset...
    pop dx                          ; ...and its segment, which is [cc_ovseg]:
                                    ; discarded rather than stashed
    mov si, [cc_ovrsp]              ; AX, DX and SI are all dead here - the ABI
    mov [cc_ovrst+si], ax           ; has no callee-saved register, so nothing
    add si, 2                       ; is live in one across a call, which is
    mov [cc_ovrsp], si              ; the same fact the push-imm lowering rests
    call bx                         ; on (tools/cc8086.py)
    mov si, [cc_ovrsp]
    sub si, 2
    mov [cc_ovrsp], si
    push word [cc_ovseg]            ; AX is the answer and nothing above
    push word [cc_ovrst+si]         ; touches it
    retf

; -----------------------------------------------------------------------------
; cc_ovbind - stamp the segments into both vector tables. Preserves all.
;
; Every vector is a dword of (offset, segment) whose OFFSET is assembled in -
; the module is assembled WITH the package, one nasm job, so both halves agree
; about every address by construction - and whose segment is the one runtime
; fact. There are two tables and they take different segments:
;
;   cc_ovm_*  the module's own entry points: the claim it was read into
;   cc_ovv_*  the resident shims the module far-calls back through: CS
;
; Both are generated by tools/cc8086.py, contiguous, and walked here, so
; adding a function to either side is no change to this file.
; -----------------------------------------------------------------------------
cc_ovbind:
    push ax
    push bx
    mov ax, [cc_ovseg]
    mov bx, cc_ovm_first
.m:
    cmp bx, cc_ovm_end
    jae .res
    mov [bx+2], ax
    add bx, 4
    jmp short .m
.res:
    mov ax, cs
    mov bx, cc_ovv_first
.v:
    cmp bx, cc_ovv_end
    jae .done
    mov [bx+2], ax
    add bx, 4
    jmp short .v
.done:
    pop bx
    pop ax
    ret

section .data
cc_ovfile:  db CC_PKG_NAME, '.OVL', 0
cc_ovm_mem: db 'Not enough memory for ', CC_PKG_NAME, '.OVL', 0
cc_ovm_gone:db CC_PKG_NAME, '.OVL is not on this disk', 0
cc_ovm_stale: db CC_PKG_NAME, '.OVL does not match this program', 0
section .text
%endif  ; CC_HAS_OVL

%ifdef CC_HAS_PARTS
; =============================================================================
; THE PARTS STANDARD (SPEC.md 20.12)
;
; A C package that carries more than its own segment - a second segment of
; code, or an asset it would otherwise trail as a sidecar file - declares
; CC_HAS_PARTS and a table, and gets op_seg/op_fetch/op_drop through the
; thunks below. The kernel learns one bit about any of it.
;
; TWO THINGS DIFFER FROM AN ASSEMBLY PACKAGE, and both are handled here so the
; shim does not have to know:
;
;   * THE STANDARD'S BSS. Its words are an equ chain off OP_BSS_AT, which for
;     an assembly package is `os88_image_end` - the bytes past the image. A C
;     package has no such label; its bss is a real section the loader zeroes
;     between cc_bss_beg and cc_bss_end. So OP_BSS bytes are reserved down
;     there and OP_BSS_AT points at them.
;
;   * THE TABLE'S SECTION. OS88_PARTS_BEGIN emits data wherever the assembler
;     happens to be, and in a C package that is whatever section the last
;     %include left open. CC_PARTS_BEGIN/CC_PARTS_END put it in .data and
;     leave the assembler back in .text, which is CLAUDE.md's section rule
;     applied at the one place a C author would otherwise have to know it.
;
; And op_load is called by cc_entry, FIRST, because SI arrives holding the
; kernel's name pointer and nothing later can recover it (SPEC.md 20.2).
; =============================================================================
%define OP_BSS_AT cc_parts_bss
%include "os88parts.inc"

; CC_PARTS_BEGIN n / OS88_PART ... / CC_PARTS_END - the shim's table.
%macro CC_PARTS_BEGIN 1
section .data
    OS88_PARTS_BEGIN %1
%endmacro
%macro CC_PARTS_END 0
    OS88_PARTS_END
section .text
%endmacro
%endif  ; CC_HAS_PARTS

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

%ifdef CC_HAS_OVL
; The module's size, as a word the resident half can read before it has seen
; the module: .modc is vstart=0, so this label's value IS the byte count, and
; cc_ovneed turns it into the KB it claims. It is initialised DATA rather than
; an equ because `%if` cannot compare a section-relative label to a constant
; (SPEC.md 73.2, the same reason there is no assembly-time size assertion).
cc_ovsize:  dw cc_modc_end
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
cc_wksp:    resw 1                  ; SP at the top of the worker's stack
                                    ; slice, 0 until one is spawned. cc_iswk
                                    ; compares against it. Defined whether or
                                    ; not this package HAS a worker, so the
                                    ; test needs no %ifdef of its own
%ifdef CC_HAS_PARTS
cc_parts_bss: resb OP_BSS           ; the parts standard's own words, which an
                                    ; assembly package puts past its image and
                                    ; a C package puts here (OP_BSS_AT above)
%endif
%ifdef CC_HAS_OVL
cc_ovseg:   resw 1                  ; the claim the module was read into,
                                    ; 0 = not loaded yet (SPEC.md 73.14)
cc_ovrsp:   resw 1                  ; and the return stash cc_ovthunk keeps -
cc_ovrst:   resw CC_OVDEPTH         ; see it for why a global LIFO is safe
%endif

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
; the names SPEC.md 73.2 pins, and they resolve because each is a difference
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
%ifdef CC_HAS_OVL
section .modc
cc_modc_end:                        ; .modc is vstart=0, so this label's value
                                    ; is the module's size - which is what the
                                    ; stamp compares and what cc_ovneed claims
%endif
section .data
align 2, db 0                       ; see above - NOT cosmetic
cc_image_end:
CC_IMAGE_SIZE equ cc_image_end
CC_BSS_SIZE   equ cc_bss_end - cc_bss_beg
section .text
%endmacro
