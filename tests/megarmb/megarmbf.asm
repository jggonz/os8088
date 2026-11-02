; =============================================================================
; os8088 - tests/megarmb/megarmbf.asm
;
; MEGARMBF, a TEST instrument (nothing under tests/ ships): the real-mode
; bracket's FALLBACK and its TABLE (SPEC.md 53.2.3,
; docs/plans/MEGA-VIDEO-REALMODE-PLAN.md W2), every result on port E9 for
; tests/megarmbref.py (Bochs). OS88_HIGH and OS88_HIGHMEM, so on kern_mega in
; protected mode its region is above 1MB (a real-mode bracket runs a COPY)
; and an OSAPI_MEM_HUGE claim is above 1MB too.
;
; Its window's timer starts it once. Every leg calls OSAPI_FSX_RUN with SI =
; FSXR_REAL and records CF, the SI that came back, and - from inside its
; entry proc - OSAPI_CPU_INFO's AH and a mark. In order:
;
;   K  FSXF_REAL + FSXF_KEEPWORKER: falls back, FSXR_KEEP
;   S  FSXF_REAL with a TF_SERVICE task alive - the harness sets the bit on
;      task 0 while the package waits (`wait S`): REAL since W7, the worker
;      parked (it was FSXR_SVC)
;   E  FSXF_REAL with EXTD.DRV's row said loaded (the harness pokes its
;      MODR_SEG with a value that names no descriptor): REAL since W7 - the
;      door no longer tests the row, and a row with no image is no low copy
;      (it was FSXR_EXTD; a LOADED image is tests/megarmbext.py's, 86Box)
;   L  FSXF_REAL with a REAL SERIAL.DRV lease open on COM1 (W7; it was
;      FSXR_SERIAL), a byte queued by hand in the ring's TX half: inside,
;      `int 24h` - IRQ4's vector in the bracket - runs the pointer's ISR,
;      which serves the leased row through SERIAL.DRV at its paragraph and
;      sends the byte from the ring's COPY; after, the owner's ring (in this
;      OS88_HIGH region, above 1MB) shows the byte gone. x = 01dd: the
;      vector was the kernel's and dd bytes went; 02xx: it was not; EEee:
;      the lease refused, ee its AX
;   M  FSXF_REAL with the heap filled by the package's own claims (through
;      the DMA door, CX = 0: conventional ones - OSAPI_MEM_CLAIM gives an
;      OS88_HIGHMEM package memory above 1MB), so no
;      conventional room is left for its region's copy: FSXR_ROOM
;   H  THE PACKAGE'S OWN FALLBACK: an OSAPI_MEM_HUGE claim (above 1MB) in
;      os88real's list - os88real_prep answers CF = 1, so the package asks
;      without FSXF_REAL; SI comes back as it went
;   P  ...and the other side of it: a conventional claim in the list, prep
;      CF = 0, FSXF_REAL taken; inside, os88real_swap makes the word a real
;      segment, the entry writes MF_PAT through it, swaps back - and after,
;      in protected mode, the word through its SELECTOR reads MF_PAT
;   N  FSXF_REAL, and inside, cells the real-mode table does not offer:
;      OSAPI_MEM_CLAIM (a rare cell) and OSAPI_FILE_RENAME (a 3-byte JCELL)
;      answer CF = 1 with every other register kept; OSAPI_SEG (the twin)
;      answers SEG_LIN CS x 16 and refuses SEG_AT; OSAPI_FSX_WAIT FSXW_FRAME
;      and OSAPI_MOUSE return; then MF_LOOK ticks of halting for the harness
;      to read the table, and the entry returns ASKING for protected mode -
;   n  ...which the package gives itself: the same call without FSXF_REAL,
;      where OSAPI_MEM_CLAIM is had
;
; E9:  MEGARMBF: start
;      MEGARMBF: wait <c> / go <c> / unwait <c>      (the harness's pokes)
;      MEGARMBF: <leg> cf C why WW feat FF mark MMMM [x XXXX]
;      MEGARMBF: N claim CCCC kept KKKK rename RRRR lin LLLLLLLL at AAAA
;                wait WWWW mouse MMMM
;      MEGARMBF: done
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'MEGARMBF', mf_entry, 0, OS88_STACK_192 | OS88_HIGH | OS88_HIGHMEM

MF_MARK     equ 0xC33C
MF_PAT      equ 0x5AA5
MF_WAIT     equ 36                  ; the timer: two seconds after launch
MF_LOOK     equ 54                  ; leg N's three seconds inside
MF_NCLM     equ 40                  ; leg M's claims, at most
MF_BIG      equ 60000               ; bss that makes the REGION ~61 KB: a
                                    ; package's claims stop at its floor, and
                                    ; the kernel's claim for the region's copy
                                    ; may still have the ~51 KB under it, so M
                                    ; needs a copy bigger than that (W3 - W1's
                                    ; door claimed 64 KB for this region,
                                    ; a rounding defect, which is what M used
                                    ; to fall back on)

mf_entry:
    push si
    mov si, mf_s_start
    call mf_say
    call mf_nl
    mov si, mf_tpl
    call OSAPI_WM_CREATE
    jc .out
    mov [mf_win], bx
    mov ax, mf_tick
    call OSAPI_WM_ONTIMER
    mov ax, MF_WAIT
    call OSAPI_WM_TIMER
    clc
.out:
    pop si
    ret

mf_paint:
    ret

; the timer: under the gfx lock, a window callback - the bracket's context
mf_tick:
    cmp byte [mf_ran], 0
    je .go
    ret
.go:
    mov byte [mf_ran], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    ; --- K ------------------------------------------------------------
    mov di, mf_rk
    mov cx, FSXF_REAL + FSXF_KEEPWORKER
    call mf_run
    mov al, 'K'
    call mf_rep
    ; --- S, E: a state the harness makes, then puts back; L: a lease ---
    mov al, 'S'
    mov di, mf_rs
    call mf_poked
    mov al, 'E'
    mov di, mf_re
    call mf_poked
    call mf_lease                   ; L: a REAL lease (W7), no poke
    ; --- M: no conventional room -----------------------------------------
    call mf_fill
    mov di, mf_rm
    mov cx, FSXF_REAL
    call mf_run
    call mf_unfill
    mov al, 'M'
    call mf_rep
    ; --- H: a claim with no real segment: the package's own fallback ------
    mov ax, 16
    call OSAPI_MEM_HUGE             ; DX = its selector (above 1MB)
    mov [mf_cl], dx
    mov word [mf_x], 0xFFFF
    jc .hno
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = its linear base
    mov [mf_x], dx                  ; (the 64KB it is in: 0010h+ above 1MB)
.hno:
    mov si, mf_list
    mov di, mf_list_bank
    call os88real_prep
    sbb al, al                      ; FFh: a word with no real segment
    mov [mf_hprep], al
    mov cx, FSXF_REAL               ; ...so the package does not ask
    or al, al
    jz .hask
    xor cx, cx
.hask:
    mov di, mf_rh
    call mf_run
    mov dx, [mf_cl]
    call OSAPI_MEM_FREE
    mov al, 'H'
    call mf_rep
    ; --- P: a conventional claim, renamed by the package -------------------
    mov ax, 4
    xor cx, cx                      ; the DMA door, CX = 0: the plain claim,
    call OSAPI_MEM_CLAIM_DMA        ; CONVENTIONAL for an OS88_HIGHMEM package
    mov [mf_cl], dx                 ; (OSAPI_MEM_CLAIM's goes above 1MB)
    push ds
    mov ds, dx
    mov word [0], 0                 ; (through the selector, before)
    pop ds
    mov si, mf_list
    mov di, mf_list_bank
    call os88real_prep
    sbb al, al
    mov [mf_pprep], al
    mov di, mf_rp
    mov cx, FSXF_REAL
    mov word [mf_ent], mf_main_p
    call mf_run0
    mov dx, [mf_cl]
    push ds
    mov ds, dx
    mov ax, [0]                     ; ...and after, through the selector
    pop ds
    mov [mf_x], ax
    call OSAPI_MEM_FREE
    mov al, 'P'
    call mf_rep
    ; --- N: refused cells inside, then the package's protected re-run ----
    mov byte [mf_needpm], 0
    mov di, mf_rn
    mov cx, FSXF_REAL
    mov word [mf_ent], mf_main_n
    call mf_run0
    mov al, 'N'
    call mf_rep
    call mf_nrep
    mov di, mf_rn2
    xor cx, cx
    cmp byte [mf_needpm], 0
    je .nn
    mov word [mf_ent], mf_main_n2
    call mf_run0
.nn:
    mov ax, [mf_n2c]
    mov [mf_x], ax
    mov al, 'n'
    call mf_rep
    mov si, mf_s_done
    call mf_say
    call mf_nl
    mov byte [mf_fin], 1
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mf_poked - AL = the leg's letter, DI = its record: ask the harness for the
; state (`wait c`), run FSXF_REAL, report, and have it put back (`unwait c`)
mf_poked:
    push ax
    mov [mf_code], al
    mov si, mf_s_wait
    call mf_say
    call mf_out1
    call mf_nl
.w:
    sti
    hlt
    mov al, [mf_code]
    cmp [mf_go], al
    jne .w
    mov cx, FSXF_REAL
    call mf_run
    mov si, mf_s_unwait
    call mf_say
    mov al, [mf_code]
    call mf_out1
    call mf_nl
.u:
    sti
    hlt
    cmp byte [mf_go], 0
    jne .u
    pop ax
    jmp mf_rep

; mf_lease - leg L (W7): a lease on COM1, named by the user (115.1.1), the
; region pinned first as SPEC.md 115 asks; a byte queued straight into the
; ring's TX half; the bracket; the ring read after; the lease closed
mf_lease:
    mov dx, ds
    xor ax, ax
    call OSAPI_MEM_MOVABLE          ; pin the region before the ring
    xor ax, ax                      ; Open
    mov bx, 0x0081                  ; COM1, the user's; 9600
    mov si, mf_ser
    call OSAPI_SERIAL
    jnc .open
    mov ah, 0xEE
    mov [mf_x], ax
    jmp short .rep
.open:
    mov bl, [mf_ser+4]              ; TX tail
    xor bh, bh
    mov byte [mf_ser+SER_TX+bx], 'Z'
    inc bl
    mov [mf_ser+3], bl              ; TX head: one byte queued
    mov byte [mf_lint], 2
    mov di, mf_rl
    mov cx, FSXF_REAL
    mov word [mf_ent], mf_main_l
    call mf_run0
    mov al, [mf_ser+4]              ; the tail, moved by the ISR inside:
    sub al, [mf_ser+3]              ; 0 when it reached the head, FFh not
    inc al                          ; (so 1 = the byte went)
    mov ah, [mf_lint]
    mov [mf_x], ax
    mov al, 1                       ; Close
    call OSAPI_SERIAL
.rep:
    mov al, 'L'
    jmp mf_rep

; L's entry: IRQ4's vector in the bracket is IVT 24h (the 8259 stays at its
; protected-mode base); the kernel's there, it is run once by hand
mf_main_l:
    call mf_main
    push es
    xor ax, ax
    mov es, ax
    cmp word [es:0x24*4+2], 0x0060
    pop es
    jne .out
    mov byte [mf_lint], 1
    int 0x24
.out:
    ret

; mf_run - CX = flags, DI = the leg's record: the bracket, entry mf_main.
; mf_run0 - ...with the entry at [mf_ent]
mf_run:
    mov word [mf_ent], mf_main
mf_run0:
    mov [mf_cur], di
    mov bx, [mf_win]
    mov ax, [mf_ent]
    xor si, si                      ; FSXR_REAL in
    xor bp, bp                      ; no FSXL_ list (W3)
    call OSAPI_FSX_RUN
    sbb dl, dl
    mov di, [mf_cur]
    mov [di], dl                    ; cf (FFh refused)
    mov ax, si
    mov [di+1], al                  ; the answer
    ret

; the plain entry: what kind of bracket this is, and that it ran
mf_main:
    call OSAPI_CPU_INFO
    mov bx, [mf_cur]
    mov [bx+2], ah
    mov word [bx+4], MF_MARK
    ret

; P's entry: the claim's word made real (or not), written through, put back
mf_main_p:
    call mf_main
    mov si, mf_list
    mov di, mf_list_bank
    call os88real_swap
    mov ax, [mf_cl]
    mov [mf_preal], ax              ; the word INSIDE: the real paragraph
    push es
    mov es, ax
    mov word [es:0], MF_PAT
    pop es
    call os88real_swap
    ret

; N's entry: the refusals, the twin, the kept cells, a look, and a request
mf_main_n:
    call mf_main
    mov [mf_ncs], cs
    mov ax, 1                       ; OSAPI_MEM_CLAIM refused, all kept
    mov bx, 0x1111
    mov cx, 0x2222
    mov dx, 0x3333
    mov si, 0x4444
    mov di, 0x5555
    mov bp, 0x6666
    call OSAPI_MEM_CLAIM
    sbb ah, ah                      ; AH = FFh refused (AL is 1 still)
    mov [mf_nclaim], ax
    xor ax, ax
    cmp bx, 0x1111
    jne .k
    cmp cx, 0x2222
    jne .k
    cmp dx, 0x3333
    jne .k
    cmp si, 0x4444
    jne .k
    cmp di, 0x5555
    jne .k
    cmp bp, 0x6666
    jne .k
    mov ax, 0x0BAD + 0x5000         ; = 5BADh: every register kept
.k:
    mov [mf_nkept], ax
    mov si, mf_s_done               ; OSAPI_FILE_RENAME, a 3-byte JCELL
    mov di, mf_s_done
    call OSAPI_FILE_RENAME
    sbb ax, ax
    mov [mf_nren], ax
    mov dx, cs                      ; the SEG twin: SEG_LIN is CS x 16
    mov bl, SEG_LIN
    call OSAPI_SEG
    jnc .lin
    mov dx, 0xFFFF
.lin:
    mov [mf_nlin], ax
    mov [mf_nlin+2], dx
    mov dx, cs
    xor ax, ax
    xor cx, cx
    mov bl, SEG_AT                  ; ...and SEG_AT refused
    call OSAPI_SEG
    sbb ax, ax
    mov [mf_nat], ax
    mov al, FSXW_FRAME              ; a kept cell with a real-mode arm
    call OSAPI_FSX_WAIT
    sbb ax, ax
    mov [mf_nwait], ax
    mov cx, 0xFFFF                  ; ...and the other: CX = mouse_x, which a
    call OSAPI_MOUSE                ; refusal would leave FFFFh
    mov [mf_nmou], cx
    call OSAPI_GET_TICKS
    mov bx, ax
    mov byte [mf_inside], 1
.w:
    sti
    hlt
    call OSAPI_GET_TICKS
    sub ax, bx
    cmp ax, MF_LOOK
    jb .w
    mov byte [mf_inside], 0
    call OSAPI_CPU_INFO             ; a real-mode bracket could not have the
    test ah, CPU_F_PM               ; claim: ask for protected mode
    jnz .out
    mov byte [mf_needpm], 1
.out:
    ret

; n's entry, in protected mode: the claim N could not have
mf_main_n2:
    call mf_main
    mov ax, 1
    call OSAPI_MEM_CLAIM
    sbb ax, ax
    mov [mf_n2c], ax                ; 0 = had
    jnz .out
    call OSAPI_MEM_FREE
.out:
    ret

; mf_fill - the heap filled by claims of ours, 64KB down to 1KB; mf_unfill
mf_fill:
    mov word [mf_ncl], 0
    mov cx, 64
.f:
    cmp word [mf_ncl], MF_NCLM
    jae .done
    mov ax, cx
    push cx
    xor cx, cx                      ; conventional, as P's (the DMA door)
    call OSAPI_MEM_CLAIM_DMA
    pop cx
    jc .smaller
    mov bx, [mf_ncl]
    shl bx, 1
    mov [mf_cls+bx], dx
    inc word [mf_ncl]
    jmp short .f
.smaller:
    shr cx, 1
    jnz .f
.done:
    ret
mf_unfill:
    mov cx, [mf_ncl]
    mov ax, cx
    mov [mf_x], ax                  ; (how many: the report's x)
    jcxz .done
    xor bx, bx
.u:
    mov dx, [mf_cls+bx]
    call OSAPI_MEM_FREE
    inc bx
    inc bx
    loop .u
.done:
    ret

; mf_rep - AL = the leg's letter, DI = its record: one E9 line
mf_rep:
    mov si, mf_name
    call mf_puts
    call mf_out1
    mov si, mf_s_cf
    call mf_puts
    mov al, [di]
    and al, 1
    call mf_dig
    mov si, mf_s_why
    call mf_puts
    mov al, [di+1]
    call mf_hex2
    mov si, mf_s_feat
    call mf_puts
    mov al, [di+2]
    call mf_hex2
    mov si, mf_s_mark
    call mf_puts
    mov ax, [di+4]
    call mf_hex4
    mov si, mf_s_x
    call mf_puts
    mov ax, [mf_x]
    call mf_hex4
    cmp di, mf_rh
    jne .np
    mov si, mf_s_prep
    call mf_puts
    mov al, [mf_hprep]
    call mf_hex2
.np:
    cmp di, mf_rp
    jne .nq
    mov si, mf_s_prep
    call mf_puts
    mov al, [mf_pprep]
    call mf_hex2
    mov si, mf_s_real
    call mf_puts
    mov ax, [mf_preal]
    call mf_hex4
    mov si, mf_s_bank
    call mf_puts
    mov ax, [mf_list_bank]
    call mf_hex4
.nq:
    mov word [mf_x], 0
    jmp mf_nl

mf_nrep:
    mov si, mf_s_n
    call mf_say
    mov ax, [mf_nclaim]
    call mf_hex4
    mov si, mf_s_kept
    call mf_puts
    mov ax, [mf_nkept]
    call mf_hex4
    mov si, mf_s_ren
    call mf_puts
    mov ax, [mf_nren]
    call mf_hex4
    mov si, mf_s_lin
    call mf_puts
    mov ax, [mf_nlin+2]
    call mf_hex4
    mov ax, [mf_nlin]
    call mf_hex4
    mov si, mf_s_at
    call mf_puts
    mov ax, [mf_nat]
    call mf_hex4
    mov si, mf_s_wt
    call mf_puts
    mov ax, [mf_nwait]
    call mf_hex4
    mov si, mf_s_mou
    call mf_puts
    mov ax, [mf_nmou]
    call mf_hex4
    mov si, mf_s_cs
    call mf_puts
    mov ax, [mf_ncs]
    call mf_hex4
    jmp mf_nl

; --- E9 ----------------------------------------------------------------------
mf_say:                             ; the name, ": ", then [SI]
    push si
    mov si, mf_name
    call mf_puts
    pop si
mf_puts:
    push ax
.l:
    lodsb
    or al, al
    jz .o
    out 0xE9, al
    jmp short .l
.o:
    pop ax
    ret
mf_nl:
    push ax
    mov al, 10
    out 0xE9, al
    pop ax
    ret
mf_out1:
    out 0xE9, al
    ret
mf_dig:
    add al, '0'
    out 0xE9, al
    ret
mf_hex4:
    push ax
    mov al, ah
    call mf_hex2
    pop ax
mf_hex2:
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 15
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .p
    add al, 7
.p:
    out 0xE9, al
    ret

%include "os88real.inc"

    OS88REAL_LIST mf_list, mf_cl    ; the one claim word H and P rename

mf_cl       dw 0
mf_tpl:
    dw 8, 300, 200, 40
    dw mf_ttl, mf_paint, 0, 0
mf_ttl:     db 'MegaRMB fallback', 0
mf_name:    db 'MEGARMBF: ', 0
mf_s_start: db 'start', 0
mf_s_wait:  db 'wait ', 0
mf_s_unwait: db 'unwait ', 0
mf_s_cf:    db ' cf ', 0
mf_s_why:   db ' why ', 0
mf_s_feat:  db ' feat ', 0
mf_s_mark:  db ' mark ', 0
mf_s_x:     db ' x ', 0
mf_s_prep:  db ' prep ', 0
mf_s_real:  db ' real ', 0
mf_s_bank:  db ' bank ', 0
mf_s_n:     db 'N claim ', 0
mf_s_kept:  db ' kept ', 0
mf_s_ren:   db ' rename ', 0
mf_s_lin:   db ' lin ', 0
mf_s_at:    db ' at ', 0
mf_s_wt:    db ' wait ', 0
mf_s_mou:   db ' mouse ', 0
mf_s_cs:    db ' cs ', 0
mf_s_done:  db 'done', 0

; THE RESULT BLOCK, in the bss: tests/megarmbref.py writes mf_go (its
; offset is os88_image_end, the .bin header's image size) and reads E9
    OS88_BSS MF_RESN + MF_BIG
    OS88_IMAGE_END

mf_res      equ os88_image_end
mf_go       equ mf_res + 0          ; byte: the harness's answer to `wait c`
mf_code     equ mf_res + 1
mf_fin      equ mf_res + 2
mf_ran      equ mf_res + 3
mf_win      equ mf_res + 4
mf_cur      equ mf_res + 6
mf_ent      equ mf_res + 8
mf_x        equ mf_res + 10
mf_ncl      equ mf_res + 12
mf_hprep    equ mf_res + 14
mf_pprep    equ mf_res + 15
mf_preal    equ mf_res + 16
mf_needpm   equ mf_res + 18
mf_inside   equ mf_res + 19
mf_nclaim   equ mf_res + 20
mf_nkept    equ mf_res + 22
mf_nren     equ mf_res + 24
mf_nlin     equ mf_res + 26         ; dword
mf_nat      equ mf_res + 30
mf_nwait    equ mf_res + 32
mf_nmou     equ mf_res + 34
mf_n2c      equ mf_res + 36
mf_ncs      equ mf_res + 38
mf_rk       equ mf_res + 40         ; the legs' records: cf, why, feat, -, mark
mf_rs       equ mf_rk + 6
mf_re       equ mf_rs + 6
mf_rl       equ mf_re + 6
mf_rm       equ mf_rl + 6
mf_rh       equ mf_rm + 6
mf_rp       equ mf_rh + 6
mf_rn       equ mf_rp + 6
mf_rn2      equ mf_rn + 6
mf_cls      equ mf_rn2 + 6          ; leg M's claims
mf_lint     equ mf_cls + MF_NCLM * 2 ; leg L: 1 the ISR run, 2 not the kernel's
mf_ser      equ mf_lint + 2         ; ...and its ring (SPEC.md 115)
SER_TX      equ 261
MF_RESN     equ mf_ser + SER_BUFFER_SIZE - mf_res
