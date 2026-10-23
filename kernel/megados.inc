; =============================================================================
; kernel/megados.inc - the DOS box under kern_mega: the PARK
; (docs/plans/KERN-MEGA-DOS-PLAN.md 2.3, waves W1 and W2)
;
; On kern_mega the DOS box has one route, the whole-machine handoff (SPEC.md
; 96.40), with EXTENDED MEMORY standing in for the hibernation file. HIBER.DRV's
; `hbm_dosrun` asks for it in three far calls:
;
;   mega_dosclaimf  the park and the XMS arena, out of the XMEM pool, BEFORE
;                   anything is torn down - so a machine with no room refuses
;                   with nothing to undo
;   mega_dosparkf   THE SNAPSHOT, where kern_big writes HIBERNAT.IMG: all of
;                   conventional memory copied into the park and a sparse sum
;                   taken over it and over the kernel's state above 1MB. It
;                   returns TWICE, the way the image's wake address does: AL =
;                   0 now, and AL = 1 when the session comes back, at which
;                   point hbm_dosrun frees the claims and jumps to hbm_wake
;   mega_dosgo      at the handover: the staged launch block's kern_mega tail,
;                   the IVT's tick and keyboard back to the BIOS's, and out of
;                   protected mode - CR0 on a 386, the shutdown-0Ah reset on a
;                   286 (mega_r286's one branch) - into mega_dropr, which puts
;                   the 8259 back at 08h and enters the existing loader stub
;
; THE WAY BACK is kern_dos's `kd_unpark` (kerndos/kdpark.inc, -DKD_MEGA): it
; fetches the park stub out of the park's header with `int 15h AH=87h` into
; the text framebuffer and jumps to it. The stub (kernel/megapark.inc, in
; HIBER.DRV's image, never run there) opens A20, enters protected
; mode on a GDT of its own, re-takes the sum and either restores conventional
; memory and far-jumps to mega_dwake, or - the sum differs - says so on the
; glass, waits for a key and restarts the machine (12.1 item 3).
;
; Everything here is assembled only with -DKERN_MEGA (mega.inc's scope).
; =============================================================================

; --- the kern_mega tail of the launch block, MIRRORED (kerndos/kdlaunch.inc) --
KDL_CAP     equ 10              ; dw KB the machine has: lowered to the park
KDL_XBASE   equ 488             ; dd the XMS arena's linear base, 0 = none
KDL_XKB     equ 492             ; dw ...and its KB
KDL_PBASE   equ 494             ; dd the park's linear base, 0 = no park
KDL_PSUM    equ 498             ; dd the sum
KDL_PKB     equ 502             ; dw KB parked
KDL_PLOW    equ 504             ; dw the lowest paragraph left in place, 0 = none
KDL_POUT    equ 506             ; dw ms the copy and sum took
MPK_LBNAME  equ 106             ; the launch block's body: dos_name first

; --- the park block, at [mega_dpbase] ----------------------------------------
; THE HEADER IS NOT SUMMED: it is what the way back runs and reads, written
; partly after the sum, and a header a program damaged fails as a stub that
; cannot run - a reset - or as a list that sums the wrong memory, a mismatch.
MPK_GDT     equ 0x380           ; +000 the stub; +380 its GDT, six entries:
                                ;      08 code and 10 data at the park, 18 and
                                ;      20 windows it moves, 28 the text page
MPK_C_PBASE equ 0x3B0           ; dd the park, for the restore's windows
MPK_C_PSUM  equ 0x3B4           ; dd the sum
MPK_C_PKB   equ 0x3B8           ; dw KB parked
MPK_C_POUT  equ 0x3BA           ; dw ms out
MPK_C_PACC  equ 0x3BC           ; dd the stub's own PIT clocks
MPK_C_PPREV equ 0x3C0           ; dw ...and its last count
MPK_C_KGDTR equ 0x3C2           ; 6 the kernel's GDTR...
MPK_C_KIDTR equ 0x3C8           ; 6 ...and IDTR, which it loads last
MPK_C_WAKE  equ 0x3CE           ; dd where it jumps then: mega_dwake for a
                                ;    park, mega_hbwake for a hibernate (W5)
MPK_C_END   equ 0x3D2
MPK_MAGIC   equ 0x400           ; +400 dd 'PARK'
MPK_NREG    equ 0x404           ; +404 dw regions the sum covers
MPK_NCPY    equ 0x406           ; +406 dw COPIES before the restore (W5)
MPK_REG     equ 0x408           ; +408 {dd linear base, dd 64-byte LINES} x n,
                                ;      then {dd from, dd to, dd bytes} x NCPY
MPK_CPYSZ   equ 12
MPK_RMAX    equ MX_NBLK + 10      ; every pool block (mega.inc's table, which
                                ; holds the kernel's own blocks too - not
                                ; XM_MAX_BLKS) and the fixed regions: a full
                                ; list DROPS a region (mega_dreg_add)
MPK_CMAX    equ MX_NBLK + 1     ; a hibernate's copies: the tables and every
                                ; pool block
MPK_PCPY    equ 4               ; a PARK's copies: the table bytes the sum's
                                ; 64-byte lines cannot cover (mega_dsave)...
MPK_RUNMAX  equ MEM_MAX + 1     ; ...and one per RUN IN USE past the first
                                ; (mega_dmapb): [0, mem_base) and the claims
                                ; can make no more runs than there are of them
MPK_SLACK   equ 16              ; KB a runs park is claimed over the count,
                                ; for what is claimed between the claim and
                                ; the snapshot (a refusal there is the prefix)
MPK_SAVEL   equ 2               ; the banked table bytes' lines (MPK_SAVE)
%if MPK_RMAX * 8 + (MPK_PCPY + MPK_RUNMAX) * MPK_CPYSZ >= MPK_CMAX * MPK_CPYSZ
MPK_LIST    equ MPK_RMAX * 8 + (MPK_PCPY + MPK_RUNMAX) * MPK_CPYSZ ; the
%else                           ; larger of the two lists: MPS_REG's bytes
MPK_LIST    equ MPK_CMAX * MPK_CPYSZ
%endif
                                ; THE REST OF THE HEADER IS DERIVED from the
                                ; lists, which grow with mx_tab (MX_NBLK,
                                ; KERN-MEGA-PLAN 14.49). At 24 entries it is
                                ; the fixed layout it was: +700, +800, +800
MPK_SAVE    equ (MPK_REG + MPK_LIST + 63) & ~63 ; ...banked here, on a line:
                                ; GDT line 0 (+0), 1FF8h-1FFDh (+40) and IDT
                                ; 20h-21h (+70) - two lines, summed with the
                                ; list (from +400)
MPK_STACK   equ MPK_SAVE + MPK_SAVEL * 64 + 0x80 ; the stub's stack top, 128
                                ; bytes over the bank and under the image
MPK_HDR     equ (MPK_STACK + 1023) & ~1023 ; the image of linear 0 .. P, on
                                ; a KB (hbm_xneed counts the header in KB)
MPK_ANCHOR1 equ 'PK'            ; W7: GDT entry 0 while a park stands - these
MPK_ANCHOR2 equ 'RS'            ; two words, then the park's dd base
MPK_RESETCODE equ (0x80 | 9) << 8 ; W7's exit code: KDC_FAIL with DER_RESET
                                ; (apps/dos), so the box says the program was
                                ; stopped by a restart and gives its prompt
MPK_MIN_KB  equ 128             ; a park smaller than this is no park: kern_dos
                                ; was ~42KB at 0x600 (SPEC.md 96.40) and is in
                                ; the HMA since W4, but the stage still sits
                                ; 16KB under P, clear of its low half
%if MPK_REG + MPK_RMAX * 8 > MPK_HDR
 %error "the park's region list outgrew its header"
%endif
%if MPK_C_END > MPK_MAGIC
 %error "the park header's cells run into its list"
%endif
%if MPK_SAVE + MPK_SAVEL * 64 + 0x80 > MPK_STACK
 %error "the banked table bytes run into the park stub's stack"
%endif
%if MPK_REG + MPK_LIST > MPK_SAVE
 %error "the park's lists outgrew its header, into the banked table bytes"
%endif

; MPK_SUMLINES - THE SUM's inner loop, ONCE for both of its takers (the
; kernel's mega_dsum and the stub's .sum). DS = a 64KB window, CX = 64-byte
; lines (1..1024); AX = s1 and DX = s2 carried across calls. Clobbers BX, DI.
;
; SPARSE BUT DISTRIBUTED (12.1 item 3): one word in every 64-byte line, and
; the word's place in the line steps 22 bytes (mod 64) from one line to the
; next, so every even offset of a line is sampled somewhere in each 32 lines.
; A window always starts at phase 0 (1024 x 22 is 0 mod 64), which is what
; makes the two takers agree whatever their chunking. The sums are Fletcher's,
; mod 65535: a zeroed run that happens to add to zero still moves s2.
%macro MPK_SUMLINES 0
    xor bx, bx
    xor di, di
%%l:
    add ax, [bx+di]
    adc ax, 0
    add dx, ax
    adc dx, 0
    add di, 22
    and di, 62
    add bx, 64
    loop %%l
%endmacro

section .text
cpu 286

mega_dpbase dw 0, 0                 ; the park, 0 = none
mega_dpkb   dw 0                    ; KB of conventional memory it holds
mega_dxbase dw 0, 0                 ; the XMS arena
mega_dxkb   dw 0
mega_dsum   dw 0, 0                 ; s1, s2 at the snapshot
mega_dss    dw 0                    ; the snapshot's stack: mega_dwake's
mega_dsp    dw 0
mega_dimr1  db 0                    ; the 8259's masks at the snapshot
mega_dimr2  db 0
mega_d8042  db 0xFF                 ; the 8042's command byte at the snapshot,
                                    ; FFh = not read (mega_d8042get)
mega_dkm1   db 0                    ; ...and the ones kern_dos is handed
mega_dkm2   db 0
mega_dstg   dw 0                    ; the staging segment, for mega_dropr
mega_dwoke  db 0                    ; 1 = mega_dosparkf's second return
mega_drop   db 0                    ; 1 = mega_r286's next landing is a drop
mega_drom   dw 0                    ; W6: the IRQ lines an option ROM owns
mega_dout   dw 0                    ; ms out (the copy and the sum)...
mega_dback  dw 0                    ; ...and back (the stub's sum and copy)
mega_dpit   dw 0, 0, 0              ; PIT ch0's last count, and clocks since
mega_parkcap dw 0xFFFF              ; KB: THE TEST'S CAP ON THE PARK. A 1MB AT
                                    ; is a pool of ~190KB; Bochs is 16MB, so
                                    ; `os88bochs.py dbg "setpmem <linear> 2
                                    ; 0x00C0"` makes it the 1MB machine (12.1
                                    ; item 5). Nothing in the kernel writes it
mega_dname  times 12 db ' '         ; ...and the program's name
HS_STAGE_PARA equ 0x400             ; the 16KB a stage is (HS_STACK's top)
mega_dmsw   dw 0                    ; the MSW at the snapshot
mega_dparked db 0                   ; 1 from the snapshot to the drop: hbm_down
                                    ; tears nothing down (0 again in the copy)
mega_drmode db 0                    ; 1: THE RUNS IN USE are parked, not 0..P
mega_dikb   dw 0                    ; KB the park's image holds (P, or the runs)
mega_dtkb   dw 0                    ; ...and conventional memory's, mapped
mega_dpchk  dw 0, 0, 0              ; MPK_SAVE's first six: the KB and the base
mega_dclkb  dw 0                    ; KB claimed for it
MEGA_DMAPKB equ 768                 ; MPS_MAP's reach: past any conventional
                                    ; top - a bit a KB, set = in use
mega_dnreg  dw 0                    ; the header's MPK_NREG...
mega_dncpy  dw 0                    ; ...and MPK_NCPY (0 on a park)
; THE PARK'S SCRATCH (KERN-MEGA-SEGMENTS-PLAN W3, KERN-MEGA-PLAN 14.45): the
; three BUFFERS the trip builds - the region and copy list, the KB-in-use
; map and the header's GDT and cells - are not in `.text`. They are the tail
; of `.mega`'s RUNG, past its last byte (MEGA_SCR_OFF), reached through their
; own data selector MEGA_SEL_PSCR, and so above 1MB with `.mega` on every
; machine that copies it there. Not the park block itself, which section 3.6
; proposed: the list and the map are built BEFORE any block exists (the
; claim sizes itself off the map; a hibernate's write and its resume build
; the list with no block at all). The tail is NOT SUMMED - the high kernel's
; region stops at `.mega`'s last whole line - which the header's cells need,
; being written after the sum; the list and the map are copied into the
; header or read before it. Nothing in it outlives one trip or one image, and
; every user writes before it reads. Offsets in MEGA_SEL_PSCR, and EVERY
; ACCESS NAMES A SEGMENT: an MPS_ offset read through DS is the kernel's
; bytes at that offset
MPS_HDR     equ 0                   ; 'PARK', NREG, NCPY: the header's +400,
                                    ; filled from the .text cells at the copy
MPS_REG     equ 8                   ; the list, MPK_LIST bytes (the header's
                                    ; +408, copied with the 8 before it)
MPS_MAP     equ MPS_REG + MPK_LIST  ; the KB in use, a bit each
MPS_HC      equ MPS_MAP + MEGA_DMAPKB / 8 ; the header's +380 .. MPK_C_END
MPS_SIZE    equ MPS_HC + MPK_C_END - MPK_GDT
; mega_dsave - THE TABLE BYTES THE SUM CANNOT COVER (KERN-MEGA-DOS-PLAN 10.4's
; lesser items), banked in the header at the snapshot and put back by the
; stub's copy list before it loads the kernel's tables: GDT entries 1-5 and 7
; (0 is W7's anchor, 6 is ODD's, written per use), 1FF8h-1FFDh (the sum stops
; short of XA and XB, which it moves itself, on a 64-byte line) and IDT gates
; 20h-21h (mega_ivt_sync rebuilds from 22h). {dd linear, dw bytes, dw at}
mega_dsave  dd MEGA_GDT_LIN + 0x08
            dw 0x28, MPK_SAVE + 0x08
            dd MEGA_GDT_LIN + 0x38
            dw 0x08, MPK_SAVE + 0x38
            dd MEGA_GDT_LIN + 0xFFC0
            dw 0x30, MPK_SAVE + 0x40
            dd MEGA_IDT_LIN + 0x100
            dw 0x10, MPK_SAVE + 0x70
mega_dsave_end:
%if (mega_dsave_end - mega_dsave) / 8 != MPK_PCPY
 %error "mega_dsave and MPK_PCPY disagree"
%endif
mega_hbwk   dw 0, 0                 ; W5: where a hibernated image wakes -
                                    ; hbm_wake, HIBER.DRV's CS - stamped
                                    ; BEFORE its picture is taken
mega_hbgdtr dw 47, 0, 0             ; ...the stub's GDTR, at the staging block
mega_hbxl   dw 0, 0                 ; HIBER.DRV's scratch for the extended
mega_hbxd   dw 0, 0                 ; section: its bytes, bytes on the disk,
mega_hbs    dw 0, 0                 ; a run's source and what is left of it,
mega_hbr    dw 0, 0                 ; the bounce's linear base and its fill
mega_hbbl   dw 0, 0                 ; (hiber.inc's hbm_xwrite, hbm_xres)
mega_hbf    dw 0
%ifdef MEGA_DIAG
mega_dstr   db "DOSP 0000K o 0000 b 0000", 0    ; the trip, on the menu bar:
                                    ; KB parked, ms out, ms back (decimal;
                                    ; MEGADIAG=1, KERN-MEGA-PLAN 14.65)
%endif
mega_s_kbdos db "Use this disk's DOS.O88", 0 ; mega_pkgok's refusal

; -----------------------------------------------------------------------------
; mega_dosclaimf - the park and the arena (far; HIBER.DRV, PM, DS = KDATA)
; out: CF = 0 claimed; CF = 1 not even MPK_MIN_KB could be had
;
; The park wants THE RUNS IN USE (mega_dmapb) and MPK_SLACK over them, and
; the header; and when they do not fit - or the cap is under them - [mem_top]
; x 16 bytes, capped by [mega_parkcap], and if the pool will not hold that,
; 16KB less each try down to MPK_MIN_KB - KERN-MEGA-DOS-PLAN 12.1 item 5:
; park what fits, leave the rest IN PLACE and give the program less
; (mega_dosgo lowers KDL_CAP to the park). A runs park gives it everything.
; THE ARENA IS EVERYTHING LEFT (item 7): the pool's free KB, 16KB less each
; try while first fit cannot place it in one run.
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_dosclaimf:
    pusha
    call COLD_SEG:dsk_rah_dropf     ; the read-ahead store's KB to the park
    xor ax, ax                      ; first (it is re-taken at the next mount)
    mov [mega_dpbase], ax
    mov [mega_dpbase+2], ax
    mov [mega_dxbase], ax
    mov [mega_dxbase+2], ax
    mov [mega_dxkb], ax
    mov ax, [mem_top]
    mov cl, 6
    shr ax, cl                      ; paragraphs -> KB
    cmp ax, [mega_parkcap]
    jbe .want
    mov ax, [mega_parkcap]
.want:
    mov [mega_dpkb], ax
    mov byte [mega_drmode], 0
    call mega_dmapb                 ; AX = KB in use: THE RUNS, if they fit
    add ax, MPK_SLACK               ; under the cap and are worth it (a park
    cmp ax, [mega_dpkb]             ; of nearly all of it is the prefix's
    jae .ptry                       ; one copy, and needs no list)
    mov [mega_dclkb], ax
    mov cx, ax
    add cx, MPK_HDR / 1024
    call .alloc
    jc .ptry
    mov byte [mega_drmode], 1
    jmp short .pgot1
.ptry:
    mov cx, [mega_dpkb]
    cmp cx, MPK_MIN_KB
    jb .no
    add cx, MPK_HDR / 1024
    call .alloc                     ; CX KB -> DX:AX, CF
    jnc .pgot
    sub word [mega_dpkb], 16
    jmp short .ptry
.pgot:
    mov cx, [mega_dpkb]
    mov [mega_dclkb], cx
.pgot1:
    mov [mega_dpbase], ax
    mov [mega_dpbase+2], dx
    call mx_caps                    ; AX = free KB
    and al, 0xF0
.xtry:
    cmp ax, 16
    jb .ok
    mov cx, ax
    push cx
    call .alloc
    pop cx
    jnc .xgot
    mov ax, cx
    sub ax, 16
    jmp short .xtry
.xgot:
    mov [mega_dxbase], ax
    mov [mega_dxbase+2], dx
    mov [mega_dxkb], cx
.ok:
    popa
    clc
    retf
.no:
    popa
    stc
    retf
.alloc:                             ; CX KB -> DX:AX the block, CF = refused
    mov ax, cx
    mov dx, cx
    push cx
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    pop cx
    mov bl, 0xFF                    ; the kernel's own
    jmp mx_alloc

; mega_dosfreef - both claims back (far; after the trip). Clobbers nothing
mega_dosfreef:
    pusha
    mov bl, 0xFF
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    mov cx, ax
    or cx, dx
    jz .px
    call mx_free
.px:
    mov ax, [mega_dxbase]
    mov dx, [mega_dxbase+2]
    mov cx, ax
    or cx, dx
    jz .xx
    call mx_free
.xx:
    xor ax, ax
    mov [mega_dpbase], ax
    mov [mega_dpbase+2], ax
    mov [mega_dxbase], ax
    mov [mega_dxbase+2], ax
    popa
    retf

; mega_dpitck - PIT channel 0's count since the last call, added to
; [mega_dpit+2] as 32 bits. Called often enough (a 32KB chunk at most) that
; the counter cannot wrap twice between two of them. Clobbers AX, BX
mega_dpitck:
    xor al, al
    out 0x43, al
    in al, 0x40
    mov ah, al
    in al, 0x40
    xchg al, ah
    mov bx, [mega_dpit]
    mov [mega_dpit], ax
    sub bx, ax
    add [mega_dpit+2], bx
    adc word [mega_dpit+4], 0
    ret

; mega_dreg_add - a region onto the list: DX:AX linear, CX:BX lines
mega_dreg_add:
    push si
    push es
    mov si, [mega_dnreg]
    cmp si, MPK_RMAX
    jae .full
    inc word [mega_dnreg]
    shl si, 3
    push word MEGA_SEL_PSCR
    pop es
    mov [es:si+MPS_REG], ax
    mov [es:si+MPS_REG+2], dx
    mov [es:si+MPS_REG+4], bx
    mov [es:si+MPS_REG+6], cx
.full:
    pop es
    pop si
    ret

; -----------------------------------------------------------------------------
; mega_dmapb - THE KB IN USE (KERN-MEGA-DOS-PLAN 12.1 item 5's "park only the
; runs in use"), a bit each in MPS_MAP: [0, mem_base) - the IVT, the
; kernel, the stacks - and every live claim, rounded out to whole KB. A FREE
; RUN IS NOBODY'S: mem_tab is out of band, so free heap holds no state, and
; a park of the rest restores everything the session can see.
; out: AX = KB marked, CX = runs (maximal stretches). Clobbers BX DX SI DI BP
; -----------------------------------------------------------------------------
mega_dmapb:
    push es
    push word MEGA_SEL_PSCR
    pop es
    mov di, MPS_MAP
    mov cx, MEGA_DMAPKB / 16
    xor ax, ax
    cld
    rep stosw
    pop es
    mov dx, [mem_base]              ; AX = the first KB, DX = the paragraph
    call .mark                      ; past the last
    mov si, mem_tab                 ; (mem_next's walk, which is `.cold`)
.cl:
    mov ax, [ss:si+MC_SEG]
    or ax, ax
    jz .cln
    mov dx, ax
    add dx, [ss:si+MC_PARA]
    mov cl, 6
    shr ax, cl
    call .mark
.cln:
    add si, MC_SIZE
    cmp si, mem_tab + MEM_MAX * MC_SIZE
    jb .cl
.count:
    call mega_dtopkb
    mov di, ax
    xor ax, ax
    xor bp, bp
    xor dl, dl                      ; DL = the last bit
    xor bx, bx
.cn:
    cmp bx, di
    jae .cd
    call .bt
    jz .clr
    inc ax
    or dl, dl
    jnz .nx
    inc bp
    mov dl, 1
    jmp short .nx
.clr:
    xor dl, dl
.nx:
    inc bx
    jmp short .cn
.cd:
    mov cx, bp
    ret
.mark:                              ; [AX KB, DX para rounded up): set
    push bx
    push cx
    push si
    push es
    push word MEGA_SEL_PSCR
    pop es
    add dx, 63
    mov cl, 6
    shr dx, cl
    mov bx, ax
.mk:
    cmp bx, dx
    jae .mkd
    cmp bx, MEGA_DMAPKB
    jae .mkd
    call .bit
    or [es:si+MPS_MAP], ch
    inc bx
    jmp short .mk
.mkd:
    pop es
    pop si
    pop cx
    pop bx
    ret
.bt:                                ; BX = a KB -> ZF = 0 in use. Clobbers
    call .bit                       ; CX SI
    push es
    push word MEGA_SEL_PSCR
    pop es
    test [es:si+MPS_MAP], ch        ; (the pop keeps ZF)
    pop es
    ret
.bit:                               ; BX -> SI = its byte, CH = its bit
    mov cl, bl
    and cl, 7
    mov ch, 1
    shl ch, cl
    mov si, bx
    shr si, 1
    shr si, 1
    shr si, 1
    ret

; mega_dacc - THE ACCESSED BIT on every present code or data descriptor in the
; GDT and the LDT. The CPU sets it the first time a selector is loaded, and the
; park sums both tables in place: the handoff loads the text page's B800 after
; the snapshot, so one bit of its identity entry differed and the stub refused
; the trip. It is bookkeeping the kernel never reads, so it is set here, once,
; before the sum, and no load after it writes a table. Clobbers AX, CX, DI
mega_dacc:
    push es
    mov ax, MEGA_SEL_WIN            ; the GDT, as data
    mov cx, MEGA_GDT_N
    call .one
    mov ax, MEGA_SEL_LDTD           ; ...and the LDT
    mov cx, MEGA_LDT_N
    call .one
    pop es
    ret
.one:
    mov es, ax
    xor di, di
.e:
    mov al, [es:di+5]
    and al, 0x91                    ; present, code/data, not yet accessed
    cmp al, 0x90
    jne .n
    or byte [es:di+5], 1
.n:
    add di, 8
    loop .e
    ret

; mega_dtopkb - AX = conventional memory's KB, as far as MPS_MAP reaches
mega_dtopkb:
    push cx
    mov ax, [mem_top]
    mov cl, 6
    shr ax, cl
    pop cx
    cmp ax, MEGA_DMAPKB
    jbe .r
    mov ax, MEGA_DMAPKB
.r:
    ret

; mega_kblin - AX KB -> DX:AX bytes. Clobbers CL
mega_kblin:
    mov dx, ax
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    ret

; -----------------------------------------------------------------------------
; mega_dosparkf - THE SNAPSHOT (far; HIBER.DRV, PM, DS = KDATA, the lock held
;                 and [sch_lock] raised, as hbm_wrimg's image write has them)
; out: AL = 0 parked, carry on to the drop; AL = 1 the session is BACK (this is
;      mega_dwake returning through the same frame). Every other register is
;      the caller's either way
; -----------------------------------------------------------------------------
mega_dosparkf:
    pushf
    cli
    pusha
    push ds
    push es
    mov byte [mega_dwoke], 0        ; ...and so 0 in the snapshot
    call mega_drom_scan             ; W6: the ROM-owned lines - HERE, before
    mov [mega_drom], ax             ; the snapshot, so the wake reads them too
    smsw ax                         ; ...the MSW's MP, EM and TS, which
    mov [mega_dmsw], ax             ; kd_unpark's `or al, 1` keeps as the
                                    ; program left them (KERN-MEGA-DOS-PLAN
                                    ; 3.3's ledger): an EM a DOS program set
                                    ; would #NM the next package's x87 code
    in al, 0x21                     ; the masks the session comes back with
    mov [mega_dimr1], al
    in al, 0xA1
    mov [mega_dimr2], al
    call mega_d8042get              ; ...and the 8042's command byte, which a
                                    ; DOS program may leave with the keyboard
                                    ; switched off (KERN-MEGA-PLAN 54.3)
    mov word [mega_dpit+2], 0
    mov word [mega_dpit+4], 0
    call mega_dpitck

    call mega_dacc                  ; the tables' accessed bits, set first
    ; --- THE RUNS, re-counted at the snapshot ----------------------------------
    ; mega_dosclaimf sized the claim off a count taken before the volume and
    ; the drivers were seen to; a park they outgrew is the prefix of the claim
    cmp byte [mega_drmode], 0
    je .pfx
    call mega_dmapb                 ; AX = KB in use, CX = runs
    cmp ax, [mega_dclkb]
    ja .fallb
    cmp cx, MPK_RUNMAX
    ja .fallb
    mov [mega_dikb], ax
    xor bx, bx                      ; the first run, from 0: the stub's own
.k0:                                ; restore takes it (MPK_C_PKB), and every
    cmp bx, MEGA_DMAPKB             ; later one is a copy
    jae .k0d
    call mega_dmapb.bt
    jz .k0d
    inc bx
    jmp short .k0
.k0d:
    mov [mega_dpkb], bx
    jmp short .regs
.fallb:
    mov byte [mega_drmode], 0
    mov ax, [mega_dclkb]
    mov [mega_dpkb], ax
.pfx:
    mov ax, [mega_dpkb]
    mov [mega_dikb], ax
.regs:
    ; --- the regions the sum covers -------------------------------------------
    mov word [mega_dnreg], 0
    mov word [mega_dncpy], 0        ; (a hibernate's resume sets it)
    mov bx, [mega_dikb]             ; the park's IMAGE, in 64-byte lines
    mov ax, 16                      ; (KB x 16)
    mul bx
    mov bx, ax
    mov cx, dx
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    add ax, MPK_HDR
    adc dx, 0
    call mega_dreg_add
    cmp byte [mega_drmode], 0       ; (a runs park leaves nothing in place:
    jne .noplace                    ; what it did not take is free)
    mov ax, [mem_top]               ; ...what was left IN PLACE, P .. the top
    mov cl, 6
    shr ax, cl
    sub ax, [mega_dpkb]
    jbe .noplace
    mov bx, 16
    mul bx
    mov bx, ax
    mov cx, dx
    mov ax, [mega_dpkb]
    mov dx, ax
    push cx
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    pop cx
    call mega_dreg_add
.noplace:
    xor cx, cx                      ; the GDT less entries 0-7 (WIN and ODD
    mov bx, (0x1FFE * 8 - 0x40) / 64 ; move) and 1FFEh-1FFFh (the copy's XA
    mov ax, (MEGA_GDT_LIN + 0x40) & 0xFFFF  ; and XB, which this very sum uses)
    mov dx, MEGA_GDT_LIN >> 16
    call mega_dreg_add
    mov bx, MEGA_LDT_N * 8 / 64     ; the LDT
    mov ax, MEGA_LDT_LIN & 0xFFFF
    mov dx, MEGA_LDT_LIN >> 16
    call mega_dreg_add
    mov bx, 4                       ; the IDT, less the gates mega_ivt_sync
    mov ax, MEGA_IDT_LIN & 0xFFFF   ; rebuilds (22h-27h, 70h-77h): the drivers'
    call mega_dreg_add              ; detach rewrites those after this, and
    mov bx, 9                       ; mega_dwake rebuilds them from the IVT
    mov ax, (MEGA_IDT_LIN + 0x28 * 8) & 0xFFFF
    call mega_dreg_add
    mov bx, 17
    mov ax, (MEGA_IDT_LIN + 0x78 * 8) & 0xFFFF
    call mega_dreg_add
    cmp byte [mega_coldhi], 0       ; `.cold` and `.mega`, above 1MB: one
    je .nocold                      ; region, the gap between them with it
    mov bx, (MEGA_HI_LIN + MEGA_SIZE - MEGA_COLD_LIN) / 64
    mov ax, MEGA_COLD_LIN & 0xFFFF
    mov dx, MEGA_COLD_LIN >> 16
    call mega_dreg_add
.nocold:
    mov si, mx_tab                  ; every block the pool has given out,
.mx:                                ; less the park and the arena
    mov ax, [ss:si+MX_KB]
    or ax, ax
    jz .mxn
    mov bx, 16
    mul bx
    mov bx, ax
    mov cx, dx
    mov ax, [ss:si+MX_OFF]
    mov dx, ax
    push cx
    mov cl, 10
    shl ax, cl
    mov cl, 6
    shr dx, cl
    pop cx
    add dx, MEGA_XPOOL >> 16
    cmp ax, [mega_dpbase]
    jne .mx1
    cmp dx, [mega_dpbase+2]
    je .mxn
.mx1:
    cmp ax, [mega_dxbase]
    jne .mx2
    cmp dx, [mega_dxbase+2]
    je .mxn
.mx2:
    call mega_dreg_add
.mxn:
    add si, MX_BLKSZ
    cmp si, mx_tab_end
    jb .mx

    ; --- the table bytes the lines miss, banked, and a copy each back -------
    mov ax, [mega_dpbase]           ; the header's list and the bank, so a
    mov dx, [mega_dpbase+2]         ; damaged list or count cannot come back
    add ax, MPK_MAGIC               ; summing less than was parked
    adc dx, 0
    mov bx, (MPK_SAVE + MPK_SAVEL * 64 - MPK_MAGIC) / 64
    xor cx, cx
    call mega_dreg_add
    push es                         ; the copies, through the scratch
    push word MEGA_SEL_PSCR
    pop es
    mov di, [mega_dnreg]            ; the copies follow the regions
    shl di, 3
    add di, MPS_REG
    mov si, mega_dsave
.sv:
    mov ax, [si]                    ; {from: the bank, to: the table}
    mov dx, [si+2]
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    mov [es:di+4], ax
    mov [es:di+6], dx
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    add ax, [si+6]
    adc dx, 0
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov [es:di], ax
    mov [es:di+2], dx
    mov cx, [si+4]
    mov [es:di+8], cx
    mov word [es:di+10], 0
    push si
    push di
    call KERNEL_SEG:mega_lincopyf
    pop di
    pop si
    inc word [mega_dncpy]
    add di, MPK_CPYSZ
    add si, 8
    cmp si, mega_dsave_end
    jb .sv
    mov ax, [mega_dpkb]             ; the bank's first word, which no copy
    mov [mega_dpchk], ax            ; puts back (entry 0's): the KB and the
    mov ax, [mega_dpbase]           ; base, summed, for the stub's check
    mov [mega_dpchk+2], ax
    mov ax, [mega_dpbase+2]
    mov [mega_dpchk+4], ax
    push di
    xor dx, dx
    mov ax, KERNEL_SEG << 4
    add ax, mega_dpchk
    adc dx, 0
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    add ax, MPK_SAVE
    adc dx, 0
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov cx, 6
    call KERNEL_SEG:mega_lincopyf
    pop di
    cmp byte [mega_drmode], 0       ; --- ...and a copy per run past the first,
    je .norun                       ; packed into the image after it
    call mega_dtopkb
    mov [mega_dtkb], ax
    mov bx, [mega_dpkb]             ; BX = the KB, BP = the image's next KB
    mov bp, bx
.rs:
    cmp bx, [mega_dtkb]
    jae .norun
    call mega_dmapb.bt
    jnz .rgo
    inc bx
    jmp short .rs
.rgo:
    push bx                         ; a run starts here...
.re:
    inc bx
    cmp bx, [mega_dtkb]
    jae .rend
    call mega_dmapb.bt
    jnz .re
.rend:                              ; ...and ends here
    pop ax
    push bx
    mov cx, bx
    sub cx, ax
    push cx
    call mega_kblin                 ; {from: the image, to: the run, bytes}
    mov [es:di+4], ax
    mov [es:di+6], dx
    pop ax
    push ax
    call mega_kblin
    mov [es:di+8], ax
    mov [es:di+10], dx
    mov ax, bp
    call mega_kblin
    add ax, MPK_HDR
    adc dx, 0
    add ax, [mega_dpbase]
    adc dx, [mega_dpbase+2]
    mov [es:di], ax
    mov [es:di+2], dx
    pop ax
    add bp, ax
    pop bx
    inc word [mega_dncpy]
    add di, MPK_CPYSZ
    jmp short .rs
.norun:
    pop es

    ; --- the header: the stub and the list ------------------------------------
    mov bx, sp                      ; the stub is in the CALLER's image -
    mov ax, [ss:bx+24]              ; HIBER.DRV's - so its CS, under the
    call mega_dstubup               ; frame (es ds pusha flags ip CS), says where
                                    ; (the GDT, the cells and the name follow
                                    ; at the drop: mega_dosgo)

    ; --- THE SNAPSHOT POINT ----------------------------------------------------
    ; Everything above is in the copy; everything below is not, and is either
    ; lost on the way back or recomputed there. mega_dwake comes back HERE.
    mov [mega_dss], ss
    mov [mega_dsp], sp

    ; --- conventional memory, 0 .. P, into the park ----------------------------
    xor si, si                      ; SI:BP = the offset, in bytes
    xor bp, bp
.copy:
    mov ax, bp                      ; ...done when SI:BP reaches P
    mov dx, si
    call .tokb
    mov cx, [mega_dpkb]
    sub cx, ax
    jbe .runs
    cmp cx, 32                      ; a whole 32KB, or what is left of P
    jb .last
    mov cx, 32
.last:
    xchg ch, cl                     ; KB -> bytes (x 1024: a byte and 2 bits)
    shl cx, 1
    shl cx, 1
    mov [mega_xsrc], bp
    mov [mega_xsrc+2], si
    mov ax, bp
    mov dx, si
    add ax, MPK_HDR
    adc dx, 0
    add ax, [mega_dpbase]
    adc dx, [mega_dpbase+2]
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    push si
    push bp
    push cx
    call KERNEL_SEG:mega_lincopyf
    call mega_dpitck
    pop cx
    pop bp
    pop si
    add bp, cx
    adc si, 0
    jmp short .copy
.runs:                              ; --- a runs park's other runs, each to
    cmp byte [mega_drmode], 0       ; where its copy will bring it back from
    je .copied
    push es                         ; (the list: the scratch's)
    push word MEGA_SEL_PSCR
    pop es
    mov si, [mega_dnreg]
    shl si, 1
    shl si, 1
    shl si, 1
    add si, MPS_REG + MPK_PCPY * MPK_CPYSZ
    mov bp, [mega_dncpy]
    sub bp, MPK_PCPY
.rc:
    or bp, bp
    jz .rcd
    mov ax, [es:si+4]
    mov [mega_xsrc], ax
    mov ax, [es:si+6]
    mov [mega_xsrc+2], ax
    mov ax, [es:si]
    mov [mega_xdst], ax
    mov ax, [es:si+2]
    mov [mega_xdst+2], ax
    mov ax, [es:si+8]               ; DX:AX = bytes left
    mov dx, [es:si+10]
.rch:
    mov cx, ax
    or cx, dx
    jz .rnext
    mov cx, 0x8000
    or dx, dx
    jnz .rfull
    cmp ax, cx
    jae .rfull
    mov cx, ax
.rfull:
    sub ax, cx
    sbb dx, 0
    push ax
    push dx
    push cx
    push si
    push bp
    call KERNEL_SEG:mega_lincopyf
    call mega_dpitck
    pop bp
    pop si
    pop cx
    pop dx
    pop ax
    add [mega_xsrc], cx
    adc word [mega_xsrc+2], 0
    add [mega_xdst], cx
    adc word [mega_xdst+2], 0
    jmp short .rch
.rnext:
    add si, MPK_CPYSZ
    dec bp
    jmp short .rc
.rcd:
    pop es
.copied:
    call mega_dsumall               ; DX:AX = s2:s1
    mov [mega_dsum], ax
    mov [mega_dsum+2], dx
    call mega_dpitck
    mov ax, [mega_dpit+2]           ; mode 2: one count a clock
    mov dx, [mega_dpit+4]
    mov bx, 1193
    cmp dx, bx
    jae .slow
    div bx
    jmp short .tout
.slow:
    mov ax, 0xFFFF
.tout:
    mov [mega_dout], ax
    mov si, mega_m_park             ; "kern_mega: parked kkkK at pppppp sum
    call mm_puts                  ;  ssss:ssss" on port E9 (KERN-MEGA-DOS-
    mov ax, [mega_dikb]             ; PLAN 10's W1 line), so a break-on-purpose
    call mm_hex4                  ; run knows where to write
    mov si, mega_m_at
    call mm_puts
    mov ax, [mega_dpbase+2]
    call mm_hex2
    mov ax, [mega_dpbase]
    call mm_hex4
    mov si, mega_m_sum
    call mm_puts
    mov ax, [mega_dsum+2]
    call mm_hex4
    mov al, ':'
    call mm_putc
    mov ax, [mega_dsum]
    call mm_hex4
    mov al, 10
    call mm_putc
.ret:
    pop es
    pop ds
    popa
    mov al, [mega_dwoke]
    popf
    retf
.tokb:                              ; DX:AX bytes -> AX KB. Clobbers CX
    mov cl, 10
    shr ax, cl
    mov cl, 6
    shl dx, cl
    or ax, dx
    ret

; mega_dsumall - the sum over the scratch's list, through MEGA_SEL_XA. out DX:AX =
; s2:s1. Clobbers BX CX SI DI BP
mega_dsumall:
    push es
    push word MEGA_SEL_PSCR         ; (the list: the scratch's)
    pop es
    xor ax, ax
    xor dx, dx
    mov si, MPS_REG
    mov bp, [mega_dnreg]
.reg:
    or bp, bp
    jz .done
    push word [es:si+6]             ; [sp+2]:[sp] = lines left, as 32 bits
    push word [es:si+4]
    push word [es:si+2]             ; ...and the window, as 32 bits
    push word [es:si]
.chunk:
    mov bx, sp
    mov cx, [ss:bx+4]
    or cx, [ss:bx+6]
    jz .rdone
    mov cx, 1024
    cmp word [ss:bx+6], 0
    jne .n
    cmp [ss:bx+4], cx
    jae .n
    mov cx, [ss:bx+4]
.n:
    sub [ss:bx+4], cx
    sbb word [ss:bx+6], 0
    push ax
    push dx
    push cx
    mov ax, [ss:bx]
    mov dx, [ss:bx+2]
    mov bx, MEGA_SEL_XA
    call KERNEL_SEG:mega_xdescf
    pop cx
    pop dx
    pop ax
    push ds
    push word MEGA_SEL_XA
    pop ds
    MPK_SUMLINES
    pop ds
    push ax
    push bx
    call mega_dpitck
    pop bx
    pop ax
    mov bx, sp
    inc word [ss:bx+2]              ; the next 64KB
    jmp short .chunk
.rdone:
    add sp, 8
    add si, 8
    dec bp
    jmp .reg
.done:
    pop es
    ret

; -----------------------------------------------------------------------------
; mega_dosgo - the handover (far JUMP from hbm_dosrun; PM, IF = 0, ES = the
;              stage, the stub, the extents and the launch block staged)
; Never returns: the session comes back through mega_dwake.
; -----------------------------------------------------------------------------
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_dosgo:
    cli
    mov [mega_dstg], es
    mov di, KDS_LB                  ; --- the staged block's kern_mega tail ---
    mov ax, [mega_dxbase]
    mov [es:di+KDL_XBASE], ax
    mov ax, [mega_dxbase+2]
    mov [es:di+KDL_XBASE+2], ax
    mov ax, [mega_dxkb]
    mov [es:di+KDL_XKB], ax
    mov ax, [mega_dpbase]
    mov [es:di+KDL_PBASE], ax
    mov ax, [mega_dpbase+2]
    mov [es:di+KDL_PBASE+2], ax
    mov ax, [mega_dsum]
    mov [es:di+KDL_PSUM], ax
    mov ax, [mega_dsum+2]
    mov [es:di+KDL_PSUM+2], ax
    mov ax, [mega_dout]
    mov [es:di+KDL_POUT], ax
    mov ax, [mega_dpkb]
    mov [es:di+KDL_PKB], ax
    mov word [es:di+KDL_PLOW], 0
    cmp byte [mega_drmode], 0       ; (a runs park took all that is in use)
    jne .capok
    cmp [es:di+KDL_CAP], ax         ; a PARTIAL park: the program's arena
    jbe .capok                      ; ends below the first byte left in place
    mov [es:di+KDL_CAP], ax         ; (12.1 item 5)
    mov cl, 6
    shl ax, cl
    mov [es:di+KDL_PLOW], ax
.capok:
    mov si, clk_sec                 ; the clock and the BIOS tick it was read
    lea bx, [di+KDL_CLK]            ; at, which hbm_wake's catch-up measures
    mov cx, 10                      ; the program's run from (SPEC.md 96.49.5)
.clk:
    mov al, [si]
    mov [es:bx], al
    inc si
    inc bx
    loop .clk
    push ds
    push word MEGA_BDA
    pop ds
    mov ax, [0x6C]
    mov dx, [0x6E]
    pop ds
    mov [es:di+KDL_TICK], ax
    mov [es:di+KDL_TICK+2], dx
    mov [es:di+KDL_STAGE], es

    ; --- the header's GDT and cells, and the program's name -------------------
    mov ax, mega_dwake
    call mega_dhcfill
    lea si, [di+MPK_LBNAME]         ; the name, spaces past its NUL
    mov bx, mega_dname
    mov cx, 12
.nm:
    mov al, [es:si]
    or al, al
    jz .nmpad
    mov [bx], al
    inc si
    inc bx
    loop .nm
    jmp short .nmd
.nmpad:
    mov byte [bx], ' '
    inc bx
    loop .nmpad
.nmd:
    xor dx, dx                      ; ...and into the header
    mov ax, KERNEL_SEG << 4
    add ax, mega_dname
    adc dx, 0
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    add ax, mpk_s_name - mpk_stub
    adc dx, 0
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov cx, 12
    push es
    call KERNEL_SEG:mega_lincopyf
    pop es
    mov di, KDS_LB

    ; --- the IVT's tick and keyboard: the BIOS's, for kern_dos ---------------
    ; mouse_unhook and the kernel's tail put mega's BCODE halves there
    push es
    push word MEGA_SEL_LOW0
    pop es
    mov ax, [mega_old08]
    mov [es:0x08*4], ax
    mov ax, [mega_old08+2]
    mov [es:0x08*4+2], ax
    mov ax, [mega_old09]
    mov [es:0x09*4], ax
    mov ax, [mega_old09+2]
    mov [es:0x09*4+2], ax
    pop es

    ; --- W7: THE ANCHOR, where a warm reset's boot looks first --------------
    ; GDT entry 0, which the CPU never reads, and outside the sum (the GDT's
    ; first line is not in it): a magic and the park's base. mega_dwake
    ; clears it on the way home and mega_wrprobe on the way in
    push es
    mov ax, MEGA_SEL_WIN
    mov es, ax
    mov word [es:0], MPK_ANCHOR1
    mov word [es:2], MPK_ANCHOR2
    mov ax, [mega_dpbase]
    mov [es:4], ax
    mov ax, [mega_dpbase+2]
    mov [es:6], ax
    pop es

    ; --- the masks kern_dos is handed: hbm_handover's -------------------------
    in al, 0x21
    mov [es:HS_MASKM], al
    or al, 0x1A                     ; IRQ 1, 3, 4: the keyboard and the mice
    mov [mega_dkm1], al
    mov byte [es:HS_SLAVE], 1
    in al, 0xA1
    mov [es:HS_MASKS], al
    or al, 0x10                     ; IRQ 12
    mov [mega_dkm2], al
    ; --- W6: AN OPTION ROM's LINE IS LEFT OPEN (KERN-MEGA-DOS-PLAN 9.2) -----
    ; What POST would have left: the PicoMEM's multiplexer runs only while its
    ; own line is unmasked (SPEC.md 34.10.1), and nothing in a DOS program
    ; knows to open it. Both mask pairs: the one the loader stub restores
    ; (HS_MASKM/S, which kern_dos runs under) and the one mega_dropr sets
    mov ax, [mega_drom]             ; the ROM-owned lines, as the snapshot
    or ah, ah                       ; scanned them (mega_dosparkf)
    jz .nocas
    or al, 4                        ; a slave line needs the cascade
.nocas:
    not ax
    and [es:HS_MASKM], al
    and [mega_dkm1], al
    and [es:HS_MASKS], ah
    and [mega_dkm2], ah
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    mov al, 0x8D                    ; NMI off across the switch
    out 0x70, al
    jmp KERNEL_SEG:mega_dosgo_lo    ; ...and down, from below 1MB
section .text                   ; the drop itself (KERN-MEGA-SEGMENTS-PLAN 3.6)
mega_dosgo_lo:
    cmp byte [cpu_tier], CPU_386
    jb .r286
cpu 386
    mov eax, cr0                    ; REAL MODE, the 386's way
    and al, 0xFE
    mov cr0, eax
cpu 286
    db 0xEA
    dw mega_dropr, KERNEL_SEG
.r286:                              ; ...and the 286's (W2): the reset
    mov byte [mega_drop], 1         ; mega_svc.r286 takes, landing in
    mov [mega_rss], ss              ; mega_r286, whose foot sends a drop here
    mov [mega_rsp], sp
    push word MEGA_BDA
    pop es
    mov word [es:0x67], mega_r286
    mov word [es:0x69], KERNEL_SEG
%ifdef MEGA_DIAG
    mov ax, [es:0x3E]
    mov [mega_fdpre], ax
%endif
    mov al, 0x8F
    out 0x70, al
    mov al, 0x0A
    out 0x71, al
    lidt [mega_nullidt]
    int 3
.hang:
    hlt
    jmp short .hang
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)

; mega_hdesc - [ES:BX] = a 64KB descriptor, base DX:AX, access CL
mega_hdesc:
    mov word [es:bx], 0xFFFF
    mov [es:bx+2], ax
    mov [es:bx+4], dl
    mov [es:bx+5], cl
    mov word [es:bx+6], 0
    ret

; -----------------------------------------------------------------------------
; mega_dstubup - THE STUB AND THE LIST into the block at [mega_dpbase]
; in:  AX = the code selector of the image the stub is in (HIBER.DRV's)
; Clobbers AX BX CX DX SI DI
; -----------------------------------------------------------------------------
mega_dstubup:
    call MEGA_SEL_CODE:mega_selbasef               ; DX:AX = the image's base
    add ax, mpk_stub
    adc dx, 0
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov cx, (mpk_stub_end - mpk_stub + 1) & 0xFFFE
    call KERNEL_SEG:mega_lincopyf
    push es                         ; ...and the list, magic and counts first,
    push word MEGA_SEL_PSCR         ; in front of it in the scratch
    pop es
    mov word [es:MPS_HDR], 'PA'
    mov word [es:MPS_HDR+2], 'RK'
    mov ax, [mega_dnreg]
    mov [es:MPS_HDR+4], ax
    mov ax, [mega_dncpy]
    mov [es:MPS_HDR+6], ax
    pop es
    mov ax, MEGA_SEL_PSCR
    call mega_selbase               ; DX:AX = the scratch's base
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    add ax, MPK_MAGIC
    adc dx, 0
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov cx, 8 + MPK_LIST
    call KERNEL_SEG:mega_lincopyf   ; (low: `.mega` reaches it far)
    ret

; -----------------------------------------------------------------------------
; mega_dhcfill - THE HEADER'S GDT AND CELLS, staged in the scratch (MPS_HC)
; and copied up
; in:  AX = the wake (a `.mega` offset); ES = the text page (28h);
;      [mega_dpbase], [mega_dsum], [mega_dpkb], [mega_dout]
; Clobbers AX BX CX DX SI; DI and ES kept
; -----------------------------------------------------------------------------
mega_dhcfill:
    push di
    push es
    mov bx, es                      ; BX = the text page, before ES moves
    push word MEGA_SEL_PSCR
    pop es
    mov [es:MPS_HC + MPK_C_WAKE - MPK_GDT], ax
    mov word [es:MPS_HC + MPK_C_WAKE - MPK_GDT + 2], MEGA_SEL_CODE ; `.mega`'s
    push bx
    mov ax, [mega_dpbase]           ; 08 code and 10 data, at the block
    mov dx, [mega_dpbase+2]
    mov [es:MPS_HC + MPK_C_PBASE - MPK_GDT], ax
    mov [es:MPS_HC + MPK_C_PBASE - MPK_GDT + 2], dx
    mov bx, MPS_HC + 0x08
    mov cl, 0x9A
    call mega_hdesc
    mov bx, MPS_HC + 0x10
    mov cl, 0x92
    call mega_hdesc
    pop ax                          ; 28 the text page: the stage's own
    mov dx, ax                      ; segment, which is B000 or B800
    shl ax, 4
    shr dx, 12
    mov bx, MPS_HC + 0x28
    call mega_hdesc
    mov ax, [mega_dsum]
    mov [es:MPS_HC + MPK_C_PSUM - MPK_GDT], ax
    mov ax, [mega_dsum+2]
    mov [es:MPS_HC + MPK_C_PSUM - MPK_GDT + 2], ax
    mov ax, [mega_dpkb]
    mov [es:MPS_HC + MPK_C_PKB - MPK_GDT], ax
    mov ax, [mega_dout]
    mov [es:MPS_HC + MPK_C_POUT - MPK_GDT], ax
    mov si, mega_gdtr               ; the kernel's own GDTR and IDTR
    mov di, MPS_HC + MPK_C_KGDTR - MPK_GDT
    mov cx, 6
    rep movsw
    mov ax, MEGA_SEL_PSCR           ; ...and all of it into the header
    call mega_selbase
    add ax, MPS_HC
    adc dx, 0
    mov [mega_xsrc], ax
    mov [mega_xsrc+2], dx
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    add ax, MPK_GDT
    adc dx, 0
    mov [mega_xdst], ax
    mov [mega_xdst+2], dx
    mov cx, MPK_C_END - MPK_GDT
    call KERNEL_SEG:mega_lincopyf
    pop es
    pop di
    ret

; -----------------------------------------------------------------------------
; mega_dropr - REAL MODE, at the end of either way down (IF = 0, NMI off)
; The 8259 back at 08h with every line masked (ICW1 resets its latches, so the
; masks go on first), kern_dos's masks, NMI on, and into the loader stub the
; kernel staged - hbm_handover's last instruction, with no kernel left to run.
; -----------------------------------------------------------------------------
section .text                   ; (KERN-MEGA-SEGMENTS-PLAN: back from .mega)
mega_dropr:
    cli
    mov ax, KERNEL_SEG
    mov ds, ax
    mov byte [mega_drop], 0
    mov ax, ss                      ; LOW_SEG: the selector IS the segment
    mov ss, ax
    lidt [mega_rmidtr]
    mov es, [mega_dstg]
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    mov al, 0x11                    ; ICW1
    out 0x20, al
    mov al, 0x08                    ; ICW2: the BIOS's base
    out 0x21, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x01
    out 0x21, al
    mov al, [mega_dkm1]
    out 0x21, al
    mov al, [mega_dkm2]
    out 0xA1, al
    call mega_kdrain                ; a byte the ICW1 took the edge of would
                                    ; leave the PROGRAM no keyboard (14.62)
    mov al, 0x0D                    ; NMI on (index 0Dh, read-only)
    out 0x70, al
%ifdef DOSR_MARK
    ; DOSRMARK=1: the stage stays in the framebuffer, as on kern_big, so the
    ; loader stub's own HBMARK trace (CS-relative) is on the glass - and this
    ; landing says it got here, at column 0 where kd_resume puts its 'J'.
    ; Bochs cannot run this arm: it will not fetch from video memory.
    mov byte [es:HM_ROW], 'D'
    mov byte [es:HM_ROW + 1], 0x07
    push es
    push word HS_CODE
    retf
%endif
    ; **THE STAGE IS RUN BELOW THE TOP OF THE PARK, NOT IN THE FRAMEBUFFER.**
    ; It is position-independent (hbstage.inc's offsets), what it overwrites
    ; is parked by now (or, for a runs park, free), and the 16KB under the top
    ; is clear of every place the loader writes - the FAT snapshot and mount
    ; buffers at 0060h and kern_dos's packed stream at KDS_TMPSEG, kern_dos
    ; itself going to the HMA (W4) - for any park of MPK_MIN_KB or more. Bochs
    ; will not fetch an instruction out of video memory at all, and a park has
    ; no reason to want to.
    mov ax, [mem_top]               ; (a runs park: under the top, which
    cmp byte [mega_drmode], 0       ; is parked or is free)
    jne .stg
    mov ax, [mega_dpkb]
    mov cl, 6
    shl ax, cl
.stg:
    sub ax, HS_STAGE_PARA
    push ds
    push es
    pop ds
    mov es, ax
    xor si, si
    xor di, di
    mov cx, HS_STAGE_PARA * 8
    cld
    rep movsw
    pop ds
    push es
    push word HS_CODE
    retf                            ; into the stub, IF = 0

; -----------------------------------------------------------------------------
; mega_dwake - THE SESSION IS BACK (PM, entered by the park stub's far jump
;              with the real GDTR and IDTR loaded, IF = 0, NMI off).
; in:  BX = ms out, CX = ms back
; The stack the snapshot banked, the 8259 at 20h with its parked masks, NMI
; on, the IDT's IRQ gates rebuilt from the restored IVT, and out through
; mega_dosparkf's own epilogue with AL = 1.
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_dwake:
    ; STKBALANCE-OK: abandons the stub's stack on purpose - SP goes back to the frame mega_dosparkf banked
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov ax, MEGA_SEL_LDT
    lldt ax
    mov ss, [mega_dss]
    mov sp, [mega_dsp]
    lmsw [mega_dmsw]                ; the MSW the session parked with
    mov [mega_dout], bx
    mov [mega_dback], cx
    push es                         ; W7's anchor goes: the session is home
    mov ax, MEGA_SEL_WIN
    mov es, ax
    xor ax, ax
    mov [es:0], ax
    mov [es:2], ax
    mov [es:4], ax
    mov [es:6], ax
    pop es
    call KERNEL_SEG:mega_pic_pmf                ; 08h -> 20h
    call mega_d8042put              ; the keyboard back as the session left it,
                                    ; before any line is open
    call mega_dkbup
    not word [mega_ivtsh]           ; ...and the gates, whatever the shadow says
    call KERNEL_SEG:mega_ivt_syncf
    call mega_dprime                ; W6: the option ROMs' lines, primed
%ifdef MEGA_DIAG
    or byte [mega_tellq], 0x20      ; the trip, on the menu bar
%endif
    mov si, mega_m_unpark
    call mm_puts
    mov byte [mega_dwoke], 1
    jmp mega_dosparkf.ret

; mega_dkbup - the 8042 drained, the banked masks on, NMI on. Clobbers AL, CX
;
; **THE 8042 IS DRAINED, OR THE KEYBOARD IS DEAD** (found in Bochs, W4):
; ICW1 resets the 8259's edge latch, and a byte the controller is still
; holding - the release of the key that ended the program, typically - keeps
; IRQ1 high with no new edge, so the controller never sends another and no
; key reaches the desktop again. KERN-MEGA-DOS-PLAN 9.3's stranded request,
; on the one device every machine has. A stale byte is nobody's: the aux
; port's too, since hbm_wake re-arms the mouse anyway
mega_dkbup:
    mov cx, 16
.kdrain:
    in al, 0x64
    test al, 1
    jz .kempty
    in al, 0x60
    loop .kdrain
.kempty:
    mov al, [mega_dimr1]
    out 0x21, al
    mov al, [mega_dimr2]
    out 0xA1, al
    mov al, 0x0D
    out 0x70, al
    ret

; -----------------------------------------------------------------------------
; mega_hbgof - A HIBERNATE'S RESUME, the way out of the fresh kernel (W5; far,
;              from HIBER.DRV's hbm_xres, PM, DS = KDATA). Does not return.
; in:  ES = the text page (hbm_wake's staging cells are already in it);
;      [mega_dpbase] the staging block, holding the image at MPK_HDR;
;      [mega_dpkb] KB of conventional memory to put back; [mega_dncpy] and
;      MPS_REG the copies the stub makes first (the tables, every pool
;      block); [mega_dnreg] = 0
;
; THE PARK STUB DOES THE WORK. A hibernate's resume and a park's return are
; the same act - conventional memory and the kernel's state above 1MB put
; back from a block above 1MB, and into the kernel's own tables - so the
; fresh kernel reads the image into such a block with its own file layer and
; then enters the stub in place, as kd_unpark does, on the stub's own GDT.
; NEITHER CPU LEAVES PROTECTED MODE (KERN-MEGA-DOS-PLAN 8.3 planned a drop
; into the real-mode resume stub; Bochs will not fetch from the framebuffer
; that stub runs in, and a 286 would spend a reset on it).
; -----------------------------------------------------------------------------
mega_hbgof:
    ; STKBALANCE-OK: never returns - the far jump below is into the park stub, above 1MB
    cli
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    mov al, 0x8D                    ; NMI off: the stub writes the IDT
    out 0x70, al
    mov bx, sp
    mov ax, [ss:bx+2]               ; the caller's CS: the stub is in its image
    call mega_dstubup
    xor ax, ax
    mov [mega_dsum], ax             ; no regions, so the sum is 0:0
    mov [mega_dsum+2], ax
    mov [mega_dout], ax
    mov ax, mega_hbwake
    call mega_dhcfill
    mov ax, [mega_dpbase]
    mov dx, [mega_dpbase+2]
    add ax, MPK_GDT
    adc dx, 0
    mov [mega_hbgdtr+2], ax
    mov [mega_hbgdtr+4], dx
    lgdt [mega_hbgdtr]
    xor bx, bx                      ; the stub's check, from this kernel's own
    xor dx, dx                      ; cells: no sum, the KB and the block
    mov cx, [mega_dpkb]
    mov di, [mega_dpbase]
    mov si, [mega_dpbase+2]
    db 0xEA                         ; jmp far 08h:0 - the stub, above 1MB
    dw 0, 0x08

; -----------------------------------------------------------------------------
; mega_hbwake - THE HIBERNATED SESSION IS BACK (PM, the stub's far jump with
;               the image's tables loaded, IF = 0, NMI off, every line masked)
; The image's LDT and data, hbm_wake's stack, the masks banked at the write,
; and on to the wake address the write stamped (hbm_wake, which takes it from
; there exactly as on kern_big: SPEC.md 87.6). The 8259 is at 20h already -
; this kernel never left protected mode - and the IDT is the image's own.
; -----------------------------------------------------------------------------
section .mega                   ; protected mode only (KERN-MEGA-SEGMENTS-PLAN)
mega_hbwake:
    ; STKBALANCE-OK: entered on the stub's stack and left by a far jump into hbm_wake, which builds its own
    mov ax, MEGA_SEL_KDATA
    mov ds, ax
    mov ax, MEGA_SEL_LDT
    lldt ax
    mov ax, LOW_SEG
    mov ss, ax
    mov sp, [hb_sp]
    ; THE PICTURE WAS TAKEN MID-WRITE, so three cells hold what was in
    ; flight then (SPEC.md 87.6's class: true of the machine that wrote the
    ; image, false of this one): a null-segment instruction being stepped
    ; (found in Bochs: a native-IDE write reads segment 0 one word at a time
    ; under mega_null0, so its flag is in every such image, and every later
    ; null fixup refused - "never nests" - and faulted), dsk_xfer's
    ; transfer, and the IRQs a reflection had caught
    xor ax, ax
    mov [mega_n0], al
    mov [mega_fdx], al
    mov [mega_irqpend], al
    call mega_dkbup
    mov si, mega_m_hbwake
    call mm_puts
    jmp far [mega_hbwk]

; -----------------------------------------------------------------------------
; mega_drom_scan - the IRQ lines an OPTION ROM owns (mega_irqrom's own test,
;                  over IVT 0Ah-0Fh and 70h-77h). out: AX, bit n = IRQ n.
;                  Every other register kept
; -----------------------------------------------------------------------------
mega_drom_scan:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push word MEGA_SEL_LOW0
    pop es
    xor bx, bx
    mov si, 2                       ; IRQ 2 to 15
.l:
    mov ax, si                      ; IRQ 2-7: IDT 22h-27h over IVT 0Ah-0Fh;
    add al, 0x20                    ; 8-15: IDT and IVT both 70h-77h
    mov di, si
    add di, 8
    cmp si, 8
    jb .m
    mov ax, si
    add al, 0x68
    mov di, ax
.m:
    shl di, 2
    mov cx, [es:di+2]
    call KERNEL_SEG:mega_irqromf
    jnc .n
    mov cx, si
    mov ax, 1
    shl ax, cl
    or bx, ax
.n:
    inc si
    cmp si, 16
    jb .l
    mov ax, bx
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mega_dprime - W6's other edge (KERN-MEGA-DOS-PLAN 9.3): ICW1 resets the
; 8259's edge latches, twice on the trip, so a device that raised its line
; while it was masked and is still holding it high never makes another edge.
; The PicoMEM's request list is one such device. Each line [mega_drom] named
; at the drop gets ONE pass of its ROM's handler, by the reflection an IRQ
; there would have taken (int 22h+n / 70h+n reaches mega_irv and mega_svc);
; the handler finds its list empty or not and acknowledges either way.
; UNVERIFIED that a pass with nothing pending is harmless - the plan's 9.3 -
; which is the Victory's question. In: PM, IF = 0, DS = KDATA
; -----------------------------------------------------------------------------
mega_dprime:
    pusha
    mov dx, [mega_drom]
    mov bx, mega_dpt
.l:
    shr dx, 1
    jnc .n
    push bx
    push dx
    call bx
    pop dx
    pop bx
.n:
    add bx, 3
    cmp bx, mega_dpt_end
    jb .l
    popa
    ret
mega_dpt:                           ; one `int n / ret` a line, IRQ 0 to 15:
%assign mega_q 0                    ; 0, 1 never set (the tick, the keyboard)
%rep 16
 %if mega_q < 8
    int 0x20 + mega_q
 %else
    int 0x68 + mega_q
 %endif
    ret
 %assign mega_q mega_q + 1
%endrep
mega_dpt_end:

; -----------------------------------------------------------------------------
; THE KEYBOARD, ACROSS THE TRIP (KERN-MEGA-PLAN 10.3 item 54.3). A DOS program
; owns the 8042 while it runs, and the Victory 286 came back from Test Drive
; III with the PS/2 mouse working and the keyboard dead: the controller alive
; (IRQ12 delivered) and the KEYBOARD's half of it switched off - its interrupt
; or its clock disabled in the command byte, or the keyboard itself told to
; stop scanning. The ledger of 3.3 named the command byte and nothing built it.
; mega_d8042get banks it at the snapshot (IF = 0); mega_d8042put, in
; mega_dwake with every line still masked, puts it back, re-enables the
; interface (ADh's undo) and sends the KEYBOARD F4 (enable scanning, F5's
; undo), reading its FAh here so the keyboard ISR never sees it. Both drain
; the output buffer first, and a byte the AUX device sent (status bit 5) is
; never taken for the command byte.
; -----------------------------------------------------------------------------
mega_d8042get:
    push cx
    call mega_dkflush
    call mm_kbw
    mov al, 0x20                    ; read the command byte
    out 0x64, al
    call mega_dkobf
    mov al, 0xFF
    jc .n                           ; no answer: put nothing back
    in al, 0x60
    test al, 0x04                   ; the SYSTEM flag POST sets: a byte without
    jnz .n                          ; it is not a command byte, so do not
    mov al, 0xFF                    ; write it back over the real one
.n:
    mov [mega_d8042], al
    pop cx
    ret

mega_d8042put:
    push cx
    call mega_dkflush
    mov al, [mega_d8042]
    cmp al, 0xFF
    je .en
    call mm_kbw
    mov al, 0x60                    ; write the command byte
    out 0x64, al
    call mm_kbw
    mov al, [mega_d8042]
    out 0x60, al
.en:
    call mm_kbw
    mov al, 0xAE                    ; the keyboard interface on
    out 0x64, al
    call mm_kbw
    mov al, 0xF4                    ; ...and the keyboard scanning
    out 0x60, al
    call mega_dkobf                 ; its FAh
    jc .x
    in al, 0x60
.x:
    call mega_dkflush
    pop cx
    ret

mega_dkflush:                       ; whatever sits in the output buffer
    push cx
    mov cx, 16
.f:
    in al, 0x64
    test al, 1
    jz .d
    in al, 0x60
    loop .f
.d:
    pop cx
    ret

mega_dkobf:                         ; a byte from the CONTROLLER or the
    push cx                         ; keyboard: CF = 1 if none comes. An AUX
    xor cx, cx                      ; byte (status bit 5) is read and dropped
.w:
    in al, 0x64
    test al, 1
    jz .nx
    test al, 0x20
    jz .ok
    in al, 0x60
.nx:
    loop .w
    stc
    pop cx
    ret
.ok:
    clc
    pop cx
    ret

%ifdef MEGA_DIAG
; mega_dtell - mega_tell's line for the trip: "DOSP kkkkK o tttt b tttt"
mega_dtell:
    pusha
    push es
    push ds
    pop es
    mov di, mega_dstr + 5
    mov ax, [mega_dikb]
    call mega_dec4
    mov di, mega_dstr + 13
    mov ax, [mega_dout]
    call mega_dec4
    mov di, mega_dstr + 20
    mov ax, [mega_dback]
    call mega_dec4
    mov si, mega_dstr
    jmp mega_nmitell.say

mega_dec4:                          ; AX -> four decimal digits at ES:DI
    mov cx, 4
    add di, 3
    mov bx, 10
.d:
    xor dx, dx
    div bx
    add dl, '0'
    mov [es:di], dl
    dec di
    loop .d
    ret
%endif

; =============================================================================
; .cold: the loader's check. THE PARK STUB IS kernel/megapark.inc, in
; HIBER.DRV's image: it is data as well as code, which .cold may not carry
; =============================================================================
mega_m_park db "kern_mega: parked ", 0     ; the debug port's lines, read through
mega_m_at   db "K at ", 0                 ; `.mega`'s CS by mm_puts
mega_m_sum  db " sum ", 0
mega_m_unpark db "kern_mega: unparked", 10, 0
mega_m_hbwake db "kern_mega: resumed", 10, 0
section .cold

; =============================================================================
; mega_wrprobe - W7: A PARK THE MACHINE WAS RESET OUT OF (KERN-MEGA-DOS-PLAN
; 10, wave W7; BLOBCALL'd from kmain_o, real mode, before drv_boot)
;
; Ctrl-Alt-Del inside a DOS program, or a program that resets the machine:
; the ROM's warm boot keeps RAM, and a kern_mega boot does nothing above 1MB
; until mega_enter - the last thing kmain does - so the park, the session's
; GDT, LDT, IDT, `.cold` and pool blocks are all still where it left them.
; The anchor says there is one (mega_dosgo wrote it, mega_dwake clears it):
; it is CLEARED HERE FIRST, so a recovery that fails cannot be tried again,
; and then the park is entered exactly as kd_unpark enters it - text mode,
; hbm_wake's cells in the page (this boot's clock, as a resume stages it),
; A20 open, the stub's own GDT - and the stub's sum is the judge: a session
; the reset or the BIOS damaged is refused in a sentence and restarts.
; Returns (RETF, every register kept) only when there is nothing to take.
; =============================================================================
section .boot2
mega_wrprobe:
    ; STKBALANCE-OK: a BP frame (`sub sp, 64` / `mov sp, bp`) the walker does not model, and the park's way leaves by a far jump into the stub
    cmp byte [cpu_tier], CPU_286
    jae .try
    retf
.try:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push bp
    mov bp, sp
    sub sp, 64                      ; [bp-48] int 15h AH=87h's descriptors,
                                    ; [bp-64] 16 bytes of buffer
    mov ax, MEGA_GDT_LIN & 0xFFFF   ; the anchor
    mov dx, MEGA_GDT_LIN >> 16
    mov cx, 4
    xor bx, bx                      ; BL = 0: read
    call .mv
    jc .out
    cmp word [ss:bp-64], MPK_ANCHOR1
    jne .out
    cmp word [ss:bp-62], MPK_ANCHOR2
    jne .out
    mov si, [ss:bp-60]              ; SI:DI = the park
    mov di, [ss:bp-58]
    push di
    lea di, [bp-64]                 ; ...and the anchor CLEARED, before
    push ss                         ; anything is decided
    pop es
    xor ax, ax
    mov cx, 4
    cld
    rep stosw
    pop di
    mov ax, MEGA_GDT_LIN & 0xFFFF
    mov dx, MEGA_GDT_LIN >> 16
    mov cx, 4
    mov bl, 1                       ; BL = 1: write
    call .mv
    mov ax, si                      ; the header's magic
    mov dx, di
    add ax, MPK_MAGIC
    adc dx, 0
    mov cx, 2
    xor bx, bx
    call .mv
    jc .out
    cmp word [ss:bp-64], 'PA'
    jne .out
    cmp word [ss:bp-62], 'RK'
    jne .out
    mov ax, si                      ; ...and the sum and the KB for the stub's
    mov dx, di                      ; check, which after a reset nothing but
    add ax, MPK_C_PSUM              ; the header itself still holds
    adc dx, 0
    mov cx, 3
    xor bx, bx
    call .mv
    jc .out
    push si                         ; on port E9, as the kernel's other lines
    mov si, .said
.say:
    cs lodsb
    or al, al
    jz .said0
    out 0xE9, al
    jmp short .say
.said:
    db "kern_mega: a park survived the reset", 10, 0
.said0:
    pop si
    ; --- A20, which the warm boot may have shut --------------------------------
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    call .kbw
    mov al, 0xD1
    out 0x64, al
    call .kbw
    mov al, 0xDF
    out 0x60, al
    call .kbw
    push ds
    xor ax, ax
    mov ds, ax
    dec ax
    mov es, ax
    mov bx, [0xFFE0]
    not word [es:0xFFF0]
    mov ax, [0xFFE0]
    not word [es:0xFFF0]
    pop ds
    cmp ax, bx
    jne .out                        ; still shut: an ordinary boot
    ; --- text mode and hbm_wake's cells ----------------------------------------
    mov ax, 0x0003
    mov bx, 0xB800
    cmp byte [vid_kind], VID_HERC
    jne .mode
    mov al, 0x07
    mov bh, 0xB0
.mode:
    push bx
    int 0x10
    pop es
    push si
    push di
    mov si, clk_sec                 ; this boot's clock, current, and the
    mov di, HS_CLK                  ; tick -1: hbm_res's staging
    mov cx, 5
    rep movsw
    mov word [es:HS_DOSCODE], MPK_RESETCODE
    mov di, HS_KSEG
    mov ax, KERNEL_SEG
    stosw
    xor ax, ax
    stosw
    dec ax
    stosw
    stosw
    pop di
    pop si
    ; --- into the park stub, on its own GDT ------------------------------------
    cli
    mov al, 0xFF
    out 0x21, al
    out 0xA1, al
    mov al, 0x8D                    ; NMI off
    out 0x70, al
    add si, MPK_GDT
    adc di, 0
    mov word [ss:bp-8], 47
    mov [ss:bp-6], si
    mov [ss:bp-4], di
cpu 286
    lgdt [ss:bp-8]
    smsw ax
    or al, 1
    lmsw ax
cpu 8086
    sub si, MPK_GDT                 ; SI:DI = the park, as the stub takes it
    sbb di, 0
    xchg si, di
    mov bx, [ss:bp-64]              ; DX:BX the sum, CX the KB
    mov dx, [ss:bp-62]
    mov cx, [ss:bp-60]
    db 0xEA                         ; jmp far 08h:0 - the stub, above 1MB
    dw 0, 0x08
.out:
    mov sp, bp
    pop bp
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf
.kbw:                               ; the 8042's input buffer empty, or give up
    push cx
    xor cx, cx
.kbl:
    in al, 0x64
    test al, 2
    loopnz .kbl
    pop cx
    ret
.mv:                                ; int 15h AH=87h: CX words between linear
    push si                         ; DX:AX and the buffer at [bp-64], BL = 0
    push di                         ; there to here, 1 here to there. CF
    push es
    push cx
    push ss
    pop es
    lea di, [bp-48]
    push ax
    xor ax, ax
    mov cx, 24
    cld
    rep stosw
    pop ax
    lea di, [bp-48+0x10]            ; descriptor 2, the source, unless BL
    or bl, bl
    jz .src
    lea di, [bp-48+0x18]
.src:
    mov word [ss:di], 0xFFFF
    mov [ss:di+2], ax
    mov [ss:di+4], dl
    mov byte [ss:di+5], 0x93
    xor bl, 1                       ; ...and the other end, the buffer
    lea di, [bp-48+0x10]
    or bl, bl
    jz .dst
    lea di, [bp-48+0x18]
.dst:
    mov ax, ss
    mov dx, ax
    mov cl, 4
    shl ax, cl
    mov cl, 12
    shr dx, cl
    lea cx, [bp-64]
    add ax, cx
    adc dx, 0
    mov word [ss:di], 0xFFFF
    mov [ss:di+2], ax
    mov [ss:di+4], dl
    mov byte [ss:di+5], 0x93
    pop cx
    lea si, [bp-48]
    mov ah, 0x87
    int 0x15
    pop es
    pop di
    pop si
    ret
section .cold                   ; ...and back to the loader's check

; -----------------------------------------------------------------------------
; mega_pkgok - may this package run on kern_mega? (the loader, before its
;              entry proc; .cold, PM, DS = KDATA, [ld_base] the image)
; out: CF = 0 yes; CF = 1 no, and the toast says why
;
; ONE PACKAGE IS REFUSED: kern_big's DOS.O88 (12.1 item 4). Its box runs DOS
; programs inside the kernel's session, which a protected-mode kernel cannot
; host, and it must stay byte-identical, so it cannot say so itself. A
; kern_mega DOS.O88 marks both its entry procs with 'KMEG' in the four bytes
; before them; a 'DOS' package without the mark is the other one.
; -----------------------------------------------------------------------------
mega_pkgok:
    push ax
    push bx
    push es
    mov es, [ld_base]
    cmp word [es:16], 'DO'
    jne .yes
    cmp word [es:18], 'S'           ; 'S', 0: exactly "DOS"
    jne .yes
    mov bx, [es:6]                  ; the entry offset
    cmp bx, 4
    jb .no
    cmp word [es:bx-4], 'KM'
    jne .no
    cmp word [es:bx-2], 'EG'
    je .yes
.no:
    push cx
    push si
    push ds
    pop es
    mov si, mega_s_kbdos
    xor cx, cx
    call KERNEL_SEG:cw_toast_show
    pop si
    pop cx
    pop es
    pop bx
    pop ax
    stc
    ret
.yes:
    pop es
    pop bx
    pop ax
    clc
    ret

section .text
cpu 286
