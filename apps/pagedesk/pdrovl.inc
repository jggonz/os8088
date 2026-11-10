; =============================================================================
; os8088 - apps/pagedesk/pdrovl.inc     PDREND's second code segment
;
; PAGEDESK-SPEC 20.22.7.9: the module flavour of SPEC.md 73.14's runtime
; (apps/cc/crt0.asm's cc_ov*), so that tools/cc8086.py's `ovl_*` split works
; for PDREND. A C function named ovl_* (pdrovl.h renames them) is emitted into
; .modc, which pdrend.asm declares `follows=.data vstart=0` and the Makefile
; CUTS off the assembled module into PDRENDC.MOD - a second PART of
; PAGEDESK.O88 that the primary loads with PDREND into a claim of its own and
; names with PDV_REND_CODE before any other verb. Every global, literal and
; bss byte the moved code names stays in PDREND's DS: ONLY CODE MOVES.
;
; What differs from crt0's lazy part, and why:
;   - cc_ovneed is never called: the part is loaded and bound before a verb
;     runs (the door refuses every verb until PDV_REND_CODE has bound it), so
;     cc8086 runs with --ovl-resident and a call site is the far call alone;
;   - the bind is the door's, on PDV_REND_CODE: BX = the part's CODE selector
;     (OS88_CODESEL'd by the primary on kern_mega; = CX on a real-mode kernel)
;     and CX = its DATA segment, where the stamp is checked. A primary that
;     moves the claim sends PDV_REND_CODE again;
;   - the stash is cc_ovthunk's LIFO, and it is safe for crt0's reason: PDREND
;     runs on one task (the primary's, on its deep stack).
; =============================================================================

PDR_OVDEPTH     equ 16                  ; module -> resident -> module nesting
PDR_MODC_MAGIC  equ 0x4352              ; 'RC'

section .modc
pdr_modc_beg:
    dw PDR_MODC_MAGIC                   ; the stamp the door checks: magic...
    dw pdr_modc_end                     ; ...and the part's size, which ties it
                                        ; to THIS build of PDREND.MOD

section .data
cc_ovcs:    dw 0                        ; the part's code selector

section .bss
pdr_ovok:   resw 1                      ; 1 = bound (cleared with the bss)
cc_ovrsp:   resw 1
cc_ovrst:   resw PDR_OVDEPTH

section .text
; the door's PDV_REND_CODE: BX = code selector, CX = data segment. CF = 0 and
; AX = 0 bound, CF = 1 AX = PDE_RANGE a part that is not this build's.
pdr_ovcode:
    push es
    mov es, cx
    cmp word [es:0], PDR_MODC_MAGIC
    jne .bad
    cmp word [es:2], pdr_modc_end
    jne .bad
    pop es
    mov [cc_ovcs], bx
    push bx
    mov ax, bx
    mov bx, cc_ovm_first                ; the module's entries: its selector
.m: cmp bx, cc_ovm_end
    jae .r
    mov [bx+2], ax
    add bx, 4
    jmp short .m
.r: mov ax, cs                          ; the resident shims: our own CS
    mov bx, cc_ovv_first
.v: cmp bx, cc_ovv_end
    jae .ok
    mov [bx+2], ax
    add bx, 4
    jmp short .v
.ok:
    pop bx
    mov word [pdr_ovok], 1
    xor ax, ax
    clc
    ret
.bad:
    pop es
    mov ax, PDE_RANGE
    stc
    ret

; cc8086 names it at a resident call site only without --ovl-resident; kept so
; a build without the flag still assembles and runs (the part is always here)
cc_ovneed:
    clc
    ret

; -----------------------------------------------------------------------------
; cc_ovthunk - the module calling a RESIDENT routine (crt0.asm's, verbatim but
; for the stash's size): BX = the routine, the stack [IP][CS][args] as the
; module's `call far [cc_ovv_*]` left it, turned into the near call the
; routine's frame expects and back.
; -----------------------------------------------------------------------------
cc_ovthunk:
    pop ax                              ; the module's return offset...
    pop dx                              ; ...and its CS, which is [cc_ovcs]
    mov si, [cc_ovrsp]
    cmp si, PDR_OVDEPTH * 2
    jae .full
    mov [cc_ovrst+si], ax
    add si, 2
    mov [cc_ovrsp], si
    call bx
.back:
    mov si, [cc_ovrsp]
    sub si, 2
    mov [cc_ovrsp], si
    push word [cc_ovcs]
    push word [cc_ovrst+si]
    retf
.full:                                  ; a full stash refuses the call: AX = 0
    push dx
    push ax
    xor ax, ax
    retf

; cc_ovmthunk - the module calling another module function (crt0.asm's): BX =
; the callee's cc_ovm_* vector. The part never moves while a verb runs, so a
; full stash's direct far call is exact, not a margin.
cc_ovmthunk:
    pop ax
    pop dx
    mov si, [cc_ovrsp]
    cmp si, PDR_OVDEPTH * 2
    jae .full
    mov [cc_ovrst+si], ax
    add si, 2
    mov [cc_ovrsp], si
    call far [bx]
    jmp short cc_ovthunk.back
.full:
    push dx
    push ax
    jmp far [bx]
