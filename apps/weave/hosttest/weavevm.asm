; =============================================================================
; os8088 - apps/weave/hosttest/weavevm.asm
;
; A BOOT SECTOR THAT RUNS THE SHIPPING apps/weave/wvm.inc ON A REAL x86, WITH
; SS != DS AND NOTHING ELSE ON THE MACHINE, and diffs it case by case against
; tools/weavesim.py's end states (WEAVE-SPEC 12.3, 12.1.1).
;
; apps/runcpm/hosttest/rcz80test.asm and apps/c64/hosttest/c64memtest.asm are
; the shape and the precedent; what those headers say about %include applies
; here word for word. What runs is the SHIPPING TEXT of the VM core, not a
; copy of it, and it runs with no kernel, no window, no gfx lock and no C
; runtime under it - so a case that passes here passes because of the core.
;
; WEAVE-SPEC 13.1 makes this the FIRST gate of wave 3: the interaction is
; wired to the VM only after the VM has been diffed against the model. A
; runtime built on an unverified interpreter fails in the app, where the cause
; looks like a widget.
;
; WHAT IS COMPARED. Not a transcript: WEAVE-SPEC 8.3's SERIALIZED GLOBALS, the
; bytes saveState writes. The image is handle-free by construction, so the
; model (which has no handle table) and the machine (which has nothing else)
; describe the same thing - and the machine has to produce those bytes anyway,
; so the gate exercises shipping code rather than a routine written for it.
;
; WHAT EACH ROW CHECKS, and none of it is the core's logic restated:
;
;   1. the eleven bytecode cases in tests/weave/vmcorpus/, each run exactly as
;      the runtime runs one - bind, the module-init function if the bundle has
;      one, then the entry function in ADAPTIVE SLICES with a collection
;      between them whenever the core asks for one (WEAVE-SPEC 4.8, 4.10) -
;      and their 8.3 images compared byte for byte;
;   2. the SLICE ITSELF: every case is run twice, once with a 256-op budget
;      and once with a budget of 1, and both must reach the same end state.
;      A core that kept state in a register across a slice boundary passes
;      the first and fails the second, and nothing else in this family would
;      catch it;
;   3. the error rows: the ones that stop must stop with 10.6.1's code AND
;      leave the globals the model leaves - `divide by zero.` after two
;      assignments is two assignments, not none;
;   4. the RING's overflow policy (4.9) against the model's own Ring - the
;      coalesce-in-place / collapse-to-the-back distinction 4.9 pins, which is
;      invisible in any single-event test;
;   5. after every call: ES, SS:SP and BP are what they were and DF IS CLEAR;
;   6. NEGATIVE CONTROLS: one bytecode row and one ring row whose expected
;      answer is deliberately wrong, which must FAIL. A harness that cannot
;      see a broken core has proved nothing.
;
; RUN IT:  apps/weave/hosttest/weavevm.sh
; =============================================================================

cpu 8086
bits 16
org 0x7C00

SEG_STACK   equ 0x1000          ; SS, and SS != DS is the whole point
SEG_KERNEL  equ 0x3000          ; the ES sentinel: it must come back intact
SEG_VM      equ 0x4000          ; the VM claim (WEAVE-SPEC 4.7)
SEG_SAVE    equ 0x5000          ; where wvm_save writes the image we compare
VM_BYTES    equ 16384           ; 2.2's default ask
IMG_SECTORS equ 64              ; 32KB, and it is a CEILING and not a choice:
                                ; stage 1 loads with ES = 0 and a 16-bit BX,
                                ; so a sector past 0xFFFF wraps BX and writes
                                ; over the boot sector itself - which reads as
                                ; a machine that prints nothing at all

section .text
section .rodata follows=.text
section .data   follows=.rodata
section .bss    follows=.data nobits
section .text

; -----------------------------------------------------------------------------
; STAGE 1 - the boot sector reads the rest of the image in
; -----------------------------------------------------------------------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld
    mov word [lba], 1
    mov bx, 0x7E00
.rd:
    mov ax, [lba]
    cmp ax, IMG_SECTORS
    jae .go
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    mov dl, 0
    mov ax, 0x0201
    int 0x13
    jc .err
    add bx, 512
    inc word [lba]
    jmp .rd
.go:
    jmp 0x0000:body
.err:
    mov al, 'D'
    call putc
    jmp halt
lba: dw 0

    times 510-($-$$) db 0
    dw 0xAA55

; -----------------------------------------------------------------------------
; STAGE 2
; -----------------------------------------------------------------------------
body:
    cli
    mov ax, SEG_STACK
    mov ss, ax
    mov sp, 0xFFF0
    xor ax, ax
    mov ds, ax                  ; DS = CS = 0 stands in for the package, and
    mov ax, SEG_KERNEL          ; the corpus's code and atoms are addressed as
    mov es, ax                  ; offsets in it - which is what lets the
    sti                         ; "bundle claim" segment be 0
    cld
    xor bp, bp                  ; the failure count

    mov si, msg_hello
    call puts

    ; --- 1: the bytecode cases, at two budgets ------------------------------
    xor si, si                  ; SI = the row index
.case:
    cmp si, WVC_N
    jae .rings
    mov word [budget], 256
    call runcase
    mov [r1], ax
    mov word [budget], 1        ; ...and again, one op at a time: check 2
    call runcase
    mov [r2], ax
    mov ax, [r1]
    and ax, [r2]                ; both budgets must agree
    call verdict
    inc si
    jmp .case

    ; --- 4: the ring policy --------------------------------------------------
.rings:
    mov si, msg_ring
    call puts
    xor si, si
.ring:
    cmp si, WVR_N
    jae .done
    call runring
    call verdict
    inc si
    jmp .ring

.done:
    mov al, 'T'
    call putc
    mov ax, bp
    call putdec
    mov si, msg_tail
    or bp, bp
    jz .ok
    mov si, msg_bad
.ok:
    call puts
    mov al, 1
    or bp, bp
    jz .exit
    mov al, 2
.exit:
    mov dx, 0xF4                ; isa-debug-exit: the code is AL*2+1, so
    out dx, al                  ; 3 = passed and 5 = failed
halt:
    cli
    hlt
    jmp halt

; -----------------------------------------------------------------------------
; verdict - AX = 1 the row agreed, 0 it did not.  The NEG column inverts the
; expectation: a negative control that AGREES is the failure.
; in: SI = the row index (bytecode rows carry the flag; ring rows are keyed by
;     being the last one)
; -----------------------------------------------------------------------------
verdict:
    push ax
    push bx
    mov bx, [negrow]
    or bx, bx
    jnz .neg
    or ax, ax
    jz .fail
    mov al, '.'
    call putc
    jmp short .out
.fail:
    mov al, 'X'
    call putc
    call putname
    inc bp
    jmp short .out
.neg:
    or ax, ax
    jnz .slip
    mov al, 'N'                 ; correctly caught
    call putc
    jmp short .out
.slip:
    mov al, 'x'                 ; the control PASSED: the check proves nothing
    call putc
    call putname
    inc bp
.out:
    pop bx
    pop ax
    ret

putname:
    push ax
    push si
    mov al, ' '
    call putc
    mov si, [nameptr]
    call puts
    mov si, msg_nl
    call puts
    pop si
    pop ax
    ret

; =============================================================================
; ONE BYTECODE CASE
; in:  SI = the row index, [budget] = the ops per slice
; out: AX = 1 it agreed with the model, 0 it did not
; =============================================================================
runcase:
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, si
    mov cx, WVC_ROW
    mul cx
    mov bx, ax
    add bx, wvc_tab
    mov ax, [bx]                ; +0  name
    mov [nameptr], ax
    mov ax, [bx+24]             ; +24 the negative-control flag
    mov [negrow], ax
    mov ax, [bx+2]              ; +2  code
    mov [c_code], ax
    mov ax, [bx+4]
    mov [c_clen], ax
    mov ax, [bx+6]
    mov [c_nfunc], ax
    mov ax, [bx+8]
    mov [c_atoms], ax
    mov ax, [bx+12]
    mov [c_natom], ax
    mov ax, [bx+14]
    mov [c_init], ax
    mov ax, [bx+16]
    mov [c_entry], ax
    mov ax, [bx+18]
    mov [c_exp], ax
    mov ax, [bx+20]
    mov [c_explen], ax
    mov ax, [bx+22]
    mov [c_err], ax

    ; --- bind (WEAVE-SPEC 4.7) ----------------------------------------------
    mov ax, 0x1234              ; 8.1.1's pinned seed, the model's own
    push ax
    mov ax, nblk
    push ax
    push word [c_natom]
    push word [c_atoms]
    push word [c_nfunc]
    push word [c_clen]
    push word [c_code]
    xor ax, ax                  ; the bundle claim: segment 0, because the
    push ax                     ; corpus IS the image
    mov ax, VM_BYTES
    push ax
    mov ax, SEG_VM
    push ax
    call _wvm_bind
    add sp, 20

    ; --- the module-init function, when the bundle has one (2.6.2) ----------
    mov ax, [c_init]
    cmp ax, 0xFFFF
    je .entry
    push ax
    call _wvm_begin
    add sp, 2
    call drive
.entry:
    push word [c_entry]
    call _wvm_begin
    add sp, 2
    call drive                  ; AX = the last wvm_slice answer

    ; --- 3: the stop reason -------------------------------------------------
    mov dx, [c_err]
    cmp dx, 0xFFFF
    je .wantdone
    cmp ax, WR_ERR
    jne .no
    call _wvm_errcode
    cmp ax, dx
    jne .no
    jmp short .state
.wantdone:
    cmp ax, WR_DONE
    jne .no

    ; --- the end state, byte for byte (8.3) ---------------------------------
.state:
    mov ax, 4096
    push ax
    xor ax, ax
    push ax
    mov ax, SEG_SAVE
    push ax
    call _wvm_save
    add sp, 6
    cmp ax, [c_explen]
    jne .no
    mov cx, ax
    mov si, [c_exp]
    xor di, di
    push es
    mov ax, SEG_SAVE
    mov es, ax
    repe cmpsb
    pop es
    jne .no
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    call discipline
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; drive - run slices until the handler is finished, collecting whenever the
; core asks (WEAVE-SPEC 4.8: the collector runs BETWEEN slices, never inside
; one).  out: AX = the last answer.
drive:
    push cx
    mov cx, 20000               ; a bound, so a core that never finishes ends
.slice:                         ; the run instead of the gate
    push cx
    push word [budget]
    call _wvm_slice
    add sp, 2
    pop cx
    cmp ax, WR_MORE
    je .again
    cmp ax, WR_GC
    jne .out
    push ax
    call _wvm_gc
    pop ax
.again:
    loop .slice
.out:
    pop cx
    ret

; =============================================================================
; ONE RING CASE (WEAVE-SPEC 4.9)
; in:  SI = the row index.  out: AX = 1 agreed, 0 did not
; =============================================================================
runring:
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, si
    mov cx, WVR_ROW
    mul cx
    mov bx, ax
    add bx, wvr_tab
    mov ax, [bx]
    mov [nameptr], ax
    mov ax, [bx+2]
    mov [c_code], ax            ; the ops
    mov ax, [bx+4]
    mov [c_clen], ax            ; ...how many
    mov ax, [bx+6]
    mov [c_exp], ax
    mov word [negrow], 0
    mov ax, si
    inc ax
    cmp ax, WVR_N               ; the LAST ring row is the negative control
    jne .go
    mov word [negrow], 1
.go:
    ; a bind, purely to lay the claim out - no bytecode runs in these rows
    mov ax, 0x1234
    push ax
    mov ax, nblk
    push ax
    xor ax, ax
    push ax
    push ax
    push ax
    push ax
    push ax
    push ax
    mov ax, VM_BYTES
    push ax
    mov ax, SEG_VM
    push ax
    call _wvm_bind
    add sp, 20

    mov si, [c_code]
    mov cx, [c_clen]
.op:
    jcxz .check
    push cx
    push si
    cmp byte [si], 0
    jne .deq
    push word [si+6]            ; d2
    push word [si+4]            ; d1
    mov al, [si+2]
    xor ah, ah
    push ax                     ; atom
    mov al, [si+1]
    xor ah, ah
    push ax                     ; comp
    call _wvm_enq
    add sp, 8
    jmp short .opn
.deq:
    mov ax, rec
    push ax
    call _wvm_deq
    add sp, 2
.opn:
    pop si
    pop cx
    add si, 8
    dec cx
    jmp short .op

.check:
    mov si, [c_exp]
    mov cl, [si]                ; the expected count
    xor ch, ch
    inc si
    call _wvm_rcount
    cmp ax, cx
    jne .no
    jcxz .ok
.next:
    push cx
    push si
    mov ax, rec
    push ax
    call _wvm_deq
    add sp, 2
    pop si
    pop cx
    or ax, ax
    jz .no
    mov ax, [rec]               ; comp
    mov dl, [si]
    xor dh, dh
    cmp ax, dx
    jne .no
    mov ax, [rec+2]             ; atom
    mov dl, [si+1]
    xor dh, dh
    cmp ax, dx
    jne .no
    mov ax, [rec+4]
    cmp ax, [si+2]
    jne .no
    mov ax, [rec+6]
    cmp ax, [si+4]
    jne .no
    add si, 6
    loop .next
.ok:
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    call discipline
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; 5: THE DISCIPLINE CHECK - ES, DF and the stack, after every call out
; A routine that left ES on the claim would write into the kernel on a real
; machine and NOT fault (LESSONS.md 4); a routine that left DF set would run
; every later string instruction backwards.
; =============================================================================
discipline:
    push ax
    push bx
    pushf
    pop bx
    test bx, 0x0400             ; DF
    jnz .bad
    push es
    pop bx
    cmp bx, SEG_KERNEL
    jne .bad
    push ss
    pop bx
    cmp bx, SEG_STACK
    jne .bad
    pop bx
    pop ax
    ret
.bad:
    pop bx
    pop ax
    xor ax, ax                  ; the row fails, whatever it computed
    ret

; =============================================================================
; THE SERIAL PORT (COM1) - the harness's only output
; =============================================================================
putc:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    push dx
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

puts:
    push ax
    push si
.n:
    mov al, [cs:si]
    or al, al
    jz .d
    call putc
    inc si
    jmp short .n
.d:
    pop si
    pop ax
    ret

; putdec - AX, unsigned, on the wire.
putdec:
    push ax
    push bx
    push cx
    push dx
    mov cx, 0
    mov bx, 10
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.out:
    pop ax
    add al, '0'
    call putc
    loop .out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

section .data
msg_hello: db 'weavevm: the WJS VM in raw QEMU, SS != DS', 13, 10, 0
msg_ring:  db 13, 10, 'ring: ', 0
msg_tail:  db ' failures - weavevm OK', 13, 10, 0
msg_bad:   db ' FAILURES in weavevm', 13, 10, 0
msg_nl:    db 13, 10, 0

; --- THE SHIPPING CORE, %included and never copied --------------------------
; It sits between the harness's .data and its .bss because the block below is
; sized from WN_SIZE, and nasm evaluates a `resw` where it SEES it: a forward
; reference there is `-w+error=forward` at best and a scratch block of the
; wrong size under every native call at worst (weave.asm says the same thing
; about wdraw.inc's OS88LINE_SZ, and for the same reason).
section .text
%include "wvm.inc"

section .bss
budget:   resw 1
r1:       resw 1
r2:       resw 1
nameptr:  resw 1
negrow:   resw 1
c_code:   resw 1
c_clen:   resw 1
c_nfunc:  resw 1
c_atoms:  resw 1
c_natom:  resw 1
c_init:   resw 1
c_entry:  resw 1
c_exp:    resw 1
c_explen: resw 1
c_err:    resw 1
rec:      resw 4
nblk:     resw WN_SIZE / 2

section .text

; The includer's one obligation (wvm.inc's header). Nothing in the corpus
; reaches a component, a builtin that draws or a file - they are
; weavesession's - so a call to this one means the core sent something out
; that WEAVE-SPEC 8.1 says is pure, and the row must fail loudly rather than
; quietly returning null.
wvm_native:
    mov bx, nblk
    mov word [bx+WN_ERRC], 99
    mov ax, 1
    ret

; --- THE CORPUS, GENERATED (WEAVE-SPEC 12.1.1) ------------------------------
section .rodata
%include "weavevmcorp.inc"
section .text
