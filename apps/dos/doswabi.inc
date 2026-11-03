; =============================================================================
; os8088 - apps/dos/doswabi.inc
;
; **THE WINDOWED LAYER'S SEGMENT BOUNDARY, WRITTEN ONCE** (SPEC.md 96.54.9,
; docs/plans/DOS-PARTS-PLAN.md 2, option C'). `dosw.inc` and `doswm.inc` are
; assembled in one of THREE shapes, and every reference they make across the
; line between the HOST (the instance's region: the console, the core, the
; doors) and the LAYER is spelled with one of the macros below, chosen by WHO
; OWNS THE SYMBOL - never by what the line happens to do:
;
;   (neither)   INLINE - the layer inside the host, as it always was. Every
;               macro is its IDENTITY: `HCALL x` is `call x`, `HOSTDS` is
;               `push cs / pop ds`, `PS` is nothing. build/dos.bin, DOSTRACE
;               and kern_small's plain dos.o88 are this shape - and so was
;               every DOSKPART host until DVW_SPLIT, which is the IDENTITY
;               GATE: with these arms the host assembled BYTE-IDENTICAL to the
;               dosp.bin before the macros, so every site was found before one
;               of them was changed.
;   DVW_SPLIT   the HOST of a split build (dos.asm, -DDOSKPART on kern_big and
;               kern_emu): only the RESIDENT pieces of dosw.inc - the worker's
;               loop and the door table - plus doswr.inc's stubs and thunks.
;   DVW_PART    the PART (apps/dos/doswp.asm): everything else, at org 0 in a
;               claim of its own, CS = the claim, against the host's symbols
;               out of its map ($(BUILD)/doswexp.inc, tools/os88dvwexp.py).
;
; THE CONVENTION THE MACROS ENCODE: inside the part, DS IS THE HOST wherever
; part code touches host data or calls the kernel (an OSAPI cell names its
; caller by DS), and the part's own cells are ALWAYS addressed through CS.
; tools/os88dvwexp.py's checker refuses, at build time, every form these
; macros do not cover: a near call or jump to a host routine, a `cs:` operand
; naming a host cell, a part cell with no segment, a bare `push cs` or `mov
; x, cs`, a part routine handed out as a number, and a string instruction or
; register-indirect branch nobody has looked at (`; DVWCHK-OK:` marks one
; that somebody has, with the reason). What the checker cannot see is a
; computed address; those sites are marked and argued where they stand.
; =============================================================================

%ifndef DOSWABI_INC
%define DOSWABI_INC

; --- the part's far entries: entry n is at part:DVWE_n * 4 -------------------
; A CALL entry is `call body / retf` (the host stub far-calls it and `ret`s);
; a JUMP entry is `jmp body` + a pad (the host stub far-JUMPS to it, so the
; body sees exactly the stack its near caller built: dvw_post's stub return,
; DHK_SNAP's return into the core).
DVWE_START   equ 0              ; dos_run, after the fetch
DVWE_WAKE    equ 1              ; dos_wake's tail, while a program runs
DVWE_KEY     equ 2              ; dos_key's, AX = the key, BX = the window
DVWE_ONCLOSE equ 3              ; OSAPI_WM_ONCLOSE, while a program runs
DVWE_MOUSE   equ 4              ; DHK_MOUSE, bound for a windowed run
DVWE_TXT     equ 5              ; DHK_TXT, ditto
DVWE_FSMAIN  equ 6              ; OSAPI_FSX_RUN's proc (Alt+Enter, a shunt)
DVWE_RUNPROG equ 7              ; the worker's one call, dvw_runprog
DVWE_REGROW  equ 8              ; dos_run's DER_FIT, an Automatic limit
DVWE_POST    equ 9              ; JUMP: the 23 door stubs' dvw_post
DVWE_FIN     equ 10             ; JUMP: DHK_SNAP, dvw_fin
DVWE_N       equ 11
DVWE_NCALL   equ 9              ; ...the first nine are CALL entries

; --- the host routines the part calls, each through a 4-byte thunk ----------
; `call x / retf` in the host (doswr.inc) and a dword `dvwx_x` in the part,
; whose segment word dvw_hbind writes at every start. HCALL of a name that is
; not here fails to assemble (no dvwx_ of it), so the list cannot drift.
%macro DVW_HOSTCALLS 0
    DVW_HX con_clear
    DVW_HX con_mark
    DVW_HX con_markall
    DVW_HX con_markcells
    DVW_HX con_markcur
    DVW_HX con_marks
    DVW_HX con_tx_ice
    DVW_HX con_write
    DVW_HX dos_be_bind
    DVW_HX dos_build_psp
    DVW_HX dos_cache_free
    DVW_HX dos_con_draw
    DVW_HX dos_con_ended
    DVW_HX dos_date_init
    DVW_HX dos_drv_back
    DVW_HX dos_drv_bank
    DVW_HX dos_fsx_ae
    DVW_HX dos_fsx_owed
    DVW_HX dos_hk_mouse
    DVW_HX dos_hk_txt
    DVW_HX dos_hook_vectors
    DVW_HX dos_m33_evt
    DVW_HX dos_m33_wall
    DVW_HX dos_pkt_shut
    DVW_HX dos_prog_enter
    DVW_HX dos_repaint
    DVW_HX dos_snap
    DVW_HX dos_swap
%endmacro

; =============================================================================
; THE MACROS
; =============================================================================
; Owner-neutral annotations, the same bytes in every shape:
;   PARTDS / PARTES   `push cs / pop ds|es` that means THIS code's segment
;                     (a block of the layer's own, read by a string op)
;   PARTCS r          `mov r, cs` that means this code's segment (a vector
;                     or a return frame INTO the layer)
;   PADDR(x)          a layer label used as a NUMBER that is meant to be an
;                     offset in the layer's own segment
%macro PARTDS 0
    push cs
    pop ds
%endmacro
%macro PARTES 0
    push cs
    pop es
%endmacro
%macro PARTCS 1
    mov %1, cs
%endmacro
%define PADDR(x) x

%ifdef DVW_PART
; --- the part's arms ---------------------------------------------------------
%define PS cs:                  ; a part cell, reached while DS is the host's
%define PSRC cs                 ; a string op's SOURCE in the part: `PSRC lodsw`
%define HENT(x) hent_ %+ x      ; a part routine handed out as a NEAR entry:
                                ; the host's stub for it (doswr.inc)
%macro HCALL 1
    call far [cs:dvwx_%1]
%endmacro
%macro HJMP 1                   ; a tail jump into the host: its ret comes
    call far [cs:dvwx_%1]       ; back here, and this one goes on to ours
    ret
%endmacro
%macro HICALL 1                 ; `call word [cs:%1]`, %1 a HOST cell holding
    call far [cs:dvwx_icall]    ; a host routine: the one thunk that has it
%endmacro                       ; (only dvw_rtgt, dvw_serve's posted door)
%macro HOSTDS 0
    mov ds, [cs:dvw_hseg]
%endmacro
%macro HOSTES 0
    mov es, [cs:dvw_hseg]
%endmacro
; DVW_LOWSTK - dvw_tolow's SS:SP = LOW_SEG:[dos_dstk], a HOST cell read from
; code whose DS is anybody's (IF = 0). SS itself is the host for one load:
; SS is what is being replaced anyway, so nothing is banked to reach the
; cell. (This was HREF, which banked DS in a part cell for two sites; the
; other, dvw_cpace's, is reached with DS the host's now.)
%macro DVW_LOWSTK 0
    mov ss, [cs:dvw_hseg]
    mov sp, [ss:dos_dstk]   ; DVWCHK-OK: SS is the host for this one load
    mov ss, [cs:dvw_lss]
%endmacro
; DVW_HBIND - at the head of dvw_start, the one entry that runs first in a
; run: the host's segment into dvw_hseg and every HCALL dword (DS = the host)
%macro DVW_HBIND 0
    call dvw_hbind
%endmacro
; DVW_POSTRET - dvw_post's one way back to the core, which near-called a HOST
; stub: the ten popped, then a far jump to a host `ret`, which pops the core's
; own near return with CS the host's. Its one site, so the pops are here
%macro DVW_POSTRET 0
    pop ax
    pop bx
    pop cx
    pop dx
    pop si
    pop di
    pop bp
    pop es
    pop ds
    popf
    jmp far [cs:dvwx_ret]
%endmacro
; DVW_CLRDK - dvw_start's `done, kill` word: done is the host's (the
; resident worker sets it), kill the part's
%macro DVW_CLRDK 0
    mov [dvw_done], al
    mov [cs:dvw_kill], al
%endmacro
; DVW_PCOPY / DVW_PCEND - a block copy WITHIN the part (DS:SI and ES:DI both
; part offsets) where the inline layer had `push ds / pop es`, both segments
; then being the host's own
%macro DVW_PCOPY 0
    push ds
    push cs
    pop es
    push cs
    pop ds
%endmacro
%macro DVW_PCEND 0
    pop ds
    push ds
    pop es
%endmacro
; DVW_PESDS - ES = the part, for a copy INTO a part block from a DS that is
; neither (the BDA): the inline layer's `push ds / pop es`, DS being its own
%macro DVW_PESDS 0
    push cs
    pop es
%endmacro
%macro DVW_PESEND 0               ; ...and ES = DS = the host again after it,
    push ds                     ; as the inline layer's ES already was
    pop es
%endmacro

%else
; --- the inline (and the split host's) arms: the identity --------------------
%define PS
%define PSRC
%ifdef DVW_SPLIT
 %define HENT(x) hent_ %+ x     ; the split host names the part's entries by
%else                           ; their stubs, and nothing else of the part
 %define HENT(x) x
%endif
%macro HCALL 1
    call %1
%endmacro
%macro HJMP 1
    jmp %1
%endmacro
%macro HICALL 1
    call word [cs:%1]
%endmacro
%macro HOSTDS 0
    push cs
    pop ds
%endmacro
%macro HOSTES 0
    push cs
    pop es
%endmacro
%macro DVW_LOWSTK 0
    mov ss, [cs:dvw_lss]
    mov sp, [cs:dos_dstk]
%endmacro
%macro DVW_HBIND 0
%endmacro
%macro DVW_POSTRET 0
    jmp short dvw_rpop
%endmacro
%macro DVW_CLRDK 0
    mov [dvw_done], ax
%endmacro
%macro DVW_PCOPY 0
    push ds
    pop es
%endmacro
%macro DVW_PCEND 0
%endmacro
%macro DVW_PESDS 0
    push ds
    pop es
%endmacro
%macro DVW_PESEND 0
%endmacro
%endif

; PCALL - HOST code calling a part routine (the resident worker, the door
; stubs): the routine itself inline, its stub in a split host
%macro PCALL 1
    call HENT(%1)
%endmacro

%endif                              ; DOSWABI_INC
