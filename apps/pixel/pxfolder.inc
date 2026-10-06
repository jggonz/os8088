; =============================================================================
; os8088 - apps/pixel/pxfolder.inc
;
; PiXEL's FOLDER (SPEC.md 106.21): Prev and Next through the folder list
; px_walk keeps, and the windowed slideshow - whose next slide is decoded
; HIDDEN beside the one shown. (The filmstrip, its thumbnails and their cache
; PIXEL.THC were withdrawn at the owner's request, 2026-10-06.)
;
; A HIDDEN DECODE is 106.9's pipeline with the picture shown banked as the
; previous one (px_prev) and its HAVE byte masked, so px_claims never frees it
; and px_mreloc still follows its master; px_tables does nothing while
; [px_hmode] says so, nothing is laid out, painted or composed, and at its end
; the slide waits for its deadline, or px_restore puts the shown picture back
; as a cancel would - with no tables to build, because none were touched.
; Every callback that could draw or act first calls px_hstop, which cancels
; one and waits for the worker's answer.
; =============================================================================

PX_SLIDE    equ 91                  ; ticks a slide shows (5 s)

; =============================================================================
; INPUT, AND THE FOLDER LIST
; =============================================================================

; px_acted - a key or a press: a hidden decode gives the record back, and a
; slideshow running stops - CF = 1 then, the input spent on stopping it
; (SPEC.md 106.21). Preserves all but the flags
px_acted:
    call px_hstop
    cmp byte [px_slon], 0
    je .no
    call px_slstop
    stc
    ret
.no:
    clc
    ret

; px_slarm - a slideshow begun: the timer at its pace now (px_ontimer keeps
; it there while one runs). Preserves all
px_slarm:
    push ax
    push bx
    mov bx, [px_win]
    mov ax, PX_TQ
    call OSAPI_WM_TIMER
    pop bx
    pop ax
    ret

; px_foldok - CF = 0 when the open picture has another beside it in the
; folder list: Prev, Next and the slideshow are live. Preserves all
px_foldok:
    call px_haspic
    jc .no
    cmp word [px_nnames], 2
    jb .no
    push ax
    mov ax, [px_ncur]
    cmp ax, [px_nnames]
    pop ax
    jae .no
    clc
    ret
.no:
    stc
    ret

; px_rec - AX = a name: SI = its record. Preserves all but SI
px_rec:
    push ax
    push dx
    mov si, PX_NREC
    mul si
    add ax, px_names
    mov si, ax
    pop dx
    pop ax
    ret

; px_nfgo - stand in the folder the list is of (a cancelled dialog may have
; left the instance elsewhere). CF = 1 it is gone (a disk swapped).
; Preserves all
px_nfgo:
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE
    cmp dx, [px_nfdir]
    jne .go
    cmp bl, [px_nfvol]
    je .ok
.go:
    mov dx, [px_nfdir]              ; (the quiet form: a folder that will not
    mov bl, [px_nfvol]              ; list moves nothing, review-w5 F6)
    call OSAPI_FILE_GOTO_QM
    jc .out
.ok:
    clc
.out:
    pop dx
    pop bx
    pop ax
    ret

; px_navto - AX = a name in the folder list: its picture opened, as File >
; Open would (SPEC.md 106.21). THE ONE PLACE A PICTURE IS LEFT: wave 7's
; OS88UI_ASAVE goes here, before anything moves, and nowhere else. Lock held.
; Preserves all
px_navto:
    push ax
    push si
    push di
    cmp ax, [px_nnames]
    jae .out
    mov [px_lvarg], ax              ; UNSAVED EDITS: "Save changes?" first
    mov al, PXL_NAV                 ; (SPEC.md 106.24), and this again once
    call px_gate                    ; its answer allows (px_lvdo)
    mov ax, [px_lvarg]
    jc .out
    call px_nfgo
    jc .gone
    call px_rec
    mov di, px_oname
    call px_strcpy
    call px_open
    jmp short .out
.gone:
    mov si, px_s_notfile
    call px_toast
.out:
    pop di
    pop si
    pop ax
    ret

; px_navstep - AL = 1 the next picture, 0 the previous, wrapping. Lock held.
; Preserves all
px_navstep:
    call px_foldok
    jc .out
    push ax
    push dx
    mov dl, al
    mov ax, [px_ncur]
    or dl, dl
    jz .prev
    inc ax
    cmp ax, [px_nnames]
    jb .go
    xor ax, ax
    jmp short .go
.prev:
    or ax, ax
    jnz .p1
    mov ax, [px_nnames]
.p1:
    dec ax
.go:
    call px_navto
    pop dx
    pop ax
.out:
    ret

; px_navend - AL = 0 the folder's first picture, 1 its last. Lock held.
; Preserves all
px_navend:
    call px_foldok
    jc .out
    push ax
    or al, al
    mov ax, 0
    jz .f
    mov ax, [px_nnames]
    dec ax
.f:
    cmp ax, [px_ncur]
    je .o
    call px_navto
.o:
    pop ax
.out:
    ret

; px_foldkey - AL/AH = a key: Space the next picture, Backspace the
; previous, Home the first, End the last. CF = 0 it was one. Preserves all
px_foldkey:
    push ax
    cmp al, ' '
    jne .b
    mov al, 1
    jmp short .step
.b:
    cmp ax, 0x0E08                  ; Backspace (and not Ctrl+H)
    jne .h
    xor al, al
.step:
    call px_navstep
    jmp short .yes
.h:
    cmp ah, 0x47                    ; Home
    jne .e
    xor al, al
    jmp short .end
.e:
    cmp ah, 0x4F                    ; End
    jne .no
    mov al, 1
.end:
    call px_navend
.yes:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; px_navbtn - AX = a live button that fired: CF = 0 it was the folder's -
; Prev, Next (the toolbar's and the status bar's), the slideshow - and has
; been done. Preserves all
px_navbtn:
    push ax
    cmp ax, PX_B_PREV
    je .prev
    cmp ax, PX_B_SPREV
    je .prev
    cmp ax, PX_B_NEXT
    je .next
    cmp ax, PX_B_SNEXT
    je .next
    cmp ax, PX_B_SHOW
    jne .no
    call px_sltoggle
    jmp short .yes
.prev:
    xor al, al
    jmp short .step
.next:
    mov al, 1
.step:
    call px_navstep
.yes:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; px_hfile - px_examine's facts for a hidden decode's file, out of its
; record: no walk. Preserves all
px_hfile:
    push ax
    push si
    mov ax, [px_hidx]
    call px_rec
    mov ax, [si + NR_SIZE]
    mov [px_fsize], ax
    mov ax, [si + NR_SIZE + 2]
    mov [px_fsize + 2], ax
    mov al, [si + NR_FLAGS]
    and al, NRF_PACKED
    mov [px_fcomp], al
    mov byte [px_ffound], 1
    mov ax, [px_nnames]             ; (a folder lists 64 at most: all of them)
    mov [px_fcount], ax
    mov ax, [px_hidx]
    inc ax
    mov [px_fidx], ax
    pop si
    pop ax
    ret

; =============================================================================
; THE HIDDEN DECODE (SPEC.md 106.21)
; =============================================================================

; px_hopen - AX = a name, the next slide: decoded HIDDEN, the picture shown
; banked and kept. UI task, lock held, nothing in flight. CF = 1 AX = a PXD_*
; refusal (the window exactly as it was)
px_hopen:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov [px_hidx], ax
    mov byte [px_hmode], 2
    mov si, px_cur                  ; bank what is showing, as px_open does
    mov di, px_prev
    mov cx, PXR_SZ
    cld
    rep movsb
    mov si, px_pal
    mov di, px_ppal
    mov cx, 768
    rep movsb
    mov si, px_plan
    mov di, px_pplan
    mov cx, 768
    rep movsb
    mov al, [px_pvalid]
    mov [px_ppvalid], al
    mov al, 0xFF                    ; FULL SCREEN's is unmasked (106.23):
    cmp byte [px_fsin], 0           ; the glass keeps the shown picture, so
    jne .hm                         ; its master may go for a finer scale
    mov al, [px_prev + PXR_HAVE]    ; ...the window's is MASKED: px_claims
    mov byte [px_prev + PXR_HAVE], 0    ; never frees it for a better scale
.hm:                                ; (px_mreloc still follows its master)
    mov [px_hhave], al
    mov al, [px_tabok]
    mov [px_htab], al
    mov al, [px_tdepth]
    mov [px_htdep], al
    mov al, [px_zreq]
    mov [px_hzreq], al
    mov byte [px_zreq], 0xFF
    mov ax, [px_mcap]
    mov [px_hmcap], ax
    mov byte [px_cur + PXR_HAVE], 0
    mov word [px_cur + PXR_MSEG], 0
    mov word [px_cur + PXR_MKB], 0
    mov ax, [px_hidx]
    call px_rec
    mov di, px_cur + PXR_NAME
    call px_strcpy
    mov ax, [px_nfdir]
    mov [px_cur + PXR_DIR], ax
    mov al, [px_nfvol]
    mov [px_cur + PXR_VOL], al
    mov word [px_hseg], 0
    mov word [px_flatraw], 0
    mov byte [px_rflat], 0
    mov byte [px_clok], 0
    call px_examine                 ; (px_hfile, no walk), the head, the sniff
    jc .refuse
    mov al, [px_cur + PXR_FMT]
    mov [px_rfmt], al
    call px_hdrparse
    jc .refuse
    call px_headfree
    call px_claims
    mov cx, [px_hmcap]              ; (a test's cap back; mov keeps CF)
    mov [px_mcap], cx
    jc .refuse
    call px_setpal
    call px_pumpinit
    call px_pumpone
    call px_spawn
    jc .nowork
    mov byte [px_abort], 0
    mov byte [px_tabok], 0
    mov word [px_ypaint], 0
    mov word [px_ypbot], 0
    mov byte [px_job], JOB_DECODE   ; LAST: the worker starts on this byte
    clc
    jmp short .out
.nowork:
    mov ax, PXD_MEM
    call px_relclaims
.refuse:
    push ax
    call px_headfree
    call px_relflat
    call px_hback
    pop ax
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; px_hback - the picture shown back, as a cancel would put it, and its
; tables' state with it: no hidden decode. Preserves all
px_hback:
    push ax
    mov al, [px_hhave]
    cmp al, 0xFF                    ; (unmasked: what px_claims left stands)
    je .u
    mov [px_prev + PXR_HAVE], al
.u:
    mov ax, [px_hmcap]
    mov [px_mcap], ax
    call px_restore
    mov al, [px_htab]
    mov [px_tabok], al
    mov al, [px_htdep]
    mov [px_tdepth], al
    mov al, [px_hzreq]
    mov [px_zreq], al
    mov byte [px_hmode], 0
    pop ax
    ret

; px_hfree - the hidden picture's master given back, then px_hback.
; Preserves all
px_hfree:
    push dx
    mov dx, [px_cur + PXR_MSEG]
    or dx, dx
    jz .b
    call OSAPI_MEM_FREE
    mov word [px_cur + PXR_MSEG], 0
.b:
    call px_hback
    pop dx
    ret

; px_hdone - the worker has answered a hidden decode (UI task, lock held):
; the ring and the work go back; a slide decoded waits for its deadline (mode
; 3), and one refused or stopped is let go - the picture shown put back, and
; a refusal opens the slide in sight at the deadline. Preserves all
px_hdone:
    push dx
    mov dx, [px_ringraw]
    or dx, dx
    jz .w
    call OSAPI_MEM_FREE
    mov word [px_ringraw], 0
.w:
    mov dx, [px_wbase]
    or dx, dx
    jz .f
    call OSAPI_MEM_FREE
    mov word [px_wbase], 0
    mov word [px_wseg], 0
.f:
    call px_relflat
    cmp byte [px_wres], 0
    jne .bad
    mov byte [px_hmode], 3          ; decoded: px_sltick shows it when due
    jmp short .out
.bad:
    cmp byte [px_wres], PXD_ABORT
    je .ab
    mov byte [px_slmem], 1          ; it opens in sight at the deadline
.ab:
    call px_hfree
.out:
    pop dx
    ret

; px_hstop - THE USER ACTS: a hidden decode running is cancelled and its
; answer waited for - one row of the decoder at most - and a slide decoded
; and waiting let go; the window is then exactly as it was. A GIF's frame
; job stops the same way (106.25: its next tick takes it up where it was).
; Every callback that draws or acts calls this first. Preserves all
px_hstop:
    cmp byte [px_anjob], 0
    je .h
    call px_await
    push cx
    mov cl, AV_WAKE                 ; (its claims back, its rect owed)
    call px_ancall
    pop cx
.h:
    cmp byte [px_hmode], 0
    jne .go
    ret
.go:
    push ax
    cmp byte [px_hmode], 3
    je .ready
    call px_await
    call px_hdone
    cmp byte [px_hmode], 3
    jne .out
.ready:
    call px_hfree
.out:
    pop ax
    ret

; px_await - the worker's job aborted and its answer waited for - one row
; of the decoder at most. The worker needs no lock: holding it while
; yielding only keeps other drawers waiting. Preserves all
px_await:
    mov byte [px_abort], 1
.w:
    cmp byte [px_job], JOB_NONE
    je .x
    call OSAPI_TASK_YIELD
    jmp short .w
.x:
    ret

; px_hcommit - the slide decoded (mode 3) REPLACES the picture shown: the old
; master freed, the tables built for its palette by the worker (px_busy 2),
; and px_slpre / px_finished compose and draw the window when they are.
; Lock held
px_hcommit:
    push ax
    call px_hcommit0
    call px_viewnew                 ; at Fit
    call px_viewclaim
    mov al, [px_bpp]
    mov [px_tdepth], al
    xor al, al
    mov [px_tabok], al
    mov [px_hok], al
    mov [px_tabdirty], al
    call px_spawn
    mov byte [px_abort], 0
    mov byte [px_busy], 2
    mov byte [px_slpost], 1
    mov byte [px_job], JOB_TABLES   ; LAST
    pop ax
    ret

; px_hcommit0 - the commit's head, full screen's too (106.23): the banked
; picture's master given back, the hidden one shown. Clobbers AX
px_hcommit0:
    call px_anfree                  ; (a slide is shown still, 106.25)
    call px_eclear                  ; undo, the selection: an open's
                                    ; (review-w7 F9)
    mov al, [px_hhave]              ; (0FFh: full screen's unmasked decode -
    cmp al, 0xFF                    ; the shown one's HAVE was never touched)
    je .u
    mov [px_prev + PXR_HAVE], al
.u:
    call px_freeprev                ; the old master goes
    mov ax, [px_hmcap]
    mov [px_mcap], ax
    mov al, [px_hzreq]
    mov [px_zreq], al
    mov byte [px_hmode], 0
    mov byte [px_cur + PXR_HAVE], 1
    inc byte [px_cur + PXR_SERIAL]
    mov ax, [px_hidx]
    mov [px_ncur], ax
    mov byte [px_cvmsg], 0
    ret

; px_slpre - before px_finish: a commit's tables are built, so the window is
; composed for the new picture first. Lock held. Preserves all
px_slpre:
    cmp byte [px_slpost], 0
    je .out
    push ax
    push bx
    call px_compose
    call px_flags
    mov bx, [px_win]
    mov ax, px_title
    call OSAPI_WM_TITLE
    pop bx
    pop ax
.out:
    ret

; px_finished - AL = what px_busy was, after px_finish: a picture opened (or
; a commit's tables built) sets a slideshow's next deadline. Lock held.
; Preserves all
px_finished:
    push ax
    cmp byte [px_slpost], 0
    je .open
    mov byte [px_slpost], 0
    mov al, PX_R_TB | PX_R_STATUS
    call px_regdraw
    jmp short .shown
.open:
    cmp al, 1                       ; an open that ended with its picture
    jne .out
    cmp byte [px_wres], 0
    jne .out
.shown:
    cmp byte [px_slon], 0
    je .out
    call OSAPI_GET_TICKS
    add ax, PX_SLIDE
    mov [px_sldue], ax
.out:
    pop ax
    ret

; =============================================================================
; A GIF THAT PLAYS (SPEC.md 106.25): the resident's hooks. The GIF part does
; the rest - its frames on the worker, its UI verbs through PXV_INFO
; =============================================================================

; px_ancall - CL = an AV_* verb of the GIF part's (fetched if it must be).
; Preserves all but the flags
px_ancall:
    push ax
    push bx
    mov al, PXPART_GIF
    mov bl, PXV_INFO
    call px_pcall
    pop bx
    pop ax
    ret

; px_antick - the timer: a GIF playing goes on while the window is in front,
; and stops - holding nothing but its own small claim - while it is not.
; Lock held. Preserves all
px_antick:
    cmp byte [px_anon], 0
    je .x
    cmp word [px_behind], 0         ; (px_ontimer's)
    je .front
    jmp px_hstop
.front:
    cmp byte [px_pcon], 0           ; A CARD UP OR A PRESS HELD: no frame is
    jne .x                          ; begun or let go under it - its release
    cmp byte [px_press], PX_PR_NONE ; acts, and an action meets no running
    jne .x                          ; job (review-w8 A2)
    push ax
    call os88ui_armed
    or ax, ax
    pop ax
    jnz .x
    push cx
    mov cl, AV_TICK
.c:
    call px_ancall
    pop cx
.x:
    ret

; px_antog - File > Play / Stop Animation, and A. Lock held. Preserves all
px_antog:
    cmp byte [px_anim], 0           ; (no GIF that moves: nothing fetched -
    je px_antick.x                  ; review-w8 A6)
    push cx
    mov cl, AV_TOGGLE
    jmp short px_antick.c

; px_anfree - a picture left: its animation's claim back, nothing playing.
; Preserves all
px_anfree:
    push dx
    mov dx, [px_anseg]
    or dx, dx
    jz .z
    call OSAPI_MEM_FREE
.z:
    xor dx, dx
    mov [px_anseg], dx
    mov [px_anim], dx               ; (and px_anon, the byte after it)
    mov [px_anuser], dl
    pop dx
    ret

; =============================================================================
; THE SLIDESHOW (SPEC.md 106.21)
; =============================================================================

; px_sltoggle - File > Slideshow, the toolbar's button. Lock held
px_sltoggle:
    cmp byte [px_slon], 0
    jne px_slstop
    call px_foldok
    jnc .go
    push si
    mov si, px_s_noother
    call px_toast
    pop si
    ret
.go:
    cmp byte [px_busy], 0           ; not over an operation or a save under
    je .g2                          ; way: its result would be thrown away
    push si                         ; with no question (review-w7 F9)
    mov si, px_s_busy
    call px_toast
    pop si
    ret
.g2:
    push ax
    mov al, PXL_SLIDE               ; unsaved edits: asked first (106.24)
    call px_gate
    pop ax
    jc .x0
    push ax
    push bx
    mov byte [px_slon], 1
    mov byte [px_slmem], 0
    call OSAPI_GET_TICKS
    add ax, PX_SLIDE
    mov [px_sldue], ax
    call px_flags                   ; the button latched, the item "Stop"
    call px_update
    call px_slarm                   ; (the timer's pace: the slideshow's now)
    pop bx
    pop ax
.x0:
    ret

; px_slstop - the slideshow stops, a slide decoded and waiting let go.
; Preserves all
px_slstop:
    cmp byte [px_slon], 0
    je .out
    mov byte [px_slon], 0
    call px_hstop
    call px_flags
    call px_update
.out:
    ret

; px_sltick - the slideshow's turn of the timer: the next picture decoded
; hidden beside the one shown, and shown when it is due; when there is no
; room for that, opened in sight when it is due. Lock held. Preserves all
px_sltick:
    cmp byte [px_slon], 0
    je .out
    cmp byte [px_busy], 0
    jne .out
    cmp byte [px_job], JOB_NONE
    jne .out
    cmp byte [px_dirty], 0          ; AN EDIT on the slide shown (one that
    je .cl                          ; was under way when it began): the
    push si                         ; slideshow stops there - the next slide
    call px_slstop                  ; would replace it unasked (review-w7
    mov si, px_s_sfirst             ; F9)
    call px_toast
    pop si
    ret
.cl:
    push ax
    push dx
    call OSAPI_GET_TICKS
    sub ax, [px_sldue]              ; >= 0: due
    cmp byte [px_hmode], 3
    jne .h0
    or ax, ax
    js .o
    call px_hcommit
    jmp short .o
.h0:
    cmp byte [px_hmode], 0
    jne .o
    cmp byte [px_slmem], 0
    jne .vis
    call px_slnextidx
    call px_nfgo
    jc .nr
    call px_hopen
    jnc .o
.nr:
    mov byte [px_slmem], 1          ; no room beside it: in sight when due
    jmp short .o
.vis:
    or ax, ax
    js .o
    mov byte [px_slmem], 0
    call px_slnext
.o:
    pop dx
    pop ax
.out:
    ret

; px_slnext - THE SLIDESHOW'S STEP: the next picture in the folder opened in
; sight. Full screen's slideshow (wave 6) steps through this. Lock held.
; Preserves all
px_slnext:
    push ax
    call px_slnextidx
    call px_navto
    pop ax
    ret

; px_slnextidx - AX = the picture after the open one, wrapping
px_slnextidx:
    mov ax, [px_ncur]
    inc ax
    cmp ax, [px_nnames]
    jb .r
    xor ax, ax
.r:
    ret
