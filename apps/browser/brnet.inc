; =============================================================================
; The browser's NETWORK half - docs/plans/completed/NET-STACK-PLAN.md stage D
;
; SPEC.md 62.11's socket verbs, an HTTP/1.0 GET, and the page handed to the
; parser that already existed. The rendering half of this application needed
; no change at all to gain a network, which is the claim docs/plans/completed/BROWSER-PLAN.md
; made when it kept fetching out of the layout work entirely.
;
; --- THE WORKER OWNS THE FETCH, AND MAY NOT OWN THE MEMORY -------------------
; SPEC.md 20.6 rule 7: a worker may not call OSAPI_MEM_*, and every NETV_*
; verb is non-blocking (netpkg.inc), so the division is forced and is the
; right one anyway:
;
;   the UI task  claims the first 1 KiB of SOURCE and starts the fetch (br_go)
;   the worker   opens, polls, sends the request and drains into the source
;                claim; at its end it pauses and asks the UI task to grow it
;   the UI task  grows by 4 KiB under br_memroom, then lets the worker resume
;   the UI task  cuts the source to the page and claims the other three
;                buffers from its size (br_netclaim, SPEC.md 71.14)
;   the worker   parses, lays out without the gfx lock, then paints under it
;
; ...and a FRAMESET goes round once more (SPEC.md 71.15): its children are
; drained into the page's own buffers, which were cut to the frameset's size,
; so the worker posts [br_fin] = 4 instead of fetching the first child, the UI
; task frees the document and line table, regrows the source to 1 KiB and
; claims the two again from it (br_frameclaim - the link arena is KEPT, the
; queued frame srcs live there), and answers 5, on which br_nstep starts the
; first child. [br_fin] is 0 draining, 1 the page is in and the UI task owes
; its claims, 2 claimed - parse, 3 parsing (br_abort waits that one out), 4
; the UI task owes a frameset's re-claim, 5 re-claimed - start the first child.
;
; **THE SOURCE CLAIM IS KEPT UNTIL THE NEXT LOAD**, as a file load keeps it:
; File > Save As writes it. The network claim starts at one KiB and grows in
; the UI task only when full, through the first 96 KiB raw-source floor and
; farther while br_memroom can preserve the load-start 20-percent reserve. The
; receive counter and parser's logical cursor cross each 64 KiB alias. Parser
; helpers see only bounded 48 KiB windows; document positions remain 16-bit.
; br_netclaim cuts it to the received size; br_free_all returns it at the next
; load.
;
; --- WHAT IS AND IS NOT PARSED ------------------------------------------------
; The status line's CODE is read, because 404 and 200 are different pages and
; showing the error body without saying so is the worst available outcome. The
; headers are otherwise skipped to the blank line and **nothing is copied**:
; the byte-by-byte scan writes into the source claim only once BN_BODY is
; reached, so the page starts at offset 0 by construction. Buffering the whole
; reply and moving the body down afterwards would have been a `rep movsb` of up
; to the entire response for no gain.
;
; No chunked encoding: HTTP/1.0 is what this asks for and a 1.0 server answers
; with a length or a close. 301/302/303/307/308 retain only Location and reuse
; the same claims for at most three hops.
;
; EVERYTHING OFF THE WIRE IS HOSTILE (docs/plans/completed/NET-STACK-PLAN.md 8.5). The only
; thing this trusts a server about is how many bytes it just handed over, and
; that is bounded by the capacity passed to NETV_RECV.
; =============================================================================

; **THE ORDER IS LOAD-BEARING, NOT COSMETIC.** BN_OPEN..BN_BODY is a
; CONTIGUOUS RANGE and it is the whole definition of "a fetch is in flight" -
; br_go's guard is a range test on it (see br_go), because the states either
; side of that range are both finished ones. Insert a new IN-FLIGHT state
; inside the range; a new terminal state goes after BN_ERR.
BN_IDLE     equ 0                   ; ...nothing has been fetched yet
BN_OPEN     equ 1                   ; --- in flight from here...
BN_WAIT     equ 2
BN_SEND     equ 3
BN_HEAD     equ 4                   ; ...draining, still looking for the blank
BN_BODY     equ 5                   ; ...and past it
BN_DONE     equ 6                   ; --- ...to here. Finished, page rendered
BN_ERR      equ 7                   ; finished, and it did not work

BR_HOSTMAX  equ 64                  ; matches netpkg.inc's NET_HOSTMAX field
BR_PATHMAX  equ 96
BR_REQMAX   equ BR_PATHMAX + BR_HOSTMAX + BR_URLMAX + 180
                                    ; path + host + the 159-byte POST body and
                                    ; 180 bytes for the longest request line and
                                    ; headers; both request forms share br_rxb.
BR_CHUNK    equ 512                 ; one NETV_RECV, well under NET_SKMAX
BR_COOKIE_MAX equ 95               ; one bounded name=value session cookie
BR_PARSE_LAYOUT equ 0x80            ; parser state is repurposed after final EOF

; -----------------------------------------------------------------------------
; br_hire - one worker, retried until granted (SPEC.md 20.6)
; A full task table is a NORMAL, TRANSIENT outcome, so this is called from
; every paint and latches only on success.
; -----------------------------------------------------------------------------
br_hire:
    push ax
    push bx
    cmp byte [br_spawn], 1
    je .out
    mov ax, br_worker
    mov bx, [br_win]
    call OSAPI_TASK_SPAWN
    jc .out
    mov byte [br_spawn], 1
    ; ...AND THE WORKER MAY BE RESTARTED (SPEC.md 66.6.2). Hiring one
    ; would otherwise pin the region for ever - task_spawn wrote our
    ; segment into that worker's frame before its first instruction:
    ; br_nstep banks a generation and every store is guarded on it, so a fetch
    ; abandoned by a restart writes nothing - which is br_abort's own design.
    ; The kernel restarts a worker only where it PARKS, which for us is
    ; inside OSAPI_TASK_ALIVE at the top of the loop; we do not declare
    ; OSAPI_MEM_PARKSAFE, so the gfx-lock park is not in play.
    OS88_WORKER_RESTARTABLE br_worker
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_go - start a fetch of the URL at DS:SI
; in:  SI = a NUL URL; UI task, gfx lock held
; out: nothing. Failure is REPORTED IN THE STATUS LINE rather than returned:
;      every caller is a click or a keystroke and none has anywhere to put a
;      carry flag.
; -----------------------------------------------------------------------------
; --- br_promise - tell the kernel whether our content is standing still ------
; in:  nothing; THE GFX LOCK HELD BY THE CALLER
; out: nothing (every register and the flags preserved)
;
; SPEC.md 71.11. This app is SPEC.md 11.96.1's disqualifier written out - the
; worker advances the page and SKIPS DRAWING when the window cannot be seen,
; which is content changing without being drawn - but only while a fetch is in
; flight. [br_nstate] already answers that: BN_OPEN..BN_BODY is a fetch, and
; BN_IDLE / BN_DONE / BN_ERR are a page standing still. So the promise is per
; FETCH rather than per package, and this is the state machine's second job.
;
; THE LOCK IS A CONDITION, NOT POLITENESS (SPEC.md 20.6 rule 7): the clear
; frees the raise cache, so a worker calling it outside a hold could free a
; buffer the UI task is blitting out of. Two of the three call sites are the
; worker's and both are already inside one.
;
; The window pointer comes from [br_win] and not from whatever BX holds, which
; is SPEC.md 11.96.11.4's lesson: Paint once handed the kernel a screen HEIGHT
; as a window record and the kernel set a bit inside its own jump table.
br_promise:
    pushf
    push ax
    push bx
    mov bx, [br_win]
    or bx, bx                       ; before wm_create, and after a refused
    jz .out                         ; one: there is nothing to promise about
    xor al, al                      ; ...a fetch IS in flight: withdraw
    cmp byte [br_nstate], BN_OPEN
    jb .say                         ; BN_IDLE - nothing fetched yet, so still
    cmp byte [br_nstate], BN_DONE
    jb .say                         ; BN_OPEN..BN_BODY: AL stays 0
    mov al, OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP
                                    ; BN_DONE / BN_ERR: the page has settled -
                                    ; and it is two colours, which is what
                                    ; gets it under wm_su_kb's 64,512 ceiling
                                    ; on a VGA (SPEC.md 11.96.17)
.say:
    call OSAPI_WM_SAVEU
.out:
    pop bx
    pop ax
    popf
    ret

br_go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov al, [br_nstate]             ; **A FETCH IN FLIGHT IS DROPPED, NOT A
    cmp al, BN_OPEN                 ; REASON TO REFUSE.** It used to answer
    jb .free                        ; `Still fetching` - and that refusal went
    cmp al, BN_DONE                 ; out through the .say tail below, which
    jb .abort                       ; sets BN_ERR: the worker's page was still
                                    ; arriving, its socket still open, and the
                                    ; state machine now said the fetch had
                                    ; failed, so nothing ever finished it and
                                    ; nothing ever closed the socket. Pressing
                                    ; Back twice was enough, and the second
                                    ; press wedged the app for the session.
                                    ; A reader pressing Back twice means it,
                                    ; so the right answer was never a refusal.
                                    ; **IN FLIGHT IS A RANGE AND NOT "NOT
                                    ; IDLE"**: this tested `!= BN_IDLE`, and a
                                    ; finished fetch leaves BN_DONE - so the
                                    ; FIRST page loaded and every page after it
                                    ; was refused with `Still fetching` about a
                                    ; fetch that had ended, for the life of the
                                    ; window. Closing and reopening the browser
                                    ; was the only way on. BN_ERR is the same
                                    ; and worse: one bad URL and the app was
                                    ; finished. tests/brfetch.py and
                                    ; tests/ethernet.py both PASSED throughout,
                                    ; because both assert `br_nstate = 6` after
                                    ; ONE fetch and neither ever asked for a
                                    ; second - a gate that stops where the bug
                                    ; starts
    jmp short .free
.abort:
    call br_abort
.free:
    mov al, [br_post]
    mov ah, [br_reqpost]            ; method of the page we are leaving
    mov [br_reqpost], al
    mov byte [br_post], 0
    mov byte [br_postwarn], 0
    or al, al                       ; a history GET cannot replay the old body
    jnz .method
    cmp byte [br_nopush], 0
    je .method
    cmp byte [br_nstate], BN_DONE
    jne .method
    or ah, ah
    jz .method
    mov byte [br_postwarn], 1
.method:
    mov word [br_nmsg], 0
    cmp byte [br_targetnavflag], 0  ; a child-target navigation carries one
    jne .targetpending              ; URL override through this top-level fetch
    mov byte [br_frameoverridevalid], 0
.targetpending:
    mov byte [br_targetnavflag], 0
    call br_split                   ; the URL into host, port and path
    jc .badurl
    cmp byte [br_reqpost], 0        ; form responses are page navigations; the
    jne .native                     ; download worker currently issues GETs
    mov si, br_path                 ; route non-rendered file suffixes before
    call br_dl_autosave             ; allocating the page/source arenas
    jnc .native
    mov ax, 0FFFFh                  ; direct URL sentinel for br_dl_link
    call br_dl_link
    call br_dl_restorepage          ; a file action never becomes the page URL
    jmp .out
.native:
    mov byte [br_islocal], 0        ; only an accepted HTTP navigation is remote
    mov byte [br_viewsrc], 0        ; a navigation always returns to rendered HTML
    mov al, 3                       ; refresh the per-instance bookmark cache
    call br_bmrequest               ; refresh this page before drawing its star
    push si
    mov si, br_loc
    mov di, br_ubuf                 ; ...and the bar shows what we are getting,
    call os88line_set               ; whether it was typed there or not -
    call os88line_draw              ; **AND IT IS DRAWN HERE AND NOT BY THE
    call br_bmcheck                 ; reflect the destination even before the
    call br_stardraw                ; response arrives or after a failed fetch
    pop si                          ; CALLERS.** os88line_set moves the BUFFER;
                                    ; only a draw moves the glass, and every
                                    ; caller but the typed one had forgotten it
                                    ; - so following a link, submitting a form,
                                    ; Back, Forward and Reload all fetched the
                                    ; right page under the PREVIOUS page's
                                    ; address, and the bar corrected itself the
                                    ; next time anything happened to repaint it
                                    ; (clicking in it, which is how this was
                                    ; reported). One draw at the one place the
                                    ; text changes, which is here
    call br_free_all
    call OSAPI_MEM_AVAIL
    mov [br_membase], bx            ; conventional free total at this fetch's start
    mov ax, 1                       ; claim a small source window; grow on demand
    call br_srcclaim
    jc .nomem
    call br_claimrest               ; parser output and metadata must exist before
    jc .nomem                       ; the first response block is consumed
    mov byte [br_memstop], 0
    mov byte [br_frameappend], 0
    mov byte [br_framecount], 0
    mov byte [br_frameidx], 0
    mov byte [br_framecut], 0
    cmp byte [br_nopush], 0         ; the history, unless this trip IS
    jne .nohist                     ; the history: Back, Forward and Reload all
    call br_hpush                   ; come through here and none of them is a
.nohist:                            ; new place (BROWSER-PLAN 5)
    xor ax, ax
    mov [br_nmsg], ax               ; ...and a new fetch owns the state line, so
                                    ; last time's verdict goes: without this a
                                    ; `Saved` would survive into the next page
                                    ; and be shown again when it finished
    cmp byte [br_postwarn], 0
    je .warned
    mov word [br_nmsg], br_s_postget ; history stores URLs, never a POST body
.warned:
    mov byte [br_postwarn], 0
    mov [br_nlen], ax
    mov word [br_nlen_hi], 0
    mov word [br_srclen_hi], 0
    mov [br_fin], al                ; ...the hand-over (SPEC.md 71.14)
    mov [br_code], ax
    mov [br_trunc], al              ; ...the CUT flag, which br_parse clears on
                                    ; a file load and nothing cleared on this
                                    ; path, so one truncated page would have
                                    ; reported every page after it as cut
    mov [br_hcol], ax               ; per-fetch header state
    mov [br_hstate], al
    mov byte [br_lstate], 0
    mov byte [br_ckstate], 0       ; a header token never crosses responses
    mov byte [br_ckattrstate], 0
    mov word [br_cklen], 0
    mov byte [br_ckbuf], 0
    mov word [br_ckattrlen], 0
    mov byte [br_ckpathbuf], 0
    mov byte [br_ctstate], 0
    mov byte [br_ctpos], 0
    mov byte [br_ctlen], 0
    mov byte [br_ctseen], 0
    mov byte [br_nonhtml], 0
    mov byte [br_cttype], 0
    mov byte [br_ctparamstate], 0
    mov byte [br_httpcharsetseen], 0
    mov byte [br_http_utf8], 0
    mov word [br_redirlen], 0
    mov byte [br_redirurl], 0
    mov byte [br_redirs], 0
                                    ; [br_hcol] only ever counts up, so it sat
                                    ; at 12 after the first reply's status line
                                    ; and every byte of every later fetch was
                                    ; taken past the digit accumulator - the
                                    ; code was read for the FIRST page of the
                                    ; session and no other
    mov byte [br_nstate], BN_OPEN
    push ax                         ; ...AND THE PROMISE IS WITHDRAWN HERE, at
    push bx                         ; the transition and UNCONDITIONALLY, not
    mov bx, [br_win]                ; by re-reading [br_nstate] on the way out
    xor al, al                      ; (SPEC.md 71.11). BX IS THE WINDOW and is
    call OSAPI_WM_SAVEU             ; not optional: without it wm_saveu's own
    pop bx                          ; bounds check refuses the call and the
    pop ax                          ; withdrawal silently does not happen -
                                    ; SPEC.md 11.96.11.4's guard earning its
                                    ; keep a second time, and the reason this
                                    ; is not `whatever BX held`.
                                    ;
                                    ; THAT WAS A BUG AND tests/brpromise.py
                                    ; FOUND IT. The worker does not need the
                                    ; lock to move the state machine - only to
                                    ; DRAW - so with no NIC it reaches
                                    ; net_find, fails, and writes BN_ERR while
                                    ; br_go still holds the lock. A br_promise
                                    ; at the exit then read BN_ERR, called the
                                    ; state settled, and SET the promise: the
                                    ; withdrawal never happened at all, and
                                    ; the gate caught it as a one-in-seven
                                    ; flake rather than as a failure
    call br_toolbar                 ; the strip, because br_hpush above has
    jmp short .out                  ; just moved what Back and Forward may do
.badurl:
    mov si, [br_nmsg]
    or si, si
    jnz .say
    mov si, br_s_badurl
    jmp short .say
.nomem:
    mov si, br_s_nomem
.say:
    mov byte [br_postwarn], 0
    call br_errst                   ; the refusal, said
    call br_promise                 ; **THIS TAIL IS A THIRD SETTLE** (SPEC.md
                                    ; 71.11). A `https://` URL, an empty host
                                    ; or a refused claim lands here with the
                                    ; withdrawal from the BN_OPEN transition
                                    ; still outstanding - and the fetch it was
                                    ; withdrawn for was aborted, so neither
                                    ; br_finish nor br_nshow ever runs to put
                                    ; it back. The page then stands still with
                                    ; no promise on it for the rest of the
                                    ; session: every raise repaints instead of
                                    ; restoring. BN_ERR is already stored, so
                                    ; br_promise's own state test re-states it
.out:                               ; ...and NOTHING here. The withdrawal is
    jmp br_pop6                     ; above, at the transition; the two other
                                    ; ways a fetch settles own the set, and the
                                    ; paths that never start one leave a
                                    ; promise that was already true untouched

; A non-rendered URL is handled before history push or page teardown. The
; caller has already placed that URL in the shared location buffer, so restore
; the current history entry after the Save dialog returns. This keeps a
; download link from becoming the Reload or bookmark destination while the
; original document and title remain on screen.
br_dl_restorepage:
    cmp word [br_histn], 0
    je .out
    mov bx, [br_histi]
    cmp bx, [br_histn]
    jae .out
    call br_hrestore
    mov si, br_ubuf
    call br_split                  ; restore the displayed page's URL base
.out:
    ret

; -----------------------------------------------------------------------------
; br_split - a URL at DS:SI into br_host, br_port and br_path
; out: CF=1 = not something we can fetch
;
; `http://host[:port][/path]`, and a bare `host/path` is accepted because a
; person typing an address into a box does not type a scheme. **`https://` is
; REFUSED HERE AND BY NAME** (SPEC.md 71.2): a TLS handshake is public-key
; arithmetic and an RSA-2048 private operation is MINUTES on a 4.77MHz 8088,
; so this machine cannot reach an HTTPS site at all - and a browser that
; hangs for two minutes and then fails is worse than one that says so at once.
; -----------------------------------------------------------------------------
br_split:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [br_pendingflag], 0
    push ds                         ; br_hrefpfx reads ES:SI - the link
    pop es                          ; resolver's, so one case-blind prefix
    mov word [br_port], 80          ; test serves both
    mov bx, br_s_schemes            ; https:// ?
    call br_hrefpfx
    jc .nohttps
    stc
    jmp .out
.nohttps:
    mov bx, br_s_scheme             ; http:// - stepped over if present
    call br_hrefpfx
    jc .host                        ; ...NOT present: SI is already the host
    add si, 7                       ; ...present: past it. br_hrefpfx answers
.host:                              ; CF=0 for a MATCH and does not advance SI
    mov di, br_host
    mov cx, BR_HOSTMAX-1
.hloop:
    mov al, [si]
    or al, al
    jz .hend
    cmp al, '/'
    je .hend
    cmp al, ':'
    je .colon
    mov [di], al
    inc di
    inc si
    loop .hloop
    mov al, [si]
    or al, al
    jz .hend
    cmp al, '/'
    je .hend
    cmp al, ':'
    je .colon
    jmp .long
.hend:
    mov byte [di], 0
    cmp di, br_host
    je .bad                         ; no host at all
    jmp short .path
.colon:
    mov byte [di], 0
    inc si
    mov word [br_port], 0
.pd:
    mov bl, [si]
    cmp bl, '0'
    jb .pdone
    cmp bl, '9'
    ja .pdone
    inc si
    mov ax, [br_port]
    mov cx, 10
    mul cx
    sub bl, '0'
    xor bh, bh
    add ax, bx
    mov [br_port], ax
    jmp short .pd
.pdone:
    cmp word [br_port], 0
    jne .path
    mov word [br_port], 80
.path:
    mov di, br_path
    mov cx, BR_PATHMAX-1
    mov al, [si]
    cmp al, '/'
    je .ploop
    mov byte [di], '/'              ; a bare host is its root, which is what
    inc di                          ; every browser and every user assumes
    dec cx
.ploop:
    mov al, [si]
    or al, al
    jz .pend
    cmp al, '#'
    je .fragment
    mov [di], al
    inc di
    inc si
    loop .ploop
    cmp byte [si], 0
    jne .long
.pend:
    mov byte [di], 0
    clc
    jmp short .out
.fragment:
    mov byte [di], 0               ; fragment is never part of the request path
    inc si
    mov di, br_pendingfrag
    mov cx, BR_ANCHORNAMEMAX
.fragcopy:
    lodsb
    stosb
    or al, al
    jz .fragdone
    loop .fragcopy
    mov byte [di], 0
.fragdone:
    mov byte [br_pendingflag], 1
    clc
    jmp short .out
.bad:
    stc
    jmp .out
.long:
    mov word [br_nmsg], br_s_urltoo
    stc
.out:
    jmp br_pop7

; =============================================================================
; THE WORKER
; =============================================================================
br_worker:
.loop:
    mov bx, [br_win]
    call OSAPI_TASK_ALIVE           ; the death door: may never return
    call br_nstep
    call br_dl_step                ; Browser 1.6b's independent download socket
    mov ax, 1
    call OSAPI_TASK_SLEEP
    jmp .loop

br_nstep:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; ES = OURS for every NETV_* buffer
    mov al, [br_gen]                ; **BANK THE GENERATION** (br_abort): this
    mov [br_gen0], al               ; pass may be pre-empted inside any of the
                                    ; driver calls below, and while it is, the
                                    ; UI task can drop this fetch and start
                                    ; another. Every store this routine makes
                                    ; is guarded on the two still agreeing, so
                                    ; a superseded pass writes NOTHING - not
                                    ; the handle, not the state, and above all
                                    ; not the received bytes, which would land
                                    ; in a claim br_go has already freed
    mov al, [br_nstate]
    cmp al, BN_OPEN
    je .open
    cmp al, BN_WAIT
    je .wait
    cmp al, BN_SEND
    je .send
    cmp al, BN_HEAD
    je .drain
    cmp al, BN_BODY
    jne .out
    mov al, [br_fin]                ; ...past the drain (SPEC.md 71.14):
    or al, al                       ; 0 still draining, 1 waiting for the UI
    jz .drain                       ; task's claims, 2 claimed - parse; 4
    cmp al, 4                       ; waiting for the UI task's frameset
    je .kick                        ; re-claim, 5 re-claimed - start the first
    cmp al, 5                       ; child (SPEC.md 71.15)
    je .framestart
    cmp al, 6                       ; UI task owns the source growth handshake
    je .kick
    cmp al, 7                       ; growth refused: drain and discard overflow
    je .drain
    dec al
    jz .kick                        ; the wake was refused or not yet served
    call br_finish
    jmp .out

.open:
    call net_find                   ; WHICH DRIVER, and is it one of ours?
                                    ; DRVC_NET is the card and DRVC_FILE the
                                    ; cable, RAMDISK.DRV shares the second, and
                                    ; net_find asks each for NETV_IDENT
    jc .nodrv                       ; (apps/os88sock.inc, SPEC.md 20.11.1)
    jz .nolink                      ; ...and NETV_STATE's NSTF_SOCK with it,
                                    ; which is OS88SOCK_STATE's half
    mov bh, NET_CLASS
    mov bl, NETV_OPEN
    mov si, br_host
    mov cx, [br_port]
    call OSAPI_DRV_CALL
    jc .retry
    call br_gchk                    ; ...and it preserves AX, which is the
    jc .stalesock                   ; handle we have just been given
    mov [br_hnd], al
    mov byte [br_nstate], BN_WAIT
    jmp .redraw
.stalesock:                         ; superseded WHILE OPENING: this socket
    mov bh, NET_CLASS               ; belongs to nobody, and leaving it would
    mov bl, NETV_CLOSE              ; hold a handle for the session
    call OSAPI_DRV_CALL
    jmp .out
.nodrv:
    mov si, br_s_nodrv
    jmp .fail
.nolink:
    mov si, br_s_nolink
    jmp .fail

.wait:
    call br_qstat
    jc .retry
    cmp ah, NSK_UP
    je .up
    cmp ah, NSK_CONNECT
    je .out
    mov si, br_s_refused
    jmp .fail
.up:
    call br_gchk
    jc .out
    call br_req                     ; compose the bounded request into br_reqb
    mov word [br_sent], 0
    mov byte [br_nstate], BN_SEND
    jmp .redraw

.send:
    mov si, br_reqb
    add si, [br_sent]
    mov cx, [br_reqn]
    sub cx, [br_sent]
    mov al, [br_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_SEND
    call OSAPI_DRV_CALL
    jc .retry
    call br_gchk
    jc .out
    add [br_sent], cx
    mov ax, [br_sent]
    cmp ax, [br_reqn]
    jb .out                         ; it took some of it; the rest next turn
    mov byte [br_nstate], BN_HEAD
    jmp .redraw

.drain:
    ; Ask the driver for no more than the currently available source bytes.
    ; When the extent fills, pause before the next receive so the UI task can
    ; regrow the claim while no worker pointer into it is live.
    cmp byte [br_fin], 7
    je .drainonly
    cmp byte [br_nonhtml], 0
    jne .drainonly                  ; do not grow page storage for a non-HTML body
    mov ax, [br_srckb]
    mov cl, 6
    shr ax, cl                      ; complete 64 KiB banks in the claim
    cmp [br_nlen_hi], ax
    ja .grow                        ; defensive: do not address past the claim
    jb .bankfull
    mov ax, [br_srckb]
    and ax, 63                      ; final partial bank, in KiB
    mov cl, 10
    shl ax, cl
    sub ax, [br_nlen]               ; bytes left in that final bank
    jbe .grow
    mov bx, ax
    jmp short .roomcheck
.bankfull:
    xor bx, bx
    sub bx, [br_nlen]               ; bytes to the next 64 KiB boundary
    jnz .roomcheck
    mov bx, BR_CHUNK                ; 65,536 cannot be represented in BX
.roomcheck:
    or bx, bx
    jnz .recvroom
.grow:
    mov byte [br_fin], 6
    mov bx, [br_win]
    call OSAPI_WM_WAKE
    jmp .out
.recvroom:
    mov cx, BR_CHUNK
    cmp bx, cx
    jae .recv
    mov cx, bx
    jmp short .emsroom
.emsroom:
    cmp word [br_srcemshnd], 0
    je .recv
    mov bx, [br_nlen]
    and bx, EMS_PAGE-1
    mov ax, EMS_PAGE
    sub ax, bx                      ; keep this receive inside one EMS page
    cmp cx, ax
    jbe .recv
    mov cx, ax
    jmp short .recv
.drainonly:
    mov cx, BR_CHUNK                ; capacity refused: consume without storing
.recv:
    mov al, [br_hnd]
    mov di, br_rxb
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc .retry
    call br_gchk                    ; **THE ONE THAT MATTERS**: br_go has freed
    jc .out                         ; and re-claimed the source buffer, so a
    or cx, cx                       ; superseded br_take writes into memory
    jz .maybeend                    ; that is not ours any more
    call br_take                    ; ...CX bytes into the source claim
    call br_streamparse             ; parse the newly received safe prefix
    jmp .out
.maybeend:
    call br_qstat                   ; NOTHING READ IS NOT AN END (netpkg.inc):
    jc .retry                       ; the end is NSK_CLOSING with nothing left
    cmp ah, NSK_UP
    je .out
    cmp ah, NSK_CONNECT
    je .out
    or cx, cx
    jnz .out                        ; closing, and there is still data behind
    call br_gchk
    jc .out
    call br_hclose
    mov ax, [br_code]               ; **A REDIRECT IS DECIDED HERE, BEFORE THE
    cmp ax, 301                     ; HAND-OVER** (SPEC.md 71.15): the source
    je .redirect                    ; is still the live receive claim and nothing
    cmp ax, 302                     ; else has been claimed, so a hop neither
    je .redirect                    ; leaks a claim nor drains its target into
    cmp ax, 303                     ; one cut to the redirect body's size - and
    je .redirect                    ; a 301 with no body at all, which is what
    cmp ax, 307                     ; most servers send, is followed rather
    je .redirect                    ; than reported `Empty reply` below
    cmp ax, 308
    je .redirect
    mov ax, [br_code]
    cmp ax, 204
    je .nocontent
    cmp ax, 304
    je .notcached
    cmp byte [br_nonhtml], 0
    jne .nothtml
    mov ax, [br_nlen]               ; **THE PAGE HAS ARRIVED, AND ITS SIZE IS
    mov [br_srclen], ax             ; A FACT** (SPEC.md 71.14): hand it to the
    mov ax, [br_nlen_hi]
    mov [br_srclen_hi], ax
    or ax, [br_nlen]                ; UI task, which cuts the source to it and
    jz .empty                       ; claims the other three buffers from it -
    mov byte [br_fin], 1            ; a worker may not (SPEC.md 20.6 rule 7).
                                    ; The state stays BN_BODY, IN FLIGHT, so a
.kick:                              ; navigation in the gap drops this fetch
    mov bx, [br_win]                ; as it always did. Posted again on every
    call OSAPI_WM_WAKE              ; pass until answered: a wake into a full
    jmp short .out                  ; ring is refused, and the slot says kick
.empty:                             ; again
    mov si, br_s_empty
    jmp short .fail
.nothtml:
    mov si, br_s_nothtml
    jmp short .fail
.nocontent:
    mov si, br_s_http204
    jmp short .fail
.notcached:
    mov si, br_s_http304
    jmp short .fail

.retry:
    cmp ax, NETE_BUSY
    je .out                         ; the Link volume has the wire: next tick.
                                    ; NOT a failure (netpkg.inc)
    mov si, br_s_lost
.fail:
    call br_gchk                    ; a failure belongs to the fetch that had
    jc .out                         ; it, and this one has been dropped
    mov [br_nmsg], si
    mov byte [br_nstate], BN_ERR
    call br_hclose
.failchild:                         ; ...and a CHILD that failed closes the
    cmp byte [br_frameappend], 0    ; page whole - the table it was a row of,
    je .redraw                      ; D_END, the frameset's title, bar and
    call OSAPI_GFX_LOCK             ; base back (br_frameclose) - under the
    call br_gchk                    ; lock br_go runs under, so a navigation
    jc .failun                      ; in the gap cannot have freed beneath it
    call br_frameclose
.failun:
    call OSAPI_GFX_UNLOCK
.redraw:
    call br_nshow
.out:
    jmp br_pop7

.redirect:                          ; 301/302/303/307/308, AX = the code.
    cmp byte [br_redirurl], 0       ; Decided UNDER THE LOCK br_go runs under,
    je .redirbad                    ; the way br_finish decides .page: a
    cmp byte [br_redirs], 3         ; navigation that waited on it has dropped
    jae .redirmany                  ; this fetch and owns the bar now, so the
    inc byte [br_redirs]            ; generation is asked again before
    cmp ax, 307                     ; br_resolve writes it. 301/302/303 after
    jae .redirsame                  ; a POST become a GET (SPEC.md 71.17); 307
    mov byte [br_reqpost], 0        ; and 308 keep the method, which is what
.redirsame:                         ; they are for
    mov si, br_redirurl
    push ds
    pop es                          ; br_resolve reads ES:SI...
    call OSAPI_GFX_LOCK             ; ...and may paint its refusal (br_errst)
    call br_gchk
    jc .redirunlock
    call br_resolve
    jc .redirrefused
    mov si, br_loc                  ; br_ubuf IS the bar's LN_BUF, so its
    call os88line_resync            ; LN_LEN and LN_CAR follow the new text
    mov si, br_ubuf
    call br_split
    jc .redirunbad
    mov word [br_nlen], 0           ; per-fetch header state, as br_go sets it
    mov word [br_nlen_hi], 0
    mov word [br_code], 0
    mov byte [br_trunc], 0
    mov word [br_hcol], 0
    mov byte [br_hstate], 0
    mov byte [br_lstate], 0
    mov byte [br_ctstate], 0
    mov byte [br_ctpos], 0
    mov byte [br_ctlen], 0
    mov byte [br_ctseen], 0
    mov byte [br_nonhtml], 0
    mov byte [br_cttype], 0
    mov byte [br_ctparamstate], 0
    mov byte [br_httpcharsetseen], 0
    mov byte [br_http_utf8], 0
    mov word [br_redirlen], 0
    mov byte [br_redirurl], 0
    mov byte [br_nstate], BN_OPEN   ; ...and the source claim stands at its
.redirunlock:                       ; ceiling for the target
    call OSAPI_GFX_UNLOCK
    jmp .out
.redirrefused:                      ; https: and too-long said why themselves
    call OSAPI_GFX_UNLOCK           ; and stored BN_ERR; a `#frag` refuses
    cmp byte [br_nstate], BN_ERR    ; silently, and left in BN_BODY it would
    je .failchild                   ; drain the closed handle and say `Link
    mov si, br_s_redirbad           ; lost` about a redirect it declined
    jmp .fail
.redirunbad:
    call OSAPI_GFX_UNLOCK
.redirbad:
    mov si, br_s_redirbad
    jmp .fail
.redirmany:
    mov si, br_s_redirect
    jmp .fail

.framestart:                        ; [br_fin] = 5: the UI task has re-claimed
    call OSAPI_GFX_LOCK             ; the page buffers at the ceiling
    call br_gchk                    ; (br_frameclaim, SPEC.md 71.15), and the
    jc .frameun                     ; first child starts the way br_finish
    call br_framefetch              ; starts the others - under the lock br_go
    mov byte [br_fin], 0            ; runs under, so a navigation in the gap
    cmp byte [br_nstate], BN_ERR    ; cannot free beneath it; 0 for the
    jne .frameun                    ; child's own drain
    call br_frameclose              ; a refused first child closes the page
.frameun:                           ; whole, as a refused later one does
    call OSAPI_GFX_UNLOCK
    cmp byte [br_nstate], BN_ERR
    jne .out
    jmp .redraw

; --- br_qstat - NETV_STATUS on our handle. out AH = state, CX = readable ----
br_qstat:
    mov al, [br_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_STATUS
    call OSAPI_DRV_CALL
    ret

; -----------------------------------------------------------------------------
; br_abort - drop the fetch in flight so br_go can start another
; in:  UI task, gfx lock held, [br_nstate] in BN_OPEN..BN_BODY
; out: nothing; all registers preserved
;
; THE UI TASK CLOSES AND THE WORKER OPENS, and that division is the whole
; safety argument. Only br_nstep ever calls NETV_OPEN, and it is one task, so
; between this close and the next socket there is at least one pass boundary -
; at which br_gchk turns the superseded pass into a no-op. A call already
; dispatched against the old handle therefore lands on a socket that is closed
; and NOT yet reissued, which the driver refuses; there is no window in which
; a stray RECV can read the new fetch's bytes.
; -----------------------------------------------------------------------------
br_abort:
    push ax
    inc byte [br_gen]               ; FIRST, and the order is the handshake:
    mov ax, [br_srclen]             ; ...then a parse in progress is STOPPED
    mov word [br_srclen], 0         ; - every bound br_parse tests is this
    mov word [br_srclen_hi], 0
.parse:                             ; word - and waited for, because the
    cmp byte [br_fin], 3            ; caller frees its buffers next (SPEC.md
    jne .parsed                     ; 71.14). The worker holds this state through
    call OSAPI_TASK_YIELD           ; parse or final layout can finish without
    jmp short .parse                ; freeing its claims underneath the worker
.parsed:                            ; superseded and never starts
    mov [br_srclen], ax             ; back: br_load reads it as the file's size
    mov word [br_srclen_hi], 0
    call br_hclose
    xor ax, ax
    mov [br_nstate], al             ; BN_IDLE
    mov [br_nmsg], ax
    pop ax
    ret

; --- br_gchk - CF=1 if br_go started another fetch under this worker pass ----
; Preserves every register INCLUDING AX, which .open needs: the handle it is
; about to store is sitting in it.
br_gchk:
    push ax
    mov al, [br_gen]
    cmp al, [br_gen0]
    je .ok
    pop ax
    stc
    ret
.ok:
    pop ax
    clc
    ret

; --- br_hclose - give the handle back, whatever happened --------------------
br_hclose:
    push ax
    push bx
    cmp byte [br_hnd], 0
    je .out
    mov al, [br_hnd]
    mov bh, NET_CLASS
    mov bl, NETV_CLOSE
    call OSAPI_DRV_CALL
    mov byte [br_hnd], 0
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_req - compose GET or POST into br_reqb, and set br_reqn
;
; HTTP/1.0 and `Connection: close`, which together are the whole reason this
; needs no chunked decoder: a 1.0 server ends a response by closing, and
; NSK_CLOSING with nothing readable is exactly that seen from netpkg.inc.
; -----------------------------------------------------------------------------
br_req:
    push ax
    push bx
    push si
    push di
    mov di, br_reqb
    mov bx, br_reqb + BR_REQMAX - 1 ; the last byte br_rputs may write, set
                                    ; ONCE for every piece: the size above
                                    ; is derived and correct, and this is what
                                    ; keeps a longer User-Agent from quietly
                                    ; making it wrong again
    cmp byte [br_reqpost], 0
    je .get
    mov si, br_r_post
    call br_rputs
    jmp short .line
.get:
    mov si, br_s_get
    call br_rputs
.line:
    mov si, br_path
    call br_rputs
    mov si, br_s_http
    call br_rputs
    mov si, br_host
    call br_rputs
    cmp byte [br_reqpost], 0
    je .cookie
    cmp byte [br_cookievalid], 0    ; a POST's body is the tail: add the Cookie
    je .cookie                      ; header only if the tail still fits after
    mov ax, br_r_endbody - br_r_posttail + 7
    add ax, [br_un]                 ; it (header, length digits, CRLFCRLF, body)
    add ax, di
    mov si, br_cookievalue
.cklen:
    cmp byte [si], 0
    je .ckfit
    inc si
    inc ax
    jmp short .cklen
.ckfit:
    add ax, 10                      ; "\r\nCookie: "
    cmp ax, bx
    ja .aftck             ; no room: omit it, keep the body whole
.cookie:
    call br_reqcookie              ; one bounded session cookie, host-isolated
.aftck:
    cmp byte [br_reqpost], 0
    je .gettail
    mov si, br_r_posttail
    call br_rputs
    mov ax, [br_un]
    call br_rputdec
    mov si, br_r_endbody
    call br_rputs
    mov si, br_url
    call br_rputs
    jmp short .finish
.gettail:
    mov si, br_s_tail
    call br_rputs
.finish:
    sub di, br_reqb
    mov [br_reqn], di
    pop di
    pop si
    pop bx
    pop ax
    ret

; The body is at most 159 bytes, so three decimal digits cover its exact length.
; The request bound above includes this field and the existing body staging.
br_rputdec:
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    xor cx, cx
    mov bx, 10
.digits:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .digits
.emit:
    pop dx
    add dl, '0'
    mov [di], dl
    inc di
    loop .emit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_rputs - the NUL string at DS:SI to DS:DI, DI advanced ---------------
; in:  BX = the LAST offset it may write - a string that does not fit is CUT,
;      the way br_copyspan bounds its own copy, rather than written past the
;      end of the buffer
br_rputs:
    push ax
.c:
    mov al, [si]
    or al, al
    jz .out
    cmp di, bx
    ja .out
    mov [di], al
    inc di
    inc si
    jmp short .c
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_take - CX bytes at br_rxb into the source claim
;
; While BN_HEAD it is looking for the blank line and keeps only what follows;
; while BN_BODY every byte is the page. The header scan is a four-state match
; on CR LF CR LF, which is what lets it work across a chunk boundary - the
; reply's headers and its first body bytes arrive in one recv about as often
; as they do not.
; -----------------------------------------------------------------------------
br_take:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [br_srcemshnd], 0
    je .convseg
    push cx
    mov ax, [br_nlen]
    mov bx, ax
    mov cl, 14
    shr bx, cl                      ; page within the current 64 KiB group
    mov dx, [br_nlen_hi]
    mov cl, 2
    shl dx, cl
    add bx, dx
    mov cx, bx
    mov dx, [br_srcemshnd]
    xor al, al                      ; map this page into quarter zero
    mov bx, (DRVC_EMS << 8) | EMSV_MAP
    call OSAPI_DRV_CALL
    pop cx
    jc .badmap
    mov es, [br_emsseg]
    mov di, [br_nlen]
    and di, EMS_PAGE-1
    jmp short .mapped
.convpm:                            ; kern_mega (KERN-MEGA-MEMORY-PLAN MC7):
    mov dx, [br_srcseg]             ; the claim is a selector and `+ hi << 12`
    or dx, dx                       ; names some other descriptor, so the
    jz .convnone                    ; receive's own BOUND window is aimed at
    mov cx, ax                      ; the 64 KiB the chunk lands in - one
    xor ax, ax                      ; kernel call a chunk, rebased with the
    push bx                         ; claim like the segment it replaces
    mov bx, br_rwin
    call br_bwaim
    pop bx
    mov ax, es                      ; (refused: the claim itself, never a
    jmp short .convset              ; fault - br_bwaim's CF=1)
.convnone:
    xor ax, ax                      ; freed: the zero the sum would have made
    jmp short .convset
.convseg:
    push cx                         ; **THE CEILING IS THE CLAIM'S**, not the
    pop cx                          ; br_nstep has bounded this receive to the
                                    ; live source claim and never lets a chunk
; cross its 64 KiB segment alias.
    push cx                         ; CL is the shift count below, but CX is
    mov ax, [br_nlen_hi]
    cmp byte [br_mega], 0
    jne .convpm
    mov cl, 12
    shl ax, cl                      ; one segment paragraph window per 64 KiB
    add ax, [br_srcseg]
.convset:
    pop cx                          ; NETV_RECV's byte count drives LOOP below
    mov es, ax                      ; contiguous claim, MOVABLE (br_movable)
    mov dx, es                      ; but not while this worker RUNS, which
    or dx, dx                       ; pins it (inst_seg_parked) - and ZERO
    jz .done                        ; means br_go has freed it (the alias of 0
                                    ; is the kernel image and every task stack)
    mov di, [br_nlen]
.mapped:
    mov si, br_rxb
.next:
    mov al, [si]
    inc si
    mov ah, [br_gen]                ; **THE GUARD COVERS THE OPERATION AND NOT
    cmp ah, [br_gen0]               ; ITS ENTRY** (SPEC.md 71.8). br_nstep
    jne .done                       ; checks once a pass and this loop is up to
                                    ; BR_CHUNK stores long: a tick landing
                                    ; inside it lets the UI task drop the fetch
                                    ; and start another, and the rest of the
                                    ; chunk then went into the NEW fetch's
                                    ; buffer - where [br_nstate] is BN_OPEN
                                    ; again, so the stale bytes are read as its
                                    ; headers. The bytes belong to a fetch that
                                    ; no longer exists, so dropping them is the
                                    ; whole of the fix
    cmp byte [br_nstate], BN_BODY
    je .body
    call br_hdrb
    jmp short .step
.body:
    cmp byte [br_nonhtml], 0
    jne .step                        ; drain the response without a source copy
    cmp byte [br_fin], 7            ; after a refused growth, drain and discard
    je .step                        ; the rest of the body to close the socket
    mov di, [br_nlen]
    cmp word [br_srcemshnd], 0
    je .bst
    and di, EMS_PAGE-1              ; EMS: the page mapped into quarter zero
.bst:
    mov [es:di], al
    inc word [br_nlen]
    jnz .step
    inc word [br_nlen_hi]
.step:
    loop .next
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.badmap:
    mov byte [br_trunc], 1
    mov byte [br_fin], 7
    jmp short .done

; -----------------------------------------------------------------------------
; br_hdrb - one header byte: the status code, then the blank line
;
; [br_hstate] counts how much of CR LF CR LF has been seen, and [br_hcol] how
; far into the FIRST line we are, which is the only line whose contents matter:
; `HTTP/1.0 200 OK` puts the code at columns 9..11.
; -----------------------------------------------------------------------------
br_hdrb:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [br_hcol], 12
    jae .eol
    cmp word [br_hcol], 9
    jb .col
    cmp al, '0'
    jb .col
    cmp al, '9'
    ja .col
    push ax
    mov ax, [br_code]
    mov cx, 10
    mul cx
    mov bx, ax
    pop ax
    mov cl, al
    sub cl, '0'
    xor ch, ch
    add bx, cx
    mov [br_code], bx
.col:
    inc word [br_hcol]
.eol:
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
    call br_locbyte
    call br_cookiebyte              ; bounded Set-Cookie capture, no header copy
    call br_ctbyte                  ; bounded Content-Type token capture
    mov byte [br_hstate], 0         ; any other byte breaks the run
    jmp short .out
.cr:
    cmp byte [br_hstate], 2
    je .three
    mov byte [br_hstate], 1
    jmp short .out
.three:
    mov byte [br_hstate], 3
    jmp short .out
.lf:
    mov byte [br_lstate], 0
    call br_cookiefinish            ; commit a complete Set-Cookie header line
    call br_ctfinish
    mov byte [br_ctstate], 0
    mov byte [br_ctpos], 0
    mov byte [br_ctlen], 0
    mov byte [br_ctparamstate], 0
    cmp byte [br_hstate], 3
    je .blank
    cmp byte [br_hstate], 1
    jne .zero
    mov byte [br_hstate], 2
    jmp short .out
.zero:
    mov byte [br_hstate], 0
    jmp short .out
.blank:
    call br_httpstatus
    mov byte [br_nstate], BN_BODY   ; ...and everything after this is the page
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Retain a small set of useful HTTP status descriptions for HTML error pages.
; Redirects are handled before publication; 204/304 receive dedicated EOF paths.
br_httpstatus:
    push ax
    mov ax, [br_code]
    cmp ax, 400
    je .badrequest
    cmp ax, 403
    je .forbidden
    cmp ax, 404
    je .notfound
    cmp ax, 500
    je .server
    jmp short .out
.badrequest:
    mov word [br_nmsg], br_s_http400
    jmp short .out
.forbidden:
    mov word [br_nmsg], br_s_http403
    jmp short .out
.notfound:
    mov word [br_nmsg], br_s_http404
    jmp short .out
.server:
    mov word [br_nmsg], br_s_http500
.out:
    pop ax
    ret

; Match a Content-Type field one byte at a time. State survives recv boundaries;
; only the media-type token is kept, with a 31-byte bound.
br_ctbyte:
    push ax
    push bx
    push dx
    push si
    cmp byte [br_ctseen], 0
    jne .out                         ; first field wins; duplicate fields are ignored
    mov bl, [br_ctstate]
    cmp bl, 2
    je .value
    cmp bl, 1
    je .space
    cmp bl, 3
    je .param
    xor bh, bh
    mov bl, [br_ctpos]
    mov si, br_s_contenttype
    add si, bx
    mov dl, al
    cmp dl, 'A'
    jb .prefix
    cmp dl, 'Z'
    ja .prefix
    or dl, 0x20
.prefix:
    cmp dl, [si]
    jne .miss
    inc byte [br_ctpos]
    cmp byte [br_ctpos], 13
    jne .out
    mov byte [br_ctstate], 1
    jmp short .out
.space:
    cmp al, ' '
    je .out
    cmp al, 9
    je .out
    mov byte [br_ctstate], 2
.value:
    cmp al, ';'
    je .endvalue
    cmp al, ' '
    je .endvalue
    cmp al, 9
    je .endvalue
    mov bl, [br_ctlen]
    cmp bl, 31
    jae .miss
    xor bh, bh
    mov [br_cttype+bx], al
    inc byte [br_ctlen]
    jmp short .out
.endvalue:
    mov byte [br_ctstate], 3
    jmp short .out
.param:
    cmp byte [br_ctlen], 0
    je .out                          ; not a Content-Type field
    call br_ctparambyte
    jmp short .out
.miss:
    mov byte [br_ctstate], 3
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; Finalize a token only at its header-line boundary. Missing or empty fields keep
; the historical HTML assumption; known HTML media types remain renderable.
br_ctfinish:
    push ax
    push bx
    push si
    push es
    cmp byte [br_ctseen], 0
    jne .out
    cmp byte [br_ctlen], 0
    je .out
    xor bx, bx
    mov bl, [br_ctlen]
    mov byte [br_cttype+bx], 0
    mov byte [br_ctseen], 1
    push ds
    pop es
    mov si, br_cttype
    mov bx, br_s_text_html
    call br_targeteq
    jnc .html
    mov bx, br_s_xhtml
    call br_targeteq
    jnc .html
    mov byte [br_nonhtml], 1
.html:
.out:
    pop es
    pop si
    pop bx
    pop ax
    ret

; Recognise a bounded `charset=utf-8` parameter after the media type. This is
; deliberately a tiny streaming state machine: it retains no complete header
; line and can span arbitrary NETV_RECV boundaries.
br_ctparambyte:
    push ax
    push bx
    push dx
    mov dl, al
    cmp dl, 'A'
    jb .lower
    cmp dl, 'Z'
    ja .lower
    or dl, 0x20
.lower:
    mov al, [br_ctparamstate]
    cmp al, 0
    je .findc
    cmp al, 1
    je .h
    cmp al, 2
    je .a
    cmp al, 3
    je .r
    cmp al, 4
    je .s
    cmp al, 5
    je .e
    cmp al, 6
    je .t
    cmp al, 7
    je .equals
    cmp al, 8
    je .value
    cmp al, 9
    je .utf_t
    cmp al, 10
    je .utf_f
    cmp al, 11
    je .utf_dash
    cmp al, 12
    je .utf_8
    cmp al, 15
    je .quoted_u
    jmp .done
.findc:
    cmp dl, 'c'
    jne .done
    mov byte [br_ctparamstate], 1
    jmp .done
.h:
    cmp dl, 'h'
    jne .restart
    mov byte [br_ctparamstate], 2
    jmp .done
.a:
    cmp dl, 'a'
    jne .restart
    mov byte [br_ctparamstate], 3
    jmp .done
.r:
    cmp dl, 'r'
    jne .restart
    mov byte [br_ctparamstate], 4
    jmp .done
.s:
    cmp dl, 's'
    jne .restart
    mov byte [br_ctparamstate], 5
    jmp .done
.e:
    cmp dl, 'e'
    jne .restart
    mov byte [br_ctparamstate], 6
    jmp .done
.t:
    cmp dl, 't'
    jne .restart
    mov byte [br_ctparamstate], 7
    jmp .done
.equals:
    cmp dl, ' '
    je .done
    cmp dl, 9
    je .done
    cmp dl, '='
    jne .restart
    mov byte [br_ctparamstate], 8
    jmp .done
.value:
    cmp dl, ' '
    je .done
    cmp dl, 9
    je .done
    mov byte [br_httpcharsetseen], 1
    cmp dl, '"'
    je .quoted
    cmp dl, 39
    je .quoted
    cmp dl, 'u'
    jne .paramdone
    mov byte [br_ctparamstate], 9
    jmp .done
.quoted:
    mov byte [br_ctparamstate], 15
    jmp .done
.quoted_u:
    cmp dl, 'u'
    jne .paramdone
    mov byte [br_ctparamstate], 9
    jmp .done
.utf_t:
    cmp dl, 't'
    jne .paramdone
    mov byte [br_ctparamstate], 10
    jmp .done
.utf_f:
    cmp dl, 'f'
    jne .paramdone
    mov byte [br_ctparamstate], 11
    jmp .done
.utf_dash:
    cmp dl, '-'
    jne .paramdone
    mov byte [br_ctparamstate], 12
    jmp .done
.utf_8:
    cmp dl, '8'
    jne .paramdone
    mov byte [br_http_utf8], 1
    mov byte [br_ctparamstate], 13
    jmp .done
.paramdone:
    mov byte [br_ctparamstate], 13
    jmp .done
.restart:
    mov byte [br_ctparamstate], 0
    cmp dl, 'c'
    jne .done
    mov byte [br_ctparamstate], 1
.done:
    pop dx
    pop bx
    pop ax
    ret

; Recognise "Location:" at the start of a line while the bytes pass.  Only its
; value survives; retaining a 160-byte general header line would buy nothing.
br_locbyte:
    push ax
    push bx
    push cx
    push si
    mov bl, [br_lstate]
    cmp bl, 0FFh
    je .out
    cmp bl, 9
    jae .value
    xor bh, bh
    mov cl, al
    cmp cl, 'A'
    jb .cmp
    cmp cl, 'Z'
    ja .cmp
    or cl, 0x20
.cmp:
    cmp cl, [br_s_location+bx]
    jne .miss
    inc byte [br_lstate]
    cmp byte [br_lstate], 9
    jne .out
    mov word [br_redirlen], 0
    mov byte [br_redirurl], 0
    jmp .out
.value:
    cmp bl, 9
    jne .store
    cmp al, ' '
    ja .first
    jmp short .out
.first:
    inc byte [br_lstate]
.store:
    mov bx, [br_redirlen]
    cmp bx, BR_UBUF-1
    jae .out
    mov [br_redirurl+bx], al
    inc bx
    mov [br_redirlen], bx
    mov byte [br_redirurl+bx], 0
    jmp .out
.miss:
    mov byte [br_lstate], 0FFh
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; Keep one name=value pair and a bounded Path attribute from Set-Cookie.
br_cookiebyte:
    push ax
    push bx
    push cx
    mov bl, [br_ckstate]
    cmp bl, 0FFh
    je .out
    cmp bl, 12
    jae .attribute
    cmp bl, 11
    jae .value
    xor bh, bh
    mov cl, al
    cmp cl, 'A'
    jb .cmp
    cmp cl, 'Z'
    ja .cmp
    or cl, 0x20
.cmp:
    cmp cl, [br_s_setcookie+bx]
    jne .miss
    inc byte [br_ckstate]
    jmp .out
.value:
    cmp bl, 11
    jne .store
    cmp al, ' '
    jbe .out
.store:
    cmp al, ';'
    je .attrs
    mov bx, [br_cklen]
    cmp bx, BR_COOKIE_MAX
    jae .attrs
    mov [br_ckbuf+bx], al
    inc bx
    mov [br_cklen], bx
    mov byte [br_ckbuf+bx], 0
    jmp .out
.attrs:
    mov byte [br_ckstate], 12
    mov byte [br_ckattrstate], 0
    mov word [br_ckattrlen], 0
    mov byte [br_ckpathbuf], 0
    jmp .out
.attribute:
    cmp al, 13
    je .out
    cmp al, 10
    je .out
    mov bl, [br_ckattrstate]
    cmp bl, 6
    je .pathvalue
    cmp bl, 7
    je .attrdone
    cmp bl, 8
    je .pathqstart
    cmp bl, 9
    je .pathqvalue
    cmp al, ';'
    je .attrstart
    cmp bl, 0
    je .seekpath
    cmp bl, 4
    je .pathsep
    cmp bl, 5
    je .pathstart
    jmp .pathkey
.attrstart:
    mov byte [br_ckattrstate], 0
    jmp .out
.seekpath:
    cmp al, ' '
    je .out
    cmp al, 9
    je .out
    cmp al, 'A'
    jb .seeklower
    cmp al, 'Z'
    ja .seeklower
    or al, 0x20
.seeklower:
    cmp al, 'p'
    jne .out
    mov byte [br_ckattrstate], 1
    jmp .out
.pathkey:
    cmp bl, 1
    jne .key2
    mov cl, 'a'
    jmp short .keycmp
.key2:
    cmp bl, 2
    jne .key3
    mov cl, 't'
    jmp short .keycmp
.key3:
    cmp bl, 3
    jne .key4
    mov cl, 'h'
    jmp short .keycmp
.key4:
    cmp bl, 4
    jne .keybad
    jmp .pathsep
.keycmp:
    cmp al, 'A'
    jb .keylower
    cmp al, 'Z'
    ja .keylower
    or al, 0x20
.keylower:
    cmp al, cl
    jne .keybad
    inc byte [br_ckattrstate]
    jmp .out
.keybad:
    mov byte [br_ckattrstate], 0
    jmp .out
.pathsep:
    cmp al, ' '
    je .out
    cmp al, 9
    je .out
    cmp al, '='
    jne .keybad
    mov byte [br_ckattrstate], 5
    jmp .out
.pathstart:
    cmp al, ' '
    je .out
    cmp al, 9
    je .out
    cmp al, '"'
    je .pathquoted
    cmp al, '/'
    jne .attrignore
    mov byte [br_ckattrstate], 6
    mov byte [br_ckpathbuf], '/'    ; the slash is part of the path: seed it
    mov byte [br_ckpathbuf+1], 0
    mov word [br_ckattrlen], 1
    jmp .out
.pathquoted:
    mov byte [br_ckattrstate], 8
    jmp .out
.pathqstart:
    cmp al, ' '
    je .out
    cmp al, 9
    je .out
    cmp al, '/'
    jne .attrignore
    mov byte [br_ckattrstate], 9
    mov byte [br_ckpathbuf], '/'    ; ...for the quoted form too
    mov byte [br_ckpathbuf+1], 0
    mov word [br_ckattrlen], 1
    jmp .out
.pathvalue:
    cmp al, ';'
    je .pathdone
    cmp al, 13
    je .out
    cmp al, 10
    je .out
.pathstore:
    mov bx, [br_ckattrlen]
    cmp bx, BR_PATHMAX-1
    jae .pathdone
    mov [br_ckpathbuf+bx], al
    inc bx
    mov [br_ckattrlen], bx
    mov byte [br_ckpathbuf+bx], 0
    jmp .out
.pathqvalue:
    cmp al, '"'
    je .pathdone
    cmp al, ';'
    je .pathdone
    cmp al, 13
    je .pathdone
    cmp al, 10
    je .pathdone
    jmp .pathstore
.attrignore:
    mov byte [br_ckattrstate], 7
    jmp .out
.pathdone:
    mov byte [br_ckattrstate], 7
    jmp .out
.attrdone:
    cmp al, ';'
    jne .out
    mov byte [br_ckattrstate], 0
    jmp .out
.miss:
    mov byte [br_ckstate], 0FFh
.out:
    pop cx
    pop bx
    pop ax
    ret

; A Set-Cookie value is committed only at its CRLF. Never mix a partial line
; or a cookie from one host into another host's request.
br_cookiefinish:
    push ax
    push cx
    push si
    push di
    push es
    cmp byte [br_ckstate], 11
    je .candidate
    cmp byte [br_ckstate], 12
    jne .reset
.candidate:
    cmp word [br_cklen], 0
    je .reset
    push ds
    pop es
    mov si, br_ckbuf
    mov di, br_cookievalue
    mov cx, BR_COOKIE_MAX+1
    cld
    rep movsb
    mov si, br_host
    mov di, br_cookiehost
    mov cx, BR_HOSTMAX
    rep movsb
    cmp word [br_ckattrlen], 0
    je .defaultpath
    mov cx, [br_ckattrlen]
    mov [br_cookiepathlen], cx
    mov si, br_ckpathbuf
    mov di, br_cookiepath
    rep movsb
    mov byte [di], 0
    jmp short .pathsaved
.defaultpath:
    ; RFC default path: the response directory, stopping before any query.
    mov si, br_path
    xor cx, cx
    mov dx, 1
.pathscan:
    mov al, [si]
    or al, al
    jz .pathscanned
    cmp al, '?'
    je .pathscanned
    cmp al, '/'
    jne .pathnext
    mov dx, cx
    inc dx
.pathnext:
    inc si
    inc cx
    jmp short .pathscan
.pathscanned:
    mov [br_cookiepathlen], dx
    mov cx, dx
    mov si, br_path
    mov di, br_cookiepath
    rep movsb
    mov byte [di], 0
.pathsaved:
    mov byte [br_cookievalid], 1
.reset:
    mov byte [br_ckstate], 0
    mov byte [br_ckattrstate], 0
    mov word [br_cklen], 0
    mov word [br_ckattrlen], 0
    mov byte [br_ckbuf], 0
    mov byte [br_ckpathbuf], 0
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; Append Cookie only for the exact host that issued Set-Cookie. One cookie is
; retained per Browser instance; a later Set-Cookie replaces it.
br_reqcookie:
    push ax
    push bx
    push cx
    push dx
    push si
    push di                           ; request cursor, while DI compares hosts
    cmp byte [br_cookievalid], 0
    jne .check
    pop di
    jmp short .return
.check:
    mov si, br_cookiehost
    mov di, br_host
.host:
    mov al, [si]
    mov ah, [di]
    cmp al, 'A'
    jb .hostcase
    cmp al, 'Z'
    ja .hostcase
    or al, 0x20
.hostcase:
    cmp ah, 'A'
    jb .compare
    cmp ah, 'Z'
    ja .compare
    or ah, 0x20
.compare:
    cmp al, ah
    jne .no
    or al, al
    jz .match
    inc si
    inc di
    jmp .host
.match:
    call br_ckpathmatch
    jc .no
    pop di                            ; resume appending after "Host: ..."
    mov si, br_s_cookiehdr
    call br_rputs
    mov si, br_cookievalue
    call br_rputs
    jmp short .return
.no:
    pop di
.return:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Cookie Path match: exact path, slash-boundary prefix, or query boundary.
; The helper preserves registers and returns CF=0 only for an eligible request.
br_ckpathmatch:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [br_cookiepathlen]
    jcxz .no
    mov si, br_cookiepath
    mov di, br_path
    xor bx, bx
    xor dx, dx
.reqend:
    mov al, [di+bx]
    or al, al
    jz .reqlen
    cmp al, '?'
    je .reqlen
    inc bx
    inc dx
    jmp short .reqend
.reqlen:
    cmp dx, cx
    jb .no
    xor bx, bx
.compare:
    mov al, [si+bx]
    cmp al, [di+bx]
    jne .no
    inc bx
    cmp bx, cx
    jb .compare
    cmp dx, cx
    je .yes
    mov al, [di+bx]
    cmp al, '/'
    je .yes
    cmp al, '?'
    je .yes
    dec bx
    cmp byte [si+bx], '/'
    je .yes
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    stc
    ret
.yes:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; br_finish - the page is complete: parse it and put it on screen
;
; Runs on the WORKER, which is legal for everything it does: parse and layout
; touch only claimed memory, and the final draw takes the gfx lock the way any
; background painter does (SPEC.md 20.6 rule 3). Layout runs unlocked so a
; large page cannot monopolize desktop drawing. What it may NOT do is free the
; source claim - see the file header. Nor may it own the buffers' LIFETIME:
; [br_fin] = 3 says "parsing/layout", and br_abort waits on it before a
; navigation frees what either pass writes into (SPEC.md 71.14).
; -----------------------------------------------------------------------------
br_finish:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [br_fin], 3            ; main's page-buffer ownership handshake
    call br_gchk
    jc .drop                        ; (a redirect never gets here: br_nstep's
                                    ; .maybeend follows it before the hand-over)
.page:
    mov al, [br_trunc]              ; br_take's ceiling, BANKED ACROSS THE
    push ax                         ; parse/output cuts combine at publication
    mov byte [br_parse_final], 1
    call br_parse                   ; resumes at the saved source cursor
    cmp byte [br_docfull], 0
    je .parsedoc
    mov byte [br_trunc], 1          ; keep the complete formatted prefix
.parsedoc:
    pop ax                          ; source and formatted-output cuts combine
    mov byte [br_fin], 0            ; ...over: a waiting UI task may free now
    call OSAPI_GFX_LOCK             ; and the rest is decided UNDER THE LOCK,
    call br_gchk                    ; which br_go runs under: a navigation
    jc .nodraw                      ; during the parse dropped this page, and
                                    ; it must not store BN_DONE over the new
                                    ; fetch's BN_OPEN or label its history
    or al, [br_trunc]
    mov [br_trunc], al              ; keep either source or output truncation
    or [br_framecut], al
    cmp byte [br_frameappend], 0
    jne .framechild
    cmp byte [br_framecount], 0
    je .parsed
    cmp byte [br_framecut], 0
    jne .parsed                     ; a cut frameset cannot be safely composed
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, br_ptitle
    mov di, br_frametitle
    mov cx, BR_TITMAX
    rep movsb
    mov si, br_ubuf                 ; ...and the frameset's own URL, which the
    mov di, br_frameurl             ; children overwrite in the bar and
    mov cx, BR_UBUF                 ; br_frameclose puts back
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    mov word [br_doclen], 0         ; frame order is rebuilt from queue metadata
    mov byte [br_factive], 0
    mov byte [br_frameappend], 1
    mov byte [br_frameidx], 0
    mov byte [br_fin], 4            ; **THE CHILDREN NEED THE PAGE BUFFERS AT
    call OSAPI_GFX_UNLOCK           ; THE CEILING** (SPEC.md 71.15): the source
    mov bx, [br_win]                ; was cut to the frameset's own size and
    call OSAPI_WM_WAKE              ; the document claimed from it, so the
    jmp .out                        ; first child was cut at a kilobyte - and a
                                    ; worker may not claim. The UI task
                                    ; re-claims on this wake (br_frameclaim)
                                    ; and answers 5; br_nstep .framestart
                                    ; fetches the first child
.frameleave:
    cmp byte [br_nstate], BN_ERR
    jne .frameun
    call br_frameclose              ; a refused child closes the page whole
.frameun:
    call OSAPI_GFX_UNLOCK           ; **THE HOLD TAKEN AT .page COMES OFF
    cmp byte [br_nstate], BN_ERR    ; HERE**: both frame legs left through
    jne .out                        ; .out with it held, and the next lock on
    call br_nshow                   ; either task spun for ever. A refused
    jmp .out                        ; child is said once the hold is off -
                                    ; br_nshow takes the lock itself
.framechild:
    cmp byte [br_framecut], 0
    jne .framedone                  ; stop fetching after the document ceiling
    mov ax, [br_doclen]
    mov dx, [br_doclen_hi]
    or ax, dx
    jz .noend
    sub ax, 1                       ; the sentinel is at the logical tail,
    sbb dx, 0                       ; which may be above the 64 KiB seam
    call br_docmap
    cmp byte [es:di], D_END
    jne .noend
    sub word [br_doclen], 1         ; remove this fragment's D_END
    sbb word [br_doclen_hi], 0
.noend:
    mov al, [br_frameidx]
    cmp al, [br_framecount]
    jae .framedone
    call br_framefetch
    jmp .frameleave
.framedone:
    call br_frameclose
.parsed:
    cmp byte [br_framecut], 0
    je .linkwarn
    mov word [br_nmsg], br_s_trunc  ; ...and a page that was CUT says so, which
    jmp .notrunc
.linkwarn:
    cmp byte [br_linklimited], 0
    je .notrunc
    mov word [br_nmsg], br_s_linklim
.notrunc:                           ; is SPEC.md 47 rule 3: the state line is
                                    ; the only place it can, and `Ready` under
                                    ; an article that stops mid-sentence reads
                                    ; as the site being broken
    cmp byte [br_ptitle], 0         ; **THE PAGE NAMES ITS OWN HISTORY ENTRY.**
    je .notit                       ; br_hpush ran back in br_go, before a byte
    mov bx, [br_histi]              ; had arrived, so the URL is all it had to
    cmp bx, [br_histn]              ; label the slot with; the <title> is known
    jae .notit                      ; only now. A page without one keeps the
    mov si, br_ptitle               ; URL, which is why this is a test and not
    call br_hlabel                  ; an unconditional overwrite
.notit:
    mov byte [br_nstate], BN_DONE
    call br_promise                 ; ...and the page is standing still again
    call br_settitle                ; network titles arrive only after parsing
    mov byte [br_parse_started], BR_PARSE_LAYOUT
    mov byte [br_fin], 3             ; br_abort waits until layout stops touching claims
    call OSAPI_GFX_UNLOCK           ; whole-page layout must not hold the
                                    ; desktop's global drawing lock
    mov si, [br_win]
    mov word [br_cols], 0           ; force layout: page changed under same width
.layout:
    call br_measure                 ; resize during layout triggers a fresh pass
    jc .layout                      ; CF=0 only after a stable-width pass
    mov byte [br_parse_started], 2  ; final EOF state; line table is now stable
    mov byte [br_fin], 0            ; no document access before acquiring lock:
    call OSAPI_GFX_LOCK             ; a waiting abort may now safely free claims
    call br_gchk                    ; it may have superseded us during layout
    jc .nodraw
    mov word [br_top], 0            ; or not we draw: br_paint measures but
    cmp byte [br_pendingflag], 0
    je .fragmentdone
    push es
    push ds
    pop es
    mov si, br_pendingfrag
    call br_gotoanchor              ; applies other-page#fragment after layout
    pop es
    mov byte [br_pendingflag], 0
.fragmentdone:
    mov ax, [br_top]
    mov [br_ptop], ax               ; a pending fragment is already the view top
    ; does not lay out, so a reveal after a
                                    ; skipped frame would paint a stale line
                                    ; table. (A br_layout call followed here
                                    ; and laid the page out a SECOND time.)
    mov bx, [br_win]
    call OSAPI_WM_CLIP_SET          ; **A BACKGROUND PAINTER ARMS A REGION OR
    jc .nodraw                      ; PAINTS OVER THE WINDOW ON TOP OF IT**
                                    ; (SPEC.md 20.6 rule 5, SPEC.md 11.3): what
                                    ; follows is the band, the scroll bar and
                                    ; the strip at ABSOLUTE screen
                                    ; coordinates, so a reader who clicked
                                    ; another window while the page arrived got
                                    ; the whole of it drawn across that
                                    ; window's content. The clip and not
                                    ; br_nshow's OSAPI_WM_OBSCURED, because
                                    ; this is the paint that puts the finished
                                    ; page up and one covered pixel must not
                                    ; veto it; it includes the hidden test the
                                    ; rule asks for and dies at the unlock
                                    ; below
    call br_paint_band
    call br_sbar
    call br_toolbar                 ; ...AND THE STRIP, not just br_status: a
                                    ; fetch that completes is the one moment
                                    ; Back and Forward change what they are
                                    ; allowed to do, and the greying is what
                                    ; says so. br_toolbar draws the state too,
                                    ; so this replaces the br_status that was
                                    ; here rather than joining it
.nodraw:
    call OSAPI_GFX_UNLOCK
    jmp br_pop6
.out:
    jmp br_pop6
.drop:                              ; superseded before the parse began: the
    mov byte [br_fin], 0            ; buffers may already be gone. Only the
    jmp br_pop6                     ; worker sets 1, so 0 cannot hide a hand-over

; -----------------------------------------------------------------------------
; br_netclaim - the UI task's half of the hand-over (SPEC.md 71.14)
; Called from br_onwake when the worker has drained a page and set [br_fin] = 1.
; Cuts the source claim to the page, starts document/layout storage at 1 KiB
; each, then hands back to the worker with
; [br_fin] = 2. A refusal is the BN_ERR br_go used to give for the same claims.
; All registers preserved.
; -----------------------------------------------------------------------------
br_netclaim:
    push ax
    push bx
    push cx
    push dx
    cmp byte [br_frameappend], 0
    jne .claimed                     ; keep the existing page claims for frames
    mov ax, [br_srclen_hi]          ; ceil(DX:AX / 1024), without a 32-bit
    mov cl, 6                       ; divide instruction on the 8086
    shl ax, cl
    mov bx, [br_srclen]
    mov cl, 10
    shr bx, cl
    add ax, bx
    test word [br_srclen], 0x03FF
    jz .sized
    inc ax
.sized:
    cmp word [br_srcemshnd], 0
    jne .keep                        ; EMS source has no conventional claim to shrink
    mov dx, [br_srcseg]
    call br_regrow                  ; a SHRINK, which always succeeds in place
    jc .keep                        ; ...and if it did not, the ceiling stands
    mov [br_srcseg], dx
    mov [br_srckb], ax
.keep:
.claimed:
    mov byte [br_fin], 2            ; the worker parses on its next pass
    jmp short .out
.nomem:
    mov word [br_nmsg], br_s_nomem
    mov byte [br_nstate], BN_ERR
    call br_nshow
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Publish the current source extent and resume the semantic parser once its
; small charset-probe prefix is available. Source, document and link data all
; remain in the Browser's existing arenas; no body-sized scratch copy exists.
br_streamparse:
    push ax
    push bx
    push dx
    cmp byte [br_nonhtml], 0
    jne .out
    cmp byte [br_parse_started], 3  ; storage already refused this output prefix
    je .out
    cmp byte [br_fin], 7            ; growth refused: br_fin must stay 7, and the
    je .out                         ; parse below would overwrite it with 0
    mov ax, [br_nlen]
    mov [br_srclen], ax
    mov ax, [br_nlen_hi]
    mov [br_srclen_hi], ax
    or ax, ax
    jnz .parse
    cmp word [br_srclen], 2048      ; preserve the existing first-2K META sniff
    jb .out                          ; small bodies parse at final EOF
.parse:
    mov byte [br_parse_final], 0
    mov byte [br_fin], 3            ; br_abort waits until this slice parks
    call br_parse
    mov byte [br_fin], 0
.out:
    pop dx
    pop bx
    pop ax
    ret

; Reuse the source and socket for one queued frame. br_frameidx is the next
; link-arena entry; no frame-sized allocation or retained response is needed.
br_framefetch:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; Restore the base URL of the frameset before resolving relative srcs.
    push ds
    pop es
    mov si, br_framehost
    mov di, br_host
    mov cx, BR_HOSTMAX
    rep movsb
    mov si, br_framepath
    mov di, br_path
    mov cx, BR_PATHMAX
    rep movsb
    mov al, [br_frameidx]
    cmp byte [br_frameoverridevalid], 0
    je .framesrc
    cmp al, [br_frameoverrideidx]
    jne .framesrc
    ; The selected target is already an absolute, resolved URL.
    push ds
    pop es
    mov si, br_frameoverrideurl
    mov di, br_ubuf
    mov cx, BR_UBUF
    rep movsb
    mov byte [br_frameoverridevalid], 0 ; one shot even if its fetch fails
    mov si, br_ubuf
    call br_split
    jc .failed
    jmp .resolved
.framesrc:
    xor bx, bx
    mov bl, [br_frameidx]
    mov al, [br_frameurls+bx]
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [br_lnkoff+bx]
    mov es, [br_lnkseg]
    mov bx, br_s_schemes
    call br_hrefpfx
    jnc .failed                     ; HTTPS cannot be fetched by this stack
    call br_resolve
    jc .failed
.resolved:
    ; Save this frame's resolved base URL once in the existing link arena.
    ; Every anchor in the frame can then keep its short source href.
    mov ax, [br_upos]
    inc ax                              ; include the terminating NUL
    mov bx, [br_lnkw]
    add ax, bx
    cmp ax, [br_linkcap]
    jae .nobase
    mov cx, [br_upos]
    inc cx
    mov si, br_ubuf
    mov dx, [br_lnkw]
    mov di, dx
    mov es, [br_lnkseg]
    cld
    rep movsb
    mov [br_lnkw], di
    push ds
    pop es
    xor bx, bx
    mov bl, [br_frameidx]
    shl bx, 1
    mov [br_framebaseoff+bx], dx
    jmp short .baseok
.nobase:
    xor bx, bx
    mov bl, [br_frameidx]
    shl bx, 1
    mov word [br_framebaseoff+bx], 0FFFFh
.baseok:
    mov al, [br_frameidx]
    xor bx, bx
    mov bl, al
    mov byte [br_framebaseseen+bx], 0
    mov [br_framecurrent], al
    mov si, br_ubuf
    call br_split
    jc .failed
    xor bx, bx
    mov bl, [br_frameidx]
    mov ah, [br_frameflags+bx]
    test ah, 2                      ; horizontal frames share a table row
    jz .fullwidth
    cmp byte [br_factive], 0
    jne .tableopen
    mov al, D_TAB1
    call br_put
    mov byte [br_factive], 1
.tableopen:
    test ah, 1
    jz .cell
    mov al, D_ROW
    call br_put
.cell:
    mov al, D_CELL
    call br_put
    jmp .markers
.fullwidth:
    cmp byte [br_factive], 0
    je .rule
    mov al, D_TAB0
    call br_put
    mov byte [br_factive], 0
.rule:
    mov al, D_HR
    call br_put
.markers:
    call br_flushbuf
    mov word [br_nlen], 0
    mov word [br_nlen_hi], 0
    mov word [br_srclen], 0        ; the next child reuses the source claim
    mov word [br_srclen_hi], 0
    mov byte [br_parse_started], 0 ; but has its own streaming parser state
    mov byte [br_parse_final], 0
    mov byte [br_docfull], 0
    mov byte [br_skipraw], 0
    mov byte [br_skipmatch], 0
    mov byte [br_commentmode], 0
    mov byte [br_commentmatch], 0
    mov byte [br_on], 0
    mov word [br_code], 0
    mov byte [br_trunc], 0
    mov word [br_hcol], 0
    mov byte [br_hstate], 0
    mov byte [br_lstate], 0
    mov byte [br_ctstate], 0
    mov byte [br_ctpos], 0
    mov byte [br_ctlen], 0
    mov byte [br_ctseen], 0
    mov byte [br_nonhtml], 0
    mov byte [br_cttype], 0
    mov byte [br_ctparamstate], 0
    mov byte [br_httpcharsetseen], 0
    mov byte [br_http_utf8], 0
    mov word [br_redirlen], 0
    mov byte [br_redirurl], 0
    mov byte [br_redirs], 0
    inc byte [br_frameidx]
    mov byte [br_nstate], BN_OPEN
    jmp .out
.failed:
    mov word [br_nmsg], br_s_framebad
    mov byte [br_nstate], BN_ERR    ; said by the caller, once its hold is off
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_frameclose - the last child is in (or one was refused): close the page
; in:  the worker, gfx lock held, [br_frameappend] = 1
; out: the document ended, the frameset's title, host, path and bar back,
;      [br_frameappend] = 0; all registers preserved
;
; ONE TAIL FOR BOTH ENDINGS. The children resolve into br_ubuf - the bar's own
; LN_BUF - and br_split writes br_host/br_path from it, so after the last one
; all three described the LAST CHILD: Reload re-fetched that child alone with
; the frameset gone, a top-level relative link resolved against its directory,
; and the bar's LN_LEN was the length of whatever text came before. And a child
; that FAILED left the table it was a row of open and its own title in the
; caption. The frameset's own four are banked at .page's frameset entry and
; put back here, whichever way the children ended.
; -----------------------------------------------------------------------------
br_frameclose:
    push ax
    push cx
    push si
    push di
    push es
    cmp byte [br_factive], 0
    je .end
    mov al, D_TAB0
    call br_put
    mov byte [br_factive], 0
.end:
    mov al, D_END
    call br_put
    call br_flushbuf
    mov byte [br_frameappend], 0
    push ds
    pop es
    cld
    mov si, br_frametitle
    mov di, br_ptitle
    mov cx, BR_TITMAX
    rep movsb
    mov si, br_framehost
    mov di, br_host
    mov cx, BR_HOSTMAX
    rep movsb
    mov si, br_framepath
    mov di, br_path
    mov cx, BR_PATHMAX
    rep movsb
    mov si, br_frameurl
    mov di, br_ubuf
    mov cx, BR_UBUF
    rep movsb
    mov si, br_loc                  ; br_ubuf IS the bar's LN_BUF, so its
    call os88line_resync            ; LN_LEN and LN_CAR follow the text
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; --- br_nshow - the worker's own status repaint, under the lock -------------
br_nshow:
    push ax
    push bx
    push si
    call OSAPI_GFX_LOCK
    call br_promise                 ; ...ABOVE the obscured test: the promise
                                    ; is not drawing and is owed whether or not
                                    ; one pixel of us shows. This is where
                                    ; BN_ERR lands (SPEC.md 71.11)
    mov bx, [br_win]
    call OSAPI_WM_OBSCURED
    jc .un                          ; covered: the next paint owes it, and a
                                    ; background painter that drew anyway would
                                    ; paint over the window on top (SPEC.md 11.3)
    mov si, [br_win]
    call br_measure
    call br_status
.un:
    call OSAPI_GFX_UNLOCK
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_status - the one-line report under the location bar
;
; ONE OPAQUE font_run, space-padded to a fixed width (SPEC.md 6.1): it changes
; on every state edge of a fetch and a fill-then-letter pair would blank it
; each time.
;
; br_errst is the refusal's own door: SI = the message, the state BN_ERR, and
; then the report. On the UI task only - it draws without taking the lock.
; -----------------------------------------------------------------------------
br_errst:
    mov [br_nmsg], si
    mov byte [br_nstate], BN_ERR
br_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov bl, [br_nstate]
    xor bh, bh
    shl bx, 1
    mov si, [br_nnames + bx]
    cmp byte [br_nstate], BN_DONE   ; **A MESSAGE BELONGS TO A FINISHED STATE,
    jb .have                        ; NOT TO A FAILED ONE.** This tested BN_ERR
    cmp word [br_nmsg], 0           ; alone, so anything with something to say
    je .have                        ; had to claim to have failed to say it -
    mov si, [br_nmsg]               ; and File > Save As did, reporting a
                                    ; successful write as an error state. While
                                    ; a fetch is in flight the STATE is the
                                    ; interesting thing and wins; once it is
                                    ; over the reason does, because `Failed` is
                                    ; what all of them look like from outside
.have:
    mov cx, [br_swid]               ; **THE FIELD'S WIDTH IS DERIVED**, not
    jcxz .out                       ; fixed: it is right-aligned in the toolbar
                                    ; strip and shrinks when a narrow window
                                    ; leaves the buttons less room to spare.
                                    ; Zero means there is none, and the state
                                    ; is simply not drawn
    push si                         ; **THE PAD GOES IN FRONT.** The field is
    xor bx, bx                      ; right-aligned in the strip and the TEXT
.len:                               ; has to be as well, or a short state sits
    cmp byte [si], 0                ; at the field's left edge and reads as
    je .lend                        ; centred - which is what the first version
    inc si                          ; looked like
    inc bx
    jmp .len
.lend:
    pop si
    push ds                         ; the string ops write through ES, which
    pop es                          ; is not ours on either task
    cld
    mov di, br_nsline
    cmp bx, cx
    jbe .pad
    mov bx, cx                      ; longer than the field: it is clipped, and
.pad:                               ; the pad is then empty
    sub cx, bx                      ; CX = the spaces in front
    mov al, ' '
    rep stosb
    mov cx, bx                      ; ...then the state itself
    rep movsb
    mov [di], ch                    ; ...and its NUL (CX is 0 after the rep)
    mov cx, [br_spen]               ; 8-aligned by br_srect, so this takes
    mov dx, [br_tby]                ; font_run's single-store path and never
    add dx, (BR_TBH - 8) / 2        ; blanks - os88line's own finding, and this
                                    ; field is redrawn on every state change.
                                    ; The pen y is the BUTTON LABEL's, so the
                                    ; state sits on their baseline
    mov si, br_nsline
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
.out:
    jmp br_pop7

br_s_get:    db 'GET ', 0
br_r_post:   db 'POST ', 0
br_s_http:   db ' HTTP/1.0', 13, 10, 'Host: ', 0
br_s_tail:   db 13, 10, 'User-Agent: os8088 Browser 1.6c', 13, 10
             db 'Connection: close', 13, 10, 13, 10, 0
br_r_posttail: db 13, 10, 'User-Agent: os8088 Browser 1.6c', 13, 10
               db 'Connection: close', 13, 10
               db 'Content-Type: application/x-www-form-urlencoded', 13, 10
               db 'Content-Length: ', 0
br_r_endbody: db 13, 10, 13, 10, 0

br_nnames:   dw br_s_ready, br_s_look, br_s_conn, br_s_asking
             dw br_s_read, br_s_read, br_s_ready, br_s_fail
br_s_ready:  db 'Ready', 0
br_s_look:   db 'Looking up', 0
br_s_conn:   db 'Connecting', 0
br_s_asking: db 'Requesting', 0
br_s_read:   db 'Reading', 0
br_s_fail:   db 'Failed', 0
br_s_nodrv:  db 'No link driver', 0
br_s_nolink: db 'No partner', 0
br_s_refused: db 'Connection refused', 0
br_s_lost:   db 'Link lost', 0
br_s_empty:  db 'Empty reply', 0
br_s_nothtml: db 'Response is not HTML', 0
br_s_http204: db 'HTTP 204: No content', 0
br_s_http304: db 'HTTP 304: Not cached', 0
br_s_http400: db 'HTTP 400: Bad request', 0
br_s_http403: db 'HTTP 403: Forbidden', 0
br_s_http404: db 'HTTP 404: Not found', 0
br_s_http500: db 'HTTP 500: Server error', 0
br_s_badurl: db 'Not an http URL', 0
br_s_trunc:  db 'Page truncated', 0
br_s_location: db 'location:', 0
br_s_setcookie: db 'set-cookie:', 0
br_s_contenttype: db 'content-type:', 0
br_s_text_html: db 'text/html', 0
br_s_xhtml: db 'application/xhtml+xml', 0
br_s_cookiehdr: db 13, 10, 'Cookie: ', 0
br_s_crlf: db 13, 10, 0
br_s_redirect: db 'Too many redirects', 0
br_s_redirbad: db 'Redirect failed', 0
br_s_framebad: db 'Frame URL invalid', 0
br_s_linklim: db 'Links limited', 0
br_s_nomem:  db 'Not enough memory', 0
