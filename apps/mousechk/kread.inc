; =============================================================================
; os8088 - apps/mousechk/kread.inc - reading the kernel's debug registry
;                                    (SPEC.md 57.3's exception, 126.4)
;
; MOUSECHK is the ONE shipped package that reads the registry's 'MO', 'ID' and
; 'VD' blocks (owner decision O1, 2026-11-05). It pays for it by checking, once
; at open, that the kernel it is running on is the build it was MADE with: the
; 'ID' block's three words (SPEC.md 57.6) against the three the build wrote
; into mousechk_kid.inc from the kernel's own map. They match -> [mc_kok] = 1
; and the offsets in moblk.inc may be decoded. They do not (another release's
; kernel, a knob build, a package carried between kernels) -> [mc_kok] = 0 and
; every section shows RAW HEX and says it cannot decode it. Nothing here is
; ever guessed.
;
; ES = KERNEL_SEG throughout: it is on entry to the entry proc and to every
; callback, and kernel memory is read through it (CLAUDE.md, package boundary).
; =============================================================================

%include "mousechk_kid.inc"         ; MC_KID_A, MC_KID_B, MC_KID_C (generated)

; -----------------------------------------------------------------------------
; mc_dbgfind - look a published block up in the debug registry (SPEC.md 57.2)
; in:  AX = the tag (DBG_TAG_* in os88api.inc); ES = KERNEL_SEG
; out: CF = 1 this kernel does not publish it, else CF = 0 and BX = the block's
;      offset in KERNEL_SEG. The tag is checked twice, as the registry pair and
;      as the block's own first word, so an offset followed to the wrong place
;      answers "not published". Every other register kept
; -----------------------------------------------------------------------------
mc_dbgfind:
    push si
    mov si, [es:0x000E]             ; 0060:000E, the registry, or 0
    or si, si
    jz .none
.scan:
    mov bx, [es:si]
    or bx, bx
    jz .none                        ; end of list
    cmp bx, ax
    je .hit
    add si, 4
    jmp short .scan
.hit:
    mov bx, [es:si+2]
    cmp [es:bx], ax
    jne .none
    pop si
    clc
    ret
.none:
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; mc_kinit - once at open: bank the 'ID' words and set [mc_kok]
; in:  ES = KERNEL_SEG; out: nothing; keeps every register
; A mismatch still banks all three words, so a section can print them as the
; report's "which kernel is this".
; -----------------------------------------------------------------------------
mc_kinit:
    push ax
    push bx
    push cx
    mov byte [mc_kok], 0
    mov ax, DBG_TAG_BUILD
    call mc_dbgfind
    jc .out
    mov bx, [es:bx+2]               ; the span: three plain words
    mov ax, [es:bx]
    mov [mc_kid], ax
    mov ax, [es:bx+2]
    mov [mc_kid+2], ax
    mov ax, [es:bx+4]
    mov [mc_kid+4], ax
    xor cx, cx                      ; CX = 0 while they all match
    mov ax, [mc_kid]
    xor ax, MC_KID_A
    or cx, ax
    mov ax, [mc_kid+2]
    xor ax, MC_KID_B
    or cx, ax
    mov ax, [mc_kid+4]
    xor ax, MC_KID_C
    or cx, ax
    jnz .out
    mov byte [mc_kok], 1
.out:
    pop cx
    pop bx
    pop ax
    ret
