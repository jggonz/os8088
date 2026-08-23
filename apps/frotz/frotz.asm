; =============================================================================
; os8088 - apps/frotz/frotz.asm
;
; FROTZ, the fifteenth shipped package (SPEC.md 61): an interpreter for
; Infocom's Z-machine, windowed, with its own story floppy in drive B:.
;
; It is NOT a port of David Griffith's Frotz. That is C, and this tree has no
; C toolchain on purpose (nasm -f bin flat binaries, no linker); a port would
; have needed an ia16 cross-compiler and a libc to arrive at a program that
; could not call OSAPI_WM_CREATE without being rewritten around it anyway.
; This is an independent implementation of the Z-Machine Standard 1.1, in
; 8086 assembly, named in the Frotz tradition the way dfrotz and wfrotz are -
; and the About box says so, because a program called Frotz that is not Frotz
; is a misattribution if it does not say so.
;
; What it takes from Frotz is the SHAPE: Windows Frotz is a real windowed
; application with menus, a status line, styles, sound and cover art, not a
; terminal in a box. docs/FROTZ-PLAN.md is the design record.
;
; THE THREE DECISIONS THAT SHAPE EVERYTHING (docs/FROTZ-PLAN.md 3, 4, 5):
;
;  - **The whole story is resident. There is no paging.** int 13h costs ~400ms
;    on the target machine whatever it moves (PERFORMANCE.md), and one turn of
;    a v3 game touches high memory hundreds of times just to decode its text.
;    Paging is not a slower design here, it is a broken one - so Frotz sizes
;    itself from OSAPI_MEM_AVAIL and the directory entry's size, and REFUSES
;    with the reason on screen (SPEC.md 47) rather than shipping something
;    that technically runs. On a 640KB machine everything the disk ships fits;
;    on a 256KB XT the v3 stories fit and the big ones do not.
;
;  - **The VM is a worker task** (SPEC.md 20.6), the same shape as Arkanoid's
;    game loop. A turn is tens of thousands of instructions - seconds on a
;    4.77MHz 8088 - and running it in a key callback would hold the gfx lock
;    for those seconds and freeze the clock, the mouse and every other window.
;    Two worker rules then shape the whole program: the 256-byte stack (so the
;    VM keeps its own stack in a claim and nothing recurses), and no file slot,
;    no OSAPI_FILE_DLG and no OSAPI_MEM_* from a worker - which is what the
;    request handshake in zio.inc exists to answer.
;
;  - **ES is the story while the VM runs.** A story address is up to 20 bits;
;    a claim hands back a base segment, so the PC is a live ES:SI and an
;    instruction fetch is `lods`. Every kernel call from inside the VM saves
;    and restores ES, and every [es:bx+W_*] window read happens with ES put
;    back to KERNEL_SEG. zmem.inc owns this and the big-endian word reads that
;    come with it - nothing else forms a Z-machine word by hand.
;
; Prefix `zf_` for state and UI, `zm_`/`zt_`/`zo_`/`zd_`/`zx_`/`zw_`/`zi_`/
; `zp_`/`zs_` for the modules that own memory, text, objects, dictionary,
; execution, windows, i/o, pictures and sound.
; =============================================================================

%include "os88api.inc"

; SPEC.md 13.10.5's thumb GESTURE. AT THE TOP (13.10.7.4): os88ui.inc is
; included at the very bottom of this file and a %ifdef is answered in FILE
; ORDER.
%ifndef SBDRAGOFF
%define OS88UI_SBDRAG
%ifndef SB_RATE
%define SB_RATE 0               ; RATE 0 (13.10.5.4): a scrollback step
%endif                          ; re-letters rows out of the ring
ZF_SBRATE   equ SB_RATE
%endif

    OS88_HEADER 'FROTZ', zf_entry, 3    ; bit0 = icon, bit1 = association block

; --- the 16x16 icon: a lamp, which is what you are carrying ------------------
    OS88_ICON16
    dw 0x0180                       ; 16 mask rows (white underlay)
    dw 0x03C0
    dw 0x07E0
    dw 0x0FF0
    dw 0x1FF8
    dw 0x3FFC
    dw 0x3FFC
    dw 0x7FFE
    dw 0x7FFE
    dw 0x7FFE
    dw 0x3FFC
    dw 0x1FF8
    dw 0x0FF0
    dw 0x0FF0
    dw 0x1FF8
    dw 0x1FF8
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0180
    dw 0x0240
    dw 0x0420
    dw 0x0810
    dw 0x1008
    dw 0x2004
    dw 0x4002
    dw 0x4812
    dw 0x4002
    dw 0x2004
    dw 0x1008
    dw 0x0810
    dw 0x0810
    dw 0x0FF0
    dw 0x0000
    OS88_ICON16_END

; --- the extensions a story file wears (SPEC.md 54.6) ------------------------
; Five slots, and there are more Z-machine extensions than that: Z3 and Z5
; cover the two the disk is mostly made of, Z8 the big modern ones, DAT is
; what an Infocom disk called its story, and SAV is our own save file so
; double-clicking one opens the interpreter.
    OS88_ASSOC16
        db 5                        ; the count byte is the author's to emit;
                                    ; the macro only reserves the 16 bytes
        OS88_ASSOC_EXT 'Z3'
        OS88_ASSOC_EXT 'Z5'
        OS88_ASSOC_EXT 'Z8'
        OS88_ASSOC_EXT 'DAT'
        OS88_ASSOC_EXT 'SAV'
    OS88_ASSOC16_END

; --- bss first: OS88_BSS takes no forward reference, so ZF_BSS_TOTAL has to
; --- be a number NASM already has (apps/frotz/zbss.inc says why it is %defines)
%include "zbss.inc"

; =============================================================================
; Geometry
; =============================================================================
; Authored against the VGA reference screen (SPEC.md 39): 640x480, dock at
; 440. wm_create clamps the frame onto whatever screen this machine has, and
; everything that reads an edge at RUN time asks OSAPI_VIDEO - so these are
; a starting size, not a promise. 69 columns x 46 rows of the 8x8 font.
ZF_WIN_X    equ 24
ZF_WIN_Y    equ 28
ZF_WIN_W    equ 560                 ; content 558
ZF_WIN_H    equ 396                 ; content 377
ZF_MIN_W    equ 200                 ; below this the text is not worth wrapping
ZF_MIN_H    equ 120

CELL_W      equ 8                   ; the kernel font is 8x8 (SPEC.md 6)
CELL_H      equ 8
ZF_LEAD     equ 9                   ; one pixel of leading; 8 is unreadable in
                                    ; a wall of prose and 10 wastes two rows
ZF_MARGIN   equ 4                   ; content inset, left and right
ZF_SBW      equ 14                  ; scroll bar width, matching Note Pad

; int 16h scan codes, the same set Note Pad names (apps/notepad/notepad.asm).
; PgUp and PgDn belong to the INTERPRETER at all times and are taken before
; the ring, so a story that is reading a character cannot swallow the
; scrollback (SPEC.md 61.5).
KEY_PGUP    equ 0x49
KEY_PGDN    equ 0x51
KEY_UP      equ 0x48
KEY_DOWN    equ 0x50
KEY_LEFT    equ 0x4B
KEY_RIGHT   equ 0x4D
KEY_HOME    equ 0x47
KEY_END     equ 0x4F
KEY_DEL     equ 0x53

; =============================================================================
; The modules
; =============================================================================
; Order matters only for NASM's one-pass-per-section forward references, and
; every module is written to be included in this order. Each owns exactly one
; subject and none of them reaches into another's bss (SPEC.md 61.1).
%include "zmem.inc"                 ; story memory, the header, big-endian words
%include "ztext.inc"                ; Z-string decode, ZSCII, the output stream
%include "zobj.inc"                 ; the object tree, attributes, properties
%include "zdict.inc"                ; the dictionary and the tokeniser
%include "zwin.inc"                 ; v1-v5 windows: wrap, scroll, status line
%include "zwin6.inc"                ; v6: eight windows in pixel coordinates
%include "zpic.inc"                 ; .PIX and Infocom .mg1 pictures
%include "zsnd.inc"                 ; @sound_effect
%include "zio.inc"                  ; input ring, the request handshake, Quetzal
%include "zexec.inc"                ; decode, dispatch, and every opcode
%ifdef ZHARNESS
%include "zharness.inc"             ; DEVELOPMENT ONLY - `make zh`, never a
%endif                              ; shipped disk. See the file's header.

; =============================================================================
; zf_entry - package entry point (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, IF = 1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort)
;
; Creates the window, attaches the menu set and the About handler, and marks
; it sizable - the text re-wraps to whatever width the user drags to, which is
; the whole reason to be a window rather than a screen. It must not show,
; draw, or spawn the worker: the loader publishes the instance only after this
; returns, so OSAPI_TASK_SPAWN would be refused by contract (SPEC.md 20.6).
; The first W_PAINT hires the worker instead.
;
; It MAY claim memory, and it does not: nothing is known about the story yet,
; and a claim sized before the file dialog has named a file would be a guess.
; Every claim Frotz makes is made by zi_load, on the UI task, once the size is
; known from the directory entry.
;
; OSAPI_ARG_FILE is read here because that is the only place it is offered: a
; story double-clicked in the Disk window (SPEC.md 54) arrives as a name, and
; the alternative to taking it now is an interpreter that opens empty when the
; user just told it what to open.
; =============================================================================
zf_entry:
    push si
    push ax
    push cx
    push dx

    mov si, zf_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF=1 = window table full
    jc .out
    mov [zf_win], bx

    mov si, zf_menus
    call OSAPI_MENU_SET             ; preserves every register AND the flags
    mov si, zf_about
    call OSAPI_ABOUT_SET
    mov al, 1
    call OSAPI_WM_SIZABLE           ; re-wrap on drag
    push cx                         ; ...and the floor is DECLARED now (SPEC.md
    push dx                         ; 11.100.2) rather than answered by a
    mov cx, ZF_MIN_W                ; negotiator. zf_onsize was ten
    mov dx, ZF_MIN_H                ; instructions that did exactly this and
    call OSAPI_WM_MINSIZE           ; are gone with it - and it is a WIDER
    pop dx                          ; promise than the one it replaced: a
    pop cx                          ; negotiator is consulted by the grow box
                                    ; and by OSAPI_WM_RESIZE, and by neither of
                                    ; the two clamps that had no floor at all
%ifdef OS88UI_SBDRAG
    pushf                           ; the entry still owes the loader
    push ax                         ; wm_create's CF (SPEC.md 13.10.7.1)
    mov ax, zf_onup                 ; SPEC.md 13.10.6.4: the THUMB's alone.
    call OSAPI_WM_ONMOUSEUP         ; zwin6.inc's v6 input poll owns a gesture
    mov ax, zf_ondrag               ; that cannot be live at the same time as a
    call OSAPI_WM_ONDRAG            ; bar drag, so 13.7's no-mixing rule is
    sbb al, al                      ; satisfied by "not at the same time"
    mov [zw_nodrag], al
    pop ax
    popf
%endif
    mov ax, zf_onresize             ; ...and TELL US when the adapter moves
    call OSAPI_WM_ONRESIZE          ; (SPEC.md 11.98)

    ; A story named by a double-click (SPEC.md 54.7). SI comes back pointing
    ; into KERNEL_SEG, so it is read through ES and copied before anything
    ; else is called - the slot is read-and-clear and the buffer is the
    ; kernel's, valid for this call only.
    ;
    ; DX AND BL ARE HALF THE ANSWER AND ARE BANKED FIRST. The call also hands
    ; back the directory the document lives in and its volume, which is the
    ; pair OSAPI_FILE_GOTO takes - and without them the name is only findable
    ; from wherever the kernel happens to be standing. It is standing in the
    ; PROGRAM's directory: ld_run_body reads the image out of it and far-calls
    ; this proc as one unit, and assoc_back does not restore the document's
    ; folder until the load has returned. So a story in B:\INFOCOM, opened by
    ; double-click while FROTZ.O88 sits in the root, was searched for in the
    ; root and reported 'That story is not on this disk.' A story in the root
    ; worked, which is why this survived: it is the case the two directories
    ; are the same one. zi_openpend spends the pair.
    call OSAPI_ARG_FILE             ; out CF=0, SI = ES:SI name, DX = its
    jc .noarg                       ; directory cluster, BL = its volume
    mov [zi_argclus], dx
    mov [zi_argdrv], bl
    mov byte [zi_arghave], 1        ; 0,0 is a real locator, so the pair cannot
                                    ; speak for itself
    mov di, zf_pend
    call zf_copyname                ; ES:SI -> DS:DI, NUL, at most 13 bytes
    mov byte [zf_pendok], 1
.noarg:
%ifdef ZHARNESS
    ; The harness opens B:\STORY.DAT whatever it was launched from, so the
    ; scripted walk that starts it may double-click either the story or the
    ; interpreter and does not have to tell them apart on the desktop. An
    ; ARG_FILE, when there is one, still wins: that is the shipping path and
    ; the harness must not be the only thing that exercises it.
    call zh_init
    cmp byte [zf_pendok], 0
    jne .zhready
    push si
    push di
    mov si, zh_story
    mov di, zf_pend
.zhcp:
    mov al, [si]
    inc si
    mov [di], al
    inc di
    or al, al
    jnz .zhcp
    pop di
    pop si
    mov byte [zf_pendok], 1
.zhready:
    push si
    mov si, zh_m_ready
    call zh_mark
    pop si
%endif
    ; **RELOAD BX.** Our contract is BX = the window pointer, and everything
    ; between wm_create and here is free to clobber it - OSAPI_ARG_FILE does.
    ; Returning CF=0 with a bogus BX is not defended against: the loader's
    ; inst_bind_win divides by it (wm_ptr2idx) with no int 0 handler, so the
    ; whole machine hangs with the cursor still moving because the mouse ISR
    ; keeps running. That is exactly how this was found.
    mov bx, [zf_win]
    clc                             ; wm_create's CF was consumed at .out's jc
.out:
    pop dx
    pop cx
    pop ax
    pop si
    ret

; -----------------------------------------------------------------------------
; zf_copyname - copy a NUL 8.3 name out of the kernel segment into our own
; in:  ES:SI = source (KERNEL_SEG), DS:DI = destination
; out: nothing; preserves all registers
;
; Its own proc because it is the one thing in this file that reads through ES
; on purpose, and inlining it three times is three chances to forget the
; override (SPEC.md 20.5: without it you read your own image at that offset,
; which assembles cleanly and runs wrong).
; -----------------------------------------------------------------------------
zf_copyname:
    push ax
    push cx
    push si
    push di
    mov cx, 13                      ; 8.3 + dot + NUL
.loop:
    mov al, [es:si]
    inc si
    mov [di], al
    inc di
    or al, al
    jz .done
    loop .loop
    mov byte [di-1], 0              ; truncate rather than run on
.done:
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; zf_paint - W_PAINT (SPEC.md 11)
; in:  SI = window ptr; the gfx lock is HELD and the content is already white
; out: nothing; MUST return SI unchanged (kernel/wm.inc reloads the window
;      pointer from it after the call - the SPEC never says so)
;
; Three states, and the paint proc is where the worker gets hired because the
; entry proc could not do it:
;
;   no story        the splash: what this is, and how to open one
;   loading         a one-line notice; zi_load owns the screen
;   running         zw_paint / zw6_paint repaints from the scrollback
; =============================================================================
zf_paint:
    push ax
    push bx
    push cx
    push dx
    push di
    push si

    mov [zf_win], si                ; the window pointer never changes, but a
                                    ; repaint is the cheapest place to be sure

    ; A story named by a double-click (SPEC.md 54.7) is opened HERE and not in
    ; the entry proc, because opening one draws - the refusal notice, the
    ; status line, the first screenful - and the entry proc may not. The first
    ; paint is the earliest moment that is allowed to, and the flag makes it
    ; happen exactly once.
    cmp byte [zf_pendok], 0
    je .nopend
    mov byte [zf_pendok], 0
    call zi_openpend
.nopend:

    cmp byte [zf_state], ZFS_RUN
    jne .nostory
    call zf_hire                    ; idempotent; retried every paint because a
                                    ; full task table is normal and TRANSIENT
    call zw_paint
    jmp .done
.nostory:
    call zf_splash
.done:
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zf_hire - make sure this instance owns its worker, and say nothing when it
;           already does
; in:  gfx lock held (OSAPI_TASK_SPAWN requires it), [zf_state] == ZFS_RUN
; out: nothing; preserves all registers
;
; Called from every paint, which is what makes a refusal survivable: the task
; table filling up is a normal, transient outcome (close a Timer and the next
; repaint gets the worker), so this retries rather than failing the story.
; Until one is granted the window carries the notice zw_notice drew and the VM
; does not advance - there is no fallback that runs the VM under the caller's
; lock, because a turn is seconds and that is exactly what the worker exists
; to avoid.
;
; THERE IS DELIBERATELY NO OSAPI_MEM_PARKSAFE HERE, and this is the one app in
; the tree that must never grow one (SPEC.md 66.5.9.1). That declaration says
; "no register and no stack slot of mine holds a pointer derived from a
; movable claim across a call that can yield" - and zx_lock does exactly that,
; by design: ES is the program counter's segment and it is PUSHED across
; OSAPI_GFX_LOCK, because an OSAPI slot owes us nothing but DS. The
; interpreter is built on ES being the PC (SPEC.md 61.3); it cannot make this
; promise, and it does not need to. Its claims move at the ordinary
; OSAPI_TASK_ALIVE park instead, which zx_worker reaches once per 64-opcode
; burst - and zi_yield, the only other place ALIVE is called with a story
; segment live, repairs its own ES from [zf_sdelta].
; -----------------------------------------------------------------------------
zf_hire:
    push ax
    cmp byte [zf_hired], 0
    jne .out
    mov ax, zx_worker
    call OSAPI_TASK_SPAWN           ; CF=1 refused, nothing was created
    jc .out
    mov byte [zf_hired], 1
.out:
    pop ax
    ret

; =============================================================================
; zf_onkey - W_ONKEY (SPEC.md 11)
; in:  AL = ASCII, AH = scan code, SI = window ptr; gfx lock HELD, UI task
; out: nothing
;
; The UI half of the input path. It does three things and none of them is the
; VM: deposit the key in the ring the worker reads (zi_putkey), service a
; pending file request if the worker posted one (zi_service - THIS is the only
; place a dialog can be raised, because a worker may not raise one), and
; handle the scrollback keys the interpreter owns rather than the story.
; =============================================================================
zf_onkey:
    push ax
    push bx
    push cx
    push dx
    push di

    cmp byte [zf_state], ZFS_RUN
    jne .out

    ; Scrollback first: PgUp/PgDn belong to the interpreter at all times, so
    ; a story cannot swallow them by asking for a character.
    cmp ah, KEY_PGUP
    je .pgup
    cmp ah, KEY_PGDN
    je .pgdn

    call zi_putkey                  ; into the ring; the worker consumes it
    call zi_service                 ; a posted @save/@restore, if any
    call zi_script_flush            ; and SCRIPT.TXT catches up. The worker
                                    ; buffers the transcript because it may not
                                    ; touch a file slot; this is the UI task,
                                    ; so this is where it reaches the disk
    jmp .out
.pgup:
    mov ax, -1
    call zw_scrollpage
    jmp .out
.pgdn:
    mov ax, 1
    call zw_scrollpage
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zf_onclick - W_ONCLICK
; in:  CX = x, DX = y (ABSOLUTE screen coords), SI = window ptr; lock held
; out: nothing
;
; The scroll bar, and in v6 the mouse the story can read (@read_mouse).
; =============================================================================
zf_onclick:
    push ax
    push bx
    push cx
    push dx
    cmp byte [zf_state], ZFS_RUN
    jne .out
%ifdef OS88UI_SBDRAG
    call os88ui_sbdragging          ; A PRESS CANNOT ARRIVE DURING A LIVE DRAG,
    jc .nostale                     ; so one that does means the release never
    push cx                         ; came (SPEC.md 13.10.5.7)
    push dx
    call zw_sbdrop
    pop dx
    pop cx
.nostale:
%endif
    call zw_click
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%ifdef OS88UI_SBDRAG
; -----------------------------------------------------------------------------
; zf_ondrag / zf_onup - the thumb gesture's other two edges (SPEC.md 13.10.5)
; in:  CX = x, DX = y (ABSOLUTE), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
;
; THE THUMB'S ALONE, like Word's (13.10.6.4): this app has no other use for
; either edge, so a release with no drag live falls straight out.
; -----------------------------------------------------------------------------
zf_ondrag:
    push ax
    push bx
    push cx
    push dx
    cmp byte [zf_state], ZFS_RUN
    jne zf_sbd_out
    call zw_sbtrack                 ; DX is still the pointer's y
    jmp short zf_sbd_out
zf_onup:
    push ax
    push bx
    push cx
    push dx
    cmp byte [zf_state], ZFS_RUN
    jne zf_sbd_out
    call zw_sbcommit                ; the release COMMITS, unconditionally
zf_sbd_out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif

; =============================================================================
; zf_onresize - OSAPI_WM_ONRESIZE (SPEC.md 11.98): the ADAPTER moved under us
; in:  SI = window, CX/DX = the new content size; the gfx lock is held and
;      DRAWING IS FORBIDDEN
; out: nothing; preserves every register
;
; **THE BOX NEEDS NOTHING SAID TO IT** - zw_geom re-reads it on every pass and
; the text re-wraps from there, which is what OSAPI_WM_SIZABLE has always meant
; here. What goes stale is one byte: [zw_bpp], which zw_geom LATCHES behind
; [zw_vidok] with the comment "the adapter is a fact we test once". It is a
; fact, and this is the one event that changes it - so the latch is dropped and
; the next pass asks again. Four sites read it, all of them deciding whether
; @set_colour means anything, and on a 1bpp adapter (SPEC.md 39.4) every colour
; rounds to black, white or a dither, so a story that colours by meaning goes
; unreadable if we say we can.
;
; **[zm_bpp] IS DELIBERATELY NOT TOUCHED.** That one is the same question asked
; for a different purpose: bit 0 of the story's Flags1 header byte, "colours
; available", which zm_setup writes when the story is LOADED. A Z-machine
; header is state the story reads at startup and the Standard does not expect
; it to move underneath a running game, so its granularity is the story load
; and that is where it stays.
; -----------------------------------------------------------------------------
zf_onresize:
    mov byte [zw_vidok], 0
    ret


; =============================================================================
; zf_oncmd - AM_ONCMD, the menu bar (SPEC.md 12.2)
; in:  AL = item index, AH = menu index, SI = window ptr, BX = the menu set
; out: nothing; runs on the UI task under the gfx lock
;
; This is the task that owns every file slot, so Open / Save / Restore all
; live here and the worker only ever ASKS for them (zio.inc).
; =============================================================================
zf_oncmd:
    push bx
    push cx
    push dx
    push di
    or ah, ah
    jnz .m1
    ; --- File ---------------------------------------------------------------
    cmp al, ZF_F_OPEN
    je .open
    cmp al, ZF_F_SAVE
    je .save
    cmp al, ZF_F_RESTORE
    je .restore
    jmp .out
.m1:
    cmp ah, 1
    jne .out
    ; --- Story --------------------------------------------------------------
    cmp al, ZF_S_SCRIPT
    je .script
    jmp .out

.open:
    call zi_open_dlg
    jmp .out
.save:
    mov byte [zf_req], ZREQ_SAVE    ; the menu can start one too, not just @save
    call zi_service
    jmp .out
.restore:
    mov byte [zf_req], ZREQ_RESTORE
    call zi_service
    jmp .out
.script:
    xor byte [zf_script], 1
    call zi_script_flush            ; turning it OFF must land what is buffered;
                                    ; turning it on costs one empty write
.out:
    pop di
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; zf_abouth - the About handler (OSAPI_ABOUT_SET)
; in:  SI = window ptr; gfx lock held
; out: nothing
;
; It says what this is and what it is not, because the name is borrowed.
; =============================================================================
zf_abouth:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call zw_clear
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, zf_abt_lines
    add dx, 12
.loop:
    mov cx, [si]
    or cx, cx
    jz .done
    push si
    mov si, cx
    mov cx, bx
    add cx, 8
    call OSAPI_FONT_STR
    pop si
    add si, 2
    add dx, ZF_LEAD
    jmp .loop
.done:
    mov byte [zf_showabout], 1      ; the next key or click repaints the story
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; zf_splash - the no-story screen
; in:  SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; =============================================================================
zf_splash:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov bx, ax
    add dx, 16
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov si, zf_splash_lines
.loop:
    mov cx, [si]
    or cx, cx
    jz .done
    push si
    mov si, cx
    mov cx, bx
    add cx, 12
    call OSAPI_FONT_STR
    pop si
    add si, 2
    add dx, ZF_LEAD
    jmp .loop
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Data: the window template, the menu set, the strings
; =============================================================================
zf_tpl:
    dw ZF_WIN_X, ZF_WIN_Y, ZF_WIN_W, ZF_WIN_H
    dw zf_ttl, zf_paint, zf_onkey, zf_onclick

    OS88_MENUSET zf_menus, zf_name, zf_oncmd
        OS88_MENU zf_m_file, zf_i_file, 3
        OS88_MENU zf_m_story, zf_i_story, 1
    OS88_MENUSET_END zf_menus

ZF_F_OPEN    equ 0
ZF_F_SAVE    equ 1
ZF_F_RESTORE equ 2
ZF_S_SCRIPT  equ 0

zf_name:     db 'Frotz', 0
zf_ttl:      db 'Frotz', 0
zf_m_file:   db 'File', 0
zf_i_file:   dw zf_it_open, zf_it_save, zf_it_rest
zf_it_open:  db 'Open Story...', 0
zf_it_save:  db 'Save Game...', 0
zf_it_rest:  db 'Restore Game...', 0
zf_m_story:  db 'Story', 0
zf_i_story:  dw zf_it_script
zf_it_script: db 'Transcript', 0

zf_about:    db 'Frotz', 0

zf_abt_lines:
    dw zf_a1, zf_a2, zf_a3, zf_a4, zf_a5, zf_a6, 0
zf_a1: db 'Frotz - a Z-machine for os8088', 0
zf_a2: db 'Plays Infocom and Inform stories, v1-v8.', 0
zf_a3: db 0
zf_a4: db 'An independent implementation of the', 0
zf_a5: db 'Z-Machine Standard 1.1 in 8086 assembly.', 0
zf_a6: db "Not a port of David Griffith's Frotz.", 0

zf_splash_lines:
    dw zf_p1, zf_p2, zf_p3, 0
zf_p1: db 'Frotz', 0
zf_p2: db 0
zf_p3: db 'File > Open Story... to begin.', 0

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
; AT THE END OF THE CODE, just before OS88_BSS, which is os88ui.inc's own rule:
; the package header and an OS88_ICON16 block are at FIXED OFFSETS in the image
; (SPEC.md 20.2), so code emitted between them fails the icon macro's assertion.
;
; Only the scroll bar - this app draws no standard buttons. It had the LAST
; private implementation of that widget (SPEC.md 13.10.6), and zw_thumb's own
; header said what it was.
%define OS88UI_SCROLL
%define OS88UI_BARONLY          ; ...and NOTHING ELSE (SPEC.md 13.10.6.5): this
                                ; app draws no standard button, and carrying
                                ; one cost 567 bytes of a floppy
%include "os88ui.inc"

; =============================================================================
; bss (SPEC.md 21 step 5: the loader zeroes exactly OS88_BSS bytes)
; =============================================================================
; All-zero must be a valid fresh state and it is: ZFS_NONE is 0, no claim is
; held, the worker is not hired and the ring is empty.
ZFS_NONE    equ 0                   ; [zf_state]
ZFS_LOAD    equ 1
ZFS_RUN     equ 2

ZREQ_NONE   equ 0                   ; [zf_req] - see zio.inc
ZREQ_SAVE   equ 1
ZREQ_RESTORE equ 2
ZREQ_RESTART equ 3                  ; @restart: dynamic memory is re-read off
                                    ; the floppy, and a file slot is the UI
                                    ; task's (SPEC.md 61.4/59.6)

    OS88_BSS ZF_BSS_TOTAL
    OS88_IMAGE_END
