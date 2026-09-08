; =============================================================================
; os8088 - apps/frotz/zio.inc
;
; Input, the request handshake, and saved games. This is the seam between the
; two tasks Frotz runs on, and the only file in the package that knows both
; sides of it (docs/plans/completed/FROTZ-PLAN.md 4.1, SPEC.md 61.7).
;
; THE TASK SPLIT IS THE WHOLE DESIGN. The VM is a worker (SPEC.md 20.6), and a
; worker may not call a file slot, may not call OSAPI_FILE_DLG, and may not
; call OSAPI_MEM_*. None of that is enforced - a file slot from a worker
; cross-links the mounted FAT silently - so the split is kept here, by
; construction, rather than remembered at each call site:
;
;   UI TASK ONLY   zi_open_dlg, zi_ondlg (the completion proc), zi_load,
;                  zi_service, zi_putkey
;   WORKER ONLY    zi_getkey, zi_readline, zi_readchar, zi_save, zi_restore,
;                  zi_save_undo, zi_restore_undo, zi_yield
;
; Every claim Frotz will ever hold is made by zi_load, on the UI task, before
; the worker has anything to run: the story, the Z-stack, the save staging
; buffer and the undo snapshot. The worker allocates nothing, ever.
;
; The handshake (docs/plans/completed/FROTZ-PLAN.md 4.1) is four steps and one shared byte:
;
;   1. the worker stages the whole save image into [zi_savseg] itself - a
;      memory copy, which is allowed - sets [zf_req], prints a prompt and
;      waits on the ordinary input path;
;   2. the user presses RETURN, which is a KEY CALLBACK on the UI task;
;      zf_onkey calls zi_service, which raises OSAPI_FILE_DLG;
;   3. the dialog's completion proc - still the UI task - does the
;      OSAPI_FILE_WRITE, writes [zf_reqres], and clears [zf_req] LAST,
;      because the worker is watching that byte and nothing else;
;   4. the worker wakes and takes the branch @save owes the story.
;
; Cancelling the dialog calls nothing at all (SPEC.md 38.6), so a cancel
; simply leaves [zf_req] set and the worker waiting: the next RETURN raises
; the dialog again. That is deliberate - it costs one keypress and needs no
; timeout, and a timeout is the thing that would make @save answer "failed"
; while the dialog is still on screen.
;
; SAVED GAMES ARE QUETZAL (the IFF 'FORM....IFZS' standard), because a save
; written on an XT then opens in Frotz on a laptop and one written there opens
; here. Dynamic memory goes in as UMem - an uncompressed dump - and NOT as
; CMem, and that is a memory decision taken in docs/plans/completed/FROTZ-PLAN.md 8 rather
; than a shortcut: CMem is a run-length encoding of current dynamic memory
; XORed against the ORIGINAL, so it needs a pristine copy resident, and
; FROTZ-PLAN 3 has already spent that second copy on not existing (a restart
; re-reads the story off the floppy instead). UMem is a legal Quetzal
; alternative every interpreter reads; it costs disk, which the story disks
; have, rather than RAM, which they do not.
;
;   WHERE THE PRISTINE COPY IS: nowhere. There is no resident original. That
;   is why this module writes UMem and REFUSES a CMem save it is handed, with
;   the reason on screen (SPEC.md 47) instead of a wrong answer. Reading CMem
;   becomes possible the day the UI task re-reads the story's first
;   [zf_static] bytes with OSAPI_FILE_READ_AT into the tail of the staging
;   claim; that is a UI-task job and it is not done here.
;
; THE Z-STACK FRAME LAYOUT (ZI_FR_* below) IS DECLARED HERE because Stks is
; the chunk that serialises it and this is the only code that walks the chain
; end to end. zexec.inc builds frames in exactly this shape.
; =============================================================================

; --- what the worker is waiting for; [zi_want], for zwin's input cursor ------
ZIW_NONE     equ 0
ZIW_LINE     equ 1
ZIW_CHAR     equ 2

; --- [zf_reqres]: the UI task's answer to a [zf_req] -------------------------
ZRES_NONE    equ 0
ZRES_OK      equ 1
ZRES_FAIL    equ 2

; --- which dialog is on screen, so one completion proc can serve all three ---
ZID_NONE     equ 0
ZID_STORY    equ 1
ZID_SAVE     equ 2
ZID_RESTORE  equ 3

; --- the Z-stack -------------------------------------------------------------
; 4KB = 2048 words. The Standard's own guarantee (S 6.3.3) is that a game
; using no more than 32 stack entries at a time gets 90 levels of recursion
; out of 1024 words, and "old Infocom games will all run with 512 words"; 2048
; is four times that and still one claim.
ZI_STKKB     equ 16     ; and zexec.inc's ZX_STKBYTES is DERIVED from this,
                        ; because the two disagreed once (4KB claimed, 16KB
                        ; bounds-checked) and the story ran off the end of its
                        ; own claim into the next one
ZI_STKMAX    equ ZI_STKKB * 1024

; A frame, as it sits on the Z-stack. Locals follow the header, then this
; frame's evaluation stack up to the next frame's base (or [zf_sp] for the
; newest). Nothing here is on the 8086 stack: the worker's own stack is 384
; bytes (SCH_STACK) shared with the tick, mouse and sound IRQs.
ZI_FR_PREV   equ 0                  ; word: previous frame's base, 0xFFFF = none
ZI_FR_PCL    equ 2                  ; word: return PC, low 16 bits
ZI_FR_PCH    equ 4                  ; word: return PC, bits 16..19
ZI_FR_NLOC   equ 6                  ; byte: locals in this frame, 0..15
ZI_FR_STORE  equ 7                  ; byte: store variable, 0xFF = discard
ZI_FR_ARGS   equ 8                  ; word: argument-supplied mask, bits 0..6
ZI_FR_SIZE   equ 10

; Dynamic memory ceiling for a save. The staging buffer is addressed as one
; segment plus a 16-bit offset - which is what keeps the whole builder free of
; segment arithmetic - so the image has to fit 64KB. 0xE000 of dynamic memory
; plus the headers plus a full 4KB stack is 61.5KB, and no story ever written
; comes close (Bronze, the largest thing the disks carry, is about 50KB of
; dynamic memory). Past that we refuse with the reason rather than truncate.
ZI_DYNMAX    equ 0E000h

; --- our own bss, out of zio.inc's slack in zbss.inc -------------------------
%define zi_fsz_l      (os88_image_end + ZIO_SLACK + 0)   ; the dialog's DX:CX
%define zi_fsz_h      (os88_image_end + ZIO_SLACK + 2)
%define zi_kb         (os88_image_end + ZIO_SLACK + 4)   ; KB the story needs
%define zi_dlg        (os88_image_end + ZIO_SLACK + 6)   ; byte: ZID_*
%define zi_taddr      (os88_image_end + ZIO_SLACK + 8)   ; readline: text buffer
%define zi_paddr      (os88_image_end + ZIO_SLACK + 10)  ; readline: parse buffer
%define zi_pcl        (os88_image_end + ZIO_SLACK + 12)  ; save/restore PC
%define zi_pch        (os88_image_end + ZIO_SLACK + 14)
%define zi_dyn        (os88_image_end + ZIO_SLACK + 16)  ; dynamic memory bytes
%define zi_cur        (os88_image_end + ZIO_SLACK + 18)  ; frame walk: this frame
%define zi_top        (os88_image_end + ZIO_SLACK + 20)  ; frame walk: its eval end
%define zi_n          (os88_image_end + ZIO_SLACK + 22)  ; frame walk: frame count
%define zi_ebase      (os88_image_end + ZIO_SLACK + 24)  ; frame walk: eval base
%define zi_ecnt       (os88_image_end + ZIO_SLACK + 26)  ; frame walk: eval words
%define zi_fixup      (os88_image_end + ZIO_SLACK + 28)  ; a chunk length's offset
%define zi_umoff      (os88_image_end + ZIO_SLACK + 30)  ; parse: UMem body offset
%define zi_umlen      (os88_image_end + ZIO_SLACK + 32)  ; parse: UMem body length
%define zi_skoff      (os88_image_end + ZIO_SLACK + 34)  ; parse: Stks body offset
%define zi_sklen      (os88_image_end + ZIO_SLACK + 36)  ; parse: Stks body length
%define zi_end        (os88_image_end + ZIO_SLACK + 38)  ; parse: end of the image
%define zi_savname    (os88_image_end + ZIO_SLACK + 40)  ; 13: NAME.SAV, +40..+52

; The document a double-click named (SPEC.md 54.7). OSAPI_ARG_FILE answers a
; name AND the pair that locates it, and the name alone is not enough: the
; entry proc runs while the kernel is standing in the PROGRAM's directory, so
; a story in B:\INFOCOM opened from the root is not found by name. frotz.asm
; banks these two at entry and zi_openpend spends them on OSAPI_FILE_GOTO.
%define zi_argclus    (os88_image_end + ZIO_SLACK + 84)  ; word: its directory
%define zi_argdrv     (os88_image_end + ZIO_SLACK + 86)  ; byte: and its volume
%define zi_arghave    (os88_image_end + ZIO_SLACK + 87)  ; byte: ...and whether
                                                         ; those two mean
                                                         ; anything
; The flag is not redundant with a zero cluster: 0,0 IS a valid locator, the
; root of drive 0, so zeroed bss cannot be read as "nobody set this". And
; [zf_pend] has a second writer - the development harness (zharness.inc) seeds
; it with its own story name and never calls OSAPI_ARG_FILE - so without the
; flag zi_openpend would send that build to A:\ before looking for a story
; that is on B:.

; THE OFFSETS ABOVE +38 ARE NOT CONTIGUOUS AND MUST NOT BE PACKED. Three
; buffers used to start at +40 on top of each other: zi_savname's 13 bytes,
; the scrollback's three words (now +78) and the find record's 24 (now +54).
; Saving a game while a transcript was open wrote the save's file name over
; [zi_scrseg], which zi_script_write then loaded into ES - a 16KB store into
; whatever segment 'MI' happens to be. Anything taken from this slack from
; here on goes at the END of it, and the map is this comment.

; =============================================================================
; THE KEY RING - the one place the two tasks touch without a lock
; =============================================================================
; ZI_RING_N is 32 and a power of two, so the wrap is an AND. The UI task
; writes [zi_rhead] and never reads it back; the worker writes [zi_rtail] and
; never reads it back. Each side reads the other's cursor, and a byte store is
; atomic against an interrupt on an 8086, so there is nothing to lock. A full
; ring DROPS, exactly as the kernel's own event queue does (EVQ_CAP, SPEC.md
; 13) - input overrun is one of PERFORMANCE.md's three emulator-invisible
; defects, so it is a decision taken here and not an accident.
; =============================================================================

; -----------------------------------------------------------------------------
; zi_putkey - UI task: deposit a keystroke for the worker
; in:  AL = ASCII (0 for an extended key), AH = int 16h scan code
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zi_putkey:
    push bx
    push cx
    mov bl, [zi_rhead]
    mov bh, 0
    mov cl, bl
    inc cl
    and cl, ZI_RING_N - 1
    cmp cl, [zi_rtail]
    je .full                        ; one slot is always left empty so that
                                    ; head == tail can only mean "empty"
    mov [zi_ring + bx], al
    mov [zi_rings + bx], ah
    mov [zi_rhead], cl
.full:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zi_getkey - worker: take a keystroke, if there is one
; out: AL = ASCII, AH = scan code, CF = 0; CF = 1 and AX undefined if empty.
;      Preserves every other register.
; -----------------------------------------------------------------------------
zi_getkey:
    push bx
    mov bl, [zi_rtail]
    cmp bl, [zi_rhead]
    je .empty
    mov bh, 0
    mov al, [zi_ring + bx]
    mov ah, [zi_rings + bx]
    inc bl
    and bl, ZI_RING_N - 1
    mov [zi_rtail], bl
    pop bx
    clc
    ret
.empty:
    pop bx
%ifdef ZHARNESS
    jmp zh_getkey                   ; the host's keyboard, polled (zharness.inc)
%else
    stc
    ret
%endif

; -----------------------------------------------------------------------------
; zi_flush - worker: throw away everything queued
; out: nothing; preserves all registers
;
; Called after a file request completes. The RETURN that raised the dialog is
; a handshake keystroke, not something the story asked for, and leaving it in
; the ring makes @save answer and then eat the player's next command.
; -----------------------------------------------------------------------------
zi_flush:
    push ax
    mov al, [zi_rhead]
    mov [zi_rtail], al
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_yield - worker: stay alive, then give up the slice
; out: nothing; preserves all registers. MAY NEVER RETURN.
;
; THE GFX LOCK MUST NOT BE HELD. OSAPI_TASK_ALIVE is the teardown point - if
; the close box was clicked it destroys the window, frees the region and ends
; this task inside the call - and gfx_lock is not reentrant, so calling it
; under the lock deadlocks the whole machine and no watchdog breaks it
; (SPEC.md 20.6 rule 4). Every wait loop in this module goes through here.
; -----------------------------------------------------------------------------
zi_yield:
    push ax
    push bx
    push es                         ; ES IS THE PROGRAM COUNTER'S SEGMENT
                                    ; (docs/plans/completed/FROTZ-PLAN.md 3.1). OSAPI_TASK_ALIVE
                                    ; takes the gfx lock and OSAPI_TASK_YIELD
                                    ; runs the whole scheduler; neither owes us
                                    ; ES, and this is called from EVERY wait
                                    ; loop in the module - so the story got as
                                    ; far as its first prompt and then executed
                                    ; whatever lay at the same offset in some
                                    ; other segment.
    mov ax, [zf_sdelta]             ; ...AND THE STORY CAN MOVE ACROSS THIS
                                    ; (SPEC.md 66.5.9). OSAPI_TASK_ALIVE is
                                    ; the heap compactor's park point, so this
                                    ; is the one place in the interpreter
                                    ; where a story-derived segment is on a
                                    ; stack while the block under it is
                                    ; eligible to slide. Bank the running
                                    ; shift, difference it after, and add
                                    ; what the compactor moved
    mov bx, [zf_win]
    call OSAPI_TASK_ALIVE           ; never returns if we were closed
    call OSAPI_TASK_YIELD
    mov bx, [zf_sdelta]
    sub bx, ax                      ; BX = paragraphs the story moved by, 0 in
    pop es                          ; every ordinary pass - so the whole of
    jnz .moved                      ; this costs one word and a compare
.out:
    pop bx
    pop ax
    ret

    ; IT MOVED. Shift the saved ES only if it POINTED INTO THE STORY, and
    ; decide that by range rather than by "was it the PC's segment": the
    ; @verify walk holds a story segment that is not the PC, and a caller
    ; holding something else entirely must not have a delta added to it.
.moved:
    push ax
    push cx
    mov ax, es
    mov cx, [zf_sseg]
    sub cx, bx                      ; CX = where the story was when ES was
    sub ax, cx                      ; saved, so AX = paragraphs into it
    jb .nofix                       ; below the claim: never ours
    push dx
    mov dx, [zf_skb]
    mov cl, 6
    shl dx, cl                      ; DX = the claim in paragraphs; 508KB is
    cmp ax, dx                      ; 32,512, so this cannot overflow a word
    pop dx
    jae .nofix                      ; past its end: not ours either
    mov ax, es
    add ax, bx
    mov es, ax
.nofix:
    pop cx
    pop ax
    jmp short .out

; =============================================================================
; SMALL ACCESSORS - the only places this module forms an address by hand
; =============================================================================

; -----------------------------------------------------------------------------
; zi_zsw / zi_zsp - read and write a word on the Z-stack
; in:  BX = a BYTE offset into the claim at [zf_stkseg]; zi_zsp also AX = value
; out: zi_zsw: AX = the word. Both preserve every other register.
;
; The Z-stack is native little-endian - it is our data structure, not the
; story's - so this is a plain word access and NOT zmem's business. DS has to
; move to reach it and DS is where [zf_stkseg] lives, so the segment is loaded
; into a register first.
; -----------------------------------------------------------------------------
zi_zsw:
    push ds
    push si
    mov si, [zf_stkseg]
    mov ds, si
    mov ax, [bx]
    pop si
    pop ds
    ret

zi_zsp:
    push ds
    push si
    mov si, [zf_stkseg]
    mov ds, si
    mov [bx], ax
    pop si
    pop ds
    ret

; -----------------------------------------------------------------------------
; zi_kbof - bytes to whole KB, rounded up
; in:  AX = a byte count; out: AX = KB. Preserves every other register.
; -----------------------------------------------------------------------------
zi_kbof:
    push cx
    push dx
    xor dx, dx
    add ax, 1023
    adc dx, 0                       ; 32 bits: 0xFC00 bytes would wrap 16
    mov cx, 10
.shift:
    shr dx, 1
    rcr ax, 1
    loop .shift
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; zi_ww / zi_wd - write a big-endian word / dword to the staging buffer
; in:  ES:DI = the cursor, AX (or DX:AX) = the value
; out: DI advanced. zi_ww preserves AX; zi_wd clobbers it.
;
; IFF is big-endian throughout and the 8086 is not, which is the same problem
; zmem.inc owns for story memory - but this is our own container rather than
; the story's, so it is written here rather than borrowed.
; -----------------------------------------------------------------------------
zi_ww:
    xchg al, ah
    stosb                           ; high byte first
    xchg al, ah
    stosb
    ret

zi_wd:
    push ax
    mov al, dh
    stosb
    mov al, dl
    stosb
    pop ax
    call zi_ww
    ret

; -----------------------------------------------------------------------------
; zi_rw / zi_rd - read a big-endian word / dword out of the staging buffer
; in:  ES = [zi_savseg], SI = the cursor
; out: AX (or DX:AX) = the value, SI advanced. Preserves every other register.
; -----------------------------------------------------------------------------
zi_rw:
    mov ah, [es:si]
    mov al, [es:si+1]
    add si, 2
    ret

zi_rd:
    call zi_rw
    mov dx, ax
    call zi_rw
    ret

; -----------------------------------------------------------------------------
; zi_scmp - compare story bytes against staged bytes
; in:  SI = a story byte address < 64K, ES:DI = staged bytes, CX = count
; out: CF = 1 if they differ. SI/DI advanced, CX consumed.
;
; DS moves to the story claim so `repe cmpsb` can do the work in one
; instruction; the story segment is loaded into AX first because [zf_sseg] is
; unreachable once DS has moved.
; -----------------------------------------------------------------------------
zi_scmp:
    push ax
    push ds
    mov ax, [zf_sseg]
    mov ds, ax
    repe cmpsb
    pop ds
    pop ax
    jne .differ
    clc
    ret
.differ:
    stc
    ret

; =============================================================================
; THE UI SIDE
; =============================================================================

; -----------------------------------------------------------------------------
; zi_open_dlg - UI task: raise the Open Story dialog
; in:  nothing; the gfx lock is HELD (menu command or key handler)
; out: nothing; preserves all registers
;
; It does NOT block and it does NOT repaint (SPEC.md 38): the dialog is
; already on screen when OSAPI_FILE_DLG returns, and zi_ondlg runs much later.
; A refusal (one dialog already up, or the window table full) is silent and
; harmless - the user clicks the menu again.
; -----------------------------------------------------------------------------
zi_open_dlg:
    push ax
    push bx
    push si
    push di
    mov byte [zi_dlg], ZID_STORY
    mov bx, [zf_win]
    mov di, zi_ondlg
    xor si, si                      ; no default name
    mov al, FDLG_OPEN
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_service - UI task: act on a request the worker posted
; in:  [zf_req]; the gfx lock is HELD
; out: nothing; preserves all registers
;
; Called from zf_onkey on every keystroke while a story runs, and from the
; File menu. Raising the dialog is ALL it does - the write and the read happen
; in zi_ondlg, because that is where a name exists. [zf_req] is cleared there
; and nowhere else.
; -----------------------------------------------------------------------------
zi_service:
    push ax
    push bx
    push si
    push di
    cmp byte [zf_req], ZREQ_NONE
    je .out
    cmp byte [zf_req], ZREQ_RESTART
    je .restart
    cmp byte [zf_req], ZREQ_SAVE
    jne .restore
    call zi_savename                ; seed Save with STORY.SAV
    mov byte [zi_dlg], ZID_SAVE
    mov si, zi_savname
    mov al, FDLG_SAVE
    jmp short .raise
.restore:
    mov byte [zi_dlg], ZID_RESTORE
    xor si, si
    mov al, FDLG_OPEN
.raise:
    mov bx, [zf_win]
    mov di, zi_ondlg
    call OSAPI_FILE_DLG             ; CF=1 = one is already up; ignore it,
                                    ; the request simply stands
    jmp short .out

    ; @restart. No dialog: the story is the one already open, and the only
    ; thing that has to happen is that dynamic memory goes back to how it was
    ; on disk. There is no resident pristine copy (SPEC.md 61.4), so this is a
    ; re-read - which is a FILE slot, which is why it is here on the UI task
    ; and not in the worker that executed the opcode.
    ;
    ; Only the bytes below [zf_static] are re-read. Static and high memory
    ; cannot have changed: zm_wrok refuses every write at or above it, so the
    ; copy in the claim is still the copy that was loaded.
.restart:
    call zi_reload
    jmp short .out
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_reload - put dynamic memory back the way the file has it (UI TASK)
; in:  nothing; out: [zf_reqres] and [zf_req] answered; preserves all registers
;
; OSAPI_FILE_READ_AT is what makes a resident pristine copy unnecessary: it
; reads a RANGE, so the restart costs one seek and [zf_static] bytes rather
; than a permanent second buffer the size of dynamic memory. On the target
; machine that is one int 13h run - about 400ms - for something the player
; asked for and expects to take a moment.
;
; zm_setup runs again afterwards because the header just came back off disk
; with the story's own bytes in it: the interpreter number, the screen
; dimensions and the Flags 1/2 capability bits all have to be re-asserted, and
; zm_setup is the one routine that owns which those are (Standard 11.1).
; -----------------------------------------------------------------------------
zi_reload:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov byte [zf_reqres], ZRES_FAIL
    call zm_dynlen                  ; AX = [zf_static]
    or ax, ax
    jz .done
    mov cx, ax
    mov es, [zf_sseg]               ; the story claim, at offset 0
    xor bx, bx
    xor dx, dx
    xor ax, ax                      ; DX:AX = read from the start of the file
    mov si, zi_story
    call OSAPI_FILE_READ_AT         ; out CF=0 and DX:AX = bytes delivered
    jc .done
    call zm_setup                   ; re-assert the header (Standard 11.1)
    jc .done
    mov byte [zf_reqres], ZRES_OK
.done:
    mov byte [zf_req], ZREQ_NONE    ; last: the worker is watching this byte
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_restart - @restart's worker half (WORKER)
; out: CF = 0 if dynamic memory was reloaded; preserves all registers
;
; zexec.inc's zxo_restart re-boots the VM itself once this answers, so all
; this owes it is the memory.
; -----------------------------------------------------------------------------
zi_restart:
    push ax
    mov al, ZREQ_RESTART
    call zi_request
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_savename - compose STORY.SAV from the story's own file name
; out: [zi_savname] filled; preserves all registers
; -----------------------------------------------------------------------------
zi_savename:
    push ax
    push cx
    push si
    push di
    mov si, zi_story
    mov di, zi_savname
    mov cx, 8
.stem:
    mov al, [si]
    or al, al
    jz .ext
    cmp al, '.'
    je .ext
    mov [di], al
    inc si
    inc di
    loop .stem
.ext:
    cmp di, zi_savname              ; a nameless story would make a nameless
    jne .dot                        ; save; give it one
    mov word [di], 'ZS'
    add di, 2
.dot:
    mov word [di], '.S'
    mov word [di+2], 'AV'
    mov byte [di+4], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; zi_ondlg - the file dialog's completion proc (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = our window ptr, ES:DI = the chosen name
;      (NUL, <= 12, IN KERNEL_SEG), DX:CX = that file's SIZE IN BYTES (0 = the
;      listing has no such file: a typed Save name, a folder)
; out: nothing; no register need be preserved. UI task, gfx lock HELD.
;
; ONE completion proc for all three dialogs, because the kernel gives us one
; registration per raise and [zi_dlg] already says which raise this was.
; DX:CX is banked FIRST - CX is about to be a copy counter - and the name is
; copied through ES because the buffer is the kernel's and lives for this call
; only (SPEC.md 20.5).
; =============================================================================
zi_ondlg:
    mov [zi_fsz_l], cx
    mov [zi_fsz_h], dx
    mov si, di
    mov di, zi_name
    call zf_copyname                ; ES:SI -> DS:DI, bounded, NUL-terminated
    mov al, [zi_dlg]
    mov byte [zi_dlg], ZID_NONE
    cmp al, ZID_STORY
    je zi_dlg_story
    cmp al, ZID_SAVE
    je zi_dlg_save
    cmp al, ZID_RESTORE
    je zi_dlg_restore
    ret

; -----------------------------------------------------------------------------
; zi_dlg_story - refuse from the SIZE, before any disk I/O
;
; This is SPEC.md 47's refusal path and docs/plans/completed/FROTZ-PLAN.md 3 is why it exists.
; The whole story has to be resident - there is no paging, because int 13h
; costs ~400ms whatever it moves and one turn touches high memory hundreds of
; times - so a story that does not fit cannot be played at all, and the honest
; answer is to say so. It is also FREE to say it here: the dialog read the
; size out of the mount snapshot in RAM, so refusing costs no motor time,
; where refusing after the read would be ten seconds of floppy on an XT.
; -----------------------------------------------------------------------------
zi_dlg_story:
    mov cx, [zi_fsz_l]
    mov dx, [zi_fsz_h]
    mov ax, cx
    or ax, dx
    jz .nofile                      ; 0 means "the listing had no size"
    or dh, dh
    jnz .toobig                     ; >= 16MB: not a story, and not claimable
    add cx, 1023
    adc dx, 0
    mov al, ch                      ; AX = (DX:CX) >> 8 ...
    mov ah, dl
    mov cl, 2
    shr ax, cl                      ; ... >> 2 again = KB, rounded up
    mov [zi_kb], ax
    push ax
    call OSAPI_MEM_AVAIL            ; AX = the LARGEST FREE RUN, not the total
    pop cx
    cmp cx, ax
    ja .toobig
    call zi_load
    ret
.nofile:
    mov si, zi_m_nofile
    jmp short .say
.toobig:
    mov si, zi_m_toobig
.say:
    call zw_notice
    ret

; -----------------------------------------------------------------------------
; zi_dlg_save - the worker staged it; write it
;
; ES is set to the staging claim and BX to 0 - the canonical big transfer
; (SPEC.md 18.4.1: the stub reduces ES:BX to a segment and walks the segment,
; so there is no 64KB ceiling). [zf_req] is cleared LAST, after [zf_reqres],
; because the worker polls [zf_req] and nothing else.
; -----------------------------------------------------------------------------
zi_dlg_save:
    mov byte [zf_reqres], ZRES_FAIL
    cmp word [zi_savseg], 0
    je .done
    push es
    mov es, [zi_savseg]
    xor bx, bx
    mov cx, [zi_savlen_l]
    mov dx, [zi_savlen_h]
    mov si, zi_name
    call OSAPI_FILE_WRITE           ; out CF=0 AX=0, else AX = FERR_*
    pop es
    jc .failed
    mov byte [zf_reqres], ZRES_OK
    jmp short .done
.failed:
    mov si, zi_m_wfail
    call zw_notice
.done:
    mov byte [zf_req], ZREQ_NONE    ; the worker is watching this byte
    ret

; -----------------------------------------------------------------------------
; zi_dlg_restore - read the save into the staging buffer; the worker parses it
;
; The refusal is again taken from the dialog's size rather than from a failed
; read: a save larger than the staging claim cannot be parsed, and saying so
; costs nothing.
; -----------------------------------------------------------------------------
zi_dlg_restore:
    mov byte [zf_reqres], ZRES_FAIL
    cmp word [zi_savseg], 0
    je .done
    mov ax, [zi_savkb]
    mov bx, 1024
    mul bx                          ; DX:AX = the staging capacity in bytes
    cmp [zi_fsz_h], dx
    ja .toobig
    jb .fits
    cmp [zi_fsz_l], ax
    ja .toobig
.fits:
    mov cx, ax
    push es
    mov es, [zi_savseg]
    xor bx, bx
    mov si, zi_name
    call OSAPI_FILE_READ            ; DX:CX = capacity in, DX:AX = size out
    pop es
    jc .failed
    mov [zi_savlen_l], ax
    mov [zi_savlen_h], dx
    mov byte [zf_reqres], ZRES_OK
    jmp short .done
.toobig:
    mov si, zi_m_toobig
    jmp short .say
.failed:
    mov si, zi_m_rfail
.say:
    call zw_notice
.done:
    mov byte [zf_req], ZREQ_NONE
    ret

; =============================================================================
; zi_load - UI task: claim, read and start the story named in [zi_name]
; in:  [zi_name], [zi_kb], [zi_fsz_l]/[zi_fsz_h]; the gfx lock is HELD
; out: CF = 1 refused (with the reason already on screen); preserves nothing
;
; Every claim Frotz will hold is made here, in this order, and the order is
; the point: the story first, because it is the one that can fail for size;
; then the Z-stack, which is small and mandatory; then the save staging
; buffer, whose failure costs saving but not play; then the undo snapshot,
; which docs/plans/completed/FROTZ-PLAN.md 3 says outright is optional - a story that loses
; UNDO on a small machine is better than one that will not start.
; =============================================================================
zi_load:
    call zi_free_all                ; re-opening: hand the old story back
                                    ; BEFORE claiming the new one, or both
                                    ; have to fit at once
    mov byte [zf_state], ZFS_LOAD

    mov ax, [zi_kb]
    call OSAPI_MEM_CLAIM            ; out CF=0 and DX = the base SEGMENT
    jc .nomem
    mov [zf_sseg], dx
    mov ax, [zi_kb]
    mov [zf_skb], ax

    push es
    mov es, dx                      ; ES:BX = the claim, offset 0
    xor bx, bx
    mov cx, [zi_fsz_l]
    mov dx, [zi_fsz_h]              ; DX is an ARGUMENT here and comes back as
    mov si, zi_name                 ; the size's high word
    call OSAPI_FILE_READ
    pop es
    jc .ioerr
    mov [zf_slen_l], ax
    mov [zf_slen_h], dx

    call zm_setup                   ; CF=1 and AL = ZERR_* if unsupported
    jc .badstory

    mov dx, [zf_sseg]           ; ONLY NOW is it movable (SPEC.md 66.2/66.5.9)
    mov ax, zf_reloc            ; - not at the claim. OSAPI_FILE_READ above
    call OSAPI_MEM_MOVABLE      ; holds ES:BX into this block across a call
    mov word [zf_sdelta], 0     ; that itself CLAIMS (18.95's sector cache),
                                ; and zm_setup reads the header through it.
                                ; A reload needs no pin/unpin pair the way the
                                ; editors do (66.5.7.1), because zi_free_all
                                ; above hands the old claim back and a FRESH
                                ; claim starts undeclared

    mov ax, ZI_STKKB
    call OSAPI_MEM_CLAIM
    jc .nomem
    mov [zf_stkseg], dx
    mov ax, zf_reloc
    call OSAPI_MEM_MOVABLE      ; one word: every cursor into it - [zf_sp],
                                ; [zf_fp], every frame's base - is a BYTE
                                ; OFFSET, and zi_zsw/zi_zsp load ES per word
    mov word [zf_sp], 0
    mov word [zf_fp], 0FFFFh        ; no frame yet; zx_run pushes the dummy one

    ; The staging buffer holds a whole Quetzal image: dynamic memory, the
    ; frame chain, and about forty bytes of chunk headers.
    call zm_dynlen                  ; AX = [zf_static]
    mov [zi_dyn], ax
    call zi_kbof
    add ax, ZI_STKKB + 2
    mov [zi_savkb], ax
    call OSAPI_MEM_CLAIM
    jc .nosave
    mov [zi_savseg], dx
    jmp short .undo
.nosave:
    mov word [zi_savkb], 0
    mov si, zi_m_nosave
    call zw_notice

.undo:
    ; @save_undo is allowed to answer "cannot" (Standard 6.1.2), so this claim
    ; is asked for and not insisted on.
    mov ax, [zi_savkb]
    or ax, ax
    jz .win
    call OSAPI_MEM_CLAIM
    jc .noundo
    mov [zi_undoseg], dx
    push ax
    mov ax, zf_reloc            ; one word too, and NOT a file target - a
    call OSAPI_MEM_MOVABLE      ; snapshot is a memory copy (Standard 6.1.2)
    pop ax
    mov ax, [zi_savkb]
    mov [zi_undokb], ax
    jmp short .win
.noundo:
    mov si, zi_m_noundo
    call zw_notice

    ; The transcript buffer, asked for on the same terms as undo: a machine
    ; too small for it still plays the story, it just cannot write SCRIPT.TXT.
    call zi_script_open

.win:
    call zw_setup                   ; geometry + the scrollback claim
    jc .nowin

    ; The story's own name seeds the save name and the title. Retitling costs
    ; the TITLE_H strip and nothing else (SPEC.md 11.92, 43ms measured) - it
    ; is not a repaint and must not be treated as one.
    mov si, zi_name
    mov di, zi_story
    mov cx, 13
    cld
    rep movsb
    mov bx, [zf_win]
    mov ax, zi_name
    call OSAPI_WM_TITLE

    mov byte [zf_state], ZFS_RUN
    call zw_clear
    call zw_paint

    ; THE PICTURES. Last of the claims and after the window is drawn, for two
    ; reasons: it is the only one whose failure costs nothing - @picture_data
    ; is required to be able to answer "unavailable" and stories handle it
    ; (SPEC.md 61.7) - so it must not be able to take the story down with it;
    ; and zp_reason may have something to say, which needs a window to say it
    ; in. It has to happen BEFORE zf_hire: the worker can execute
    ; @picture_data on its first instruction, and an answer of "none" given
    ; because the archive had not been read yet is one a story lays itself out
    ; around and never asks again.
    ;
    ; It needs [zi_story], which is why it is here rather than beside the
    ; other claims above - the archive's name is derived from the story's
    ; (apps/frotz/zpic.inc, zp_mkname).
    call zp_load
    call zp_reason                  ; SI = a fact worth saying, or 0
    or si, si
    jz .nosay
    call zw_notice
.nosay:

    ; HIRE THE WORKER HERE, not only from W_PAINT. A story loaded into a
    ; window that is ALREADY ON SCREEN gets no repaint from the kernel - and
    ; zf_hire lives in zf_paint, so nothing ever started the VM and the story
    ; sat there fully loaded with a blank window and a correct status line.
    ;
    ; It looked story-specific for a while because the other two ways in did
    ; get a paint: a fresh instance is shown by the loader after the entry
    ; proc returns (SPEC.md 21 step 9), and an association used to hand its
    ; name to the first W_PAINT. Only File > Open Story into a live window
    ; missed, which is the ordinary way to open a story - and since SPEC.md
    ; 54.10 the association is the same case, zf_onwake running after that
    ; paint rather than inside it, so this call is now load-bearing twice.
    ;
    ; Safe to call twice - zf_hire is idempotent on [zf_hired] - and safe if
    ; it is refused, because a full task table is transient and every later
    ; repaint retries (SPEC.md 20.6).
    call zf_hire
    clc
    ret

.ioerr:
    mov si, zi_m_ioerr
    jmp short .fail
.badstory:
    mov si, zi_m_bad
    jmp short .fail
.nowin:
    mov si, zi_m_nowin
    jmp short .fail
.nomem:
    mov si, zi_m_nomem
.fail:
    call zw_notice
    call zi_free_all
    mov byte [zf_state], ZFS_NONE
    stc
    ret

; -----------------------------------------------------------------------------
; zf_reloc - the compactor moved one of our claims (SPEC.md 66.5.9)
; in:  BX = the base it WAS at, DX = the base it is at NOW, DS = CS = ours
; out: nothing; preserves every register
;
; THREE claims, and the story is the reason any of this is worth doing: at up
; to 508KB it is the largest block this OS hands out, and it is resident by
; contract (SPEC.md 61.4 - there is no paging, so a story that does not fit is
; REFUSED). A heap that cannot compact around it is a heap where the second
; story of a session does not open.
;
; The story costs three words and not one, and the extra two are the whole
; difficulty §61.3 describes:
;
;   [zf_sseg]    the base itself
;   [zf_pcseg]   THE PROGRAM COUNTER, which is a live segment INTO the story
;                and must be shifted rather than set - the PC is somewhere in
;                the middle of it, not at its start
;   [zf_sdelta]  the running total, which zi_yield differences to fix an ES
;                that was pushed before the move (66.5.9)
;
; What costs NOTHING is everything else the Z-machine holds, and that is a
; property of the design rather than luck: zm_pcaddr turns the live ES:SI back
; into a 20-bit STORY ADDRESS, so every return address in every frame on the
; Z-stack, every object pointer and every packed address is relative to the
; base and survives a move untouched.
;
; The Z-stack and the undo snapshot are one word each. Neither is a file
; target and both are addressed by loading ES immediately before a burst of
; string ops (zi_zsw/zi_zsp do it per WORD), so there is no derived segment
; anywhere to fall out of step.
; -----------------------------------------------------------------------------
zf_reloc:
    cmp bx, [zf_sseg]
    jne .stack
    mov [zf_sseg], dx
    push ax
    mov ax, dx
    sub ax, bx                      ; AX = the paragraph delta
    add [zf_sdelta], ax
    cmp word [zf_pcseg], 0          ; 0 = no PC yet: a story claimed but not
    je .nopc                        ; booted, and zx_boot writes it shortly.
    add [zf_pcseg], ax              ; Adding to 0 would invent an address
.nopc:
    pop ax
    ret
.stack:
    cmp bx, [zf_stkseg]
    jne .undo
    mov [zf_stkseg], dx
    ret
.undo:
    cmp bx, [zi_undoseg]
    jne .out
    mov [zi_undoseg], dx
.out:
    ret

; -----------------------------------------------------------------------------
; zi_free_all - UI task: hand every claim back
; out: nothing; preserves all registers
;
; Not a teardown hook - the kernel frees every claim an instance holds when it
; dies (SPEC.md 50.4) and there is nothing to do there. This exists for the
; ONE case that needs it: opening a second story while the first is loaded, so
; the old one is released before the new one is claimed rather than both
; having to fit at once.
;
; It does not touch zwin's scrollback claim: that is another module's state
; (zbss.inc 4), and zw_setup reuses it.
; -----------------------------------------------------------------------------
zi_free_all:
    push ax
    push dx
    mov dx, [zf_sseg]
    or dx, dx
    jz .nostory
    call OSAPI_MEM_FREE
    mov word [zf_sseg], 0
.nostory:
    mov dx, [zf_stkseg]
    or dx, dx
    jz .nostack
    call OSAPI_MEM_FREE
    mov word [zf_stkseg], 0
.nostack:
    mov dx, [zi_savseg]
    or dx, dx
    jz .nosave
    call OSAPI_MEM_FREE
    mov word [zi_savseg], 0
.nosave:
    mov dx, [zi_undoseg]
    or dx, dx
    jz .noundo
    call OSAPI_MEM_FREE
    mov word [zi_undoseg], 0
.noundo:
    mov byte [zi_undook], 0
    call zp_free                    ; the art goes back with the story that
    pop dx                          ; named it, or opening a second one keeps
    pop ax                          ; the first one's claim for ever
    ret

; =============================================================================
; THE WORKER SIDE: INPUT
; =============================================================================

; -----------------------------------------------------------------------------
; zi_xlate - an extended key to its ZSCII code (Standard 3.8)
; in:  AH = the int 16h scan code
; out: AL = the ZSCII code, or 0 for a key the Z-machine has no name for.
;      Preserves every other register.
;
; The numeric keypad is deliberately absent. NumLock makes it send 4/6/8/2 and
; friends with EXACTLY the scan codes of Left/Right/Up/Down (apps/notepad),
; but with AL non-zero - so those keys never reach here, and a table entry for
; them would only be reachable with NumLock off, where they ARE the arrows.
; -----------------------------------------------------------------------------
zi_xlate:
    push bx
    mov bx, zi_scantab
.loop:
    mov al, [bx]
    or al, al
    jz .none
    cmp al, ah
    je .hit
    inc bx
    inc bx
    jmp short .loop
.hit:
    mov al, [bx+1]
    pop bx
    ret
.none:
    pop bx
    ret

; -----------------------------------------------------------------------------
; zi_isterm - does this ZSCII code terminate a line? (Standard 10.5.2.1)
; in:  AL = a ZSCII code
; out: CF = 1 if it terminates; preserves all registers
;
; Only function key codes may appear in the table - 129..154 plus 252..254 -
; and 255 is the wildcard meaning "any of them". Below v5 there is no table
; and only a carriage return ends a line, which zi_readline handles itself.
; -----------------------------------------------------------------------------
zi_isterm:
    push ax
    push bx
    push cx
    mov cl, al
    cmp byte [zf_ver], 5
    jb .no
    cmp cl, 129
    jb .no                          ; a printable character is never a
                                    ; terminator, whatever the table says
    mov bx, [zf_termtab]
    or bx, bx
    jz .no
.scan:
    mov ax, bx
    call zm_gb16
    or al, al
    jz .no                          ; zero-terminated list
    cmp al, 255
    je .yes                         ; the wildcard
    cmp al, cl
    je .yes
    inc bx
    jmp short .scan
.yes:
    pop cx
    pop bx
    pop ax
    stc
    ret
.no:
    pop cx
    pop bx
    pop ax
    clc
    ret

; =============================================================================
; zi_readchar - worker: @read_char
; out: AL = the ZSCII character; preserves every other register
;
; Blocks, which is free in a worker and would be fatal in a callback. Every
; turn of the wait goes through zi_yield, so the close box still works while a
; story is sitting at a "press any key" prompt.
; =============================================================================
zi_readchar:
    push bx
    push es                         ; the PC's segment; see zi_yield
    mov byte [zi_want], ZIW_CHAR
%ifdef ZHARNESS
    call zh_snap                    ; the windows, BEFORE the marker: the host
    push si                         ; photographs the screen when it sees the
    mov si, zh_m_key                ; marker, and must already hold what the
    call zh_mark                    ; interpreter thinks is on it. A title
    pop si                          ; screen is exactly this moment (BEAR.Z5
%endif                              ; opens on one) - and it is not a command,
                                    ; so the host answers it itself rather than
                                    ; spending a line of the script.
.wait:
    call zi_getkey
    jnc .got
    call zi_yield
    jmp short .wait
.got:
    or al, al
    jnz .done                       ; an ordinary character: ZSCII is ASCII
    call zi_xlate                   ; AH = scan -> AL = ZSCII, 0 = no name
    or al, al
    jz .wait                        ; a key the Z-machine cannot express is
                                    ; not an answer; keep waiting
.done:
    mov byte [zi_want], ZIW_NONE
    pop es
    pop bx
    ret

; =============================================================================
; zi_readline - worker: @read's line editor (Standard 15, 10.5.2)
; in:  AX = the text buffer's story address, BX = the parse buffer's address
;      (0 = do not tokenise), CX = the most characters the story will take
; out: AL = the terminating character; preserves every other register
;
; It echoes through zw_typec rather than drawing itself, so the typed line
; lands in the scrollback and wraps, scrolls and repaints exactly like story
; output - which is the whole reason the scrollback is a stream of characters
; and not a picture of the screen. zw_typec rather than zw_putc because an
; echo has to appear on the keystroke, not on the next space (zwin.inc).
;
; The two buffer layouts differ and the difference is not cosmetic:
;   v1-4   byte 0 = max letters MINUS ONE; the text goes at byte 1 with a
;          ZERO TERMINATOR
;   v5+    byte 0 = max letters; byte 1 = the COUNT actually typed; the text
;          goes at byte 2 with no terminator
; The caller has already turned byte 0 into CX, so this only chooses where the
; bytes land. Text is reduced to lower case in both, which the Standard asks
; for so the story can print the command back tidily.
; =============================================================================
zi_readline:
    push bx
    push cx
    push dx
    push si
    push di
    push es                         ; the PC's segment; see zi_yield
    mov [zi_taddr], ax
    mov [zi_paddr], bx
    or cx, cx
    jnz .havemax
    mov cx, 1
.havemax:
    cmp cx, 250                     ; zi_line is 256; the story does not get
    jbe .capped                     ; to name a bigger number than we have
    mov cx, 250
.capped:
    mov [zi_lmax], cx
    mov word [zi_linen], 0
    mov byte [zi_want], ZIW_LINE
%ifdef ZHARNESS
    call zh_snap                    ; ...and the windows first; see zi_readchar
    push si                         ; the host sends the next line when it sees
    mov si, zh_m_read               ; this, rather than guessing at a delay -
    call zh_mark                    ; which is what makes a scripted run
    pop si                          ; reproducible (apps/frotz/zharness.inc)
%endif

.wait:
    call zi_getkey
    jnc .key
    call zi_yield
    jmp short .wait

.key:
    or al, al
    jz .ext
    cmp al, 13
    je .enter
    cmp al, 8
    je .back
    cmp al, ' '
    jb .wait                        ; a control character we have no use for
    cmp al, 126
    ja .wait
    ; --- an ordinary character -------------------------------------------
    mov bx, [zi_linen]
    cmp bx, [zi_lmax]
    jae .wait                       ; full: refuse silently rather than
                                    ; scroll characters the story cannot take
    cmp al, 'A'
    jb .store
    cmp al, 'Z'
    ja .store
    add al, 32                      ; reduced to lower case (Standard 15)
.store:
    mov [zi_line + bx], al
    inc bx
    mov [zi_linen], bx
    call zw_typec                   ; echo, drawn NOW - not held in the wrap
    jmp short .wait                 ; buffer until a space arrives (zwin.inc)

.back:
    mov bx, [zi_linen]
    or bx, bx
    jz .wait
    dec bx
    mov [zi_linen], bx
    call zw_rubc                    ; zwin removes the last cell of the line
    jmp short .wait

.ext:
    call zi_xlate
    or al, al
    jz .wait
    call zi_isterm
    jnc .wait
    jmp short .fin                  ; AL is the terminating character

.enter:
    mov al, 13
    call zw_newline                 ; "a carriage return is printed, so the
                                    ; cursor moves to the next line"
.fin:
    push ax                         ; bank the terminating character
    call zi_commit
    pop ax
    mov byte [zi_want], ZIW_NONE
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zi_commit - write [zi_line] into the story's text buffer and tokenise
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zi_commit:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [zi_taddr]
    mov cx, [zi_linen]
    cmp byte [zf_ver], 5
    jae .v5
    ; --- v1-4: bytes 1.., zero-terminated --------------------------------
    inc si
    xor bx, bx
.v14:
    cmp bx, cx
    jae .v14end
    mov ax, si
    add ax, bx
    push bx
    mov bl, [zi_line + bx]
    call zm_pb16
    pop bx
    inc bx
    jmp short .v14
.v14end:
    mov ax, si
    add ax, cx
    xor bl, bl
    call zm_pb16                    ; the terminator the story looks for
    jmp short .parse
    ; --- v5+: byte 1 = the count, bytes 2.. = the text --------------------
.v5:
    mov ax, si
    inc ax
    mov bl, cl
    call zm_pb16
    add si, 2
    xor bx, bx
.v5loop:
    cmp bx, cx
    jae .parse
    mov ax, si
    add ax, bx
    push bx
    mov bl, [zi_line + bx]
    call zm_pb16
    pop bx
    inc bx
    jmp short .v5loop

.parse:
    mov bx, [zi_paddr]
    or bx, bx
    jz .out                         ; v5+ with parse-buffer 0: no lexing
    mov ax, [zi_taddr]
    xor cx, cx                      ; the story's own dictionary
    xor dl, dl
    call zd_tokenise
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE WORKER SIDE: SAVED GAMES
; =============================================================================

; -----------------------------------------------------------------------------
; zi_request - post [zf_req] and wait for the UI task to answer it
; in:  AL = ZREQ_*
; out: CF = 0 if the UI task answered ZRES_OK; preserves all registers
;
; The prompt is the whole user interface of this: a worker cannot raise a
; dialog, so it asks for a keypress it can see, and the key callback that
; delivers the keypress is the thing that raises the dialog. Cancelling means
; pressing RETURN again.
; -----------------------------------------------------------------------------
zi_request:
    push ax
    push si
    mov byte [zf_reqres], ZRES_NONE
    mov [zf_req], al                ; LAST, and after reqres: the UI task
                                    ; reads this byte to decide there is work
    mov si, zi_m_press
    call zw_notice
.wait:
    call zi_yield                   ; the only way out if the window closes
    cmp byte [zf_req], ZREQ_NONE
    jne .wait
    call zi_flush                   ; the RETURN was a handshake, not input
    cmp byte [zf_reqres], ZRES_OK
    pop si
    pop ax
    jne .failed
    clc
    ret
.failed:
    stc
    ret

; =============================================================================
; zi_save - worker: @save
; out: CF = 0 saved; preserves every other register
;
; Stages a whole Quetzal image into [zi_savseg] - a memory copy, which a
; worker may do - then posts ZREQ_SAVE and waits. The UI task writes the file.
;
; The container is:
;     'FORM' <len> 'IFZS'
;       'IFhd' 13   release(2) serial(6) checksum(2) pc(3) + a pad byte
;       'UMem' n    dynamic memory, uncompressed
;       'Stks' n    the frame chain, oldest frame first
; Chunk bodies are padded to an even length and the pad is NOT counted in the
; length word, which is IFF's rule and the one thing every reader checks.
;
; Release, serial and checksum are copied out of the story header BYTE FOR
; BYTE, because the header is already big-endian and so is IFF - there is no
; conversion to get wrong. The PC is the address of the @save opcode's own
; branch/store byte, so zexec.inc must call this with the PC sitting there.
; =============================================================================
zi_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld

    cmp word [zi_savseg], 0
    je .refuse
    mov ax, [zf_static]
    cmp ax, ZI_DYNMAX
    ja .toobig
    mov [zi_dyn], ax

    call zm_pcaddr                  ; DX:AX = the live ES:SI as a story address
    mov [zi_pcl], ax                ; taken FIRST, while ES is still the story
    mov [zi_pch], dx

    mov es, [zi_savseg]
    xor di, di

    ; --- FORM ------------------------------------------------------------
    mov si, zi_id_form
    mov cx, 4
    rep movsb
    mov [zi_fixup], di              ; back-patched once the size is known
    xor ax, ax
    xor dx, dx
    call zi_wd
    mov si, zi_id_ifzs
    mov cx, 4
    rep movsb

    ; --- IFhd ------------------------------------------------------------
    mov si, zi_id_ifhd
    mov cx, 4
    rep movsb
    xor dx, dx
    mov ax, 13
    call zi_wd
    mov si, 2                       ; release number, header $02
    mov cx, 2
    call zi_scpy
    mov si, 12h                     ; serial code, header $12
    mov cx, 6
    call zi_scpy
    mov si, 1Ch                     ; file checksum, header $1C
    mov cx, 2
    call zi_scpy
    mov ax, [zi_pch]
    stosb                           ; the PC, three bytes big-endian
    mov ax, [zi_pcl]
    call zi_ww
    xor al, al
    stosb                           ; 13 is odd, so IFhd always pads

    ; --- UMem ------------------------------------------------------------
    mov si, zi_id_umem
    mov cx, 4
    rep movsb
    xor dx, dx
    mov ax, [zi_dyn]
    call zi_wd
    xor si, si                      ; the whole of dynamic memory, verbatim
    mov cx, [zi_dyn]
    call zi_scpy
    test byte [zi_dyn], 1
    jz .even
    xor al, al
    stosb
.even:

    ; --- Stks ------------------------------------------------------------
    mov si, zi_id_stks
    mov cx, 4
    rep movsb
    mov [zi_fixup+2-2], di          ; (see the patch below; kept in a word)
    push di
    xor ax, ax
    xor dx, dx
    call zi_wd
    call zi_wstks
    pop bx                          ; BX = where the Stks length went
    mov ax, di
    sub ax, bx
    sub ax, 4                       ; the body, excluding its own length word
    push di
    mov di, bx
    xor dx, dx
    call zi_wd
    pop di
    test al, 1
    jz .even2
    xor al, al
    stosb
.even2:

    ; --- back-patch the FORM length --------------------------------------
    mov ax, di
    sub ax, 8                       ; everything after 'FORM' and its length
    mov [zi_savlen_l], di
    mov word [zi_savlen_h], 0
    mov bx, di
    mov di, [zi_fixup]
    xor dx, dx
    call zi_wd
    mov di, bx

    pop es                          ; ES is the story again before anything
    push es                         ; else in this package runs
    call zi_ask_save
    jmp short .out
.toobig:
    mov si, zi_m_bigsave
    jmp short .say
.refuse:
    mov si, zi_m_nosave
.say:
    call zw_notice
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; A separate proc only so the CF that zi_request produces survives the pops.
zi_ask_save:
    mov al, ZREQ_SAVE
    call zi_request
    ret

; -----------------------------------------------------------------------------
; zi_scpy - copy story bytes into the staging buffer
; in:  SI = a story byte address < 64K, CX = count, ES:DI = the cursor
; out: SI/DI advanced, CX consumed; preserves every other register
;
; The one bulk path that bypasses zmem, and legitimately: it moves bytes
; without interpreting them, so there is no word to get the wrong way round.
; Dynamic memory is below [zf_static] and therefore inside one segment by
; construction (Standard 1.1.1), so a single rep movsb reaches all of it.
; -----------------------------------------------------------------------------
zi_scpy:
    push ax
    push ds
    mov ax, [zf_sseg]
    mov ds, ax
    rep movsb
    pop ds
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_wstks - write the Stks chunk body: every frame, OLDEST FIRST
; in:  ES:DI = the cursor
; out: DI advanced; preserves every other register
;
; The frame chain runs newest-to-oldest ([zf_fp] back through ZI_FR_PREV) and
; Quetzal wants the other order, so each frame is reached by walking the chain
; from the head. That is O(n^2) in the frame count, which is the right trade
; here: n is a routine-call depth (tens), this runs once per save against a
; ~400ms disk write, and the alternative is an array of frame offsets on a
; 384-byte worker stack.
;
; A frame's evaluation stack runs from just past its locals to the base of the
; NEXT YOUNGER frame - or to [zf_sp] for the newest - which is why the walk
; wants two positions at a time.
; -----------------------------------------------------------------------------
zi_wstks:
    push ax
    push bx
    push cx
    push si

    xor cx, cx
    mov bx, [zf_fp]
.count:
    cmp bx, 0FFFFh
    je .counted
    inc cx
    add bx, ZI_FR_PREV
    call zi_zsw
    mov bx, ax
    jmp short .count
.counted:
    mov [zi_n], cx

    xor si, si                      ; SI = i, 0 = the oldest frame
.frame:
    cmp si, [zi_n]
    jae .done
    mov cx, [zi_n]
    dec cx
    sub cx, si
    call zi_walk
    mov [zi_cur], bx
    mov ax, [zi_n]
    dec ax
    cmp si, ax
    je .newest
    mov cx, ax
    dec cx
    sub cx, si
    call zi_walk
    mov [zi_top], bx                ; the younger frame's base
    jmp short .emit
.newest:
    mov ax, [zf_sp]
    mov [zi_top], ax
.emit:
    call zi_wframe
    inc si
    jmp short .frame
.done:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_walk - CX steps back along the frame chain from [zf_fp]
; in:  CX = steps; out: BX = the frame's base offset. Clobbers CX.
; -----------------------------------------------------------------------------
zi_walk:
    push ax
    mov bx, [zf_fp]
    jcxz .done
.step:
    add bx, ZI_FR_PREV
    call zi_zsw
    mov bx, ax
    loop .step
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_wframe - one Quetzal stack frame
; in:  [zi_cur] = its base, [zi_top] = the end of its evaluation stack,
;      ES:DI = the cursor
; out: DI advanced; preserves every other register
; -----------------------------------------------------------------------------
zi_wframe:
    push ax
    push bx
    push cx
    push dx

    ; return PC, three bytes big-endian
    mov bx, [zi_cur]
    add bx, ZI_FR_PCH
    call zi_zsw
    stosb
    mov bx, [zi_cur]
    add bx, ZI_FR_PCL
    call zi_zsw
    call zi_ww

    ; flags = locals count, plus bit 4 when the result is discarded
    mov bx, [zi_cur]
    add bx, ZI_FR_NLOC
    call zi_zsw                     ; AL = nlocals, AH = store variable
    mov dl, al
    and al, 0Fh
    cmp ah, 0FFh
    jne .stores
    or al, 10h
    xor ah, ah
.stores:
    stosb
    mov al, ah
    stosb                           ; the variable the result goes into
    mov bx, [zi_cur]
    add bx, ZI_FR_ARGS
    call zi_zsw
    stosb                           ; the argument-supplied mask

    ; how many words of evaluation stack belong to this frame
    mov bl, dl
    xor bh, bh
    mov ax, bx
    shl ax, 1
    add ax, ZI_FR_SIZE
    add ax, [zi_cur]
    mov [zi_ebase], ax
    mov cx, [zi_top]
    sub cx, ax
    shr cx, 1
    mov [zi_ecnt], cx
    mov ax, cx
    call zi_ww

    ; the locals, then the evaluation stack
    mov cx, bx
    mov bx, [zi_cur]
    add bx, ZI_FR_SIZE
    call zi_wwords
    mov cx, [zi_ecnt]
    mov bx, [zi_ebase]
    call zi_wwords

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_wwords - CX words off the Z-stack at BX, big-endian, to ES:DI
; out: DI advanced; clobbers AX, BX, CX
; -----------------------------------------------------------------------------
zi_wwords:
    jcxz .done
.loop:
    call zi_zsw
    call zi_ww
    add bx, 2
    loop .loop
.done:
    ret

; =============================================================================
; zi_restore - worker: @restore
; out: CF = 0 restored, and the new PC is in [zf_pcseg]/[zf_pcoff] for the
;      caller to reload into ES:SI. Preserves every other register.
;
; Posts ZREQ_RESTORE, waits for the UI task to read the file into the staging
; buffer, then parses it. Nothing is written into the machine until every
; chunk has been found and the IFhd has matched - a half-applied restore is
; worse than a refused one, because the story has no way to notice.
;
; The story identity check is release + serial + checksum, which S 6.1.2 calls
; normal, and it is done against the LIVE header before dynamic memory is
; overwritten. Those three fields never change during play.
; =============================================================================
zi_restore:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    cmp word [zi_savseg], 0
    je .refuse
    mov al, ZREQ_RESTORE
    call zi_request
    jc .refuse
    mov es, [zi_savseg]
    call zi_parse
    jc .refuse
    call zi_apply
    jc .refuse
    pop es
    jmp short .ok
.refuse:
    pop es
    stc
    jmp short .out
.ok:
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_parse - walk the IFF container and find the chunks
; in:  ES = [zi_savseg], [zi_savlen_l] = the bytes the UI task read
; out: CF = 1 rejected (with the reason on screen); [zi_umoff]/[zi_umlen],
;      [zi_skoff]/[zi_sklen], [zi_pcl]/[zi_pch] set. Clobbers AX/BX/CX/DX/SI.
;
; A saved game is a file off a floppy anybody can write, so every length is
; treated as hostile: a chunk that claims to run past the end of the image
; ends the walk instead of addressing memory we were not given.
; -----------------------------------------------------------------------------
zi_parse:
    mov word [zi_umoff], 0
    mov word [zi_umlen], 0
    mov word [zi_skoff], 0
    mov word [zi_sklen], 0
    mov ax, [zi_savlen_l]
    cmp word [zi_savlen_h], 0
    je .havelen
    mov ax, 0FFF0h                  ; > 64K: we can only reach the first
.havelen:
    mov [zi_end], ax
    cmp ax, 24
    jb .bad
    xor si, si
    call zi_rw
    cmp ax, 4946h                   ; 'FO'... no: 'FORM' is 46 4F 52 4D
    jmp short .form0
.form0:
    cmp ax, 464Fh
    jne .bad
    call zi_rw
    cmp ax, 524Dh
    jne .bad
    call zi_rd                      ; the FORM length; the file's own size is
                                    ; the bound we actually trust
    call zi_rw
    cmp ax, 4946h                   ; 'IFZS'
    jne .bad
    call zi_rw
    cmp ax, 5A53h
    jne .bad

.chunk:
    mov ax, si
    add ax, 8
    cmp ax, [zi_end]
    ja .walked
    call zi_rw
    mov bx, ax                      ; BX:CX = the four-character id
    call zi_rw
    mov cx, ax
    call zi_rd                      ; DX:AX = the body length
    or dx, dx
    jnz .walked                     ; a chunk longer than 64K is not ours
    mov [zi_fixup], ax              ; the body length, for the handlers
    mov dx, si                      ; DX = the body's offset
    add ax, si
    jc .walked
    cmp ax, [zi_end]
    ja .walked
    test byte [zi_fixup], 1
    jz .noskip
    inc ax                          ; IFF pads bodies to an even length
.noskip:
    push ax                         ; the next chunk's offset
    cmp bx, 4946h
    jne .notifhd
    cmp cx, 6864h                   ; 'IFhd'
    jne .notifhd
    mov si, dx
    call zi_ifhd
    jc .stop
    jmp short .next
.notifhd:
    cmp cx, 656Dh                   ; ...'Mem'
    jne .notmem
    cmp bx, 554Dh                   ; 'UMem'
    jne .notumem
    mov [zi_umoff], dx
    mov ax, [zi_fixup]
    mov [zi_umlen], ax
    jmp short .next
.notumem:
    cmp bx, 434Dh                   ; 'CMem' - see the banner: there is no
    jne .next                       ; pristine copy to XOR against
    mov si, zi_m_cmem
    call zw_notice
    jmp short .stop
.notmem:
    cmp bx, 5374h                   ; 'Stks'
    jne .next
    cmp cx, 6B73h
    jne .next
    mov [zi_skoff], dx
    mov ax, [zi_fixup]
    mov [zi_sklen], ax
.next:
    pop si
    jmp .chunk                      ; near: the chunk walk grew past 128 bytes
.stop:
    pop ax
    stc
    ret

.walked:
    cmp word [zi_umoff], 0
    je .bad
    clc
    ret
.bad:
    mov si, zi_m_notsave
    call zw_notice
    stc
    ret

; -----------------------------------------------------------------------------
; zi_ifhd - check the header chunk and take the PC out of it
; in:  ES = staging, SI = the body's offset, [zi_fixup] = its length
; out: CF = 1 rejected; [zi_pcl]/[zi_pch] set. Clobbers AX/BX/CX/DX/SI/DI.
; -----------------------------------------------------------------------------
zi_ifhd:
    cmp word [zi_fixup], 13
    jb .wrong
    mov di, si
    mov si, 2                       ; release number
    mov cx, 2
    call zi_scmp
    jc .wrong
    mov si, 12h                     ; serial code
    mov cx, 6
    call zi_scmp
    jc .wrong
    mov si, 1Ch                     ; checksum
    mov cx, 2
    call zi_scmp
    jc .wrong
    mov si, di                      ; DI now sits on the three PC bytes
    mov al, [es:si]
    xor ah, ah
    mov [zi_pch], ax
    inc si
    call zi_rw
    mov [zi_pcl], ax
    clc
    ret
.wrong:
    mov si, zi_m_wrong
    call zw_notice
    stc
    ret

; -----------------------------------------------------------------------------
; zi_apply - put the parsed image into the machine
; in:  ES = staging, the chunk offsets from zi_parse
; out: CF = 1 refused; clobbers AX/BX/CX/DX/SI/DI
;
; Dynamic memory first, then the stack, then the PC. zm_setup runs again
; afterwards because the restored bytes carry the story's OWN header, and the
; interpreter fields in it (the Flags 1 capability bits, the screen size, the
; interpreter number) are ours to stamp, not the save file's to dictate.
; -----------------------------------------------------------------------------
zi_apply:
    mov cx, [zi_umlen]
    cmp cx, [zf_static]
    jne .wrong                      ; a UMem body is dynamic memory exactly
    mov si, [zi_umoff]
    xor di, di
    push ds
    push es
    pop ds                          ; DS:SI = the staged image
    mov ax, [zf_sseg - 0]
    pop ds                          ; (restored below; see the reload)
    push es
    mov es, [zf_sseg]               ; ES:DI = dynamic memory
    push ds
    mov ax, [zi_savseg]
    mov ds, ax
    rep movsb
    pop ds
    pop es

    call zi_rstks
    jc .wrong
    call zm_setup                   ; re-cache the header and re-stamp the
    jc .wrong                       ; interpreter's own fields
    mov ax, [zi_pcl]
    mov dx, [zi_pch]
    push es
    call zm_seek                    ; ES:SI = the restored PC
    mov [zf_pcseg], es
    mov [zf_pcoff], si
    pop es
    clc
    ret
.wrong:
    mov si, zi_m_wrong
    call zw_notice
    stc
    ret

; -----------------------------------------------------------------------------
; zi_rstks - rebuild the frame chain from the Stks chunk
; in:  ES = staging, [zi_skoff]/[zi_sklen]
; out: CF = 1 refused; [zf_sp]/[zf_fp]/[zf_nlocals] set.
;      Clobbers AX/BX/CX/DX/SI/DI.
;
; Frames arrive oldest first, which is the order they are built in, so the
; PREV link is just "the frame before this one". Every push is bounded against
; ZI_STKMAX: the file came off a floppy and a frame count is not a promise.
; -----------------------------------------------------------------------------
zi_rstks:
    mov si, [zi_skoff]
    mov ax, si
    add ax, [zi_sklen]
    mov [zi_end], ax
    mov word [zf_sp], 0
    mov word [zf_fp], 0FFFFh
    mov byte [zf_nlocals], 0
    mov word [zi_cur], 0FFFFh       ; the previous frame's base
.frame:
    mov ax, si
    add ax, 8
    cmp ax, [zi_end]
    ja .done
    mov di, [zf_sp]
    mov ax, di
    add ax, ZI_FR_SIZE + 2*15
    cmp ax, ZI_STKMAX
    jae .over
    ; return PC, three bytes big-endian
    mov al, [es:si]
    xor ah, ah
    inc si
    mov bx, di
    add bx, ZI_FR_PCH
    call zi_zsp
    call zi_rw
    mov bx, di
    add bx, ZI_FR_PCL
    call zi_zsp
    ; flags, store variable, argument mask
    mov al, [es:si]
    inc si
    mov ah, [es:si]
    inc si
    mov dl, al
    and al, 0Fh                     ; locals count
    test dl, 10h
    jz .stores
    mov ah, 0FFh                    ; the result is discarded
.stores:
    mov [zf_nlocals], al
    mov bx, di
    add bx, ZI_FR_NLOC
    push ax
    call zi_zsp
    pop ax
    mov cl, al
    xor ch, ch                      ; CX = locals
    mov al, [es:si]
    inc si
    xor ah, ah
    mov bx, di
    add bx, ZI_FR_ARGS
    call zi_zsp
    ; the link to the frame before this one
    mov ax, [zi_cur]
    mov bx, di
    call zi_zsp
    mov [zi_cur], di
    mov [zf_fp], di
    ; locals, then the evaluation stack
    mov bx, di
    add bx, ZI_FR_SIZE
    call zi_rwords
    jc .over
    call zi_rw                      ; the evaluation-stack word count
    mov cx, ax
    call zi_rwords
    jc .over
    mov [zf_sp], bx
    jmp .frame                      ; near: the frame walk is over 128 bytes
.done:
    cmp word [zf_fp], 0FFFFh
    je .empty
    clc
    ret
.empty:
.over:
    stc
    ret

; -----------------------------------------------------------------------------
; zi_rwords - CX big-endian words from the staging buffer onto the Z-stack
; in:  ES = staging, SI = the cursor, BX = the Z-stack byte offset
; out: BX past the last word, SI advanced, CF = 1 if the stack would overflow.
;      Clobbers AX, CX.
; -----------------------------------------------------------------------------
zi_rwords:
    jcxz .done
.loop:
    cmp bx, ZI_STKMAX - 2
    jae .over
    mov ax, si
    add ax, 2
    cmp ax, [zi_end]
    ja .over
    call zi_rw
    call zi_zsp
    add bx, 2
    loop .loop
.done:
    clc
    ret
.over:
    stc
    ret

; =============================================================================
; UNDO - the same state, with no file anywhere near it
; =============================================================================
; @save_undo needs no dialog, no task hop and no disk: it is dynamic memory
; plus the Z-stack, copied into a claim. The claim is optional
; (docs/plans/completed/FROTZ-PLAN.md 3), so both of these answer "cannot" rather than
; assuming, which is exactly what Standard 6.1.2 allows @save_undo to do.
;
; The snapshot's layout, at [zi_undoseg]:0000:
;     +0  word  dynamic memory length      +8  word  PC, bits 16..19
;     +2  word  [zf_sp]                    +10 byte  [zf_nlocals]
;     +4  word  [zf_fp]                    +12       dynamic memory
;     +6  word  PC, low 16 bits            +12+n     the used Z-stack
; =============================================================================

; -----------------------------------------------------------------------------
; zi_save_undo - worker: @save_undo
; out: CF = 0 saved; preserves every other register
; -----------------------------------------------------------------------------
zi_save_undo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    mov byte [zi_undook], 0
    cmp word [zi_undoseg], 0
    je .no
    mov ax, [zf_static]
    cmp ax, ZI_DYNMAX
    ja .no
    call zm_pcaddr                  ; while ES is still the story
    mov [zi_pcl], ax
    mov [zi_pch], dx
    mov es, [zi_undoseg]
    xor di, di
    mov ax, [zf_static]
    stosw
    mov ax, [zf_sp]
    stosw
    mov ax, [zf_fp]
    stosw
    mov ax, [zi_pcl]
    stosw
    mov ax, [zi_pch]
    stosw
    mov al, [zf_nlocals]
    stosb
    xor al, al
    stosb
    xor si, si
    mov cx, [zf_static]
    call zi_scpy                    ; dynamic memory
    mov cx, [zf_sp]
    call zi_stkcpy                  ; the used part of the Z-stack
    mov byte [zi_undook], 1
    pop es
    clc
    jmp short .out
.no:
    pop es
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_restore_undo - worker: @restore_undo
; out: CF = 0 restored, and the PC is in [zf_pcseg]/[zf_pcoff] for the caller
;      to reload into ES:SI. Preserves every other register.
; -----------------------------------------------------------------------------
zi_restore_undo:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cld
    cmp byte [zi_undook], 0
    je .no
    push es
    mov es, [zi_undoseg]
    mov cx, [es:0]
    cmp cx, [zf_static]
    pop es
    jne .no                         ; the story changed underneath it
    mov si, 12
    xor di, di
    push es
    mov es, [zf_sseg]
    call zi_ucpy                    ; the snapshot -> dynamic memory
    pop es
    push es
    mov es, [zi_undoseg]
    mov ax, [es:2]
    mov [zf_sp], ax
    mov cx, ax
    mov ax, [es:4]
    mov [zf_fp], ax
    mov ax, [es:6]
    mov [zi_pcl], ax
    mov ax, [es:8]
    mov [zi_pch], ax
    mov al, [es:10]
    mov [zf_nlocals], al
    pop es
    mov si, 12
    add si, [zf_static]
    call zi_ustk                    ; the snapshot -> the Z-stack
    mov ax, [zi_pcl]
    mov dx, [zi_pch]
    push es
    call zm_seek
    mov [zf_pcseg], es
    mov [zf_pcoff], si
    pop es
    pop es
    clc
    jmp short .out
.no:
    pop es
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_stkcpy - CX bytes of Z-stack -> ES:DI
; zi_ucpy   - CX bytes of the undo claim at SI -> ES:DI
; zi_ustk   - CX bytes of the undo claim at SI -> the Z-stack at offset 0
; All preserve every register except SI/DI/CX, which they consume.
; -----------------------------------------------------------------------------
zi_stkcpy:
    push ax
    push ds
    push si
    mov ax, [zf_stkseg]
    mov ds, ax
    xor si, si
    rep movsb
    pop si
    pop ds
    pop ax
    ret

zi_ucpy:
    push ax
    push ds
    mov ax, [zi_undoseg]
    mov ds, ax
    rep movsb
    pop ds
    pop ax
    ret

zi_ustk:
    push ax
    push es
    push di
    mov ax, [zf_stkseg]
    mov es, ax
    xor di, di
    push ds
    mov ax, [zi_undoseg]
    mov ds, ax
    rep movsb
    pop ds
    pop di
    pop es
    pop ax
    ret

; =============================================================================
; Read-only data
; =============================================================================
zi_id_form:  db 'FORM'
zi_id_ifzs:  db 'IFZS'
zi_id_ifhd:  db 'IFhd'
zi_id_umem:  db 'UMem'
zi_id_stks:  db 'Stks'

; scan code -> ZSCII (Standard 3.8: 129..132 are the cursor keys, 133..144 the
; function keys). A zero scan code ends the table.
zi_scantab:
    db 048h, 129                    ; cursor up
    db 050h, 130                    ; cursor down
    db 04Bh, 131                    ; cursor left
    db 04Dh, 132                    ; cursor right
    db 03Bh, 133                    ; F1..
    db 03Ch, 134
    db 03Dh, 135
    db 03Eh, 136
    db 03Fh, 137
    db 040h, 138
    db 041h, 139
    db 042h, 140
    db 043h, 141
    db 044h, 142                    ; ..F10
    db 057h, 143                    ; F11
    db 058h, 144                    ; F12
    db 0

; Interpreter messages. Every one of them names a FACT - what did not fit,
; what could not be read - because SPEC.md 47 asks for a reason and not a
; shrug, and because a player who is told "story too large for this machine"
; can pick a smaller story.
zi_m_nofile:  db 'That file has no size to read.', 0
zi_m_toobig:  db 'Story too large for this machine.', 0
zi_m_nomem:   db 'Not enough memory for this story.', 0
zi_m_ioerr:   db 'Could not read the story file.', 0
zi_m_bad:     db 'Not a Z-machine story file.', 0
zi_m_nosave:  db 'No room to save; play continues.', 0
zi_m_noundo:  db 'No room for undo; play continues.', 0
zi_m_nowin:   db 'No room for the scrollback.', 0
zi_m_press:   db 'Press RETURN to choose a file.', 0
zi_m_wfail:   db 'Could not write the save file.', 0
zi_m_rfail:   db 'Could not read the save file.', 0
zi_m_bigsave: db 'This story is too large to save.', 0
zi_m_cmem:    db 'Compressed saves cannot be read.', 0
zi_m_wrong:   db 'That save is for another story.', 0
zi_m_notsave: db 'Not a Quetzal saved game.', 0

; =============================================================================
; THE TRANSCRIPT (output stream 2, Standard 7.1.2)
; =============================================================================
; ztext.inc calls zi_script_putc for every character while stream 2 is on, and
; stream 2 is on when the Story > Transcript item is ticked or the story turns
; it on itself with @output_stream 2.
;
; IT IS BUFFERED WHOLE AND WRITTEN WHOLE, and that is forced rather than
; chosen. A worker may not touch a file slot (SPEC.md 20.6), so the write is
; the UI task's; and OSAPI_FILE_APPEND (slot 0x0350) refuses a file whose size
; is not a whole number of clusters, so a transcript cannot be dribbled out a
; line at a time without padding the file with rubbish. OSAPI_FILE_WRITE
; REPLACES, so the only correct thing to replace it with is everything so far.
;
; That makes the buffer the ceiling: ZI_SCRKB of it, and when it fills the
; transcript STOPS and says so on screen rather than silently losing the end.
; A transcript that quietly drops its last thousand lines is worse than one
; that admits where it stopped - it is read afterwards, when nobody is
; watching for the moment it went wrong (SPEC.md 47).
;
; CRLF, because SCRIPT.TXT is meant to be read on a DOS machine or by Note Pad
; and both expect it (SPEC.md 19).
ZI_SCRKB    equ 16                  ; 16,384 characters - about 250 lines of
                                    ; prose, and Note Pad's own ceiling is the
                                    ; same number for the same reason
; +78 and not +40: these sat on top of zi_savname's 13 bytes, and a Save while
; scripting was on overwrote the claim segment this loads into ES. The map is
; the comment beside zi_savname.
%define zi_scrseg  (os88_image_end + ZIO_SLACK + 78)  ; word: the claim, 0 = none
%define zi_scrlen  (os88_image_end + ZIO_SLACK + 80)  ; word: bytes buffered
%define zi_scrfull (os88_image_end + ZIO_SLACK + 82)  ; byte: it filled up

; -----------------------------------------------------------------------------
; zi_script_open - claim the transcript buffer (UI TASK)
; in:  nothing; out: CF=1 refused - which is normal and not fatal
; Called from zi_load beside the other claims, so the worker never allocates.
; -----------------------------------------------------------------------------
zi_script_open:
    push ax
    push dx
    cmp word [zi_scrseg], 0
    jne .ok                         ; idempotent: a second story reuses it
    mov ax, ZI_SCRKB
    call OSAPI_MEM_CLAIM
    jc .no
    mov [zi_scrseg], dx
.ok:
    mov word [zi_scrlen], 0
    mov byte [zi_scrfull], 0
    clc
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_script_putc - one character into the buffer (WORKER)
; in:  AL = the character; out: nothing; preserves every register
;
; ES is the STORY here (docs/plans/completed/FROTZ-PLAN.md 3.1), so it is saved and put back -
; forgetting that is the bug the whole ES rule exists to prevent.
; -----------------------------------------------------------------------------
zi_script_putc:
    push ax
    push bx
    push es
    cmp word [zi_scrseg], 0
    je .out
    cmp byte [zi_scrfull], 0
    jne .out
    mov bx, [zi_scrlen]
    cmp bx, ZI_SCRKB * 1024 - 2     ; -2: a newline may need two bytes
    jae .fill
    mov es, [zi_scrseg]
    cmp al, 13
    jne .plain
    mov byte [es:bx], 13            ; CR LF, for a DOS-readable .TXT
    inc bx
    mov al, 10
.plain:
    mov [es:bx], al
    inc bx
    mov [zi_scrlen], bx
    jmp short .out
.fill:
    mov byte [zi_scrfull], 1
    push si
    mov si, zi_m_scrfull
    call zw_notice                  ; say where it stopped, once
    pop si
.out:
    pop es
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zi_script_flush - write SCRIPT.TXT (UI TASK ONLY)
; in:  nothing; out: nothing; preserves every register
;
; Called from the key callback, so the file on disk is never more than one
; keystroke behind what is on screen - which is what makes it usable as a test
; oracle (docs/plans/completed/FROTZ-PLAN.md 11: the transcript is read back off the image on
; the host and diffed against dfrotz).
; -----------------------------------------------------------------------------
zi_script_flush:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov cx, [zi_scrlen]
    or cx, cx
    jz .out
    cmp word [zi_scrseg], 0
    je .out
    mov es, [zi_scrseg]
    xor bx, bx
    xor dx, dx                      ; DX:CX = the count; DX is an INPUT here
    mov si, zi_scrname
    call OSAPI_FILE_WRITE           ; creates or REPLACES - so it has to be
                                    ; the whole transcript every time
.out:
    pop es                          ; back to KERNEL_SEG for the caller's
    pop si                          ; [es:bx+W_*] reads
    pop dx
    pop cx
    pop bx
    pop ax
    ret

zi_scrname:  db 'SCRIPT.TXT', 0
zi_m_scrfull: db 'Transcript full - it stops here.', 0

; The find record is ours, and the %define has to precede its first USE - a
; %define expands at the use site but NASM still reads this file top to bottom.

; The find record is ours. The %define must precede its first USE: a %define
; expands at the use site, but NASM still reads this file top to bottom.
%define zi_find (os88_image_end + ZIO_SLACK + 54)   ; OSAPI_FIND_SZ = 24, so
                                                    ; +54..+77 - clear of
                                                    ; zi_savname's +40..+52

; -----------------------------------------------------------------------------
; zi_openpend - open the story a double-click named (UI TASK, zf_onwake)
; in:  [zf_pend] = a NUL 8.3 name, the gfx lock HELD; out: nothing; the notice
;      says why on failure
;
; The association path (SPEC.md 54.7) hands over a name and a LOCATOR, while
; the file dialog hands over a name AND the size - and the size is what
; SPEC.md 61.4's refusal is decided from, before the file itself is read. So
; this looks the size up the only way a package can: OSAPI_FILE_FIND.
;
; IT IS NOT FREE, and an earlier version of this comment said it was ("walks
; the mount snapshot, which is in RAM, so the walk costs no motor time").
; There is no such snapshot: names resolve through the RAW DIRECTORY SECTORS
; and so does OSAPI_FILE_FIND (apps/os88api.inc's slot text), which re-seeks
; from the ordinal and so "costs a directory re-read per entry"
; (kernel/disk.inc, dsk_find_x). The refusal rule stands anyway - a directory
; is at most a few hundred entries and the walk is a handful of sector reads
; against a document read of tens of KB - but it is a cheaper read, not a
; free one, and the file-dialog route is the only one that gets the size for
; nothing.
;
; It walks the directory we are STANDING IN, so the locator frotz.asm banked
; is spent first. Nothing here searches for the file - the whole of opening a
; document is copy the name, GOTO, read - and skipping the GOTO is why a story
; in a folder reported itself missing while the same story in the root opened.
;
; Then it joins the dialog's own path at zi_dlg_story, so a story opened by
; double-click gets exactly the same sizing, the same refusal and the same
; message as one opened through File > Open Story. Two ways in, one decision.
; -----------------------------------------------------------------------------
zi_openpend:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; THE GOTO GOES FIRST, before ES is set up, because it is a REMOUNT and
    ; nothing promises what it leaves in ES. CF=1 is the folder no longer being
    ; listable - a disk swapped between the double-click and this first paint -
    ; and searching the wrong directory would report the wrong reason, so it
    ; takes the not-found exit.
    ;
    ; Only when a locator actually came with the name. The harness build sets
    ; [zf_pend] itself and has none, and must go on searching where it already
    ; stands.
    cmp byte [zi_arghave], 0
    je .located
    mov dx, [zi_argclus]
    mov bl, [zi_argdrv]
    call OSAPI_FILE_GOTO
    jc .nofile
.located:

    ; ES SECOND, and this is not a formality. We are called from a callback, so
    ; ES is KERNEL_SEG (SPEC.md 20.5) - and `rep movsb` stores through ES:DI. The
    ; first version of this proc copied the name before setting ES and wrote
    ; thirteen bytes into the KERNEL's segment, which assembles cleanly, runs,
    ; and leaves [zi_name] empty so the search below finds nothing. It is set
    ; here rather than at the top so that the remount above cannot come between
    ; the load and its use.
    push ds
    pop es

    ; zi_dlg_story reads [zi_name], not [zf_pend].
    mov si, zf_pend
    mov di, zi_name
    mov cx, 13
    cld
    rep movsb

    xor cx, cx                      ; ordinal 0 = start of this directory
.scan:
    mov di, zi_find
    call OSAPI_FILE_FIND            ; out CF=1 at the end, else CX = next
    jc .nofile
    push cx
    mov si, zi_find
    mov di, zi_name
    call zi_samename
    pop cx
    jne .scan
    mov ax, [zi_find + 14]          ; the type word
    cmp ax, OSAPI_FT_DIR
    jae .nofile                     ; a folder with the story's name is not it
    mov cx, [zi_find + 18]          ; the size, all 32 bits of it
    mov dx, [zi_find + 20]
    mov [zi_fsz_l], cx
    mov [zi_fsz_h], dx
    push ds
    pop es                          ; zi_dlg_story and zi_load expect ES back
    call zi_dlg_story               ; sizing, refusal and the load, shared
    jmp short .out
.nofile:
    mov si, zi_m_notfound
    call zw_notice
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

zi_m_notfound: db 'That story is not on this disk.', 0

; -----------------------------------------------------------------------------
; zi_samename - compare two NUL 8.3 names, case-insensitively
; in:  SI = one, DI = the other (both in our segment)
; out: ZF=1 if they match; preserves all registers
;
; Case-insensitive because the two sources disagree: os88disk.py uppercases
; every name it writes, but a host OS that rewrote the disk may not have.
; -----------------------------------------------------------------------------
zi_samename:
    push si
    push di
    push ax
    push bx
.loop:
    mov al, [si]
    mov bl, [di]
    cmp al, 'a'
    jb .noa
    cmp al, 'z'
    ja .noa
    sub al, 32
.noa:
    cmp bl, 'a'
    jb .nob
    cmp bl, 'z'
    ja .nob
    sub bl, 32
.nob:
    cmp al, bl
    jne .out                        ; ZF=0
    or al, al
    jz .out                         ; both hit NUL together: ZF=1
    inc si
    inc di
    jmp short .loop
.out:
    pop bx
    pop ax
    pop di
    pop si
    ret


