; =============================================================================
; os8088 - apps/pagedesk/stubs/pdstub.asm     STAND-IN MODULES (PAGEDESK-SPEC
;                                              3.1, 20.6)
;
; One source, eight images: assembled with -DSTUB_ENG, -DSTUB_REND or
; -DSTUB_WRT into a module with the real one's header and door (PAGEDESK-SPEC
; 3.1, apps/loom/lmpvmod.asm's shape), so the primary - its parts table, its
; header checks, its far vectors, its job scheduler and its three layouts - is
; built and looked at before PDENG, PDREND and PDWRT exist. The Makefile's
; PD_ENG_MOD / PD_REND_MOD / PD_WRT_MOD default to these and the lead flips
; them at integration. NOTHING HERE SHIPS once the real modules land.
;
; What each one answers, and it is deliberately little:
;
;   PDENG   INIT; OPEN in three slices (PDE_BUSY twice - the scheduler's path)
;           and then a 12-page US Letter document; PAGEINFO for those pages;
;           CLOSE. Every other verb CF=1, AX=PDE_NOTFOUND.
;   PDREND  INIT, FONTBANK, BEGIN, STEP (one 16-row band a call, grey bars
;           where text would be, under the gfx lock and the window's clip
;           region it takes itself - the real verb's contract), ABORT, and
;           STATUS, which says the page is an INCOMPLETE rendering by "stub
;           renderer" - so the picture can never be mistaken for a render.
;   PDWRT   INIT; everything else PDE_NOTFOUND (the save path's error text).
;   PDXK PDXG PDXF PDXI PDXJ (-DSTUB_XK ... -DSTUB_XJ, PAGEDESK-SPEC
;           20.22.2.3): the slot's occupants until each owner's lands -
;           PDV_X_INIT, everything else PDE_NOTFOUND, and 1 KiB of bss so the
;           OP_COMP row packs. The Makefile's PD_XK_MOD ... PD_XJ_MOD.
;
; It touches no file. Constants are spelled out here rather than taken from
; apps/pagedesk/pdabi.inc, which is the math agent's and had not landed when
; this was written; the values are pdabi.h's.
; =============================================================================

cpu 8086
bits 16
%include "os88api.inc"

PDE_OK       equ 0
PDE_RANGE    equ 32
PDE_NOTFOUND equ 53
PDE_BUSY     equ 65
PDM_ABI      equ 2                  ; pdabi.h: any PDF (20.22)
%define PDS_BSS 256

%ifdef STUB_ENG
  PDS_MAGIC equ 0x4550
  PDS_BASE  equ 0x00
%elifdef STUB_REND
  PDS_MAGIC equ 0x5250
  PDS_BASE  equ 0x40
%elifdef STUB_WRT
  PDS_MAGIC equ 0x5750
  PDS_BASE  equ 0x80
%elifdef STUB_XK
  PDS_MAGIC equ 0x4B50              ; 'PK'
%elifdef STUB_XG
  PDS_MAGIC equ 0x4750              ; 'PG'
%elifdef STUB_XF
  PDS_MAGIC equ 0x4650              ; 'PF'
%elifdef STUB_XI
  PDS_MAGIC equ 0x4950              ; 'PI'
%elifdef STUB_XJ
  PDS_MAGIC equ 0x4A50              ; 'PJ'
%else
  %error "assemble with -DSTUB_ENG/REND/WRT or -DSTUB_XK/XG/XF/XI/XJ"
%endif
%ifndef PDS_BASE
  %define STUB_OCC                  ; an occupant: its verbs start at 0xC0
  PDS_BASE  equ 0xC0                ; PDV_X_INIT
  %define PDS_BSS 1024
%endif

org 0
pds_hdr:
    dw PDS_MAGIC                    ; +0 magic
    dw PDM_ABI                      ; +2 the ABI
    dw pds_end                      ; +4 image size
    dw PDS_BSS                      ; +6 bss: unused, declared so the part
                                    ;    (image + bss as zeros) compresses the
                                    ;    way a real module's does (OP_COMP)
%if ($ - $$) != 8
  %error "the door is not at +8"
%endif

; the door: AL = verb, BX/CX/DX args; AX out, CF = 1 + AX = PDE_* on failure.
; DS and ES are the caller's on return.
pds_door:
    push ds
    push es
    push si
    push di
    push cs
    pop ds
    cld
    sub al, PDS_BASE
    xor ah, ah
    mov si, ax
    cmp si, (pds_tab_end - pds_tab) / 2
    jae pds_nf
    shl si, 1
    jmp [pds_tab + si]
pds_nf:
    mov ax, PDE_NOTFOUND
pds_fail:
    stc
    jmp short pds_out
pds_ok0:
    xor ax, ax
pds_okax:
    clc
pds_out:
    pop di
    pop si
    pop es
    pop ds
    retf

; INIT, every module: BX = the exchange block
pds_init:
    mov [pds_xb], bx
    jmp pds_ok0

%include "pagedesk/pdkgate.inc"   ; D23: every kernel call on the UI stack (PAGEDESK-SPEC 20.15)

%ifdef STUB_ENG
; ---------------------------------------------------------------------------
pds_tab:
    dw pds_init, pds_open, pds_ok0, pds_pinfo
pds_tab_end:

; OPEN: XB pdxb_open, state at +14. Three slices: BUSY, BUSY, done.
pds_open:
    mov es, [pds_xb]
    mov ax, [es:14]
    inc ax
    mov [es:14], ax
    cmp ax, 3
    jae .done
    mov ax, PDE_BUSY
    jmp pds_okax
.done:
    mov word [es:16], 12            ; npages
    mov word [es:18], 1             ; nrev
    mov word [es:20], 14            ; version 1.4
    mov word [es:22], 0             ; flags
    mov word [es:24], 0             ; size: unknown to a stub
    mov word [es:26], 0
    jmp pds_ok0

; PAGEINFO: BX = page. pdxb_pageinfo: objnum gen media[4] crop[4] rot nannots flags
pds_pinfo:
    cmp bx, 12
    jb .ok
    mov ax, PDE_RANGE
    jmp pds_fail
.ok:
    mov es, [pds_xb]
    xor di, di
    lea ax, [bx+3]
    stosw                           ; objnum
    xor ax, ax
    stosw                           ; gen
    mov cx, 2
.box:
    xor ax, ax
    stosw
    stosw                           ; x0 y0 = 0
    mov ax, 612 * 16
    stosw
    mov ax, 792 * 16
    stosw                           ; x1 y1 = Letter, pt16
    loop .box
    xor ax, ax
    stosw                           ; rotate
    stosw                           ; nannots
    stosw                           ; flags
    jmp pds_ok0
%endif

%ifdef STUB_REND
; ---------------------------------------------------------------------------
pds_tab:
    dw pds_init, pds_begin, pds_step, pds_abort, pds_nfj, pds_nfj
    dw pds_status, pds_ok0
pds_tab_end:
pds_nfj:
    jmp pds_nf

; BEGIN: copy pdxb_render's view and the window; the band starts at the clip
pds_begin:
    push ds
    mov ds, [cs:pds_xb]
    xor si, si
    push cs
    pop es
    mov di, pds_view
    mov cx, 17                      ; view (14 words) + page + win_off + mono
    rep movsw
    pop ds
    ; page width/height in pixels: W*zoom/1600, H*zoom/1600*asp_num/asp_den
    mov ax, [pds_view+4]
    sub ax, [pds_view+0]
    imul word [pds_view+10]
    mov cx, 1600
    idiv cx
    mov [pds_pw], ax
    mov ax, [pds_view+6]
    sub ax, [pds_view+2]
    imul word [pds_view+10]
    idiv cx
    imul word [pds_view+12]
    idiv word [pds_view+14]
    mov [pds_ph], ax
    mov ax, [pds_view+22]           ; clip_y0
    mov [pds_y], ax
    jmp pds_ok0

pds_abort:
    mov word [pds_y], 0x7FFF
    jmp pds_ok0

; STEP: one band of 16 rows, [pds_y, pds_y+15] cut to the clip and the page
pds_step:
    mov ax, [pds_y]
    cmp ax, [pds_view+26]           ; past clip_y1: done
    jle .go
    jmp pds_ok0
.go:
    mov bx, [pds_view+18]           ; oy
    add bx, [pds_ph]                ; page bottom + 1
    cmp ax, bx
    jl .draw
    mov word [pds_y], 0x7FFF
    jmp pds_ok0
.draw:
    call OSAPI_GFX_LOCK
    mov bx, [pds_win]
    call OSAPI_WM_CLIP_SET
    jc .unlock
    mov al, 8                       ; CDGRAY: a placeholder, not text
    call OSAPI_SET_COLOR
    ; x extent: ox + 24 .. ox + pw - 25, cut to the clip
    mov ax, [pds_view+16]
    add ax, 24
    cmp ax, [pds_view+20]
    jge .x0ok
    mov ax, [pds_view+20]
.x0ok:
    mov [pds_x0], ax
    mov ax, [pds_view+16]
    add ax, [pds_pw]
    sub ax, 25
    cmp ax, [pds_view+24]
    jle .x1ok
    mov ax, [pds_view+24]
.x1ok:
    mov [pds_x1], ax
    cmp ax, [pds_x0]
    jl .unlock
    ; every row y of the band with (y - oy - 24) mod 12 in 0..3 and the row
    ; below the top margin and above the bottom one is a bar
    mov cx, 16
    mov dx, [pds_y]
.row:
    cmp dx, [pds_view+22]
    jl .next
    cmp dx, [pds_view+26]
    jg .next
    mov ax, dx
    sub ax, [pds_view+18]
    sub ax, 24
    jl .next
    mov bx, [pds_ph]
    sub bx, 48
    cmp ax, bx
    jge .next
    push dx
    xor dx, dx
    mov bx, 12
    div bx
    cmp dx, 4
    pop dx
    jae .next
    mov ax, [pds_x0]
    mov bx, [pds_x1]
    push cx
    push dx
    call OSAPI_GFX_HLINE            ; AX=x1, BX=x2, DX=y
    pop dx
    pop cx
.next:
    inc dx
    loop .row
.unlock:
    call OSAPI_GFX_UNLOCK
    add word [pds_y], 16
    mov ax, PDE_BUSY
    jmp pds_okax

; STATUS: pdxb_rstatus - incomplete bits, first[32], ops, passes
pds_status:
    mov es, [pds_xb]
    xor di, di
    mov ax, 0x0040                  ; PDF_INC_OPER: nothing was interpreted
    stosw
    mov si, pds_why
    mov cx, 32
    rep movsb
    xor ax, ax
    stosw
    stosw
    jmp pds_ok0

pds_why:    db 'stub renderer', 0
            times 32 - ($ - pds_why) db 0
pds_view:   times 14 dw 0
pds_page:   dw 0
pds_win:    dw 0
pds_mono:   dw 0
pds_pw:     dw 0
pds_ph:     dw 0
pds_y:      dw 0x7FFF
pds_x0:     dw 0
pds_x1:     dw 0
%endif

%ifdef STUB_WRT
; ---------------------------------------------------------------------------
pds_tab:
    dw pds_init
pds_tab_end:
%endif

%ifdef STUB_OCC
; ---------------------------------------------------------------------------
pds_tab:
    dw pds_init                     ; PDV_X_INIT
pds_tab_end:
%endif

pds_xb:     dw 0
align 2, db 0
pds_end:
