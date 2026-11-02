; =============================================================================
; os8088 - apps/video/vcold.asm
;
; THE kern_mega ARM's COLD PART (SPEC.md 98.3.24.1): part 3 of the arm's
; VIDEO.O88 - apps/video/vcold.inc is its header and its contract. Everything
; here ran in the player's image until the arm's diag builds outgrew
; APP_MAX_SIZE (MEGA-VIDEO-REALMODE-PLAN W5): the real-mode bracket's door and
; its afterword, the info card's ninth line and its names, and with VP_DIAG
; the profile's two ends (apps/video/vprof.inc, which the shipped player
; still expands in place) and with VP_RQ the bracket's reflection counts. It
; is cold code by choice - once a play at most, on the desktop or at a
; bracket's edge - so a far call and a trampoline are nothing against it.
;
; DS IS THE PLAYER'S throughout: every variable named below is the player's,
; at the offset tools/os88plink.py read out of the map its assembly wrote
; ($(MEGAAPPDIR)/vclink.inc), and the part writes nothing through its own CS,
; which is a read-only code alias under protected mode. Its own tables (the
; reasons' names) are read through CS. A player routine is reached through
; VPC - [vp_ptgt] the routine, then the player's vp_ptramp, far - and the
; player's CS, where a routine wants it, is [vp_ptfp + 2].
;
; Assembled with the player's own defines ($(VPDEF) -DPKG_MEGA): a diag
; player's part carries the profile, a plain one's does not.
; =============================================================================
    cpu 8086
    bits 16
    org 0

%include "os88api.inc"
%include "video/vcold.inc"
%ifdef VP_DIAG
%define VP_RQ                       ; (the arm's diag: video.asm's own rule)
%endif
%include "vclink.inc"               ; (GENERATED: the player's names)

;@data vp_rasked vp_pm vp_lsess vp_livem vp_resid vp_xems vp_xbank vp_einp
;@data vp_xip vp_snd vp_v2 vp_shadow vp_lysplt vp_lybkb vp_rlst vp_rlst_bank
;@data vp_rwhy vp_xbase vp_xrun vp_nkeys vp_skscr vp_kekb vp_audio vp_nosnd
;@data vp_aseg vp_spkpwm vp_rlbuf vp_name vp_rran vp_rpic vp_rxo vp_ptgt
;@data vp_ptfp VP_SPK VP_RL
;@call os88real_prep vp_mclaim vp_scaps vp_fxget vp_ringkb vp_mfree vp_bset
;@call vp_fxbind vp_sesspic vp_fmt
;@data:VP_DIAG vp_prseg vp_prn vp_prnl vp_prbd vp_prna vp_frames vp_pr61
;@data:VP_DIAG vp_pitdiv vp_pitper0 vp_flip vp_lbk vp_late vp_ditab
;@data:VP_DIAG vp_ditab_end vp_ring vp_rclm vp_prfn VP_PRMAX VP_PRH VP_DIL
;@data:VP_DIAG VP_DIN VP_RLMAX
;@call:VP_DIAG vp_mmov
;@data:VP_RDTIME vp_prlp
;@data:VP_DIAG vp_rqd

; VPC - a call to the PLAYER's routine %1, every register and flag its own
%macro VPC 1
    mov word [vp_ptgt], PC_%1
    call far [vp_ptfp]
%endmacro
%define VPR_CALL VPC
%macro VPR_CS 0
    mov ax, [vp_ptfp + 2]           ; (the player's CS, not ours)
%endmacro

    dw VK_SIGVAL                    ; +0
    dw VK_LINKIMG                   ; +2 the player this part was linked to
    db VK_N                         ; +4
    db 0
    dw vc_rdoor                     ; +6 VK_RDOOR
    dw vc_rpost                     ;    VK_RPOST
    dw vc_rline                     ;    VK_RLINE
%ifdef VP_DIAG
    dw vc_prbeg                     ;    VK_PRBEG
    dw vc_prend                     ;    VK_PREND
%else
    dw 0, 0
%endif
%ifdef VP_RQ
    dw vc_rqget                     ;    VK_RQGET
%else
    dw 0
%endif

; the vectors: each a near routine below, far-called
vc_rdoor:
    call vp_rdoor
    retf
vc_rpost:
    call vp_rpost
    retf
vc_rline:
    call vp_rline
    retf
%ifdef VP_DIAG
vc_prbeg:
    call vp_prbeg
    retf
vc_prend:
    call vp_prend
    retf
%endif
%ifdef VP_RQ
vc_rqget:
    call vp_rqget
    retf
%endif

VPR_LIVE    equ 0x11                ; the player's reasons, [vp_rwhy] (the
VPR_SPK     equ 0x12                ; kernel's are FSXR_*, 1-9)
VPR_V2      equ 0x13
VPR_RES     equ 0x14
VPR_SPLIT   equ 0x15
VPR_MOVED   equ 0x16
VPR_CLAIM   equ 0x17
VPR_SHAD    equ 0x18

; vp_rdoor - OSAPI_FSX_RUN's door: CL |= FSXF_REAL, BP = the list and SI = 0
; when real mode serves this play - its two claims taken and its words
; banked (os88real_prep) - else [vp_rwhy] the reason it does not, and CL,
; BP as they came. Every other register kept
vp_rdoor:
    mov byte [vp_rasked], 0
    cmp byte [vp_pm], 0
    jne .k
    ret                             ; (a real-mode kernel: as shipped)
.k:
    push ax
    push bx
    push dx
    push si
    push di
    mov al, VPR_LIVE                ; LIVE (98.3.10): selector runs on the
    cmp byte [vp_lsess], 0          ; desktop, protected mode by ruling
    jne .pm
    cmp byte [vp_livem], 0
    jne .pm
    mov al, VPR_RES                 ; RESIDENT: its cursors are offsets in
    cmp byte [vp_resid], 0          ; protected mode, segments in real
    jne .pm
    cmp byte [vp_xems], 0           ; XMS READ IN PLACE (5.3 ruling 1), the
    jne .nb                         ; bank's or the hold's, as protected
    mov ah, [vp_xbank]              ; mode decided it: under the speaker,
    or ah, [vp_einp]                ; a version 2 file or the shadow, real
    or ah, [vp_xip]                 ; mode's borrow would hold interrupts
    jz .nb                          ; off through decoders with no yield
    mov al, VPR_SPK                 ; site, and its rules copy instead
    cmp byte [vp_snd], VP_SPK
    je .pm
    mov al, VPR_V2
    cmp byte [vp_v2], 0
    jne .pm
    mov al, VPR_SHAD
    cmp byte [vp_shadow], 0
    jne .pm
.nb:
    mov al, VPR_MOVED               ; a block not where its token names: real
    call vp_xmoved                  ; mode reads it at the token's address
    jc .pm
    mov al, VPR_SPLIT               ; THE LAYER's split (98.1.9.2), owed: it
    cmp byte [vp_lysplt], 0         ; claims and frees XMS on verdicts taken
    jne .ns                         ; inside, which real mode refuses
    cmp word [vp_lybkb], 0
    je .ns
    cmp byte [vp_xems], 0
    je .pm
.ns:
    call vp_rclaims                 ; what the bracket cannot claim (the
                                    ; sound ahead's lead is staged already,
                                    ; where vp_spos read it - 98.1.8.2)
    mov si, vp_rlst                 ; ...and every word a real segment
    mov di, vp_rlst_bank
    VPC os88real_prep
    mov al, VPR_CLAIM
    jc .pmf
    call vp_rmklist                 ; BP = the list
    or cl, FSXF_REAL
    mov byte [vp_rasked], 1
    pop di
    pop si
    xor si, si                      ; (FSXR_REAL in)
    jmp short .o
.pmf:
    call vp_rgive
.pm:
    mov [vp_rwhy], al
    pop di
    pop si
.o:
    pop dx
    pop bx
    pop ax
    ret

; vp_xmoved - CF=1: the hold's or bank's XMS block is not at its token (the
; pool moved it), so real mode, which reads at the token, may not have it.
; Every register kept
vp_xmoved:
    cmp byte [vp_xems], 0
    jne .no
    push ax
    push bx
    push dx
    mov ax, [vp_xbase]
    or ax, [vp_xbase + 2]
    jz .ok                          ; (no block: CF=0 from the or)
    mov dx, [vp_xrun]
    or dx, dx
    stc
    jz .o                           ; (no run to ask: moved, for all we know)
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = where it is now
    jc .o
    cmp ax, [vp_xbase]
    jne .mv
    cmp dx, [vp_xbase + 2]
    je .ok
.mv:
    stc
    jmp short .o
.ok:
    clc
.o:
    pop dx
    pop bx
    pop ax
    ret
.no:
    clc
    ret

; vp_rclaims - the two claims a real-mode bracket cannot make, at its door: a
; seek's key-entry scratch, and the sound's ring when none is held (Unmute in
; the full screen) - with the speaker's shaper part where the speaker would
; shape. Each is what the bracket's own claim would have been; none is an
; error. Every register kept
vp_rclaims:
    push ax
    push bx
    push cx
    push dx
    cmp word [vp_nkeys], 0
    je .snd
    cmp word [vp_skscr], 0
    jne .snd
    mov ax, [vp_kekb]
    VPC vp_mclaim
    jc .snd
    mov [vp_skscr], dx
.snd:
    cmp byte [vp_audio], 0
    je .o
    cmp byte [vp_nosnd], 0
    jne .o
    cmp word [vp_aseg], 0
    jne .o
    VPC vp_scaps
    test ax, SND_CAP_PCM_BG
    jz .spk
    mov ax, VP_RL / 1024 + 1        ; a card's: vp_sndprep's .hw
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA_HI
    jc .o
    jmp short .got
.spk:
    cmp byte [vp_spkpwm], 0         ; the speaker's: its shaper part first
    jne .sp1                        ; (a card's clip, 98.3.15.2)
    VPC vp_fxget
    jc .o
.sp1:
    VPC vp_ringkb
    VPC vp_mclaim
    jc .o
.got:
    mov [vp_aseg], dx               ; held with [vp_snd] 0: no sound yet
.o:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_rgive - after the bracket: the door's scratch given back, and the sound's
; ring if the play went out silent (muted inside, or never unmuted). Every
; register kept
vp_rgive:
    push dx
    xor dx, dx
    xchg dx, [vp_skscr]
    or dx, dx
    jz .s
    VPC vp_mfree
.s:
    cmp byte [vp_snd], 0
    jne .o
    xor dx, dx
    xchg dx, [vp_aseg]
    or dx, dx
    jz .o
    VPC vp_mfree
.o:
    pop dx
    ret

; vp_rmklist - BP = the FSXL_ list (SPEC.md 53.2.3): the file, HDD.DRV, the
; card when the play may sound through one, EMS for an EMS bank. Every other
; register kept
vp_rmklist:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, vp_rlbuf
    mov al, FSXL_FILE
    stosb
    mov si, vp_name                 ; the name TO ITS NUL and no further
    mov cx, 13                      ; (SPEC.md 98.3.24): the list's next
.nm:                                ; entry starts after it, and a 13-byte
    lodsb                           ; copy put a short name's padding there
    stosb                           ; - a 0, FSXL_END, so DRVC_SOUND was
    or al, al                       ; never read and the bracket played
    loopnz .nm                      ; silent (KERN-MEGA-PLAN 14.132)
    mov ax, FSXL_DRV | DRVC_DISK << 8
    stosw
    cmp byte [vp_xems], 0
    je .ne
    mov ax, FSXL_DRV | DRVC_EMS << 8
    stosw
.ne:
    cmp byte [vp_audio], 0
    je .e
    cmp byte [vp_nosnd], 0
    jne .e
    push di
    VPC vp_scaps
    pop di
    test ax, SND_CAP_PCM_BG
    jz .e
    mov ax, FSXL_DRV | DRVC_SOUND << 8
    stosw
.e:
    mov al, FSXL_END
    stosb
    mov bp, vp_rlbuf
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; vp_rpost - right after OSAPI_FSX_RUN, protected mode again, flags kept: the
; kernel's answer onto the card, and what the bracket owed undone - the block
; decoder's pointers made again on its selector, the frame put in the box,
; the door's claims given back. Every register kept
vp_rpost:
    pushf
    cmp byte [vp_rasked], 0
    je .o
    push ax
    mov ax, si
    mov [vp_rwhy], al               ; (0: it ran in real mode)
    cmp byte [vp_rran], 0
    je .f
    mov byte [vp_rran], 0
    VPC vp_bset
    VPC vp_fxbind                  ; (the shaper's, on its selectors again)
    cmp byte [vp_rpic], 0
    je .f
    mov byte [vp_rpic], 0
    VPC vp_sesspic                 ; THE FRAME IN THE BOX (98.3.7), which
    VPC vp_fmt                     ; the bracket could not claim or draw
.f:
    call vp_rxgive                  ; (the XMS let go of inside)
    call vp_rgive
    pop ax
.o:
    popf
    ret


; vp_rxgive - vp_rpost's: every block a real-mode bracket owed, freed.
; Every register kept
vp_rxgive:
    push ax
    push bx
    push cx
    push dx
    mov bx, vp_rxo
.l:
    xor ax, ax
    xor dx, dx
    xchg ax, [bx]
    xchg dx, [bx + 2]
    mov cx, ax
    or cx, dx
    jz .z
    call OSAPI_XMEM_FREE
.z:
    add bx, 4
    cmp bx, vp_rxo + 8
    jb .l
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; vp_rline - the info card's ninth line (98.3.24): which bracket the last play
; took, and why not real mode. DI = the line. Every register kept
vp_rline:
    cmp byte [vp_rwhy], 0xFF
    je .r
    push ax
    push bx
    push si
    mov si, vp_s_rreal
    mov al, [vp_rwhy]
    or al, al
    jz .p
    mov si, vp_s_rpm
    call vc_puts
    mov bl, al
    xor bh, bh
    sub bl, 0x11
    jae .own                        ; the player's
    add bl, 0x11 + VPR_NOWN - 1     ; ...or the kernel's, after them
.own:
    cmp bl, VPR_NALL
    jae .x
    shl bx, 1
    mov si, [cs:vp_rwhys + bx]
.p:
    call vc_puts
.x:
    pop si
    pop bx
    pop ax
.r:
    ret


; vc_puts - vp_puts with the string in OUR segment: CS:SI -> [DI] (ES = the
; player's), up to its NUL or the line's (vp_putc). SI past it
vc_puts:
    push ax
.l:
    mov al, [cs:si]
    inc si
    or al, al
    jz .out
    cmp byte [di], 0
    je .l
    stosb
    jmp short .l
.out:
    pop ax
    ret

vp_s_rreal:   db 'Real mode', 0
vp_s_rpm:     db 'Protected: ', 0
VPR_NOWN      equ 8                     ; vp_rwhys: VPR_LIVE.. first, then
VPR_NALL      equ VPR_NOWN + 9          ; FSXR_KEEP..FSXR_DROOM
vp_rwhys:     dw vp_s_rw11, vp_s_rw12, vp_s_rw13, vp_s_rw14, vp_s_rw15
              dw vp_s_rw16, vp_s_rw17, vp_s_rw18
              dw vp_s_rk1, vp_s_rk2, vp_s_rk3, vp_s_rk4, vp_s_rk5, vp_s_rk6
              dw vp_s_rk7, vp_s_rk8, vp_s_rk5
vp_s_rw11:    db 'Live', 0
vp_s_rw12:    db 'speaker, in place', 0
vp_s_rw13:    db 'v2, in place', 0
vp_s_rw14:    db 'resident', 0
vp_s_rw15:    db 'layer split', 0
vp_s_rw16:    db 'bank moved', 0
vp_s_rw17:    db 'claim above 1MB', 0
vp_s_rw18:    db 'shadow, in place', 0
vp_s_rk1:     db 'a worker kept', 0
vp_s_rk2:     db "a driver's worker", 0
vp_s_rk3:     db 'extended desktop', 0
vp_s_rk4:     db 'a serial lease', 0
vp_s_rk5:     db 'no low memory', 0
vp_s_rk6:     db 'its volume', 0
vp_s_rk7:     db 'fragmented', 0
vp_s_rk8:     db 'a driver', 0

%ifdef VP_DIAG
%include "video/vprof.inc"
    VPROF_BEG
    VPROF_END

; vp_prlin - AX = a segment or, in protected mode, a selector: AX = its
; linear address in paragraphs, the low 16 bits of it (the trailer's
; arithmetic is mod 4 KB and within a claim, so a wrap above 1MB costs it
; nothing). A selector SEG_LIN refuses, or 0, is left as it is. Every other
; register kept
vp_prlin:
    cmp byte [vp_pm], 0
    je .r
    or ax, ax
    jz .r
    push bx
    push cx
    push dx
    push ax
    mov dx, ax
    mov bl, SEG_LIN
    call OSAPI_SEG                  ; DX:AX = the linear address
    pop bx                          ; (BX = as it was)
    jnc .ok
    xchg ax, bx                     ; refused: as it was
    jmp short .o
.ok:
    mov cl, 4
    shr ax, cl
    mov cl, 12
    shl dx, cl
    or ax, dx
.o:
    pop dx
    pop cx
    pop bx
.r:
    ret
%endif

%ifdef VP_RQ
; vp_rqget - DI -> 4 words: 0040:00F0's, through a window in protected mode.
; Every register kept
vp_rqget:
    push ax
    push bx
    push cx
    push dx
    push si
    push ds
    push es
    mov dx, 0x004F
    mov cx, 8
    xor ax, ax
    mov bl, SEG_WIN
    call OSAPI_SEG                  ; DX = it (the paragraph in real mode)
    jc .z
    push dx
    push ds
    pop es
    mov ds, dx
    xor si, si
    mov cx, 4
    cld
    rep movsw
    pop dx
    push es
    pop ds
    cmp byte [vp_pm], 0
    je .r
    push ds
    pop es                          ; (ES off it: SEG_FREE's pop es)
    mov bl, SEG_FREE
    call OSAPI_SEG
    jmp short .r
.z:
    xor ax, ax
    mov cx, 4
    push ds
    pop es
    cld
    rep stosw
.r:
    pop es
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif
