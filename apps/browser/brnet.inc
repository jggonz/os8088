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
;   the UI task  claims the SOURCE buffer and starts the fetch (br_go), out
;                of a menu command, a Return in the location bar, or a form's
;                Submit - all of which already run with the gfx lock held
;   the worker   opens, polls, sends the request, drains the reply into the
;                source claim, and posts a wake
;   the UI task  cuts the source to the page and claims the other three
;                buffers from its size (br_netclaim, SPEC.md 71.14)
;   the worker   parses and repaints under the lock
;
; **THE SOURCE CLAIM IS KEPT UNTIL THE NEXT LOAD**, as a file load keeps it:
; File > Save As writes it. It is claimed at BR_MAXKB and cut to the page's own
; size by br_netclaim, and given back by br_free_all at the start of whatever
; is loaded next.
;
; --- WHAT IS AND IS NOT PARSED ------------------------------------------------
; The status line's CODE is read, because 404 and 200 are different pages and
; showing the error body without saying so is the worst available outcome. The
; headers are otherwise skipped to the blank line and **nothing is copied**:
; the byte-by-byte scan writes into the source claim only once BN_BODY is
; reached, so the page starts at offset 0 by construction. Buffering the whole
; reply and moving the body down afterwards would have been a `rep movsb` of up
; to BR_MAXKB for no gain.
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
BR_REQMAX   equ 76 + BR_PATHMAX + BR_HOSTMAX
                                    ; **DERIVED FROM THE PIECES**, br_nsline's
                                    ; reason one buffer along: it was a literal
                                    ; 200 against a request that composes 4 +
                                    ; path + 17 + host + 55 = 234 at the limits
                                    ; br_split already enforces, so a long path
                                    ; on a long host ran 34 bytes into
                                    ; br_nsline - which br_status then rewrote
                                    ; with the state, cutting the request's own
                                    ; tail off before .send ever put it on the
                                    ; wire
BR_CHUNK    equ 512                 ; one NETV_RECV, well under NET_SKMAX

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
    mov word [br_nmsg], 0
    call br_split                   ; the URL into host, port and path
    jc .badurl
    push si
    mov si, br_loc
    mov di, br_ubuf                 ; ...and the bar shows what we are getting,
    call os88line_set               ; whether it was typed there or not -
    call os88line_draw              ; **AND IT IS DRAWN HERE AND NOT BY THE
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
    mov ax, BR_MAXKB                ; claim only the source until the page arrives
    call br_srcclaim
    jc .nomem
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
    mov [br_nlen], ax
    mov [br_fin], al                ; ...the hand-over (SPEC.md 71.14)
    mov [br_code], ax
    mov [br_trunc], al              ; ...the CUT flag, which br_parse clears on
                                    ; a file load and nothing cleared on this
                                    ; path, so one truncated page would have
                                    ; reported every page after it as cut
    mov [br_hcol], ax               ; per-fetch header state
    mov [br_hstate], al
    mov byte [br_lstate], 0
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
    jz .drain                       ; task's claims, 2 claimed - parse
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
    call br_req                     ; compose GET ... into br_reqb
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
    mov al, [br_hnd]
    mov di, br_rxb
    mov cx, BR_CHUNK
    mov bh, NET_CLASS
    mov bl, NETV_RECV
    call OSAPI_DRV_CALL
    jc .retry
    call br_gchk                    ; **THE ONE THAT MATTERS**: br_go has freed
    jc .out                         ; and re-claimed the source buffer, so a
    or cx, cx                       ; superseded br_take writes into memory
    jz .maybeend                    ; that is not ours any more
    call br_take                    ; ...CX bytes into the source claim
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
    call br_hclose                  ; **THE PAGE HAS ARRIVED, AND ITS SIZE IS
    mov ax, [br_nlen]               ; A FACT** (SPEC.md 71.14): hand it to the
    mov [br_srclen], ax             ; UI task, which cuts the source to it and
    or ax, ax                       ; claims the other three buffers from it -
    jz .empty                       ; a worker may not (SPEC.md 20.6 rule 7).
    mov byte [br_fin], 1            ; The state stays BN_BODY, IN FLIGHT, so a
.kick:                              ; navigation in the gap drops this fetch
    mov bx, [br_win]                ; as it always did. Posted again on every
    call OSAPI_WM_WAKE              ; pass until answered: a wake into a full
    jmp short .out                  ; ring is refused, and the slot says kick
.empty:                             ; again
    mov si, br_s_empty
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
.redraw:
    call br_nshow
.out:
    jmp br_pop7

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
.parse:                             ; word - and waited for, because the
    cmp byte [br_fin], 3            ; caller frees its buffers next (SPEC.md
    jne .parsed                     ; 71.14). br_finish stores 3 before it
    call OSAPI_TASK_YIELD           ; checks the generation, so a worker that
    jmp short .parse                ; has not got there yet finds itself
.parsed:                            ; superseded and never starts
    mov [br_srclen], ax             ; back: br_load reads it as the file's size
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
; br_req - compose the request into br_reqb, and set br_reqn
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
                                    ; ONCE for all five pieces: the size above
                                    ; is derived and correct, and this is what
                                    ; keeps a longer User-Agent from quietly
                                    ; making it wrong again
    mov si, br_s_get
    call br_rputs
    mov si, br_path
    call br_rputs
    mov si, br_s_http
    call br_rputs
    mov si, br_host
    call br_rputs
    mov si, br_s_tail
    call br_rputs
    sub di, br_reqb
    mov [br_reqn], di
    pop di
    pop si
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
    push cx                         ; **THE CEILING IS THE CLAIM'S**, not the
    mov bx, [br_srckb]              ; constant's, which is br_flushbuf's shape
    mov cl, 10                      ; against [br_dockb]: a file load claims the
    shl bx, cl                      ; FILE's size and nothing else here knows
    dec bx                          ; that, so a fetch bounded on BR_MAXKB
    pop cx                          ; writes past a smaller block
    mov es, [br_srcseg]             ; ONCE - the claim is PINNED, so it cannot
    mov dx, es                      ; move under us - and ZERO means br_go has
    or dx, dx                       ; freed it: segment 0 is the kernel image
    jz .done                        ; and every task's stack
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
    mov di, [br_nlen]
    cmp di, bx
    jb .store
    mov byte [br_trunc], 1          ; the ceiling: the page is TRUNCATED and
    jmp short .step                 ; br_trunc is what says so on screen - it
                                    ; is set HERE as well as in br_flushbuf,
                                    ; because a reply cut at the ceiling used
                                    ; to record nothing at all and the reader
                                    ; got a page that stops mid-sentence under
                                    ; a status line reading `Ready`. A browser
                                    ; that stopped reading would leave the far
                                    ; side driving a socket nobody is draining
.store:
    mov [es:di], al
    inc word [br_nlen]
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
    mov byte [br_nstate], BN_BODY   ; ...and everything after this is the page
.out:
    pop di
    pop si
    pop dx
    pop cx
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

; -----------------------------------------------------------------------------
; br_finish - the page is complete: parse it and put it on screen
;
; Runs on the WORKER, which is legal for everything it does: the parse and the
; layout touch only claimed memory, and the draw takes the gfx lock the way any
; background painter does (SPEC.md 20.6 rule 3). What it may NOT do is free the
; source claim - see the file header. Nor may it own the buffers' LIFETIME:
; [br_fin] = 3 says "parsing", and br_abort stops the parse and waits on it
; before a navigation frees what the parse writes into (SPEC.md 71.14).
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
    jc .drop
    mov ax, [br_code]
    cmp ax, 301
    je .redirect
    cmp ax, 302
    je .redirect
    cmp ax, 303
    je .redirect
    cmp ax, 307
    je .redirect
    cmp ax, 308
    jne .page
.redirect:
    cmp byte [br_redirurl], 0
    je .redirfail
    cmp byte [br_redirs], 3
    jae .redirfail
    inc byte [br_redirs]
    mov si, br_redirurl
    push ds
    pop es
    call OSAPI_GFX_LOCK
    call br_resolve
    pushf
    call OSAPI_GFX_UNLOCK
    popf
    jc .redirresolved
    mov si, br_ubuf
    call br_split
    jc .redirfail
    mov word [br_nlen], 0
    mov word [br_code], 0
    mov byte [br_trunc], 0
    mov word [br_hcol], 0
    mov byte [br_hstate], 0
    mov byte [br_lstate], 0
    mov word [br_redirlen], 0
    mov byte [br_redirurl], 0
    mov byte [br_nstate], BN_OPEN
    mov byte [br_fin], 0
    jmp .out
.redirresolved:
    mov byte [br_fin], 0
    call br_nshow
    jmp .out
.redirfail:
    cmp byte [br_redirs], 3
    jb .redirectbad
    mov word [br_nmsg], br_s_redirect
    jmp .redirectsay
.redirectbad:
    mov word [br_nmsg], br_s_redirbad
.redirectsay:
    mov byte [br_nstate], BN_ERR
    mov byte [br_fin], 0
    call br_nshow
    jmp .out
.page:
    mov al, [br_trunc]              ; br_take's ceiling, BANKED ACROSS THE
    push ax                         ; PARSE: br_parse clears the flag on its
    call br_parse                   ; way in and br_flushbuf may set it again,
    pop ax                          ; so the two truncations are one answer
    mov byte [br_fin], 0            ; ...over: a waiting UI task may free now
    call OSAPI_GFX_LOCK             ; and the rest is decided UNDER THE LOCK,
    call br_gchk                    ; which br_go runs under: a navigation
    jc .nodraw                      ; during the parse dropped this page, and
                                    ; it must not store BN_DONE over the new
                                    ; fetch's BN_OPEN or label its history
    or al, [br_trunc]
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
    pop es
    pop di
    pop si
    pop cx
    mov word [br_doclen], 0         ; frame order is rebuilt from queue metadata
    mov byte [br_factive], 0
    mov byte [br_frameappend], 1
    mov byte [br_frameidx], 0
    call br_framefetch
    jmp .out
.framechild:
    cmp byte [br_framecut], 0
    jne .framedone                  ; stop fetching after the document ceiling
    mov bx, [br_doclen]
    or bx, bx
    jz .noend
    dec bx
    mov es, [br_docseg]
    cmp byte [es:bx], D_END
    jne .noend
    dec word [br_doclen]            ; remove this fragment's D_END
.noend:
    mov al, [br_frameidx]
    cmp al, [br_framecount]
    jae .framedone
    call br_framefetch
    jmp .out
.framedone:
    cmp byte [br_factive], 0
    je .frameend
    mov al, D_TAB0
    call br_put
    mov byte [br_factive], 0
.frameend:
    mov al, D_END
    call br_put
    call br_flushbuf
    mov byte [br_frameappend], 0
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, br_frametitle
    mov di, br_ptitle
    mov cx, BR_TITMAX
    rep movsb
    pop es
    pop di
    pop si
    pop cx
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
    mov si, [br_win]
    mov word [br_cols], 0           ; force br_measure to relayout: the page
                                    ; changed under a band whose width did not
    call br_measure                 ; ...and THAT is the layout, run whether
    mov word [br_top], 0            ; or not we draw: br_paint measures but
    mov word [br_ptop], 0           ; does not lay out, so a reveal after a
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
; Cuts the source claim to the page and claims the document, the line table and
; the link arena from that size, then hands back to the worker with
; [br_fin] = 2. A refusal is the BN_ERR br_go used to give for the same claims.
; All registers preserved.
; -----------------------------------------------------------------------------
br_netclaim:
    push ax
    push cx
    push dx
    cmp byte [br_frameappend], 0
    jne .claimed                     ; keep the existing page claims for frames
    mov ax, [br_srclen]             ; br_load's own sizing: KB, plus one
    mov cl, 10
    shr ax, cl
    inc ax
    mov dx, [br_srcseg]
    call OSAPI_MEM_REGROW           ; a SHRINK, which always succeeds in place
    jc .keep                        ; ...and if it did not, the ceiling stands
    mov [br_srcseg], dx
    mov [br_srckb], ax
.keep:
    call br_claimrest
    jc .nomem
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
    mov word [br_code], 0
    mov byte [br_trunc], 0
    mov word [br_hcol], 0
    mov byte [br_hstate], 0
    mov byte [br_lstate], 0
    mov word [br_redirlen], 0
    mov byte [br_redirurl], 0
    mov byte [br_redirs], 0
    inc byte [br_frameidx]
    mov byte [br_nstate], BN_OPEN
    jmp .out
.failed:
    mov word [br_nmsg], br_s_framebad
    mov byte [br_nstate], BN_ERR
    call br_nshow
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
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
br_s_http:   db ' HTTP/1.0', 13, 10, 'Host: ', 0
br_s_tail:   db 13, 10, 'User-Agent: os8088 Browser 1.3', 13, 10
             db 'Connection: close', 13, 10, 13, 10, 0

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
br_s_badurl: db 'Not an http URL', 0
br_s_trunc:  db 'Page truncated', 0
br_s_location: db 'location:', 0
br_s_redirect: db 'Too many redirects', 0
br_s_redirbad: db 'Redirect failed', 0
br_s_framebad: db 'Frame URL invalid', 0
br_s_linklim: db 'Links limited', 0
br_s_nomem:  db 'Not enough memory', 0
