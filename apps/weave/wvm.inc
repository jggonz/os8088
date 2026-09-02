; =============================================================================
; os8088 - apps/weave/wvm.inc     THE WJS BYTECODE VM (WEAVE-SPEC 4)
;
; Part of WEAVE, the .WAB runtime (docs/WEAVE-SPEC.md). %included by
; apps/weave/weave.asm after the compiled C, and %included UNCHANGED by
; apps/weave/hosttest/weavevm.asm, which runs it in a raw-QEMU boot sector
; with SS != DS against tools/weavesim.py's end states (WEAVE-SPEC 12.3).
; apps/runcpm/hosttest/rcz80test.asm and apps/c64/hosttest/c64memtest.asm are
; the shape, and their headers carry the doctrine this file obeys: what the
; gate runs is the SHIPPING TEXT, %included and never copied.
;
; -----------------------------------------------------------------------------
; WHY THIS IS ASSEMBLY, AND WHAT IT MAY NOT NAME
; -----------------------------------------------------------------------------
; WEAVE-SPEC 4.12 contracts 10,000-30,000 bytecode ops/s on a 4.77 MHz 8088.
; A C dispatch loop cannot approach that - SPEC.md 73.11 is the rule and the
; two shipped interpreter cores are the precedent - so the whole of section 4's
; machine lives here.
;
; It names NOTHING outside itself except `wvm_native`, the one near proc the
; includer owes it. No OSAPI_ slot, no os88ui_, no C function, no WEAVE state.
; That is not tidiness: the raw-QEMU gate boots this file with no kernel under
; it, and every symbol it reached for would have to be stubbed by the harness -
; which is exactly how a gate stops testing the thing it was written for.
;
; -----------------------------------------------------------------------------
; THE SEGMENTS, WHICH ARE THE WHOLE OF THE DISCIPLINE
; -----------------------------------------------------------------------------
; Inside a slice:
;
;   DS = the VM claim      globals, handles, stacks, ring, arenas (4.7)
;   ES = the bundle claim  bytecode and the ATOMS pool - read-only (2.1)
;   CS = ours              the dispatch table, and every variable this file
;                          owns, reached with an explicit `cs:` override
;   SS:SP = the task stack untouched; the CPU stack never carries VM state
;
; WEAVE-SPEC 4.12 pins the dispatch shape, and the `cs:` on the table is not
; decoration - DS is the claim, so an unprefixed `jmp [bx+wvm_tab]` would read
; the claim's globals as code pointers. CS = DS in a package (SPEC.md 20.1)
; and CS = DS = 0 in the boot-sector harness, so `cs:` reaches our own .bss in
; both, which is what lets one file serve both.
;
;   SI = the bytecode pointer, an offset in the BUNDLE claim
;   DI = the eval-stack pointer, a byte offset in the VM claim, PAST the top
;   CX = ops left in this slice.  EVERY HELPER HERE PRESERVES CX; that is what
;        buys the budget a `jcxz`/`dec` per op rather than a read-modify-write
;        on memory (WEAVE-SPEC 4.7.2's note about HS_LEFT).
;   BP = the C frame pointer of whichever entry point we are inside. Never
;        used as a register here: [bp+n] addresses SS, and SS != DS.
;
; C-callable entry points are cdecl (SPEC.md 73.3): arguments at [bp+4],
; [bp+6], ...; the answer in AX; BP, DS, ES, SS:SP and DF preserved; AX BX CX
; DX SI DI are the caller's to lose. A routine answering in AL clears AH
; itself (apps/cword/cwtype.inc's rule 2).
;
; -----------------------------------------------------------------------------
; THE ONE THING THE INCLUDER OWES: `wvm_native`
; -----------------------------------------------------------------------------
; GETP, SETP, CALLM and the six IMPURE builtins (WEAVE-SPEC 8.1's own split)
; leave this file, because they reach components, the screen, the speaker and
; a file - none of which exists in a boot sector. They leave through one near
; proc the includer defines:
;
;   wvm_native:
;     in:  the request is in the NATIVE BLOCK (WN_* below), whose address the
;          includer handed to wvm_bind - so the block lives in the INCLUDER's
;          memory and C reads and writes it as an ordinary array.
;          DS = the includer's segment; ES = whatever it was when the slice
;          began (a callback's ES is the kernel's, SPEC.md 20.1, and a thunk
;          may rely on it); DF clear.
;     out: AX = 0 done, the answer cell is in WN_REST/WN_RESV
;          AX = 1 script error - WN_ERRC/WN_ERRA/WN_ERRB say which (10.6.1)
;          AX = 2 an allocation did not fit: collect and RE-EXECUTE the op
;                 (WEAVE-SPEC 4.8 - retrying means re-executing)
;     every register but AX is the callee's to lose.
;
; A `call wvm_native` with no definition is an nasm external-reference error
; naming it - the same fence weave.asm's callback %defines stand behind.
; =============================================================================

section .text

; -----------------------------------------------------------------------------
; THE REFUSAL MACROS, and they exist for an 8086 reason (CLAUDE.md's hard rule)
;
; An 8086 has NO near conditional jump - `jne <far label>` is a 386 encoding
; and `cpu 8086` refuses it. Every operand bound in 4.5.1 and every type test
; in 4.4 refuses to one of four labels at the top of the file, and most of the
; tests are hundreds of bytes away from them. Written longhand that is
; `je .ok / jmp wvm_type / .ok:` at fifty sites, with fifty local labels to
; keep unique; written like this it reads as the test it is.
;
; The name is the CONDITION THAT REFUSES: RNE means "refuse when not equal".
; -----------------------------------------------------------------------------
%macro RNE 1
    je %%o
    jmp %1
%%o:
%endmacro
%macro RE 1
    jne %%o
    jmp %1
%%o:
%endmacro
%macro RAE 1
    jb %%o
    jmp %1
%%o:
%endmacro
%macro RB 1
    jae %%o
    jmp %1
%%o:
%endmacro
%macro RC 1
    jnc %%o
    jmp %1
%%o:
%endmacro
%macro RL 1
    jge %%o
    jmp %1
%%o:
%endmacro
%macro RG 1
    jle %%o
    jmp %1
%%o:
%endmacro

; --- WEAVE-SPEC 4.3's tags ---------------------------------------------------
WT_INT   equ 0
WT_STR   equ 1
WT_ARR   equ 2
WT_COMP  equ 3
WT_NULL  equ 4
WT_BOOL  equ 5

; --- WEAVE-SPEC 4.7's claim layout, by offset from the base ------------------
WV_GLOB   equ 0x0000            ; 128 x 4-byte cells
WV_HTAB   equ 0x0200            ; 256 x 4-byte handle entries
WV_STK    equ 0x0600            ; 64 x 4-byte cells
WV_STKEND equ 0x0700            ; one past the last - and the overflow test
WV_FRM    equ 0x0700            ; 16 x 6-byte frames
WV_RING   equ 0x0780            ; 16 x 8-byte records
WV_FXSTK  equ 0x0800            ; wave 4's (5.3); reserved, untouched here
WV_ARENA  equ 0x0880            ; the string arena begins here
WV_ARRSZ  equ 2048              ; the array arena, at S-2064
WV_TAIL   equ 2064              ; ...plus the 16-byte hot scratch

WV_NGLOB  equ 128
WV_NHAND  equ 256
WV_NFRM   equ 16
WV_NRING  equ 16
WV_RECSZ  equ 8

; handle-table entry (4.8)
HT_OFF    equ 0                 ; word: arena offset, 0xFFFF = free
HT_TYPE   equ 2                 ; byte
HT_FLAG   equ 3                 ; byte: bit 0 = mark
HTY_FREE  equ 0
HTY_STR   equ 1
HTY_ARR   equ 2
HTY_SSTR  equ 3                 ; an ATOMS-pool string in the bundle claim

; arena object header (4.8)
AO_SIZE   equ 0                 ; word: header + payload, rounded even
AO_HAND   equ 2                 ; byte
AO_FLAG   equ 3                 ; byte
AO_HDR    equ 4

; frame (4.7.1)
FR_RET    equ 0                 ; word: the CALLER's next bytecode offset
FR_BASE   equ 2                 ; word: eval-stack BYTE offset of local 0
FR_FN     equ 4                 ; byte
FR_NLOC   equ 5                 ; byte
FR_SZ     equ 6

; the hot scratch (4.7.2), from S-16
HS_BUDGET equ 0
HS_LEFT   equ 2
HS_RHEAD  equ 4
HS_RCOUNT equ 5
HS_GCREQ  equ 6
HS_STATE  equ 7
HS_SEED   equ 8

; --- the native block: this file's half of the contract, weave.h's other -----
WN_KIND   equ 0                 ; 0 GETP, 1 SETP, 2 CALLM, 3 BUILT
WN_COMP   equ 2
WN_ATOM   equ 4                 ; or the builtin index when KIND = 3
WN_ARGC   equ 6
WN_REST   equ 8                 ; the answer cell: tag
WN_RESV   equ 10                ; ...and payload
WN_ERRC   equ 12
WN_ERRA   equ 14
WN_ERRB   equ 16
WN_ARG0   equ 18                ; 9 cells x 4 bytes, args in SOURCE order
WN_SIZE   equ 54

WNK_GETP  equ 0
WNK_SETP  equ 1
WNK_CALLM equ 2
WNK_BUILT equ 3

; --- wvm_slice's answers -----------------------------------------------------
WR_IDLE   equ 0                 ; nothing was running
WR_DONE   equ 1                 ; the handler finished
WR_MORE   equ 2                 ; the budget ran out; call again
WR_GC     equ 3                 ; collect, then call again
WR_ERR    equ 4                 ; stopped: wvm_errcode() says why (10.6)

; --- WEAVE-SPEC 10.6.1's sentences, as codes.  The TEXT is the includer's ----
; A boot sector has no status row, and the strings are hundreds of bytes only
; the runtime has anywhere to put.  Codes 7 and up are the runtime's own and
; arrive through WN_ERRC; nothing here ever sets one.
WE_TYPE   equ 0                 ; type mismatch.
WE_DIV0   equ 1                 ; divide by zero.
WE_STRSP  equ 2                 ; out of string space.
WE_DEEP   equ 3                 ; too deep.
WE_AIDX   equ 4                 ; array index %d of %d.
WE_OPCODE equ 5                 ; bad opcode.
WE_BUILT  equ 6                 ; bad builtin.

; =============================================================================
; STATE.  All of it `cs:`-addressed, because DS is the claim inside a slice.
; =============================================================================
section .bss

wvm_vmseg:  resw 1              ; the VM claim, 0 = nothing bound
wvm_bseg:   resw 1              ; the bundle claim
wvm_ourds:  resw 1              ; ...and OURS, banked so a slice can get back
wvm_hs:     resw 1              ; the hot scratch's offset, = S-16
wvm_nblk:   resw 1              ; the native block, in our segment

wvm_code:   resw 1              ; the CODE section's base in the bundle claim
wvm_clen:   resw 1              ; ...and its length
wvm_body:   resw 1              ; the first byte a jump may land on: +2+4F
wvm_nfunc:  resw 1
wvm_atoms:  resw 1              ; the ATOMS section's base
wvm_natom:  resw 1              ; app atoms in the pool (ids 64 .. 63+n)

wvm_pc:     resw 1              ; SI across a slice boundary
wvm_sp:     resw 1              ; DI
wvm_nfrm:   resw 1              ; frames in use; 0 = no handler is running
wvm_cbase:  resw 1              ; the top frame's, unpacked for LDL/STL
wvm_cnloc:  resw 1
wvm_cfn:    resw 1              ; ...and for 10.6's sentence

wvm_stop:   resw 1              ; the answer wvm_slice is about to give
wvm_errc:   resw 1
wvm_erra:   resw 1
wvm_errb:   resw 1
wvm_errfn:  resw 1

wvm_stop_ct: resw 1             ; the string arena's top
wvm_stop_at: resw 1             ; ...and the array arena's
wvm_send:   resw 1              ; one past the string arena
wvm_abase:  resw 1              ; the array arena's base
wvm_aend:   resw 1

wvm_argn:   resw 1              ; wvm_argi's staging, for wvm_begin
wvm_argv:   resw 16             ; 8 cells

; the native call's request, staged before the segments move
wvm_nkind:  resw 1
wvm_ncomp:  resw 1
wvm_natomv: resw 1
wvm_nargc:  resw 1
wvm_npop:   resw 1              ; cells to drop off the stack on success
wvm_npush:  resw 1              ; 1 = the answer is pushed (SETP pushes none)
wvm_nback:  resw 1              ; bytes to rewind SI by on a GC answer

wvm_bcx:    resw 1              ; the budget, across a call that loses CX
wvm_besv:   resw 1              ; the caller's ES, for wvm_native

; the CALL handler's unpacked function-table row
wvm_tofs:   resw 1
wvm_tna:    resw 1
wvm_tnl:    resw 1
wvm_tfn:    resw 1
wvm_tbase:  resw 1
wvm_tval:   resw 2              ; RET's value, across the frame pop

; two string descriptors: segment, offset of the FIRST CHARACTER, length
wvm_sa:     resw 3
wvm_sb:     resw 3
wvm_num:    resb 10             ; str()'s digits, in OUR segment

section .text

; =============================================================================
; SMALL PRIMITIVES.  Every one preserves CX (the slice budget).
; =============================================================================

; wvm_pushv - push the cell AX:DX.  DI advances.  Overflow is 4.7's `too deep.`
wvm_pushv:
    cmp di, WV_STKEND
    jae .deep
    mov [di], ax
    mov [di+2], dx
    add di, 4
    ret
.deep:
    mov al, WE_DEEP
    jmp wvm_err

; wvm_popv - pop into AX:DX.
wvm_popv:
    sub di, 4
    mov ax, [di]
    mov dx, [di+2]
    ret

; -----------------------------------------------------------------------------
; wvm_truth - is the cell AX:DX true (WEAVE-SPEC 4.4)?
; out: ZF=1 false, ZF=0 true.  AX, DX, CX preserved.
;
; false: int 0, bool 0, the empty string, null.  True: everything else -
; arrays, components (component 0, the app, INCLUDED), a non-empty string.
; -----------------------------------------------------------------------------
wvm_truth:
    push ax
    push bx                     ; BX AS WELL, and it is not defensive: wvm_jz
    push dx                     ; and wvm_jnz hold their displacement in BX
                                ; across this call, and wvm_sdesc below eats it
    cmp ax, WT_NULL
    je .false
    cmp ax, WT_STR
    je .str
    cmp ax, WT_INT
    je .num
    cmp ax, WT_BOOL
    je .num
    mov ax, 1                   ; arrays and components: always true
    jmp short .test
.num:
    mov ax, dx
    jmp short .test
.false:
    xor ax, ax
    jmp short .test
.str:
    call wvm_sdesc
    mov ax, [cs:wvm_sa+4]       ; the length; 0 is the empty string
.test:
    or ax, ax
    pop dx
    pop bx
    pop ax
    ret

; wvm_hent - DS-relative address of handle DX's table entry, in BX.
wvm_hent:
    mov bx, dx
    and bx, 0xFF
    shl bx, 1
    shl bx, 1
    add bx, WV_HTAB
    ret

; -----------------------------------------------------------------------------
; wvm_sdesc - describe the string whose handle is DX, into wvm_sa.
; AX, DX, CX preserved; BX lost.
;
; The two kinds are the point (WEAVE-SPEC 4.8): a STATIC handle names bytes in
; the pinned bundle claim and is never copied or collected, a dynamic one names
; an arena object in the VM claim. Both are `length byte then bytes`, so only
; the segment and the base differ - which is why every string routine below
; takes a descriptor and not a handle.
; -----------------------------------------------------------------------------
wvm_sdesc:
    push ax
    push dx
    call wvm_hent
    mov ax, [bx+HT_OFF]
    cmp byte [bx+HT_TYPE], HTY_SSTR
    je .static
    add ax, AO_HDR              ; dynamic: past the object header
    mov dx, [cs:wvm_vmseg]
    jmp short .have
.static:
    mov dx, [cs:wvm_bseg]
.have:
    mov [cs:wvm_sa], dx
    mov bx, ax
    push ds
    mov ds, dx                  ; cc8086:allow whichever claim holds the bytes,
    mov dl, [bx]                ; for one byte; restored on the next line
    pop ds
    xor dh, dh
    inc bx
    mov [cs:wvm_sa+2], bx
    mov [cs:wvm_sa+4], dx
    pop dx
    pop ax
    ret

; wvm_atob - copy wvm_sa into wvm_sb (the second operand of a pair).
wvm_atob:
    push ax
    mov ax, [cs:wvm_sa]
    mov [cs:wvm_sb], ax
    mov ax, [cs:wvm_sa+2]
    mov [cs:wvm_sb+2], ax
    mov ax, [cs:wvm_sa+4]
    mov [cs:wvm_sb+4], ax
    pop ax
    ret

; wvm_nosb - wvm_sb is empty (a one-piece wvm_mkstr).
wvm_nosb:
    mov word [cs:wvm_sb], 0
    mov word [cs:wvm_sb+2], 0
    mov word [cs:wvm_sb+4], 0
    ret

; =============================================================================
; THE ARENAS AND THE HANDLE TABLE (WEAVE-SPEC 4.8)
; =============================================================================

; wvm_hnew - claim a free handle.  out: AX = the index, 0 = none free.
; HANDLE 0 IS NEVER ALLOCATED, so 0 means "none" in every holder - which is
; what lets the runtime keep a component's string slot in a zeroed .bss.
wvm_hnew:
    push bx
    push dx
    mov bx, WV_HTAB + 4
    mov dx, 1
.scan:
    cmp byte [bx+HT_TYPE], HTY_FREE
    je .got
    inc dx
    add bx, 4
    cmp dx, WV_NHAND
    jb .scan
    xor dx, dx
.got:
    mov ax, dx
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; wvm_alloc - allocate an arena object.
; in:  AX = payload bytes, BL = HTY_STR or HTY_ARR
; out: CF=0 AX = the handle, BX = the DS offset of the PAYLOAD
;      CF=1 it does not fit, or no handle is free: the GC-request byte is set
;           and the caller must end the slice (WEAVE-SPEC 4.8)
; CX preserved.
; -----------------------------------------------------------------------------
wvm_alloc:
    push cx
    push dx
    mov dl, bl                  ; DL = the type
    add ax, AO_HDR + 1
    and ax, ~1                  ; the total, rounded even
    mov cx, ax
    cmp dl, HTY_ARR
    je .arr
    mov bx, [cs:wvm_stop_ct]
    mov ax, bx
    add ax, cx
    jc .full
    cmp ax, [cs:wvm_send]
    ja .full
    mov [cs:wvm_stop_ct], ax
    jmp short .have
.arr:
    mov bx, [cs:wvm_stop_at]
    mov ax, bx
    add ax, cx
    jc .full
    cmp ax, [cs:wvm_aend]
    ja .full
    mov [cs:wvm_stop_at], ax
.have:
    push bx                     ; the object's base
    call wvm_hnew
    pop bx
    or ax, ax
    jz .full                    ; the table is full: the same refusal, and the
                                ; collector is what frees an entry
    mov [bx+AO_SIZE], cx
    mov [bx+AO_HAND], al
    mov byte [bx+AO_FLAG], 0
    mov ch, dl                  ; bank the TYPE in CH, not in DH: wvm_hent
                                ; takes the handle in DX, and `mov dx, ax`
                                ; below would eat a copy parked there
    push ax                     ; the handle
    push bx                     ; the object's base
    mov dx, ax
    call wvm_hent               ; BX = the table entry
    pop ax                      ; the base
    mov [bx+HT_OFF], ax
    mov [bx+HT_TYPE], ch
    mov byte [bx+HT_FLAG], 0
    mov bx, ax
    add bx, AO_HDR
    pop ax                      ; the handle
    clc
    jmp short .out
.full:
    push bx
    mov bx, [cs:wvm_hs]
    mov byte [bx+HS_GCREQ], 1
    pop bx
    stc
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; wvm_mkstr - a new string object from wvm_sa, or from wvm_sa + wvm_sb.
; out: CF=0 AX = the handle
;      CF=1 AX = 0 the caller must end the slice for a collection
;           AX = 1 over 255 characters: 4.4's `out of string space.`
; CX preserved; DI untouched.
; -----------------------------------------------------------------------------
wvm_mkstr:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, [cs:wvm_sa+4]
    add ax, [cs:wvm_sb+4]
    cmp ax, 256
    jae .toolong
    mov dx, ax                  ; DX = the character count
    inc ax                      ; ...plus the length byte
    mov bl, HTY_STR
    call wvm_alloc
    jc .gc
    mov [bx], dl                ; the length byte
    inc bx
    mov [cs:wvm_tval], ax       ; bank the handle: the copies lose AX
    mov di, bx
    push ds
    push ds
    pop es                      ; cc8086:allow ES = the claim, the DESTINATION
    mov si, [cs:wvm_sa+2]
    mov cx, [cs:wvm_sa+4]
    mov ds, [cs:wvm_sa]         ; ...and DS = wherever the source is
    cld
    rep movsb
    pop ds
    push ds
    push ds
    pop es
    mov si, [cs:wvm_sb+2]
    mov cx, [cs:wvm_sb+4]
    mov ds, [cs:wvm_sb]         ; cc8086:allow the second piece; nothing at all
    cld                         ; when its length is 0, which is the one-piece
    rep movsb                   ; case (wvm_nosb)
    pop ds
    mov ax, [cs:wvm_tval]
    clc
    jmp short .out
.gc:
    xor ax, ax
    stc
    jmp short .out
.toolong:
    mov ax, 1
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; THE COLLECTOR (WEAVE-SPEC 4.8) - between slices only, never under the lock
; =============================================================================

; wvm_mark1 - mark the cell AX:DX if it holds a collectable handle.
; DS is the claim.  AX, CX, DX, SI, DI preserved.
wvm_mark1:
    push bx
    cmp ax, WT_STR
    je .h
    cmp ax, WT_ARR
    jne .out
.h:
    call wvm_hent
    cmp byte [bx+HT_TYPE], HTY_SSTR
    je .out                     ; static: nothing to keep alive
    cmp byte [bx+HT_TYPE], HTY_FREE
    je .out
    or byte [bx+HT_FLAG], 1
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; void wvm_mark(int tag, int handle) - mark one of the includer's OWN roots
; (WEAVE-SPEC 4.8.1: a component's text slot, a list's item override).
; Called between slices, before wvm_gc.  Marking them from in here would mean
; naming the runtime, which the header forbids.
; -----------------------------------------------------------------------------
_wvm_mark:
    push bp
    mov bp, sp
    push bx
    push dx
    push ds
    mov ax, [cs:wvm_vmseg]
    or ax, ax
    jz .out
    mov ds, ax                  ; cc8086:allow the claim, restored below
    mov ax, [bp+4]
    mov dx, [bp+6]
    call wvm_mark1
.out:
    pop ds
    pop dx
    pop bx
    xor ax, ax
    pop bp
    ret

; -----------------------------------------------------------------------------
; void wvm_gc(void) - mark, sweep, compact.
; -----------------------------------------------------------------------------
_wvm_gc:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ax, [cs:wvm_vmseg]
    or ax, ax
    jz .out
    mov ds, ax                  ; cc8086:allow the claim, for the whole
                                ; collection; restored at the exit
    mov bx, WV_GLOB             ; --- roots 1: the 128 globals
    mov cx, WV_NGLOB
.mg:
    mov ax, [bx]
    mov dx, [bx+2]
    call wvm_mark1
    add bx, 4
    loop .mg
    mov bx, WV_STK              ; --- roots 2: the live eval-stack cells
    mov si, [cs:wvm_sp]
.ms:
    cmp bx, si
    jae .sweep
    mov ax, [bx]
    mov dx, [bx+2]
    call wvm_mark1
    add bx, 4
    jmp short .ms
.sweep:                         ; --- every unmarked non-static handle is freed
    mov bx, WV_HTAB + 4
    mov cx, WV_NHAND - 1
.sw:
    mov al, [bx+HT_TYPE]
    cmp al, HTY_SSTR
    je .swnext                  ; static: never collected, never compacted
    cmp al, HTY_FREE
    je .swnext
    test byte [bx+HT_FLAG], 1
    jnz .swclear
    mov byte [bx+HT_TYPE], HTY_FREE
    mov word [bx+HT_OFF], 0xFFFF
.swclear:
    and byte [bx+HT_FLAG], ~1
.swnext:
    add bx, 4
    loop .sw
    mov si, WV_ARENA            ; --- compact, one arena then the other
    mov di, WV_ARENA
    mov dx, [cs:wvm_stop_ct]
    call wvm_slide
    mov [cs:wvm_stop_ct], di
    mov si, [cs:wvm_abase]
    mov di, si
    mov dx, [cs:wvm_stop_at]
    call wvm_slide
    mov [cs:wvm_stop_at], di
    mov bx, [cs:wvm_hs]
    mov byte [bx+HS_GCREQ], 0
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; wvm_slide - compact one arena. Live objects move down in address order and
; ONLY the handle table is rewritten (WEAVE-SPEC 4.8: no other value in the
; system holds an arena address, by construction).
; in:  SI = the arena base, DI = where the first survivor goes, DX = the top
; out: DI = the new top.  DS is the claim.
wvm_slide:
    push ax
    push bx
    push cx
    push es
    push ds
    pop es                      ; cc8086:allow ES = DS for an intra-claim move;
    cld                         ; both restored at the exit
.next:
    cmp si, dx
    jae .done
    mov cx, [si+AO_SIZE]
    or cx, cx
    jz .done                    ; a zero size cannot advance: stop, rather than
                                ; loop for ever on a claim that got corrupted
    mov al, [si+AO_HAND]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    shl bx, 1
    add bx, WV_HTAB
    cmp byte [bx+HT_TYPE], HTY_FREE
    je .dead
    mov [bx+HT_OFF], di         ; the one rewrite
    cmp si, di
    je .keep                    ; already in place: no move at all
    push si
    push di
    rep movsb
    pop di
    pop si
    mov cx, [si+AO_SIZE]
.keep:
    add di, cx
    add si, cx
    jmp short .next
.dead:
    add si, cx
    jmp short .next
.done:
    pop es
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE ATOM POOL (WEAVE-SPEC 2.7) - the only thing here that reads the bundle
; besides bytecode.
; =============================================================================

; wvm_atomoff - the bundle-claim offset of atom AL's LENGTH BYTE.
; out: CF=0 BX = the offset; CF=1 not an atom this bundle can name.
; Well-known ids 1..63 have no string in the pool (2.7) and refuse here -
; which is why every PUSHA operand a packer emits is 64..250.
wvm_atomoff:
    push ax
    push dx
    xor ah, ah
    sub ax, 64
    jb .no
    cmp ax, [cs:wvm_natom]
    jae .no
    shl ax, 1
    add ax, 2
    add ax, [cs:wvm_atoms]
    mov bx, ax
    push es
    mov es, [cs:wvm_bseg]       ; cc8086:allow the bundle, for one word
    mov bx, [es:bx]
    pop es
    add bx, [cs:wvm_atoms]
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; wvm_atomh - the STATIC handle for atom AL, bound on first use (4.8).
; out: CF=0 AX = the handle; CF=1 a bad atom, or the table is full.
wvm_atomh:
    push bx
    push cx
    push dx
    call wvm_atomoff
    jc .no
    mov dx, bx                  ; DX = the pool offset we are looking for
    mov bx, WV_HTAB + 4
    mov cx, 1
.scan:
    cmp byte [bx+HT_TYPE], HTY_SSTR
    jne .next
    cmp [bx+HT_OFF], dx
    je .found
.next:
    inc cx
    add bx, 4
    cmp cx, WV_NHAND
    jb .scan
    call wvm_hnew               ; not bound yet
    or ax, ax
    jz .no
    mov cx, ax
    push dx
    mov dx, ax
    call wvm_hent
    pop dx
    mov [bx+HT_OFF], dx
    mov byte [bx+HT_TYPE], HTY_SSTR
    mov byte [bx+HT_FLAG], 0
.found:
    mov ax, cx
    pop dx
    pop cx
    pop bx
    clc
    ret
.no:
    pop dx
    pop cx
    pop bx
    stc
    ret

; =============================================================================
; C-CALLABLE SET-UP AND READOUT
; =============================================================================

; -----------------------------------------------------------------------------
; void wvm_bind(unsigned vmseg, unsigned vmbytes, unsigned bseg,
;               unsigned codeoff, unsigned codelen, unsigned nfunc,
;               unsigned atomoff, unsigned natoms, void *nblk, unsigned seed)
;
; Lay the claim out (WEAVE-SPEC 4.7) and forget everything the last bundle
; left. The wipe covers globals, handles, stacks, frames, ring and the FX
; stack - 0x880 bytes - and NOT the arenas, which are 15 to 30KB the user is
; waiting through and which nothing reads before it is written.
; -----------------------------------------------------------------------------
_wvm_bind:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    push ds
    pop ax
    mov [cs:wvm_ourds], ax
    mov ax, [bp+4]
    mov [cs:wvm_vmseg], ax
    mov ax, [bp+8]
    mov [cs:wvm_bseg], ax
    mov ax, [bp+10]
    mov [cs:wvm_code], ax
    mov ax, [bp+12]
    mov [cs:wvm_clen], ax
    mov ax, [bp+14]
    mov [cs:wvm_nfunc], ax
    shl ax, 1
    shl ax, 1
    add ax, 2
    add ax, [cs:wvm_code]
    mov [cs:wvm_body], ax       ; the first byte a jump may land on (4.5.1)
    mov ax, [bp+16]
    mov [cs:wvm_atoms], ax
    mov ax, [bp+18]
    mov [cs:wvm_natom], ax
    mov ax, [bp+20]
    mov [cs:wvm_nblk], ax

    mov ax, [bp+6]              ; the claim's size in BYTES
    sub ax, WV_TAIL
    mov [cs:wvm_send], ax       ; one past the string arena
    mov [cs:wvm_abase], ax
    add ax, WV_ARRSZ
    mov [cs:wvm_aend], ax
    mov [cs:wvm_hs], ax         ; ...and the scratch is one past the array
                                ; arena, which is S-16 exactly

    mov ax, [cs:wvm_vmseg]
    mov ds, ax                  ; cc8086:allow the claim, for the wipe below;
    mov es, ax                  ; both restored at the exit
    cld
    xor ax, ax
    xor di, di
    mov cx, WV_ARENA >> 1
    rep stosw
    mov di, WV_HTAB             ; ...then every handle to `free`: offset 0xFFFF
    mov cx, WV_NHAND            ; over a type that the wipe already made 0
    mov ax, 0xFFFF
.freeh:
    mov [di], ax
    add di, 4
    loop .freeh
    mov bx, [cs:wvm_hs]         ; the hot scratch (4.7.2)
    xor ax, ax
    mov [bx+HS_BUDGET], ax
    mov [bx+HS_LEFT], ax
    mov [bx+HS_RHEAD], ax       ; head and count in one store
    mov [bx+HS_GCREQ], ax       ; ...gcreq and state likewise
    mov ax, [bp+22]
    mov [bx+HS_SEED], ax

    mov word [cs:wvm_stop_ct], WV_ARENA
    mov ax, [cs:wvm_abase]
    mov [cs:wvm_stop_at], ax
    mov word [cs:wvm_nfrm], 0
    mov word [cs:wvm_sp], WV_STK
    mov word [cs:wvm_pc], 0
    mov word [cs:wvm_argn], 0
    mov word [cs:wvm_errc], 0
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; void wvm_unbind(void) - forget the claim, so a freed segment is never read.
_wvm_unbind:
    mov word [cs:wvm_vmseg], 0
    mov word [cs:wvm_nfrm], 0
    ret

; void wvm_argi(int v) - stage one int argument for the next wvm_begin.
_wvm_argi:
    push bp
    mov bp, sp
    push bx
    mov bx, [cs:wvm_argn]
    cmp bx, 8
    jae .out
    shl bx, 1
    shl bx, 1
    mov word [cs:wvm_argv+bx], WT_INT
    mov ax, [bp+4]
    mov [cs:wvm_argv+bx+2], ax
    inc word [cs:wvm_argn]
.out:
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; int wvm_begin(int fn) - start a handler with the staged arguments.
; out: 1 it is running, 0 there is no such function index.
;
; WEAVE-SPEC 4.9.1's pad-and-truncate happens here: missing arguments become
; int 0 and extra ones are dropped, to the FUNCTION's own nargs - so a
; `function onKey(ch)` written against a two-word event is legal.
; -----------------------------------------------------------------------------
_wvm_begin:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ax, [cs:wvm_vmseg]
    or ax, ax
    jz .no
    mov ax, [bp+4]
    cmp ax, [cs:wvm_nfunc]
    jae .no
    mov [cs:wvm_tfn], ax
    mov es, [cs:wvm_bseg]       ; cc8086:allow the bundle, for the table row
    shl ax, 1
    shl ax, 1
    add ax, [cs:wvm_code]
    add ax, 2
    mov bx, ax
    mov ax, [es:bx]
    add ax, [cs:wvm_code]
    mov [cs:wvm_tofs], ax
    mov al, [es:bx+2]
    xor ah, ah
    mov [cs:wvm_tna], ax
    mov al, [es:bx+3]
    xor ah, ah
    mov [cs:wvm_tnl], ax
    mov ds, [cs:wvm_vmseg]      ; cc8086:allow the claim, for the frame set-up

    mov di, WV_STK
    xor si, si
    mov cx, [cs:wvm_tna]
.args:
    jcxz .fill
    mov ax, WT_INT
    xor dx, dx
    cmp si, [cs:wvm_argn]
    jae .pad
    mov bx, si
    shl bx, 1
    shl bx, 1
    mov ax, [cs:wvm_argv+bx]
    mov dx, [cs:wvm_argv+bx+2]
.pad:
    mov [di], ax
    mov [di+2], dx
    add di, 4
    inc si
    dec cx
    jmp short .args
.fill:
    mov cx, [cs:wvm_tnl]
    sub cx, [cs:wvm_tna]
    jbe .done
    mov ax, WT_NULL             ; the non-argument locals are NULL (4.7.1),
    xor dx, dx                  ; which is tag 4 and not a zeroed cell
.fl:
    mov [di], ax
    mov [di+2], dx
    add di, 4
    loop .fl
.done:
    mov [cs:wvm_sp], di
    mov ax, [cs:wvm_tofs]
    mov [cs:wvm_pc], ax
    mov word [cs:wvm_nfrm], 1
    mov bx, WV_FRM
    mov word [bx+FR_RET], 0
    mov word [bx+FR_BASE], WV_STK
    mov ax, [cs:wvm_tfn]
    mov [bx+FR_FN], al
    mov [cs:wvm_cfn], ax
    mov ax, [cs:wvm_tnl]
    mov [bx+FR_NLOC], al
    mov [cs:wvm_cnloc], ax
    mov word [cs:wvm_cbase], WV_STK
    mov bx, [cs:wvm_hs]
    mov byte [bx+HS_STATE], 1
    mov word [cs:wvm_argn], 0
    mov ax, 1
    jmp short .out
.no:
    mov word [cs:wvm_argn], 0
    xor ax, ax
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; int wvm_busy(void) - is a handler part-run?
_wvm_busy:
    mov ax, [cs:wvm_nfrm]
    or ax, ax
    jz .no
    mov ax, 1
.no:
    ret

; void wvm_abort(void) - WEAVE-SPEC 4.11's Stop: the stacks are cleared, the
; ring is preserved and the globals stand as they are.
_wvm_abort:
    push bx
    push ds
    mov word [cs:wvm_nfrm], 0
    mov word [cs:wvm_sp], WV_STK
    mov ax, [cs:wvm_vmseg]
    or ax, ax
    jz .out
    mov ds, ax                  ; cc8086:allow the claim, restored below
    mov bx, [cs:wvm_hs]
    mov byte [bx+HS_STATE], 0
.out:
    pop ds
    pop bx
    ret

; The error readouts.  The runtime turns the code into 10.6.1's sentence.
_wvm_errcode:
    mov ax, [cs:wvm_errc]
    ret
_wvm_errfn:
    mov ax, [cs:wvm_errfn]
    ret
_wvm_erra:
    mov ax, [cs:wvm_erra]
    ret
_wvm_errb:
    mov ax, [cs:wvm_errb]
    ret

; int wvm_gcreq(void) - did an allocation ask for a collection?
_wvm_gcreq:
    push bx
    push ds
    xor ax, ax
    mov bx, [cs:wvm_vmseg]
    or bx, bx
    jz .out
    mov ds, bx                  ; cc8086:allow the claim, restored below
    mov bx, [cs:wvm_hs]
    mov al, [bx+HS_GCREQ]
    xor ah, ah
.out:
    pop ds
    pop bx
    ret

; =============================================================================
; THE SLICE (WEAVE-SPEC 4.10) AND THE DISPATCH LOOP (4.12)
; =============================================================================

; int wvm_slice(int budget) - run up to `budget` ops.
_wvm_slice:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov word [cs:wvm_stop], WR_IDLE
    cmp word [cs:wvm_nfrm], 0
    je .out
    mov cx, [bp+4]
    or cx, cx
    jz .out
    push es
    pop ax
    mov [cs:wvm_besv], ax       ; the caller's ES, for wvm_native
    mov ds, [cs:wvm_vmseg]      ; cc8086:allow THE SLICE'S SEGMENTS, which the
    mov es, [cs:wvm_bseg]       ; header sets out; both restored at .out
    cld
    mov si, [cs:wvm_pc]
    mov di, [cs:wvm_sp]
    mov word [cs:wvm_stop], WR_MORE
    jmp short wvm_next
.out:
    mov ax, [cs:wvm_stop]
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; every handler comes back here
wvm_next:
    jcxz wvm_x_budget
    dec cx
    xor bh, bh
    mov bl, [es:si]
    inc si
    cmp bl, 38                  ; 4.5.1: the opcode's own bound
    RAE wvm_badop
    shl bx, 1
    jmp [cs:bx+wvm_tab]

; --- the four ways out -------------------------------------------------------
wvm_x_budget:
    mov word [cs:wvm_stop], WR_MORE
    jmp short wvm_exit
wvm_x_done:
    mov word [cs:wvm_nfrm], 0
    mov di, WV_STK              ; HALT and the outermost RET clear the stack
    mov word [cs:wvm_stop], WR_DONE
    mov bx, [cs:wvm_hs]
    mov byte [bx+HS_STATE], 0
    jmp short wvm_exit
wvm_x_gc:
    mov word [cs:wvm_stop], WR_GC
    mov bx, [cs:wvm_hs]
    mov byte [bx+HS_GCREQ], 1
    jmp short wvm_exit

; wvm_err - stop the handler with code AL (10.6): stacks cleared, ring kept,
; globals as they are, the app lives on.
wvm_err:
    xor ah, ah
    mov [cs:wvm_errc], ax
wvm_err2:                       ; ...and this entry for a code the runtime set
    mov ax, [cs:wvm_cfn]
    mov [cs:wvm_errfn], ax
    mov word [cs:wvm_nfrm], 0
    mov di, WV_STK
    mov word [cs:wvm_stop], WR_ERR
    mov bx, [cs:wvm_hs]
    mov byte [bx+HS_STATE], 0
    ; fall through

wvm_exit:
    mov [cs:wvm_pc], si
    mov [cs:wvm_sp], di
    jmp _wvm_slice.out

; 4.5.1's refusals, reached from every operand bound
wvm_badop:
    mov al, WE_OPCODE
    jmp wvm_err
wvm_badbi:
    mov al, WE_BUILT
    jmp wvm_err
wvm_type:
    mov al, WE_TYPE
    jmp wvm_err

; =============================================================================
; THE 38 HANDLERS.  Entered with SI past the opcode byte, CX = the budget.
; =============================================================================

wvm_halt:
    jmp wvm_x_done

wvm_pushi:
    mov dx, [es:si]
    inc si
    inc si
    mov ax, WT_INT
    call wvm_pushv
    jmp wvm_next

wvm_pusha:
    mov al, [es:si]
    inc si
    call wvm_atomh
    RC wvm_badop                ; 4.5.1: an atom this bundle cannot name
    mov dx, ax
    mov ax, WT_STR
    call wvm_pushv
    jmp wvm_next

wvm_pushn:
    mov ax, WT_NULL
    xor dx, dx
    call wvm_pushv
    jmp wvm_next

wvm_pushb:
    mov dl, [es:si]
    xor dh, dh
    inc si
    mov ax, WT_BOOL
    call wvm_pushv
    jmp wvm_next

wvm_pushc:
    mov dl, [es:si]
    xor dh, dh
    inc si
    mov ax, WT_COMP
    call wvm_pushv
    jmp wvm_next

wvm_ldg:
    mov bl, [es:si]
    xor bh, bh
    inc si
    cmp bx, WV_NGLOB
    RAE wvm_badop
    shl bx, 1
    shl bx, 1
    mov ax, [bx+WV_GLOB]
    mov dx, [bx+WV_GLOB+2]
    call wvm_pushv
    jmp wvm_next

wvm_stg:
    mov bl, [es:si]
    xor bh, bh
    inc si
    cmp bx, WV_NGLOB
    RAE wvm_badop
    shl bx, 1
    shl bx, 1
    add bx, WV_GLOB
    call wvm_popv
    mov [bx], ax
    mov [bx+2], dx
    jmp wvm_next

wvm_ldl:
    mov bl, [es:si]
    xor bh, bh
    inc si
    cmp bx, [cs:wvm_cnloc]
    RAE wvm_badop
    shl bx, 1
    shl bx, 1
    add bx, [cs:wvm_cbase]
    mov ax, [bx]
    mov dx, [bx+2]
    call wvm_pushv
    jmp wvm_next

wvm_stl:
    mov bl, [es:si]
    xor bh, bh
    inc si
    cmp bx, [cs:wvm_cnloc]
    RAE wvm_badop
    shl bx, 1
    shl bx, 1
    add bx, [cs:wvm_cbase]
    call wvm_popv
    mov [bx], ax
    mov [bx+2], dx
    jmp wvm_next

wvm_pop:
    sub di, 4
    jmp wvm_next

wvm_dup:
    mov bx, di
    mov ax, [bx-4]
    mov dx, [bx-2]
    call wvm_pushv
    jmp wvm_next

; --- ADD: int+int, or str+str (WEAVE-SPEC 4.4) -------------------------------
; The one allocating arithmetic op, so it PEEKS its operands and pops only when
; the answer exists: a GC request has to leave the stack as it found it and
; rewind SI to the opcode byte (4.8).
wvm_add:
    mov bx, di
    cmp word [bx-8], WT_STR
    jne .ints
    cmp word [bx-4], WT_STR
    RNE wvm_type
    mov dx, [bx-2]              ; the RIGHT string first: wvm_sb takes a copy
    call wvm_sdesc              ; of wvm_sa, so the second descriptor built is
    call wvm_atob               ; the one that stays in wvm_sa
    mov bx, di
    mov dx, [bx-6]
    call wvm_sdesc
    call wvm_mkstr
    jc .no
    sub di, 8
    mov dx, ax
    mov ax, WT_STR
    call wvm_pushv
    jmp wvm_next
.no:
    or ax, ax
    jnz .long
    dec si                      ; back to our own opcode byte
    jmp wvm_x_gc
.long:
    mov al, WE_STRSP
    jmp wvm_err
.ints:
    call wvm_i2
    add ax, dx
    jmp wvm_ipush

; wvm_i2 - pop two cells that must both be ints.  out: AX = a, DX = b.
wvm_i2:
    mov bx, di
    cmp word [bx-8], WT_INT
    RNE wvm_type
    cmp word [bx-4], WT_INT
    RNE wvm_type
    mov ax, [bx-6]
    mov dx, [bx-2]
    sub di, 8
    ret

; wvm_ipush - push AX as an int.  The 16-bit wrap is the machine's own
; (WEAVE-SPEC 4.4: defined behaviour, not an error).
wvm_ipush:
    mov dx, ax
    mov ax, WT_INT
    call wvm_pushv
    jmp wvm_next

wvm_sub:
    call wvm_i2
    sub ax, dx
    jmp wvm_ipush

wvm_mul:
    call wvm_i2
    imul dx                     ; DX:AX; we keep AX, which IS the wrap
    jmp wvm_ipush

; DIV and MOD share two guards. A zero divisor is 10.6's `divide by zero.`;
; -32768 / -1 is WEAVE-SPEC 4.4's NAMED case - `idiv` raises INT 0 there, with
; no handler in a package, so the machine would hang where the rule says the
; answer is -32768.
wvm_div:
    call wvm_i2
    or dx, dx
    RE wvm_div0
    cmp ax, 0x8000
    jne .ok
    cmp dx, 0xFFFF
    jne .ok
    mov ax, 0x8000              ; wrap16(32768)
    jmp wvm_ipush
.ok:
    mov bx, dx
    cwd
    idiv bx
    jmp wvm_ipush
wvm_div0:
    mov al, WE_DIV0
    jmp wvm_err

wvm_mod:
    call wvm_i2
    or dx, dx
    RE wvm_div0
    cmp ax, 0x8000
    jne .ok
    cmp dx, 0xFFFF
    jne .ok
    xor ax, ax                  ; -32768 % -1 is 0, and idiv would trap
    jmp wvm_ipush
.ok:
    mov bx, dx
    cwd
    idiv bx
    mov ax, dx                  ; the remainder carries the DIVIDEND's sign,
    jmp wvm_ipush               ; which is idiv's own rule and 4.4's

wvm_neg:
    mov bx, di
    cmp word [bx-4], WT_INT
    RNE wvm_type
    mov ax, [bx-2]
    sub di, 4
    neg ax
    jmp wvm_ipush

; --- EQ / NE: tag AND payload; strings by contents, arrays by handle --------
wvm_eq:
    call wvm_cmpeq
    jmp wvm_bpush
wvm_ne:
    call wvm_cmpeq
    xor al, 1
    jmp wvm_bpush

; wvm_bpush - push AL (0 or 1) as a bool.
wvm_bpush:
    mov dl, al
    xor dh, dh
    mov ax, WT_BOOL
    call wvm_pushv
    jmp wvm_next

; wvm_cmpeq - pop two cells; AL = 1 when equal.  NEVER a type error: any pair
; is comparable (4.4), and two different tags are simply unequal - so
; `0 == false` is FALSE, tag 0 against tag 5.
wvm_cmpeq:
    mov bx, di
    mov ax, [bx-8]
    cmp ax, [bx-4]
    jne .no
    cmp ax, WT_STR
    je .str
    mov ax, [bx-6]
    cmp ax, [bx-2]
    jne .no
    sub di, 8
    mov al, 1
    ret
.no:
    sub di, 8
    xor al, al
    ret
.str:
    mov dx, [bx-6]
    call wvm_sdesc
    call wvm_atob
    mov bx, di
    mov dx, [bx-2]
    call wvm_sdesc              ; wvm_sa = the RIGHT one, wvm_sb = the left
    call wvm_scmp               ; ...so the answer's sign is inverted, which
    sub di, 8                   ; equality does not care about
    or al, al
    jnz .no2
    mov al, 1
    ret
.no2:
    xor al, al
    ret

; --- LT / LE / GT / GE: two ints, or two strings ----------------------------
wvm_lt:
    call wvm_cmpord
    jl wvm_true
    jmp wvm_false
wvm_le:
    call wvm_cmpord
    jle wvm_true
    jmp wvm_false
wvm_gt:
    call wvm_cmpord
    jg wvm_true
    jmp wvm_false
wvm_ge:
    call wvm_cmpord
    jge wvm_true
    jmp wvm_false
wvm_true:
    mov al, 1
    jmp wvm_bpush
wvm_false:
    xor al, al
    jmp wvm_bpush

; wvm_cmpord - pop two comparable cells and leave the SIGNED flags of a - b.
; Two ints or two strings; anything else is `type mismatch.`
wvm_cmpord:
    mov bx, di
    mov ax, [bx-8]
    cmp ax, [bx-4]
    RNE wvm_type
    cmp ax, WT_INT
    je .ints
    cmp ax, WT_STR
    RNE wvm_type
    mov dx, [bx-2]              ; the right one into wvm_sb, the left into
    call wvm_sdesc              ; wvm_sa, so wvm_scmp's sign is a - b
    call wvm_atob
    mov bx, di
    mov dx, [bx-6]
    call wvm_sdesc
    call wvm_scmp
    sub di, 8
    cbw
    or ax, ax
    ret
.ints:
    mov ax, [bx-6]
    mov dx, [bx-2]
    sub di, 8
    cmp ax, dx
    ret

; wvm_scmp - compare wvm_sa with wvm_sb.  out: AL = -1 a<b, 0 equal, 1 a>b.
; Bytewise UNSIGNED over the common prefix, then the shorter is less - which
; is what "bytewise order" means and is NOT what a signed `char` compare would
; give (docs/C-TOOLCHAIN.md's own trap, said about assembly).  CX preserved.
wvm_scmp:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov si, [cs:wvm_sa+2]
    mov di, [cs:wvm_sb+2]
    mov cx, [cs:wvm_sa+4]
    mov dx, [cs:wvm_sb+4]
    mov ds, [cs:wvm_sa]         ; cc8086:allow both strings' segments, for the
    mov es, [cs:wvm_sb]         ; compare; both restored at the exit
    cmp cx, dx
    jbe .have
    mov cx, dx                  ; the common prefix
.have:
    jcxz .prefix
    cld
    repe cmpsb
    je .prefix
    mov al, [si-1]
    mov ah, [es:di-1]
    cmp al, ah
    ja .gt
    mov al, -1
    jmp short .out
.gt:
    mov al, 1
    jmp short .out
.prefix:
    mov cx, [cs:wvm_sa+4]
    mov dx, [cs:wvm_sb+4]
    xor al, al
    cmp cx, dx
    je .out
    ja .gt
    mov al, -1
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

wvm_not:
    call wvm_popv
    call wvm_truth
    jz .t
    xor al, al
    jmp wvm_bpush
.t:
    mov al, 1
    jmp wvm_bpush

; --- the jumps.  4.5.1 bounds every target inside the CODE body -------------
wvm_jmp:
    mov ax, [es:si]
    inc si
    inc si
    add si, ax
    jmp wvm_jchk
wvm_jz:
    mov bx, [es:si]
    inc si
    inc si
    call wvm_popv
    call wvm_truth
    jnz wvm_next
    add si, bx
    jmp wvm_jchk
wvm_jnz:
    mov bx, [es:si]
    inc si
    inc si
    call wvm_popv
    call wvm_truth
    jz wvm_next
    add si, bx
    jmp wvm_jchk
wvm_jchk:
    cmp si, [cs:wvm_body]
    jb wvm_badop
    mov ax, [cs:wvm_code]
    add ax, [cs:wvm_clen]
    cmp si, ax
    RAE wvm_badop
    jmp wvm_next

; --- CALL / RET (WEAVE-SPEC 4.7.1) ------------------------------------------
wvm_call:
    mov bl, [es:si]
    xor bh, bh
    inc si
    cmp bx, [cs:wvm_nfunc]
    RAE wvm_badop
    mov ax, [cs:wvm_nfrm]
    cmp ax, WV_NFRM
    jae .deep
    mov [cs:wvm_tfn], bx
    shl bx, 1
    shl bx, 1
    add bx, [cs:wvm_code]
    add bx, 2
    mov ax, [es:bx]
    add ax, [cs:wvm_code]
    mov [cs:wvm_tofs], ax
    mov al, [es:bx+2]
    xor ah, ah
    mov [cs:wvm_tna], ax
    mov al, [es:bx+3]
    xor ah, ah
    mov [cs:wvm_tnl], ax
    mov ax, [cs:wvm_tna]
    shl ax, 1
    shl ax, 1
    mov bx, di
    sub bx, ax
    mov [cs:wvm_tbase], bx      ; base = sp - nargs cells
    mov bx, [cs:wvm_tnl]
    sub bx, [cs:wvm_tna]
.fl:
    or bx, bx
    jz .filled
    push bx
    mov ax, WT_NULL             ; the non-argument locals are NULL (4.7.1)
    xor dx, dx
    call wvm_pushv
    pop bx
    dec bx
    jmp short .fl
.filled:
    mov ax, [cs:wvm_nfrm]       ; the frame record, at WV_FRM + 6n
    mov bx, ax
    shl bx, 1
    add bx, ax
    shl bx, 1
    add bx, WV_FRM
    mov [bx+FR_RET], si         ; the CALLER's next instruction
    mov ax, [cs:wvm_tbase]
    mov [bx+FR_BASE], ax
    mov [cs:wvm_cbase], ax
    mov ax, [cs:wvm_tfn]
    mov [bx+FR_FN], al
    mov [cs:wvm_cfn], ax
    mov ax, [cs:wvm_tnl]
    mov [bx+FR_NLOC], al
    mov [cs:wvm_cnloc], ax
    inc word [cs:wvm_nfrm]
    mov si, [cs:wvm_tofs]
    jmp wvm_next
.deep:
    mov al, WE_DEEP
    jmp wvm_err

wvm_ret:
    call wvm_popv
    mov [cs:wvm_tval], ax
    mov [cs:wvm_tval+2], dx
    dec word [cs:wvm_nfrm]
    mov ax, [cs:wvm_nfrm]
    mov bx, ax
    shl bx, 1
    add bx, ax
    shl bx, 1
    add bx, WV_FRM
    mov di, [bx+FR_BASE]        ; sp = base
    mov si, [bx+FR_RET]
    cmp word [cs:wvm_nfrm], 0
    RE wvm_x_done               ; the outermost RET ends the invocation
    mov ax, [cs:wvm_tval]
    mov dx, [cs:wvm_tval+2]
    call wvm_pushv
    mov ax, [cs:wvm_nfrm]       ; the caller is the top frame again
    dec ax
    mov bx, ax
    shl bx, 1
    add bx, ax
    shl bx, 1
    add bx, WV_FRM
    mov ax, [bx+FR_BASE]
    mov [cs:wvm_cbase], ax
    mov al, [bx+FR_NLOC]
    xor ah, ah
    mov [cs:wvm_cnloc], ax
    mov al, [bx+FR_FN]
    xor ah, ah
    mov [cs:wvm_cfn], ax
    jmp wvm_next

; --- INCG / DECG -------------------------------------------------------------
wvm_incg:
    mov bl, [es:si]
    xor bh, bh
    inc si
    cmp bx, WV_NGLOB
    RAE wvm_badop
    shl bx, 1
    shl bx, 1
    add bx, WV_GLOB
    cmp word [bx], WT_INT
    RNE wvm_type
    inc word [bx+2]
    jmp wvm_next

wvm_decg:
    mov bl, [es:si]
    xor bh, bh
    inc si
    cmp bx, WV_NGLOB
    RAE wvm_badop
    shl bx, 1
    shl bx, 1
    add bx, WV_GLOB
    cmp word [bx], WT_INT
    RNE wvm_type
    dec word [bx+2]
    jmp wvm_next

; --- AGET / ASET -------------------------------------------------------------
; The array payload is a count word then count words (WEAVE-SPEC 4.8).
wvm_aget:
    mov bx, di
    cmp word [bx-8], WT_ARR
    RNE wvm_type
    cmp word [bx-4], WT_INT
    RNE wvm_type
    mov dx, [bx-6]
    mov ax, [bx-2]
    sub di, 8
    call wvm_apay               ; BX = the payload; AX is the index
    mov dx, [bx]                ; the count
    cmp ax, dx
    RAE wvm_aoob
    shl ax, 1
    add bx, ax
    mov ax, [bx+2]
    jmp wvm_ipush

wvm_aset:
    mov bx, di
    cmp word [bx-12], WT_ARR
    RNE wvm_type
    cmp word [bx-8], WT_INT
    RNE wvm_type
    cmp word [bx-4], WT_INT
    RNE wvm_type
    mov ax, [bx-2]
    mov [cs:wvm_tval], ax       ; the value, across the bounds check
    mov dx, [bx-10]
    mov ax, [bx-6]
    sub di, 12
    call wvm_apay
    mov dx, [bx]
    cmp ax, dx
    RAE wvm_aoob
    shl ax, 1
    add bx, ax
    mov ax, [cs:wvm_tval]
    mov [bx+2], ax
    jmp wvm_next

; the shared refusal: `array index %d of %d.`  AX = the index, DX = the count.
wvm_aoob:
    mov [cs:wvm_erra], ax
    mov [cs:wvm_errb], dx
    mov al, WE_AIDX
    jmp wvm_err

; wvm_apay - the payload address of array handle DX, in BX.  AX preserved.
; A handle whose table type is not an array means the tag said array and the
; table did not, which is a corrupt claim - 10.6's `type mismatch.`
wvm_apay:
    push ax
    call wvm_hent
    cmp byte [bx+HT_TYPE], HTY_ARR
    RNE wvm_type
    mov bx, [bx+HT_OFF]
    add bx, AO_HDR
    pop ax
    ret

; =============================================================================
; GETP / SETP / CALLM / the impure builtins - out through wvm_native
; =============================================================================

wvm_getp:
    mov al, [es:si]
    xor ah, ah
    inc si
    mov [cs:wvm_natomv], ax
    mov bx, di
    cmp word [bx-4], WT_COMP
    RNE wvm_type
    mov ax, [bx-2]
    mov [cs:wvm_ncomp], ax
    mov word [cs:wvm_nkind], WNK_GETP
    mov word [cs:wvm_nargc], 0
    mov word [cs:wvm_npop], 1
    mov word [cs:wvm_npush], 1
    mov word [cs:wvm_nback], 2
    jmp wvm_nat

wvm_setp:
    mov al, [es:si]
    xor ah, ah
    inc si
    mov [cs:wvm_natomv], ax
    mov bx, di
    cmp word [bx-8], WT_COMP
    RNE wvm_type
    mov ax, [bx-6]
    mov [cs:wvm_ncomp], ax
    mov word [cs:wvm_nkind], WNK_SETP
    mov word [cs:wvm_nargc], 1
    mov word [cs:wvm_npop], 2
    mov word [cs:wvm_npush], 0
    mov word [cs:wvm_nback], 2
    jmp wvm_nat

wvm_callm:
    mov al, [es:si]
    xor ah, ah
    mov [cs:wvm_natomv], ax
    mov bl, [es:si+1]
    xor bh, bh
    cmp bx, 9
    RAE wvm_badop
    inc si
    inc si
    mov [cs:wvm_nargc], bx
    inc bx
    mov [cs:wvm_npop], bx       ; the arguments and the component
    shl bx, 1
    shl bx, 1
    mov ax, di
    sub ax, bx
    mov bx, ax
    cmp word [bx], WT_COMP
    RNE wvm_type
    mov ax, [bx+2]
    mov [cs:wvm_ncomp], ax
    mov word [cs:wvm_nkind], WNK_CALLM
    mov word [cs:wvm_npush], 1
    mov word [cs:wvm_nback], 3
    jmp wvm_nat

; --- BUILT: the six PURE builtins here, the six impure ones out (8.1) -------
wvm_built:
    mov bl, [es:si]
    xor bh, bh
    cmp bx, 12
    RAE wvm_badbi
    mov al, [es:si+1]
    xor ah, ah
    cmp ax, 9
    RAE wvm_badop
    mov [cs:wvm_nargc], ax
    cmp bx, 6
    jb .impure
    inc si
    inc si
    shl bx, 1
    jmp [cs:bx+wvm_btab-12]     ; the six pure ones are indices 6..11
.impure:
    mov [cs:wvm_natomv], bx     ; the builtin index rides in the atom slot
    inc si
    inc si
    mov ax, [cs:wvm_nargc]
    mov [cs:wvm_npop], ax
    mov word [cs:wvm_ncomp], 0
    mov word [cs:wvm_nkind], WNK_BUILT
    mov word [cs:wvm_npush], 1
    mov word [cs:wvm_nback], 3
    jmp wvm_nat

wvm_btab:
    dw wvm_b_str, wvm_b_len, wvm_b_substr, wvm_b_find, wvm_b_rand, wvm_b_array

; -----------------------------------------------------------------------------
; wvm_nat - the bridge.  The arguments are COPIED off the eval stack and NOT
; popped, because a GC answer has to leave the stack as it found it (4.8).
; SI is already past the operands; wvm_nback rewinds it on that answer.
; -----------------------------------------------------------------------------
wvm_nat:
    mov [cs:wvm_pc], si
    mov [cs:wvm_sp], di
    mov [cs:wvm_bcx], cx
    mov bx, [cs:wvm_nblk]
    mov ax, [cs:wvm_nkind]
    mov [cs:bx+WN_KIND], ax
    mov ax, [cs:wvm_ncomp]
    mov [cs:bx+WN_COMP], ax
    mov ax, [cs:wvm_natomv]
    mov [cs:bx+WN_ATOM], ax
    mov cx, [cs:wvm_nargc]
    mov [cs:bx+WN_ARGC], cx
    mov word [cs:bx+WN_ERRC], 0
    mov ax, cx
    shl ax, 1
    shl ax, 1
    mov si, di
    sub si, ax                  ; the first argument's cell
    add bx, WN_ARG0
    jcxz .copied
.cp:
    mov ax, [si]
    mov [cs:bx], ax
    mov ax, [si+2]
    mov [cs:bx+2], ax
    add si, 4
    add bx, 4
    loop .cp
.copied:
    push ds
    push es
    mov ds, [cs:wvm_ourds]      ; cc8086:allow the includer's own segment, so
    mov es, [cs:wvm_besv]       ; that C runs normally; ES is what the caller
    cld                         ; had, because a callback's ES is the kernel's
    call wvm_native
    pop es
    pop ds
    mov si, [cs:wvm_pc]
    mov di, [cs:wvm_sp]
    mov cx, [cs:wvm_bcx]
    or ax, ax
    jz .ok
    cmp ax, 2
    je .gc
    mov bx, [cs:wvm_nblk]       ; a script error the runtime named (10.6.1)
    mov ax, [cs:bx+WN_ERRC]
    mov [cs:wvm_errc], ax
    mov ax, [cs:bx+WN_ERRA]
    mov [cs:wvm_erra], ax
    mov ax, [cs:bx+WN_ERRB]
    mov [cs:wvm_errb], ax
    jmp wvm_err2
.gc:
    sub si, [cs:wvm_nback]
    jmp wvm_x_gc
.ok:
    mov ax, [cs:wvm_npop]
    shl ax, 1
    shl ax, 1
    sub di, ax
    cmp word [cs:wvm_npush], 0
    je wvm_next
    mov bx, [cs:wvm_nblk]
    mov ax, [cs:bx+WN_REST]
    mov dx, [cs:bx+WN_RESV]
    call wvm_pushv
    jmp wvm_next

; =============================================================================
; THE SIX PURE BUILTINS (WEAVE-SPEC 8.1) - here rather than out, which is what
; puts them inside the weavevm corpus.  Entered with SI past both operands and
; wvm_nargc set; a GC answer rewinds SI by 3.
; =============================================================================

; wvm_bgc / wvm_bargs - the two things every one of them starts or ends with.
wvm_bgc:
    sub si, 3
    jmp wvm_x_gc

; wvm_b_str(v) -> string.  int / bool / null / string (WEAVE-SPEC 8.1).
wvm_b_str:
    cmp word [cs:wvm_nargc], 1
    RNE wvm_badbi
    mov bx, di
    mov ax, [bx-4]
    mov dx, [bx-2]
    cmp ax, WT_STR
    je .same
    cmp ax, WT_INT
    je .int
    cmp ax, WT_BOOL
    je .bool
    cmp ax, WT_NULL
    je .null
    jmp wvm_type
.same:
    jmp wvm_next                ; a string is already its own text - and no
                                ; pop, because the value stays where it is
.int:
    mov ax, dx
    call wvm_itoa               ; wvm_num, and AX = the length
    jmp short .made
.bool:
    or dx, dx
    jz .false
    mov ax, 4
    mov word [cs:wvm_num], 'tr'
    mov word [cs:wvm_num+2], 'ue'
    jmp short .made
.false:
    mov ax, 5
    mov word [cs:wvm_num], 'fa'
    mov word [cs:wvm_num+2], 'ls'
    mov byte [cs:wvm_num+4], 'e'
    jmp short .made
.null:
    mov ax, 4
    mov word [cs:wvm_num], 'nu'
    mov word [cs:wvm_num+2], 'll'
.made:
    call wvm_ownsa              ; wvm_sa = (our segment, wvm_num, AX)
    call wvm_nosb
    call wvm_mkstr
    RC wvm_bgc                  ; a name this short cannot be `too long`
    sub di, 4
    mov dx, ax
    mov ax, WT_STR
    call wvm_pushv
    jmp wvm_next

; wvm_ownsa - wvm_sa describes AX bytes at wvm_num, in OUR segment.
wvm_ownsa:
    push ax
    mov ax, [cs:wvm_ourds]
    mov [cs:wvm_sa], ax
    mov word [cs:wvm_sa+2], wvm_num
    pop ax
    mov [cs:wvm_sa+4], ax
    ret

; wvm_itoa - AX, signed, into wvm_num.  out: AX = the length.  CX preserved.
wvm_itoa:
    push bx
    push cx
    push dx
    push di
    mov di, wvm_num + 9
    mov bx, 0                   ; the sign
    or ax, ax
    jns .pos
    mov bx, 1
    neg ax                      ; -32768 negates to itself, and the loop below
.pos:                           ; divides the UNSIGNED value, which is right
    mov cx, 10
    xor dx, dx
.dig:
    xor dx, dx
    div cx
    add dl, '0'
    dec di
    mov [cs:di], dl
    or ax, ax
    jnz .dig
    or bx, bx
    jz .nosign
    dec di
    mov byte [cs:di], '-'
.nosign:
    mov ax, wvm_num + 9
    sub ax, di
    push ax                     ; the length; now shuffle it down to wvm_num
    mov cx, ax
    mov bx, wvm_num
.mv:
    mov dl, [cs:di]
    mov [cs:bx], dl
    inc di
    inc bx
    loop .mv
    pop ax
    pop di
    pop dx
    pop cx
    pop bx
    ret

; wvm_b_len(v) -> int.  A string or an array (8.1).
wvm_b_len:
    cmp word [cs:wvm_nargc], 1
    RNE wvm_badbi
    mov bx, di
    mov ax, [bx-4]
    mov dx, [bx-2]
    cmp ax, WT_STR
    je .str
    cmp ax, WT_ARR
    RNE wvm_type
    call wvm_apay
    mov ax, [bx]
    sub di, 4
    jmp wvm_ipush
.str:
    call wvm_sdesc
    mov ax, [cs:wvm_sa+4]
    sub di, 4
    jmp wvm_ipush

; wvm_b_substr(s, start, len) -> string.  0-based, CLAMPED, never an error.
wvm_b_substr:
    cmp word [cs:wvm_nargc], 3
    RNE wvm_badbi
    mov bx, di
    cmp word [bx-12], WT_STR
    RNE wvm_type
    cmp word [bx-8], WT_INT
    RNE wvm_type
    cmp word [bx-4], WT_INT
    RNE wvm_type
    mov dx, [bx-10]
    call wvm_sdesc
    mov bx, di
    mov ax, [bx-6]              ; start
    or ax, ax
    jns .st
    xor ax, ax                  ; max(0, start)
.st:
    cmp ax, [cs:wvm_sa+4]
    jbe .in
    mov ax, [cs:wvm_sa+4]       ; past the end: an empty answer
.in:
    add [cs:wvm_sa+2], ax
    sub [cs:wvm_sa+4], ax       ; what is left after `start`
    mov ax, [bx-2]              ; len
    or ax, ax
    jns .ln
    xor ax, ax                  ; max(0, len)
.ln:
    cmp ax, [cs:wvm_sa+4]
    jae .keep
    mov [cs:wvm_sa+4], ax
.keep:
    call wvm_nosb
    call wvm_mkstr
    RC wvm_bgc                  ; a substring cannot be over 255 either
    sub di, 12
    mov dx, ax
    mov ax, WT_STR
    call wvm_pushv
    jmp wvm_next

; wvm_b_find(s, needle) -> int.  The first index, -1 when absent; an empty
; needle answers 0, which is what the model's str.find does.
wvm_b_find:
    cmp word [cs:wvm_nargc], 2
    RNE wvm_badbi
    mov bx, di
    cmp word [bx-8], WT_STR
    RNE wvm_type
    cmp word [bx-4], WT_STR
    RNE wvm_type
    mov dx, [bx-2]              ; the needle into wvm_sb
    call wvm_sdesc
    call wvm_atob
    mov bx, di
    mov dx, [bx-6]
    call wvm_sdesc              ; ...and the haystack into wvm_sa
    sub di, 8
    call wvm_find
    jmp wvm_ipush

; wvm_find - wvm_sb inside wvm_sa.  out: AX = the index, or -1.  CX preserved.
wvm_find:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ds, [cs:wvm_sa]         ; cc8086:allow the two strings' segments, for
    mov es, [cs:wvm_sb]         ; the scan; both restored at the exit
    mov dx, [cs:wvm_sa+4]
    sub dx, [cs:wvm_sb+4]
    js .no                      ; the needle is longer than the haystack
    xor bx, bx                  ; BX = the start we are trying
.try:
    cmp bx, dx
    ja .no
    mov si, [cs:wvm_sa+2]
    add si, bx
    mov di, [cs:wvm_sb+2]
    mov cx, [cs:wvm_sb+4]
    jcxz .hit
    cld
    repe cmpsb
    je .hit
    inc bx
    jmp short .try
.hit:
    mov ax, bx
    jmp short .out
.no:
    mov ax, -1
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; wvm_b_rand(n) -> int.  WEAVE-SPEC 8.1.1's pinned LCG, over the WHOLE word:
; the low bits of any 16-bit LCG are poor.
wvm_b_rand:
    cmp word [cs:wvm_nargc], 1
    RNE wvm_badbi
    mov bx, di
    cmp word [bx-4], WT_INT
    RNE wvm_type
    mov ax, [bx-2]
    cmp ax, 1
    RL wvm_type                 ; 10.6.1's `rand of %s.` is the runtime's; the
                                ; core has no number formatter for a sentence
    mov [cs:wvm_tna], ax        ; n, banked over the multiply
    sub di, 4
    mov bx, [cs:wvm_hs]
    mov ax, [bx+HS_SEED]
    mov dx, 25173
    mul dx                      ; DX:AX; AX alone IS the `mod 65536`
    add ax, 13849
    mov [bx+HS_SEED], ax
    xor dx, dx                  ; ...and the answer is the WHOLE word mod n:
    div word [cs:wvm_tna]       ; the low bits of a 16-bit LCG are poor (8.1.1)
    mov ax, dx
    jmp wvm_ipush

; wvm_b_array(n) -> array.  Legal only as a `var` initializer (8.1), which the
; packer enforces; here it is simply an allocation.
wvm_b_array:
    cmp word [cs:wvm_nargc], 1
    RNE wvm_badbi
    mov bx, di
    cmp word [bx-4], WT_INT
    RNE wvm_type
    mov ax, [bx-2]
    cmp ax, 1
    RL wvm_type
    cmp ax, 2048
    RG wvm_type
    mov [cs:wvm_tna], ax        ; the count, banked over the allocation
    shl ax, 1
    add ax, 2                   ; count word + count words
    mov bl, HTY_ARR
    call wvm_alloc              ; preserves CX, which is the budget
    RC wvm_bgc
    mov [cs:wvm_tval], ax       ; the handle
    mov dx, [cs:wvm_tna]
    mov [bx], dx                ; the count
    add bx, 2
    xor ax, ax
.z:
    or dx, dx
    jz .zd
    mov [bx], ax
    add bx, 2
    dec dx
    jmp short .z
.zd:
    sub di, 4
    mov dx, [cs:wvm_tval]
    mov ax, WT_ARR
    call wvm_pushv
    jmp wvm_next

; =============================================================================
; THE EVENT RING (WEAVE-SPEC 4.9) - C-callable, because a kernel callback is
; where a record is born and the callback is in C.
; A record is 8 bytes: comp byte, atom byte, data1 word, data2 word, 0.
; =============================================================================

; wvm_slot - DS-relative address of ring slot AX (0..15), in BX.
wvm_slot:
    push ax
    and ax, 15
    mov bx, ax
    shl bx, 1
    shl bx, 1
    shl bx, 1
    add bx, WV_RING
    pop ax
    ret

; -----------------------------------------------------------------------------
; int wvm_enq(int comp, int atom, int d1, int d2)
; out: 1 queued, 0 refused - and 0 with the ring full of keys is the caller's
;      cue to sound WEAVE-SPEC 4.9 rule 5's BEL.
;
; The policy is the whole of 4.9, in its order. The one thing the ring does
; that the model's list does not is name a slot, and 4.9's paragraph on that
; is why coalescing OVERWRITES IN PLACE and collapsing REMOVES AND APPENDS.
; -----------------------------------------------------------------------------
_wvm_enq:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    xor ax, ax
    mov dx, [cs:wvm_vmseg]
    or dx, dx
    jz .out
    mov ds, dx                  ; cc8086:allow the claim, restored at .out
    mov si, [cs:wvm_hs]
    mov dx, [bp+6]              ; the atom
    ; --- rule 3: ontimer and ontick collapse to one, newest at the BACK -----
    cmp dx, 59                  ; ontimer
    je .single
    cmp dx, 57                  ; ontick
    je .single
    ; --- rule 2: onchange/onselect/onclick/onscore coalesce per component ---
    cmp dx, 49                  ; onchange
    je .coal
    cmp dx, 51                  ; onselect
    je .coal
    cmp dx, 48                  ; onclick
    je .coal
    cmp dx, 56                  ; onscore
    je .coal
    jmp short .plain
.single:
    call wvm_rdrop              ; remove every record with this atom, any comp
    jmp short .append
.coal:
    mov cx, 0
    mov al, [si+HS_RCOUNT]
    xor ah, ah
    mov di, ax
.cl:
    cmp cx, di
    jae .plain
    mov al, [si+HS_RHEAD]
    xor ah, ah
    add ax, cx
    call wvm_slot
    mov al, [bx]
    xor ah, ah
    cmp ax, [bp+4]
    jne .cn
    mov al, [bx+1]
    xor ah, ah
    cmp ax, dx
    jne .cn
    call wvm_rfill              ; overwrite IN PLACE - full or not
    mov ax, 1
    jmp short .out
.cn:
    inc cx
    jmp short .cl
.plain:
    mov al, [si+HS_RCOUNT]
    cmp al, WV_NRING
    jb .append
    ; --- rule 4: a full ring taking a KEY drops the newest non-key ---------
    cmp dx, 50                  ; onkey
    jne .refuse
    call wvm_rdropnk
    or ax, ax
    jz .refuse                  ; rule 5: full of keys - the caller BELs
.append:
    mov al, [si+HS_RCOUNT]
    cmp al, WV_NRING
    jae .refuse
    xor ah, ah
    mov cl, [si+HS_RHEAD]
    xor ch, ch
    add ax, cx
    call wvm_slot
    call wvm_rfill
    inc byte [si+HS_RCOUNT]
    mov ax, 1
    jmp short .out
.refuse:
    xor ax, ax
.out:
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; wvm_rfill - write the record from [bp+4..] into slot BX.
wvm_rfill:
    push ax
    mov ax, [bp+4]
    mov [bx], al
    mov ax, [bp+6]
    mov [bx+1], al
    mov ax, [bp+8]
    mov [bx+2], ax
    mov ax, [bp+10]
    mov [bx+4], ax
    mov word [bx+6], 0
    pop ax
    ret

; wvm_rdrop - remove every queued record whose atom is DX.  SI = the scratch.
wvm_rdrop:
    push ax
    push cx
    push dx
    xor cx, cx
.scan:
    mov al, [si+HS_RCOUNT]
    xor ah, ah
    cmp cx, ax
    jae .done
    mov al, [si+HS_RHEAD]
    xor ah, ah
    add ax, cx
    call wvm_slot
    mov al, [bx+1]
    xor ah, ah
    cmp ax, dx
    jne .next
    call wvm_rremove            ; slides the rest down and decrements the count
    jmp short .scan
.next:
    inc cx
    jmp short .scan
.done:
    pop dx
    pop cx
    pop ax
    ret

; wvm_rdropnk - remove the NEWEST queued non-key record (rule 4).
; out: AX = 1 one went, 0 the ring is all keys.
wvm_rdropnk:
    push cx
    push dx
    mov cl, [si+HS_RCOUNT]
    xor ch, ch
.scan:
    or cx, cx
    jz .no
    dec cx
    push cx
    mov al, [si+HS_RHEAD]
    xor ah, ah
    add ax, cx
    call wvm_slot
    mov al, [bx+1]
    cmp al, 50                  ; onkey
    pop cx
    je .scan
    call wvm_rremove
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop dx
    pop cx
    ret

; wvm_rremove - drop the CXth-oldest record; the ones after it slide down one
; slot. At most 15 eight-byte moves, on a path that fires 18 times a second.
; in: CX = the position, SI = the scratch.  CX and DX preserved.
wvm_rremove:
    push ax
    push cx
    push dx
    push di
    mov dl, [si+HS_RCOUNT]
    xor dh, dh
    dec dx                      ; the last position
.mv:
    cmp cx, dx
    jae .done
    mov al, [si+HS_RHEAD]
    xor ah, ah
    add ax, cx
    call wvm_slot
    mov di, bx
    inc cx
    mov al, [si+HS_RHEAD]
    xor ah, ah
    add ax, cx
    call wvm_slot               ; BX = the NEXT record
    push cx
    mov cx, WV_RECSZ
.b:
    mov al, [bx]
    mov [di], al
    inc bx
    inc di
    loop .b
    pop cx
    jmp short .mv
.done:
    dec byte [si+HS_RCOUNT]
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; int wvm_deq(int *rec) - take the oldest record into rec[0..3].
; out: 1 got one, 0 the ring is empty.
; -----------------------------------------------------------------------------
_wvm_deq:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    xor ax, ax
    mov dx, [cs:wvm_vmseg]
    or dx, dx
    jz .out
    push ds
    pop cx                      ; CX = our segment, for the answer
    mov ds, dx                  ; cc8086:allow the claim, restored at .out
    mov si, [cs:wvm_hs]
    cmp byte [si+HS_RCOUNT], 0
    je .none
    mov al, [si+HS_RHEAD]
    xor ah, ah
    call wvm_slot
    mov di, [bp+4]
    push ds
    mov ds, cx                  ; cc8086:allow ...and ours, to write the answer
    push es
    mov es, dx
    mov al, [es:bx]
    xor ah, ah
    mov [di], ax                ; comp
    mov al, [es:bx+1]
    xor ah, ah
    mov [di+2], ax              ; atom
    mov ax, [es:bx+2]
    mov [di+4], ax              ; data1
    mov ax, [es:bx+4]
    mov [di+6], ax              ; data2
    pop es
    pop ds
    mov al, [si+HS_RHEAD]
    inc al
    and al, 15
    mov [si+HS_RHEAD], al
    dec byte [si+HS_RCOUNT]
    mov ax, 1
    jmp short .out
.none:
    xor ax, ax
.out:
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; int wvm_rcount(void)
_wvm_rcount:
    push bx
    push ds
    xor ax, ax
    mov bx, [cs:wvm_vmseg]
    or bx, bx
    jz .out
    mov ds, bx                  ; cc8086:allow the claim, restored below
    mov bx, [cs:wvm_hs]
    mov al, [bx+HS_RCOUNT]
    xor ah, ah
.out:
    pop ds
    pop bx
    ret

; =============================================================================
; STRINGS, FOR THE RUNTIME (and 8.3's serialization, which shares the walk)
; =============================================================================

; -----------------------------------------------------------------------------
; int wvm_str_read(int handle, char *dst, unsigned cap)
; out: the number of characters copied; dst is NUL-terminated.
; -----------------------------------------------------------------------------
_wvm_str_read:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    xor ax, ax
    mov dx, [cs:wvm_vmseg]
    or dx, dx
    jz .empty
    mov ds, dx                  ; cc8086:allow the claim, for the descriptor;
    mov dx, [bp+4]              ; every segment is restored at .out
    call wvm_sdesc
    mov cx, [bp+8]
    or cx, cx
    jz .out                     ; a zero capacity: nothing, not even the NUL
    dec cx                      ; ...leaving room for it
    cmp cx, [cs:wvm_sa+4]
    jbe .fit
    mov cx, [cs:wvm_sa+4]
.fit:
    mov ax, [cs:wvm_ourds]
    mov es, ax                  ; cc8086:allow ES = ours, the DESTINATION, and
    mov di, [bp+6]              ; DS = wherever the characters are; both back
    mov si, [cs:wvm_sa+2]       ; at .out
    mov ds, [cs:wvm_sa]
    cld
    mov ax, cx                  ; the answer, before rep eats CX
    rep movsb
    mov byte [es:di], 0
    jmp short .out
.empty:
    mov ax, [cs:wvm_ourds]
    mov es, ax
    mov di, [bp+6]
    cmp word [bp+8], 0
    je .out
    mov byte [es:di], 0
    xor ax, ax
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; -----------------------------------------------------------------------------
; int wvm_str_make(const char *s)
; out: the handle; 0 = a collection is needed, -1 = over 255 characters.
; -----------------------------------------------------------------------------
_wvm_str_make:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    mov ax, [cs:wvm_vmseg]
    or ax, ax
    jz .none
    mov si, [bp+4]
    xor cx, cx
.len:
    cmp byte [si], 0
    je .have
    inc si
    inc cx
    cmp cx, 256
    jb .len
.have:
    mov ax, [cs:wvm_ourds]
    mov [cs:wvm_sa], ax
    mov ax, [bp+4]
    mov [cs:wvm_sa+2], ax
    mov [cs:wvm_sa+4], cx
    call wvm_nosb
    mov ds, [cs:wvm_vmseg]      ; cc8086:allow the claim, restored at .out
    call wvm_mkstr
    jnc .out
    or ax, ax
    jz .out                     ; 0 = collect and try again
    mov ax, -1
    jmp short .out
.none:
    xor ax, ax
.out:
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; int wvm_atom_handle(int atom) - the static handle for an atom, 0 = none.
_wvm_atom_handle:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push ds
    xor ax, ax
    mov dx, [cs:wvm_vmseg]
    or dx, dx
    jz .out
    mov ds, dx                  ; cc8086:allow the claim, restored below
    mov ax, [bp+4]
    call wvm_atomh
    jnc .out
    xor ax, ax
.out:
    pop ds
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; =============================================================================
; THE GLOBALS, READ AND WRITTEN BY THE RUNTIME (8.3's rehydration needs it,
; and so does every diagnostic)
; =============================================================================

; int wvm_gtag(int i) / int wvm_gval(int i)
_wvm_gtag:
    push bp
    mov bp, sp
    push bx
    push ds
    xor ax, ax
    mov bx, [cs:wvm_vmseg]
    or bx, bx
    jz .out
    mov ds, bx                  ; cc8086:allow the claim, restored below
    mov bx, [bp+4]
    cmp bx, WV_NGLOB
    jae .out
    shl bx, 1
    shl bx, 1
    mov ax, [bx+WV_GLOB]
.out:
    pop ds
    pop bx
    pop bp
    ret

_wvm_gval:
    push bp
    mov bp, sp
    push bx
    push ds
    xor ax, ax
    mov bx, [cs:wvm_vmseg]
    or bx, bx
    jz .out
    mov ds, bx                  ; cc8086:allow the claim, restored below
    mov bx, [bp+4]
    cmp bx, WV_NGLOB
    jae .out
    shl bx, 1
    shl bx, 1
    mov ax, [bx+WV_GLOB+2]
.out:
    pop ds
    pop bx
    pop bp
    ret

; void wvm_gset(int i, int tag, int val)
_wvm_gset:
    push bp
    mov bp, sp
    push bx
    push dx
    push ds
    mov bx, [cs:wvm_vmseg]
    or bx, bx
    jz .out
    mov ds, bx                  ; cc8086:allow the claim, restored below
    mov bx, [bp+4]
    cmp bx, WV_NGLOB
    jae .out
    shl bx, 1
    shl bx, 1
    add bx, WV_GLOB
    mov ax, [bp+6]
    mov [bx], ax
    mov dx, [bp+8]
    mov [bx+2], dx
.out:
    pop ds
    pop dx
    pop bx
    xor ax, ax
    pop bp
    ret

; =============================================================================
; WEAVE-SPEC 8.3's SERIALIZATION - the .SAV image, and the corpus's end state.
; ONE routine for both, which is what makes the differential a differential:
; what weavevm compares is exactly the bytes saveState writes.
; =============================================================================

; -----------------------------------------------------------------------------
; unsigned wvm_save(unsigned seg, unsigned off, unsigned cap)
; out: the number of bytes written, or 0 when it does not fit.
; -----------------------------------------------------------------------------
_wvm_save:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    xor ax, ax
    mov dx, [cs:wvm_vmseg]
    or dx, dx
    jz .out
    mov es, [bp+4]              ; cc8086:allow the destination, and DS the
    mov di, [bp+6]              ; claim; both restored at .out
    mov ds, dx
    mov si, di                  ; SI = where we started
    mov byte [es:di], 'W'
    mov byte [es:di+1], 'S'
    mov byte [es:di+2], 'V'
    mov byte [es:di+3], 0x1A
    mov word [es:di+4], 1
    add di, 6
    mov bx, WV_GLOB
    mov cx, WV_NGLOB
.cell:
    mov ax, [bx]
    mov [es:di], ax
    add di, 2
    mov dx, [bx+2]
    cmp ax, WT_STR
    je .str
    cmp ax, WT_ARR
    je .arr
    mov [es:di], dx
    add di, 2
    jmp short .next
.str:
    push bx
    push cx
    call wvm_sdesc
    mov cx, [cs:wvm_sa+4]
    mov [es:di], cl
    inc di
    call wvm_copyout
    pop cx
    pop bx
    jmp short .next
.arr:
    push bx
    push cx
    call wvm_apay
    mov cx, [bx]
    mov [es:di], cx
    add di, 2
    add bx, 2
.aw:
    jcxz .ad
    mov ax, [bx]
    mov [es:di], ax
    add di, 2
    add bx, 2
    dec cx
    jmp short .aw
.ad:
    pop cx
    pop bx
.next:
    add bx, 4
    dec cx
    jnz .cell
    mov ax, di
    sub ax, si                  ; the length
    cmp ax, [bp+8]
    jbe .out
    xor ax, ax                  ; it did not fit: 0, and the caller refuses
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; wvm_copyout - CX bytes from wvm_sa to ES:DI, DI advancing.  DS is the claim
; on the way in and on the way out.
wvm_copyout:
    push ax
    push bx
    push cx
    push si
    push ds
    mov si, [cs:wvm_sa+2]
    mov ds, [cs:wvm_sa]         ; cc8086:allow whichever claim holds the bytes
    cld
    rep movsb
    pop ds
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; int wvm_load(unsigned seg, unsigned off, unsigned len)
; out: 1 loaded, 0 refused - a bad magic, a short image, or no room for a
;      string. WEAVE-SPEC 8.3: false, never a crash.
;
; A component handle that no longer resolves is the RUNTIME's to null out: it
; knows which comp_ids this bundle declares and this file may not (the
; header's rule). wvm_gtag/wvm_gval/wvm_gset are how it does that.
; -----------------------------------------------------------------------------
_wvm_load:
    push bp
    mov bp, sp
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    xor ax, ax
    mov dx, [cs:wvm_vmseg]
    or dx, dx
    jz .out
    cmp word [bp+8], 6 + 4 * WV_NGLOB
    jb .out                     ; shorter than the smallest legal image
    mov es, [bp+4]              ; cc8086:allow the source, and DS the claim
    mov si, [bp+6]
    mov ds, dx
    cmp byte [es:si], 'W'
    jne .out
    cmp byte [es:si+1], 'S'
    jne .out
    cmp byte [es:si+2], 'V'
    jne .out
    cmp byte [es:si+3], 0x1A
    jne .out
    cmp word [es:si+4], 1
    jne .out
    add si, 6
    mov bx, WV_GLOB
    mov cx, WV_NGLOB
.cell:
    mov ax, [es:si]
    add si, 2
    cmp ax, WT_STR
    je .str
    cmp ax, WT_ARR
    je .arr
    cmp ax, WT_BOOL
    ja .out                     ; 4.3: tags >= 6 do not exist
    mov [bx], ax
    mov dx, [es:si]
    mov [bx+2], dx
    add si, 2
    jmp short .next
.str:
    push bx
    push cx
    mov al, [es:si]
    xor ah, ah
    inc si
    push ax
    push es
    pop dx
    mov [cs:wvm_sa], dx
    mov [cs:wvm_sa+2], si
    mov [cs:wvm_sa+4], ax
    call wvm_nosb
    call wvm_mkstr
    pop dx
    add si, dx
    pop cx
    pop bx
    jc .out                     ; no room: false, per 8.3
    mov word [bx], WT_STR
    mov [bx+2], ax
    jmp short .next
.arr:
    push bx
    push cx
    mov ax, [es:si]
    add si, 2
    mov [cs:wvm_tna], ax
    shl ax, 1
    add ax, 2
    push si
    mov bl, HTY_ARR
    call wvm_alloc
    pop si
    jc .oops
    mov dx, [cs:wvm_tna]
    mov [bx], dx
    add bx, 2
.aw:
    or dx, dx
    jz .ad
    push ax
    mov ax, [es:si]
    mov [bx], ax
    pop ax
    add si, 2
    add bx, 2
    dec dx
    jmp short .aw
.ad:
    pop cx
    pop bx
    mov word [bx], WT_ARR
    mov [bx+2], ax
    jmp short .next
.oops:
    pop cx
    pop bx
    jmp short .out
.next:
    add bx, 4
    dec cx
    jnz .cell
    mov ax, 1
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop bp
    ret

; =============================================================================
; THE DISPATCH TABLE - GENERATED, so the model and this core cannot drift
; (WEAVE-SPEC 4.5, 12.1). build/wvmtab.inc is written by
; `python3 tools/weavesim.py --emit-optab` and is a Makefile prerequisite of
; build/weave.bin, so an opcode added to the model that has no handler here is
; an nasm error naming the missing label rather than a silent disagreement.
; =============================================================================
%include "wvmtab.inc"

section .text
