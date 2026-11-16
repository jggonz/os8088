; =============================================================================
; os8088 - apps/pagedesk/pdkgate.inc   THE KERNEL-CALL GATE OF A PAGEDESK MODULE
;                                      (PAGEDESK-SPEC 20.15, DECISIONS D23)
;
; Every PageDesk module verb runs on the PRIVATE MODULE STACK, a 2 KiB row of
; claim B that the primary's pdu_mcall (pdstk.inc) swaps SS:SP to around the
; far call into the module's door. The kernel cannot run there: its bodies
; reach .lowbss THROUGH SS (gfx_rowbase's vid_rowtab, evq_push's ring), and
; os88api.inc says so: SS = LOW_SEG is a requirement of every OSAPI call. So
; every kernel call a module makes goes through this gate, which swaps BACK to
; the UI task's stack for the call and down again after it.
;
; HOW A CALL IS ROUTED HERE. Every slot named below is re-defined, for the
; rest of the module's assembly, from `KERNEL_SEG:0xNNNN` (or a RARE cell's
; door, SPEC.md 20.3.4) to a near label
; `pdk_k_<SLOT>`, so apps/cc/os88thunk.asm's `call OSAPI_<SLOT>` - and
; pdrprim.inc's own - land on a three-instruction slot here. NOTHING in
; apps/cc/ changes, for PageDesk or anyone else. A slot is assembled only
; when the C names a thunk that makes that call (the CC_U_<label> defines
; tools/cc8086.py writes, the same gating os88thunk.asm itself uses), so a
; module carries only the slots it uses.
;
; AND NO KERNEL CALL CAN MISS IT: after the slots, KERNEL_SEG itself is
; re-defined to a symbol that does not exist. A `call OSAPI_<anything not
; listed>` then expands to `call pdk_UNWRAPPED_...:0xNNNN` and the module does
; not assemble - add the slot to the table below. (Nothing in a module has any
; other business with KERNEL_SEG: ES is the kernel's on entry to a PACKAGE
; callback, not to a module's door.)
;
; INCLUDE IT ONCE, after the compiled C and before cc/os88thunk.asm (the
; slots read the C's CC_U_ defines, and the thunks must see the re-definition).
; The stand-in modules (stubs/pdstub.asm, no compiled C) include it after
; their door; under STUB_REND it asks for the five slots that one calls.
;
; WHICH STACK. pdk_up decides from SS:SP alone, so no register and no segment
; is needed to ask:
;   * SP < PD_DSTK_BYTES. Task 0's stack sits ON TOP of .lowbss (kernel.asm's
;     STK0_BOT = KLOW_SIZE: every other task's slice, the disk buffers, the
;     glyph table), so its SP is never below 2 KiB; the deep stack's always is.
;   * and the record at the deep stack's top says so: [ss:PD_DSTK_MAG] is
;     PD_DSTK_MAGIC and [ss:PD_DSTK_SELF] is SS itself.
; Either test failing means the module was called flat (no document is open,
; so there is no deep stack; or pdu_mcall was re-entered from inside a
; kernel call, see pdstk.inc) and the call is made where it stands.
;
; THE SWAP keeps every register and every flag the slot was called with, both
; ways (the API's CF, ZF and DF come back as it left them), through statics
; that are TRANSIT only: each is written and read inside one non-reentrant
; stretch (IF = 0 for the swap itself), and what must survive the kernel call
; - the deep SS:SP and whether there was a swap - rides ON THE UI STACK. So a
; kernel call that dispatches the package (a modal loop) and so re-enters a
; module, flat, cannot overwrite it. Those statics live in this segment's
; image and are written through DS, the module's own - os88thunk.asm's
; contract (SS = LOW_SEG, DS = ours at every OSAPI call) less the SS half,
; which is what this file restores. pdrprim.inc's pdr_blit calls with DS ours
; too, and BP is an argument there: nothing here touches BP.
;
; A CALL WHOSE DS IS AN ARGUMENT. OSAPI_DECOMP reads its stream at DS:SI, and
; the slot needs DS = ours on both sides of the far call (the statics). So the
; caller (pdrprim.inc's pdr_lzdec, PAGEDESK-SPEC 20.22.14) STAGES the segment
; in pdk_xds: pdk_up loads it as its last act, after every static it writes,
; and pdk_dn puts DS back from CS before it touches one - a module's CS is its
; DS at every kernel call (each door's `push cs / pop ds`). Assembled only in
; a module whose C names pdr_lzdec, so every other module is byte-identical.
;
; COST: 10 bytes a slot, 74 bytes of body; about 90 clocks of instructions a
; kernel call on top of the kernel's own (8088: ~70 us) - against 756 us for
; the cheapest drawing call and ~400 ms for a disk one.
; =============================================================================

section .text

%ifndef PDABI_INC                   ; the stand-ins spell their constants out
  %define PD_DSTK_BYTES 2048        ; (stubs/pdstub.asm's header says why);
  %define PD_DSTK_UISP  2040        ; these are pdabi.h's, and the real
  %define PD_DSTK_UISS  2042        ; modules take them from pdabi.inc
  %define PD_DSTK_SELF  2044
  %define PD_DSTK_MAG   2046
  %define PD_DSTK_MAGIC 0x4B53
%endif

; -----------------------------------------------------------------------------
; pdk_up - near-called by a slot, every register and flag the API's input.
; On the deep stack: IF off, bank the deep SS:SP ON THE UI STACK under a
; nonzero word (the deep SS), stand on the UI SS:SP the record names. Flat:
; push a zero word where it stands. Either way the slot's flags come back and
; it resumes, ready for its far call.
; -----------------------------------------------------------------------------
pdk_up:
    ; STKBALANCE-OK: a STACK SWITCH (PAGEDESK-SPEC 20.15) - SS:SP move between the
    ; UI stack and the private module stack; each side is balanced on its own stack
    pop word [pdk_ra]               ; the slot's resume address
    pushf
    pop word [pdk_fl]               ; the API's input flags
    cmp sp, PD_DSTK_BYTES           ; task 0's SP is never below 2 KiB
    jae .flat
    cmp word [ss:PD_DSTK_MAG], PD_DSTK_MAGIC
    jne .flat
    push ax
    mov ax, ss
    cmp ax, [ss:PD_DSTK_SELF]
    pop ax
    jne .flat
    cli
    mov [pdk_dsp], sp
    mov [pdk_dss], ss
    mov sp, [ss:PD_DSTK_UISP]       ; SP first: the SS load below still
    mov ss, [ss:PD_DSTK_UISS]       ; reads through the deep SS
    push word [pdk_dsp]
    push word [pdk_dss]             ; nonzero: "came from the deep stack"
    jmp short .go
.flat:
    push word [pdk_zero]            ; zero: "made where it stood"
.go:
    push word [pdk_fl]
%ifdef CC_U__pdr_lzdec              ; a call whose DS is an ARGUMENT (below)
    cmp word [pdk_xds], 0
    je .nx
    mov ds, [pdk_xds]               ; the caller's staged segment, loaded LAST:
.nx:                                ; every static above was ours
    popf
    jmp word [cs:pdk_ra]            ; a module's CS is its DS (the doors)
%else
    popf                            ; IF and every flag as the slot had them
    jmp word [pdk_ra]
%endif

; -----------------------------------------------------------------------------
; pdk_dn - jumped to by a slot after the API returned: take the word pdk_up
; left, go back down if it says so, then `ret` to the thunk from the stack
; the thunk called on, with the API's flags.
; -----------------------------------------------------------------------------
pdk_dn:
    ; STKBALANCE-OK: a STACK SWITCH (PAGEDESK-SPEC 20.15) - SS:SP move between the
    ; UI stack and the private module stack; each side is balanced on its own stack
%ifdef CC_U__pdr_lzdec
    push cs                         ; DS back to ours before a static is
    pop ds                          ; touched (the staged one came back)
%endif
    pushf
    pop word [pdk_fl]               ; the API's answer flags
    pop word [pdk_dss]
    cmp word [pdk_dss], 0
    je .out
    cli
    pop word [pdk_dsp]
    mov ss, [pdk_dss]               ; (on kern_mega a selector: re-read, so a
    mov sp, [pdk_dsp]               ;  compaction during the call is followed)
.out:
    push word [pdk_fl]
    popf
    ret

; transit statics (see the header): in the image, written through DS
align 2, db 0
pdk_ra:     dw 0
pdk_fl:     dw 0
pdk_dsp:    dw 0
pdk_dss:    dw 0
pdk_zero:   dw 0
%ifdef CC_U__pdr_lzdec
pdk_xds:    dw 0                    ; staged DS for the next call, 0 = none
%endif

; THE SLOTS, after the body: `jmp pdk_dn` reaches back short from most of them
; --- the table: a slot, then every caller that names it --------------------
; PDK_SLOT <OSAPI name without OSAPI_>, <label>[, <label>...]: assembled when
; CC_U_<label> is defined for any label listed.
%macro PDK_SLOT 2-*
  %assign %%want 0
  %rep %0 - 1
    %rotate 1
    %ifdef CC_U_%1
      %assign %%want 1
    %endif
  %endrep
  %rotate 1                         ; %1 is the slot again (%0 rotations)
  %if %%want
pdk_k_%1:
    call pdk_up                     ; to the UI stack, if on the deep one
    call OSAPI_%1                   ; KERNEL_SEG:0xNNNN - expanded HERE, before
    jmp pdk_dn                      ; the define below: back, and ret
    %define OSAPI_%1 pdk_k_%1
  %endif
%endmacro

%ifdef STUB_REND                    ; the stand-in renderer's own five calls
  %define CC_U__os88_gfx_lock
  %define CC_U__os88_gfx_unlock
  %define CC_U__os88_wm_clip_set
  %define CC_U__pds_stub
%endif

PDK_SLOT FILE_DFREE,     _os88_disk_cluster_sectors
PDK_SLOT FILE_FIND,      _os88_file_find
PDK_SLOT FILE_READ_SEQ,  _os88_file_read_seq
PDK_SLOT FILE_READ_AT,   _os88_file_read_at
PDK_SLOT FILE_GOTO_QM,   _os88_file_goto_q_mark
PDK_SLOT FILE_HERE,      _os88_file_here
PDK_SLOT FILE_DELETE,    _os88_file_delete
PDK_SLOT FILE_RENAME,    _os88_file_rename
PDK_SLOT FILE_WRITE,     _os88_file_write_segoff, _os88_file_write
PDK_SLOT FILE_WRITE_SEQ, _os88_file_write_seq
PDK_SLOT GET_TICKS,      _os88_ticks
PDK_SLOT GFX_LOCK,       _os88_gfx_lock
PDK_SLOT GFX_UNLOCK,     _os88_gfx_unlock
PDK_SLOT WM_CLIP_SET,    _os88_wm_clip_set
PDK_SLOT GFX_BLIT1,      _pdr_blit         ; pdrprim.inc, directly
PDK_SLOT GFX_BLIT4,      _pdr_blit
PDK_SLOT DECOMP,         _pdr_lzdec        ; pdrprim.inc: DS staged (20.22.14)
PDK_SLOT SET_COLOR,      _pds_stub
PDK_SLOT GFX_HLINE,      _pds_stub

; ...and from here on, an unlisted slot does not assemble (see the header).
; Both spellings of a kernel call are closed: a table cell's far call names
; KERNEL_SEG, and a RARE cell's door (`int OS88_RARE` + its index, SPEC.md
; 20.3.4) names no segment at all, so os88api.inc's OS88_RARE_CALL - which
; every rare `call OSAPI_X` expands through - is re-defined to refuse. The
; slots above expanded theirs already
%define KERNEL_SEG pdk_UNWRAPPED_KERNEL_CALL_add_its_slot_to_pdkgate_inc
%unimacro OS88_RARE_CALL 1
%imacro OS88_RARE_CALL 1
  %error "a RARE kernel cell called outside its gate slot: add its slot to pdkgate.inc"
%endmacro
