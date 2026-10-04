; =============================================================================
; os8088 - apps/pixel/pxfolder.inc
;
; PiXEL's FOLDER (SPEC.md 106.21): Prev and Next through the folder list
; px_walk keeps, the filmstrip's pagers and its cards pressed, the cards'
; THUMBNAILS - made from the open picture's master, or by a HIDDEN DECODE of
; the others in idle time - their store, their cache SYSTEM/APPDATA/PIXEL.THC,
; and the windowed slideshow.
;
; A HIDDEN DECODE is 106.9's pipeline with the picture shown banked as the
; previous one (px_prev) and its HAVE byte masked, so px_claims never frees it
; and px_mreloc still follows its master; px_tables does nothing while
; [px_hmode] says so, nothing is laid out, painted or composed, and at its end
; px_restore puts the shown picture back as a cancel would - with no tables to
; build, because none were touched. Every callback that could draw or act
; first calls px_hstop, which cancels one and waits for the worker's answer.
; =============================================================================

; (the store's layout, a thumbnail's key and the cache's header are
; pxthc.inc's: the FOLDER part, apps/pixel/pxfold.asm, reads them too)
PX_TKB      equ (PXT_SL0 + PX_TSLOTS * PXT_SLSZ + 1023) / 1024
PX_THMINFREE equ 128                ; KB the heap keeps beside the store
PX_THIDLE   equ 18                  ; ticks of no input before a decode
PX_SLIDE    equ 91                  ; ticks a slide shows (5 s)

; =============================================================================
; INPUT, AND THE FOLDER LIST
; =============================================================================

; px_touch - the tick of the last input: the thumbnails wait PX_THIDLE past
; it. Preserves all
px_touch:
    push ax
    call OSAPI_GET_TICKS
    mov [px_lastact], ax
    pop ax
    ret

; px_acted - a key or a press: a hidden decode gives the record back, and a
; slideshow running stops - CF = 1 then, the input spent on stopping it
; (SPEC.md 106.21). Preserves all but the flags
px_acted:
    call px_hstop
    call px_touch
    cmp byte [px_slon], 0
    je .no
    call px_slstop
    stc
    ret
.no:
    clc
    ret

; px_tqarm - the thumbnails may have work: the timer at their pace now.
; Preserves all
px_tqarm:
    cmp byte [px_thoff], 0
    jne .out
    push ax
    push bx
    mov byte [px_tq], 1
    mov bx, [px_win]
    mov ax, PX_TQ
    call OSAPI_WM_TIMER
    pop bx
    pop ax
.out:
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
    mov dx, [px_nfdir]
    mov bl, [px_nfvol]
    call OSAPI_FILE_GOTO
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
; Prev, Next (the toolbar's and the status bar's), the pagers, the
; slideshow - and has been done. Preserves all
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
    cmp ax, PX_B_FSL
    je .pl
    cmp ax, PX_B_FSR
    je .pr
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
    jmp short .yes
.pl:
    xor al, al
    jmp short .page
.pr:
    mov al, 1
.page:
    call px_fspage
.yes:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; px_fspage - AL = 1 the strip a stripful on, 0 back: the cards that change
; are drawn (px_fslay clamps the turn), and the thumbnails told. Lock held.
; Preserves all
px_fspage:
    push ax
    push bx
    mov bx, [px_fcn]
    or al, al
    jnz .r
    neg bx
.r:
    add [px_fsoff], bx
    call px_update
    call px_tqarm
    pop bx
    pop ax
    ret

; px_pgflags - the filmstrip's pagers greyed at either end of the folder.
; Preserves all
px_pgflags:
    and word [px_bflags + 2 * PX_B_FSL], ~OS88UI_DIS
    and word [px_bflags + 2 * PX_B_FSR], ~OS88UI_DIS
    cmp word [px_fcs], 0
    jne .l
    or word [px_bflags + 2 * PX_B_FSL], OS88UI_DIS
.l:
    push ax
    mov ax, [px_fcs]
    add ax, [px_fcn]
    cmp ax, [px_nnames]
    pop ax
    jb .r
    or word [px_bflags + 2 * PX_B_FSR], OS88UI_DIS
.r:
    ret

; px_cardat - CX, DX = a point: CF = 0 and AX = the name whose filmstrip card
; is there. Preserves all but AX
px_cardat:
    cmp byte [px_fson], 0
    je .no
    cmp dx, [px_fry1]
    jl .no
    cmp dx, [px_fry2]
    jg .no
    push bx
    push dx
    mov ax, cx
    sub ax, [px_fcl]
    jl .no2
    xor dx, dx
    mov bx, PX_CARDW
    div bx
    cmp ax, [px_fcn]
    jae .no2
    add ax, [px_fcs]
    cmp ax, [px_nnames]
    jae .no2
    pop dx
    pop bx
    clc
    ret
.no2:
    pop dx
    pop bx
.no:
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

; px_hopen - AX = a name, DL = 1 its thumbnail or 2 the next slide: decoded
; HIDDEN, the picture shown banked and kept. UI task, lock held, nothing in
; flight. CF = 1 AX = a PXD_* refusal (the window exactly as it was)
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
    mov [px_hmode], dl
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
    mov al, [px_prev + PXR_HAVE]    ; ...and MASKED: px_claims never frees it
    mov [px_hhave], al              ; for a better scale (px_mreloc still
    mov byte [px_prev + PXR_HAVE], 0    ; follows its master)
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
    cmp byte [px_hmode], 1
    jne .claim
    call px_tcap                    ; a thumbnail's: the master covering it
.claim:
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
    cmp byte [px_hmode], 1
    jne .ok
    inc word [px_thdec]
.ok:
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

; px_tcap - [px_mcap] := the master of the coarsest scale at or below 1/8
; whose sides still cover the thumbnail's box (min(72, w) by min(54, h)),
; and the reserve px_pickscale adds: memory then decides that scale, or a
; coarser one. Preserves all
px_tcap:
    push ax
    push bx
    push cx
    push dx
    mov dx, [px_cur + PXR_SW]       ; DX = the width wanted...
    cmp dx, PX_TW
    jb .w
    mov dx, PX_TW
.w:
    mov ax, [px_cur + PXR_SH]       ; ...AX the height
    cmp ax, PX_TH
    jb .h
    mov ax, PX_TH
.h:
    mov cl, 3
.s:
    mov bx, [px_cur + PXR_SW]
    shr bx, cl
    cmp bx, dx
    jb .n
    mov bx, [px_cur + PXR_SH]
    shr bx, cl
    cmp bx, ax
    jae .got
.n:
    dec cl
    jnz .s                          ; (1/1 always covers it)
.got:
    call px_mkb                     ; AX = that master's KB
    add ax, PX_RESKB
    mov [px_mcap], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_hback - the picture shown back, as a cancel would put it, and its
; tables' state with it: no hidden decode. Preserves all
px_hback:
    push ax
    mov al, [px_hhave]
    mov [px_prev + PXR_HAVE], al
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
; the ring and the work go back; a thumbnail is made from the master and the
; picture shown put back; a slide decoded waits for its deadline (mode 3).
; A refused thumbnail's name is marked. CF = 0: a card has something new
px_hdone:
    push ax
    push dx
    push si
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
    mov al, [px_wres]
    cmp byte [px_hmode], 2
    je .slide
    or al, al
    jnz .tbad
    mov ax, [px_hidx]
    call px_thmake                  ; the thumbnail, through its palette
    call px_hfree
    clc
    jmp short .out
.tbad:
    cmp al, PXD_ABORT               ; (stopped: it starts again later)
    je .tab
    mov [px_thref], al              ; (the refusal, for a test)
    mov ax, [px_hidx]
    call px_rec
    or byte [si + NR_FLAGS], NRF_NOTH
.tab:
    call px_hfree
    stc
    jmp short .out
.slide:
    or al, al
    jnz .sbad
    mov byte [px_hmode], 3          ; decoded: px_sltick shows it when due
    stc
    jmp short .out
.sbad:
    cmp al, PXD_ABORT
    je .sab
    mov byte [px_slmem], 1          ; it opens in sight at the deadline
.sab:
    call px_hfree
    stc
.out:
    pop si
    pop dx
    pop ax
    ret

; px_hstop - THE USER ACTS: a hidden decode running is cancelled and its
; answer waited for - one row of the decoder at most - and a slide decoded
; and waiting let go; the window is then exactly as it was. Every callback
; that draws or acts calls this first. Preserves all
px_hstop:
    cmp byte [px_hmode], 0
    jne .go
    ret
.go:
    push ax
    cmp byte [px_hmode], 3
    je .ready
    mov byte [px_abort], 1
.w:
    cmp byte [px_job], JOB_NONE     ; (the worker needs no lock: holding it
    je .ans                         ; while yielding only keeps other
    call OSAPI_TASK_YIELD           ; drawers waiting)
    jmp short .w
.ans:
    call px_hdone                   ; (a thumbnail it finished is kept)
    cmp byte [px_hmode], 3
    jne .out
.ready:
    call px_hfree
.out:
    pop ax
    ret

; px_hcommit - the slide decoded (mode 3) REPLACES the picture shown: the old
; master freed, the tables built for its palette by the worker (px_busy 2),
; and px_slpre / px_finished compose and draw the window when they are.
; Lock held
px_hcommit:
    push ax
    mov al, [px_hhave]
    mov [px_prev + PXR_HAVE], al
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
    mov word [px_fsoff], 0
    mov byte [px_cvmsg], 0
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
; a commit's tables built) sets a slideshow's next deadline, and the
; thumbnails are told (the open one's is made from its master in the next
; idle step). Lock held. Preserves all
px_finished:
    push ax
    cmp byte [px_slpost], 0
    je .open
    mov byte [px_slpost], 0
    mov al, PX_R_TB | PX_R_FS | PX_R_STATUS
    call px_regdraw
    jmp short .shown
.open:
    cmp al, 1                       ; an open that ended with its picture
    jne .arm
    cmp byte [px_wres], 0
    jne .arm
.shown:
    cmp byte [px_slon], 0
    je .arm
    call OSAPI_GET_TICKS
    add ax, PX_SLIDE
    mov [px_sldue], ax
.arm:
    call px_tqarm
    pop ax
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
    push ax
    push bx
    mov byte [px_slon], 1
    mov byte [px_slmem], 0
    call OSAPI_GET_TICKS
    add ax, PX_SLIDE
    mov [px_sldue], ax
    call px_flags                   ; the button latched, the item "Stop"
    call px_update
    call px_tqarm                   ; (the timer's pace: the slideshow's now)
    pop bx
    pop ax
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
    call px_tqarm
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
    mov dl, 2
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


; =============================================================================
; THE STORE AND A CARD'S KEY (what a paint needs: resident)
; =============================================================================

; px_tstore - CF = 0 the store is here ([px_tseg]); claimed now, when the heap
; keeps PX_THMINFREE KB beside it. Preserves all
px_tstore:
    cmp word [px_tseg], 0
    je .new
    clc
    ret
.new:
    cmp byte [px_tnone], 0
    jne .no
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call OSAPI_MEM_AVAIL            ; AX = the largest run, BX = the total, KB
    cmp ax, PX_TKB
    jb .ref
    sub bx, PX_TKB
    jb .ref
    cmp bx, PX_THMINFREE
    jb .ref
    mov ax, PX_TKB
    call OSAPI_MEM_CLAIM
    jc .ref
    mov [px_tseg], dx
    mov ax, px_treloc
    call OSAPI_MEM_MOVABLE
    mov es, dx                      ; every slot empty
    mov di, PXT_SL0 + PX_TDATA + TK_FLAGS
    mov cx, PX_TSLOTS
.e:
    mov byte [es:di], 0
    add di, PXT_SLSZ
    loop .e
    xor ax, ax
    mov [px_tplok], al
    mov [px_tt1ok], al
    mov [px_cstat], al
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret
.ref:
    mov byte [px_tnone], 1          ; (a new folder asks again)
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
.no:
    stc
    ret

; px_treloc - the store's relocation proc (SPEC.md 66): BX = where it was, DX
; = where it is
px_treloc:
    cmp [px_tseg], bx
    jne .out
    mov [px_tseg], dx
.out:
    ret

; px_tpin - AL = 1 the store pinned for file transfers into it (66.5.7.1),
; 0 movable again. Preserves all, the flags too
px_tpin:
    pushf
    push ax
    push dx
    mov dx, [px_tseg]
    cmp al, 1
    mov ax, 0
    je .set
    mov ax, px_treloc
.set:
    call OSAPI_MEM_MOVABLE
    pop dx
    pop ax
    popf
    ret

; px_tkmake - AX = a name: [px_tkey] := its key - the name, the folder's
; cluster, the file's first cluster, its size, the drive; the rest 0.
; Preserves all
px_tkmake:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    push ax
    mov di, px_tkey
    xor ax, ax
    mov cx, PX_TKSZ / 2
    cld
    rep stosw
    pop ax
    call px_rec                     ; SI = its record
    push si
    mov di, px_tkey + TK_NAME
    mov cx, 12
.n:
    lodsb
    or al, al
    jz .nd
    stosb
    loop .n
.nd:
    pop si
    mov ax, [si + NR_CLUS]
    mov [px_tkey + TK_CLUS], ax
    mov ax, [si + NR_SIZE]
    mov [px_tkey + TK_SIZE], ax
    mov ax, [si + NR_SIZE + 2]
    mov [px_tkey + TK_SIZE + 2], ax
    mov ax, [px_nfdir]
    mov [px_tkey + TK_DIR], ax
    mov al, [px_nfvol]
    mov [px_tkey + TK_VOL], al
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; px_tsfind - AX = a name: CF = 0 and BX = the store's slot holding its
; thumbnail. Preserves all but BX
px_tsfind:
    cmp word [px_tseg], 0
    je .no
    push cx
    push si
    push di
    push es
    call px_tkmake
    mov es, [px_tseg]
    mov di, PXT_SL0 + PX_TDATA
    xor bx, bx
    cld
.s:
    test byte [es:di + TK_FLAGS], TKF_VALID
    jz .n
    push di
    mov si, px_tkey
    mov cx, TK_CMP
    repe cmpsb
    pop di
    je .yes
.n:
    add di, PXT_SLSZ
    inc bx
    cmp bx, PX_TSLOTS
    jb .s
    pop es
    pop di
    pop si
    pop cx
.no:
    stc
    ret
.yes:
    pop es
    pop di
    pop si
    pop cx
    clc
    ret

; px_thkey - AX = a card's name: AX + 2000h when its thumbnail is held and
; this display can draw it - a card's record (SPEC.md 106.15). Preserves all
; but AX
px_thkey:
    push bx
    call px_tsfind
    jc .out
    cmp byte [px_bpp], 1
    jbe .y
    cmp byte [px_tplok], 0          ; (a colour display needs the plans)
    je .out
.y:
    or ax, 0x2000
.out:
    pop bx
    ret

; =============================================================================
; THE FOLDER PART'S VERBS, AND THE PLANS (SPEC.md 106.21)
; =============================================================================

; px_fcall - CL = a verb of the FOLDER part (pxfold.asm): the part fetched,
; called with the store pinned - its transfers land in it - and dropped
; (SPEC.md 106.20). out its CF/AX. A part that cannot be had, or is another
; build's, sets [px_tnone]: no thumbnails until the folder is walked again.
; Lock held. Preserves all but AX
px_fcall:
    push bx
    mov al, 1
    call px_tpin
    mov al, PXPART_FOLD
    mov bl, PXV_DECODE
    call px_pcall
    pushf
    push ax
    xor al, al
    call px_tpin
    mov al, PXPART_FOLD
    call px_pdrop
    pop ax
    popf
    jnc .out
    cmp byte [px_pfail], 0          ; the verb's own refusal
    jne .bad
    cmp ax, PXD_LINK
    jne .no
.bad:
    mov byte [px_tnone], 1
    cmp ax, PXE_PART                ; (op_fetch has said why)
    je .no
    push si
    mov si, px_s_badpart
    call px_toast
    pop si
.no:
    stc
.out:
    pop bx
    ret

; px_thmake - AX = a name: its thumbnail made from [px_cur]'s master (the
; FOLDER part's PF_MAKE); none, and the name is marked, when no slot is free
; or the part is not to be had. Then, on a colour display, the cube's plans.
; Lock held. Preserves all
px_thmake:
    push ax
    push cx
    push si
    mov [px_tname], ax
    mov cl, PF_MAKE
    call px_fcall
    jnc .ok
    mov ax, [px_tname]
    call px_rec
    or byte [si + NR_FLAGS], NRF_NOTH
    jmp short .out
.ok:
    cmp byte [px_bpp], 1
    jbe .out
    call px_tplans
.out:
    pop si
    pop cx
    pop ax
    ret

; px_tplans - the cube's 4bpp plans into the store, by a decoder part's
; PXV_PLANS (the CUBE's shipped plans, 106.20): the part held, else the GIF
; part's, fetched. [px_plan] is banked round it. UI task, lock held.
; Preserves all; [px_tplok] says whether it worked
px_tplans:
    cmp byte [px_tplok], 0
    je .go
    ret
.go:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [px_tseg]
    mov si, px_plan
    mov di, PXT_BUF
    mov cx, 768 / 2
    cld
    rep movsw                       ; [px_plan] banked
    mov bl, [px_cur + PXR_PMODE]
    push bx
    mov byte [px_cur + PXR_PMODE], PM_CUBE
    mov al, [px_kheld]
    or al, al
    jnz .h
    mov al, PXPART_GIF
.h:
    call px_kneed
    push ds
    pop es
    mov bl, PXV_PLANS
    mov di, px_k
    call px_pcall
    pop bx
    mov [px_cur + PXR_PMODE], bl
    jc .back
    mov es, [px_tseg]               ; (a fetch may have moved it)
    mov si, px_plan
    mov di, PXT_PLAN
    mov cx, 768 / 2
    rep movsw
    mov byte [px_tplok], 1
.back:
    mov ax, [px_tseg]
    push ds
    push ds
    pop es
    mov ds, ax
    mov si, PXT_BUF
    mov di, px_plan
    mov cx, 768 / 2
    cld
    rep movsw
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; DRAWING ONE (SPEC.md 106.21): the band at the card's box, the ground round
; it, from px_card (its geometry, [px_fcpair]); layout done, clip armed
; =============================================================================

; px_thcard - AX = the card's name, whose thumbnail the store holds.
; Preserves all
px_thcard:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call px_tsfind
    jnc .have
    jmp .out
.have:
    mov [px_tslot], bx
    call px_slotdi
    mov es, [px_tseg]
    mov al, [es:di + PX_TDATA + TK_TW]
    mov [px_ttw], al
    mov al, [es:di + PX_TDATA + TK_TH]
    mov [px_tth], al
    mov bl, [px_rkind]              ; its rows: th x the pixel's aspect, at
    xor bh, bh                      ; most the box's inside
    shl bx, 1
    xor ah, ah
    mov al, [px_tth]
    mul word [px_aspn + bx]
    div word [px_aspd + bx]
    mov cx, [px_fciy2]
    sub cx, [px_fct]                ; CX = the inside's rows
    cmp ax, cx
    jbe .r1
    mov ax, cx
.r1:
    or ax, ax
    jnz .r2
    inc ax
.r2:
    mov [px_tdh], ax
    sub cx, ax                      ; its first row: centred
    shr cx, 1
    add cx, [px_fct]
    inc cx
    mov [px_tty], cx
    mov al, PX_TW                   ; its left margin in the band
    sub al, [px_ttw]
    shr al, 1
    xor ah, ah
    mov [px_tlp], ax
    mov si, [px_fcx]                ; SI = the band's left: on the grid
    add si, 16
    mov [px_tbandx], si
    mov al, [px_fcpair]             ; THE GROUND round the band (one colour
    mov [px_pc], al                 ; for the four): left of it,
    mov ax, [px_fcix]
    mov bx, [px_fct]
    inc bx
    mov cx, si
    dec cx
    mov dx, [px_fciy2]
    call px_fillc
    mov ax, si                      ; right of it,
    add ax, PX_TW
    mov cx, [px_fcix]
    add cx, PX_BOXW - 3
    call px_fillc
    mov ax, si                      ; over it,
    mov cx, si
    add cx, PX_TW - 1
    mov dx, [px_tty]
    dec dx
    call px_fillc
    mov bx, [px_tty]                ; under it
    add bx, [px_tdh]
    mov dx, [px_fciy2]
    call px_fillc
    ; --- the band: 1bpp bits, or four planes - when the probe says the card
    ; may take them; a block it refuses (a straddle) is the card's ground --
    mov al, [px_fcpair]
    mov [px_tgv], al
    mov byte [px_tdep], 1
    cmp byte [px_bpp], 1
    ja .four
    cmp al, CWHITE                  ; (1bpp: the ground's bit)
    mov byte [px_tgv], 1
    je .form
    mov byte [px_tgv], 0
    jmp short .form
.four:
    mov byte [px_tdep], 4
    mov ax, si
    mov bx, [px_tty]
    mov cx, PX_TW
    mov dx, [px_tdh]
    mov di, 0x8000 | 0x4000 | 1     ; (the probe: nothing drawn)
    call OSAPI_GFX_BLITP
    jnc .form
    add cx, ax                      ; refused: the box's ground there
    dec cx
    add dx, bx
    dec dx
    call px_fillc
    jmp short .out
.form:
    mov ax, PX_TW / 8               ; (a plane's bytes)
    mul word [px_tdh]
    mov [px_tsi], ax
    xor bp, bp                      ; BP = the band's row
.r:
    cmp bp, [px_tdh]
    jae .blit
    call px_throw
    call px_thpack
    inc bp
    jmp short .r
.blit:
    mov di, [px_tsi]
    or di, 0x4000                   ; (walk an armed clip, SPEC.md 5.4.3.6)
    mov es, [px_tseg]
    mov ax, [px_tbandx]
    mov bx, [px_tty]
    mov cx, PX_TW
    mov dx, [px_tdh]
    mov si, PXT_BUF
    mov bp, PX_TW / 8
    cmp byte [px_tdep], 1
    jne .b4
    push ax
    mov ax, (CBLACK << 8) | CWHITE  ; a set bit lit, a clear one dark
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    call OSAPI_GFX_BLIT1
    jmp short .out
.b4:
    call OSAPI_GFX_BLITP
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

; px_slotdi - BX = a slot: DI = its first byte in the store. Preserves all
; but DI
px_slotdi:
    push ax
    push dx
    mov ax, PXT_SLSZ
    mul bx
    add ax, PXT_SL0
    mov di, ax
    pop dx
    pop ax
    ret

; px_throw - band row BP's PX_TW values at the store's PXT_ROW: the ground,
; the thumbnail's row (BP x th / dh) through the cube's plans or thresholds
; against the 8x8 Bayer, the ground. Clobbers AX, BX, CX, DX, DI
px_throw:
    push si
    push es
    mov es, [px_tseg]
    cld
    mov ax, bp                      ; the thumbnail's row...
    mov cl, [px_tth]
    xor ch, ch
    mul cx
    div word [px_tdh]
    mov cl, [px_ttw]
    mul cx
    mov bx, [px_tslot]
    call px_slotdi
    add ax, di
    mov si, ax                      ; ...at ES:SI
    mov ax, [px_tty]                ; the Bayer's row: the screen's
    add ax, bp
    and ax, 7
    mov cl, 3
    shl ax, cl
    add ax, px_bayer
    mov bx, ax
    mov di, PXT_ROW
    xor cx, cx                      ; CX = the column
    mov dx, [px_tlp]
    call px_thgnd
    mov dl, [px_ttw]
    xor dh, dh
.s:
    mov al, [es:si]
    inc si
    call px_thpix
    stosb
    inc cx
    dec dx
    jnz .s
    mov dx, PX_TW
    sub dx, cx
    call px_thgnd
    pop es
    pop si
    ret

; px_thgnd - DX values of the ground at ES:DI, CX advanced by them
px_thgnd:
    or dx, dx
    jz .o
    add cx, dx
    push cx
    mov cx, dx
    mov al, [px_tgv]
    rep stosb
    pop cx
.o:
    ret

; px_thpix - AL = a cube index at column CX, BX = the Bayer's row: AL = its
; colour (4bpp: c2 below the plan's threshold, else c1 - px_texpand's rule)
; or its bit (1bpp: lit below the threshold). ES = the store. Preserves all
; but AX
px_thpix:
    push bx
    push dx
    mov dx, cx
    and dx, 7
    add bx, dx
    mov dl, [bx]                    ; DL = the cell's Bayer value
    xor ah, ah
    mov bx, ax
    cmp byte [px_tdep], 1
    jne .four
    cmp dl, [es:PXT_T1 + bx]
    mov al, 1
    jb .o
    xor al, al
    jmp short .o
.four:
    shl bx, 1
    add bx, ax                      ; BX = the plan: c1, c2, t
    cmp dl, [es:PXT_PLAN + 2 + bx]
    mov al, [es:PXT_PLAN + 1 + bx]
    jb .o
    mov al, [es:PXT_PLAN + bx]
.o:
    pop dx
    pop bx
    ret

; px_thpack - PXT_ROW's values into band row BP in its form: 1bpp bits, or
; four planes (plane k at k x the plane's bytes). Clobbers AX, BX, CX, DX,
; SI, DI
px_thpack:
    push es
    mov es, [px_tseg]
    mov si, PXT_ROW
    mov ax, PX_TW / 8
    mul bp
    add ax, PXT_BUF
    mov di, ax
    mov cx, PX_TW / 8
.g:
    push cx
    mov dx, 8
.p:
    mov al, [es:si]
    inc si
    shr al, 1
    rcl bl, 1
    shr al, 1
    rcl bh, 1
    shr al, 1
    rcl cl, 1
    shr al, 1
    rcl ch, 1
    dec dx
    jnz .p
    mov [es:di], bl                 ; plane 0 (on 1bpp, the band itself)
    cmp byte [px_tdep], 1
    je .nx
    mov ax, [px_tsi]
    push di
    add di, ax
    mov [es:di], bh
    add di, ax
    mov [es:di], cl
    add di, ax
    mov [es:di], ch
    pop di
.nx:
    inc di
    pop cx
    loop .g
    pop es
    ret

; =============================================================================
; THE ENGINE (SPEC.md 106.21): one step a timer call, in idle time
; =============================================================================

; px_thstep - when nothing else is going on and the user has been still
; PX_THIDLE ticks: for the cards on show that lack one, the cache read (once
; a folder, once a strip's window), the open picture's made from its master,
; else the next card's hidden decode begun; with none lacking, the cache
; written and the timer slowed. Lock held. Preserves all
px_thstep:
    push ax
    push bx
    push cx
    push dx
    push si
    cmp byte [px_thoff], 0
    je .t0
    jmp .idle
.t0:
    cmp byte [px_hmode], 0
    jne .out1
    cmp byte [px_busy], 0
    jne .out1
    cmp byte [px_job], JOB_NONE
    jne .out1
    cmp byte [px_slon], 0           ; A SLIDESHOW: only the open picture's,
    jne .lay                        ; from its master (its decodes are slides)
    cmp byte [px_press], PX_PR_NONE
    jne .out1
    cmp byte [px_hdrop + OS88UI_DR_OPEN], 0
    jne .out1
    call os88ui_armed               ; (a button held down)
    or ax, ax
    jnz .out1
    call OSAPI_GET_TICKS
    sub ax, [px_lastact]
    cmp ax, PX_THIDLE
    jb .out1
.lay:
    call px_layout                  ; (not visible - minimised: idle until
    jc .idle                        ; a paint arms it again)
    cmp byte [px_fson], 0
    je .flush
    cmp byte [px_tnone], 0
    jne .idle
    call px_thpick                  ; a card on show lacks its thumbnail?
    jc .flush
    call px_tstore
    jc .idle
    cmp byte [px_slon], 0
    jne .cur
    cmp byte [px_cstat], 2          ; THE CACHE: read for this folder and this
    je .cur                         ; window of the strip, once
    mov ax, [px_nsum]
    cmp ax, [px_cvsum]
    jne .visit
    mov ax, [px_fcs]
    cmp ax, [px_cvfcs]
    jne .visit
    mov ax, [px_fcn]
    cmp ax, [px_cvfcn]
    je .cur
.visit:
    mov ax, [px_nsum]
    mov [px_cvsum], ax
    mov ax, [px_fcs]
    mov [px_cvfcs], ax
    mov ax, [px_fcn]
    mov [px_cvfcn], ax
    mov cl, PF_VISIT
    call px_fcall
    jc .out1
    or ax, ax
    jz .cur
    jmp short .drew
.out1:
    jmp .out
.cur:
    mov ax, [px_ncur]               ; THE OPEN PICTURE'S, from its master
    cmp ax, [px_nnames]
    jae .dec
    cmp byte [px_cur + PXR_HAVE], 0
    je .dec
    call px_thwant
    jc .dec
    call px_thmake
    jmp short .drew
.dec:
    cmp byte [px_slon], 0
    jne .out1
    call px_thpick                  ; the next card's: a hidden decode
    jc .flush
    call px_nfgo
    jc .idle
    mov dl, 1
    call px_hopen
    jnc .out
    mov [px_thref], al              ; (the refusal, for a test)
    mov ax, [px_hidx]               ; refused before the worker: marked
    call px_rec
    or byte [si + NR_FLAGS], NRF_NOTH
    jmp short .out
.drew:
    cmp byte [px_bpp], 1
    jbe .dr
    call px_tplans                  ; (a colour display draws by the plans)
    call px_kkeep                   ; (and keeps no part the picture is not)
.dr:
    call px_update
    jmp short .out
.flush:
    cmp byte [px_slon], 0           ; (not mid-slideshow: mid-browse)
    jne .out1
    cmp byte [px_cnew], 0           ; THE CACHE WRITTEN, the strip idle
    je .idle
    cmp byte [px_cstat], 1
    jne .idle
    cmp byte [px_cnowr], 0
    jne .idle
    mov cl, PF_WRITE
    call px_fcall
    jnc .idle
    cmp ax, 1                       ; the volume refused: said once, and not
    jne .idle                       ; tried again this session
    mov byte [px_cnowr], 1
    mov si, px_s_nothc
    call px_toast
.idle:
    mov byte [px_tq], 0
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; px_thpick - CF = 0 and AX = the card that lacks its thumbnail next: the
; open picture's first when it is on show (else the strip's middle), then
; outward, right before left. Preserves all but AX
px_thpick:
    push cx
    push dx
    mov dx, [px_ncur]
    mov ax, [px_fcs]
    cmp dx, ax
    jb .mid
    add ax, [px_fcn]
    cmp dx, ax
    jb .c
.mid:
    mov dx, [px_fcn]
    shr dx, 1
    add dx, [px_fcs]
.c:
    xor cx, cx                      ; CX = the distance
.d:
    mov ax, dx
    add ax, cx
    call px_thwant
    jnc .yes
    jcxz .nx
    mov ax, dx
    sub ax, cx
    call px_thwant
    jnc .yes
.nx:
    inc cx
    cmp cx, [px_fcn]
    jbe .d
    stc
    jmp short .out
.yes:
    clc
.out:
    pop dx
    pop cx
    ret

; px_thwant - AX = a name: CF = 0 when its card is on show, it has no
; thumbnail and none was refused. Preserves all
px_thwant:
    cmp ax, [px_fcs]
    jl .no
    push bx
    push si
    mov bx, [px_fcs]
    add bx, [px_fcn]
    cmp ax, bx
    jge .no1
    cmp ax, [px_nnames]
    jge .no1
    call px_rec
    test byte [si + NR_FLAGS], NRF_NOTH
    jnz .no1
    call px_tsfind
    jnc .no1
    pop si
    pop bx
    clc
    ret
.no1:
    pop si
    pop bx
.no:
    stc
    ret
