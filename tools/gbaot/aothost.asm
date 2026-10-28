; =============================================================================
; os8088 - tools/gbaot/aothost.asm
;
; RealDMG's own core code, hosted for tools/gbaot.py on the HOST, under an
; 8086 emulator (Unicorn): the full static recompiler's translator and its
; equivalence instrument (docs/plans/REALDMG-AOT-PLAN.md, REALDMG-SPEC 20).
;
; It is rdgsys.inc and rdgjit.inc as the package and the bare harness include
; them, so a block translated here is byte for byte what the machine's own
; translator would make, and the run arms below are the machine's own code
; paths. Python drives it: it fills the parameter block, enters at one of the
; entries and is handed the machine back at an INT 3 - the end of an entry,
; or a service the host gives (a bank, below).
;
; The run modes, h_mode:
;   0  interpret          the interpreter alone (rt_dispatch 0)
;   1  recompile          the dynamic recompiler (rt_init)
;   2  translate          rt_init over the table and two cache segments, the
;                         profile at p_prf, rt_preload in rounds (h_round)
;   3  precompiled        no translator at all: the table and the code made
;                         by mode 2, FIXED (S_JITFIX), the HRAM's code
;                         interpreted (h_jitstub), the resume through
;                         tr_enter
;
; Assembled at org 0 by tools/gbaot.py, which finds the entries at the top.
; =============================================================================

cpu 8086
bits 16
org 0

%include "rdgdefs.inc"

; --- the entries: Python enters at these offsets ----------------------------
    jmp near h_init                     ; 0
    jmp near h_preload                  ; 3
    jmp near h_round                    ; 6
    jmp near h_frame                    ; 9
    jmp near h_cont                     ; 12
    times 16-($-$$) nop

; --- the parameter block, at 10h (tools/gbaot.py's P_*) ---------------------
p_view      dw 0                        ; 10h  the view's segment
p_core      dw 0                        ; 12h  the core's
p_rom       dw 0                        ; 14h  an image holding bank 0 at +0
p_nbanks    dw 0                        ; 16h  the ROM's 16KB banks
p_hash      dw 0                        ; 18h  the block table's
p_prf       dw 0                        ; 1Ah  the profile's (mode 2)
p_prfmask   dw 0                        ; 1Ch  its entries - 1
p_sega      dw 0                        ; 1Eh  the cache's first segment
p_segb      dw 0                        ; 20h  ...and its second
p_mode      db 0                        ; 22h  h_mode, above
p_hmask     db 0                        ; 23h  S_HMASK: 31 (64KB) or 7
p_noc       db 0                        ; 24h  S_JITNOC for the first round
p_reason    db 0                        ; 25h  why the INT 3: 0 done, 1 a bank
p_out       dw 0                        ; 26h  an entry's answer
p_budget    dw 0                        ; 28h  rdg_run's budget, M-cycles
p_bank      dw 0                        ; 2Ah  the bank asked for, x 2 (reason 1)
p_bmp       dw 0                        ; 2Ch  the block starts' bitmap (RT_AOTBMP)
p_sern      dw 0                        ; 2Eh  bytes sent on the link port
p_serbuf    dw ser_buf                  ; 30h  ...and where they are
p_list      dw 0                        ; 32h  the table's overflow list (mode 3)
p_listn     dw 0                        ; 34h  ...its entries
p_aotint    dw rdg_aotmiss.no           ; 36h  where a miss goes to the
                                        ;      interpreter (the heat's misses)
p_stload    dw h_stload                 ; 38h  entered here: a save state
p_filt      dw 0                        ; 3Ah  the list's filter (mode 3)
    times 40h-($-$$) db 0

; the stops: the host takes the machine at the INT 3 and puts it back at the
; instruction after it
%macro STOP 1
    mov byte [cs:p_reason],%1
    int 3
%endmacro

; h_init - rdg_init over the parameter block, then the mode's recompiler
h_init:
    mov word [cs:rdg_nres],1            ; the bank table is the host's: it maps
    mov word [cs:rdg_bfill],h_bfill     ;   what fits and fetches the rest
    mov word [cs:rdg_bmiss],h_bmiss     ;   (rdgsys.inc's rdg_nres)
    mov ax,[cs:p_view]
    mov bx,[cs:p_core]
    mov cx,[cs:p_rom]
    mov dx,[cs:p_nbanks]
    call rdg_init
    mov ds,[cs:p_view]
    mov al,[cs:p_mode]
    cmp al,1
    je .dyn
    cmp al,2
    je .tr
    cmp al,3
    je .aot
    xor al,al                           ; 0: the interpreter as there was one
    call rt_dispatch                    ;   before the recompiler
    jmp .done
.tr:
    mov ax,[cs:p_prf]
    mov [S_PRFSEG],ax
    mov ax,[cs:p_prfmask]
    mov [S_PRFMASK],ax
.dyn:
    mov al,[cs:p_noc]
    mov [S_JITNOC],al
    mov ax,[cs:p_sega]
    mov bx,[cs:p_hash]
    mov cx,[cs:p_segb]
    mov dl,[cs:p_hmask]
    call rt_init
    jmp .done
.aot:
    ; PRECOMPILED: the table and the code are the host's, already in place;
    ; what rt_init would have said of them, and nothing translated - not
    ; even HRAM, whose blocks the translator would make on demand
    mov ax,[cs:p_sega]
    mov [S_JITSEGA],ax
    mov [S_JITSEG],ax
    mov [S_JITOUT+2],ax
    mov ax,[cs:p_segb]
    mov [S_JITSEGB],ax
    mov ax,[cs:p_hash]
    mov [S_JITHSEG],ax
    mov al,[cs:p_hmask]
    mov [S_HMASK],al
    mov byte [S_JITNOC],1
    mov word [S_JITVEC],h_jitstub
    mov [S_JITVEC+2],cs
    mov word [S_RESVEC],RDGC_TR_ENTER
    mov byte [S_JITON],1
    mov byte [S_JITFIX],1
    mov byte [S_JITPRE],2
    mov word [S_JITEND],0               ; no room: rt_room is never reached,
    mov al,1                            ;   but say so
    call rt_dispatch
    mov ax,[cs:p_list]                  ; the table's misses: the host's list
    mov cx,[cs:p_listn]
    mov bx,[cs:p_filt]                  ;   and its filter
    call rdg_aotattach
.done:
    STOP 0
    jmp short $

; h_preload - one round of rt_preload: p_out = the blocks translated
h_preload:
    mov ds,[cs:p_view]
    call rt_preload
    mov [cs:p_out],ax
    STOP 0
    jmp short $

; h_round - the next round's two cache segments, p_sega and p_segb, the
; tails copied into each, the first current; the core's tier spent (it is
; the first round's) and the table KEPT - rt_flush would forget it. The host
; has already read the last round's two out and taken their blocks off the
; profile
h_round:
    mov ds,[cs:p_view]
    mov ax,[cs:p_sega]
    mov [S_JITSEGA],ax
    call rt_init.tails
    mov ax,[cs:p_segb]
    mov [S_JITSEGB],ax
    call rt_init.tails
    mov byte [S_JITNOC],1
    mov byte [S_JITFIX],0
    mov byte [S_JITPRE],0
    mov byte [S_JITTIER],0
    mov byte [S_JITUSED],1
    mov ax,[cs:p_sega]
    call rt_tier
    STOP 0
    jmp short $

; h_cont - the next round in the segments the last one was writing: it
; ended with room left (its entries all went in), and only the next group of
; banks' entries are new
h_cont:
    mov ds,[cs:p_view]
    mov byte [S_JITFIX],0
    mov byte [S_JITPRE],0
    STOP 0
    jmp short $

; h_frame - rdg_run until a frame (or the core stopped): p_out = RDGX_*
; h_stload - a save state: the host has put the view's 8000-FFFF and the
; record at RDG_STREC (rwstate.inc's file); p_out = 1 if it is not this
; game's, else the machine is the state's (rdg_stapply)
h_stload:
    mov ds,[cs:p_view]
    mov word [cs:p_out],1
    call rdg_stcheck
    jc .x
    call rdg_stapply
    mov word [cs:p_out],0
.x:
    STOP 0
    jmp short $

h_frame:
    mov ds,[cs:p_view]
    mov ax,[S_NOW]                      ; (when it began: with the LCD off no
    mov [cs:h_fnow],ax                  ;   VBlank ends one, so a frame's time
    mov ax,[S_NOW+2]                    ;   does - Prehistorik Man keeps it off
    mov [cs:h_fnow+2],ax                ;   for seconds as a level loads)
.r:
    mov ax,[cs:p_budget]
    call rdg_run
    cmp ax,RDGX_BUDGET
    jne .out
    cmp byte [S_LCDON],0
    jne .r
    mov ax,[S_NOW]
    mov dx,[S_NOW+2]
    sub ax,[cs:h_fnow]
    sbb dx,[cs:h_fnow+2]
    jnz .lcdoff
    cmp ax,RDG_FRAME
    jb .r
.lcdoff:
    mov ax,RDGX_BUDGET
.out:
    mov [cs:p_out],ax
    STOP 0
    jmp short $
h_fnow      dd 0

; rdg_init's call for the bank table (rdg_nres): the host fills RDG_BTAB
h_bfill:
    mov word [cs:p_bank],0FFFFh
    STOP 1
    ret

; rdg_setbank's, BX = the bank x 2, for one the table has no segment for
h_bmiss:
    mov [cs:p_bank],bx
    STOP 1
    ret

rdg_putc:                               ; the link port: kept for the host
    push bx                             ;   (Blargg's verdicts, gbflags' sum)
    mov bx,[cs:p_sern]
    and bx,SER_LEN - 1
    mov [cs:ser_buf+bx],al
    inc word [cs:p_sern]
    pop bx
rdg_trbyte:
    ret
SER_LEN     equ 1024
ser_buf     times SER_LEN db 0

%include "rdgsys.inc"
%include "rdgaot.inc"
RT_AOTBMP       equ 2Ch                 ; p_bmp: rdgjit.inc's rt_aotstop
%define RT_AOTSTOP
%include "rdgjit.inc"

; the translator's door in mode 3: nothing is translated, so the block is
; the interpreter's (rdg_jit's own answer for a ROM PC under a fixed table)
h_jitstub:
    RDG_TOHOST
    mov word [S_JITOUT],0
    jmp rdg_ret
