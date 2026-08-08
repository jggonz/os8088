; =============================================================================
; os8088 - clip.inc
;
; THE SYSTEM CLIPBOARD (SPEC.md 55): one text buffer, owned by the kernel,
; shared by every application on the machine.
;
; It is TEXT ONLY and that is a decision rather than a first step. A typed
; clipboard needs a format registry, a negotiation between the putter and the
; getter, and a rule for what happens when neither side understands the other
; - and the thing every app here actually wants to hand another app is a run
; of characters. Paint's bitmap clipboard stays Paint's own (SPEC.md 42),
; because a picture pasted into Note Pad has no meaning to negotiate about.
;
; THE BUFFER IS A HEAP CLAIM AND IT IS SIZED TO WHAT IS IN IT (SPEC.md 50.3).
; Nothing is reserved: a machine that has never copied anything pays nothing
; at all, a machine holding one line pays one kilobyte, and emptying the
; clipboard gives every byte back. The alternative - a fixed buffer in .bss -
; would have cost KERN_BUDGET its size on every machine forever, for a
; feature most sessions never use, and would still have had a ceiling.
;
; A put that changes the SIZE frees the old block before it claims the new
; one, which is the opposite of what mem_regrow is for and is right here:
; a regrow preserves bytes, and every byte of the old clipboard is about to
; be overwritten. Preserving them would need the old block and the new one
; free AT ONCE, which is exactly the refusal mem_regrow exists to avoid.
; A put at the same whole-kilobyte size allocates nothing.
;
; The owner word is MEM_K_CLIP, a kernel tag, so:
;   - it is NOT freed when the app that copied closes (SPEC.md 50.4's
;     teardown fence walks an instance slot, and this is not one). Copy in
;     Note Pad, close Note Pad, paste in ArtfulType - the point of the
;     feature;
;   - the Task Manager counts it under System with the other 0xFFxx tags,
;     with no change needed there (mem_sum_kb tests the high byte).
;
; CONTEXT: any. The three entry points take no lock, touch no VRAM and do no
; I/O, so a worker task may call them (SPEC.md 20.6 rule 7). What they do
; touch is the claim table, and mem_claim/mem_free run their own critical
; sections, so two tasks racing to copy cannot corrupt it - the loser simply
; overwrites the winner's text, which is what one clipboard means.
; =============================================================================

section .text

CLIP_MAXKB equ 32               ; the ceiling, KB. Not arbitrary: the largest
                                ; document in this system is ArtfulType's
                                ; 60KB (SPEC.md 46.9) and the largest note is
                                ; 16KB (27.6), so 32 holds any realistic
                                ; selection while staying a size a 128KB
                                ; machine can still refuse honestly. It also
                                ; keeps CLIP_MAXKB * 1024 inside a word,
                                ; which is what the length check compares

; -----------------------------------------------------------------------------
; clip_put - replace the clipboard's contents (API slot 0x0310)
;
; in:       ES:SI = the text, CX = its length in bytes. CX = 0 EMPTIES the
;           clipboard, which is a real operation and not an error
; out:      CF = 0 taken; CF = 1 refused - too long, or the heap could not
;           fund it, in which case the clipboard is left EMPTY rather than
;           stale (the old block was already given back)
; clobbers: nothing but the flags
;
; ES:SI and not "the caller's DS", because the text an app wants to copy is
; very often not in the app's own segment: Note Pad's document is a heap
; claim of its own (SPEC.md 27.6) and so is ArtfulType's. A far pointer the
; caller builds costs one `mov es` at the call site and reaches anything;
; an X stub would have reached the package's image and nothing else.
; -----------------------------------------------------------------------------
clip_put:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov bp, es                  ; bank the caller's segment: the copy runs
                                ; with DS pointed at it, and by then nothing
                                ; in this module is readable any more
    jcxz .empty
    cmp cx, CLIP_MAXKB * 1024
    ja .toobig                  ; the clipboard it already has is still good:
                                ; a refused put must not destroy one
    mov ax, cx
    add ax, 1023
    push cx
    mov cl, 10
    shr ax, cl                  ; AX = whole kilobytes, rounded up
    pop cx
    cmp ax, [clip_kb]
    je .room                    ; the same size: reuse the block outright
    call clip_drop              ; ...otherwise free THEN claim, because every
                                ; byte in the old block is about to be
                                ; overwritten and a regrow would need both
                                ; blocks free at once to preserve them
    mov bx, MEM_K_CLIP
    call mem_claim              ; out CF = 0, DX = base segment
    jc .toobig                  ; clip_drop already emptied it
    mov [clip_seg], dx
    mov [clip_kb], ax
.room:
    mov [clip_bytes], cx        ; while DS is still ours
    mov ax, [clip_seg]
    mov es, ax
    mov ds, bp                  ; DS:SI = the caller's text, ES:DI = the claim
    xor di, di
    cld                         ; SPEC.md 1: every string op sets it
    rep movsb
    push cs
    pop ds                      ; ...and back, before anything else is read
    clc
    jmp short .out
.empty:
    call clip_drop
    clc
    jmp short .out
.toobig:
    stc
.out:
    pop es                      ; POP touches no flags, so the answer above
    pop ds                      ; rides out through all nine of them
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; clip_get - copy the clipboard into the caller's buffer (API slot 0x0318)
;
; in:       ES:DI = the destination, CX = its capacity in bytes
; out:      CF = 1 the clipboard is empty (AX = CX = 0); CF = 0 with
;           AX = the clipboard's WHOLE length and CX = the bytes actually
;           copied, which is min(capacity, length)
; clobbers: AX and CX (both outputs) and the flags
;
; Two numbers out, because a caller that asked with too small a buffer has to
; be able to tell. The honest ordering is clip_size first, make room, then
; this - but a caller that skips that gets a truncated paste rather than a
; corrupted anything.
; -----------------------------------------------------------------------------
clip_get:
    push bx
    push dx
    push si
    push di
    push ds
    mov ax, [clip_bytes]
    or ax, ax
    jz .empty
    cmp cx, ax
    jbe .fits
    mov cx, ax                  ; the buffer is bigger than the text
.fits:
    push ax
    push cx
    mov ax, [clip_seg]
    mov ds, ax                  ; DS:SI = the clipboard, ES:DI the caller's
    xor si, si
    cld
    rep movsb
    push cs
    pop ds
    pop cx
    pop ax
    clc
    jmp short .out
.empty:
    xor cx, cx
    stc
.out:
    pop ds
    pop di
    pop si
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; clip_size - how much text the clipboard holds (API slot 0x0320)
;
; in:       nothing
; out:      CF = 1 and AX = 0 when it is empty; else CF = 0 and AX = the
;           length in bytes
; clobbers: AX (the output) and the flags
;
; The call a paste makes FIRST, so it can grow its own document before it
; asks for the bytes - which on a machine with a claim heap is the difference
; between a paste that works and one that half-arrives.
; -----------------------------------------------------------------------------
clip_size:
    mov ax, [clip_bytes]
    or ax, ax
    jz .empty
    clc
    ret
.empty:
    stc
    ret

; -----------------------------------------------------------------------------
; clip_drop - give the block back and forget it (module-internal)
; in:  nothing; out: nothing (every register AND the flags preserved)
; -----------------------------------------------------------------------------
clip_drop:
    pushf
    push ax
    push bx
    push dx
    cmp word [clip_seg], 0
    je .out
    mov dx, [clip_seg]
    mov bx, MEM_K_CLIP
    call mem_free               ; a refusal cannot happen (we hold it) and
                                ; would still leave the three words below
                                ; consistent with an empty clipboard
    mov word [clip_seg], 0
    mov word [clip_kb], 0
.out:
    mov word [clip_bytes], 0
    pop dx
    pop bx
    pop ax
    popf
    ret

section .bss
; All three arrive zeroed, which IS an empty clipboard - the boot image's
; .text/.ovl gap is emitted as zeros and the boot sector's one contiguous
; read lands it (kernel.asm, the boot overlay). Nothing here needs an
; init entry point, and clip_size answers correctly on a machine that has
; not copied anything yet.
clip_seg    resw 1              ; the claim's base segment, 0 = none held
clip_kb     resw 1              ; ...and its size in KB, so a put at the same
                                ; size allocates nothing
clip_bytes  resw 1              ; the text's length. Always <= clip_kb * 1024

section .text
