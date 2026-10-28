; =============================================================================
; os8088 - apps/waverack/wrrec.inc - capture: the take (SPEC.md 123.7, 123.8)
;
; THE CARD WRITES A RING OF OURS AND THE UI TASK WRITES THE FILE. Verb 4 with
; SND_OPENF_EXT (SPEC.md 34.6.4): a 32 KB ring (16 KB tier), its two control
; words and the gain table in one OSAPI_MEM_CLAIM_DMA_HI claim; each 512-byte
; block IRQ adds the block to a free-running count in the claim. Nothing of
; the driver's sits between the card and the take, and no task of ours has to
; run for the card to go on capturing - it goes on through every int 13h the
; machine makes, which is what a disk write with [sch_lock] held needs.
;
; THE WORKER COPIES AND THE UI TASK WRITES. Every tick the worker (wc_feed,
; the play's task - wt_hire) folds the count into 32 bits and COPIES what was
; captured out of the ring into STAGING through INPUT GAIN's table, noting
; the extremes for the meter and the live strip in the same pass; it touches
; no file and draws nothing (SPEC.md 20.6 rule 7), and it runs through a menu
; held down, another window's paint and a package load - the UI task's
; absences, measured at 3.4 s and 4.7 s against a ring of 2.95 s. The UI's
; tick (wc_tick, the window's one-shot timer) takes the copy for itself
; while it runs, gives it back, and writes the full units - one plain
; OSAPI_FILE_WRITE_SEQ each (a crash keeps every write). The units are
; max(16 KB, a cluster) on a fixed disk and 32 KB on a floppy (the commit a
; write pays there is ~0.8 s, so a bigger write is what keeps 8 kHz up; a
; floppy cannot keep 11 kHz up at all - wc_go refuses it), 64 KB of them,
; so every append but the last is whole clusters and a 32 KB cluster is
; written from staging, never from the ring in place.
;
; THE FILE is VOICEnnn.WAV in the folder the instance stands in: a header
; ONE CLUSTER long - RIFF, WAVE, fmt, JUNK to 8 bytes short, data - made with
; OSAPI_FILE_WRITE at Record, then the appends, then at Stop the header
; cluster again, with the sizes, by one OSAPI_FILE_WRITE_AT at offset 0
; (the offset is a cluster multiple; data's size is at the cluster's end).
; Until then RIFF and data
; say 0, which SPEC.md 123.4's reader opens as an UNFINISHED take - the
; recovery path. WAVERACK.TAK in the boot volume's SYSTEM\APPDATA names the
; take while it runs (os88data.inc), and is marked done at Stop.
; =============================================================================

WC_RINGKB   equ 32                  ; the record ring...
WC_RINGKB2  equ 16                  ; ...and its tier (each + 1 KB: the
                                    ; words and the gain table)
WC_BLKSH    equ 2                   ; the card's block: 2,048 >> 2 = 512
WC_TABOFS   equ 16                  ; the gain table, past the ring's words
WC_OUTKB    equ 16                  ; the output claim on a fixed disk (or a
WC_FLOPKB   equ 32                  ; cluster, if larger); on a floppy (the
                                    ; soak row's: 24 KB units overran at 8 kHz
                                    ; where 32 KB kept up, the commit being
                                    ; most of a floppy write)
WC_MINSEC   equ 2                   ; Record wants this many seconds of room
WC_FLOPHZ   equ 8000                ; a floppy's top take rate
WC_NOINPUT  equ 18                  ; ticks with nothing captured: no input
WC_MAXW     equ 2                   ; writes a tick, at most
WC_NU       equ 4                   ; staging units of 16 KB (2 if larger)
WC_FPP      equ 1024                ; frames a column of the live strip
WC_MAXCOL   equ 640                 ; the strip's columns, at most
WC_MKSZ     equ 24                  ; WAVERACK.TAK's bytes...
WC_MKSEG    equ 21                  ; ...the recorder's segment at +21

; -----------------------------------------------------------------------------
; wc_active - CF = 0 while a take is held: RECORDING or REC_PAUSED.
; keeps every register
; -----------------------------------------------------------------------------
wc_active:
    push ax
    mov al, [wt_state]
    sub al, WT_REC
    cmp al, 2
    pop ax
    cmc
    ret

; =============================================================================
; THE COMMANDS (the UI task; gfx lock held)
; =============================================================================

; -----------------------------------------------------------------------------
; wc_go - RECORD from IDLE (SPEC.md 123.7): the document emptied (an edited
;         one was asked about first: WRQ_REC), the take's folder, its
;         cluster and room said (`C: 312 KB free - 28 s`), its name, the
;         claims, the gain table, the file with its one-cluster header, the
;         recovery marker, and the stream. A refusal leaves no file and says
;         why. in: BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_go:
    mov ax, [wr_caps]
    mov si, wr_s_nocap
    test ax, SND_CAP_PCM_IN
    jnz .cap
    jmp .say
.cap:
    mov bl, [wr_rate]
    xor bh, bh
    shl bx, 1
    mov ax, [wr_rates + bx]
    mov [wc_rate], ax
    call OSAPI_FILE_HERE            ; the folder the instance stands in
    mov [wc_dir], dx
    mov [wc_vol], bl
    call OSAPI_FILE_DFREE           ; DX:AX = free, BX = sectors a cluster
    jnc .df
    mov si, wr_s_wfail
    jmp .say
.df:
    mov [wc_free], ax
    mov [wc_free+2], dx
    mov si, ww_e13                  ; 'Cluster too large': past 32 KB
    or bx, bx
    jnz .c0
    jmp .say
.c0:
    cmp bx, WO_MAXCLU / 512
    jbe .c1
    jmp .say
.c1:
    mov cl, 9
    shl bx, cl
    mov [wc_clu], bx
    mov ax, [wc_rate]               ; the room: two seconds, the header and a
    mov cx, WC_MINSEC               ; cluster beyond
    mul cx
    add ax, bx
    adc dx, 0
    add ax, bx
    adc dx, 0
    cmp dx, [wc_free+2]
    jb .room
    ja .full
    cmp ax, [wc_free]
    jbe .room
.full:
    mov si, wr_s_dfull              ; 'Not enough disk space'
    jmp .say
.room:
    mov byte [wc_fixed], 0          ; THE MEDIUM: a floppy's write blocks the
    mov al, [wc_vol]                ; copy for 2.4-2.9 s (MartyPC, 32 KB),
    call OSAPI_VOL_KIND             ; which a ring of 2.95 s at 11,025 Hz does
    jc .flop                        ; not outlast: a floppy takes 8,000 Hz
    cmp al, VK_FIXED                ; (4.1 s), SPEC.md 123.7
    jne .flop
    mov byte [wc_fixed], 1
    jmp short .kind
.flop:
    mov si, wc_s_flop               ; 'Floppy: use 8,000 Hz'
    cmp word [wc_rate], WC_FLOPHZ
    jbe .kind
    jmp .say
.kind:
    call wc_name                    ; VOICEnnn.WAV, the first free
    jnc .named
    jmp .say
.named:
    call wo_new                     ; the document's claims back: the take
    call wc_claims                  ; becomes the document at Stop
    jnc .claimed
    jmp .say
.claimed:
    call wc_gbuild                  ; INPUT GAIN's table, into the ring claim
    call wc_create                  ; the file, its header cluster
    jnc .made
    jmp .free
.made:
    mov byte [wc_mstate], 1         ; the marker: a take is running
    call wc_mark
    xor ax, ax                      ; the take at nought
    mov di, wc_zero0
    mov cx, wc_zero1 - wc_zero0
    push es
    push ds
    pop es
    cld
    rep stosb
    pop es
    mov word [wc_cleft], WC_FPP
    mov word [wc_cmin], 0x00FF      ; (no extremes yet: min FFh, max 00h)
    mov word [wc_mmin], 0x00FF
    mov byte [wc_dpage], 0xFF       ; (the strip's lanes owed a clear)
    call wt_hire                    ; THE WORKER: the copy between the UI's
    mov si, wr_s_notask             ; ticks - through a menu held, another
    jc .nowk                        ; window's paint (SPEC.md 123.7)
    call wc_open                    ; the stream (CF: SI = why)
    jnc .rec
.nowk:
    push si
    call wc_unmake                  ; nothing recorded: no file left, and
    mov byte [wc_mstate], 0         ; no take for the marker to name
    call wc_mark
    pop si
    jmp short .free
.rec:
    mov byte [wt_state], WT_REC
    call wt_dark                    ; the meter at nought...
    mov byte [wr_mwho], 1           ; ...LEVEL  REC
    call OSAPI_GET_TICKS
    mov [wc_t0], ax
    mov [wt_mtick], ax
    call wo_gen1                    ; the LCD's name: the take's
    call we_cmoved
    call wr_arm
    call wc_roomsay                 ; 'C: 312 KB free - 28 s'
    clc
    ret
.free:
    push si
    call wc_release
    pop si
.say:
    call wr_say
    stc
    ret

; -----------------------------------------------------------------------------
; wc_pause - RECORDING -> REC_PAUSED: the copy the UI's (the worker stood
;            aside), the stream stopped (verb 2), every byte it captured
;            copied out and every full unit written; the partial unit kept
;            for the next stream. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_pause:
    call wc_own
    call wc_close                   ; the count is final once closed
    call wc_drain                   ; CF: a write failed, or the disk lagged
    jnc .held
    mov si, [wc_why]                ; (the take ends on it, kept)
    jmp wc_end
.held:
    mov byte [wc_uiown], 0
    mov byte [wt_state], WT_RPAUSE
    call wt_dark
    call we_cmoved
    mov si, wr_s_paused
    jmp wr_say

; -----------------------------------------------------------------------------
; wc_resume - REC_PAUSED -> RECORDING: a new stream, appending to the same
;             file: its count starts at 0 and so does the ring's place, and
;             no byte of the first stream is lost or doubled across it.
;             clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_resume:
    call wc_open
    jnc .go
    jmp wc_end                      ; refused: the take ends, kept (SI = why)
.go:
    mov byte [wt_state], WT_REC
    call OSAPI_GET_TICKS
    mov [wc_t0], ax
    call wr_arm
    jmp wc_roomsay

; -----------------------------------------------------------------------------
; wc_stop - Stop (and the window's close, with [wc_noopen]): the take ends,
;           kept. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_stop:
    xor si, si
    ; (falls into wc_end)

; -----------------------------------------------------------------------------
; wc_end - THE TAKE ENDS (SPEC.md 123.7): the copy the UI's for good, the
;          stream closed - before the ring is freed - every byte captured
;          copied out and written (the partial unit last), the sequence
;          closed, the header patched with the sizes, the claims given
;          back, the marker marked done; then the take opened as the
;          document, saved and not dirty - or, with nothing on the disk,
;          deleted. SI = what the status bar says when the take is open (0:
;          the open's own), [wc_noopen] = 1 the window is closing.
;          clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_end:
    mov [wc_why], si
    call wc_own
    call wc_close
    cmp byte [wc_dead], 0           ; a write already failed: what is on the
    jne .written                    ; disk is the take
    call wc_drain                   ; (CF: an overrun - what is copied out is
    cmp byte [wc_dead], 0           ; good, and written - or a write failed)
    jne .written
    call wc_wlast                   ; the partial unit, any length
.written:
    call wc_seqend                  ; the stream's sequence closed
    call wc_patch                   ; the sizes, at offset 0
    call wc_release                 ; the stream is closed: the ring may go
    mov byte [wc_uiown], 0
    mov byte [wc_mstate], 0         ; the marker: done
    call wc_mark
    mov byte [wt_state], WT_IDLE
    call wt_quiet
    mov ax, [wc_wrote]
    or ax, [wc_wrote+2]
    jnz .some
    call wc_unmake                  ; nothing on the disk: no file
    cmp byte [wc_noopen], 0
    jne .out
    mov si, [wc_why]
    or si, si
    jnz .say
    mov si, wc_s_empty
.say:
    call wo_moved                   ; the empty document again: NO FILE, and
    jmp wr_say                      ; the lanes the strip cleared, redrawn
.some:
    cmp byte [wc_noopen], 0
    jne .out
    call wc_stand                   ; THE TAKE IS THE DOCUMENT (wo_open in
    mov si, wc_name0                ; its folder): saved, not dirty
    mov di, wo_nbuf
    mov cx, 13
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    mov bx, [wr_win]
    call wo_open                    ; (the scan runs; it says Ready, or...)
    jc .out
    cmp word [wc_why], 0            ; ...the reason the take ended, when it
    je .out                         ; ended on one
    mov byte [ww_note], N_TAKE
.out:
    ret

; wc_drain - a CLOSED stream's every captured byte copied out (wc_pull) and
; every full unit written, until the ring is empty: the units are the copy's
; room, so a ring fuller than they are takes rounds. The UI's, owning the
; copy. out: CF = 1 the take must end ([wc_why]: an overrun, or a write that
; failed - [wc_dead]). clobbers AX, BX, CX, DX, SI, DI
wc_drain:
    call wc_pull                    ; CF: the reader was a ring behind
    jc .over
    call wc_wfull                   ; CF: a write failed
    jc .fail
    mov ax, [wc_rcap]               ; anything left in the ring?
    cmp ax, [wc_rrd]
    jne wc_drain
    mov ax, [wc_rcap+2]
    cmp ax, [wc_rrd+2]
    jne wc_drain
    clc
    ret
.over:
    call wc_wfull                   ; what was copied is good: written
    jc .fail
    call wc_overmsg
.fail:
    stc
    ret

; wc_overmsg - [wc_why] = 'Overrun: kept to 1:23'. clobbers AX, CX, DX, SI, DI
wc_overmsg:
    mov di, wc_msg
    mov si, wc_s_over
    call wr_put
    call wc_mmss
    mov byte [di], 0
    mov word [wc_why], wc_msg
    ret

; =============================================================================
; THE TICK (the window's one-shot timer while RECORDING; gfx lock held)
; =============================================================================

; -----------------------------------------------------------------------------
; wc_tick - the copy taken from the worker for the tick: the count folded;
;           the card dead, no input after a second, or an overrun end the
;           take; what is captured copied out; then - the worker free again,
;           to copy in the gaps between a write's int 13h calls - the full
;           units written (WC_MAXW at most); the meter, the time and the
;           strip. Re-arms itself. in: BX = the window.
;           clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_tick:
    call wc_own
    call wc_fold                    ; CF: the card stopped (the watchdog)
    jnc .f
    mov byte [wc_cdead], 1
.f:
    mov ax, [wc_rcap]               ; anything ever captured? (no input: none
    or ax, [wc_rcap+2]              ; in a second - or a card that stopped
    or ax, [wc_frames]              ; before a byte came, which is what a
    or ax, [wc_frames+2]            ; card that captures nothing does: QEMU's
    cmp byte [wc_cdead], 0          ; SB16, no block IRQ ever)
    je .live
    mov si, wt_s_cdead              ; 'Sound card stopped'
    or ax, ax
    jnz .end
    mov si, wc_s_noin
.end:
    jmp wc_end
.live:
    or ax, ax
    jnz .some
    call OSAPI_GET_TICKS
    sub ax, [wc_t0]
    cmp ax, WC_NOINPUT
    jb .unown
    mov si, wc_s_noin               ; 'No input from the card'
    jmp wc_end
.some:
    cmp byte [wc_over], 0           ; the worker found the disk lagging
    jne .over
    call wc_pull                    ; CF: the reader was a ring behind
    jnc .unown
.over:
    call wc_overmsg
    mov si, [wc_why]
    jmp wc_end
.unown:
    mov byte [wc_uiown], 0          ; the worker may copy again...
    call wc_wfull                   ; ...while the units are written
    jnc .shown
    mov si, [wc_why]                ; (a write failed: said, and ended)
    jmp wc_end
.shown:
    call wc_meter
    call we_cmoved
    jmp wr_arm

; =============================================================================
; THE COPY: the worker's pass and the UI's, never both at once (wt_feed's and
; wt_topup's handshake: [wc_uiown] first, then the worker's [wc_mixing] waited
; out). The units are a single-producer, single-consumer queue: the copier
; fills [wc_putu] and alone moves it; the UI writes [wc_getu] and alone moves
; it; [wc_nfull] is the only word both change, and each change is ONE inc or
; dec, which no task switch can split.
; =============================================================================

; wc_feed - THE WORKER'S PASS (worker context, no file, no drawing): the count
; folded and what is captured copied out - unless the UI owns the copy, the
; stream is closed, or an overrun has stopped it. clobbers AX, BX, CX, DX,
; SI, DI
wc_feed:
    mov byte [wc_mixing], 1         ; FIRST - before any guard
    cmp byte [wc_uiown], 0
    jne .out
    cmp byte [wc_sopen], 0
    je .out
    cmp byte [wc_over], 0
    jne .out
    call wc_fold                    ; CF: the card stopped - the UI's to say
    jnc .pull
    mov byte [wc_cdead], 1
    jmp short .out
.pull:
    call wc_pull                    ; (CF: an overrun - [wc_over] says it)
.out:
    mov byte [wc_mixing], 0         ; LAST
    ret

; wc_own - the copy is the UI's: its flag first, then the worker's pass (a
; ring at most, bounded) waited out. keeps everything but flags
wc_own:
    mov byte [wc_uiown], 1
.w:
    cmp byte [wc_mixing], 0
    je .ret
    call OSAPI_TASK_YIELD
    jmp short .w
.ret:
    ret

; -----------------------------------------------------------------------------
; wc_fold - the ring's count, free-running 16-bit, folded into [wc_rcap]; the
;           stream's state asked (verb 3). The copier's (worker or UI).
;           out: CF = 1 the card stopped (ENDED or STALE). clobbers AX, BX, DX
; -----------------------------------------------------------------------------
wc_fold:
    cmp byte [wc_sopen], 0
    je .ok
    push es
    mov es, [wc_rseg]
    mov bx, [wc_rl]
    mov ax, [es:bx+SND_EXT_CONS]
    pop es
    mov dx, ax
    sub ax, [wc_lastc]
    mov [wc_lastc], dx
    add [wc_rcap], ax
    adc word [wc_rcap+2], 0
    mov al, 3
    mov ah, [wc_hand]
    call OSAPI_SND_STREAM           ; AX = the state
    cmp ax, SND_ST_ENDED
    je .dead
    cmp ax, SND_ST_STALE
    je .dead
.ok:
    clc
    ret
.dead:
    stc
    ret

; -----------------------------------------------------------------------------
; wc_pull - everything captured and not yet copied: an OVERRUN first (the
;           reader within a block of a ring behind: the block the card is
;           filling is the ring's oldest, so the take stops at the last good
;           byte - [wc_over] = 1, CF), then copied in chunks through the gain
;           table into the unit being filled, while a unit is free. The
;           worker's or the UI's, never both. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_pull:
    mov ax, [wc_rcap]               ; the lag: captured - copied, this stream
    mov dx, [wc_rcap+2]
    sub ax, [wc_rrd]
    sbb dx, [wc_rrd+2]
    jnz .over
    cmp ax, [wc_lagmax]             ; (the ring's high water, for the record)
    jbe .lg
    mov [wc_lagmax], ax
.lg:
    mov cx, [wc_rl]                 ; (the block the card is filling is the
    sub cx, 2048 >> WC_BLKSH        ; ring's oldest: a ring less a block)
    cmp ax, cx
    ja .over
    or ax, ax
    jz .done
    mov cl, [wc_nfull]              ; a unit free to fill?
    cmp cl, [wc_nu]
    jae .done
    mov cx, ax                      ; a chunk: at most the lag, to the ring's
    mov bx, [wc_rrd]                ; seam, the unit's room, and the live
    and bx, [wc_rmask]              ; column's frames left
    mov si, bx                      ; SI = the ring's offset
    mov ax, [wc_rl]
    sub ax, bx
    cmp cx, ax
    jbe .c1
    mov cx, ax
.c1:
    mov ax, [wc_osz]
    sub ax, [wc_puto]
    cmp cx, ax
    jbe .c2
    mov cx, ax
.c2:
    cmp cx, [wc_cleft]
    jbe .c3
    mov cx, [wc_cleft]
.c3:
    call wc_copy                    ; CX bytes from ring SI, extremes noted
    add [wc_rrd], cx
    adc word [wc_rrd+2], 0
    add [wc_frames], cx
    adc word [wc_frames+2], 0
    sub [wc_cleft], cx
    jnz .col
    call wc_colend                  ; a column of the strip is done
.col:
    add cx, [wc_puto]
    cmp cx, [wc_osz]
    jb .part
    xor cx, cx                      ; the unit is full: the next one...
    mov al, [wc_putu]
    inc ax
    cmp al, [wc_nu]
    jb .nx
    xor al, al
.nx:
    mov [wc_putu], al
    mov [wc_puto], cx
    inc byte [wc_nfull]             ; ...and it is the UI's to write (ONE inc)
    jmp wc_pull
.part:
    mov [wc_puto], cx
    jmp wc_pull
.done:
    clc
    ret
.over:
    mov byte [wc_over], 1
    stc
    ret

; -----------------------------------------------------------------------------
; wc_copy - CX (1..) bytes from the ring at SI into the unit being filled, at
;           [wc_puto], each through INPUT GAIN's table (in the ring's claim,
;           so one xlat), the extremes of what is WRITTEN noted for the meter
;           and the strip. The common byte falls through both compares. The
;           ring's bytes are the card's own, which the table only maps: unity
;           is the identity. keeps CX
; -----------------------------------------------------------------------------
wc_copy:
    push cx
    push si
    mov di, [wc_puto]
    mov bl, [wc_putu]
    xor bh, bh
    shl bx, 1
    mov ax, [wc_useg + bx]
    mov bx, [wc_rl]
    add bx, WC_TABOFS               ; BX = the table, in the ring's segment
    mov dx, 0x00FF                  ; DH = the largest, DL = the smallest
    push ds
    push es
    mov es, ax
    mov ds, [wc_rseg]
    cld
.c:
    lodsb
    xlatb
    stosb
    cmp al, dl
    jb .lo
.l1:
    cmp al, dh
    ja .hi
.h1:
    loop .c
    pop es
    pop ds
    cmp dl, [wc_cmin]               ; folded into the column's and the
    jae .d                          ; meter's
    mov [wc_cmin], dl
.d:
    cmp dh, [wc_cmax]
    jbe .e
    mov [wc_cmax], dh
.e:
    cmp dl, [wc_mmin]
    jae .f
    mov [wc_mmin], dl
.f:
    cmp dh, [wc_mmax]
    jbe .g
    mov [wc_mmax], dh
.g:
    pop si
    pop cx
    ret
.lo:
    mov dl, al
    jmp short .l1
.hi:
    mov dh, al
    jmp short .h1

; wc_colend - a column of the live strip is whole: its extremes stored (a
; page of WC_MAXCOL at most, the lanes' width), the next begun; a full page
; starts a new one ([wc_page] moves: the UI clears the lanes when it sees
; it). The copier's - it touches nothing the drawing owns. keeps everything
; but flags
wc_colend:
    push ax
    push bx
    mov word [wc_cleft], WC_FPP
    mov bx, [wc_ncol]
    cmp bx, [we_ww]                 ; the page is full: a new one
    jae .page
    cmp bx, WC_MAXCOL
    jb .put
.page:
    xor bx, bx
    mov [wc_ncol], bx
    inc byte [wc_page]
.put:
    shl bx, 1
    mov ax, [wc_cmin]
    mov [wc_cols + bx], ax          ; (min, max) - the byte order of cmin
    inc word [wc_ncol]              ; (published last: ONE inc)
    mov word [wc_cmin], 0x00FF
    pop bx
    pop ax
    ret

; =============================================================================
; THE WRITES (the UI task)
; =============================================================================

; wc_wfull - the full units written in order (WC_MAXW while the stream runs;
; all of them once it is closed). out: CF = 1 a write failed ([wc_dead],
; [wc_why]). clobbers AX, BX, CX, DX, SI, DI
wc_wfull:
    mov byte [wc_nw], WC_MAXW
.again:
    cmp byte [wc_nfull], 0
    je .done
    cmp byte [wc_sopen], 0          ; a running stream writes WC_MAXW a tick;
    je .w                           ; a closed one, all it has
    dec byte [wc_nw]
    js .done
.w:
    mov bl, [wc_getu]
    xor bh, bh
    shl bx, 1
    mov ax, [wc_useg + bx]
    mov cx, [wc_osz]
    call wc_write                   ; CF: failed
    jc .fail
    mov al, [wc_getu]               ; the next unit...
    inc ax
    cmp al, [wc_nu]
    jb .nx
    xor al, al
.nx:
    mov [wc_getu], al
    dec byte [wc_nfull]             ; ...and this one free (ONE dec)
    jmp short .again
.done:
    clc
.fail:
    ret

; wc_wlast - the unit being filled, its [wc_puto] bytes: the take's last
; write, any length (the copier stood aside for good). out: CF as wc_write.
; clobbers AX, BX, CX, DX, SI, DI
wc_wlast:
    mov cx, [wc_puto]
    mov bl, [wc_putu]
    xor bh, bh
    shl bx, 1
    mov ax, [wc_useg + bx]
    call wc_write
    jc .out
    mov word [wc_puto], 0
.out:
    ret

; -----------------------------------------------------------------------------
; wc_seqend - the plain stream's sequence closed (CX = 0: SPEC.md 18.4.9);
;             each call committed, so this only gives the record back.
;             keeps nothing
; -----------------------------------------------------------------------------
wc_seqend:
    cmp word [wc_tok], 0
    je .out
    call wc_stand
    jc .out
    xor cx, cx
    mov si, wc_name0
    mov di, [wc_tok]
    call OSAPI_FILE_WRITE_SEQ
    mov word [wc_tok], 0
.out:
    ret

; -----------------------------------------------------------------------------
; wc_write - CX bytes at AX:0 appended to the take (OSAPI_FILE_WRITE_SEQ,
;            plain: committed, so a crash keeps them), standing in its
;            folder. out: CF = 1 failed: [wc_dead] = 1 and [wc_why] = the
;            reason, '... kept'. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_write:
    jcxz .ok
    call wc_stand
    mov si, wc_s_wfail
    jc .err
    push es
    mov es, ax
    xor bx, bx
    mov si, wc_name0
    mov di, [wc_tok]
    push cx
    xor al, al
    call OSAPI_FILE_WRITE_SEQ
    pop cx
    pop es
    jc .ferr
    mov [wc_tok], di
    add [wc_wrote], cx
    adc word [wc_wrote+2], 0
.ok:
    clc
    ret
.ferr:
    mov si, wc_s_full               ; 'Disk full: take kept'
    cmp ax, FERR_FULL
    je .err
    mov si, wc_s_wfail              ; 'Cannot write: kept'
.err:
    mov [wc_why], si
    mov byte [wc_dead], 1
    stc
    ret

; -----------------------------------------------------------------------------
; wc_patch - the header's sizes, now the take is what is on the disk: RIFF =
;            the file's bytes - 8, data = what was written - the header
;            cluster rebuilt in the output claim and put with ONE
;            OSAPI_FILE_WRITE_AT at offset 0 (SPEC.md 18.4.7: inside the
;            file, whole sectors: no cluster, no FAT). The whole cluster,
;            because WRITE_AT's offset is a cluster multiple and the data
;            chunk's size sits at its end. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_patch:
    cmp word [wc_useg], 0
    je .out
    mov ax, [wc_wrote]
    mov dx, [wc_wrote+2]
    call wc_header                  ; the first 512 bytes, the sizes in
    call wc_stand
    jc .out
    push es
    mov es, [wc_useg]
    xor bx, bx
    mov cx, [wc_clu]
    mov si, wc_name0
    xor ax, ax
    xor dx, dx
    call OSAPI_FILE_WRITE_AT
    pop es
.out:
    ret

; -----------------------------------------------------------------------------
; wc_header - the take's header at the output claim's start for a data
;             length DX:AX: RIFF, WAVE, a 16-byte fmt (PCM, mono, the rate,
;             8 bits), JUNK - zeros - filling the cluster to 8 bytes short,
;             data. clobbers AX, BX, CX, DX, DI
; -----------------------------------------------------------------------------
wc_header:
    push es
    mov es, [wc_useg]
    push ax
    push dx
    xor di, di                      ; zeros first: the JUNK chunk's body
    mov cx, [wc_clu]
    cmp cx, [wc_osz]
    jbe .z
    mov cx, [wc_osz]
.z:
    shr cx, 1
    xor ax, ax
    cld
    rep stosw
    pop dx
    pop ax
    mov word [es:0], 'RI'
    mov word [es:2], 'FF'
    mov bx, ax                      ; RIFF's size: the cluster + data - 8
    mov cx, dx
    or bx, cx
    jz .r0                          ; (an unfinished take says 0)
    mov bx, ax
    add bx, [wc_clu]
    adc cx, 0
    sub bx, 8
    sbb cx, 0
.r0:
    mov [es:4], bx
    mov [es:6], cx
    mov word [es:8], 'WA'
    mov word [es:10], 'VE'
    mov word [es:12], 'fm'
    mov word [es:14], 't '
    mov word [es:16], 16
    mov word [es:18], 0
    mov word [es:20], 1             ; PCM
    mov word [es:22], 1             ; mono
    mov bx, [wc_rate]
    mov [es:24], bx                 ; the rate...
    mov word [es:26], 0
    mov [es:28], bx                 ; ...the bytes a second (8-bit mono)
    mov word [es:30], 0
    mov word [es:32], 1             ; the block align
    mov word [es:34], 8             ; the bits
    mov word [es:36], 'JU'
    mov word [es:38], 'NK'
    mov bx, [wc_clu]                ; JUNK: the cluster to 8 bytes short
    sub bx, 52
    mov [es:40], bx
    mov word [es:42], 0
    mov di, [wc_clu]                ; data, and its size, at the cluster's
    sub di, 8                       ; end
    mov word [es:di], 'da'
    mov word [es:di+2], 'ta'
    mov [es:di+4], ax
    mov [es:di+6], dx
    pop es
    ret

; -----------------------------------------------------------------------------
; wc_create - the take's file, its header ONE CLUSTER long (OSAPI_FILE_WRITE:
;             the name is the first free, so this creates). out: CF = 1, SI =
;             why. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_create:
    xor ax, ax
    xor dx, dx
    call wc_header
    push es
    mov es, [wc_useg]
    xor bx, bx
    mov cx, [wc_clu]
    xor dx, dx
    mov si, wc_name0
    call OSAPI_FILE_WRITE
    pop es
    jnc .ok
    call ws_werr                    ; SI = the reason
    stc
    ret
.ok:
    mov byte [wc_made], 1
    mov word [wc_tok], 0
    clc
    ret

; wc_unmake - the take's file deleted, when one was made (a refusal, or a take
; with nothing in it). keeps nothing but flags
wc_unmake:
    cmp byte [wc_made], 0
    je .out
    mov byte [wc_made], 0
    call wc_stand
    jc .out
    mov si, wc_name0
    call OSAPI_FILE_DELETE
.out:
    ret

; wc_stand - stand in the take's folder (OSAPI_FILE_GOTO_QM, only when the
; instance is elsewhere). CF = 1 it is gone. keeps every register
wc_stand:
    push ax
    push bx
    push dx
    call OSAPI_FILE_HERE
    cmp dx, [wc_dir]
    jne .go
    cmp bl, [wc_vol]
    je .here
.go:
    mov dx, [wc_dir]
    mov bl, [wc_vol]
    call OSAPI_FILE_GOTO_QM
    jmp short .out
.here:
    clc
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wc_name - the take's name: VOICEnnn.WAV, the first of 001..999 not in the
;           folder. out: CF = 1 all taken, SI = why. clobbers AX, BX, CX, DX,
;           SI, DI
; -----------------------------------------------------------------------------
wc_name:
    mov word [wc_n], 1
.try:
    mov si, wc_s_voice              ; 'VOICE' + three digits + '.WAV'
    mov di, wc_name0
    call wr_put
    mov ax, [wc_n]
    mov cl, 100
    div cl                          ; AL = hundreds, AH = the rest
    add al, '0'
    mov [di], al
    mov al, ah
    aam                             ; AH = tens, AL = units
    add ax, '00'
    mov [di+1], ah
    mov [di+2], al
    add di, 3
    mov si, wc_s_wav
    call wr_put
    mov byte [di], 0
    mov si, wc_name0                ; there already?
    mov di, wo_nbuf
    call wr_put
    mov byte [di], 0
    call wo_find
    jc .free
    inc word [wc_n]
    cmp word [wc_n], 1000
    jb .try
    mov si, wc_s_names              ; 'No free VOICE name'
    stc
    ret
.free:
    clc
    ret

; -----------------------------------------------------------------------------
; wc_claims - the record ring (32 KB, 16 KB tier) with its words and the gain
;             table, OSAPI_MEM_CLAIM_DMA_HI, all of it the page-safe head
;             and never moved; and the output claim, a whole number of the
;             take's clusters: 16 KB or one cluster on a fixed disk, 32 KB on
;             a floppy. out: CF = 1, SI = why. clobbers AX, CX, DX
; -----------------------------------------------------------------------------
wc_claims:
    mov ax, WC_RINGKB + 1
    mov cl, 3                       ; RL's code: 32 KB
.try:
    push cx
    mov cx, ax
    call OSAPI_MEM_CLAIM_DMA_HI     ; (keeps all but DX)
    pop cx
    jnc .ring
    cmp ax, WC_RINGKB2 + 1
    je .nomem
    mov ax, WC_RINGKB2 + 1
    mov cl, 2
    jmp short .try
.ring:
    mov [wc_rseg], dx
    mov [wc_rcode], cl
    mov ax, 4096
    shl ax, cl
    mov [wc_rl], ax
    dec ax
    mov [wc_rmask], ax
    cmp byte [wc_fixed], 0          ; the units: the medium's (wc_go asked)
    jne .fixed
    mov ax, WC_FLOPKB
    jmp short .osz
.fixed:
    mov ax, [wc_clu]                ; a fixed disk: 16 KB, or one cluster
    mov cl, 10
    shr ax, cl
    cmp ax, WC_OUTKB
    jae .osz
    mov ax, WC_OUTKB
.osz:
    mov cl, WC_NU                   ; THE UNITS: four of 16 KB, two larger
    cmp ax, WC_OUTKB                ; (64 KB of staging either way: 5.9 s at
    jbe .n                          ; 11,025 Hz beyond the ring's 2.95)...
    mov cl, WC_NU / 2
.n:
    xor ch, ch
    xor bx, bx
.u:
    push ax
    call OSAPI_MEM_CLAIM
    pop ax
    jc .short
    mov [wc_useg + bx], dx
    add bx, 2
    loop .u
.short:
    shr bx, 1                       ; ...or as many as the heap gives, one
    jz .nomem                       ; at the least
    mov [wc_nu], bl
    mov cl, 10
    shl ax, cl                      ; (32 KB at most: a word)
    mov [wc_osz], ax
    clc
    ret
.nomem:
    call wc_release
    mov si, ww_e15                  ; 'Not enough memory'
    stc
    ret

; wc_release - the claims given back (the stream must be closed first:
; wc_close). keeps all but flags
wc_release:
    push dx
    mov dx, [wc_rseg]
    call wo_free
    mov [wc_rseg], dx
    push bx
    mov bx, (WC_NU - 1) * 2
.u:
    mov dx, [wc_useg + bx]
    call wo_free
    mov [wc_useg + bx], dx
    sub bx, 2
    jns .u
    pop bx
    pop dx
    ret

; -----------------------------------------------------------------------------
; wc_open - the record stream: verb 4 with SND_OPENF_EXT into our ring, RL's
;           code in AH bits 4-5, 512-byte blocks in bits 6-7, CX = 0 (a
;           driver older than SPEC.md 34.6.4 then refuses rather than
;           records into a grant it does not have). The ring's place and its
;           count start at 0. out: CF = 1, SI = why. clobbers AX, BX, CX, DX,
;           SI, DI
; -----------------------------------------------------------------------------
wc_open:
    xor ax, ax
    mov [wc_lastc], ax
    mov [wc_rcap], ax
    mov [wc_rcap+2], ax
    mov [wc_rrd], ax
    mov [wc_rrd+2], ax
    mov ah, [wc_rcode]
    mov cl, SND_OPENF_RLSH
    shl ah, cl
    or ah, SND_OPENF_RING | SND_OPENF_EXT | (WC_BLKSH << SND_OPENF_BLKSH)
    mov al, 4
    xor si, si
    mov di, [wc_rseg]
    xor cx, cx
    mov dx, [wc_rate]
    call OSAPI_SND_STREAM           ; AL = 0, AH = the handle; else why
    or al, al
    jnz .no
    mov [wc_hand], ah
    mov byte [wc_sopen], 1
    clc
    ret
.no:
    mov si, wc_s_dsp1               ; 7: a single-cycle DSP (SPEC.md 34.6.4)
    cmp al, 7
    je .say
    mov si, wr_s_cbusy              ; 1: busy
    cmp al, 1
    je .say
    mov si, wr_s_rateno             ; 2: the rate
    cmp al, 2
    je .say
    mov si, wr_s_noopen
.say:
    stc
    ret

; wc_close - the stream stopped (verb 2) if open, and its last count folded.
; keeps nothing but flags
wc_close:
    cmp byte [wc_sopen], 0
    je .out
    mov al, 2
    mov ah, [wc_hand]
    call OSAPI_SND_STREAM
    call wc_fold                    ; (the count is final: read it once more)
    mov byte [wc_sopen], 0
.out:
    ret

; -----------------------------------------------------------------------------
; wc_gbuild - INPUT GAIN's table into the ring's claim (SPEC.md 123.7): out =
;             80h + ((in - 80h) x m) >> 8, saturated to 00h..FFh, m the
;             step's Q8 multiplier (wrart_gainm, tools/wrsim.py's gain_q8 -
;             2 dB a step, unity at step 6). Rebuilt when the slider moves
;             during a take. keeps every register
; -----------------------------------------------------------------------------
wc_gbuild:
    cmp word [wc_rseg], 0
    je .ret
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov bl, [wr_gain]
    xor bh, bh
    shl bx, 1
    mov bx, [wrart_gainm + bx]      ; BX = m
    mov es, [wc_rseg]
    mov di, [wc_rl]
    add di, WC_TABOFS
    xor cx, cx
.v:
    mov ax, cx
    sub ax, 128
    imul bx                         ; DX:AX = (in - 128) x m
    mov al, ah                      ; >> 8: bits 8..23, floored
    mov ah, dl
    cmp ax, 127
    jle .hi
    mov ax, 127
.hi:
    cmp ax, -128
    jge .lo
    mov ax, -128
.lo:
    add al, 128
    mov [es:di], al
    inc di
    inc cx
    cmp cx, 256
    jb .v
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
.ret:
    ret

; =============================================================================
; WHAT THE FACE SHOWS
; =============================================================================

; -----------------------------------------------------------------------------
; wc_meter - the record meter (SPEC.md 123.8), at most every WT_MTICKS: the
;            peak of what was WRITTEN since the last look (after INPUT GAIN:
;            what is in the file) through wrart_db, the hold's 1 s decay,
;            PEAK at a rail (00h or FFh written); and the time element's
;            frames - the take's length. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_meter:
    mov ax, [wc_frames]             ; the time: the take's length
    mov dx, [wc_frames+2]
    mov [wt_ph], ax
    mov [wt_ph+2], dx
    call OSAPI_GET_TICKS
    mov di, ax
    sub ax, [wt_mtick]
    cmp ax, WT_MTICKS
    jb .out
    mov [wt_mtick], di
    mov dx, 0x00FF                  ; DL = the smallest, DH = the largest,
    xchg dx, [wc_mmin]              ; taken and reset in ONE instruction (the
                                    ; copier may be the worker)
    xor cx, cx                      ; CL = the peak |u - 80h|, CH = a rail
    cmp dl, dh
    ja .none                        ; (nothing since the last look)
    mov al, dh
    sub al, 0x80
    jae .h
    xor al, al
.h:
    mov ah, 0x80
    sub ah, dl
    jae .l
    xor ah, ah
.l:
    mov cl, al
    cmp ah, cl
    jbe .m
    mov cl, ah
.m:
    cmp dh, 0xFF
    je .rail
    or dl, dl
    jnz .none
.rail:
    inc ch
.none:
    mov al, cl
    mov bx, wrart_db
    xlatb                           ; AL = lit cells
    mov [wr_lit], al
    cmp al, [wr_hold]               ; the hold: a new high, or decayed
    jae .hnew
    mov ax, di
    sub ax, [wt_holdt]
    cmp ax, WT_HOLDT
    jb .hk
    mov al, [wr_lit]
.hnew:
    mov [wr_hold], al
    mov [wt_holdt], di
.hk:
    or ch, ch                       ; PEAK: lit at a rail, for 2 s
    jz .nclip
    mov byte [wr_peak], 1
    mov [wt_peakt], di
    ret
.nclip:
    mov dx, di
    sub dx, [wt_peakt]
    cmp dx, WT_PEAKT
    jb .out
    mov byte [wr_peak], 0
.out:
    ret

; -----------------------------------------------------------------------------
; wc_draw - THE LIVE STRIP (SPEC.md 123.7): the take's columns since the
;           last draw - WC_FPP frames each, its extremes as one line of the
;           lane - and nothing else; a new page clears the lanes once. From
;           wr_redraw and wr_paint, after the elements (the paint forgets
;           what was drawn first). gfx lock held, the clip set.
;           clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_draw:
    call wc_active
    jc .out
    mov al, [wc_page]               ; a new page: the lanes cleared once, and
    cmp al, [wc_dpage]              ; drawn from its first column
    je .cols
    mov [wc_dpage], al
    mov word [wc_drawn], 0
    mov ax, [we_bx]                 ; the lanes, cleared
    mov cx, ax
    add cx, [we_ww]
    dec cx
    mov bx, [we_by]
    add bx, [we_rulh]
    mov dx, bx
    add dx, [we_lh]
    dec dx
    mov si, R_LCD
    call wd_rect
.cols:
    mov cx, [wc_drawn]
.col:
    cmp cx, [wc_ncol]
    jae .done
    cmp cx, [we_ww]                 ; (the window narrowed under the page)
    jae .done
    mov si, cx
    shl si, 1
    mov ax, [wc_cols + si]          ; AL = the smallest, AH = the largest
    push ax
    mov al, ah
    call wc_row
    mov bx, ax                      ; BX = the top (the largest)
    pop ax
    call wc_row
    mov dx, ax                      ; DX = the bottom (the smallest)
    mov ax, [we_bx]
    add ax, cx
    mov si, R_WAVE
    call wd_vl
    inc cx
    jmp short .col
.done:
    mov [wc_drawn], cx
.out:
    ret

; wc_row - AL = an unsigned 8-bit value -> AX = its row in the lane, content
; coordinates (FFh at the top). keeps everything else
wc_row:
    push dx
    push cx
    mov cl, 255
    sub cl, al
    xor ch, ch                      ; CX = 255 - v
    mov ax, [we_lh]
    dec ax
    mul cx                          ; (lh - 1) x (255 - v)
    mov cx, 255
    div cx
    add ax, [we_by]
    add ax, [we_rulh]
    pop cx
    pop dx
    ret

; wc_drawall - a full paint: every column of the page again, the lanes not
; cleared (the paint drew them). keeps nothing
wc_drawall:
    mov word [wc_drawn], 0
    mov al, [wc_page]
    mov [wc_dpage], al
    jmp wc_draw

; -----------------------------------------------------------------------------
; wc_roomsay - the status bar: the take's volume, its room and the seconds
;              it holds - `C: 312 KB free - 28 s`; MB past 9,999 KB, minutes
;              past 999 s, and ' free' dropped when the line would pass 22.
;              clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_roomsay:
    mov di, wr_tbuf2
    mov al, [wc_vol]
    add al, 'A'
    mov ah, ':'
    mov [di], ax
    mov byte [di+2], ' '
    add di, 3
    mov ax, [wc_free]               ; KB, or MB past 9,999 KB
    mov dx, [wc_free+2]
    call wc_k1024
    mov si, wc_s_kb
    or dx, dx
    jnz .m
    cmp ax, 9999
    jbe .k
.m:
    call wc_k1024                   ; (2 GB is 2,048 MB: a word)
    mov si, wc_s_mb
.k:
    call dec_u16
    call wr_put
    push di                         ; (where ' free' would go)
    mov si, wc_s_freed
    call wr_put
    mov ax, [wc_free]               ; the seconds: free / rate, 32 bits
    mov dx, [wc_free+2]
    mov cx, [wc_rate]
    call wc_div32
    mov si, wc_s_sec
    or dx, dx
    jnz .mn
    cmp ax, 999
    jbe .sec
.mn:
    mov cx, 60                      ; (2 GB at 4,000 Hz is 8,947 min)
    call wc_div32
    mov si, wc_s_min
.sec:
    call dec_u16
    call wr_put
    mov byte [di], 0
    pop bx
    mov ax, di
    sub ax, wr_tbuf2
    cmp ax, 22
    jbe .ok
    mov si, bx                      ; past 22: ' free' out
    add si, 5
    mov di, bx
.mv:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .mv
.ok:
    mov si, wr_tbuf2
    jmp wr_say

; wc_k1024 - DX:AX >> 10. keeps the rest
wc_k1024:
    push cx
    mov cx, 10
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    pop cx
    ret

; wc_div32 - DX:AX / CX (CX > 0) -> DX:AX. keeps CX and the rest
wc_div32:
    push bx
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div cx                          ; the high word
    xchg ax, bx                     ; BX = its quotient; DX:AX = rest:low
    div cx
    mov dx, bx
    pop bx
    ret

; wc_mmss - '1:23' of the take's frames at its rate, at DI. DI past it.
; clobbers AX, CX, DX
wc_mmss:
    mov ax, [wc_frames]
    mov dx, [wc_frames+2]
    mov cx, [wc_rate]
    cmp dx, cx
    jb .d
    mov ax, 0xFFFF
    jmp short .m
.d:
    div cx                          ; AX = seconds
.m:
    xor dx, dx
    mov cx, 60
    div cx
    push dx
    call dec_u16
    mov byte [di], ':'
    inc di
    pop ax
    cmp ax, 10
    jae .two
    mov byte [di], '0'
    inc di
.two:
    jmp dec_u16

; =============================================================================
; THE RECOVERY MARKER (SPEC.md 123.7): WAVERACK.TAK in the boot volume's
; SYSTEM\APPDATA - 'WRTK', the state (1: a take is running), its volume,
; folder and name, and the segment of the instance recording it
; =============================================================================

; wc_mark - WAVERACK.TAK written with [wc_mstate] and the take's place (a
; full write the first time a session, one sector after: os88data.inc).
; keeps nothing
wc_mark:
    mov di, wc_mk
    mov word [di], 'WR'
    mov word [di+2], 'TK'
    mov al, [wc_mstate]
    mov [di+4], al
    mov al, [wc_vol]
    mov [di+5], al
    mov ax, [wc_dir]
    mov [di+6], ax
    mov si, wc_name0
    add di, 8
    mov cx, 13
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    mov [wc_mk+WC_MKSEG], ds        ; the recorder: this instance's segment
    mov si, wc_s_tak
    push es
    push ds
    pop es
    mov bx, wc_mk
    mov cx, WC_MKSZ
    mov di, wc_odst
    call od_write                   ; (a refusal: no marker - not the take's)
    pop es
    ret

; -----------------------------------------------------------------------------
; wc_probe - at launch: WAVERACK.TAK read; a take it says was running and
;            never ended makes File > Recover Take live ([wc_rec] = 1).
;            keeps nothing
; -----------------------------------------------------------------------------
wc_probe:
    mov byte [wc_rec], 0
    push es
    push ds
    pop es
    mov si, wc_s_tak
    mov bx, wc_mk
    mov cx, WC_MKSZ
    xor dx, dx
    call od_read
    pop es
    jc .out
    or dx, dx
    jnz .out
    cmp ax, WC_MKSZ
    jne .out
    cmp word [wc_mk], 'WR'
    jne .out
    cmp word [wc_mk+2], 'TK'
    jne .out
    cmp byte [wc_mk+4], 1
    jne .out
    call wc_live                    ; ...and is not another window's, live
    jc .out
    mov byte [wc_rec], 1
.out:
    ret

; -----------------------------------------------------------------------------
; wc_live - CF = 1 the take WAVERACK.TAK names is RUNNING in another window:
;           a live WaveRack at the segment the take was recorded from, not
;           this one (a WaveRack that died left its take to recover; one at
;           our own segment is us, launched after it). OSAPI_SYS_SNAPSHOT
;           into the strip's page, which no take is using. clobbers AX, BX,
;           CX, SI, DI
; -----------------------------------------------------------------------------
wc_live:
    mov ax, [wc_mk+WC_MKSEG]
    mov bx, ds
    cmp ax, bx
    je .no
    push es
    push ds
    pop es
    mov di, wc_cols
    call OSAPI_SYS_SNAPSHOT         ; BX = INST_MAX
    pop es
    mov cx, bx
    mov si, wc_cols + SS_INST
.r:
    cmp byte [si+SSI_STATE], 1
    jne .n
    mov ax, [si+SSI_SEG]
    cmp ax, [wc_mk+WC_MKSEG]
    jne .n
    cmp word [si+SSI_NAME], 'Wa'
    jne .n
    cmp word [si+SSI_NAME+2], 've'
    jne .n
    cmp word [si+SSI_NAME+4], 'Ra'
    jne .n
    cmp word [si+SSI_NAME+6], 'ck'
    jne .n
    stc
    ret
.n:
    add si, SSI_RECSZ
    loop .r
.no:
    clc
    ret
%if SYS_SNAPSHOT_SIZE > WC_MAXCOL * 2
%error "wc_live's snapshot does not fit the strip's page"
%endif

; -----------------------------------------------------------------------------
; wc_recover - File > Recover Take: the take the marker names opened, in its
;              folder, through SPEC.md 123.4's unfinished-header path (its
;              data from the file's length: it opens dirty, 'Recovered; save
;              to fix'); the marker marked done whatever the open says.
;              in: BX = the window. clobbers AX, BX, CX, DX, SI, DI
; -----------------------------------------------------------------------------
wc_recover:
    call wc_probe                   ; the marker AGAIN: since the offer, another
    cmp byte [wc_rec], 0            ; window may have begun a take of its own
    jne .still
    mov si, wc_s_norec              ; 'No take to recover'
    jmp wr_say
.still:
    mov byte [wc_rec], 0
    mov al, [wc_mk+5]               ; the take's place, out of the marker
    mov [wc_vol], al
    mov ax, [wc_mk+6]
    mov [wc_dir], ax
    mov si, wc_mk+8
    mov di, wc_name0
    mov cx, 13
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    mov byte [wc_name0+12], 0
    mov byte [wc_mstate], 0
    call wc_mark
    call wc_stand
    jnc .there
    mov si, wc_s_gone               ; 'The take is not there'
    jmp wr_say
.there:
    mov si, wc_name0
    mov di, wo_nbuf
    mov cx, 13
    push es
    push ds
    pop es
    cld
    rep movsb
    pop es
    mov bx, [wr_win]
    jmp wo_open                     ; (it says why when it refuses)

; --- strings -------------------------------------------------------------------
wc_s_voice:  db 'VOICE', 0
wc_s_wav:    db '.WAV', 0
wc_s_tak:    db 'WAVERACK.TAK', 0
wc_s_kb:     db ' KB', 0
wc_s_mb:     db ' MB', 0
wc_s_freed:  db ' free - ', 0
wc_s_sec:    db ' s', 0
wc_s_min:    db ' min', 0
wc_s_noin:   db 'No input from the card', 0
wc_s_empty:  db 'Nothing was recorded', 0
wc_s_over:   db 'Overrun: kept to ', 0
wc_s_full:   db 'Disk full: take kept', 0
wc_s_wfail:  db 'Cannot write: kept', 0
wc_s_dsp1:   db 'Needs a DSP 2.00+ card', 0
wc_s_flop:   db 'Floppy: use 8,000 Hz', 0
wc_s_names:  db 'No free VOICE name', 0
wc_s_gone:   db 'The take is not there', 0
