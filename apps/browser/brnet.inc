; =============================================================================
; The browser's NETWORK half - docs/NET-STACK-PLAN.md stage D
;
; SPEC.md 62.11's socket verbs, an HTTP/1.0 GET, and the page handed to the
; parser that already existed. The rendering half of this application needed
; no change at all to gain a network, which is the claim docs/BROWSER-PLAN.md
; made when it kept fetching out of the layout work entirely.
;
; --- THE WORKER OWNS THE FETCH, AND MAY NOT OWN THE MEMORY -------------------
; SPEC.md 20.6 rule 7: a worker may not call OSAPI_MEM_*, and every NETV_*
; verb is non-blocking (netpkg.inc), so the division is forced and is the
; right one anyway:
;
;   the UI task  claims the three buffers and starts the fetch (br_go), out of
;                a menu command, a Return in the location bar, or a form's
;                Submit - all of which already run with the gfx lock held
;   the worker   opens, polls, sends the request, drains the reply into the
;                source claim, and then parses and repaints under the lock
;
; **THE SOURCE CLAIM IS KEPT UNTIL THE NEXT LOAD**, where a file load frees it
; the moment the parse is done (br_load). That is not an oversight: freeing it
; is OSAPI_MEM_FREE and the worker may not make the call. It costs BR_MAXKB
; held while a network page is on screen, and it is given back by br_free_all
; at the start of whatever is loaded next.
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
; with a length or a close. No redirects yet - a 301 shows its own body, which
; every server writes as a link, so the page says where it went and a click
; will follow it once links do.
;
; EVERYTHING OFF THE WIRE IS HOSTILE (docs/NET-STACK-PLAN.md 8.5). The only
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
    call br_claim
    jc .nomem
    mov word [br_nmsg], 0           ; ...and a new fetch owns the state line, so
                                    ; last time's verdict goes: without this a
                                    ; `Saved` would survive into the next page
                                    ; and be shown again when it finished
    cmp byte [br_nopush], 0         ; ...and the history, unless this trip IS
    jne .nohist                     ; the history: Back, Forward and Reload all
    call br_hpush                   ; come through here and none of them is a
.nohist:                            ; new place (BROWSER-PLAN 5)
    mov word [br_nlen], 0
    mov word [br_code], 0
    mov byte [br_trunc], 0          ; ...the CUT flag, which br_parse clears on
                                    ; a file load and nothing cleared on this
                                    ; path, so one truncated page would have
                                    ; reported every page after it as cut
    mov word [br_hcol], 0           ; ...and the HEADER scan's own two, which
    mov byte [br_hstate], 0         ; are per-fetch as much as the count is:
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
    mov si, br_s_badurl
    jmp short .say
.nomem:
    mov si, br_s_nomem
.say:
    mov [br_nmsg], si
    mov byte [br_nstate], BN_ERR
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
    call br_status
.out:                               ; ...and NOTHING here. The withdrawal is
                                    ; above, at the transition; the two other
                                    ; ways a fetch settles own the set, and the
                                    ; paths that never start one leave a
                                    ; promise that was already true untouched
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; br_claim - the three buffers a page needs, claimed on the UI TASK
; out: CF=1 = one of them was refused, and br_free_all has undone the others
;
; The same FOUR br_load takes, at the ceiling rather than at a size, because a
; server's Content-Length is a claim by a stranger and the page has not arrived
; yet. The fourth is br_lnkclaim's and is a routine for exactly the reason this
; comment used to say three. A refusal here is an ordinary outcome on a small machine
; (SPEC.md 50.5) and is reported, not fatal.
; -----------------------------------------------------------------------------
br_claim:
    push ax
    push bx
    push dx
    mov ax, BR_MAXKB
    mov [br_srckb], ax
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [br_srcseg], dx
    mov ax, [br_srckb]
    mov bx, BR_DOCMUL
    mul bx
    add ax, BR_DOCSLK
    cmp ax, BR_DOCMAX
    jbe .dok
    mov ax, BR_DOCMAX
.dok:
    mov [br_dockb], ax
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [br_docseg], dx
    mov ax, BR_LINEKB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [br_lseg], dx
    call br_lnkclaim                ; ...and the link arena, which this path
                                    ; did NOT claim - so every page off the
                                    ; WIRE had its anchors rendered as plain
                                    ; text while a page opened from a floppy
                                    ; linked perfectly (BROWSER-PLAN 6)
    clc
    jmp short .out
.fail:
    call br_free_all
    stc
.out:
    pop dx
    pop bx
    pop ax
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
    mov word [br_port], 80
    mov di, br_scheme               ; https:// ?
    call br_pfx
    jc .nohttps
    stc
    jmp .out
.nohttps:
    mov di, br_scheme2              ; http:// - stepped over if present
    call br_pfx
    jc .host                        ; ...NOT present: SI is already the host
    add si, 7                       ; ...present: past it. br_pfx answers CF=0
.host:                              ; for a MATCH and does not advance SI
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
.pend:
    mov byte [di], 0
    clc
    jmp short .out
.bad:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- br_pfx - does DS:SI start with the NUL prefix at DS:DI? (case-blind) ----
; out: CF=0 yes. SI is NOT advanced - the caller decides by how much.
br_pfx:
    push ax
    push bx
    push si
    push di
.c:
    mov bl, [di]
    or bl, bl
    jz .yes
    mov al, [si]
    or al, al
    jz .no
    cmp al, 'A'
    jb .cmp
    cmp al, 'Z'
    ja .cmp
    add al, 32
.cmp:
    cmp al, bl
    jne .no
    inc si
    inc di
    jmp short .c
.yes:
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

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
    je .drain
    jmp .out

.open:
    call net_find                   ; WHICH DRIVER, and is it one of ours?
                                    ; DRVC_NET is the card and DRVC_FILE the
                                    ; cable, RAMDISK.DRV shares the second, and
                                    ; net_find asks each for NETV_IDENT
    jc .nodrv                       ; (apps/os88sock.inc, SPEC.md 20.11.1)
    mov bh, NET_CLASS
    mov bl, NETV_STATE
    call OSAPI_DRV_CALL
    jc .nodrv
    test al, NSTF_SOCK
    jz .nolink
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
    call br_finish
    jmp short .out

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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    inc byte [br_gen]
    call br_hclose
    mov byte [br_nstate], BN_IDLE
    mov word [br_nmsg], 0
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
; source claim - see the file header.
; -----------------------------------------------------------------------------
br_finish:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call br_hclose
    mov ax, [br_nlen]
    mov [br_srclen], ax
    or ax, ax
    jz .empty
    mov al, [br_trunc]              ; br_take's ceiling, BANKED ACROSS THE
    push ax                         ; PARSE: br_parse clears the flag on its
    call br_parse                   ; way in and br_flushbuf may set it again,
    pop ax                          ; so the two truncations are one answer
    or al, [br_trunc]
    jz .notrunc
    mov word [br_nmsg], br_s_trunc  ; ...and a page that was CUT says so, which
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
    call OSAPI_GFX_LOCK
    call br_promise                 ; ...and the page is standing still again
    mov si, [br_win]
    mov word [br_cols], 0           ; force br_measure to relayout: the page
                                    ; changed under a band whose width did not
    call br_measure
    mov word [br_top], 0
    mov word [br_ptop], 0
    call br_layout                  ; ...and the LAYOUT runs whether or not we
                                    ; draw: br_paint measures but does not lay
                                    ; out, so a reveal after a skipped frame
                                    ; would paint a stale line table
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
    jmp short .out
.empty:
    mov si, br_s_empty
    mov [br_nmsg], si
    mov byte [br_nstate], BN_ERR
    call br_nshow
.out:
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
; -----------------------------------------------------------------------------
br_status:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
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
    mov di, br_nsline
    cmp bx, cx
    jbe .pad
    mov bx, cx                      ; longer than the field: it is clipped, and
.pad:                               ; the pad is then empty
    sub cx, bx                      ; CX = the spaces in front
    jcxz .text
    mov al, ' '
.sp:
    mov [di], al
    inc di
    loop .sp
.text:
    mov cx, bx                      ; ...then the state itself
    jcxz .term
.copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .copy
.term:
    mov byte [di], 0
    mov cx, [br_spen]               ; 8-aligned by br_srect, so this takes
    mov dx, [br_tby]                ; font_run's single-store path and never
    add dx, (BR_TBH - 8) / 2        ; blanks - os88line's own finding, and this
                                    ; field is redrawn on every state change.
                                    ; The pen y is the BUTTON LABEL's, so the
                                    ; state sits on their baseline
    mov si, br_nsline
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

br_s_get:    db 'GET ', 0
br_s_http:   db ' HTTP/1.0', 13, 10, 'Host: ', 0
br_s_tail:   db 13, 10, 'User-Agent: os8088 Browser 1.0', 13, 10
             db 'Connection: close', 13, 10, 13, 10, 0
br_scheme:   db 'https://', 0
br_scheme2:  db 'http://', 0

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
br_s_nomem:  db 'Not enough memory', 0
