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
; CONTEXT: THE UI TASK OR A WINDOW CALLBACK - not a worker (SPEC.md 55.4).
; The three entry points take no lock, touch no VRAM and do no I/O, which is
; what the contract used to rest on, and that argument is about the wrong
; hazard. THE COPY IS THE HAZARD: clip_put and clip_get each `rep movsb` up
; to 32KB - ~117 ms on a 4.77 MHz 8088, two whole ticks - between TWO
; segments that may both move, the clipboard claim (it registers clip_reloc
; below) and the caller's document (Note Pad's and ArtfulType's are both
; OSAPI_MEM_MOVABLE claims). [mem_pinseg] is one word and covers the claim
; only, and mem_can_move's own header says a lock taken now "does NOT reach a
; task already suspended mid-access".
;
; What closes the window today is that a compaction cannot START unless some
; task calls mem_claim, a worker may not claim (SPEC.md 20.6 rule 7), and
; every shipped caller of all three slots is a menu command or a key handler
; on the UI task - which is the task blocked inside the copy. So the burst is
; safe by construction and NOT by the argument the header used to give. Widen
; the contract back and it stops being: the UI task would then be free to
; claim during a worker's 117 ms, and clip_reloc would dutifully correct the
; one word nobody is reading. The fix that day is a sch_lock bracket round
; both bursts (+16 bytes, measured); a chunked copy is NOT one, because
; nothing can repair the CALLER's segment - it passed a value, not a handle.
;
; What two tasks racing to copy cannot do is corrupt the claim table:
; mem_claim/mem_free run their own critical sections, and the loser simply
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
; clip_put - replace the clipboard's contents (API slot 0x0320)
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
    mov ax, cx                  ; AX = whole kilobytes, rounded up. CX carries
    cmp ax, CLIP_MAXKB * 1024   ; the byte count on, which is why the helper
    ja .toobig                  ; banks it; and the `never 0` clamp cannot
                                ; fire here, the jcxz above having already
                                ; answered that case. The ceiling is compared
                                ; on the ACCUMULATOR (3D imm16, 3 bytes) after
                                ; the load rather than on CX before it (81 F9
                                ; imm16, 4): the clipboard it already has is
                                ; still good either way - a refused put must
                                ; not destroy one - and AX being clobbered on
                                ; the .toobig path is harmless, that path
                                ; falling to .out and kret_bp's `pop ax`
    call mem_bytes_kb
    cmp al, [clip_kb]           ; a BYTE: mem_bytes_kb answers 1..32 here (the
                                ; ceiling above bounds it), so AH is 0
    je .room                    ; the same size: reuse the block outright
    call clip_drop              ; ...otherwise free THEN claim, because every
                                ; byte in the old block is about to be
                                ; overwritten and a regrow would need both
                                ; blocks free at once to preserve them
    mov bx, MEM_K_CLIP
    push word [mem_pinseg]      ; THE SOURCE IS PINNED ACROSS THE CLAIM (SPEC.md
    mov [mem_pinseg], bp        ; 66.3 rule 5): the caller's text is very
    call mem_claim              ; often a document claim that is itself
    pop word [mem_pinseg]       ; movable, this claim can compact, and the
                                ; copy of its segment in BP is on OUR stack -
                                ; a stale one no relocation proc can repair
    jc .toobig                  ; clip_drop already emptied it
    mov [clip_seg], dx
    mov [clip_kb], al           ; ...while AX still holds the size, so no bank
                                ; is needed round the call below
    mov ax, clip_reloc          ; ...and MOVABLE (SPEC.md 66.5.6): one word,
                                ; read immediately before each copy, and a
                                ; claim that outlives the app that filled it -
                                ; so it is long-lived by design and exactly
                                ; the shape that ends up mid-arena. BX IS
                                ; STILL MEM_K_CLIP from the claim above -
                                ; mem_claim's header is "clobbers: DX (an
                                ; output) and the flags", and files.inc:399
                                ; is the worked example that cost SPEC.md
                                ; 66.5.6.2 a shipped defect
    call COLD_SEG:mmf_mem_movable
.room:
    mov [clip_bytes], cx        ; while DS is still ours
    mov es, [clip_seg]
    mov ds, bp                  ; DS:SI = the caller's text, ES:DI = the claim
    xor di, di
    cld                         ; SPEC.md 1: every string op sets it
    rep movsb                   ; DS is the caller's for four more instructions
                                ; - .out's own `pop ds` restores the entry bank
                                ; and nothing between reads it, the two pops
                                ; addressing SS and kret_bp being a run of pops
                                ; and a ret. Do not re-add a `push cs / pop ds`
                                ; here: it wrote a register nobody read
    jmp short .ok
.empty:
    call clip_drop
.ok:                            ; both of these are the CF=0 answer, so one
    clc                         ; `clc` serves them; `.toobig` still enters at
    jmp short .out              ; its own `stc` below
.toobig:
    stc
.out:
    pop es                      ; POP touches no flags, so the answer above
    pop ds                      ; rides out through all nine of them
    jmp kret_bp

; -----------------------------------------------------------------------------
; clip_get - copy the clipboard into the caller's buffer (API slot 0x0328)
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
    call clip_size              ; the slot below IS this test: it answers
    jc .empty                   ; CF=1 with AX=0 when empty and CF=0 with the
                                ; length otherwise, which is the pair of facts
                                ; wanted here. One copy of it, not two
    cmp cx, ax
    jbe .fits
    mov cx, ax                  ; the buffer is bigger than the text
.fits:
    push cx
    mov ds, [clip_seg]          ; DS:SI = the clipboard, ES:DI the caller's
    xor si, si
    cld
    rep movsb                   ; AX is untouched from clip_size to the ret,
                                ; so the whole length rides out unbanked; and
                                ; DS stays the clipboard's for three more
                                ; instructions - `pop cx` addresses SS, `clc`
                                ; writes a flag, and .out's own `pop ds` is
                                ; two lines down. A `push cs / pop ds` here
                                ; wrote a register nobody read
    pop cx
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
; clip_size - how much text the clipboard holds (API slot 0x0330)
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
    cmp ax, 1                   ; the answer IS the compare: `cmp ax,1` borrows
    ret                         ; exactly when AX is 0 (the length is unsigned
                                ; and 0 is the only empty value), and it leaves
                                ; AX alone - which is the documented output,
                                ; CF=1 with AX=0 when empty and CF=0 with the
                                ; length otherwise. The other flags now say
                                ; something different; the contract is
                                ; "clobbers: AX (the output) and the flags",
                                ; so no caller may read them.

; -----------------------------------------------------------------------------
; clip_drop - give the block back and forget it (module-internal)
; in:  nothing; out: nothing (every register preserved; the FLAGS are not -
;      see the `ret` below, which is where the two call sites are audited)
; -----------------------------------------------------------------------------
clip_drop:
    push ax
    push bx
    push dx
    mov dx, [clip_seg]          ; loaded ONCE and tested, where the old code
    or dx, dx                   ; compared the word in memory and then loaded
    je .out                     ; the same word again
    mov bx, MEM_K_CLIP
    call mem_free               ; a refusal cannot happen (we hold it) and
                                ; would still leave the three fields below
                                ; consistent with an empty clipboard
    xor ax, ax                  ; the zero, staged AFTER the call rather than
    mov [clip_seg], ax          ; before it: three accumulator stores instead
    mov [clip_kb], al           ; of three 6-byte immediate ones, and it no
    mov [clip_bytes], ax        ; longer rests on mem_free preserving AX
.out:                           ; NOTHING IS STORED ON THIS PATH, because
                                ; clip_seg == 0 implies the other two are 0
                                ; already: .bss arrives zeroed, clip_put
                                ; writes clip_bytes only at .room (reached
                                ; only with a live block), a refused claim
                                ; falls to .toobig with all three just zeroed
                                ; here, and `je .room` needs [clip_kb] == AL
                                ; >= 1 which needs a live clip_seg
    pop dx
    pop bx
    pop ax
    ret                         ; no pushf/popf: both call sites are in
                                ; clip_put and neither reads the flags - the
                                ; first is followed by `mov bx,MEM_K_CLIP` and
                                ; a mem_claim that sets CF itself, the second
                                ; (.empty) falls into .ok's `clc`. The
                                ; contract line above is the whole contract,
                                ; this proc being module-internal

section .text

; clip_reloc - the compactor moved the clipboard (SPEC.md 66.2). One word:
; clip_put and clip_get each load it immediately before their rep movsb, and
; nothing derives anything from it.
clip_reloc:
    mov [clip_seg], dx          ; NO `cmp bx, [clip_seg]` GUARD, and it is not
    ret                         ; a defence that was dropped - it could never
                                ; fire. mem_reloc_call (memory.inc) dispatches
                                ; `mov bp,[ss:si+MC_RLOC]`, THE RECORD'S OWN
                                ; proc, with BX = that record's old base, so
                                ; BX == [clip_seg] by construction; MC_RLOC is
                                ; published as 0 by mem_claim_1, so a
                                ; freed-and-reused record cannot carry a stale
                                ; clip_reloc; and the clipboard holds at most
                                ; one claim at a time (clip_drop frees before
                                ; clip_put claims), so there is never a second
                                ; record naming this proc. dsk_dseg_reloc is
                                ; the one proc that runs for blocks it does
                                ; not own and it keeps its scan; this is not
                                ; that

section .bss
; All three arrive zeroed, which IS an empty clipboard - the boot image's
; .text/.ovl gap is emitted as zeros and the boot sector's one contiguous
; read lands it (kernel.asm, the boot overlay). Nothing here needs an
; init entry point, and clip_size answers correctly on a machine that has
; not copied anything yet.
clip_seg    resw 1              ; the claim's base segment, 0 = none held
clip_kb     resb 1              ; ...and its size in KB, so a put at the same
                                ; size allocates nothing. A BYTE: CLIP_MAXKB
                                ; is 32, so it holds 0..32 and every reader
                                ; wants AL. Nothing writes wider than its own
                                ; field here - the two `resw`s either side are
                                ; only ever stored as words to themselves
clip_bytes  resw 1              ; the text's length. Always <= clip_kb * 1024

section .text
