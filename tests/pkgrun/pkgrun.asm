; =============================================================================
; os8088 - tests/pkgrun/pkgrun.asm
;
; PKGRUN - the capability gate for OSAPI_PKG_START (SPEC.md 21.5). A TEST
; package: `make pkgrun` builds it and no shipped floppy carries it, exactly
; like tests/multiseg and tests/wire (SPEC.md 78.9).
;
; THREE CHECKS, on the one image, in order:
;
;   A  the image runs.   HELLO.O88 is read off the disk beside us into a claim
;      of ours and handed to the slot. CF=0 and AX=0, and the KERNEL's own
;      instance table then holds a live record named HELLO - which is what
;      tests/pkgrun.py reads, so the pass is asserted against the kernel and
;      not against this package's opinion of it.
;   B  a corrupt magic is REFUSED.  One byte of the header is spoiled and the
;      same call must answer CF=1 with AL = LD_EBAD (2).
;   C  a PARTS image is REFUSED.  The magic is put back and header flags bit 2
;      is set instead (SPEC.md 20.12): parts are read by a package out of its
;      OWN FILE and there is none here, so this is LD_EBAD as well - and it is
;      a DIFFERENT refusal from B, decided before ld_check_hdr rather than
;      inside it.
;
; ...AND THREE MORE ON THE OTHER DOOR (SPEC.md 21.5), because the pair is the
; point.  OSAPI_PKG_START is the loader's FRONT half - a NAME rather than an
; image - and what it settles is that PKG_RUN's parts refusal belongs to the
; CALLER'S SITUATION and not to the file:
;
;   D  the same HELLO.O88 runs BY NAME.  CF=0, and the kernel's table then
;      holds TWO live records named HELLO - one per door.
;   E  MSEG.O88 - a real package carrying five parts (tests/multiseg) - is
;      handed to PKG_RUN out of a claim and REFUSED, and then opened BY NAME
;      and RUNS.  One file, two doors, two answers.  The parts really arrive:
;      MSEG rewrites its own window title to `MSEG 5/5 OK` and tests/pkgrun.py
;      reads it, so this is not merely `a window appeared`.
;   F  a name that is not there answers CF=1 with AL = LD_EBAD, which by name
;      is the same code as `that file is not a package` (SPEC.md 21.4).
;
; THE VERDICT IS A BLOCK AT OFFSET 32, immediately after the 32-byte header
; and before any code, so the host reads it with no map of this package at
; all: find the live instance named PKGRUN, take its I_SPTR, read fourteen
; bytes at offset 32 of that segment. It opens with the tag 'PR', which is
; the debug registry's own habit (SPEC.md 57) - a reader that followed the
; wrong pointer can tell.
;
; THE CHECKS RUN FROM THE WAKE HANDLER AND NOT FROM THE ENTRY PROC, and that
; is a correctness requirement rather than a style: the entry proc runs INSIDE
; ld_start (SPEC.md 21 step 8), so calling the loader from it would re-enter
; [ld_rec] / [ld_base] / [ld_need] and load one package over another's
; bookkeeping. The entry proc creates the window and posts ONE wake to itself
; (OSAPI_WM_WAKE is legal from any context), and ui_task pops it after the
; load has finished - on the UI task, with no lock held, which is the slot's
; documented context.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'PKGRUN', pr_entry

; --- the verdict, at a FIXED offset (see the banner) --------------------------
; It is here, before the entry proc, because a host that has to be told an
; offset has to be told again every time this file changes.
pr_res:
    db 'P', 'R'                     ; +32 the tag
pr_done:    db 0                    ; +34 the ENTRY COUNT at the moment the
                                    ;     checks finished, so 1 = they ran to
                                    ;     the end on the first wake and every
                                    ;     later one arrived after them
pr_ok:      db 0                    ; +35 bit 0 = A, bit 1 = B, bit 2 = C
pr_cfa:     db 0                    ; +36 the CF each call answered, 0 or 1
pr_cfb:     db 0                    ; +37
pr_cfc:     db 0                    ; +38
pr_ala:     db 0                    ; +39 ...and the AL each one answered
pr_alb:     db 0                    ; +40
pr_alc:     db 0                    ; +41
pr_ferr:    db 0                    ; +42 OSAPI_FILE_READ's FERR_*, 0 = read
pr_len:     dw 0                    ; +43 ...and the bytes it delivered
pr_ent:     db 0                    ; +45 how many times pr_onwake was ENTERED,
                                    ;     which is what guards it - see there
pr_cfd:     db 0                    ; +46 ...and OSAPI_PKG_START's three (21.6)
pr_cfe:     db 0                    ; +47
pr_cff:     db 0                    ; +48
pr_ald:     db 0                    ; +49
pr_ale:     db 0                    ; +50
pr_alf:     db 0                    ; +51
pr_cfe1:    db 0                    ; +52 E's FIRST half: the SAME file handed
pr_ale1:    db 0                    ; +53 to PKG_RUN, which must refuse it
pr_ferr2:   db 0                    ; +54 the MSEG read's FERR_*, 0 = read
pr_len2:    dw 0                    ; +55 ...and the bytes it delivered
%if ($ - $$) != 57
  %error "the verdict block must start at offset 32 and be 25 bytes: tests/pkgrun.py reads it by ARITHMETIC, not by a map"
%endif

PR_CLAIM_KB equ 20                  ; HELLO.O88 is under a kilobyte and four was
                                    ; room for it to grow - but check E reads
                                    ; MSEG.O88 into this same claim, and that
                                    ; one is ~13KB of image and five parts
                                    ; (SPEC.md 20.12). Twenty is room for both
                                    ; without a second claim to fail on
PR_OFF      equ 64                  ; ...and the image sits THIS FAR INTO the
                                    ; claim, which is a regression guard and
                                    ; not tidiness. The first version read it
                                    ; to offset 0 and passed SI = 0, so the
                                    ; slot could - and did - lose the source
                                    ; OFFSET entirely and still copy the right
                                    ; bytes. A non-zero offset makes the
                                    ; argument load-bearing, and the poison
                                    ; below is what a lost one reads instead
PR_POISON   equ 0xA5
PR_CONT_W  equ 286                  ; content width:  288 outer - 2px borders
PR_CONT_H  equ 81                  ; ...and 100 outer - TITLE_H - 1. SIX rows
                                    ; at PR_ROW_H now, plus the 6px top
                                    ; margin, is 78 - so 76 outer stopped
                                    ; fitting when SPEC.md 21.5's three
                                    ; arrived
PR_ROW_H   equ 12

LD_EBAD    equ 2                    ; SPEC.md 21.4, mirrored - a test package
                                    ; may not include kernel/loader.inc

; -----------------------------------------------------------------------------
; pr_entry - package entry (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, gfx lock NOT held
; out: BX = window ptr, CF clear
; -----------------------------------------------------------------------------
pr_entry:
    push ax
    push si
    mov si, pr_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    jc .out
    mov [pr_win], bx                ; the wake handler is handed SI = the
                                    ; window, but the repaint below wants it
                                    ; from a path that has spent SI
    mov ax, pr_onwake
    call OSAPI_WM_ONWAKE            ; BX = the window we just created
    call OSAPI_WM_WAKE              ; ...and kick ourselves once. It is legal
                                    ; from any context and the UI task pops it
                                    ; after this load has finished, which is
                                    ; the whole reason the checks are not here
    clc                             ; OSAPI_WM_WAKE answers CF=1 on a full ring
.out:                               ; and that is not this package refusing to
    pop si                          ; start - the loader reads our CF
    pop ax
    ret

; -----------------------------------------------------------------------------
; pr_onwake - the three checks (SPEC.md 21.5)
; in:  SI = our window; UI task, NO gfx lock held
; out: nothing
; -----------------------------------------------------------------------------
pr_onwake:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; THE GUARD IS THE ENTRY COUNT and not [pr_done], because the two are not
    ; the same question. A stale wake AFTER the checks is ordinary and
    ; [pr_done] would catch it; a SECOND ENTRY WHILE THEY RUN would re-enter
    ; the loader through the very slot under test, and it would do it with the
    ; image already spoiled by check B - so the first pass's answers get
    ; overwritten by a second pass's and the block reads as three refusals
    ; beside an all-passed mask, which is a contradiction rather than a
    ; failure. It was seen once, on one run of this gate, and no path in
    ; ui_task's wake dispatch explains it - so the count is BOTH the guard and
    ; the evidence: tests/pkgrun.py prints it, and any value but 1 is a
    ; finding whether the checks passed or not. A wake handler has to be
    ; indifferent to being called with nothing to do (SPEC.md 74.1) and this
    ; is that, written so it also cannot be called with something to do TWICE.
    inc byte [pr_ent]
    cmp byte [pr_ent], 1
    jne .done

    ; --- the image, off the disk beside us --------------------------------
    mov ax, PR_CLAIM_KB
    call OSAPI_MEM_CLAIM            ; out CF=1 refused, DX = the base segment
    jc .done
    mov [pr_seg], dx
    mov es, dx

    ; --- poison the head of the claim, THEN read the image in behind it ----
    ; A slot that loses the source offset copies from here, and PR_POISON is
    ; what it gets: not a package header, so ld_check_hdr's own re-read would
    ; refuse it - except that the check happens BEFORE the copy, on the bytes
    ; the caller named. So the poison does not make the failure loud, it makes
    ; it DIFFERENT from the truth, which is what the host's byte-for-byte
    ; compare of the running instance's image needs.
    xor di, di
    mov cx, PR_OFF
    mov al, PR_POISON
    cld
    rep stosb

    mov bx, PR_OFF
    mov cx, PR_CLAIM_KB * 1024 - PR_OFF
    xor dx, dx                      ; DX:CX = the buffer's capacity
    mov si, pr_s_file
    call OSAPI_FILE_READ            ; out CF=0 and DX:AX = the bytes read
    jnc .read
    mov [pr_ferr], al
    jmp .paint
.read:
    mov [pr_len], ax                ; the low word: PR_CLAIM_KB bounds it, so
                                    ; DX is 0 and this is the whole length

    ; --- A: it runs -------------------------------------------------------
    mov si, pr_s_file
    call pr_run
    mov [pr_cfa], bl
    mov [pr_ala], al
    or bl, bl
    jnz .b
    or al, al
    jnz .b
    or byte [pr_ok], 1

    ; --- B: a corrupt magic is refused ------------------------------------
.b:
    mov es, [pr_seg]
    mov byte [es:PR_OFF], 0         ; 'O' of the 'O8' magic (SPEC.md 20.2)
    mov si, pr_s_file
    call pr_run
    mov [pr_cfb], bl
    mov [pr_alb], al
    cmp bl, 1
    jne .c
    cmp al, LD_EBAD
    jne .c
    or byte [pr_ok], 2

    ; --- C: a PARTS image is refused --------------------------------------
.c:
    mov es, [pr_seg]
    mov byte [es:PR_OFF], 'O'       ; the magic back...
    or byte [es:PR_OFF+3], 4        ; ...and header flags bit 2 instead
    mov si, pr_s_file
    call pr_run
    mov [pr_cfc], bl
    mov [pr_alc], al
    cmp bl, 1
    jne .free
    cmp al, LD_EBAD
    jne .free
    or byte [pr_ok], 4

    ; --- E, first half: the SAME FILE that D opens, handed to PKG_RUN ------
    ; C proves the flag is refused; this proves it of a REAL parted package,
    ; and it is the half that makes the pair mean something - the second half
    ; below opens this very file by name and it runs (SPEC.md 21.5).
    mov es, [pr_seg]
    mov bx, PR_OFF
    mov cx, PR_CLAIM_KB * 1024 - PR_OFF
    xor dx, dx
    mov si, pr_s_mseg
    call OSAPI_FILE_READ
    jnc .read2
    mov [pr_ferr2], al
    jmp short .free
.read2:
    mov [pr_len2], ax
    mov [pr_len], ax                ; pr_run reads this
    mov si, pr_s_mseg               ; ...and the name that goes WITH the image
    call pr_run
    mov [pr_cfe1], bl
    mov [pr_ale1], al

.free:
    mov dx, [pr_seg]                ; ours to give back: the image was COPIED
    call OSAPI_MEM_FREE             ; and never adopted (SPEC.md 21.5)
    mov word [pr_seg], 0

    ; --- D, E and F: the OTHER door, which takes a NAME (SPEC.md 21.5) -----
    ; The claim is freed first on purpose: these three are about a slot that
    ; needs no image of ours at all, and a heap still holding 20KB of one
    ; would be this package hiding the difference it exists to show.
    mov si, pr_s_file               ; D: HELLO.O88, by name
    call pr_open
    mov [pr_cfd], bl
    mov [pr_ald], al
    or bl, bl
    jnz .e
    or al, al
    jnz .e
    or byte [pr_ok], 8
.e:
    mov si, pr_s_mseg               ; E: ...and the parted package PKG_RUN
    call pr_open                    ; has just refused
    mov [pr_cfe], bl
    mov [pr_ale], al
    cmp byte [pr_cfe1], 1           ; BOTH HALVES, or the pair says nothing:
    jne .f                          ; PKG_RUN must have refused it...
    cmp byte [pr_ale1], LD_EBAD
    jne .f
    or bl, bl                       ; ...and PKG_START must have run it
    jnz .f
    or al, al
    jnz .f
    or byte [pr_ok], 16
.f:
    mov si, pr_s_none               ; F: a name that is not there
    call pr_open
    mov [pr_cff], bl
    mov [pr_alf], al
    cmp bl, 1
    jne .paint
    cmp al, LD_EBAD
    jne .paint
    or byte [pr_ok], 32
.paint:
    mov al, [pr_ent]                ; ...and WHICH entry finished them
    mov [pr_done], al
    mov si, [pr_win]                ; the one callback that is NOT under the
    mov bx, si                      ; lock, so it takes it itself for a burst
    call OSAPI_GFX_LOCK             ; it can state (SPEC.md 74.1) - and it may
    call OSAPI_WM_CLIP_SET          ; not draw before it has
    jc .unlock
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, dx
    mov cx, ax
    add cx, PR_CONT_W - 1
    add dx, PR_CONT_H - 1
    call OSAPI_GFX_FILL             ; AX is x1 already
    call pr_paint                   ; SI = the window still
.unlock:
    call OSAPI_GFX_UNLOCK
.done:
    pop es
    pop di
    pop si                          ; the prologue pushed SI too, and this
                                    ; epilogue used to skip it - 7 pushes, 6
                                    ; pops, so `ret` popped SI's slot as the
                                    ; return address. It survived while the
                                    ; dispatch stack happened to leave a
                                    ; harmless offset there; making the API
                                    ; cells reach their cold bodies without a
                                    ; resident thunk (SPEC.md 20.3.2) moved
                                    ; that offset onto the poison loop, so the
                                    ; stray return ran `rep stosb` over the
                                    ; instance table. Balance it.
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; pr_run - the IMAGE form of the one slot (SPEC.md 21.5)
; in:  [pr_seg] holds the image, [pr_len] its length, SI -> its name
; out: BL = 1 the call answered CF=1, else 0; AL = the code it answered
; clobbers: AX, BX, CX, DX, DI, ES
; -----------------------------------------------------------------------------
pr_run:
    mov es, [pr_seg]
    mov di, PR_OFF                  ; ES:DI = the image, and NOT at offset 0:
                                    ; see PR_OFF
    mov cx, [pr_len]
    xor dx, dx                      ; DX:CX = its length, and NON-ZERO is what
                                    ; says we are holding one at all: zero
                                    ; would read the file (SPEC.md 21.5)
    call OSAPI_PKG_START            ; SI = the name, the caller's to choose
    mov bl, 0
    jnc .out
    mov bl, 1
.out:
    ret

; -----------------------------------------------------------------------------
; pr_open - the BY-NAME form of the one slot (SPEC.md 21.5)
; in:  SI -> a NUL-terminated 8.3 name, in OUR segment
; out: BL = 1 the call answered CF=1, else 0; AL = the code it answered
; clobbers: AX, BX, CX, DX
;
; No claim, no length and no image: the kernel reads the FILE, which is the
; whole difference between this and pr_run above - ONE CELL, and a length of
; zero is what chooses. The name is resolved in the folder THIS INSTANCE is
; standing in (SPEC.md 19.2.1), which is the gate disk's root - where
; HELLO.O88 and MSEG.O88 both are.
; -----------------------------------------------------------------------------
pr_open:
    xor cx, cx                      ; **NO IMAGE: READ THE FILE.** Zero is the
    xor dx, dx                      ; whole of what the by-name form says
    push es                         ; ...and **ES = 0 IS "NOTHING THROUGH
    mov es, cx                      ; ES:DI"** on BOTH arms now (SPEC.md
                                    ; 21.5.3): with DX:CX zero, ES:DI is a
                                    ; DOCUMENT to open the package with, so a
                                    ; caller meaning the plain form has to say
                                    ; so. This one left whatever the CALLER
                                    ; had there, which is exactly the silent
                                    ; break that argument's fence exists for
    call OSAPI_PKG_START            ; beyond the name (SPEC.md 21.5)
    pop es
    mov bl, 0
    jnc .out
    mov bl, 1
.out:
    ret

; -----------------------------------------------------------------------------
; pr_paint - W_PAINT: the verdict in words, for the eye. The ASSERTIONS are
;            tests/pkgrun.py's reads of the block at offset 32; this is so a
;            person looking at a screenshot can see the same three answers.
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
pr_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, ax                      ; BX = the content's left column
    add dx, 6
    mov di, pr_lines                ; DI walks the three names...
    mov cl, 1                       ; ...and CL is the [pr_ok] bit beside each
.row:
    push cx
    mov si, [di]
    mov cx, bx
    add cx, 6
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop cx

    mov si, pr_s_wait
    cmp byte [pr_done], 0
    je .say
    mov si, pr_s_fail
    mov al, [pr_ok]
    test al, cl
    jz .say
    mov si, pr_s_pass
.say:
    push cx
    mov cx, bx
    add cx, 176
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop cx

    add di, 2
    add dx, PR_ROW_H
    shl cl, 1
    cmp di, pr_lines + 12
    jb .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) --------------------------
pr_tpl:
    dw 40, 40, 288, 100
    dw pr_ttl, pr_paint, 0, 0

pr_ttl:     db 'PKGRUN', 0
pr_s_file:  db 'HELLO.O88', 0
pr_s_mseg:  db 'MSEG.O88', 0
pr_s_none:  db 'NOSUCH.O88', 0
pr_s_wait:  db 'running...', 0
pr_s_fail:  db 'FAIL', 0
pr_s_pass:  db 'ok', 0
pr_lines:   dw pr_s_a, pr_s_b, pr_s_c, pr_s_d, pr_s_e, pr_s_f
pr_s_a:     db 'A run from memory', 0
pr_s_b:     db 'B bad magic refused', 0
pr_s_c:     db 'C parts refused', 0
pr_s_d:     db 'D run by name', 0
pr_s_e:     db 'E parts: no/yes', 0
pr_s_f:     db 'F no such name', 0

    OS88_BSS 4
    OS88_IMAGE_END

pr_seg      equ os88_image_end + 0  ; word: the claim holding the image
pr_win      equ os88_image_end + 2  ; word: our window
