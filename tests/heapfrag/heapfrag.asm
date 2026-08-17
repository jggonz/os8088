; =============================================================================
; os8088 - tests/heapfrag/heapfrag.asm
;
; HEAPFRAG, the heap-compaction gate package (SPEC.md 66). Like filetest,
; fmtest and sbtest it never ships - it gets its own scratch image:
;
;   make test TESTAPPS=build/heapfrag.img
;   make marty TESTAPPS=build/heapfrag.img        (the one that matters)
;
; WHY THIS EXISTS AT ALL, and why it verifies CONTENTS rather than outcomes.
; A compactor that moves bytes to the right place and a compactor that moves
; the WRONG bytes both report a successful claim, and both leave a machine
; that runs perfectly until something reads its buffer an hour later. So the
; suite fills every block with a pattern that depends on the block's index AND
; on the offset inside it, and checks every word back after the move. A
; block-for-block swap, a copy that lost a segment step, an off-by-a-paragraph
; destination and a short copy all read as a different word.
;
; It is also the FIRST ADOPTER of OSAPI_MEM_MOVABLE, which is deliberate: the
; commit that introduced the mechanism changed the behaviour of nothing that
; ships, because the default is PINNED (SPEC.md 66.2) and nothing shipped
; declares itself. This package is what proves the mechanism works at all.
;
; THE SUITE RUNS ON THE FIRST W_PAINT AND NOT IN THE ENTRY PROC, which is not
; a style choice. mem_can_move pins a claim owned by a segment it cannot find
; an idle instance for, and a package's I_SPTR is published at loader step 9 -
; AFTER the entry proc returns (SPEC.md 21/50.3). Run from the entry, every
; claim this package makes is correctly pinned, the compactor moves nothing,
; and the suite fails for a reason that has nothing to do with the code under
; test.
;
; Prefix hf_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'HEAPFRAG', hf_entry

HF_N      equ 8               ; blocks in the comb - and the ceiling, because
                              ; MEM_OWNER_MAX is 8 claims per owner (SPEC.md
                              ; 50.2) and the final claim needs a record too,
                              ; which is why exactly half the comb is freed
                              ; before it is asked for
HF_MINBLK equ 4               ; ...and the fewest worth proceeding on: three
                              ; survivors and three holes is the smallest comb
                              ; that can say anything. FEWER THAN HF_N IS
                              ; NORMAL and the suite must not treat it as a
                              ; failure - measured on a 5150, asking for L/8
                              ; eight times over gets SEVEN, because L counts
                              ; the purgeable read-ahead window as free
                              ; (SPEC.md 50.6.3, correctly - mem_claim sheds
                              ; it) and shedding it leaves the arena a little
                              ; smaller than the figure that sized the comb
HF_MINKB  equ 64              ; below this there is no room to build a comb
                              ; that proves anything, and the suite says so
                              ; rather than passing vacuously
HF_ROWS   equ 13
HF_BSS_TOTAL equ 128

; -----------------------------------------------------------------------------
; hf_entry - just the window; the suite runs on the first paint (see the header)
; -----------------------------------------------------------------------------
hf_entry:
    push si
    mov si, hf_tpl
    call OSAPI_WM_CREATE
    mov [hf_win], bx            ; the worker needs it (SPEC.md 20.6)
    pop si
    ret

; -----------------------------------------------------------------------------
; hf_worker - a worker that does nothing but exist (SPEC.md 66.5)
;
; THE SUITE IS RUN WITH ONE ALIVE ON PURPOSE. Without a worker, mem_can_move
; passes this package's claims on the strength of I_TASK = 0xFF alone and the
; park is never exercised - so the gate would pass against a kernel whose park
; handshake did not work at all, which is the whole thing 66.5 adds. With one,
; every move in checks 7..11 has had to go through the request, the wait and
; the acknowledgement at OSAPI_TASK_ALIVE.
;
; It sleeps a tick between passes, which is the ordinary worker shape and is
; also the case worth covering: a worker asleep when the request goes up has
; to wake and reach ALIVE inside INST_PARKW, and this one does.
; -----------------------------------------------------------------------------
hf_worker:
    mov bx, [hf_win]
    call OSAPI_TASK_ALIVE       ; ...and this is where it parks
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp short hf_worker

; -----------------------------------------------------------------------------
; hf_reloc - THE RELOCATION PROC (SPEC.md 66.2/66.3)
; in:  BX = the base segment the block WAS at, DX = the base it is at NOW,
;      DS = CS = our segment, ES = KERNEL_SEG. The bytes have already moved.
; out: nothing; every register preserved
;
; An ordinary near proc with a near `ret`, called through this package's own
; dispatcher exactly as W_PAINT is. It claims nothing, frees nothing, yields
; nothing and draws nothing - SPEC.md 66.3 rule 3, and the whole reason this
; one is three instructions of work.
;
; It also RECORDS what it was told, because the suite checks the notification
; and not just the outcome: a compactor that moved a block and forgot to call
; its holder is the failure that leaves a package reading its old address, and
; from inside the package that is indistinguishable from not having moved.
; -----------------------------------------------------------------------------
hf_reloc:
    push ax
    push cx
    push si
    mov si, hf_base
    xor cx, cx
.scan:
    cmp [si], bx                ; which of ours was it?
    jne .next
    mov [si], dx                ; the ONE word this package derives anything
                                ; from - a holder with more than one, or with
                                ; pointers computed off the base, puts them
                                ; all right here (SPEC.md 66.3 rule 2)
    inc word [hf_nrel]
    mov [hf_lastold], bx
    mov [hf_lastnew], dx
    jmp short .out
.next:
    add si, 2
    inc cx
    cmp cx, HF_N
    jb .scan
    inc word [hf_nbad]          ; told about a block that is not ours: the
                                ; record was reused and MC_RLOC went with it
.out:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hf_run - the suite
; in:  nothing (UI-task/callback context, gfx lock held by W_PAINT)
; out: nothing; hf_res / hf_n filled
; -----------------------------------------------------------------------------
hf_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    ; --- 1. hire the worker the park exists for -----------------------------
    mov ax, hf_worker
    mov bx, [hf_win]
    call OSAPI_TASK_SPAWN       ; UI-task callback with the lock held, which
    jc .noworker                ; is this slot's contract (SPEC.md 20.6)
    call hf_pass
    jmp short .room
.noworker:
    call hf_fail                ; refusal is normal for a package and FATAL for
    jmp .done                   ; this suite: without a worker it proves 66.5
                                ; nothing at all, so it says so and stops
.room:

    ; --- 2. is there room to build a comb that proves anything? --------------
    call OSAPI_MEM_AVAIL        ; AX = largest free run KB, BX = total free KB
    mov [hf_l0], ax
    cmp ax, HF_MINKB
    jb .toosmall
    mov cl, 3
    shr ax, cl                  ; S = L / HF_N
    or ax, ax
    jz .toosmall
    mov [hf_s], ax
    call hf_pass

    ; --- 2. claim the comb, filling the largest run --------------------------
    xor di, di
.claim:
    mov ax, [hf_s]
    call OSAPI_MEM_CLAIM        ; DX = base segment
    jc .claimdone               ; a refusal is where the comb ENDS, not where
                                ; the suite does - see HF_MINBLK
    mov bx, di
    shl bx, 1
    mov [bx+hf_base], dx
    mov [bx+hf_base0], dx       ; ...and where it STARTED, for check 9
    inc di
    cmp di, HF_N
    jb .claim
.claimdone:
    mov [hf_got], di
    cmp di, HF_MINBLK
    jb .combshort
    mov ax, di                  ; the PINNED block is the highest EVEN index
    dec ax                      ; we hold: it has to be one of the survivors,
    and ax, 0xFFFE              ; and which index that is depends on how many
    mov [hf_pin], ax            ; blocks the heap actually funded
    call hf_pass
    jmp short .fill0
.combshort:
    call hf_fail
    jmp .done
.fill0:

    ; --- 3. fill every block, then read it all back: our own pattern first ----
    xor di, di
.fill:
    call hf_fill
    inc di
    cmp di, [hf_got]
    jb .fill
    xor di, di
.vfy0:
    call hf_check
    jc .vfy0bad
    inc di
    cmp di, [hf_got]
    jb .vfy0
    call hf_pass
    jmp short .decl
.vfy0bad:
    call hf_fail                ; the harness is broken, not the kernel - and
    jmp .done                   ; saying so here is the difference between a
                                ; gate and a rumour

    ; --- 4. declare them movable, except HF_PIN ------------------------------
.decl:
    xor di, di
.dloop:
    mov bx, di
    shl bx, 1
    mov dx, [bx+hf_base]
    mov ax, hf_reloc
    cmp di, [hf_pin]
    jne .dset
    xor ax, ax                  ; AX = 0 PINS it again (SPEC.md 66.2), which is
                                ; a legal call and must answer CF = 0
.dset:
    call OSAPI_MEM_MOVABLE
    jc .dfail
    inc di
    cmp di, [hf_got]
    jb .dloop
    call hf_pass
    jmp short .free
.dfail:
    call hf_fail
    jmp .done

    ; --- 5. break the comb: free every other block ---------------------------
.free:
    mov di, 1
.floop:
    mov bx, di
    shl bx, 1
    mov dx, [bx+hf_base]
    call OSAPI_MEM_FREE
    jc .ffail
    mov word [bx+hf_base], 0    ; gone: hf_reloc must never be told about it
    add di, 2
    cmp di, [hf_got]
    jb .floop
    call hf_pass
    jmp short .frag
.ffail:
    call hf_fail
    jmp .done

    ; --- 6. ...and prove the heap really IS fragmented now -------------------
    ;
    ; Without this the suite can pass on a heap that never needed compacting:
    ; if some other run is big enough, check 7's claim succeeds on its own and
    ; proves nothing at all. THE ASSERTION IS THAT THE ANSWER IS NO before it
    ; is yes.
.frag:
    call OSAPI_MEM_AVAIL        ; AX = largest run, BX = total free
    mov [hf_l1], ax
    mov [hf_tot], bx
    inc ax                      ; ONE KB MORE THAN THE BIGGEST HOLE ANYWHERE,
    mov [hf_want], ax           ; which is the whole assertion in one number:
                                ; no single run can satisfy it, so a grant can
                                ; only have come from closing holes.
                                ;
                                ; It was 2S, and that is a figure about the
                                ; COMB rather than about the heap - fine on a
                                ; machine where the comb is the only thing in
                                ; the arena, and silently vacuous the moment
                                ; something else leaves a bigger hole
                                ; somewhere: measured with Paint running, 2S
                                ; was 66KB against a 174KB hole above it, so
                                ; the claim was granted outright and the run
                                ; proved nothing at all
    cmp ax, [hf_tot]
    ja .fragno                  ; ...and it must be fundable AT ALL: total free
    call hf_pass                ; below the ask means this machine cannot
    jmp short .big              ; demonstrate the difference, whatever the
.fragno:                        ; compactor does
    call hf_fail

    ; --- 7. the claim only compaction can satisfy ----------------------------
.big:
    ; THE GFX LOCK IS DELIBERATELY HELD ACROSS THIS CLAIM. The suite runs
    ; inside W_PAINT, so this is the WORST case for SPEC.md 66.5.3 - a worker
    ; that draws is blocked in gfx_lock right now and cannot reach
    ; OSAPI_TASK_ALIVE - and that is exactly what OSAPI_MEM_PARKSAFE (66.5.4)
    ; exists to survive. Dropping the lock here would make this gate pass
    ; against a kernel with no park-at-the-lock at all, which is the one thing
    ; it is here to prove.
    mov ax, [hf_want]
    call OSAPI_MEM_CLAIM
    jc .bigfail
    mov [hf_bigseg], dx
    call hf_pass
    jmp short .vfy1
.bigfail:
    call hf_fail

    ; --- 8. every surviving block still holds what it held -------------------
.vfy1:
    xor di, di
.vloop:
    mov bx, di
    shl bx, 1
    cmp word [bx+hf_base], 0
    je .vnext                   ; one we freed
    call hf_check
    jc .vbad
.vnext:
    add di, 2
    cmp di, [hf_got]
    jb .vloop
    call hf_pass
    jmp short .pinned
.vbad:
    mov [hf_badblk], di
    call hf_fail

    ; --- 9. the pinned block did not move ------------------------------------
.pinned:
    mov bx, [hf_pin]
    shl bx, 1
    mov ax, [bx+hf_base]
    cmp ax, [bx+hf_base0]
    jne .pinmoved
    call hf_pass
    jmp short .moved
.pinmoved:
    call hf_fail

    ; --- 10. ...and at least one movable block DID ---------------------------
    ;
    ; The other half of check 9, and the one that catches a compactor wired in
    ; but never reached: with nothing moving, checks 8 and 9 both pass.
.moved:
    xor di, di
    xor cx, cx
.mloop:
    mov bx, di
    shl bx, 1
    cmp word [bx+hf_base], 0
    je .mnext
    mov ax, [bx+hf_base]
    cmp ax, [bx+hf_base0]
    je .mnext
    inc cx
.mnext:
    add di, 2
    cmp di, [hf_got]
    jb .mloop
    mov [hf_nmoved], cx
    or cx, cx
    jz .nomove
    call hf_pass
    jmp short .rel
.nomove:
    call hf_fail

    ; --- 11. the holder was TOLD, once per move, and never about a stranger ---
.rel:
    mov cx, [hf_nmoved]
    cmp cx, [hf_nrel]
    jne .relbad
    cmp word [hf_nbad], 0
    jne .relbad
    call hf_pass
    jmp short .done
.relbad:
    call hf_fail
    jmp short .done

.toosmall:
    call hf_fail                ; not enough heap to say anything: a row that
                                ; reads FAIL rather than a suite that quietly
                                ; reports nothing
.done:
    cmp word [hf_bigseg], 0     ; GIVE THE BIG CLAIM BACK, always. It has done
    je .out                     ; its job the instant check 7 answered, and
    mov dx, [hf_bigseg]         ; holding the last large run of the machine
    call OSAPI_MEM_FREE         ; for the rest of the session means nothing
    mov word [hf_bigseg], 0     ; else can launch behind this package - which
                                ; is what tests/paintmove.py needs: a REAL
                                ; holder loaded above the comb, so that
                                ; closing this package opens a hole UNDER it
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; hf_fill / hf_check - write and re-read block DI's pattern
; in:  DI = block index; [hf_base+DI*2] = its base, [hf_s] = KB
; out: hf_check: CF = 1 a word came back wrong; hf_fill: nothing
;      both preserve every register except the flags
;
; SEGMENT-STEPPED, 1KB at a time, for mem_bcopy's own reason (SPEC.md 18.4.1):
; L/8 on a 640KB machine is past 64KB, and a 16-bit offset walking the block
; would wrap and check the same kilobyte sixteen times over while reporting a
; pass.
;
; The word at index w is `w + idx*0x0101`, so it varies with the OFFSET as
; well as the block. A constant per block would pass a copy that shifted the
; contents inside it, and a pattern with no block term would pass two blocks
; swapped.
; -----------------------------------------------------------------------------
hf_fill:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, di
    shl bx, 1
    mov dx, [bx+hf_base]        ; DX = the live base (it may have moved)
    mov bx, di
    mov ax, bx
    mov cl, 8
    shl ax, cl
    add ax, bx                  ; AX = idx * 0x0101
    mov bx, ax                  ; BX = the block term
    xor si, si                  ; SI = the running word index
    mov cx, [hf_s]              ; CX = kilobytes left
.kb:
    mov es, dx
    xor di, di
    push cx
    mov cx, 512                 ; 512 words = 1KB
.w:
    mov ax, si
    add ax, bx
    cld
    stosw
    inc si
    loop .w
    pop cx
    add dx, 64                  ; ...and the base steps a kilobyte
    loop .kb
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hf_check:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bx, di
    shl bx, 1
    mov dx, [bx+hf_base]
    mov bx, di
    mov ax, bx
    mov cl, 8
    shl ax, cl
    add ax, bx
    mov bx, ax
    xor si, si
    mov cx, [hf_s]
.kb:
    mov es, dx
    xor di, di
    push cx
    mov cx, 512
.w:
    mov ax, si
    add ax, bx
    cmp ax, [es:di]
    jne .bad
    add di, 2
    inc si
    loop .w
    pop cx
    add dx, 64
    loop .kb
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.bad:
    pop cx
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret

; --- hf_pass / hf_fail - record one result ------------------------------------
hf_pass:
    push bx
    mov bx, [hf_n]
    cmp bx, HF_ROWS
    jae .out
    mov byte [bx+hf_res], 0
    inc word [hf_n]
.out:
    pop bx
    ret

hf_fail:
    push bx
    mov bx, [hf_n]
    cmp bx, HF_ROWS
    jae .out
    mov byte [bx+hf_res], 1
    inc word [hf_n]
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; hf_paint - W_PAINT: run the suite once, then one row per check
; in:  SI = window ptr (content white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
hf_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    cmp byte [hf_done], 0
    jne .draw
    mov byte [hf_done], 1       ; ...before the run, not after: a suite that
    push si                     ; faulted must not run again on the repaint
    call hf_run
    pop si
.draw:
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    add ax, 6
    mov [hf_x], ax
    add dx, 6
    mov bp, dx
    mov al, CBLACK
    call OSAPI_SET_COLOR
    xor di, di
.row:
    cmp di, [hf_n]
    jae .figs
    mov ax, di
    inc ax
    call hf_num2
    mov cx, [hf_x]
    mov dx, bp
    mov si, hf_num
    call OSAPI_FONT_STR
    mov si, hf_s_pass
    cmp byte [di+hf_res], 0
    je .label
    mov si, hf_s_fail
.label:
    mov cx, [hf_x]
    add cx, 28
    mov dx, bp
    call OSAPI_FONT_STR
    mov bx, di                  ; ...and what the check actually was, because
    shl bx, 1                   ; a column of PASS/FAIL is unreadable from a
    mov si, [bx+hf_lbl_t]       ; screenshot two weeks later. SCALED: the
    mov cx, [hf_x]              ; labels are words where hf_res is bytes, and
    add cx, 68                  ; DI indexes both
    mov dx, bp
    call OSAPI_FONT_STR
    add bp, 10
    inc di
    jmp short .row
.figs:
    add bp, 4
    mov si, hf_s_kb             ; the four figures the suite turns on
    mov cx, [hf_x]
    mov dx, bp
    call OSAPI_FONT_STR
    add bp, 10
    mov ax, [hf_l0]
    call hf_fig
    mov ax, [hf_s]
    call hf_fig
    mov ax, [hf_l1]
    call hf_fig
    mov ax, [hf_want]
    call hf_fig
    mov ax, [hf_nmoved]
    call hf_fig
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; hf_fig - one figure per line, in: AX; uses BP as the pen y
hf_fig:
    push cx
    push dx
    push si
    call hf_num5
    mov cx, [hf_x]
    add cx, 12
    mov dx, bp
    mov si, hf_num
    call OSAPI_FONT_STR
    add bp, 10
    pop si
    pop dx
    pop cx
    ret

; hf_num2 / hf_num5 - AX to hf_num, NUL-terminated, right-aligned
hf_num2:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    mov cx, 2
    jmp short hf_numgo
hf_num5:
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    mov cx, 5
hf_numgo:
    mov si, hf_num
    add si, cx
    mov byte [si], 0
.d:
    dec si
    xor dx, dx
    div bx
    add dl, '0'
    mov [si], dl
    loop .d
    pop dx
    pop cx
    pop bx
    pop ax
    ret

hf_tpl:
    dw 140, 26, 250, 250
    dw hf_ttl, hf_paint, 0, 0

hf_ttl:    db 'Heap Compaction', 0
hf_s_pass: db 'PASS', 0
hf_s_fail: db 'FAIL', 0
hf_s_kb:   db 'L0 S L1 want moved:', 0

hf_l_wrk:  db 'worker', 0
hf_l_room: db 'room', 0
hf_l_claim:db 'comb', 0
hf_l_fill: db 'pattern', 0
hf_l_decl: db 'declare', 0
hf_l_free: db 'break', 0
hf_l_frag: db 'is frag', 0
hf_l_big:  db 'big claim', 0
hf_l_vfy:  db 'contents', 0
hf_l_pin:  db 'pin held', 0
hf_l_move: db 'did move', 0
hf_l_rel:  db 'told once', 0
hf_l_none: db '-', 0

; one label per check, in the order hf_run records them
hf_lbl_t:
    dw hf_l_wrk, hf_l_room, hf_l_claim, hf_l_fill, hf_l_decl, hf_l_free
    dw hf_l_frag, hf_l_big, hf_l_vfy, hf_l_pin, hf_l_move, hf_l_rel
    dw hf_l_none

    OS88_BSS HF_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -----------------------------------
hf_n       equ os88_image_end + 0    ; word: checks recorded
hf_x       equ os88_image_end + 2    ; word: paint scratch, text left
hf_s       equ os88_image_end + 4    ; word: one comb block, KB
hf_l0      equ os88_image_end + 6    ; word: largest run before
hf_l1      equ os88_image_end + 8    ; word: ...and with the comb broken
hf_want    equ os88_image_end + 10   ; word: what check 7 asks for
hf_got     equ os88_image_end + 12   ; word: blocks actually claimed
hf_bigseg  equ os88_image_end + 14   ; word: check 7's block
hf_nrel    equ os88_image_end + 16   ; word: relocation calls seen
hf_nbad    equ os88_image_end + 18   ; word: ...about a block that is not ours
hf_nmoved  equ os88_image_end + 20   ; word: blocks whose base changed
hf_badblk  equ os88_image_end + 22   ; word: the block that failed check 8
hf_lastold equ os88_image_end + 24   ; word: the last (old, new) we were told
hf_lastnew equ os88_image_end + 26   ; word:
hf_pin     equ os88_image_end + 30   ; word: the block deliberately left pinned
hf_win     equ os88_image_end + 88   ; word: our window, for the worker
hf_tot     equ os88_image_end + 90   ; word: total free KB when the comb broke
hf_done    equ os88_image_end + 28   ; byte: the suite has run
hf_pad     equ os88_image_end + 29   ; byte:
hf_num     equ os88_image_end + 32   ; 8 bytes: the number formatter
hf_res     equ os88_image_end + 40   ; HF_ROWS result bytes, 0 = PASS
hf_base    equ os88_image_end + 56   ; HF_N words: each block's LIVE base
hf_base0   equ os88_image_end + 72   ; HF_N words: ...and where it started
