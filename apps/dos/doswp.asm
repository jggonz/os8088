; =============================================================================
; os8088 - apps/dos/doswp.asm
;
; **DOS's WINDOWED LAYER AS A PART** (SPEC.md 96.54.9): dosw.inc and doswm.inc
; (SPEC.md 96.54 to 96.54.7) assembled on their own, at org 0, as part
; DOS_PART_DVW of DOS.O88 - an OP_ASSET | OP_COMP | OP_LAZY | OP_SELF row the
; re-homed host reads with op_self into a claim of its own at the first Run in
; Window (apps/dos/doswr.inc's dvw_pfetch).
;
; THE SECOND PASS OF A TWO-PASS BUILD. The host is assembled first with its
; map (-DDOSP_MAP), tools/os88dvwexp.py checks these sources against it and
; writes $(BUILD)/doswexp.inc - the host's symbols, by name - and this file
; assembles against that. So the part names `con_write` or `dos_win` exactly
; as the inline layer did, and the macros of apps/dos/doswabi.inc decide, by
; who OWNS each name, how the reference crosses: CS is this claim, DS is the
; host's wherever host data is touched or the kernel is called.
;
; THE LAYOUT, every offset an assembly constant of this file:
;
;   0              the entry table, DVWE_N four-byte entries (doswabi.inc)
;   ...            dosw.inc, doswm.inc and the part-only glue below
;   dvw_pcells     apps/dos/doswcell.inc: the layer's own cells, as zeros
;   dvwx_tab       a far pointer per host routine the part calls, and the
;                  two thunks of DVW_POSTRET and HICALL - zeros too, every
;                  word of it written by dvw_hbind
; =============================================================================

%define DVW
%define DVW_PART
    cpu 8086
    bits 16
    org 0

%include "os88api.inc"              ; the OSAPI_* cells and the OS88_* macros
%include "doswexp.inc"              ; ...THEN the host's names, which shadow
                                    ; any constant os88api.inc also defines
                                    ; with the same value (os88dvwexp.py)
%include "doswabi.inc"

; --- the entry table: entry n at n * 4 ---------------------------------------
dvwp_entries:
dvwe_start:   call dvw_start        ; DVWE_START
              retf
dvwe_wake:    call dvw_wake         ; DVWE_WAKE
              retf
dvwe_key:     call dvw_key          ; DVWE_KEY
              retf
dvwe_onclose: call dvw_onclose      ; DVWE_ONCLOSE
              retf
dvwe_mouse:   call dwm_mouse        ; DVWE_MOUSE
              retf
dvwe_txt:     call dwm_txt          ; DVWE_TXT
              retf
dvwe_fsmain:  call dvw_fsmain       ; DVWE_FSMAIN
              retf
dvwe_runprog: call dvw_runprog      ; DVWE_RUNPROG
              retf
dvwe_regrow:  call dvw_regrow       ; DVWE_REGROW
              retf
dvwe_post:    jmp strict near dvw_post  ; DVWE_POST - a JUMP: the door stub's
              db 0xCC               ; return is the word dvw_post reads
dvwe_fin:     jmp strict near dvw_fin   ; DVWE_FIN - a JUMP: the core's return
              db 0xCC               ; is the word dvw_fin drops
%if ($ - $$) != DVWE_N * 4 || (dvwe_runprog - $$) != DVWE_RUNPROG * 4 || (dvwe_fin - $$) != DVWE_FIN * 4
 %error "the entry table is DVWE_N four-byte entries, in DVWE_* order (doswabi.inc)"
%endif

%include "dosw.inc"
%include "doswm.inc"

; -----------------------------------------------------------------------------
; dvw_hbind - the HOST's segment into dvw_hseg and every far pointer OUT
; (DVW_HBIND, at the head of dvw_start). in: DS = the host; every register kept
;
; dvw_start is the first entry of every run, and the host can move only
; between runs (its region is pinned by the worker's frame for a run), so
; once here is enough: no other entry can be reached with stale pointers.
; -----------------------------------------------------------------------------
;
; The pointers are ZEROS in the image (they follow the cells) and their
; offsets are COMPUTED: the host's thunks are a 4-byte stride from dvwh_0, in
; DVW_HOSTCALLS order and then dvwh_ret and dvwh_icall (doswr.inc asserts it),
; so a table of 30 incompressible words ships as nothing.
; -----------------------------------------------------------------------------
dvw_hbind:
    push ax
    push di
    mov [cs:dvw_hseg], ds
    mov di, PADDR(dvwx_tab)
    mov ax, dvwh_0
.b:
    mov [cs:di], ax
    mov [cs:di+2], ds
    add ax, 4
    add di, 4
    cmp di, PADDR(dvwx_end)
    jb .b
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; dvw_key - DVWE_KEY: dos_key's arm for a running program (SPEC.md 96.54,
; 96.54.1), moved here because both cells it decides on are the part's
; in:  AX = the key, BX = the window, DS = the host
; -----------------------------------------------------------------------------
dvw_key:
    cmp ax, KSC_ENTER << 8          ; ALT+ENTER is the full screen, for a
    jne .key                        ; running program and a frozen one alike
    mov byte [cs:dvw_fsreq], 1
    call OSAPI_WM_WAKE              ; (entered from the wake: the bracket
    ret                             ; wants the lock free, and dos_key holds it)
.key:
    cmp byte [cs:dvw_frz], 0        ; a frozen program takes no keys
    jne .r
    call dvw_kput
.r:
    ret

%include "doswcell.inc"

; --- OUT: a far pointer per host routine, all of it dvw_hbind's ------------
; zeros, after the cells; numbered as doswr.inc numbers the thunks, off the
; same list, and then DVW_POSTRET's `ret` and HICALL's thunk - the order
; dvw_hbind's stride assumes
%assign DVWH_N 0
%macro DVW_HX 1
dvwx_%1:
    dd 0
%assign DVWH_N DVWH_N + 1
%endmacro
dvwx_tab:
    DVW_HOSTCALLS
dvwx_ret:
    dd 0
dvwx_icall:
    dd 0
dvwx_end:
%if dvwh_ret != dvwh_0 + 4 * DVWH_N || dvwh_icall != dvwh_ret + 4
 %error "the host's thunks are not the stride dvw_hbind computes (doswr.inc)"
%endif
