; =============================================================================
; os8088 - apps/dos/doswr.inc
;
; **THE WINDOWED LAYER'S RESIDENT HALF, IN A SPLIT HOST** (SPEC.md 96.54.9).
; The layer is PART DOS_PART_DVW of DOS.O88 (apps/dos/doswp.asm); what stays
; in the instance's region is what the host and the kernel ENTER it by, what
; it calls the host by, and the fetch, pin and move of the claim it lives in.
; dosw.inc's own resident pieces - the worker's loop and the door table - are
; assembled beside this, under the same DVW_SPLIT.
; =============================================================================

%ifdef DVW_SPLIT

; --- INTO the part: six-byte stubs, their segment word PATCHED --------------
; `call far PART:entry / ret` for a CALL entry, `jmp far PART:entry` (and a
; byte nobody reaches) for a JUMP one. The segment is written by dvw_patch at
; the fetch and by dvw_preloc at every move - never while a run is in, which
; is the only time a stub can be executing, so none is ever mid-flight
; (apps/tracker/trkplay.inc patches its immediates on the same argument).
%macro DVWT 2
hent_%1:
    db 0x9A                         ; call far
    dw %2 * 4, 0
    ret
%endmacro
%macro DVWJ 2
hent_%1:
    db 0xEA                         ; jmp far
    dw %2 * 4, 0
    db 0xCC                         ; (unreached: the stride is six)
%endmacro
dvw_stubs:
    DVWT dvw_start,   DVWE_START
    DVWT dvw_wakef,   DVWE_WAKE     ; (hent_dvw_wake is the wrapper below)
    DVWT dvw_key,     DVWE_KEY
    DVWT dvw_onclosef, DVWE_ONCLOSE ; (...and hent_dvw_onclose)
    DVWT dwm_mouse,   DVWE_MOUSE
    DVWT dwm_txt,     DVWE_TXT
    DVWT dvw_fsmain,  DVWE_FSMAIN
    DVWT dvw_runprog, DVWE_RUNPROG
    DVWT dvw_regrow,  DVWE_REGROW
    DVWJ dvw_post,    DVWE_POST
hent_dvw_fin:                       ; DVWJ's shape, less the pad: nothing
    db 0xEA                         ; follows it in the stride
    dw DVWE_FIN * 4, 0
dvw_stubs_end:
DVWT_N equ (dvw_stubs_end - dvw_stubs + 1) / 6
%if DVWT_N != DVWE_N
 %error "one host stub per part entry (doswabi.inc's DVWE_*)"
%endif

WIN_CODE                            ; everything below is the window's
                                    ; (DOS-HEADLESS-PLAN 4.4): the stubs above
                                    ; stay resident, being what a FENCED slot
                                    ; names - hent_dvw_fsmain is an fsx proc -
                                    ; and what the resident worker far-calls

; --- OUT of the part: a thunk per host routine (DVW_HOSTCALLS) --------------
; NUMBERED, not named: a core routine's name is an %xdefine of its slot in
; this host (apps/dos/doscall.inc), so `dvwh_%1` would paste the slot's
; ARITHMETIC into a label. The part numbers its far pointers in the same
; order off the same list, which is what pairs them.
%assign DVWH_N 0
%macro DVW_HX 1
dvwh_ %+ DVWH_N:
    call %1
    retf
%assign DVWH_N DVWH_N + 1
%endmacro
    DVW_HOSTCALLS
dvwh_ret:                           ; DVW_POSTRET: dvw_post back to the core,
    ret                             ; which near-called a stub of OURS
    db 0xCC, 0xCC, 0xCC             ; (the stride: dvw_hbind COMPUTES every
dvwh_icall:                         ; offset, dvwh_0 + 4n, so these two are
    call word [cs:dvw_rtgt]         ; slots too.) HICALL: dvw_serve's posted
    retf                            ; door, its registers all the program's
%if dvwh_ret != dvwh_0 + 4 * DVWH_N || dvwh_icall != dvwh_ret + 4
 %error "the host's thunks are a 4-byte stride from dvwh_0, ret and icall last: doswp.asm's dvw_hbind computes them"
%endif

; -----------------------------------------------------------------------------
; dvw_pfetch - dos_run's: the part for a run in the window, or none for one
; on the full screen (SPEC.md 96.54.9)
; in:  [dvw_want]; UI task, lock FREE
; out: CF=1 a run in the window whose part could not be had; every register
;      preserved
;
; Idempotent - dos_run comes back here on a compaction wake, and a window
; that ran in the window before keeps its part between runs. A run on the
; FULL SCREEN drops it: that arena is OSAPI_MEM_AVAIL, everything, and a part
; left standing would take its KB from the program.
; -----------------------------------------------------------------------------
dvw_pfetch:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [dvw_want], 0
    jne .want
    xor dx, dx                      ; --- the full screen: drop it ---------
    xchg dx, [dvw_pseg]
    or dx, dx
    jz .out                         ; (CF = 0)
    call OSAPI_MEM_FREE
    clc
    jmp short .out
.want:
    cmp word [dvw_pseg], 0
    jne .out                        ; (CF = 0: had already)
    mov si, dos_dvwrow
    xor ah, ah                      ; no floor: the image is the claim
    call dos_pself                  ; DX = the claim, CX = the part's length
    jc .out
    mov ax, cx                      ; ...and only the image's own KB of it:
    add ax, 1023                    ; op_self claimed for its read's slack
    mov cl, 10                      ; too, and a shrink is always in place
    shr ax, cl                      ; (OSAPI_MEM_REGROW), DX unchanged
    call OSAPI_MEM_REGROW
    call dvw_preloc                 ; [dvw_pseg] = DX, and the stubs
    call dvw_relax                  ; movable until a program runs in it
    clc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dvw_preloc - the part's claim has MOVED (SPEC.md 66.2): DX = where it is now,
; DS = CS = ours. Nothing names it between runs but [dvw_pseg] and the stubs:
; the world is out, the part's own copy of the host's segment is rewritten at
; the next start. dvw_pfetch's store too. Every register preserved
dvw_preloc:
    mov [dvw_pseg], dx              ; ...and on into dvw_patch
; dvw_patch - the part's segment into every stub; every register preserved
dvw_patch:
    push ax
    push cx
    push di
    mov ax, [dvw_pseg]
    mov di, dvw_stubs + 3
    mov cx, DVWT_N
.p:
    mov [di], ax
    add di, 6
    loop .p
    pop di
    pop cx
    pop ax
    ret

; dvw_pin / dvw_relax - the claim pinned for a run (the IVT, the worker's
; frames and the program's interrupted CS:IP name it), or movable again once
; no run is in; both preserve every register and the flags
dvw_pin:
    pushf
    push ax
    xor ax, ax
    jmp short dvw_rloc
; hent_dvw_onclose - OSAPI_WM_ONCLOSE's callback. With no program running the
; part may not even be fetched, and the close is simply allowed (CF = 0 off
; the compare)
hent_dvw_onclose:
    cmp byte [dvw_run], 0
    jne hent_dvw_onclosef
    ret

; hent_dvw_wake - dos_wake's tail, while a program runs: the part's wake,
; and THEN the claim made movable if that wake ended the run. Never inside the
; part's wake: it calls host routines that claim (dos_drv_back remounts the
; drivers), and a compaction there would move code that is on the stack
hent_dvw_wake:
    call hent_dvw_wakef             ; ...and on into dvw_relax
dvw_relax:
    pushf
    push ax
    cmp byte [dvw_run], 0
    jne dvw_rloc.out
    mov ax, dvw_preloc
dvw_rloc:
    push dx
    mov dx, [dvw_pseg]
    or dx, dx
    jz .nopart
    call OSAPI_MEM_MOVABLE
.nopart:
    pop dx
.out:
    pop ax
    popf
    ret

WIN_CODE_END
%endif                              ; DVW_SPLIT
