; =============================================================================
; kerndos/kdresume.inc - THE LIVE RESUME (SPEC.md 96.49,
; docs/plans/KERN-DOS-PLAN.md 8.1)
;
; The DOS program has exited and there is a hibernation image on the fixed
; disk. W6 got back to it by `int 19h` - a whole POST and a whole boot, after
; which os8088's own `hb_probe` finds the pointer and resumes. This puts the
; image back HERE, with no boot in between: read `HIBERNAT.IMG` over
; conventional memory and jump to the kernel's wake address, which is exactly
; what SPEC.md 87.5's stub does for an ordinary resume.
;
; **8.1 REFUSED THIS AND THE REFUSAL WAS WRONG ABOUT *WHEN*.** Its argument
; was that the extent list cannot be bounded cheaply, because *"the one place
; it could live is `kern_dos`'s own image: the text framebuffer is not
; available, since the DOS program prints into it"*. That is true DURING the
; program and this runs after it has gone - at `kd_leave`, with the screen
; about to be overwritten by the image anyway. So the list goes where the
; kernel's own goes, at `HS_EXT`, and costs this image nothing at all: 1,280
; extents of video RAM that no rung of the ladder owns.
;
; What it does cost is the STUB, and that is irreducible - `kernel/hbstub.inc`
; is ~440 bytes and has to BE somewhere to be staged. It is shared as source
; rather than copied (that file's own header says why), and the second half of
; the bill is this file's find, walk and stage.
;
; **THE DISK LAYER IS THE REASON THIS IS SMALL.** `kern_dos` already carries
; `disk.inc` whole, so finding a file and walking its chain are calls rather
; than code - and since SPEC.md 96.46 it carries the kernel's volume table
; too, so the FIXED DISK the image lives on is a volume it can actually
; mount. This wave rests on that one.
;
; EVERY REFUSAL FALLS BACK TO `int 19h`, which is W6's route and works. A
; machine that cannot find its image, cannot mount its volume or runs out of
; extents reboots and resumes the long way instead of losing the session -
; so the fast path is an optimisation and never a new way to fail.
; =============================================================================

%ifdef DOSR_MARK
  %include "hbmark.inc"         ; the trace's macros, used at the far jump
%endif                          ; below and so before hbstub.inc's include

; -----------------------------------------------------------------------------
; kd_resume - put the session back, or answer CF=1 and let kd_leave reboot
; in:  SI -> the launch block; DS = ES = KD_SEG; interrupts on
; out: DOES NOT RETURN on success. CF=1 = could not, and nothing is disturbed
;      beyond a mount.
;
; **NOTHING IS TORN DOWN UNTIL THE LAST MOMENT**, which is `hbm_res`'s order
; and for its reason: every refusal above the handover is survivable, and a
; machine that has masked its interrupts and then found no image is one that
; cannot reboot cleanly either.
; -----------------------------------------------------------------------------
kd_resume:
    test word [si+KDL_FLAGS], KDLF_HIBER
    jz .no                      ; no image was written: 96.42's floppy arm,
                                ; where the machine restarts and says so
    cmp word [si+KDL_WAKE + 2], 0
    je .no                      ; ...and no wake address is the same answer
                                ; from the other side: a kernel too old to
                                ; carry one (KDL_VER's whole job)

    ; --- 1. the volume the image is on, and the image ------------------------
    mov al, [si+KDL_HBVOL]
    cmp al, DVOL_MAX
    jae .no
    mov [kdr_vol], al
    mov dl, al
    call disk_mount_x
    jc .no
    xor ax, ax                  ; the ROOT: hb_probe writes the pointer and
    call dsk_chdir_q_x          ; the image there (SPEC.md 87.1). THE QUIET
                                ; ONE (96.47): this stands in a directory to
                                ; READ one file out of it and never to list
                                ; it, and the loud chdir rebuilds the global
                                ; listing on the way
    jc .no
    mov si, kdr_s_img
    ; **BY NAME OFF THE DIRECTORY, NEVER OUT OF THE GLOBAL LISTING**
    ; (SPEC.md 96.49.7). It was `dsk_find_name_x` then `dsk_get_dir_x`, which
    ; read the snapshot a LOUD mount used to synthesize - and since SPEC.md
    ; 22.6.3 a listing has no home of its own: it goes into the store the
    ; caller supplies through `dsk_dest_x`, and `kern_dos` has no window, no
    ; file manager and no desktop, so it supplies none. `[dsk_dseg]` is 0
    ; here and EVERY mount is quiet, which makes `[disk_nfiles]` 0 and the
    ; search refuse whatever is on the disk. `dskw_stat_x` walks the
    ; directory and answers in registers, which is the same correction
    ; `hbm_findimg` took on the kernel's side of this file.
    call dskw_stat_x            ; BX = the first cluster - NOT the FAT
    jc .no                      ; record's own 26 (SPEC.md 96.37.1 item 4) -
    mov [kdr_clus], bx          ; and DX:CX = the size
    ; **AND ITS LENGTH, WHICH IS THE FILE'S AND NOT MEMORY'S.** The first
    ; version sized the walk from `[kd_top]` - the box's banked BDA figure -
    ; because that is what `hbm_extents` does with `[mem_top]` over there.
    ; They are two different quantities: the kernel writes `[mem_top]`
    ; paragraphs and this host's idea of the top of memory is a cap the BOX
    ; banked, so a machine where they disagree restores SHORT. It presented
    ; as a session that came back with the kernel intact and the package heap
    ; full of rubbish - a window whose title was 24 bytes of 0x86 - because a
    ; region is claimed TOP-DOWN (mem_claim_hi_x) and so lives in the tail
    ; that was never read. The directory entry says how long the file is; ask
    ; it.
    ; **AT 20 AND NOT AT 28**, which is the same trap as the cluster one line
    ; up wearing different clothes: this is SPEC.md 19.1's SYNTHESIZED entry
    ; and not an on-disk FAT record, so the size is the dword at 20 (lo @20,
    ; hi @22) where the raw record keeps it at 28. Bytes 24..31 of a staged
    ; entry are DECLARED ZERO, so reading 28 gets a confident nothing - the
    ; walk refused, `kd_leave` fell back to `int 19h`, and the only symptom
    ; was a return that took fifteen guest seconds instead of three.
    mov ax, cx                  ; (DX:CX, which dskw_stat_x answered with)
    add ax, 511                 ; ...as whole sectors, rounded up
    adc dx, 0
    mov cl, 9
    shr ax, cl
    mov cl, 7
    shl dx, cl                  ; the high word's bits fold down beside them
    or ax, dx
    or ax, ax
    jz .no
    mov [kdr_secs], ax

    ; --- 2. the transport, out of the table 96.46 carried --------------------
    ; No probe and no second set of fields: the unit and the partition base
    ; are this volume's own row, and a fixed disk's spt/heads came over beside
    ; the table. That is the whole of what `hbm_geomd` works out over there.
    mov bl, [kdr_vol]
    xor bh, bh
    mov cl, 4
    shl bx, cl
    add bx, dsk_vtab
    mov al, [bx+DV_UNIT]
    xor ah, ah
    mov [kdr_unit], ax
    mov bl, [kdr_vol]
    xor bh, bh
    shl bx, 1
    shl bx, 1
    add bx, dsk_vbase
    mov ax, [bx]
    mov [kdr_base], ax
    mov ax, [bx+2]
    mov [kdr_base+2], ax
    mov si, [kd_lbp]
    mov ax, [si+KDL_HDSPT]
    or ax, ax
    jz .no                      ; a zero geometry is the mount failing later
    mov [kdr_spt], ax
    mov ax, [si+KDL_HDHDS]
    or ax, ax
    jz .no
    mov [kdr_heads], ax

    ; --- 3. the staging area, which is FREE now the program has gone --------
    ; **AND IT IS THE KERNEL'S ANSWER, NOT OURS** (SPEC.md 96.49.6).
    ; `kd_stageseg` asks the BDA's mode byte and that byte belongs to the DOS
    ; PROGRAM: `DIGIRAIN.COM` is 256 bytes that open `mov ax,2 / int 10h` and
    ; exit the same way, so on a Hercules machine the BDA reads 2 while
    ; `hbm_wake` will read `[vid_kind]` and look at B000. Everything below is
    ; then staged where nothing reads it, and what comes back is a clock and an
    ; exit code taken out of the visible desktop page. A VGA is worse: a
    ; program that exits in mode 13h leaves B800 UNMAPPED, and the stub is
    ; `rep movsb`'d into open bus.
    mov si, [kd_lbp]
    mov ax, [si+KDL_STAGE]
    or ax, ax
    jnz .stgk
    call kd_stageseg            ; ...or ask, for a block that does not carry
    mov ax, es                  ; one (KDL_VER's whole job)
.stgk:
    mov es, ax
    mov [kdr_stage], es
    ; ...AND ON A COLOUR PRIMARY, THE TEXT MODE THAT MAKES B800 EXIST.
    ; A program that exits in mode 13h leaves B800 unmapped on a VGA - the
    ; graphics controller decodes A000 alone - so the stub would be `rep
    ; movsb`'d into open bus and the far jump into it is the end of the
    ; machine. Mode 3 is `vid_text`'s own answer for every non-mono primary,
    ; and it CLEARS AND HOMES, so 96.49.4's scroll cannot arise rather than
    ; being ordered around.
    ;
    ; **THE MONO ARM IS DELIBERATELY NOT DONE HERE.** B000 is the MDA/Hercules
    ; card's own RAM and is decoded whatever mode anything is in, so there is
    ; nothing to fix - and `vid_text`'s mono arm is not a bare `int 10h`: it
    ; blanks 3B8/3BF first and asks `vid_mono_text` whether int 10h would land
    ; on a VGA instead, which is a question this host has no way to ask. A
    ; naive mode 7 on a VGA+MDA machine would map the VGA over B000.
    cmp word [kdr_stage], 0xB000
    je .stgm
    mov ax, 0x0003
    int 0x10
.stgm:

    ; --- 4. the last words, AND THEY COME BEFORE ANYTHING IS STAGED ---------
    ; **IT SAYS SO FIRST**, which is `kd_leave`'s own manner one route along:
    ; a screen that changes by itself is a crash and a screen that says why is
    ; a machine doing what it was told.
    ;
    ; **AND IT HAS TO BE PRINTED BEFORE THE EXTENT LIST IS WRITTEN, NOT AFTER**
    ; (SPEC.md 96.49.4). `kd_puts` is the ROM's teletype and the page it prints
    ; on IS the staging area: the DOS program leaves the cursor wherever it
    ; left it, so a message ending in CRLF at the bottom of a full screen makes
    ; the BIOS SCROLL - and `HS_EXT` is at 0x0A00, which is row 16 of a visible
    ; 4,000-byte page and squarely inside what a scroll moves. The chain was
    ; walked at step 3 and then shifted 160 bytes by the line announcing it,
    ; after which the stub reads a garbage LBA and `int 13h` puts the wrong
    ; sectors over conventional memory. Prince of Persia never showed it
    ; because its exit is a BIOS mode set, which clears and homes; a program
    ; that merely filled the screen would have.
    mov si, kdr_s_back
    call kd_puts
%ifdef DOSR_MARK
    call kdr_mark_line          ; ...and, under DOSRMARK=1, every number the
%endif                          ; jump is about (kernel/hbmark.inc) - ALSO a
                                ; whole line, and so also before the walk
    mov es, [kdr_stage]
    call kd_extents             ; the chain, into HS_EXT
    jc .no                      ; ...and a refusal HERE still only costs the
                                ; long way home: `kd_leave` reboots, os8088's
                                ; own `hb_probe` finds the pointer and the
                                ; session comes back. What the line promises
                                ; is the SESSION, which it delivers either way

    ; --- 5. ...and from here nothing may fail, or print ----------------------
    push ds
    push cs
    pop ds
    mov si, hbs_stub
    mov di, HS_CODE
    mov cx, hbs_stub_end - hbs_stub
    rep movsb
    pop ds
%ifdef DOSR_MARK
    call kdr_mark_init          ; the trace row, blanked, now the teletype has
%endif                          ; printed its last and cannot scroll it away
    mov ax, [kdr_unit]
    mov [es:HS_UNIT], ax
    mov ax, [kdr_spt]
    mov [es:HS_SPT], ax
    mov ax, [kdr_heads]
    mov [es:HS_HEADS], ax
    mov ax, [kdr_nx]
    mov [es:HS_NX], ax
    mov si, [kd_lbp]
    mov ax, [si+KDL_WAKE]       ; where the restored kernel comes back in
    mov [es:HS_WAKE], ax
    mov ax, [si+KDL_WAKE + 2]
    mov [es:HS_WAKE + 2], ax
    mov ax, [si+KDL_KSEG]       ; ...and the DS it comes back with, which the
    mov [es:HS_KSEG], ax        ; stub used to spell as a constant that means
                                ; something else in this host (hbstage.inc)

    ; --- the CLOCK, WHICH IS THE KERNEL'S AND A TICK TO MEASURE FROM ---------
    ; SPEC.md 87.6: the image holds the clock as it was when it was WRITTEN,
    ; and a DOS program may have run for an hour. `hbm_wake` copies these ten
    ; bytes into `clk_sec`, and that block is sec/min/hour/day/mon then
    ; **clk_rtc and clk_dirty** then the year (kernel/clock.inc) - two bytes of
    ; machine state saying whether a usable RTC was found and whether it is
    ; owed a write. `kern_dos` cannot know either, so the launch block carries
    ; the kernel's own ten and they are staged verbatim.
    ;
    ; **THIS USED TO OVERWRITE SIX OF THEM FROM `cw_clk_snapshot`, WHICH IS A
    ; BARE `retf`** (SPEC.md 96.49.5). The stores copied cleared `.bss`, so the
    ; resumed machine came home at 00:00:00 on 00/00/0000 - and a month of zero
    ; indexes `clk_mnames` three bytes before the table, which is the
    ; *"corrupted clock"* the field reported, on every DOS program rather than
    ; one. The ROM is not the answer either: a 5150 has no RTC at all, so
    ; int 1Ah AH=02h answers nothing on the machine most of these come off.
    ;
    ; What is missing is one quantity - HOW LONG THE PROGRAM RAN - and there is
    ; exactly one clock that runs on both sides of the handover to measure it
    ; with. 0040:006C is the ROM's, ticking at 18.2065 Hz under os8088 and
    ; under us alike, and the block carries the value it stood at when those
    ; ten bytes were read. Staged raw: `hbm_wake` subtracts it from the live
    ; counter and hands the difference to `clk_tick`, which has carried whole
    ; seconds through the calendar since the day it was written.
    mov si, [kd_lbp]
    add si, KDL_CLK
    mov di, HS_CLK
    mov cx, 10
.ck:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .ck
    mov si, [kd_lbp]            ; (the copy above spent SI on the block)
    mov ax, [si+KDL_TICK]
    mov [es:HS_TICK0], ax
    mov ax, [si+KDL_TICK + 2]
    mov [es:HS_TICK0 + 2], ax

    ; ...and the exit code, which is what the whole return is FOR (96.41).
    ; The BDA copy stays too: it costs nothing and a resume that loses this
    ; word still comes back, just without a number in the window.
    mov ax, [kd_code]               ; THE WHOLE WORD (96.40.7): the high byte
    mov [es:HS_DOSCODE], ax         ; says whether the low one means anything,
                                    ; and a `xor ah, ah` here would lose it on
                                    ; the live-resume route alone - the route
                                    ; that shipped LAST, which is exactly how a
                                    ; two-route field would go stale
    mov ax, [dos_akb]           ; ...AND THE ARENA IT WAS GIVEN (SPEC.md
    mov [es:HS_DOSAKB], ax      ; 96.41.1). `kd_arena` worked it out off
                                ; `[kd_top]` and the box cannot: over there
                                ; the figure is what OSAPI_MEM_AVAIL_MAX said
                                ; about a HEAP, and this program had the
                                ; machine. Staged in the cell `hbm_res` uses
                                ; for the reboot route, so one reader serves
                                ; both ways home

    ; --- 5. the PIC masks, the ROM's tick, and go ----------------------------
    ; `hbm_handover`'s tail, with the half that is about a running kernel
    ; taken out: there are no drivers to shut down, no lock to take and no
    ; mode to set - `kern_dos` has been in text mode since the ROM left it.
    cli
    in al, 0x21
    mov [es:HS_MASKM], al
    mov byte [es:HS_SLAVE], 0   ; an XT's port 0xA0 is the NMI mask register
                                ; and not a second 8259 (SPEC.md 9.9.2), so
                                ; the stub must not write it back
    or al, 0x1A                 ; IRQ 1, 3, 4 - the keyboard and the mice, and
    out 0x21, al                ; NOT the whole master: hbm_handover masks this
                                ; same set because the stub `sti`s and calls
                                ; int 13h ("the ROM wants its interrupts on an
                                ; AT"). Masking all of it takes IRQ0's tick,
                                ; IRQ6, and IRQ2 - the cascade an AT's fixed
                                ; disk answers on - so the read cannot complete
    call kd_mou_stop            ; ...and OUR mouse goes quiet before the
                                ; machine stops being ours (96.45.1)
%ifdef DOSR_MARK
    mov es, [kdr_stage]         ; 'J': this host got as far as the jump, so a
    HBMARKAT 0, 'J'             ; trace with a J and no S is a stub that did
%endif                          ; not run where it was put
    mov ax, [kdr_stage]         ; a FAR JUMP INTO THE STUB spelled as a
    push ax                     ; return, which is hbm_handover's own idiom.
    mov ax, HS_CODE             ; `push imm` is not an 8086 instruction
    push ax                     ; (CLAUDE.md 1), so both halves go via AX
    retf
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; kd_stageseg - the text framebuffer, as a segment
; out: ES = 0xB000 on a mono adapter, 0xB800 otherwise; AX spent
;
; The BDA's own byte (0040:0049), which is what the ROM set and what the DOS
; program may have changed: mode 7 is the MDA/Hercules text mode and every
; other text mode is at B800. `hbm_stageseg` asks the kernel's video state for
; the same answer, and there is none here.
; -----------------------------------------------------------------------------
kd_stageseg:
    push ds
    mov ax, 0x0040
    mov ds, ax
    mov al, [0x0049]
    pop ds
    cmp al, 7                   ; **THE COMPARE COMES FIRST, AND THAT ORDER IS
    mov ax, 0xB800              ; THE WHOLE ROUTINE** (SPEC.md 96.49.2). The
    jne .out                    ; mode byte is in AL and `mov ax, 0xB800` puts
    mov ax, 0xB000              ; 0x00 there, so a `cmp al, 7` after it tests
.out:                           ; the constant's own low byte and can NEVER be
    mov es, ax                  ; equal - B000 was unreachable. A `mov` sets no
    ret                         ; flags, so hoisting the compare costs nothing.
                                ; `hbm_stageseg` does not have this: it compares
                                ; a byte in MEMORY ([vid_kind]), which the load
                                ; cannot reach

; -----------------------------------------------------------------------------
; kd_extents - HIBERNAT.IMG's chain, as absolute runs at HS_EXT
; in:  [kdr_clus] = its first cluster, [kdr_base] the partition base,
;      ES = the staging segment
; out: [kdr_nx] = the count, CF=1 = it does not fit or the chain is broken
; clobbers: AX, BX, CX, DX, SI, DI, flags
;
; `hbm_extents` one host along, and the two are NOT shared: that one reaches
; `dsk_clus2lba_x` and `dsk_next_clus_x` through the module's far shim and
; keeps its state in `.modh` cells, so the seam runs THROUGH the routine
; rather than around it - six of its twenty lines. Where a shared body would
; have to be a macro with a call-shape parameter, this is the same arithmetic
; against the same two primitives, reached near.
;
; THE IMAGE IS AS LONG AS THE FILE SAYS, which is the one quantity both sides
; agree on: the kernel wrote `[mem_top]` paragraphs of it and this host knows
; only `[kd_top]`, a cap the box banked out of the BDA, and a machine where
; those differ restores SHORT and silently.
; -----------------------------------------------------------------------------
kd_extents:
    mov word [kdr_nx], 0
    mov di, HS_EXT              ; STRAIGHT THERE, and `[es:di-2]` below still
                                ; reaches the run in hand because DI is past
                                ; the last store by then
    mov si, [kdr_clus]
    mov cx, [kdr_secs]          ; CX = sectors still to place, out of the
                                ; DIRECTORY ENTRY (see kd_resume): the file's
                                ; own length and not this host's idea of
                                ; where memory ends
.clus:
    mov ax, si
    call dsk_clus2lba_x         ; AX = its first LBA, volume-relative
    jc .bad
    xor dx, dx
    add ax, [kdr_base]
    adc dx, [kdr_base + 2]      ; DX:AX = absolute
    mov bl, [dsk_spc]
    xor bh, bh
    cmp bx, cx
    jbe .whole
    mov bx, cx                  ; ...capped at what the image still needs
.whole:
    cmp word [kdr_nx], 0
    je .new
    cmp ax, [kdr_nextlo]        ; does this cluster follow the run in hand?
    jne .new
    cmp dx, [kdr_nexthi]
    jne .new
    add [es:di-2], bx           ; contiguous: the run grows
    jmp short .grown
.new:
    cmp word [kdr_nx], HS_XMAX
    jae .bad
    mov [es:di], ax
    mov [es:di+2], dx
    mov [es:di+4], bx
    add di, HS_XSTRIDE
    inc word [kdr_nx]
    mov [kdr_nextlo], ax
    mov [kdr_nexthi], dx
.grown:
    add [kdr_nextlo], bx
    adc word [kdr_nexthi], 0
    sub cx, bx
    jz .done
    mov ax, si
    call dsk_next_clus_x
    jc .bad
    mov si, ax
    jmp .clus                   ; near: the walk outgrew a short displacement
.done:
    clc
    ret
.bad:
    stc
    ret

kdr_s_img:  db 'HIBERNAT.IMG', 0
kdr_s_back: db 13, 10, 13, 10, 'os8088: putting the session back...', 13, 10, 0

%ifdef DOSR_MARK
; --- DOSRMARK=1: what this host knows, before it stops being able to say -----
; Every one of these is a number the jump depends on and none of them is
; visible from anywhere else: `stg` is kd_stageseg's answer - the BDA's mode
; byte, which a DOS program may have changed under us - and `wake`/`kseg` are
; the launch block's, staged from a structure written by a kernel that is not
; running. A machine that freezes with this line on it has already told you
; which of them to doubt.
;
; It goes out through the ROM's teletype, which is the LAST thing that may:
; the next statement copies the stub onto this very page, and a scroll after
; that would move it (kernel/hbmark.inc).
kdr_mark_line:
    mov si, kdr_s_m1
    call kd_puts
    mov ax, [kdr_stage]
    call kdr_hex16
    mov si, kdr_s_m2
    call kd_puts
    mov ax, [kdr_unit]
    call kdr_hex16
    mov si, kdr_s_m3
    call kd_puts
    mov ax, [kdr_spt]
    call kdr_hex16
    mov si, kdr_s_m4
    call kd_puts
    mov ax, [kdr_heads]
    call kdr_hex16
    mov si, kdr_s_m6
    call kd_puts
    mov ax, [kdr_secs]
    call kdr_hex16
    mov si, kdr_s_m7
    call kd_puts
    mov si, [kd_lbp]
    mov ax, [si+KDL_KSEG]
    call kdr_hex16
    mov si, kdr_s_m8
    call kd_puts
    mov si, [kd_lbp]
    mov ax, [si+KDL_WAKE + 2]
    call kdr_hex16
    mov al, ':'
    call kd_putc
    mov si, [kd_lbp]
    mov ax, [si+KDL_WAKE]
    call kdr_hex16
    mov si, kdr_s_m9
    call kd_puts
    ret

; --- the trace row, blanked, and its cursor with it -------------------------
; ES = the staging segment on entry.
kdr_mark_init:
    mov di, HM_ROW
    mov cx, 80
.b:
    mov word [es:di], 0x0720
    inc di
    inc di
    loop .b
    mov word [es:HM_CUR], 2     ; column 0 is this host's own 'J'
    mov ax, [kdr_nx]            ; ...and the extent COUNT, which the teletype
    mov bx, HM_ROW + HM_NXCOL * 2   ; line cannot carry: the walk that makes
    mov cx, 4                   ; it runs after the last print (96.49.4)
.x:
    rol ax, 1
    rol ax, 1
    rol ax, 1
    rol ax, 1
    mov dl, al
    and dl, 0x0F
    add dl, '0'
    cmp dl, '9'
    jbe .xd
    add dl, 7
.xd:
    mov [es:bx], dl
    mov byte [es:bx + 1], 0x07
    inc bx
    inc bx
    loop .x
    ret

kdr_hex16:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax
    mov cx, 4
.n:
    rol dx, 1
    rol dx, 1
    rol dx, 1
    rol dx, 1
    mov al, dl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .d
    add al, 7
.d:
    call kd_putc
    loop .n
    pop dx
    pop cx
    pop bx
    pop ax
    ret

kdr_s_m1:   db 'mark stg=', 0
kdr_s_m2:   db ' u=', 0
kdr_s_m3:   db ' spt=', 0
kdr_s_m4:   db ' hd=', 0
kdr_s_m6:   db ' sec=', 0
kdr_s_m7:   db ' ks=', 0
kdr_s_m8:   db ' w=', 0
kdr_s_m9:   db 13, 10, 0
%endif

section .bss
kdr_vol:    resb 1
kdr_clus:   resw 1
kdr_unit:   resw 1
kdr_spt:    resw 1
kdr_heads:  resw 1
kdr_base:   resd 1
kdr_secs:   resw 1
kdr_nx:     resw 1
kdr_nextlo: resw 1
kdr_nexthi: resw 1
kdr_stage:  resw 1
section .text

%include "hbstub.inc"           ; ...and SPEC.md 87.5's stub itself, in THIS
                                ; host's `.text` - the section is the
                                ; includer's choice (that file's header)
