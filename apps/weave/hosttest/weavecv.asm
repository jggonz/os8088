; =============================================================================
; os8088 - apps/weave/hosttest/weavecv.asm
;
; A BOOT SECTOR THAT RUNS THE SHIPPING apps/weave/wspr.inc AND
; apps/weave/wwork.inc ON A REAL x86, WITH SS != DS AND NOTHING ELSE ON THE
; MACHINE, and diffs them case by case against tools/weavesim.py's canvas
; model (WEAVE-SPEC 12.1.3, 12.3).
;
; apps/weave/hosttest/weavevm.asm is the shape and the precedent, and what
; that header says about %include applies here word for word: WHAT RUNS IS THE
; SHIPPING TEXT of the composer and the frame loop, not a copy of it, and it
; runs with no kernel, no window, no gfx lock and no claim under it.
;
; -----------------------------------------------------------------------------
; WHY IT EXISTS, AND IT IS NOT THE REASON weavevm EXISTS
; -----------------------------------------------------------------------------
; Every other differential in this family has an oracle that was already
; there: weavevm diffs two interpreters' end states, weavegrid diffs the band
; composer against the model's own band(), weavegfx diffs a card against
; `--render`. WEAVE-SPEC 6.10.2's composition has NONE of that, because the
; model deliberately does not draw pixels and the canvas's buffer is not on
; any card - a sprite composed one byte to the left, or a dirty run one band
; too short, is invisible in every screenshot this family takes and shows up
; as a game that "flickers a bit". So the model grew a composer written from
; 6.10.2 and from nothing else, and this is the machine's half of it.
;
; WEAVE-SPEC 13.1 makes it the FIRST gate of wave 5, for wave 3's and wave 4's
; reason: a canvas wired to an unverified composer reports its defects as
; component defects.
;
; WHAT EACH CASE CHECKS, and none of it is the core's logic restated:
;
;   1. THE SPRITE RECORDS after N frames - the 1/16-px accumulators, the
;      pixel positions, the velocities after every bounce, the score latch and
;      the frame nibbles - against the model's 6.10.1 arithmetic. The
;      arithmetic shift is the thing most likely to disagree, and `negx`,
;      `bounce-t` and `subpixel` are the three cases that would catch an
;      IDIV where a SAR belongs;
;   2. THE STAGING RING (6.10.6) record for record: onwall's edge codes,
;      onscore's, oncollide's two comp_ids, ontick's collapse, and
;      `ring-flood`'s overflow policy where a lost event would live;
;   3. THE DIRTY-BAND RUNS the last frame emitted - the (first band, band
;      count) pairs GFX_BLIT1 would have been called with. This is the number
;      WEAVE-SPEC 14 prices at 2-4 for a two-sprite frame, so a change that
;      composes more than it changed fails here rather than in a benchmark;
;   4. THE COMPOSED BUFFER, byte for byte. `shift3`, `shift7`, `negx` and
;      `overlap` are the four that exercise the barrel shift, the whole-byte
;      clip either side and 2.11's AND/OR rule with two sprites on one byte;
;   5. after every case: ES, SS:SP and BP are what they were and DF IS CLEAR;
;   6. NEGATIVE CONTROLS: one case whose expected BUFFER is deliberately
;      wrong and one whose expected STATE is, both of which must FAIL. A
;      differential that cannot see a broken core has proved nothing
;      (12.1.1's rule, and this file inherits it).
;
; RUN IT:  apps/weave/hosttest/weavecv.sh
; =============================================================================

cpu 8086
bits 16
org 0x7C00

%define WSM_HOSTTEST                ; wspr.inc records the blit instead of
                                    ; calling GFX_BLIT1, and wwork.inc counts
                                    ; the BEL instead of sounding it. Those
                                    ; are the only two kernel calls anywhere
                                    ; in the surface this harness drives

SEG_STACK   equ 0x1000              ; SS, and SS != DS is the whole point
SEG_KERNEL  equ 0x3000              ; the ES sentinel: it must come back intact
SEG_CANVAS  equ 0x4000              ; the canvas claim (WEAVE-SPEC 6.10.4)
HB_ROW      equ 10                  ; 6.10.7: one recorded blit is five words -
HB_MAX      equ 64                  ; band, bands, column, columns, pen - and
                                    ; a coloured run can emit more spans than
                                    ; the old 32-run bound allowed
IMG_SECTORS equ 64                  ; 32KB, weavevm.asm's ceiling and for its
                                    ; reason - stage 1 loads with ES = 0 and a
                                    ; 16-bit BX

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
    mov ds, ax                      ; DS = CS = 0 stands in for the module,
    mov ax, SEG_KERNEL              ; and the corpus's SPRITES sections are
    mov es, ax                      ; addressed as offsets in it - which is
    sti                             ; what lets wsm_bseg be 0
    cld
    xor bp, bp                      ; the failure count

    mov si, msg_hello
    call puts

    xor si, si                      ; SI = the case index
.case:
    cmp si, [cv_ncase]
    jae .done
    call runcase
    call verdict
    inc si
    jmp .case

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
    mov dx, 0xF4                    ; isa-debug-exit: the code is AL*2+1, so
    out dx, al                      ; 3 = passed and 5 = failed
halt:
    cli
    hlt
    jmp halt

; -----------------------------------------------------------------------------
; verdict - AX = 1 the case agreed, 0 it did not. The NEG column inverts the
; expectation: a negative control that AGREES is the failure.
; in: SI = the case index. Preserves SI.
; -----------------------------------------------------------------------------
verdict:
    push ax
    push bx
    call caseptr                    ; BX -> the case record
    cmp word [bx + 28], 0           ; negctl
    jne .neg
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
    mov al, 'N'                     ; correctly caught
    call putc
    jmp short .out
.slip:
    mov al, 'x'                     ; the control PASSED: the check proves
    call putc                       ; nothing at all
    call putname
    inc bp
.out:
    pop bx
    pop ax
    ret

; caseptr - BX -> case SI's 38-byte record in cv_tab. Preserves everything else.
caseptr:
    push ax
    push dx
    mov ax, si
    mov dx, 38                      ; 7 pointers + 12 words (12.1.3) - the
                                    ; twelfth is 6.10.7's palette
    mul dx
    mov bx, ax
    add bx, cv_tab
    pop dx
    pop ax
    ret

putname:
    push si
    push bx
    call caseptr
    mov al, ' '
    call putc
    mov si, [bx + 0]
    call puts
    mov si, msg_nl
    call puts
    pop bx
    pop si
    ret

; =============================================================================
; runcase - build case SI's canvas, run its frames, compare everything.
; in:  SI = the case index
; out: AX = 1 everything agreed, 0 something did not. Preserves SI.
;
; THE CLAIM IS BUILT HERE AND NOT THROUGH WSMV_BIND, which is deliberate and
; is t_wab's rule applied to a layout instead of to a file: this harness is an
; INDEPENDENT SECOND READER of WEAVE-SPEC 6.10.4, sharing with the module only
; wsmabi.inc's equs. Two implementations agreeing by accident of shared code
; is the failure that rule exists to prevent, and the dispatcher's own verbs
; are exercised on the machine by weavegame instead.
;
; The three registers weavevm checks are checked here too: ES, SS:SP and BP
; must come back, and DF must be clear. A core that leaves DF set passes every
; comparison and breaks the NEXT thing on the machine that uses a string
; instruction, which is exactly the class of defect a harness with no OS under
; it is placed to find.
; =============================================================================
runcase:
    push si
    push bp
    call caseptr
    mov [cvrec], bx
    mov ax, [bx + 24]               ; nspr
    mov [nspr], ax
    mov word [hbn], 0
    mov word [wsm_frame], 0
    mov byte [wsm_rhead], 0
    mov byte [wsm_rtail], 0
    mov byte [wsm_tickp], 0
    mov word [wsm_ovf], 0
    mov word [wsm_bels], 0
    mov word [wsm_blits], 0
    mov ax, [bx + 36]               ; 6.10.7's palette: the PAPER colour in the
    mov [wsm_pen], ax               ; low byte, `colored` in the high one. The
                                    ; module's own BIND and WSMF_COLOR compute
                                    ; both; here the corpus states them, which
                                    ; is what lets a case turn the palette on
                                    ; without a load path under it
    mov word [wsm_bseg], 0          ; the "bundle claim" is segment 0, which
    mov ax, [bx + 2]                ; is where the corpus's SPRITES sections
    mov [wsm_spoff], ax             ; live
    mov word [wsm_cvseg], SEG_CANVAS
    mov di, wsm_kstate              ; the dirty bands and the contact bits,
    mov cx, 5 + WSM_MAXBAND + 15    ; between cases
    xor al, al
    push es
    push ds
    pop es
    rep stosb
    pop es

    ; --- the claim, laid out from 6.10.4 and from nothing else ---------------
    push es
    mov ax, SEG_CANVAS
    mov es, ax
    xor di, di
    mov cx, 0x2000
    xor ax, ax
    rep stosw                       ; zeroed at birth, as the load path leaves
                                    ; it - so every sprite record starts at
                                    ; 0,0, not shown and with no image
    mov bx, [cvrec]
    mov ax, [bx + 14]               ; W
    mov [es:WSC_W], ax
    mov cl, 3
    shr ax, cl
    mov [es:WSC_STRIDE], ax
    mov ax, [bx + 16]               ; H
    mov [es:WSC_H], ax
    mov ax, [bx + 18]               ; walls
    mov [es:WSC_WALLS], ax
    mov ax, [bx + 20]               ; tick
    mov [es:WSC_TICK], ax
    mov ax, [bx + 22]               ; cid
    mov [es:WSC_CID], ax
    mov ax, [nspr]
    mov [es:WSC_NSPR], ax
    mov [wsm_nspr], ax
    mov cl, WSR_SIZE
    mul cl
    add ax, WSC_SPR
    mov [es:WSC_BUF], ax

    xor di, di                      ; the sprite index
.spr:
    cmp di, [nspr]
    jae .frames
    call setspr
    inc di
    jmp short .spr

    ; --- the birth composition, then N frames --------------------------------
.frames:
    pop es
    call wsm_markall                ; it takes ES from wsm_cvseg itself
    mov ax, SEG_CANVAS
    mov es, ax
    xor ax, ax
    xor bx, bx
    call wsm_flush                  ; what the load path's first paint does
    mov word [hbn], 0               ; ...and its blit is not the frame's
    mov bx, [cvrec]
    mov ax, [bx + 26]               ; frames
    mov [frames], ax
    xor di, di
.fr:
    cmp di, [frames]
    jae .cmp
    mov word [hbn], 0               ; only the LAST frame's runs are compared
    mov bx, [cvrec]
    mov ax, [bx + 34]               ; hideframe: the one mid-run change a case
    or ax, ax                       ; can make, and it is there because a
    jz .go                          ; contact that a HIDDEN sprite leaves is
    dec ax                          ; the second thing 6.10.1 re-arms and
    cmp ax, di                      ; nothing else in the corpus could reach it
    jne .go
    push es
    mov ax, SEG_CANVAS
    mov es, ax
    and byte [es:WSC_SPR + WSR_FLAGS], ~WSRF_SHOWN & 0xFF
    pop es
.go:
    call oneframe
    inc di
    jmp short .fr

    ; --- compare -------------------------------------------------------------
.cmp:
    mov word [ok], 1
    call cmpstate
    call cmpring
    call cmpblits
    call cmpbuf
    mov bx, [cvrec]
    mov ax, [wsm_ovf]
    cmp ax, [bx + 30]
    je .ovfok
    mov word [ok], 0
.ovfok:
    mov ax, [wsm_bels]
    cmp ax, [bx + 32]
    je .belok
    mov word [ok], 0
.belok:
    ; ES is NOT checked, and the omission is deliberate rather than an
    ; oversight: wsm_markall and wsm_flush both document clobbering it (the
    ; claim IS the destination segment), so a sentinel here would assert
    ; something the contract does not promise. weavevm checks ES because
    ; wvm_slice promises it; this checks DF, which every routine here does.
    pushf
    pop ax
    test ax, 0x0400                 ; DF
    jz .dfok
    mov word [ok], 0
.dfok:
    mov ax, [ok]
    pop bp
    pop si
    ret

; -----------------------------------------------------------------------------
; setspr - one sprite record, built from the corpus's init block and from the
; SPRITES descriptor it names (WEAVE-SPEC 2.11, 6.10.4).
; in: ES = the claim, DI = the sprite index. Preserves DI and ES.
; -----------------------------------------------------------------------------
setspr:
    push di
    mov ax, di
    mov cx, 16                      ; 6.10.7 added the colour word
    mul cx
    mov bx, [cvrec]
    add ax, [bx + 4]                ; desc x y vx vy shown frame
    mov si, ax
    mov ax, di
    mov cx, WSR_SIZE
    mul cx
    add ax, WSC_SPR
    mov di, ax                      ; ES:DI = the record

    ; the descriptor, read straight out of the section the packer emitted
    mov ax, [si]                    ; the descriptor index
    mov cx, 8
    mul cx
    add ax, 2
    add ax, [wsm_spoff]
    mov bx, ax
    mov al, [bx]                    ; w_bytes
    mov [es:di + WSR_WB], al
    mov cl, 3
    shl al, cl
    mov [es:di + WSR_PW], al
    mov al, [bx + 1]                ; h_px
    mov [es:di + WSR_PH], al
    mov al, [bx + 2]                ; frames
    mov [es:di + WSR_NFR], al
    mov ax, [bx + 4]                ; the data offset, section-relative
    add ax, [wsm_spoff]
    mov [es:di + WSR_DOFF], ax

    mov ax, [si + 2]                ; x
    mov [es:di + WSR_X], ax
    mov [es:di + WSR_OX], ax
    mov cl, 4
    shl ax, cl
    mov [es:di + WSR_PX16], ax
    mov ax, [si + 4]                ; y
    mov [es:di + WSR_Y], ax
    mov [es:di + WSR_OY], ax
    mov cl, 4
    shl ax, cl
    mov [es:di + WSR_PY16], ax
    mov ax, [si + 6]                ; vx
    mov [es:di + WSR_VX], ax
    mov ax, [si + 8]                ; vy
    mov [es:di + WSR_VY], ax
    mov al, 0
    cmp word [si + 10], 0
    je .sh
    mov al, WSRF_SHOWN
.sh:
    mov ah, [si + 14]               ; 6.10.7's colour, into the flags byte's
    and ah, 0x0F                    ; TOP nibble - the load path's WSMF_COLOR
    mov cl, WSRF_COLSH              ; write, done here because this harness
    shl ah, cl                      ; lays the claim out itself
    or al, ah
    mov [es:di + WSR_FLAGS], al
    mov ax, [si + 12]               ; the initial frame, in both nibbles: the
    and al, 0x0F                    ; birth composition is what puts the
    mov ah, al                      ; "as last composed" half there
    mov cl, 4
    shl ah, cl
    or al, ah
    mov [es:di + WSR_FRAME], al
    pop di
    ret

; -----------------------------------------------------------------------------
; oneframe - WEAVE-SPEC 6.10's steps 1-5 and step 7, without the kernel calls
; the worker makes around them. Step 6 needs OSAPI_KEY_DOWN and is weavegame's.
; -----------------------------------------------------------------------------
oneframe:
    push di
    push es
    mov ax, SEG_CANVAS
    mov es, ax
    mov ax, [es:WSC_CID]
    mov [wsm_cid], ax
    mov ax, [es:WSC_NSPR]
    mov [wsm_nspr], ax
    inc word [wsm_frame]
    xor bx, bx
.mv:
    cmp bx, [wsm_nspr]
    jae .coll
    push bx
    call wsm_move
    pop bx
    inc bx
    jmp short .mv
.coll:
    call wsm_collide
    mov ax, SEG_CANVAS
    mov es, ax
    xor bx, bx
.mk:
    cmp bx, [wsm_nspr]
    jae .fl
    push bx
    call wsm_markspr
    pop bx
    inc bx
    jmp short .mk
.fl:
    mov ax, SEG_CANVAS
    mov es, ax
    xor ax, ax
    xor bx, bx
    call wsm_flush
    mov ax, SEG_CANVAS
    mov es, ax
    mov cx, [es:WSC_TICK]
    jcxz .out
    mov ax, [wsm_frame]
    xor dx, dx
    div cx
    or dx, dx
    jnz .out
    mov ax, [wsm_cid]
    mov ah, 57                      ; ontick, data1 = the frame counter
    mov cx, [wsm_frame]
    xor dx, dx
    call wsm_stage
.out:
    pop es
    pop di
    ret

; -----------------------------------------------------------------------------
; The four comparisons. Each clears [ok] on a disagreement and says nothing;
; the case's one letter is verdict's.
; -----------------------------------------------------------------------------
cmpstate:
    push es
    mov ax, SEG_CANVAS
    mov es, ax
    mov bx, [cvrec]
    mov si, [bx + 6]                ; the expected records
    xor di, di
.s:
    cmp di, [nspr]
    jae .out
    mov ax, di
    mov cx, WSR_SIZE
    mul cx
    add ax, WSC_SPR
    mov bx, ax
    call cmpone
    add si, 16
    inc di
    jmp short .s
.out:
    pop es
    ret

; cmpone - ES:BX = the record, DS:SI = eight expected words.
cmpone:
    push bx
    push si
    mov ax, [es:bx + WSR_PX16]
    cmp ax, [si]
    jne .bad
    mov ax, [es:bx + WSR_PY16]
    cmp ax, [si + 2]
    jne .bad
    mov ax, [es:bx + WSR_X]
    cmp ax, [si + 4]
    jne .bad
    mov ax, [es:bx + WSR_Y]
    cmp ax, [si + 6]
    jne .bad
    mov ax, [es:bx + WSR_VX]
    cmp ax, [si + 8]
    jne .bad
    mov ax, [es:bx + WSR_VY]
    cmp ax, [si + 10]
    jne .bad
    mov al, [es:bx + WSR_FLAGS]
    xor ah, ah
    and al, WSRF_SHOWN | WSRF_SCORED | WSRF_WASSH
    cmp ax, [si + 12]
    jne .bad
    mov al, [es:bx + WSR_FRAME]
    xor ah, ah
    cmp ax, [si + 14]
    jne .bad
    jmp short .out
.bad:
    mov word [ok], 0
.out:
    pop si
    pop bx
    ret

cmpring:
    mov bx, [cvrec]
    mov si, [bx + 8]                ; the expected ring
    mov cx, [si]
    add si, 2
    mov al, [wsm_rtail]             ; head is 0 - nothing drains here
    xor ah, ah
    cmp ax, cx
    je .walk
    mov word [ok], 0
    ret
.walk:
    jcxz .out
    mov di, wsm_ring
.r:
    mov al, [di]
    cmp al, [si]
    jne .bad
    mov al, [di + 1]
    cmp al, [si + 1]
    jne .bad
    mov ax, [di + 2]
    cmp ax, [si + 2]
    jne .bad
    mov ax, [di + 4]
    cmp ax, [si + 4]
    jne .bad
    add di, WSM_REC
    add si, 6
    loop .r
.out:
    ret
.bad:
    mov word [ok], 0
    ret

cmpblits:
    mov bx, [cvrec]
    mov si, [bx + 10]               ; the expected spans
    mov cx, [si]
    add si, 2
    cmp cx, [hbn]
    je .walk
    mov word [ok], 0
    ret
.walk:
    jcxz .out
    mov di, hbruns
.b:
    push cx
    mov cx, HB_ROW / 2              ; all five words: band, bands, column,
.w:                                 ; columns, pen (6.10.7)
    mov ax, [di]
    cmp ax, [si]
    jne .bad2
    add di, 2
    add si, 2
    loop .w
    pop cx
    loop .b
.out:
    ret
.bad2:
    pop cx
    mov word [ok], 0
    ret

cmpbuf:
    push es
    mov ax, SEG_CANVAS
    mov es, ax
    mov bx, [cvrec]
    mov si, [bx + 12]               ; the expected buffer
    mov cx, [si]
    add si, 2
    mov di, [es:WSC_BUF]
    jcxz .out
.c:
    mov al, [es:di]
    cmp al, [si]
    jne .bad
    inc di
    inc si
    loop .c
.out:
    pop es
    ret
.bad:
    mov word [ok], 0
    pop es
    ret

; -----------------------------------------------------------------------------
; wsm_hostblit - what wspr.inc calls in place of OSAPI_GFX_BLIT1 under
; WSM_HOSTTEST: record the SPAN this blit put down - first band, band count,
; first byte column, column count - and the pen wsm_emit set for it. That is
; what WEAVE-SPEC 14 prices and what a picture cannot show, and 6.10.7 widened
; it from two words to five because a span that is right about its rows and
; wrong about its columns draws the same picture in the wrong colours.
; in: AX = screen x + 8 x col0, BX = screen y + the run's first row,
;     CX = 8 x columns, DX = the run's rows. The harness's canvas is at 0,0.
;     Preserves all.
; -----------------------------------------------------------------------------
wsm_hostblit:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [hbax], ax                  ; the four geometry registers, banked -
    mov [hbcx], cx                  ; the index multiply below needs DX and
    mov [hbdx], dx                  ; the row count is in it
    mov di, [hbn]
    cmp di, HB_MAX
    jae .out
    mov ax, HB_ROW
    mul di
    mov di, ax
    add di, hbruns
    mov cl, 3
    mov ax, bx
    shr ax, cl
    mov [di], ax                    ; the first band
    mov ax, [hbdx]
    shr ax, cl
    mov [di + 2], ax                ; ...and how many
    mov ax, [hbax]
    shr ax, cl
    mov [di + 4], ax                ; the first byte COLUMN - the harness puts
                                    ; the canvas at screen 0,0, so the blit's
                                    ; x IS 8 x col0
    mov ax, [hbcx]
    shr ax, cl
    mov [di + 6], ax                ; ...and how many columns
    mov ax, [wsm_curpen]
    mov [di + 8], ax                ; 6.10.7's pen, 0xFFFF for "no pen call"
    inc word [hbn]
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
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
msg_hello: db 'weavecv: the canvas composer in raw QEMU, SS != DS', 13, 10, 0
msg_tail:  db ' failures - weavecv OK', 13, 10, 0
msg_bad:   db ' FAILURES in weavecv', 13, 10, 0
msg_nl:    db 13, 10, 0

; --- THE SHIPPING CORE, %included and never copied --------------------------
section .text
%include "os88api.inc"              ; wwork.inc's worker loop names OSAPI_*,
                                    ; KSC_SPACE, KERNEL_SEG and W_FLAGS. None
                                    ; of that code is REACHED here - there is
                                    ; no kernel at 0060: - but it still has to
                                    ; assemble, and including the real SDK
                                    ; rather than stubbing the names is what
                                    ; keeps this a build of the shipping text
%include "wsmabi.inc"
%include "wspr.inc"
%include "wwork.inc"
%include "wsmdata.inc"
%include "weavecvcorp.inc"

section .bss
ok:      resw 1
cvrec:   resw 1
nspr:    resw 1
frames:  resw 1
hbn:     resw 1
hbax:    resw 1
hbcx:    resw 1
hbdx:    resw 1
hbruns:  resw HB_MAX * (HB_ROW / 2)
