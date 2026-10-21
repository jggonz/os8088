; =============================================================================
; os8088 - apps/syscheck/syscheck.asm
;
; SYSTEM CHECK (SPEC.md 118): is the installed system all one release? It
; reads the volume the machine BOOTED from and reports what an in-place
; upgrade (SPEC.md 52.10.15) can leave broken or behind - programs and
; drivers built for another kernel, files a release retired or moved, the
; debris of an interrupted install, a hibernation image from another build,
; file associations that point at nothing, and release files that are not
; there. Nine checks, a lamp each, the findings of the selected one listed
; under them, and a report that names every finding.
;
; THE SCAN RUNS IN WAKE SLICES ON THE UI TASK (SPEC.md 118.3, 74.1): it is
; file work, and a worker may not touch a file (SPEC.md 20.6 rule 7). Each
; wake runs steps for at most one tick, draws only what those steps changed,
; and posts the next wake while work remains - so the desktop stays usable
; and Stop is honoured at the next step. REDLINE (SPEC.md 103) was the look;
; its scheduling, one blocking call under the gfx lock, was not.
;
; "THIS RELEASE" IS THE ONE THIS PACKAGE WAS BUILT WITH (SPEC.md 118.1.1).
; No slot reports the running kernel's build, so the manifest
; tools/os88sysman.py writes from the system disk's own inputs - and
; buildnum.inc's number - describe the release, and the install puts this
; file in SYSTEM\ beside the kernel it describes.
;
; Files: scan.inc is the nine checks as a state machine, ui.inc the layout
; and every drawing call, out.inc the report and Clean Up. Label prefix sc_.
; =============================================================================

%include "os88api.inc"
%include "buildnum.inc"             ; BUILD_NUM (SPEC.md 14.2), from $(BUILD)

    OS88_HEADER 'System Check', sc_entry, 1
    OS88_ICON16                     ; a disk with a tick on it
    ; --- mask ---
    dw 0xFFFE, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    dw 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF
    ; --- data: a floppy outline, its shutter, and a tick on the label ---
    dw 0xFFFE, 0x8C33, 0x8C31, 0x8FF1
    dw 0x8001, 0x8001, 0xBFFD, 0xA00D
    dw 0xA01D, 0xA039, 0xAC71, 0xA6E5
    dw 0xA3C5, 0xA185, 0xA005, 0xFFFF
    OS88_ICON16_END

; --- the checks (SPEC.md 118.1), in row order --------------------------------
SC_KERN     equ 0                   ; Kernel and boot files
SC_DRV      equ 1                   ; Drivers
SC_PKG      equ 2                   ; Packages
SC_LEFT     equ 3                   ; Install leftovers
SC_RET      equ 4                   ; Retired programs
SC_REL      equ 5                   ; Release files (missing / other bytes)
SC_CFG      equ 6                   ; Settings (SYSTEM.CFG)
SC_ASC      equ 7                   ; File associations
SC_HIB      equ 8                   ; Hibernation image
SC_NCHK     equ 9

; severities, and a finding's flags
SV_NOTE     equ 1                   ; information, never counted
SV_WARN     equ 2                   ; stale, duplicated or wasted - works
SV_BROKEN   equ 3                   ; something will refuse to work
FL_RM       equ 1                   ; Clean Up may delete this file (118.8)
FL_ASSOC    equ 2                   ; Clean Up rebuilds ASSOC.DAT for this

; a lamp's states (ui.inc draws them)
LP_PEND     equ 0
LP_RUN      equ 1
LP_OK       equ 2
LP_WARN     equ 3
LP_BROKEN   equ 4

; the manifest's kinds and folders - tools/os88sysman.py's SCK_* / FOLDERS
SCK_KERNEL  equ 1
SCK_BOOT    equ 2
SCK_DRV     equ 3
SCK_MOD     equ 4
SCK_PKG     equ 5
SCF_ROOT    equ 0
SCF_SYSTEM  equ 1
SCF_STIK    equ 2

DRV_VER     equ 11                  ; drivers/os88drv.inc - the ABI lock binds
MOD_VER     equ 5                   ; kernel/mod.inc       it to PKG_FMT's era

; --- the findings table (SPEC.md 118.3) ---------------------------------------
SC_MAXF     equ 96
FR_CHK      equ 0                   ; byte: the check
FR_SEV      equ 1                   ; byte: SV_*
FR_FLG      equ 2                   ; byte: FL_*
FR_CLUS     equ 4                   ; word: the folder it is in (Clean Up)
FR_PATH     equ 6                   ; 44: "C:\APPS\X.O88", NUL
FR_RSN      equ 50                  ; 46: the reason, NUL
FR_SIZE     equ 96
FR_PATHN    equ 44
FR_RSNN     equ 46

SC_MAXFOLD  equ 64                  ; folders the walk remembers (paths)
FO_CLUS     equ 0
FO_PATH     equ 2                   ; "\APPS\GAMES", NUL - 40
FO_SIZE     equ 42
SC_MAXNAME  equ 200                 ; packages remembered for the duplicate test
NM_SIZE     equ 14                  ; name 13 (NUL) + the folder's index
SC_MAXDEP   equ 8                   ; folder depth the walk descends
WS_CLUS     equ 0                   ; walk stack: the folder,
WS_ORD      equ 2                   ; ...the next FIND ordinal in it,
WS_PLEN     equ 4                   ; ...the path length to put back
WS_SIZE     equ 6
SC_PATHN    equ 80                  ; the walk's folder path
SC_CHUNK    equ 4096                ; a sum's read
SC_BUFSZ    equ 8192                ; sc_buf: chunks, ASSOC.DAT (<= 5,112), cfg

; -----------------------------------------------------------------------------
; sc_entry - package entry point (SPEC.md 20.2)
; in:  DS = CS = our segment, ES = KERNEL_SEG, IF = 1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, from wm_create)
; -----------------------------------------------------------------------------
sc_entry:
    mov si, sc_tpl
    call OSAPI_WM_CREATE
    jc sc_ret
    mov [sc_win], bx
    OS88_REGION_MOVABLE_NOKEEP sc_ret ; SPEC.md 66.6.1: every byte that
                                    ; outlives a pass is a static and moves
                                    ; with us, so the proc is a bare `ret`
    mov si, sc_pref                 ; a size per adapter (SPEC.md 11.100.1)
    call OSAPI_WM_PREFER
    mov al, 1
    call OSAPI_WM_SNAP              ; content origin on a multiple of 8, so
                                    ; every run is font_run's one-store path
    mov si, sc_about
    call OSAPI_ABOUT_SET
    mov ax, sc_onwake
    call OSAPI_WM_ONWAKE
    xchg ax, bx                     ; AX = the window
    mov bx, sc_btrec
    mov si, sc_onup                 ; W_ONMOUSEUP: a button fires
    mov di, sc_ondrag
    mov dx, sc_onclick              ; a press that was not a button's
    call os88ui_btninit             ; keeps every register
    xchg ax, bx                     ; BX = the window again
    call sc_volscan                 ; ASK which drive first (SPEC.md 118.13),
    mov byte [sc_choose], 1         ; the hard disk already chosen
    clc
sc_ret:
    ret

; -----------------------------------------------------------------------------
; sc_onwake - OSAPI_WM_ONWAKE (SPEC.md 74.1): run the scan for up to a tick,
;             then draw what changed
; in:  SI = our window, the UI task, gfx lock NOT held
; out: nothing; keeps SI and BP
; -----------------------------------------------------------------------------
sc_onwake:
    push si
    push bp
    cmp byte [sc_run], 0
    je .out                         ; a stale kick: nothing to do
    call OSAPI_GET_TICKS
    mov [sc_t0], ax
.more:
    call sc_step                    ; one bounded piece of the scan
    cmp byte [sc_run], 0
    je .draw                        ; it finished (or stopped) just now
    call OSAPI_GET_TICKS
    cmp ax, [sc_t0]
    je .more                        ; still inside the tick we started in
.draw:
    call OSAPI_GFX_LOCK
    mov bx, [sc_win]
    call sc_drawowed                ; arms the clip; draws only the owed
    call OSAPI_GFX_UNLOCK
    cmp byte [sc_run], 0
    je .out
    call sc_kick
.out:
    pop bp
    pop si
    ret

; sc_kick - post the next wake; a refused post is retried from the next
;           callback (sc_owe), so a scan cannot strand (SPEC.md 118.3)
sc_kick:
    push bx
    mov bx, [sc_win]
    call OSAPI_WM_WAKE
    sbb al, al                      ; AL = FF refused / 00 posted
    mov [sc_owekick], al
    pop bx
    ret

; sc_owe - from any callback: re-post a wake a full queue refused
sc_owe:
    cmp byte [sc_owekick], 0
    je .out
    cmp byte [sc_run], 0
    je .out
    call sc_kick
.out:
    ret

; -----------------------------------------------------------------------------
; sc_paint - W_PAINT: lay out for the window as it is now and draw it all
; in:  SI = window ptr, content white-filled, gfx lock held
; out: nothing; keeps SI
; -----------------------------------------------------------------------------
sc_paint:
    push si
    mov bx, si
    call sc_layout
    call sc_drawall
    pop si
    cmp byte [sc_btrec+OS88UI_BT_N], 0 ; the About card is up: drawn LAST,
    jne .out                        ; over what it is opaque about
    push si
    mov bx, si
    mov si, sc_ablines
    call os88ui_about_d
    pop si
.out:
    call sc_owe
    ret

; sc_repaint - erase the content and paint it again, from a handler
; in:  SI = window ptr; gfx lock held
sc_repaint:
    push si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .out                         ; not one pixel visible: the next paint
    mov al, CWHITE
    call OSAPI_SET_COLOR
    call OSAPI_WM_CONTENT
    push ax
    push dx
    call OSAPI_WM_GEOM              ; CX = width, DX = height
    pop bx                          ; BX = y1
    pop ax                          ; AX = x1
    add cx, ax
    dec cx
    add dx, bx
    dec dx
    call OSAPI_GFX_FILL
    pop si
    push si
    call sc_paint
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; sc_onkey - W_ONKEY: S scan/stop, D details, R report, Esc stop, Up/Down
;            choose a check, PgUp/PgDn scroll its findings (SPEC.md 118.6)
; in:  AX = the key (AH scan code, AL character), SI = window, lock held
; -----------------------------------------------------------------------------
sc_onkey:
    cmp byte [sc_btrec+OS88UI_BT_N], 0
    jne .live
    jmp sc_abdown                   ; any key takes the About card down
.live:
    call sc_owe
    mov dl, al
    and dl, 0xDF                    ; a letter, either case
    cmp byte [sc_choose], 0
    je .keys
    cmp al, 0x0D                    ; the chooser: Enter or S checks the drive,
    je .go                          ; Esc goes back to the last results, the
    cmp dl, 'S'                     ; arrows choose
    je .go
    cmp al, 0x1B
    je .cesc
    mov al, [sc_vsel]
    cmp ah, 0x48
    je .vup
    cmp ah, 0x50
    jne .ret
    inc ax
    jmp sc_vmove
.vup:
    or al, al
    jz .ret
    dec ax
    jmp sc_vmove
.go:
    jmp sc_choose_go
.cesc:
    jmp sc_choose_esc
.keys:
    cmp al, 0x1B
    je .esc
    cmp dl, 'S'
    je .scan
    cmp dl, 'D'
    je .details
    cmp dl, 'R'
    je .report
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp ah, 0x49
    je .pgup
    cmp ah, 0x51
    je .pgdn
    ret
.esc:
    cmp byte [sc_run], 0
    je .ret
.scan:
    jmp sc_act_scan
.details:
    jmp sc_act_details
.report:
    jmp sc_act_report
.up:
    mov al, [sc_sel]
    or al, al
    jz .ret
    dec ax
    jmp sc_select
.down:
    mov al, [sc_sel]
    cmp al, SC_NCHK - 1
    jae .ret
    inc ax
    jmp sc_select
.pgup:
    mov ax, [sc_sb+10]              ; fit
    neg ax
    jmp sc_fscroll
.pgdn:
    mov ax, [sc_sb+10]
    jmp sc_fscroll
.ret:
    ret

; -----------------------------------------------------------------------------
; the buttons (the standard control, SPEC.md 20.5.1.3)
; -----------------------------------------------------------------------------
SB_SCAN     equ 0
SB_DET      equ 1
SB_SAVE     equ 2
SB_CLEAN    equ 3
SB_QUIT     equ 4
SC_NB       equ 5

sc_ondrag:
    mov bx, sc_btrec
    jmp os88ui_btndrag

; sc_onup - W_ONMOUSEUP: a button fires on the release
sc_onup:
    call sc_owe
    mov bx, sc_btrec
    call os88ui_btnup               ; AX = index + 1, 0 = cancelled
    dec ax
    js .out
    call sc_bok                     ; greyed: the control explains itself
    jc .out
    cmp al, SB_SCAN
    je sc_act_scan
    cmp al, SB_DET
    je sc_act_details
    cmp al, SB_SAVE
    je sc_act_report
    cmp al, SB_CLEAN
    je sc_act_clean
    mov bx, si                      ; Quit
    call OSAPI_WM_CLOSE             ; (a far call: never jmp to a slot)
.out:
    ret

; sc_bok - would this button do anything? (SPEC.md 47 rule 2's one predicate)
; in:  AL = index; out CF = 1 greyed; every register kept
sc_bok:
    cmp byte [sc_choose], 0         ; the chooser up: Scan and Quit only
    je .live
    cmp al, SB_DET
    je .no
    cmp al, SB_SAVE
    je .no
    cmp al, SB_CLEAN
    je .no
.live:
    cmp al, SB_SAVE
    je .save
    cmp al, SB_CLEAN
    je .clean
    clc
    ret
.save:                              ; a report needs a scan that has ended
    cmp byte [sc_run], 0
    jne .no
    cmp byte [sc_ended], 0
    je .no
    clc
    ret
.clean:                             ; ...and Clean Up something to remove
    cmp byte [sc_run], 0
    jne .no
    cmp word [sc_nrm], 0
    jne .yes
    cmp byte [sc_ascbad], 0
    je .no
.yes:
    clc
    ret
.no:
    stc
    ret

; sc_act_scan - Scan, or Stop while a scan runs
; in:  SI = window; gfx lock held
sc_act_scan:
    cmp byte [sc_run], 0
    je .start
    mov byte [sc_stop], 1           ; honoured at the next step
    ret
.start:
    cmp byte [sc_choose], 0
    je .ask
    jmp sc_choose_go                ; the chooser is up: check its drive
.ask:
    jmp sc_choose_open              ; otherwise ask again (SPEC.md 118.13)

; sc_act_details - the findings take the checks' room as well (118.4)
sc_act_details:
    cmp byte [sc_choose], 0
    jne .out
    xor byte [sc_details], 1
    jmp sc_repaint
.out:
    ret
; -----------------------------------------------------------------------------
; sc_onclick - a press that was not a button's: the About card's dismissal,
;              a Checks row, or the findings' scroll bar
; in:  CX = x, DX = y (screen), SI = window; gfx lock held
; out: CF = 1 the press was ours
; -----------------------------------------------------------------------------
sc_onclick:
    call sc_owe
    cmp byte [sc_btrec+OS88UI_BT_N], 0
    jne .live
    call sc_abdown
    stc
    ret
.live:
    cmp byte [sc_choose], 0
    je .rows
    jmp sc_vclick                   ; the chooser's: CF = 1 when it was
.rows:
    call sc_hitrow                  ; AL = the check row under CX,DX, CF = none
    jc .sb
    mov byte [sc_seluser], 1
    call sc_select
    stc
    ret
.sb:
    cmp byte [sc_laid], 0
    je .no
    mov bx, sc_sb
    call os88ui_sbhit               ; AL = the part
    cmp al, OS88UI_SBUP
    je .up
    cmp al, OS88UI_SBDOWN
    je .dn
    cmp al, OS88UI_SBPGUP
    je .pu
    cmp al, OS88UI_SBPGDN
    je .pd
.no:
    clc
    ret
.up:
    mov ax, -1
    jmp short .go
.dn:
    mov ax, 1
    jmp short .go
.pu:
    mov ax, [sc_sb+10]
    neg ax
    jmp short .go
.pd:
    mov ax, [sc_sb+10]
.go:
    call sc_fscroll
    stc
    ret

; =============================================================================
; 'About System Check' - the standard card (SPEC.md 12.2, 20.5.1). While it
; is up the button record says it has NO buttons, Timer's trick (SPEC.md
; 111.6): a press anywhere reaches sc_onclick and takes it down.
; =============================================================================
sc_about:
    mov byte [sc_btrec+OS88UI_BT_N], 0
    mov bx, si
    mov si, sc_ablines
    jmp os88ui_about

sc_abdown:
    mov byte [sc_btrec+OS88UI_BT_N], SC_NB
    jmp sc_repaint

; --- data ----------------------------------------------------------------------
sc_tpl:     dw 7, 22, 626, 436
            dw sc_ttl, sc_paint, sc_onkey, 0
    OS88_PREFER sc_pref, 626, 436, 712, 330, 632, 300

sc_ttl      equ 16                  ; the header's name field: 'System Check'

sc_s_scan:  db 'Scan', 0
sc_s_stop:  db 'Stop', 0
sc_s_det:   db 'Details', 0
sc_s_save:  db 'Save Report', 0
sc_s_clean: db 'Clean Up...', 0
sc_s_quit:  db 'Quit', 0
sc_btlbl:   dw sc_s_scan, sc_s_det, sc_s_save, sc_s_clean, sc_s_quit
sc_btw:     dw 56, 72, 104, 104, 48 ; each button's width, left to right
SC_BTROW    equ 56 + 72 + 104 + 104 + 48 + 4 * 8
    OS88UI_BTNREC sc_btrec, sc_rects, sc_btlbl, sc_bflags, SC_NB

sc_ablines:
    dw sc_ab1, sc_ab2, sc_ab3, 0
sc_ab1:     db 'System Check for os8088', 0
sc_ab2:     db 'Release build ', BUILD_STR, 0
sc_ab3:     db 'Is it all one release?', 0

%include "syscheck/scan.inc"
%include "syscheck/ui.inc"
%include "syscheck/out.inc"
%include "syscheck/choose.inc"
%include "syscheck_man.inc"         ; the release manifest (SPEC.md 118.2)
%include "os88rseq.inc"

%define DEC_U16
%define DEC_U32
%define DEC_SEP
%include "os88dec.inc"

%define OS88UI_ABOUT
%define OS88UI_SCROLL
%define OS88UI_SBUPD
%define OS88UI_ALERT
%define OS88UI_NOGLYPH
%include "os88ui.inc"

; --- loader-zeroed bss (SPEC.md 21 step 5) ------------------------------------
; Small things first, big tables last: OS88_BSS_ALIGNED puts sc_buf on the
; first 512 boundary past the image and fills the gap in front of it with
; whatever of this list fits (SPEC.md 77.31).
    OS88_AWORD sc_win
    OS88_ABYTE sc_run               ; a scan is running
    OS88_ABYTE sc_stop              ; ...and has been asked to stop
    OS88_ABYTE sc_ended             ; a scan has ended at least once
    OS88_ABYTE sc_stopped           ; ...and the last one was stopped
    OS88_ABYTE sc_owekick           ; a wake post was refused - re-post
    OS88_ABYTE sc_laid              ; sc_layout has run: the rects are real
    OS88_ABYTE sc_details           ; the findings have the checks' room
    OS88_ABYTE sc_sel               ; the selected check
    OS88_ABYTE sc_seluser           ; ...chosen by the user, so it stays
    OS88_ABYTE sc_pass              ; the scan's pass (scan.inc)
    OS88_ABYTE sc_sub               ; ...and its step inside the pass
    OS88_ABYTE sc_vol               ; the volume, and its letter
    OS88_ABYTE sc_vlet
    OS88_ABYTE sc_fixed             ; it is a hard disk
    OS88_ABYTE sc_fat16
    OS88_ABYTE sc_foreign           ; KERNEL.SYS is not this release's
    OS88_ABYTE sc_kstate            ; 0 unknown, 1 this release, 2 other, 3 missing
    OS88_ABYTE sc_ascbad            ; ASSOC.DAT wants a rebuild
    OS88_ABYTE sc_depth             ; the walk's depth
    OS88_ABYTE sc_bpp               ; the display's bits per pixel
    OS88_ABYTE sc_compact           ; the short layout (CGA)
    OS88_ABYTE sc_choose            ; the drive chooser is up (118.13)
    OS88_ABYTE sc_isboot            ; the drive being checked is the boot one
    OS88_ABYTE sc_nflop             ; floppy drives, by int 11h
    OS88_ABYTE sc_nvol              ; the chooser's rows...
    OS88_ABYTE sc_vsel              ; ...the selected one...
    OS88_ABYTE sc_vboot             ; ...and the boot volume's, FF none
    OS88_AWORD sc_vclkt             ; the last chooser press's tick
    OS88_ABUF  sc_vols, SC_MAXVOL   ; a row's volume index,
    OS88_ABUF  sc_vkind, SC_MAXVOL  ; ...its medium (VK_*),
    OS88_ABUF  sc_vtran, SC_MAXVOL  ; ...its transport (VT_*)
    OS88_AWORD sc_t0                ; the tick a wake started in
    OS88_AWORD sc_tui               ; the tick the progress lines were drawn
    OS88_AWORD sc_tstart            ; the tick the scan started
    OS88_AWORD sc_secs              ; the scan's length, when it ended
    OS88_AWORD sc_homedx            ; where the instance stood before
    OS88_ABYTE sc_homebl
    OS88_AWORD sc_csys              ; SYSTEM\'s cluster, 0 = none
    OS88_AWORD sc_cstik             ; STIKAPPS\'s cluster, 0 = none
    OS88_AWORD sc_idx               ; the pass's index
    OS88_AWORD sc_mp                ; the manifest entry being read
    OS88_AWORD sc_mi                ; ...and its index
    OS88_AWORD sc_s1                ; the sum (SPEC.md 118.2)
    OS88_AWORD sc_s2
    OS88_AWORD sc_offlo             ; ...and the bytes read so far
    OS88_AWORD sc_offhi
    OS88_ABUF  sc_hdr, 8            ; the file's first eight bytes, raw
    OS88_AWORD sc_mbytes            ; manifest bytes summed so far, in KB
    OS88_AWORD sc_mbytelo           ; ...the remainder under a KB
    OS88_AWORD sc_prog              ; 0..1000
    OS88_AWORD sc_barpx             ; the bar's fill as drawn
    OS88_AWORD sc_nfiles
    OS88_AWORD sc_nfold
    OS88_AWORD sc_npkg
    OS88_AWORD sc_nascapp           ; ASSOC.DAT's program rows: the walk's estimate
    OS88_AWORD sc_nfound            ; folders remembered
    OS88_AWORD sc_nnames            ; packages remembered
    OS88_AWORD sc_nf                ; findings kept
    OS88_AWORD sc_flost             ; ...and not kept
    OS88_AWORD sc_nrm               ; findings Clean Up may remove
    OS88_AWORD sc_freelo            ; free KB, 32 bits
    OS88_AWORD sc_freehi
    OS88_AWORD sc_rseg              ; the report's transient claim
    OS88_AWORD sc_rend              ; ...and its length so far
    OS88_AWORD sc_rdone             ; rows finished, a bit each
    OS88_AWORD sc_rrun              ; rows running, a bit each
    OS88_AWORD sc_dirty             ; what the next draw owes (ui.inc D_*)
    OS88_AWORD sc_drows             ; ...rows owed, a bit each
    OS88_AWORD sc_dfind             ; ...the first findings line owed, FFFF none
    OS88_AWORD sc_wplen             ; the walk path's length
    OS88_AWORD sc_curfold           ; the folder the walk stands in (cluster)
    OS88_AWORD sc_curfi             ; ...and its index in sc_fold, FFFF none
    OS88_ABUF  sc_rbrk, 2 * SC_NCHK ; per check: broken findings
    OS88_ABUF  sc_rwrn, 2 * SC_NCHK ; ...and warnings
    OS88_ABUF  sc_shown, SC_NCHK    ; ...the lamp state each row shows
    OS88_ABUF  sc_cursor, 16        ; os88rseq's cursor
    OS88_ABUF  sc_fbuf, OSAPI_FIND_SZ
    OS88_ABUF  sc_name, 14          ; a name being built (8.3, NUL)
    OS88_ABUF  sc_wpath, SC_PATHN   ; the walk's folder path, "\APPS"
    OS88_ABUF  sc_cpath, FR_PATHN   ; the item being read, "C:\APPS\X.O88"
    OS88_ABUF  sc_rsn, FR_RSNN      ; a reason being built
    OS88_ABUF  sc_line, 100         ; a text run being built
    OS88_ABUF  sc_txt, 100          ; ...and the text it is built from
    OS88_ABUF  sc_lband, 8          ; a 1bpp lamp, inverted
    OS88_ABUF  sc_q, 40             ; Clean Up's question
    OS88_ABUF  sc_wstk, WS_SIZE * SC_MAXDEP
    OS88_ABUF  sc_mstat, 64         ; per manifest entry: bit 0 present
    OS88_ABUF  sc_rects, 8 * SC_NB  ; the buttons, screen, inclusive
    OS88_ABUF  sc_bflags, 2 * SC_NB
    OS88_ABUF  sc_sb, 20            ; the scroll bar's block + what it drew
    OS88_ABUF  sc_geo, SCG_SIZE     ; the layout (ui.inc SCG_*)
    OS88_ABUF  sc_fold, FO_SIZE * SC_MAXFOLD
    OS88_ABUF  sc_names, NM_SIZE * SC_MAXNAME
    OS88_ABUF  sc_ftab, FR_SIZE * SC_MAXF
    OS88_BSS_ALIGNED sc_buf, SC_BUFSZ
