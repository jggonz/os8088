; =============================================================================
; os8088 - tests/netbench/netbench.asm
;
; NETBENCH: where a network transfer's time actually goes, read off the machine
; it went there on (SPEC.md 72.15).
;
; This harness measures NOTHING itself. ETHER.DRV brackets its own ten stages
; with the PIT (drivers/ether/ethprof.inc) because that is the only place the
; stages exist, and this is the window that starts the profiler, stops it and
; renders the block it hands back.
;
; **AND THAT SHAPE IS FORCED, not chosen.** MartyPC - the instrument this
; project trusts for time - has no network card of any kind, so it cannot host
; the driver under test. QEMU can, and QEMU is exact about work and useless
; about time (PERFORMANCE.md Part 3). What is left is an 86Box XT or the 5150,
; and neither has a debugger or an automation socket: the numbers have to be
; taken by the guest and read on the guest, during a real transfer from a real
; client.
;
;   make netbench                  # NETBENCH.O88 beside FTPD.O88 on one disk
;   make test TESTAPPS=build/netbench.img
;
; --- HOW TO USE IT ----------------------------------------------------------
;   1. open it, open the FTP server, and put them side by side
;   2. press S here (start, and zero)
;   3. do the transfer
;   4. press X (stop), then R (read)
;
; Starting zeroes everything, so a run is one transfer and not the day's
; total. Stopping banks the wall clock: a report read a minute later would
; otherwise divide the same work by a minute and call the stack 3% busy.
;
; --- WHAT THE COLUMNS MEAN --------------------------------------------------
;   calls   how many times the stage ran
;   KB      how many bytes crossed it (0 for the two whole-operation stages)
;   ms      how long it took, all calls added up
;   pct     ...of the wall clock, which is what makes the table a diagnosis
;   us/KB   ms/KB in the unit a per-byte cost is argued in
;
; `pump` CONTAINS `card`, `frame`, `cksum` and `rxput`; `verb` contains all of
; it plus `rxget` and `txin`. They are nested on purpose and the percentages
; therefore do not add to 100 - the number that matters is `verb`, because
; **the wall clock minus `verb` is everything that is NOT this driver**: the
; FTP server's own staging, the disk, the scheduler and the client's own
; turnaround. If that remainder is most of the run then no amount of work in
; here will move it, and that is worth knowing before a week is spent.
;
; Prefix nb_.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'NETBENCH', nb_entry

%include "netpkg.inc"
%include "os88sock.inc"

NB_BLK      equ 200             ; the report block ETHER.DRV writes: PF_BLK is
                                ; 192 today and this is the buffer it lands in.
                                ; **SIZED WITH SLACK AND CHECKED AT RUNTIME**,
                                ; because a stage added to the driver grows the
                                ; block and this package is not rebuilt with it
NB_HDR      equ 12              ; ...its header: 'PF', version, stages, dd
                                ; wall, dd ACTIVE
NB_REC      equ 18              ; ...and per stage: dd ms, dd bytes, dw calls,
                                ; 8 bytes of name
NB_MAXST    equ (NB_BLK - NB_HDR) / NB_REC  ; what nb_blk HOLDS, derived rather
                                ; than written down a second time: a count
                                ; written down twice is one the driver can grow
                                ; past while the guard still says yes

; -----------------------------------------------------------------------------
; nb_entry - package entry (SPEC.md 20.2)
; -----------------------------------------------------------------------------
nb_entry:
    push si
    call net_find               ; before the window, so the first page can say
    call nb_head                ; there is no driver rather than drawing an
    mov si, nb_tpl              ; empty table
    call OSAPI_WM_CREATE
    jc .out
    mov [nb_win], bx
    mov al, 1
    call OSAPI_WM_SNAP
    clc
.out:
    pop si
    ret

nb_paint:
    call bl_paint
    ret

; -----------------------------------------------------------------------------
; nb_onkey - S start, X stop, R read, W write the report to a file
; -----------------------------------------------------------------------------
nb_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [nb_win], si
    mov bl, al
    or bl, 0x20
    cmp bl, 's'
    je .start
    cmp bl, 'x'
    je .stop
    cmp bl, 'r'
    je .read
    cmp bl, 'w'
    je .save
    call bl_key
    jc .out
    call bl_paint
    jmp short .out
.start:
    xor al, al
    inc al                      ; NETV_PROF AL = 1: zero and start
    call nb_prof
    call nb_head
    call bl_paint
    jmp short .out
.stop:
    xor al, al                  ; ...AL = 0: stop, and bank the wall
    call nb_prof
    call nb_report
    call bl_paint
    jmp short .out
.read:
    call nb_report
    call bl_paint
    jmp short .out
.save:
    mov si, nb_s_file
    call bl_save
    call bl_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- a click pages down, so it can be driven with no keyboard ----------------
nb_onclick:
    push ax
    push si
    mov [nb_win], si
    mov ax, 0x5100              ; PageDown, which is what bl_key wants
    call bl_key
    jc .out
    call bl_paint
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; nb_prof - AL = NETV_PROF's sub-verb; out CF=1 = the driver refused
;
; **THE BUFFER IS OURS AND ES IS ALREADY OUR SEGMENT** on the way into the
; door (netpkg.inc): OSAPI_DRV_CALL is an X stub and puts the CALLER's segment
; in ES, which is the one thing about this ABI that is easy to get wrong and
; reads as memory corruption when it is (SPEC.md 77.10).
; -----------------------------------------------------------------------------
nb_prof:
    push bx
    push cx
    push di
    push es
    push ds
    pop es
    mov di, nb_blk
    mov bh, NET_CLASS
    mov bl, NETV_PROF
    call OSAPI_DRV_CALL
    pop es
    pop di
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; nb_head - the banner and the key, which is the whole page until a run happens
; -----------------------------------------------------------------------------
nb_head:
    push ax
    push si
    mov word [bl_nrow], 0       ; a re-read REPLACES the report rather than
    mov word [bl_used], 0       ; appending to it
    mov word [bl_top], 0
    mov si, nb_s_t1
    call bl_sline
    mov si, nb_s_t2
    call bl_sline
    call bl_blank
    mov si, nb_s_k1
    call bl_sline
    mov si, nb_s_k2
    call bl_sline
    mov si, nb_s_k3
    call bl_sline
    mov si, nb_s_k4
    call bl_sline
    call bl_blank
    cmp byte [net_cls], 0
    jne .have
    mov si, nb_s_nodrv
    call bl_sline
.have:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; nb_report - read the block and render it
; -----------------------------------------------------------------------------
nb_report:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call nb_head
    cmp byte [net_cls], 0
    je .out
    mov al, 2                   ; NETV_PROF AL = 2: read
    call nb_prof
    jc .refused
    cmp byte [nb_blk], 'P'
    jne .bad
    cmp byte [nb_blk+1], 'F'
    jne .bad
    cmp byte [nb_blk+2], 2
    jne .bad                    ; a version this build does not know: every
                                ; offset below would be plausible and wrong
    mov al, [nb_blk+3]
    xor ah, ah
    cmp ax, NB_MAXST
    ja .bad
    mov [nb_nst], ax

    ; --- the wall clock, and the ACTIVE window inside it -------------------
    ; **THE PERCENTAGES ARE AGAINST THE ACTIVE WINDOW**, not the wall, and the
    ; difference is a person. The wall runs from the S key to the X key and a
    ; human presses both - free the mouse from the emulator, start the client,
    ; watch for it to finish, click back in, press X. The field profiled one
    ; transfer twice and the wall fell 11,515ms where the driver's own total
    ; fell 8,670: the missing 2,845 was somebody being quicker with the
    ; keyboard. `active` is the first byte any stage moved to the last, so
    ; both ends of that are cut off.
    mov ax, [nb_blk+4]
    mov dx, [nb_blk+6]
    call nb_ms
    mov si, nb_s_wall
    mov cx, 9
    call bl_kv
    mov ax, [nb_blk+8]
    mov dx, [nb_blk+10]
    mov [nb_wall], ax           ; ...and THIS is what nb_pct divides by
    mov [nb_wall+2], dx
    call nb_ms
    mov si, nb_s_act
    mov cx, 9
    call bl_kv                  ; **PRINTED AS IT IS, INCLUDING ZERO.** The
                                ; first version quietly substituted the wall
                                ; here when no byte had moved, so a run with
                                ; NOTHING in it reported active == wall and
                                ; looked like a transfer that took twelve
                                ; seconds and did no work. A zero is the
                                ; answer; hiding it is how an instrument
                                ; starts lying (SPEC.md 72.18)
    mov ax, [nb_wall]
    or  ax, [nb_wall+2]
    jnz .haveact
    mov si, nb_s_noact          ; ...and SAY so, then price the idle machine
    call bl_sline               ; against the wall, which is a real question
    mov si, nb_s_noac2
    call bl_sline
    mov ax, [nb_blk+4]          ; and the only one this run can answer
    mov dx, [nb_blk+6]
    mov [nb_wall], ax
    mov [nb_wall+2], dx
.haveact:

    ; --- ...and the number the whole table is FOR --------------------------
    ; The wall minus `verb` is every millisecond that was not inside this
    ; driver: the FTP server's staging, the disk, the scheduler and the
    ; client's own turnaround. If that is most of the run then nothing in the
    ; table below can move it, and a week spent on the stages would buy
    ; nothing - which is exactly the mistake this instrument exists to stop.
    ; ...found BY NAME: the names travel with the numbers (SPEC.md 72.15.2)
    ; precisely so a reporter never carries its own copy of the stage order.
    ; A literal `9 * NB_REC` here would label the wrong column the day a
    ; stage is inserted, and read past the block for a driver reporting
    ; fewer than ten - the exact drift the block's own name field exists
    ; to make impossible. A miss answers ZEROS, .out2's own idiom.
    push cx
    mov si, nb_blk + NB_HDR
    mov cl, [nb_blk+3]          ; the REPORTED stage count
    xor ch, ch
    xor ax, ax
    xor dx, dx
    jcxz .vfound
.vscan:
    cmp word [si+10], 've'
    jne .vnext
    cmp word [si+12], 'rb'
    jne .vnext
    cmp byte [si+14], 0
    jne .vnext
    mov ax, [si]
    mov dx, [si+2]
    jmp short .vfound
.vnext:
    add si, NB_REC
    loop .vscan
.vfound:
    pop cx
    mov [nb_tm], ax
    mov [nb_tm+2], dx
    mov ax, [nb_wall]
    mov dx, [nb_wall+2]
    sub ax, [nb_tm]
    sbb dx, [nb_tm+2]
    jnc .out2                   ; verb longer than the ACTIVE window is not
    xor ax, ax                  ; impossible - a poll before the first byte
    xor dx, dx                  ; still counts - so this says nothing rather
.out2:                          ; than four billion, and a big lead-in is
                                ; visible as `wall` far above `active`
    call nb_ms
    mov si, nb_s_out
    mov cx, 9
    call bl_kv
    call bl_blank

    mov si, nb_s_hdr
    call bl_sline
    mov si, nb_s_rule
    call bl_sline

    xor bx, bx                  ; BX = the stage
.l:
    call nb_row
    inc bx
    cmp bx, [nb_nst]
    jb .l

    call bl_blank
    mov si, nb_s_note
    call bl_sline
    mov si, nb_s_note2
    call bl_sline
    jmp short .out
.refused:
    mov si, nb_s_noprof
    call bl_sline
    jmp short .out
.bad:
    mov si, nb_s_badblk
    call bl_sline
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; nb_row - BX = a stage: one line of the table
; -----------------------------------------------------------------------------
nb_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, NB_REC
    mul bx
    add ax, nb_blk + NB_HDR
    mov si, ax                  ; SI = the record

    call bl_lclr
    push si
    add si, 10                  ; the name travels WITH the numbers (72.15)
    xor di, di
    call bl_lput
    pop si

    mov ax, [si+8]              ; calls
    xor dx, dx
    mov di, NB_C_CALLS
    mov cx, 7
    call bl_dec

    mov ax, [si+4]              ; bytes -> KB
    mov dx, [si+6]
    mov [nb_by], ax
    mov [nb_by+2], dx
    call nb_tokb
    mov [nb_kb], ax             ; ...banked for us/KB below
    push dx
    mov di, NB_C_KB
    mov cx, 8
    call bl_dec
    pop dx

    mov ax, [si]                ; counts -> ms
    mov dx, [si+2]
    mov [nb_tm], ax
    mov [nb_tm+2], dx
    call nb_ms
    mov di, NB_C_MS
    mov cx, 8
    call bl_dec

    mov ax, [nb_tm]             ; ...and its share of the wall
    mov dx, [nb_tm+2]
    call nb_pct
    mov di, NB_C_PCT
    mov cx, 5
    call bl_dec

    mov ax, [nb_kb]             ; us per KB, but only where bytes crossed
    or ax, ax
    jz .commit
    mov cx, ax
    mov ax, [nb_tm]
    mov dx, [nb_tm+2]
    call bl_us100               ; DX:AX = us * 100 per KB
    mov di, NB_C_USKB
    call bl_usfield
.commit:
    call bl_lcommit
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; nb_ms - DX:AX counts -> DX:AX milliseconds
;
; One count is 838 ns, so ms = counts / 1193.182 and the divisor has to be an
; integer: counts * 5 / 5966 is the same number to fifteen parts per million,
; and both halves stay inside the 48-bit arithmetic benchlib already has.
; PERFORMANCE.md Part 6 rule 3 is why this is not `counts * 838 / 1000000` -
; that numerator laps 32 bits at 4.3 seconds, which is a length a transfer on
; this machine passes in its first breath.
; -----------------------------------------------------------------------------
nb_ms:
    push cx
    mov cx, 5
    call bl_mul48
    mov cx, 5966
    call bl_div48
    call bl_get32
    pop cx
    ret

; --- nb_tokb - DX:AX bytes -> AX kilobytes (DX = 0) ---------------------------
; The count is shifted rather than divided because it feeds bl_us100's CX,
; which is sixteen bits: 64MB of one stage is more than any transfer this
; machine will live to finish.
nb_tokb:
    push cx
    mov cx, 10
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    or dx, dx
    jz .ok
    mov ax, 0xFFFF              ; more than 64MB: say so rather than wrap
.ok:
    xor dx, dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; nb_pct - DX:AX counts -> DX:AX percent of [nb_wall]
;
; BOTH SIDES ARE SHIFTED DOWN TWELVE FIRST. bl_ratio divides by CX, which is
; sixteen bits, and a thirty-second run is 37 million counts - so the divisor
; has to come down before it fits, and the numerator has to come down with it
; or the answer is scaled by 4096.
; -----------------------------------------------------------------------------
nb_pct:
    push bx
    push cx
    call nb_shr12
    push ax
    mov ax, [nb_wall]
    mov dx, [nb_wall+2]
    call nb_shr12
    mov cx, ax
    pop ax
    xor dx, dx
    or cx, cx
    jz .none
    call bl_ratio
    jmp short .out
.none:
    xor ax, ax
    xor dx, dx
.out:
    pop cx
    pop bx
    ret

nb_shr12:
    push cx
    mov cx, 12
.s:
    shr dx, 1
    rcr ax, 1
    loop .s
    pop cx
    ret

%include "benchlib.inc"

NB_C_CALLS  equ 10              ; the columns, and nb_s_hdr is laid out to
NB_C_KB     equ 18              ; match them - a header that drifts from its
NB_C_MS     equ 27              ; own table is worse than no header
NB_C_PCT    equ 36
NB_C_USKB   equ 43

; **LOWER-RIGHT AND MODEST, so it can sit BESIDE the FTP server window** - the
; two are used together and one covering the other is the whole difficulty of
; using them together. ftpd's template is 40,40,400x176, so this starts below
; and to the right of it and clears the Disk window's file rows as well. A
; template that does not fit the live screen is CLAMPED and not refused
; (SPEC.md 39.7); on CGA that leaves a few rows and benchlib's paging, which is
; the honest trade on a 200-row adapter.
nb_tpl:
    dw 151, 230, 480, 240
    dw nb_ttl, nb_paint, nb_onkey, nb_onclick

nb_ttl:     db 'Net Bench', 0
nb_s_file:  db 'NETBENCH.TXT', 0
nb_s_t1:    db 'NETBENCH - where an ETHER.DRV transfer spends its time', 0
nb_s_t2:    db 'SPEC.md 72.15 - the driver brackets its own stages.', 0
nb_s_k1:    db 'S start (and zero)   X stop   R read   W write NETBENCH.TXT', 0
nb_s_k2:    db 'Start, run the transfer, stop, read. A click pages down.', 0
nb_s_k3:    db 'ACTIVE is the first byte moved to the last - the wall has', 0
nb_s_k4:    db 'your own hands at both ends of it. Percentages use ACTIVE.', 0
nb_s_nodrv: db 'NO SOCKET DRIVER ANSWERED - there is nothing to profile.', 0
nb_s_noprof: db 'THIS DRIVER HAS NO PROFILER - the cable shares the stack', 0
nb_s_badblk: db 'BLOCK VERSION UNKNOWN - rebuild the driver and this one', 0
nb_s_wall:  db 'wall, S to X (ms)', 0
nb_s_act:   db 'ACTIVE (ms)', 0
nb_s_noact: db 'NO PAYLOAD MOVED - this is an idle machine, priced', 0
nb_s_noac2: db 'against the wall. It is what a poll costs and nothing else.', 0
nb_s_out:   db 'NOT in the driver (ms)', 0
nb_s_hdr:   db 'stage      calls      KB      ms   pct       us/KB', 0
nb_s_rule:  db '---------------------------------------------------------', 0
nb_s_note:  db 'pump contains card/frame/cksum/rxput. verb contains all.', 0
nb_s_note2: db 'ACTIVE MINUS VERB IS EVERYTHING THAT IS NOT THIS DRIVER.', 0

NB_O_SCAL   equ 64
NB_O_BLK    equ NB_O_SCAL
NB_BSS_OWN  equ ((NB_O_BLK + NB_BLK + 511) / 512) * 512

    align 512                   ; benchlib's base is an int 13h target and must
                                ; be 512-aligned (bl_save)
    OS88_BSS NB_BSS_OWN + BL_BSS_SIZE
    OS88_IMAGE_END

nb_win      equ os88_image_end + 0     ; word: our window
nb_nst      equ os88_image_end + 2     ; word: stages in the block we read
nb_wall     equ os88_image_end + 4     ; dd: the run's wall clock, in counts
nb_tm       equ os88_image_end + 8     ; dd: this row's counts...
nb_by       equ os88_image_end + 12    ; dd: ...and its bytes
nb_kb       equ os88_image_end + 16    ; word: ...as kilobytes, for us/KB
nb_blk      equ os88_image_end + NB_O_BLK
    BL_BSS os88_image_end + NB_BSS_OWN
