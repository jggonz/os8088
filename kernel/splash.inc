; =============================================================================
; splash.inc - boot-time loading screen (SPEC.md 15)
;
; While the boot sector is still pulling the rest of this kernel off the
; floppy, it far-calls spl_tick (via the pinned 0800:0008 entry) after every
; sector that lands. The first tick switches to mode 12h and draws the
; welcome dialog; every tick advances a pixel-granular progress bar, a
; percentage, and a spinning vector "8088" - stroke digits coin-flipped
; about their vertical axis through a 16-entry cosine table.
;
; This module runs before ANY other module is resident, so it is entirely
; self-contained: BIOS int 10h for mode set and text, its own planar
; primitives for everything else. It must never slow the load down - it
; contains no waits and no timing loops; a completed sector read is the only
; thing that advances it, and the drawing (a few ms at 4.77MHz) happens
; inside the disk's rotational latency toward the next sector.
;
; State lives in in-module data words, never .bss: .bss starts at the image
; end, where the final sector read lands, and nothing has cleared it yet - so
; a .bss write here is a write into memory the loader is still filling. This
; module must stay off .bss.
; The build assertion at the bottom guarantees the module is aboard before
; the first tick can arrive.
; =============================================================================

section .boot2                  ; ...and it lives in STAGE 2 (SPEC.md 2.9.4).
                                ; The switch is HERE and not round the
                                ; %include in kernel.asm, because that is what
                                ; every other module that leaves .text does
                                ; and because tools/kernsize.py's per-module
                                ; markers hand each %include the section they
                                ; end in - a `section` in the caller reads as
                                ; .text to them, and the whole module lands in
                                ; the image instead

SPL_RESIDENT equ 9              ; sectors the boot sector waits for before
                                ; ticking us (must match boot/boot.asm).
                                ; It was 6, and the module ended at 3,072
                                ; bytes EXACTLY - no headroom at all, so the
                                ; next thing that had to be resident (the
                                ; equipment-flag fix in viddet.inc, 32 bytes)
                                ; failed the assertion below rather than
                                ; silently ticking a splash that was not
                                ; aboard.
                                ;
                                ; It is 8 for the second instance of exactly
                                ; that, and the shape is worth recognising:
                                ; the assertion fires on whatever crosses the
                                ; line, not on whatever is to blame. This time
                                ; it was a MERGE - SPEC.md 39.6's 15-byte CGA
                                ; pre-clear in viddet.inc arriving on top of
                                ; the other branch's growth - and the module
                                ; ended 4 bytes over 3,584. Shaving four bytes
                                ; off a module that has twice filled its rung
                                ; buys one more collision; a sector buys 512.
                                ;
                                ; **AND IT IS 9 FOR THE THIRD**, which is the
                                ; same shape a third time and by now a pattern
                                ; worth naming: another MERGE, SPEC.md 72's
                                ; Ethernet driver arriving on top of the
                                ; integration branch's four new API slots. The
                                ; module ended 20 bytes over 4,096, and the 20
                                ; is one API slot: 8 bytes of jump table plus
                                ; its 12-byte X stub, both of which live above
                                ; this module by construction. Nothing in
                                ; splash.inc grew at all - which is precisely
                                ; what "fires on whatever crosses the line"
                                ; means, and why shaving bytes out of the
                                ; splash would have been work aimed at the
                                ; wrong file.
                                ;
                                ; One more sector is one more BIOS read before
                                ; the bar starts moving: ~24 ms on the field
                                ; machine (PERFORMANCE.md's measured sector
                                ; inside a coalesced run), where the figure
                                ; this comment used to quote - a fifth of a
                                ; second - was a sector at the pre-18.91 238 ms
SPL_GC      equ 0x3CE           ; VGA Graphics Controller index port

; gfx_nextrow, brought INTO this module (SPEC.md 2.9.4) - and now a NEAR call
; to spl_nextrow at the foot of the file rather than an inlined macro.
;
; The reason it is not gfx_nextrow is unchanged and is the second of these two:
; a call and ret ACROSS A SEGMENT is 46.7 us against 11, paid per ROW; and its
; own body reads `[cs:vid_rowadd]`, which is right in `.text` and wrong here,
; CS being stage 2's segment, so the same three words have to come through DS.
;
; WHAT CHANGED IS ONLY THE MACRO, and the 46.7 does not apply to it: a near
; call inside `.boot2` is 11 us, three sites at ~1,300 rows a boot is ~14 ms of
; a ~10 s boot, and the fourteen bytes an expansion costs are blob budget the
; SPLSTARS arm has none of.  SPL_NEXTROW_CS below stays a macro: it has ONE
; user, so a call would cost three bytes rather than save eighteen.

; ...and the same three words read through CS, for SPEC.md 15.3.4's row loop -
; the one place in this module that points DS at CS so the copy out of a
; composed buffer can be a `rep movsb` instead of five instructions a byte.
%macro SPL_NEXTROW_CS 0
    add di, [cs:spl_radd]
    test di, [cs:spl_rwbit]
    jz %%r
    add di, [cs:spl_rwfix]
%%r:
%endmacro

; The bar counts SECTORS, and the boot sector's read is not the last of them:
; kmain's tail mounts A:, reads SYSTEM.CFG and loads whatever it asks for,
; which on the 1.44MB system disk is thirteen more sectors before a pixel of
; desktop can be drawn - about a second at PERFORMANCE.md's measured
; 24 ms per 512 bytes delivered (Part 2, Set 24; it was quoted here as 65 from
; Set 17, and 238 before SPEC.md 18.91's AL fix). The bar used to reach 100% at the last sector of the
; kernel and then sit there through all of it.
;
; So SPL_POST notches are added to the DENOMINATOR and spent after the load
; (SPEC.md 15.3): thirteen for that mount, three for the phases of kmain that
; are not I/O (the mouse's ~165 ms reset is the big one), and spl_finish
; forces the last. It is tuned for the common boot - a system disk whose
; SYSTEM.CFG asks for no driver - and it is a CLAMP, not a scale: a machine
; that has enabled the sound or disk driver spends twenty-seven more sectors
; here and the bar waits at 100% for them, which is what every boot used to
; do for all of it.
SPL_POST    equ 27           ; 13 mount + 3 kmain notches + 11 for the mouse
                                ; wait, which charges a notch a TICK now
                                ; (SPEC.md 15.3.3): MOU_RSTLOW + MOU_IDFLOOR is
                                ; what a machine WITH a mouse spends, and a
                                ; machine without one holds instead of jumping

; Spinner: "8088" as 19 stroke segments in a 104x40 box, screen-centred at
; x=320 with its top at SPL_TOP. The coin-flip transform only ever scales x
; about the box centre, so strokes stay horizontal/vertical after it.
SPL_TOP     equ 118
SPL_HALFW   equ 52

; ...and its ANGLE is the wall clock (SPEC.md 15.3.6). It was [spl_done] & 15 -
; the progress bar - and a boot notches that bar at three rates that differ
; eighteen-fold: once per coalesced int 13h run in stage 2 (~430 ms), once per
; SECTOR at the boot mount (~24 ms), once per system tick in the mouse wait.
; The logo inherited all three and read as turning a little, stopping, and
; turning further. The bar is SUPPOSED to move in fits and starts, because that
; is the truth about the disk; the logo is not.
;
; One of the sixteen positions per 1 << SPL_SPINSH ticks, so a revolution is
; 16 << SPL_SPINSH of them: at 2 that is four ticks a position, 64 a revolution
; and 3.52 s of it at 18.2065 Hz.
;
; The value was picked off the FRAME rate - the spinner was composed ~29 times
; in a boot, half a second apart - and 15.3.8 removed that constraint by moving
; the animation onto IRQ0. Every one of the sixteen positions is drawn now, on
; time, so this is a LOOK question with nothing mechanical left in it: 1 gives
; 1.76 s a revolution and 0 gives 0.88, and all three are equally smooth.
SPL_SPINSH  equ 2

; Dialog and bar geometry. The dialog is VGA-only and stays at its 640x480
; coordinates; the BAR is the one thing that draws on all three adapters, so
; its origin is computed at run time by spl_chrome (SPEC.md 39.6).
SPL_BAR_PX  equ 288             ; bar interior width in pixels
SPL_BAR_Y   equ 264             ; bar interior top row on VGA - inside the
                                ; trough spl_chrome draws at rows 262..281
SPL_BAR_H   equ 16
SPL_TTL_Y   equ 244             ; VGA's rows. Every COLUMN is the screen's
SPL_PCT_Y   equ 308             ; centre plus a constant and so needs no
SPL_DLG_T   equ 208             ; adapter of its own (SPEC.md 15.3.5.1); these
SPL_DLG_B   equ 343             ; five are the vertical layout, and mono packs
                                ; the same elements around the bar because
                                ; VGA's 226-row block does not fit 200 rows
SPL_TTLC    equ 17              ; the caption's cells, and the percentage's
SPL_PCTC    equ 4

; -----------------------------------------------------------------------------
; The entries the KERNEL still reaches, and it reaches them FAR: the
; splash is in `.boot2` and the kernel is not (SPEC.md 2.9.4). Four bytes each,
; the boot overlay's ovl_* shape exactly, so every body below keeps its near
; `ret` and changed in no other way.
; -----------------------------------------------------------------------------
splf_step:      call spl_step
                retf            ; ...and spl_step KEEPS its shim, because
                                ; spl_stepq near-calls the body two screens
                                ; down: a `retf` on it would return into
                                ; nowhere from there
;
; THE OTHER THREE HAVE NO SHIM ANY MORE (SPEC.md 2.6.1, 15.3.9). spl_stepq,
; spl_finish and spl_reset have exactly one `ret` apiece and NO near caller
; anywhere in this section, so each body owns its own far return and the four
; bytes in the middle are deleted. Twelve bytes of `.boot2`, which is the same
; pool the boot overlay draws from (SPEC.md 2.9.12) - `OVL_AT` moves for
; nothing, so what the loader gives back the overlay can have.
splf_anim:      call spl_anim
                retf            ; ...and SPEC.md 15.3.8.2's: sch_isr owns int
                                ; 08h for the second half of the loading
                                ; screen's life, so the animation reaches the
                                ; blob the same way every other kernel caller
                                ; does
splf_fill:      call spl_fill
                retf            ; ...and SPEC.md 15.5's BOOTMARK beacon, which
                                ; borrows the splash's byte-granular fill and
                                ; is `.text`. os88ovlchk found this one: two
                                ; near calls that were right for as long as
                                ; both ends were in the same segment

; -----------------------------------------------------------------------------
; spl_tick - stage 2 calls this after every run it reads (SPEC.md 15).
; in: AX = sectors loaded, DX = total sectors. Preserves every register and
; segment; flags clobbered. Runs on the boot stack, boot sector's segments.
; -----------------------------------------------------------------------------
spl_tick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov bx, ax                  ; AX IS THE ARGUMENT AND LOADING DS SPENDS IT.
                                ; An 8086 has no `mov ds, imm`, so the segment
                                ; goes through a general register - and this
                                ; routine took the sector count in AX, which
                                ; is why the count is banked FIRST and every
                                ; use below reads BX (SPEC.md 15.3.1).
                                ;
                                ; It cost a whole boot's worth of progress bar:
                                ; `mov [cs:spl_done], ax` stored KERNEL_SEG,
                                ; so the bar sat at 96/spl_total = 44% for the
                                ; entire kernel load and only moved again when
                                ; dsk_xfer's spl_step took over. In `.text`
                                ; there was no `mov ds` here at all - DS was
                                ; already the kernel's - so the instruction
                                ; that broke it is the one SPEC.md 2.9.4 added.
    mov ax, KERNEL_SEG          ; DS IS THE KERNEL'S, not ours (SPEC.md 2.9.4):
    mov ds, ax                  ; every vid_* and gfx_* word below is plain,
                                ; and our own data is reached `cs:` - the boot
                                ; overlay's contract, one section along

    add dx, SPL_POST            ; kmain's tail is on this bar too (SPEC 15.3)
    mov [cs:spl_total], dx
    cmp bx, dx                  ; ...and CLAMP: a bound on what a CALLER may
    jbe .cnt                    ; pass, nothing more. The loader may load the
    mov bx, dx                  ; kernel TWICE (SPEC.md 18.93/18.93.1) and the
.cnt:                           ; bar climbs from 0 again when it does - the
    mov [cs:spl_done], bx          ; sector derives the count from what is LEFT

    inc byte [spl_busy]        ; SPEC.md 15.3.8.3: the one lock, and spl_isr
                                ; refuses the tick while it is up
    cmp byte [cs:spl_on], 0
    jne .running
    mov byte [cs:spl_on], 1
    mov byte [spl_live], 1      ; ...and from here the splash OWNS the screen
    call spl_chrome             ; and the video mode, until spl_finish
    call spl_hook               ; ...and IRQ0 (15.3.8.2). HERE and not in
                                ; spl_chrome's body, which spl_rechrome shares:
                                ; a hard-disk boot starts the splash from
                                ; inside kmain with sched_init already holding
                                ; the vector, and sch_isr's gated call covers it
    call spl_kern_on            ; ...and SAY what the next nine seconds are
                                ; (SPEC.md 15.6.5). Here and not in spl_chrome
                                ; because spl_rechrome shares that body: on a
                                ; hard-disk boot nothing is loading a kernel
                                ; and spl_step's start path must not say so
.running:
    call spl_paint
    dec byte [spl_busy]

    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret                         ; NEAR: stage 2 draws the splash and stage 2
                                ; calls it, so the pinned 0060:0008 far entry
                                ; this used to answer at is gone (SPEC.md 2.9.4)

; -----------------------------------------------------------------------------
; spl_paint - one frame of the loading screen, from [cs:spl_done]/[cs:spl_total].
;             clobbers: ax, bx, cx, dx, si, di, bp, es
;
; Factored out of spl_tick so the boot sector's ticks and kmain's post-load
; notches (spl_step) cannot disagree about what a frame is.
; -----------------------------------------------------------------------------
spl_paint:
    call spl_bar                ; the ONE stage that runs on all three
                                ; adapters - see spl_chrome
    call spl_pct                ; ...on ALL THREE adapters now (SPEC.md 15.3.5)
    cmp byte [spl_quiet], 0     ; ...and the animation only when the caller can
    jne .out                    ; afford it (SPEC.md 15.3.3.1). Through DS, not
                                ; `cs:`: the flag is viddet.inc's now, so that
                                ; the two IRQ0 handlers can read it too
    call spl_anim               ; ...which since 15.3.8 is IRQ0's job, and this
                                ; call is the FAIL-SAFE: on a machine whose
                                ; IRQ0 never arrives the logo still moves, on
                                ; the notch, exactly as it did before. The
                                ; phase test inside makes it a compare when the
                                ; interrupt is doing its work
.out:
    ret

; -----------------------------------------------------------------------------
; spl_step - one post-load notch on the bar (SPEC.md 15.3)
; in:  nothing;  DS = KERNEL_SEG (every caller is kernel code)
; out: nothing - EVERY register and the flags preserved, because the hot
;      caller is the middle of dsk_xfer's per-sector loop
;
; A no-op unless the splash is on screen, which is the whole of its safety:
; after spl_finish this is a compare and a return, and it stays on the disk
; path for the life of the machine at that price (~13 us against a sector's
; ~24 ms).
;
; ...AND IT STARTS THE SPLASH ITSELF WHEN NOBODY ELSE HAS (SPEC.md 2.9.9.1).
; It could not, and that cost the hard disk its loading screen: spl_tick raises
; [spl_live] and STAGE 2 is the only caller of spl_tick, so a loader that does
; not tick - and boot/boothd.asm has no business drawing a progress bar out of
; 512 bytes - left this routine a no-op for the whole boot.
;
; The two states it has to tell apart are "nobody has started me" and "I am
; finished", and both read [spl_live] = 0. [cs:spl_on] is the difference: it is
; set once and never cleared, so the FIRST call ever made can start the screen
; and every call after spl_finish stays the compare-and-return above. Seeding
; the total with SPL_POST rather than a sector count is not a shortfall - on a
; machine whose loader finished the load before the kernel ran, kmain's own
; tail IS what is left to measure.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; spl_stepq - one notch WITHOUT the spinner (SPEC.md 15.3.3.1)
; in:  nothing;  DS = KERNEL_SEG.  out: nothing - every register and the flags
;      preserved, spl_step's own contract, which the two `mov`s below keep
;      (an immediate to memory touches no flag and spends no register)
;
; spl_paint is 32.4 ms and spl_spin is 28.7 of them. That is affordable between
; two sectors and NOT affordable inside mouse_init's identify window, where an
; 8250 holds one byte and a 1200-baud 7N1 frame is 7.5 ms: 32.4 ms away loses
; three of the burst 9.4 exists to capture. Bar and percentage together are
; 3.7 ms, which clears one byte time twice over.
;
; It sets the flag, calls the ordinary notch and clears it again, so there is
; ONE body and no other caller has to know this entry exists.
; -----------------------------------------------------------------------------
spl_stepq:
    mov byte [spl_quiet], 1
    call spl_step
    mov byte [spl_quiet], 0
    retf                        ; FAR: SPEC.md 15.3.9 deleted the splf_ shim

spl_step:
    pushf
    cmp byte [spl_live], 0
    jne .go
    cmp byte [cs:spl_on], 0
    jne .out                    ; spl_finish has been and gone
.go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    inc byte [spl_busy]        ; SPEC.md 15.3.8.3's lock. DS is the kernel's
                                ; on every caller of this entry, which is what
                                ; lets a `.text` byte be the one the ISR shares
    cmp byte [cs:spl_on], 0
    jne .notch
    mov byte [cs:spl_on], 1     ; the first call of a boot nobody ticked
    mov byte [spl_live], 1
%ifdef MOU_DIAG
    mov ax, [ticks]             ; SPEC.md 9.4.6.2: everything below this line is
    mov [mdg_nlive], ax         ; drawn INSIDE the frame the bar first shows, and
%endif                          ; on a hard disk that is the loading screen's
                                ; whole chrome. DS is the kernel's here
    mov word [cs:spl_total], SPL_POST
    mov word [cs:spl_done], 0
    call spl_rechrome           ; ...and NOT spl_chrome (SPEC.md 2.9.9.2): the
                                ; three calls it opens with are vid_detect,
                                ; vid_apply and vid_setmode, and every caller
                                ; that can reach THIS path is downstream of
                                ; kmain's vid_init, which has already run all
                                ; three. spl_tick is stage 2's alone and keeps
                                ; the full entry; a hard-disk boot used to set
                                ; the mode twice and pay two of vid_setmode's
                                ; ~110 ms wipes for the second one
    jmp short .draw
.notch:
    mov ax, [cs:spl_done]
    inc ax
    cmp ax, [cs:spl_total]
    jb .set
    mov ax, [cs:spl_total]         ; the last notch is spl_finish's, so a boot
    dec ax                      ; that overruns its allowance crowds up
.set:                           ; against 100% instead of claiming to be done
    mov [cs:spl_done], ax
.draw:
    call spl_paint
    dec byte [spl_busy]
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; spl_finish - the bar to 100%, and hand the screen over (SPEC.md 15.3)
; in:  nothing;  DS = KERNEL_SEG
; out: nothing (every register and the flags preserved)
;
; Called by kmain immediately before the first wm_paint_all, which is the last
; moment at which a full bar is still the truth: the desktop paint covers
; every pixel of the screen (the menu bar's field, the desktop dither over the
; dock strip, then the dock), so the loading screen needs no erase of its own.
;
; Clearing [spl_live] is also what re-arms vid_setmode: from here on the mode
; is the kernel's to change again (SPEC.md 39.1).
; -----------------------------------------------------------------------------
spl_finish:
    pushf
    cmp byte [spl_live], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    inc byte [spl_busy]        ; SPEC.md 15.3.8.3's lock
    mov ax, [cs:spl_total]
    mov [cs:spl_done], ax
    call spl_paint
    mov byte [spl_live], 0      ; ...and the ISR is refused from here by THIS
    dec byte [spl_busy]        ; byte, before the blob is given back (2.9.5)
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    popf
    retf                        ; FAR: SPEC.md 15.3.9 deleted the splf_ shim

; -----------------------------------------------------------------------------
; spl_reset - put the loading screen back after a mode switch (SPEC.md 39.11.3)
;
; in:       nothing
; out:      nothing (all registers preserved)
; clobbers: -
;
; vid_setmode clears the framebuffer, so a switch taken from drv_boot - while
; the boot is still reading sectors and ticking the bar - wipes the loading
; screen. This redraws it: the chrome from spl_rechrome, which is spl_chrome
; without the adapter re-probe that would undo the switch, and then one
; ordinary frame from the progress the bar had already reached.
;
; IT LIVES IN splash.inc NOW. It was in vidsel.inc because splash.inc had to
; fit the image's opening SPL_RESIDENT sectors and this is not reachable from
; the boot sector - a budget that stopped existing when the whole module moved
; to stage 2 (SPEC.md 2.9.4).
;
; [spl_live] is the guard, and it is what makes this safe to call from
; anywhere: after spl_finish the splash owns nothing and drawing a dialog
; over the desktop would be a bug. The Control Panel's switch therefore
; never calls it and would be harmless if it did.
; -----------------------------------------------------------------------------
spl_reset:
    cmp byte [spl_live], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    inc byte [spl_busy]        ; SPEC.md 15.3.8.3's lock: vid_setmode has just
                                ; wiped the screen and this is what puts it
                                ; back, so a frame landing inside it would draw
                                ; a logo the very next call erases
    mov byte [cs:spl_anlast], 0xFF  ; ...and the animation is FORCED, because
                                ; the wipe took it and its phase has not moved
    call spl_rechrome
    call spl_paint
    OVLCALL ovl_spl_redraw      ; ...and SPEC.md 15.6's status line with it.
                                ; spl_paint deliberately does NOT draw it - a
                                ; per-sector font_run is tens of milliseconds
                                ; against a ~24 ms sector - so the ONE path
                                ; that wipes the framebuffer under a live line
                                ; is the one that has to put it back, at the
                                ; NEW geometry. After kmain gives the blob
                                ; back this reaches spl_dead and returns
                                ; (2.9.5), which is the right answer: by then
                                ; the desktop owns the screen and [spl_live]
                                ; would have refused anyway
    dec byte [spl_busy]
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    retf                        ; FAR: SPEC.md 15.3.9 deleted the splf_ shim

; -----------------------------------------------------------------------------
; spl_chrome - first tick only: mode 12h, the double-framed welcome dialog,
;              the bar trough, the title. clobbers: ax, bx, cx, dx, si, di,
;              bp, es
; -----------------------------------------------------------------------------
spl_chrome:
    call KERNEL_SEG:spw_vid_detect ; the adapter probe happens HERE, before any
    call KERNEL_SEG:spw_vid_apply ; other kernel module is even resident: the
    call KERNEL_SEG:spw_vid_setmode; splash owns the first mode set, and kmain's
                                ; vid_init later re-runs all three (SPEC 39.1)

; ...and the same chrome onto a mode SOMEBODY ELSE has just set. TWO callers
; enter here, and neither may re-run the three calls above:
;
;   spl_reset, for when drv_boot moves the machine onto the adapter SYSTEM.CFG
;   asks for while the loading screen is still up (SPEC.md 39.11.3). Entering
;   above would re-run vid_detect, which answers with the PROBE's adapter and
;   would undo the switch three instructions after it happened.
;
;   spl_step's start path, for a boot whose loader ticked nothing (SPEC.md
;   2.9.9.2). Every caller of spl_step is in the kernel proper and therefore
;   downstream of vid_init, which has already probed, applied and SET the
;   mode - so entering above is a second BIOS mode set and a second ~110 ms
;   wipe of a framebuffer this routine is about to draw on anyway.
;
; Costs nothing either way: it is a label, and the geometry below is read live.
spl_rechrome:
    mov ax, [vid_cw]            ; --- the layout, banked once (SPEC.md 15.3.5)
    shr ax, 1                   ; the x centre: EVERY column below is this plus
    mov [cs:spl_l_cx], ax       ; a constant, and at 320 they are the literals
    sub ax, 56                  ; they replace. The spinner's box is 120 wide
    and al, 0xF8                ; and centred, floored to a byte
    mov [cs:spl_l_spp], ax
    mov cl, 3
    shr ax, cl
    mov [cs:spl_l_spx], ax

    mov ax, SPL_BAR_Y           ; the bar's origin. On VGA the dialog's trough
    cmp byte [vid_mono], 0      ; is at 264 and the bar sits in it, so VGA
    je .bary                    ; keeps its CONSTANT and never reads [vid_ch];
    mov ax, [vid_ch]            ; mono places the bar so that the BLOCK is
    shr ax, 1                   ; centred, which is +10 and not the -8 it was
    add ax, 10                  ; when the bar was the only thing on the screen
.bary:                          ; (SPEC.md 15.3.5.1): the block runs bar-81 to
    mov [cs:spl_l_bar], ax      ; bar+59, so centring it in vid_ch puts the bar
                                ; at vid_ch/2 + 10
    mov bx, ax                  ; ...and 15.6's line, which is bar + 24 on
    add ax, 24                  ; EVERY adapter: 264 + 24 is SPL_MSG_Y. It was
    mov [cs:spl_l_msg], ax      ; computed three times from two formulas -
                                ; here, spl_kern_on and vidsel.inc's spl_mgeom
                                ; - and banking it is what makes moving the
                                ; mono layout one edit instead of three.
                                ; Computed HERE, while AX is still the bar,
                                ; because the walk below spends AX

    mov si, spl_lay_tab         ; ...and the five rows AROUND the bar, from a
    mov di, spl_l_ttl           ; TABLE rather than two spelled-out arms: VGA
    mov cx, 5                   ; by its own constant, mono by a signed delta
.lrow:                          ; from the bar, because a 200-row screen cannot
    mov ax, [cs:si]             ; take VGA's padding. Five consecutive words,
    cmp byte [vid_mono], 0      ; which the guard beside spl_lay_tab is what
    je .lst                     ; keeps true
    mov al, [cs:si+2]
    cbw
    add ax, bx
.lst:
    mov [cs:di], ax
    add si, 3
    inc di
    inc di
    loop .lrow

    mov ax, bx                  ; AX = the bar row again, for the row base
    call KERNEL_SEG:spw_gfx_rowbase
    mov bx, [vid_cw]            ; x = (width - bar) / 2, in whole bytes: 22 at
    sub bx, SPL_BAR_PX          ; 640 (which is what SPL_BAR_ADR always was),
    mov cl, 4                   ; 27 at Hercules' 720
    shr bx, cl
    add ax, bx
    mov [cs:spl_bar_adr], ax

    mov es, [vid_seg]
                                ; ...and ALL THREE ADAPTERS draw it now
                                ; (SPEC.md 15.3.5). It was VGA-only because
                                ; the chrome was BIOS text and int 10h
                                ; teletype into a Hercules card already in
                                ; graphics writes character/attribute pairs
                                ; into the bitmap. 15.3.2 took the teletype
                                ; out; what was left was the pixel literals.
    mov bx, [cs:spl_l_dt]       ; dialog: two nested 1px frames, the mode 3
    mov dx, [cs:spl_l_db]       ; splash's double-line box reborn in pixels
    mov ax, 168                 ; ...each one a HALF-WIDTH about the banked x
    call spl_rectc              ; centre - the three sites differed in nothing
    mov bx, [cs:spl_l_dt]       ; else
    add bx, 4
    mov dx, [cs:spl_l_db]
    sub dx, 4
    mov ax, 164
    call spl_rectc

    mov bx, [cs:spl_l_bar]      ; bar trough, 1px inset from the fill
    mov dx, bx
    dec bx
    dec bx
    add dx, SPL_BAR_H + 1
    mov ax, 146
    call spl_rectc

    mov ax, [cs:spl_l_ttl]      ; the caption, BLITTED (SPEC.md 15.3.2) - it
    mov bx, SPL_TTLC            ; was 18 BIOS teletype characters and they
    call spl_cell               ; cost 40 ms each, 717 ms of a screen whose
    mov si, spl_s_welcome       ; whole contract is that it costs nothing
    jmp spl_text                ; tail call

; -----------------------------------------------------------------------------
; spl_cell - ES:DI = the first byte of a field BX cells wide, centred on the
;            screen, with its top on row AX.  clobbers: ax, bx, cx, es
; -----------------------------------------------------------------------------
spl_cell:
    call KERNEL_SEG:spw_gfx_rowbase
    push ax
    mov ax, [cs:spl_l_cx]
    mov cl, 2
    shl bx, cl                  ; BX = half the field, in pixels (cells * 8 / 2)
    sub ax, bx
    mov cl, 3
    shr ax, cl                  ; ...and the byte it starts in
    pop bx
    add ax, bx
spl_setdi:                      ; ...and the tail spl_spinadr shares: AX is a
    mov di, ax                  ; framebuffer offset, so hand it back as ES:DI
    mov es, [vid_seg]
    ret

; -----------------------------------------------------------------------------
; spl_spinadr - ES:DI = the spinner box's top-left byte.  clobbers: ax, es
; -----------------------------------------------------------------------------
spl_spinadr:
    mov ax, [cs:spl_l_spn]
    call KERNEL_SEG:spw_gfx_rowbase
    add ax, [cs:spl_l_spx]
    jmp short spl_setdi

; -----------------------------------------------------------------------------
; spl_frac - AX = done * CX / total.  clobbers: ax, dx
;
; [spl_total] is never 0: spl_tick seeds it and spl_step seeds SPL_POST.
; -----------------------------------------------------------------------------
spl_frac:
    mov ax, [cs:spl_done]
    mul cx
    div word [cs:spl_total]
    ret

; -----------------------------------------------------------------------------
; spl_bar - fill the bar to done*SPL_BAR_PX/total pixels. The fill only ever
;           grows, so drawing just the current extent is complete.
; clobbers: ax, bx, cx, dx, si, di, es
; -----------------------------------------------------------------------------
spl_bar:
    mov es, [vid_seg]

    mov cx, SPL_BAR_PX
    call spl_frac               ; AX = filled pixels, 0..SPL_BAR_PX
    mov si, ax
    mov cl, 3
    shr ax, cl                  ; AX = whole bytes of fill

    mov di, [cs:spl_bar_adr]
    mov cx, ax
    mov bx, SPL_BAR_H
    mov al, 0xFF
    jcxz .partial
    call spl_fill
    add di, cx                  ; spl_fill preserves DI/CX; step past the fill
.partial:
    and si, 7                   ; leftover pixels into the next byte
    jz .done
    mov cx, si
    neg cl
    add cl, 8
    mov al, 0xFF
    shl al, cl                  ; leading SI bits set
    call spl_bitmask
    mov bx, SPL_BAR_H
    call spl_mcol               ; the armed mask down BX rows, then the GC
.done:                          ; back to its default
    ret

; -----------------------------------------------------------------------------
; spl_pct - right-aligned percentage under the bar, BLITTED (SPEC.md 15.3.2).
; clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
spl_pct:
    mov cx, 100
    call spl_frac               ; AX = percent, 0..100

    mov word [cs:spl_pctbuf], '  '     ; digits grow leftward over the blanks
    mov bx, spl_pctbuf + 2
    mov cx, 10
.digit:
    xor dx, dx
    div cx
    add dl, '0'
    mov [cs:bx], dl
    dec bx
    test ax, ax
    jnz .digit

    mov ax, [cs:spl_l_pct]      ; ...and blitted, which is what actually pays:
    mov bx, SPL_PCTC            ; four characters a TICK and ~16 ticks a load
    call spl_cell               ; was ~2.5 s of a VGA boot (SPEC.md 15.3.2).
    mov si, spl_pctbuf          ; The leading blanks are what erase the
                                ; previous reading - spl_text is opaque, so
                                ; ' 9%' -> '10%' needs no separate wipe
    jmp spl_text                ; tail call

; -----------------------------------------------------------------------------
; spl_kern_on - 'Loading Kernel', for the stretch that IS a floppy boot
;               (SPEC.md 15.6.5)
; in:  nothing.  out: nothing.  clobbers: ax, bx, cx, si, di, bp, es
;
; IT PRESERVED EVERY REGISTER AND NOTHING WANTED THAT.  Its one caller is
; spl_tick, which banks all nine at its own entry and holds nothing across
; this - and the call two lines above it (spl_chrome) already clobbers the
; same set.  Eight pushes and eight pops of the blob's budget bought a
; contract with no reader.
;
; Nine of a floppy boot's twelve seconds are this read and 15.6's line cannot
; say so: that line is font_run into the framebuffer and font_run is `.text` -
; the very thing being read. So the glyphs are blitted HERE, out of the ROM
; 8x8 set, with this module's own primitives.
;
; **AND NOT WITH BIOS TELETYPE, WHICH IS WHAT THIS DID FIRST.** int 10h AH=0Eh
; into mode 12h costs **40 ms A CHARACTER** on a 4.77MHz 8088 - measured, and
; linear: one character added 40.9 ms to the load and fourteen added 557.4.
; Writing the caption and taking it down again was 1,075 ms on a 9.3 s read,
; which is 11% spent to say what the 11% was doing. spl_wrbyte writes the same
; glyph in eight byte-stores. The same measurement prices what is ALREADY here:
; spl_pct is four teletyped characters a tick and ~16 ticks a load, so the
; percentage alone is ~2.6 s of a VGA boot (15.3.2).
;
; It lands in 15.6's OWN field, so the boot has one place where it says what it
; is doing, and at 8 rows rather than a teletype cell's 16 - which is what lets
; it need no erase at all: `Looking for Mouse` is the same 8 rows at the same
; origin, padded to the same width, and covers it exactly (15.6.4).
;
; Raised from spl_tick and NOT from spl_chrome, which is the distinction
; SPEC.md 2.9.9.2 draws one routine along: spl_rechrome shares spl_chrome's
; body and spl_step's start path enters there, and on a hard-disk boot nothing
; is loading a kernel.
; -----------------------------------------------------------------------------
SPL_KCELLS  equ 32              ; the field, in cells - spl_mgeom's
SPL_KY      equ 288             ; SPL_MSG_CELLS and SPL_MSG_Y, which live in
                                ; `.ovl` and are %included AFTER this module.
                                ; vidsel.inc asserts the pair rather than
                                ; trusting the comment: two copies of a
                                ; geometry that must agree is exactly what a
                                ; build check is for

spl_kern_on:
    ; --- 15.6's field, at the row spl_rechrome banked ----------------------
    ; The typeface is spl_text's own question now (spl_kfont), asked at the
    ; first glyph of the boot whichever caller gets there first.
    mov ax, [cs:spl_l_msg]
    mov bx, SPL_KCELLS
    call spl_cell
    mov si, spl_s_kern
    jmp spl_text                ; tail call, spl_pct's and spl_rechrome's shape

; -----------------------------------------------------------------------------
; spl_kfont - the ROM 8x8 set into [cs:spl_kf*], once a boot
; in:  nothing.  out: nothing (every register preserved)
;
; font_init's own question (SPEC.md 6.2), asked here rather than taken from
; font_glyphs - which is LOW_SEG and is not filled until kmain - and asked of
; the BIOS, which is the one thing that IS resident this early. BAKED_FONT
; would put a copy in the blob and is not an option: it is a knob (`FONT=`),
; and a shipped kernel has no baked typeface.
;
; [spl_kfseg] is the latch as well as the answer: AX=1130h reports through
; ES:BP and F000:FA6E is the fallback, so a segment of 0 cannot be a result.
; It has to be a latch because spl_pct reaches spl_text every tick and this
; is an int 10h - the one call the whole of 15.3.2 exists to stop making.
; -----------------------------------------------------------------------------
spl_kfont:
    cmp word [cs:spl_kfseg], 0
    jne .out
    push dx                     ; AX=1130h answers in ES:BP and spends CX/DL;
    push bp                     ; ES is spl_text's framebuffer and BP its cell
    push es
    xor ax, ax
    mov es, ax
    xor bp, bp
    mov ax, 0x1130
    mov bh, 0x03
    int 0x10
    mov ax, es
    or ax, bp
    jnz .have
    mov ax, 0xF000              ; no EGA+ BIOS: the IBM PC's own 8x8 set
    mov es, ax
    mov bp, 0xFA6E
.have:
    mov [cs:spl_kfseg], es
    mov [cs:spl_kfoff], bp
    pop es
    pop bp
    pop dx
.out:
    ret

; -----------------------------------------------------------------------------
; spl_text - blit an ASCIIZ string, 8x8 OPAQUE, one byte a row
; in:  CS:SI -> the string, ES:DI = the top-left BYTE of the first cell
; out: nothing.  clobbers: ax, bx, cx, si, di
;
; One store a row, and the same store on all three adapters: with the Bit Mask
; at 0FFh every bit of the byte comes from the CPU, so a VGA plane takes the
; glyph row as it stands - white where a bit is set and BLACK where it is not -
; and a 1bpp framebuffer is that byte already. spl_fill's own write (15.3).
;
; OPAQUE is what makes it a renderer rather than a stencil, and it is the whole
; of why spl_pct can use it: the reading changes every tick and the leading
; blanks of spl_pctbuf are the erase. The mask+merge pair this drew with first
; left the previous digits underneath, and cost a latch read and two OUTs a row
; to do it. Nothing writes a pixel twice here (PERFORMANCE.md, rule 2).
; -----------------------------------------------------------------------------
spl_text:
    call spl_kfont              ; the ROM set, once a boot - ES is the caller's
    mov al, 0xFF                ; framebuffer and this preserves it
    call spl_bitmask            ; every bit of the byte from the CPU
.char:
    mov al, [cs:si]
    inc si
    or al, al
    jz .done
    push si                     ; the STRING cursor; the CELL rides in BP,
    mov bp, di                  ; because DI walks eight rows below and
    call spl_kglyph             ; spl_kglyph spends it as well (-> spl_krow)
    mov si, spl_krow
    mov bx, 8
    mov di, bp
.row:
    mov al, [cs:si]
    inc si
    mov [es:di], al             ; opaque, one pass - see the header
    call spl_nextrow
    dec bx
    jnz .row
    mov di, bp                  ; back to the cell, and on to the next BYTE
    inc di
    pop si
    jmp short .char
.done:
    ret                         ; the mask is still 0FFh, which everything else
                                ; in this module assumes on entry

; -----------------------------------------------------------------------------
; spl_kglyph - the eight rows of character AL into [cs:spl_krow]
; in:  AL = the character (>= FONT_FIRST), the ROM set in [cs:spl_kf*]
; out: nothing.  clobbers: ax, cx, si, di
;
; BUFFERED rather than read in place, because three segments are live at once
; and there are only two to read through: ES is the framebuffer and DS is the
; KERNEL's, which SPL_NEXTROW needs for [vid_rowadd] and the wrap pair. Eight
; bytes of `.boot2` is cheaper than banking three words and a second macro.
; -----------------------------------------------------------------------------
spl_kglyph:
    push ds
    mov ah, 0                   ; ...and NO `sub al, FONT_FIRST`: the ROM set
                                ; is indexed from character 0. font_init's own
                                ; `add si, FONT_FIRST * 8` is what makes
                                ; font_glyphs the table that needs subtracting
    mov cl, 3
    shl ax, cl
    add ax, [cs:spl_kfoff]
    mov si, ax
    mov ds, [cs:spl_kfseg]
    mov di, spl_krow
    mov cx, 8
.b:                             ; lodsb, not mov+inc: DS:SI is the ROM font's
    lodsb                       ; and is what this loop already read through,
    mov [cs:di], al             ; and DF=0 is stage 2's entry `cld` - the same
    inc di                      ; one spl_fill's `rep stosb` rests on.  The
    loop .b                     ; destination stays [cs:di], so ES is untouched
    pop ds
    ret

; -----------------------------------------------------------------------------
; spl_clock - AX = the low word of the BIOS 18.2065 Hz tick count at 0040:006C.
;             clobbers: ax, es
;
; THE WORD IS LIVE AT BOTH ENDS OF THE LOADING SCREEN (SPEC.md 15.3.6), which
; is the whole of why the animation may rest on it: during the load the BIOS
; still owns int 08h, and after sched_init sch_isr CHAINS the BIOS handler
; first (SPEC.md 8.1). It is the same guarantee 15.4's boot timer already rests
; on - the boot sector reads this word and kmain reads it again at the first
; desktop frame. A 16-bit read is atomic on an 8088 because the interrupt is
; taken between instructions, and the word wraps once an hour, which a boot is
; not.
;
; ES and not DS: every caller here has the KERNEL's DS and needs to keep it,
; and each of them clobbers ES on the way to the framebuffer anyway.
; -----------------------------------------------------------------------------
spl_clock:
    mov ax, 0x40
    mov es, ax
    mov ax, [es:0x6C]
    ret

; -----------------------------------------------------------------------------
; spl_spin - one animation step: the "8088" with x scaled by cos(angle), angle
;            = the WALL CLOCK mod 16 of a revolution (SPEC.md 15.3.6). cos < 0
;            mirrors the endpoints, so the back face renders itself.
;            clobbers: ax, bx, cx, dx, si, di, bp, es
;
; IT COMPOSES EACH ROW AND STORES IT ONCE (SPEC.md 15.3.4). It used to blank
; the whole 15x41 box with spl_fill and then stroke nineteen lines into it -
; the erase-then-letter pair at the largest scale in the tree, 615 bytes
; cleared to redraw about 370, with the logo ABSENT in between. A boot
; repaints ~29 times half a second apart, so it is not subliminal: it reads as
; the logo BLINKING and was reported from the field as exactly that.
;
; The shape is what makes the composer cheap: 38 of the 41 rows are the same
; row - the eight verticals - and only 0, 20 and 40 carry a bar. So two 15-byte
; buffers hold every row this frame needs, they are built in RAM where a store
; is a store, and the box is written top to bottom with each byte visited
; ONCE. No blank, no read-modify-write, no Graphics Controller OUT.
; -----------------------------------------------------------------------------
SPL_SPINB   equ 15              ; the spinner box, in bytes
SPL_SPINR   equ 41              ; ...and its rows. WHERE it is is the layout's
                                ; (SPEC.md 15.3.5.2): it was SPL_TOP * 80 + 33,
                                ; a hard stride of 80 that is VGA's and not
                                ; Hercules' 90
spl_spin:
%ifdef SPLSTARS
    mov word [cs:spl_cos], 64   ; SPEC.md 15.3.7: the logo is STILL and the
                                ; stars are the animation. It is composed and
                                ; written every frame all the same - the box
                                ; is every-pixel-once (15.3.4), so redrawing
                                ; the same picture is invisible, and both arms
                                ; of the A/B then pay the identical logo cost
%else
    call spl_clock              ; SPEC.md 15.3.6: the angle is the WALL CLOCK,
    mov cl, SPL_SPINSH          ; and [spl_done] drives the BAR alone
    shr ax, cl
    and ax, 15
    mov bx, ax
    mov al, [cs:spl_cos_tab + bx]  ; the table is signed BYTES; [spl_cos] stays
    cbw                            ; a word, which spl_xform's imul needs
    mov [cs:spl_cos], ax
%endif

    mov ax, [vid_rowadd]        ; the row walk's three words, banked into CS
    mov [cs:spl_radd], ax       ; because everything below runs with DS = CS
    mov ax, [vid_wrapbit]
    mov [cs:spl_rwbit], ax
    mov ax, [vid_wrapfix]
    mov [cs:spl_rwfix], ax
    call spl_spinadr            ; ES:DI, through gfx_rowbase like every other
                                ; row here - so CGA's bank interleave and
                                ; Hercules' four are SPL_NEXTROW_CS's, as they
                                ; already are for the bar. BEFORE the DS
                                ; switch below: it reads [vid_seg], which is
                                ; the KERNEL's, and pointing DS at CS first
                                ; read a `.boot2` byte instead and left ES
                                ; wherever that landed - the spinner vanished
                                ; on all three adapters
    push ds
    mov ax, cs
    mov ds, ax                  ; ...so the row copy is one `rep movsb`

    xor al, al                  ; ...the plain row: verticals only
    mov bx, spl_rowa
    call spl_rowmk
    mov al, 0x0F                ; ...and every digit's bar, for rows 0 and 40
    mov bx, spl_rowb
    call spl_rowmk

    call spl_putrow             ; row 0
    mov cx, 19
    call spl_putplain           ; rows 1..19
    mov al, 0x0D                ; row 20 is the MIDDLE bar, and the second
    mov bx, spl_rowb            ; digit is a 0 - SPL_DIG0 has no middle
    call spl_rowmk
    call spl_putrow             ; row 20
    mov cx, 19
    call spl_putplain           ; rows 21..39
    mov al, 0x0F
    mov bx, spl_rowb
    call spl_rowmk
    call spl_putrow             ; row 40
    pop ds
    ret

; -----------------------------------------------------------------------------
; spl_putplain - CX rows of [spl_rowa].  spl_putrow - one row of [spl_rowb].
; Both take ES:DI at the row's first byte and leave it at the next row's.
; clobbers: ax, cx, si
; -----------------------------------------------------------------------------
spl_putplain:
    mov si, spl_rowa
    call spl_prow
    loop spl_putplain
    ret
spl_putrow:
    mov si, spl_rowb
spl_prow:
    push cx
    push si
    push di
    mov cx, SPL_SPINB
    rep movsb                   ; DS = CS (spl_spin set it): OPAQUE, and the
    pop di                      ; only write each of these bytes gets
    pop si
    pop cx
    SPL_NEXTROW_CS
    ret

; -----------------------------------------------------------------------------
; spl_rowmk - build a row into the 15-byte buffer at CS:BX: the eight verticals
;             always, plus the horizontal bar of each digit whose bit is set in
;             AL.  clobbers: ax, cx, dx, si, bp - and PRESERVES DI
;
; A digit's bar runs from its left vertical to its right one, so the two x
; values a digit needs are the same three ways up. spl_span does a single
; pixel as the degenerate case (x1 == x2), which is why there is no setpixel.
; -----------------------------------------------------------------------------
spl_rowmk:
    push di                     ; DI IS THE CALLER'S VRAM ROW POINTER. It is
    mov [cs:spl_rbar], al       ; spent below and spl_spin rebuilds the bar
    mov di, bx                  ; row twice DURING the walk down the box, so
    mov cx, SPL_SPINB           ; forgetting this drew the whole spinner into
    xor al, al                  ; whatever the zero loop left behind - which
.z:                             ; is a box of nothing, measured
    mov [cs:di], al             ; (the buffer is CS's, so no rep stosb: that
    inc di                      ; would address ES, the framebuffer)
    loop .z

    mov byte [cs:spl_rdig], 0
.digit:
    mov al, 28
    mul byte [cs:spl_rdig]      ; AL = the digit's box-relative x
    push ax
    call spl_xform
    mov bp, ax                  ; BP = one end, screen x
    pop ax
    add al, 20
    call spl_xform              ; AX = the other
    mov dx, ax
    mov ax, bp
    cmp ax, dx                  ; a mirrored frame swaps them
    jbe .ordered
    xchg ax, dx
.ordered:
    mov cl, [cs:spl_rdig]
    mov ch, 1
    shl ch, cl                  ; CH = this digit's bit
    test [cs:spl_rbar], ch
    jnz .bar
    push dx                     ; no bar: the two verticals, a pixel each
    mov dx, ax
    call spl_span
    pop dx
    mov ax, dx
.bar:
    call spl_span
    inc byte [cs:spl_rdig]
    cmp byte [cs:spl_rdig], 4
    jb .digit
    pop di
    ret

; -----------------------------------------------------------------------------
; spl_span - OR the run of screen x from AX to DX (AX <= DX) into the buffer at
;            CS:BX.  clobbers: ax, cx, dx, si
; -----------------------------------------------------------------------------
spl_span:
    sub ax, [cs:spl_l_spp]      ; box-relative, in pixels
    sub dx, [cs:spl_l_spp]
    mov cx, ax
    and cl, 7
    mov si, ax
    mov ax, 0x00FF
    shr al, cl                  ; AL = the first byte's mask
    mov cx, dx
    and cl, 7
    xor cl, 7
    mov ah, 0xFF
    shl ah, cl                  ; AH = the last byte's mask
    mov cl, 3                   ; ...and ONE shift count for both byte indices:
    shr si, cl                  ; SI = the first byte, still unshifted here
    shr dx, cl                  ; DX = the last
    cmp si, dx
    jne .multi
    and al, ah                  ; one byte carries both edges
    or [cs:bx+si], al
    ret
.multi:
    or [cs:bx+si], al
    inc si
.mid:
    cmp si, dx
    jae .last
    mov byte [cs:bx+si], 0xFF
    inc si
    jmp short .mid
.last:
    or [cs:bx+si], ah
    ret

; -----------------------------------------------------------------------------
; spl_xform - coin-flip a spinner x: AL = box-relative x (0..104) ->
;             AX = 320 + (AL - SPL_HALFW) * cos / 64. clobbers: nothing else
; -----------------------------------------------------------------------------
spl_xform:
    push cx
    push dx
    xor ah, ah
    sub ax, SPL_HALFW
    imul word [cs:spl_cos]         ; DX:AX = rel * cos; |result| <= 3328
    mov cl, 6
    sar ax, cl
    add ax, [cs:spl_l_cx]       ; ...and NOT 320, which is VGA's centre alone
    pop dx
    pop cx
    ret

%ifdef SPLSTARS
; =============================================================================
; SPEC.md 15.3.7 - the still logo's twinkle, `make SPLSTARS=1`
;
; The A/B for the loading screen's ANIMATION, and a knob for TITLESNAP's
; reason: it is a look question, so it is something to be looked at rather than
; argued. spl_spin pins cos at 64 above - full width, no flip - and these six
; stars are what moves instead.
;
; A STAR IS A TABLE, NOT A DRAWING (15.3.7.1). Five shapes exist - dark, then
; radius 1, 2, 3 and 4 with their clump rings - and the twinkle is which of the
; five a star is showing this frame, so the whole of drawing one is a single
; `rep movsb` of its 22 bytes into the framebuffer. That is 15.3.4's every-
; pixel-once at a smaller scale and for its reason: a twinkle that grew and
; shrank procedurally would be an erase followed by a draw, which is
; PERFORMANCE.md rule 2's pair and is exactly what the logo's own BLINK was.
; The dark shape is 22 zero bytes, so a star that goes out is erased BY the
; write that replaces it and never by a pass of its own.
;
; Six of them are 132 opaque byte writes a frame against the logo's 615, with
; no read, no Bit Mask and no Graphics Controller `out`.
; =============================================================================
SPL_STARB   equ 2               ; a star's box, in bytes (16 px)
SPL_STARR   equ 11              ; ...and in rows. The centre is col 8, row 5,
SPL_STARCY  equ 5               ; which leaves room for the outer ring at 5
SPL_STARSZ  equ SPL_STARB * SPL_STARR
SPL_NSTAR   equ 6
SPL_STARSH  equ 3               ; one phase step per 8 ticks - 440 ms, 128 ticks
                                ; a cycle, 7.0 s, half of it dark. Picked off
                                ; the FRAME rate like SPL_SPINSH and with the
                                ; same expiry date: 15.3.8 put the animation on
                                ; IRQ0, every step is drawn on time now, and 1
                                ; would twinkle in 0.88 s

; -----------------------------------------------------------------------------
; spl_stars - one frame of the twinkle, phased off spl_clock.
; in:  DS = KERNEL_SEG   out: DS = KERNEL_SEG
; clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
spl_stars:
    call spl_clock              ; the SAME wall clock the spinner uses
    mov cl, SPL_STARSH          ; (15.3.6), so this is an A/B of the animation
    shr ax, cl                  ; and not of the change that drives it
    mov [cs:spl_stphase], ax

    mov ax, [vid_rowadd]        ; the row walk's three words, banked into CS
    mov [cs:spl_radd], ax       ; for SPL_NEXTROW_CS. spl_spin banks them too
    mov ax, [vid_wrapbit]       ; and runs first - but a routine that depends
    mov [cs:spl_rwbit], ax      ; on the ORDER spl_paint makes two calls that
    mov ax, [vid_wrapfix]       ; look independent in is a routine that breaks
    mov [cs:spl_rwfix], ax      ; when somebody swaps them

    mov byte [cs:spl_stn], 0
.one:
    mov bl, [cs:spl_stn]        ; --- where this star is, banked so that
    xor bh, bh                  ; nothing below has to keep an index alive
    mov si, bx
    shl si, 1
    add si, bx                  ; SI = star * 3
    mov al, [cs:spl_star_tab+si]
    mov [cs:spl_stdx], al
    mov al, [cs:spl_star_tab+si+1]
    mov [cs:spl_stdy], al

    mov al, [cs:spl_star_tab+si+2]  ; --- and WHICH SHAPE it is showing. The
    xor ah, ah                  ; six offsets are 0, 6, 3, 9, 12 and 5 of the
    add ax, [cs:spl_stphase]    ; sixteen, so about three are lit at any
    and ax, 15                  ; instant and they do not pulse in unison
    mov si, ax
    mov al, [cs:spl_star_ph+si]
    mov ah, SPL_STARSZ
    mul ah                      ; AX = shape * 22
    add ax, spl_star_sh
    mov si, ax                  ; SI = this frame's 22 bytes

    call spl_staradr            ; ES:DI = where they go. BEFORE the DS switch
                                ; below, because it reads [vid_seg] and that is
                                ; the KERNEL's - 15.3.5.3's bug, which cost the
                                ; spinner every adapter at once. It preserves
                                ; SI, which gfx_rowbase does too
    push ds
    mov ax, cs
    mov ds, ax                  ; ...so the row copy is one `rep movsb`
    mov bp, SPL_STARR
.row:
    push di
    mov cx, SPL_STARB
    rep movsb                   ; OPAQUE, and the only write these bytes get
    pop di
    SPL_NEXTROW_CS
    dec bp
    jnz .row
    pop ds

    inc byte [cs:spl_stn]
    cmp byte [cs:spl_stn], SPL_NSTAR
    jb .one
    ret

; -----------------------------------------------------------------------------
; spl_staradr - ES:DI = the current star's box, top-left byte.
;               in: DS = KERNEL_SEG (it reads [vid_seg])
;               clobbers: ax, bx, cx, dx, di, es - and PRESERVES SI
;
; Through spw_gfx_rowbase like every other row in this module, so CGA's bank
; interleave and Hercules' four are handled where they already are.
; -----------------------------------------------------------------------------
spl_staradr:
    mov al, [cs:spl_stdy]
    cbw                         ; SIGNED: two of the six sit ABOVE the logo box
    add ax, [cs:spl_l_spn]
    sub ax, SPL_STARCY          ; ...and a box starts five rows above centre
    call KERNEL_SEG:spw_gfx_rowbase
    mov bx, ax
    mov al, [cs:spl_stdx]
    cbw                         ; ...signed too: four of the six are LEFT of it
    add ax, [cs:spl_l_spx]
    add ax, bx
    mov di, ax
    mov es, [vid_seg]
    ret

; --- 15.3.7's data -----------------------------------------------------------
; Six stars, held RELATIVE TO THE LOGO BOX because 15.3.5 gives the three
; adapters three layouts and a screen literal is right on exactly one of them:
; a signed byte column from [spl_l_spx], a signed row from [spl_l_spn], and an
; offset into the sixteen-step cycle.
;
; NONE SITS BELOW THE LOGO. VGA leaves 50 rows between the box and the dialog
; and both 1bpp adapters leave NINE (15.3.7.2), which is not a star - so four
; are level with the logo and outside it left and right, and two are above it.
; The highest reaches 19 rows above the box top, which on CGA - where the box
; top is row 29 - is row 10, and that is the constraint that fixes the layout.
spl_star_tab:
    db  -5,   8, 0              ; left, level with the logo's upper half
    db  -4,  33, 6              ; left, lower
    db  16,   5, 3              ; right, upper
    db  17,  30, 9              ; right, lower
    db   3, -13, 12             ; above, left of centre
    db  12, -14, 5              ; above, right of centre

spl_star_ph:                    ; the twinkle: which shape, per step of 16
    db 0, 1, 2, 3, 4, 3, 2, 1   ; grow, peak, shrink...
    db 0, 0, 0, 0, 0, 0, 0, 0   ; ...and out for the other half

; The five shapes, 2 bytes by 11 rows each, centre col 8 / row 5. A crossed
; pair of lines that grows, with a ring of single-pixel clumps at distance 3
; from radius 3 and a second at 5 for the one frame at the peak.
spl_star_sh:
    times SPL_STARSZ db 0       ; 0: out

    db 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00   ; 1: radius 1
    db 0x00,0x80, 0x01,0xC0, 0x00,0x80
    db 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00

    db 0x00,0x00, 0x00,0x00, 0x00,0x00              ; 2: radius 2
    db 0x00,0x80, 0x00,0x80, 0x03,0xE0, 0x00,0x80, 0x00,0x80
    db 0x00,0x00, 0x00,0x00, 0x00,0x00

    db 0x00,0x00, 0x00,0x00                         ; 3: radius 3, inner ring
    db 0x04,0x90, 0x00,0x80, 0x00,0x80, 0x07,0xF0
    db 0x00,0x80, 0x00,0x80, 0x04,0x90
    db 0x00,0x00, 0x00,0x00

    db 0x10,0x04                                    ; 4: radius 4, both rings
    db 0x00,0x80, 0x04,0x90, 0x00,0x80, 0x00,0x80, 0x0F,0xF8
    db 0x00,0x80, 0x00,0x80, 0x04,0x90, 0x00,0x80
    db 0x10,0x04

spl_stphase dw 0                ; the cycle step this frame, before the offset
spl_stn     db 0                ; which star spl_stars is on...
spl_stdx    db 0                ; ...and where that star is
spl_stdy    db 0
%endif

; =============================================================================
; SPEC.md 15.3.8 - the animation runs on IRQ0
;
; 15.3.6 fixed the rate the logo turns at and could not fix the FRAME rate:
; spl_paint is called 1.72 times a second during a load, with a median gap of
; 581 ms and a worst of 1.21 s, because the machine spends 83% of the boot
; inside the ROM. THAT DOES NOT LOCK US OUT - int 13h runs with interrupts
; enabled, and the tick was measured at 18.2198 Hz across a whole load against
; the PIT's 18.2065, with 21 of them fired inside ONE unbroken stretch of BIOS
; execution. The CPU in there is not busy, it is waiting on IRQ6 while the DMA
; controller moves the sector - so a frame drawn from this ISR is drawn in a
; gap the machine was going to spend idle.
; =============================================================================
%ifdef SPLSTARS
SPL_ANSH    equ SPL_STARSH      ; the FINER of the two - under the knob the
%else                           ; logo is still and the stars are the clock
SPL_ANSH    equ SPL_SPINSH      ; that matters
%endif

; -----------------------------------------------------------------------------
; spl_anim - one frame of the animation, IF its phase has moved.
; in:  DS = KERNEL_SEG.  out: DS = KERNEL_SEG.
; clobbers: ax, bx, cx, dx, si, di, bp, es
;
; MOST TICKS THIS DRAWS NOTHING, which is what makes the ISR affordable: the
; phase changes every 1 << SPL_ANSH ticks - four for the spinner, eight for the
; stars - so the steady cost is one frame per phase and not one per tick.
;
; spl_paint calls it as well, and that is a FAIL-SAFE rather than a duplicate:
; on a machine where IRQ0 never arrives - masked, or a PIT somebody else has
; reprogrammed - the animation goes on being drawn on the notch exactly as it
; was before 15.3.8, and the phase test makes the second caller a compare.
; -----------------------------------------------------------------------------
spl_anim:
    call spl_clock
    mov cl, SPL_ANSH
    shr ax, cl
    and al, 15
    cmp al, [cs:spl_anlast]
    je .out                     ; the picture has not changed
    mov [cs:spl_anlast], al
    call spl_spin
%ifdef SPLSTARS
    call spl_stars
%endif
.out:
    ret

; -----------------------------------------------------------------------------
; spl_isr - int 08h while the BIOS still owns it (SPEC.md 15.3.8.2)
;
; Installed by spl_chrome, which is STAGE 2's entry alone, and taken back down
; by stage 2 immediately before the handover. A hard-disk boot starts the
; splash from inside kmain with sched_init already holding the vector, reaches
; spl_rechrome instead, and is driven by sch_isr's gated call.
; -----------------------------------------------------------------------------
spl_isr:
    pushf                       ; CHAIN FIRST, sch_isr's rule and for its two
    call far [cs:spl_old08]     ; reasons: the BIOS keeps the tick count that
                                ; 15.4 and 15.3.6 both read, and it sends the
                                ; EOI, which we must not
    push ax
    push ds
    mov ax, KERNEL_SEG
    mov ds, ax                  ; both flags are `.text` bytes (15.3.8.3)
    cmp byte [spl_live], 0
    je .out
    cmp byte [spl_quiet], 0     ; ...AND THE CALLER MAY NOT BE ABLE TO AFFORD
    jne .out                    ; ONE (SPEC.md 15.3.3.1). spl_stepq raises this
                                ; around mouse_init's identify window, where a
                                ; 28.7 ms spinner frame costs three bytes of a
                                ; 1200-baud burst - and until this test existed
                                ; the interrupt drew straight through it, which
                                ; is the very overrun spl_stepq was written to
                                ; prevent. sch_isr carries the same test
    cmp byte [spl_busy], 0      ; ...somebody is drawing, and what they may be
    jne .out                    ; holding is the Graphics Controller
    inc byte [spl_busy]        ; TAKE IT: the main line's flag is the ISR's
                                ; re-entrancy lock too, and the test-and-set is
                                ; atomic because it is above the sti
    sti                         ; ...and IRQ6 MUST NOT WAIT ON A FRAME: the
                                ; machine we interrupted is usually the BIOS
                                ; spinning on the floppy's completion
                                ; interrupt, and a frame is 4 to 12 ms
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cld                         ; spl_spin's row copy is a `rep movsb` and this
                                ; is an ISR: DF belongs to whatever we
                                ; interrupted, which during the load is a BIOS
                                ; int 13h (SPEC.md 1 - sched.inc says the same
                                ; thing above its own in-ISR blit)
    call spl_anim
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    cli
    dec byte [spl_busy]
.out:
    pop ds
    pop ax
    iret

; -----------------------------------------------------------------------------
; spl_hook / spl_unhook - take and give back int 08h.  preserve all registers
;
; spl_unhook is stage 2's, and calling it is not tidiness: leave the vector in
; place and sched_init saves OUR handler as sch_old08 and goes on calling it,
; while spl_finish gives the whole blob back to the heap (SPEC.md 2.9.5) - so
; the chain would end in a far call into freed memory.
; -----------------------------------------------------------------------------
; HOOKING AND UNHOOKING ARE THE SAME OPERATION IN OPPOSITE DIRECTIONS - an
; EXCHANGE of the vector with [cs:spl_old08] - so there is one body and the two
; entries only decide what is in spl_old08 when it runs.  It also puts both
; halves of the vector inside ONE critical section rather than two, which is
; strictly safer than what it replaces.
spl_hook:
    cmp byte [cs:spl_hooked], 0
    jne spl_hk_out              ; twice would chain us to ourselves
    mov word [cs:spl_old08], spl_isr   ; ...the vector we are about to install,
    mov [cs:spl_old08+2], cs           ; which the swap then trades for the
    mov byte [cs:spl_hooked], 1        ; BIOS's
    jmp short spl_swap

spl_unhook:
    cmp byte [cs:spl_hooked], 0
    je spl_hk_out
    mov byte [cs:spl_hooked], 0        ; ...and here spl_old08 already holds
                                       ; the BIOS's, so the same swap undoes it
spl_swap:
    push ax
    push es
    xor ax, ax
    mov es, ax
    pushf
    cli                         ; no half-written vector, ever
    mov ax, [es:0x20]
    xchg ax, [cs:spl_old08]
    mov [es:0x20], ax
    mov ax, [es:0x22]
    xchg ax, [cs:spl_old08+2]
    mov [es:0x22], ax
    popf
    pop es
    pop ax
spl_hk_out:
    ret

spl_old08   dw 0, 0             ; the BIOS's int 08h, while we stand in front
spl_hooked  db 0                ; ...and whether we are standing there
spl_anlast  db 0xFF             ; the phase spl_anim last DREW. 0xFF is no
                                ; phase at all, so the next call draws - which
                                ; is what spl_rechrome wants after a mode set
                                ; has wiped the screen under a still logo

; -----------------------------------------------------------------------------
; spl_hline - white pixel-granular horizontal line, CX = x1, DX = x2
;             (x1 <= x2), BX = y. ES = VGA_SEG.
;             preserves CX and SI; clobbers ax, bx, dx, di, bp
;
; IT SAVED SEVEN AND ITS ONE CALLER READS BACK TWO.  spl_rect (below) is the
; whole census, and it banks y1/y2 on the STACK and reloads x2 from SI - so
; the only values that must live in a REGISTER across this call are x1 (CX)
; and x2 (SI).  Five pairs of push/pop bought nothing.
; -----------------------------------------------------------------------------
spl_hline:
    push cx
    push si

    mov si, dx                  ; SI = x2 (gfx_rowbase eats CX and DX)
    mov ax, bx                  ; AX = y
    mov bx, cx                  ; BX = x1 - gfx_rowbase preserves it
    call KERNEL_SEG:spw_gfx_rowbase
    mov bp, ax                  ; BP = row base

    mov ax, bx
    mov cl, 3
    shr ax, cl
    add bp, ax                  ; BP = first byte address
    mov dx, si
    shr dx, cl
    sub dx, ax                  ; DX = whole-byte span (0 = single byte)

    mov al, 0xFF                ; left edge mask: 0xFF >> (x1 & 7)
    mov cx, bx
    and cl, 7
    shr al, cl
    mov ah, 0xFF                ; right edge mask: 0xFF << (7 - (x2 & 7))
    mov cx, si
    and cl, 7
    xor cl, 7
    shl ah, cl

    mov di, bp
    or dx, dx
    jnz .multi
    and al, ah                  ; both edges share the byte
    call spl_maskbyte
    jmp .out
.multi:
    push ax
    call spl_maskbyte           ; left edge (advances DI)
    mov al, 0xFF
    call spl_bitmask
    mov cx, dx
    dec cx
    jcxz .right
    mov al, 0xFF                ; interior: plain full-byte writes
    rep stosb
.right:
    pop ax
    mov al, ah
    call spl_maskbyte
.out:
    mov al, 0xFF
    call spl_bitmask

    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; spl_vline - white 1px vertical line, CX = x, BX = y1, DX = y2 (y1 <= y2).
;             ES = VGA_SEG.  preserves BX and DX; clobbers ax, cx, di
;
; The same census as spl_hline's, one routine along: spl_rect calls this twice
; and needs y1 and y2 to survive between them (BX, and DX which
; spw_gfx_rowbase eats).  x it reloads from SI, and SI this routine never
; touches.
; -----------------------------------------------------------------------------
spl_vline:
    push bx
    push dx

    mov di, dx
    sub di, bx                  ; DI = row count - 1
    inc di                      ; DI = row count
    push cx                     ; CX = x, and gfx_rowbase eats CX
    mov ax, bx
    call KERNEL_SEG:spw_gfx_rowbase
    mov bx, di                  ; BX = row count
    mov di, ax                  ; DI = row base
    pop cx
    mov ax, cx
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    add di, ax                  ; DI = byte address

    and cl, 7
    mov al, 0x80
    shr al, cl                  ; single-pixel mask
    call spl_bitmask
    call spl_mcol               ; the masked bit down BX rows, then the GC
                                ; back to its default
    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; spl_rectc - spl_rect about the banked x centre: AX = the half width, BX = y1,
;             DX = y2.  FALLS THROUGH, so it must stay directly above spl_rect.
;             clobbers cx and si, which is what it is there to set
; -----------------------------------------------------------------------------
spl_rectc:
    mov cx, [cs:spl_l_cx]
    mov si, cx
    sub cx, ax
    add si, ax
    dec si
; -----------------------------------------------------------------------------
; spl_rect - 1px white outline: CX = x1, SI = x2, BX = y1, DX = y2.
;            preserves BX, CX, DX and SI; clobbers ax, di, bp - which is
;            spl_hline's and spl_vline's contract arriving here.  Every path
;            into spl_rechrome has banked all three at its own entry
; -----------------------------------------------------------------------------
spl_rect:
    push bx
    push dx
    mov dx, si
    call spl_hline              ; top
    pop bx                      ; BX = y2 (was DX on entry)
    push bx
    mov dx, si
    call spl_hline              ; bottom
    pop dx
    pop bx
    push cx
    call spl_vline              ; left
    mov cx, si
    call spl_vline              ; right
    pop cx
    ret

; -----------------------------------------------------------------------------
; spl_fill - byte-granular fill: ES:DI = top-left, CX = width in bytes,
;            BX = rows, AL = value. preserves all registers
; -----------------------------------------------------------------------------
spl_fill:
    push bx
    push cx
    push di
.row:
    push cx
    push di
    rep stosb
    pop di
    pop cx
    call spl_nextrow
    dec bx
    jnz .row
    pop di
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; spl_nextrow - DI on to the same column of the next row, through DS.  The
;               three words are the KERNEL's, so DS must be KERNEL_SEG.
;               clobbers: flags (every caller follows with dec/jnz)
; -----------------------------------------------------------------------------
spl_nextrow:
    add di, [vid_rowadd]
    test di, [vid_wrapbit]
    jz .r
    add di, [vid_wrapfix]
.r: ret

; -----------------------------------------------------------------------------
; spl_mcol - BX rows of the ALREADY-ARMED mask at ES:DI, then the mask back to
;            0FFh, which is what the rest of this module assumes on entry.
;            in: ES:DI = the top byte, BX = rows.  clobbers: al, bx, di
;
; The masked COLUMN, written once: spl_bar's partial byte and spl_vline's line
; are the same six instructions, and were spelled out twice.  DS must be
; KERNEL_SEG for spl_nextrow's three reads - true at both callers.
; -----------------------------------------------------------------------------
spl_mcol:
.row:
    call spl_wrbyte
    call spl_nextrow
    dec bx
    jnz .row
    mov al, 0xFF
    call spl_bitmask
    ret

; -----------------------------------------------------------------------------
; spl_bitmask - program the GC bit mask register from AL.  preserves every
;               register but AX, which the VGA arm spends on the OUT - every
;               caller sets AL immediately before and none reads AX back
; -----------------------------------------------------------------------------
spl_bitmask:
    push dx
    cmp byte [vid_mono], 0
    je .vga
    mov [cs:spl_mask], al       ; no GC to arm: spl_wrbyte applies it in
    jmp short .out              ; software
.vga:
    mov ah, al
    mov al, 8                   ; GC index 8 = bit mask
    mov dx, SPL_GC
    out dx, ax
.out:
    pop dx
    ret

; -----------------------------------------------------------------------------
; spl_maskbyte - merge white pixels through the mask in AL at ES:DI, then
;                advance DI one byte. clobbers: al
; -----------------------------------------------------------------------------
spl_maskbyte:
    call spl_bitmask
    call spl_wrbyte
    inc di
    ret

; -----------------------------------------------------------------------------
; spl_wrbyte - merge white pixels through the armed mask at ES:DI, without
;              advancing. VGA: read to load the latches, then write FF and
;              let the Bit Mask decide which bits land. Mono: the mask is a
;              byte in RAM and the merge is an OR. clobbers: al
; -----------------------------------------------------------------------------
spl_wrbyte:
    cmp byte [vid_mono], 0
    jne .mono
    mov al, [es:di]             ; load latches
    mov byte [es:di], 0xFF
    ret
.mono:
    mov al, [cs:spl_mask]
    or [es:di], al
    ret

; -----------------------------------------------------------------------------
; data (in .text on purpose - .bss is off-limits here, see the header)
; -----------------------------------------------------------------------------

; The "8088" stroke table WAS here - nineteen segments of four bytes, 76 of
; them - and SPEC.md 15.3.4's row composer stopped reading it two years ago
; without taking it out. Nothing referenced `spl_segs` or `SPL_NSEGS`; it was
; DATA, so no unreachable-code check could see it. 15.3.8 is what found it,
; by needing the space.

%ifndef SPLSTARS                ; the ONE reference is in spl_spin's %else, so
                                ; under the knob this is 16 bytes of a blob
                                ; that is already the one over its rung
spl_cos_tab:                    ; cos(k/16 rev) * 64, k = 0..15, as SIGNED
    db 64, 59, 45, 24, 0, -24, -45, -59      ; BYTES - every value is inside
    db -64, -59, -45, -24, 0, 24, 45, 59     ; -128..127 and spl_spin cbw's it
%endif

spl_bar_adr dw 0            ; the bar's top-left byte, set by spl_chrome
spl_mask    db 0xFF          ; mono: what the GC's Bit Mask holds on VGA
spl_rowa    times SPL_SPINB db 0    ; the spinner's two composed rows, and
spl_rowb    times SPL_SPINB db 0    ; the whole of what 15.3.4 costs in RAM
spl_rdig    db 0             ; spl_rowmk's digit cursor and bar mask
spl_rbar    db 0
spl_l_cx    dw 0             ; SPEC.md 15.3.5's layout, banked by spl_rechrome
spl_l_bar   dw 0             ; once for whatever screen this machine has: the
spl_l_ttl   dw 0             ; x centre, then the bar's row and the five rows
spl_l_pct   dw 0             ; laid out around it
spl_l_dt    dw 0
spl_l_db    dw 0
spl_l_spn   dw 0
spl_l_spx   dw 0             ; the spinner box's left byte, and the same in
spl_l_spp   dw 0             ; PIXELS, which is what spl_span subtracts

; spl_rechrome's layout table (SPEC.md 15.3.5): VGA's own row as a word, then
; mono's SIGNED delta from the bar as a byte - five rows of three, walked into
; the five consecutive words above.  THE ADJACENCY IS LOAD-BEARING and this is
; what keeps it true: the walk steps DI by two and would otherwise write
; spl_l_pct's row into whatever a re-ordering put there, silently and only on
; one adapter.
%if (spl_l_pct-spl_l_ttl)!=2 || (spl_l_dt-spl_l_pct)!=2 || \
    (spl_l_db-spl_l_dt)!=2 || (spl_l_spn-spl_l_db)!=2
%error "spl_rechrome's layout walk needs spl_l_ttl..spl_l_spn consecutive"
%endif
spl_lay_tab dw SPL_TTL_Y
            db -20
            dw SPL_PCT_Y
            db 40
            dw SPL_DLG_T
            db -32
            dw SPL_DLG_B
            db 59
            dw SPL_TOP
            db -81
spl_l_msg   dw SPL_KY        ; 15.6's line, read by spl_kern_on AND by
                             ; vidsel.inc's spl_mgeom one section along. The
                             ; initialiser is VGA's, for a caller that somehow
                             ; reaches the line before the chrome
spl_radd    dw 0             ; vid_rowadd/wrapbit/wrapfix, banked into CS for
spl_rwbit   dw 0             ; SPL_NEXTROW_CS - the row loop runs with DS = CS
spl_rwfix   dw 0
; [spl_quiet] is NOT here any more: it moved to viddet.inc's `.text` beside
; [spl_live] and [spl_busy], because since 15.3.8 the readers that matter are
; the two IRQ0 handlers and both are kernel `.text`, where `cs:` names the
; KERNEL segment and not this blob (SPEC.md 15.3.3.1).
spl_on      db 0             ; the chrome has been drawn - spl_tick's latch
; ...and [spl_live] is NOT here: it is the one byte of this module the KERNEL
; reads - viddet's mode guard, fprog's boot test and vidsel's spl_reset all ask
; it through DS - so it stays in `.text` where DS reaches it (SPEC.md 2.9.4).
; One byte resident against 967 that left.
spl_done    dw 0
spl_total   dw 1
spl_cos     dw 64
spl_pctbuf  db '   %', 0
spl_s_welcome db 'Welcome to os8088', 0
spl_s_kern  db 'Loading Kernel', 0
spl_kfseg   dw 0                ; the ROM 8x8 set, asked for once at the first
spl_kfoff   dw 0                ; tick and read through DS a glyph at a time
spl_krow    times 8 db 0        ; ...into here, one character's eight rows

; THE MODULE'S OWN SIZE IS NOT A BUDGET ANY MORE (SPEC.md 2.9.4). It used to
; have to fit the image's opening SPL_RESIDENT sectors, because the boot sector
; far-called it at 0060:0008 while the rest of the kernel was still landing -
; a constant that was raised three times, never once by this file. It is stage
; 2's now, aboard in its entirety before stage 2 runs at all, and what
; SPL_RESIDENT measures instead is viddet.inc: the first tick probes the
; adapter, and that is the KERNEL's code. The assertion moved with the meaning.

section .text                   ; and hand .text back, as every module must
