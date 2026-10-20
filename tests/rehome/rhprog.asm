; =============================================================================
; os8088 - tests/rehome/rhprog.asm
;
; THE PROGRAM: part 0 of REHOME.O88, and a whole `.o88` image in its own right
; - magic, version 3, link 0, the three-byte dispatcher, its own name, its own
; entry offset and its own bss. It is never a FILE, so nothing ever validates
; it as one; the kernel reaches it through OSAPI_PKG_REHOME (SPEC.md 20.12.10)
; and runs ld_start's step 8 against it exactly as if it had come off a disk.
;
; What it proves, in the four checks its window title counts:
;
;   1. THE HANDOFF ARRIVED. Its bss is not zeroed by the kernel - it ships
;      inside the part - so the loader wrote where it put things into the head
;      of it. Nothing was published, stamped or registered to do that
;      (SPEC.md 20.12.10.2).
;   2. THE OTHER PART IS WHERE THE LOADER SAID: the asset's signature, read out
;      of the segment the vector names.
;   3. IT CAN CLAIM MEMORY. This is SPEC.md 50.3.4's whole gate: the program's
;      segment is a PART, some paragraphs into the carve, so it is the base of
;      no claim - and without mem_own's two arms every memory slot on the
;      machine refuses it, AFTER a successful launch.
;   4. IT MAY NOT FREE OR UNPIN ITS OWN CARVE. The carve is re-owned to the
;      instance SLOT (SPEC.md 20.12.10.5), which is neither its segment nor its
;      base, so mem_find_own refuses both halves of its fence. A PASS HERE IS A
;      REFUSAL, and it is what keeps a block whose I_SPTR would go stale from
;      ever being declared movable.
;
; THE VERDICT IS THE WINDOW TITLE - `REHOMED 4/4 OK` - so the host reads a
; string out of the package's own segment rather than off the glass.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'REHOMED', rp_entry

; --- the handoff, at the HEAD of the bss (SPEC.md 20.12.10.2) ---------------
; The loader and the program are one package's own code and agree by
; construction. The kernel never sees this and has no opinion about it, which
; is the whole finding: the re-home needed no mechanism for it.
RP_HAND    equ 0                    ; word: 'RH' - the loader ran
RP_ASSET   equ 2                    ; word: the segment it put part 1 at
RP_CARVE   equ 4                    ; word: the carve's own base, for check 4
RP_HAND_SZ equ 6

RP_ROW_H    equ 10
RP_CLAIM_KB equ 4

; -----------------------------------------------------------------------------
; rp_entry - the program's entry proc, reached at ld_start step 8 for the
;            SECOND time on this launch (SPEC.md 20.12.10.1)
; in:  DS = CS = our segment, ES = KERNEL_SEG, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; SI is NOT to be read as the file we came out of: the kernel passes ld_lname
; either way, but this package came out of no file - the LOADER did, and it is
; gone.
; -----------------------------------------------------------------------------
rp_entry:
    push cx
    push dx
    push si
    push di
    push es
    call rp_check
%ifdef RH_ABORT
    ; --- THE ABORT ARM (`make rehome`'s third disk) -------------------------
    ; A RE-HOMED PROGRAM THAT REFUSES ITSELF, which is the one path nothing
    ; else reaches: by here the loader's region is freed, [ld_base] names US,
    ; and the carve is owned by the instance SLOT. ld_unreserve then sweeps
    ; XMS by record, heap by SLOT - which is what has to reach the carve - and
    ; heap by [ld_base], which is what reaches everything WE claimed. Same
    ; source as the passing arm on purpose: the checks above all run first, so
    ; a failure here cannot be a package that never got going.
    mov ax, RP_CLAIM_KB             ; ...and leave one of our OWN claims
    call OSAPI_MEM_CLAIM            ; outstanding, so the [ld_base] sweep has
    xor bx, bx                      ; something to find too
    stc
    jmp short .out
%endif
    mov si, rp_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; rp_check - the four checks, in the entry proc where the answer first exists
; out: [rp_ok] = how many passed, the title rewritten
; clobbers: everything but the caller's saved set
; -----------------------------------------------------------------------------
rp_check:
    mov byte [rp_ok], 0

    ; --- 1. the handoff arrived -------------------------------------------
    cmp word [rp_hand + RP_HAND], 'RH'
    jne .n1
    inc byte [rp_ok]
.n1:
    ; --- 2. the asset is where the loader said ----------------------------
    mov ax, [rp_hand + RP_ASSET]
    mov [rp_aseg], ax
    or ax, ax
    jz .n2
    mov es, ax
    cmp word [es:0], 'RA'           ; the asset's own signature...
    jne .n2
    cmp word [es:2], 0x5AA5         ; ...and a value only it carries
    jne .n2
    inc byte [rp_ok]
.n2:
    ; --- 3. IT CAN CLAIM MEMORY (SPEC.md 50.3.4) --------------------------
    ; The check that is RED without mem_own's arms, and the reason this fixture
    ; exists. The claim's last byte is written and read back too: a fence that
    ; answered CF=0 with a segment of nothing would otherwise pass.
    mov ax, RP_CLAIM_KB
    call OSAPI_MEM_CLAIM            ; out DX = base segment
    jc .n3
    mov [rp_cseg], dx
    mov es, dx
    mov word [es:RP_CLAIM_KB * 1024 - 2], 0x1234
    cmp word [es:RP_CLAIM_KB * 1024 - 2], 0x1234
    jne .n3f
    inc byte [rp_ok]
.n3f:
    mov dx, [rp_cseg]
    call OSAPI_MEM_FREE             ; ...and THIS must work: an ordinary data
                                    ; claim of ours, which is what check 4 is
                                    ; the other side of
.n3:
    ; --- 4. ...and it may not free or unpin its carve BY ITS BASE ----------
    ; THE CARVE HAS TWO SHAPES AND ONLY ONE OF THEM IS THIS FEATURE'S. When
    ; op_claim's head slack is zero - which is every 512-byte-cluster volume,
    ; so every 1.44MB disk - the part's segment IS the carve's base, and then
    ; the claim is the program's region in every sense: mem_is_region holds,
    ; mem_rr_tab rewrites I_SPTR on a move, and mem_find_own's `MC_SEG == the
    ; caller's own segment` arm reaches it exactly as it reaches any package's
    ; own region (SPEC.md 66.6.1). Freeing it is then the same right every
    ; package has and says nothing about the re-home, so DO NOT EXERCISE IT -
    ; a package that frees the region it is running in is not a test.
    ;
    ; **WITH A NON-ZERO SLACK THIS IS STILL A REFUSAL, AND THAT IS THE POINT
    ; OF THE GATE** (SPEC.md 66.6.1.2). The declaration below now takes in
    ; BOTH shapes - mem_find_own grew a containment arm - but that arm fires
    ; only when the caller names its OWN SEGMENT. What this check passes is
    ; the carve's BASE, which is not our segment and never was, so the fence
    ; answers exactly as it did: widened to a containment, not holed.
    mov dx, [rp_hand + RP_CARVE]
    or dx, dx
    jz .n4
    mov ax, ds
    cmp ax, dx
    je .n4ok                        ; at the base: see above
    call OSAPI_MEM_FREE
    jnc .n4                         ; it worked, which IS the failure
    mov dx, [rp_hand + RP_CARVE]
    xor ax, ax                      ; (the proc is never read: the fence
    call OSAPI_MEM_MOVABLE          ; refuses before it gets that far)
    jnc .n4
.n4ok:
    inc byte [rp_ok]
.n4:
    ; --- ...and DECLARE, which BOTH shapes accept now -----------------------
    ; SPEC.md 20.12.10.5, 66.6.1.2. It used to take only where the head slack
    ; was zero and the claim was our region in mem_is_region's old sense
    ; (`MC_SEG == I_SPTR`); with a non-zero slack the program sits INSIDE the
    ; carve and the declaration was refused - correctly, because four places in
    ; the compactor would have read the claim's base where they meant the
    ; segment we run in. All four take the offset now, so this takes either
    ; way and tests/rehomemove.py moves either way - its `360` arm being the
    ; shape that was pinned. `mov dx, ds` is load-bearing: naming the carve's
    ; BASE is still refused, which is what check 4 above asserts.
    push dx
    mov dx, ds                  ; OUR REGION - and `mov dx, cs` would do as
    mov ax, rp_reloc            ; well, this package being org 0 in one segment
    call OSAPI_MEM_MOVABLE
    pop dx

    ; --- the verdict, into the title --------------------------------------
    mov al, [rp_ok]
    add al, '0'
    mov [rp_t_n], al
    mov ax, 'OK'
    cmp byte [rp_ok], 4
    je .say
    mov ax, 'BA'
.say:
    mov [rp_t_v], ax
    ret

; -----------------------------------------------------------------------------
; rp_reloc - our relocation proc (SPEC.md 66.2)
; in:  BX = the base the region WAS at, DX = where it is now, DS = the NEW base
;      (mem_reloc_call sets it: "the proc's whole job is to write its own data")
; out: nothing
;
; NOT A `ret`, and this is the one package in the tree for which that is true
; by construction. apps/os88api.inc's OS88_REGION_MOVABLE ships a bare `ret`
; because every word naming an ordinary region is the KERNEL's - W_SEG,
; I_SPTR, the owner of every claim - and mem_region_reloc puts those right.
;
; A RE-HOMED PROGRAM HAS TWO OF ITS OWN. Its region is the parts carve, and
; the OTHER PARTS ARE IN IT: the loader's handoff named the asset by absolute
; segment (SPEC.md 20.12.10.2), and that segment moves with the block it is
; inside. Nothing else in the machine knows those words exist, so nothing else
; can fix them - which is precisely the case os88api.inc's "it is where YOUR
; fix-up goes if you ever cache your own segment in a word of your own" is
; about, arriving here for the first time.
;
; [rp_cseg] is deliberately NOT adjusted: it names a data claim OUTSIDE the
; carve, which mem_region_reloc does not move and which check 3 gave back
; anyway.
; -----------------------------------------------------------------------------
rp_reloc:
    push ax
    mov ax, dx
    sub ax, bx                  ; AX = the delta, in paragraphs
    add [rp_hand + RP_ASSET], ax
    add [rp_hand + RP_CARVE], ax
    add [rp_aseg], ax           ; ...and the copy the paint reads
    inc byte [rp_moved]         ; the row reads this: a proc that was declared
    pop ax                      ; and never called is the failure 66.2 is about
    ret

; -----------------------------------------------------------------------------
; rp_paint - the window's content (SPEC.md 20.4)
; in:  SI = our window ptr, the gfx lock HELD
; -----------------------------------------------------------------------------
rp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov [rp_cx], ax
    mov [rp_cy], dx
    mov ax, [rp_aseg]
    mov di, rp_l1s
    call rp_hex4
    mov ax, [rp_cseg]
    mov di, rp_l2s
    call rp_hex4
    mov si, rp_l1
    xor dx, dx
    call rp_line
    mov si, rp_l2
    mov dx, RP_ROW_H
    call rp_line
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; rp_line - the NUL string at SI, DX pixels down the content
rp_line:
    add dx, [rp_cy]
    add dx, 4
    mov cx, [rp_cx]
    add cx, 4
    mov ax, (CWHITE << 8) | CBLACK  ; an OPAQUE run: ground and glyphs in one
    call OSAPI_FONT_RUN             ; pass (SPEC.md 6.1)
    ret

; rp_hex4 - AX as four hex digits at DI
rp_hex4:
    mov cx, 4
.d:
    rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    push ax
    and al, 0Fh
    add al, '0'
    cmp al, '9'
    jbe .st
    add al, 7
.st:
    mov [di], al
    inc di
    pop ax
    loop .d
    ret

; -----------------------------------------------------------------------------
rp_tpl:
    dw 140, 90, 250, 60             ; x, y, w, h
    dw rp_title, rp_paint, 0, 0

rp_title:  db 'REHOMED '
rp_t_n:    db '0'
           db '/4 '
rp_t_v:    db 'OK', 0, 0

rp_l1:     db 'asset seg '
rp_l1s:    db '0000', 0
rp_l2:     db 'claim seg '
rp_l2s:    db '0000', 0

    OS88_BSS RP_BSS
    OS88_IMAGE_END

; THE HANDOFF IS THE FIRST THING IN THE BSS, which is what lets the loader
; write it knowing only LD_H_IMG (SPEC.md 20.12.10.2).
rp_hand    equ os88_image_end + 0
rp_ok      equ os88_image_end + RP_HAND_SZ
rp_aseg    equ os88_image_end + RP_HAND_SZ + 2
rp_cseg    equ os88_image_end + RP_HAND_SZ + 4
rp_cx      equ os88_image_end + RP_HAND_SZ + 6
rp_cy      equ os88_image_end + RP_HAND_SZ + 8
rp_moved   equ os88_image_end + RP_HAND_SZ + 10   ; byte: relocations taken
RP_BSS     equ RP_HAND_SZ + 12

; --- AND THE BSS SHIPS INSIDE THE PART (SPEC.md 51.1.2, one format along) ---
; A part is not a file and the kernel does not zero it - ld_start jumps to step
; 8 and not step 7, precisely so the loader's handoff survives - so the bytes
; have to BE here, and this is what puts them here. It is also what makes
; LD_H_IMG + LD_H_BSS the length the loader hands to OSAPI_PKG_REHOME.
    times RP_BSS db 0
