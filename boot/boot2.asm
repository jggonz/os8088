; =============================================================================
; boot/boot2.asm - STAGE 2, THE LOADER (SPEC.md 2.9)
;
; The 512-byte sector relocates itself, finds the data area and reads the
; first BOOT2_SECS sectors of KERNEL.SYS to a scratch segment. Those sectors
; are this file. Everything the sector could not afford is here: the int 1Eh
; parameter table (SPEC.md 18.92/18.93), read_run's four bounds, the 286
; head-cross gate (18.93.2), 18.93.1's canary and its shorten-and-reload.
;
; It is assembled INTO THE KERNEL, which buys it the kernel's own constants -
; the image's sector count falls out of MODC_START rather than being measured
; and injected - and costs it the one thing the sector had for free: A KERNEL
; IS ONE BINARY FOR THREE GEOMETRIES. So SPT and HEADS are RUNTIME values
; here, handed over by stage 1 out of the volume's own BPB, where the sector
; had them as immediates and could shift instead of divide.
;
; ENTRY, at offset 0 of `.boot2`:
;   CS = HEAP_SEG, IP = 0, SS:SP = stage 1's stack. We are read STRAIGHT to
;   the heap's floor (SPEC.md 2.9.5), so everything below runs at the bottom
;   of the claim heap and the top of conventional RAM is free from the first
;   claim onward.
;   DL = boot drive     DH = heads          CX = sectors per track
;   SI = the data area's LBA (KERNEL.SYS's first sector)
;   BP = the BIOS tick stage 1 read before it loaded anything (SPEC.md 15.4)
;   DI = SPEC.md 18.93.1's KSIG, or 0 for "no canary on this image"
;
; DI IS WHY THE SIGNATURE IS A REGISTER AND NOT A CONSTANT. KSIG is read out
; of the BUILT kernel, so a kernel that contained it would have to be
; assembled twice to reach a fixed point. Stage 1 is built after the kernel
; and already carries measured constants, so it carries this one too.
; =============================================================================

; --- BOOTSTOP=2 IS THE ONE WITHOUT THE SPLASH, and this is what says so ------
; The knob moved down here with the loader (SPEC.md 2.9.4) and this derivation
; did not come with it, so `2` compiled to exactly `1`: the splash still ran,
; and the halt left a screen the splash owned rather than a text screen with a
; mark on it. Both halves are the point of `2` - a fault INSIDE the splash
; cannot be reached, and a machine that goes round instead of halting leaves
; something legible behind.
%ifdef BOOT_STOP
 %if BOOT_STOP == 2
  %define BOOT_NOSPLASH        ; BOOTSTOP=2: the load, with NO splash call at
 %endif                        ; all - so a fault inside the splash cannot be
%endif                         ; reached and read_run is on its own

; --- what stage 2 needs of the world it is assembled into --------------------
DPT_AT      equ 0x0580          ; 0000:0580 - our copy of the diskette
                                ; parameter table. ABOVE the BIOS data area
                                ; (which ends at 0x4FF), clear of every
                                ; documented use of the 0x500 page and BELOW
                                ; KERNEL_SEG, so nothing the kernel or its heap
                                ; can claim reaches it and it needs no restore
B2_STACK    equ 0x7C00          ; stage 1's STACK_TOP, which is still ours
KSIG_OFF    equ 6656            ; SPEC.md 18.93.1's probe, as a MEMORY offset
                                ; from KERNEL_SEG - the Makefile reads the same
                                ; bytes out of the file at KSIG_OFF + BOOT2_PAD,
                                ; which is FILE SECTOR 21 and has to be: the
                                ; probe must land in a run's SECOND half, and
                                ; tests/unit/t_canary.py re-derives that from
                                ; every shipped image's BPB. This equ and the
                                ; Makefile's KSIG_OFF are one number typed
                                ; twice; that row checks they agree.
                                ;
                                ; IT MOVES WITH BOOT2_SECS, and has had three
                                ; values for that reason: 11776 while the
                                ; blob was 13 sectors, 8704 when SPEC.md 2.9.12
                                ; grew it to 19, and 14336 when 2.5.3's split
                                ; took it back to 8. The number that has to
                                ; stay put is the FILE sector, because that is
                                ; what decides which half of a run the probe
                                ; lands in - and a probe in a run's FIRST half
                                ; is loaded CORRECTLY on exactly the machine
                                ; this exists to catch. tests/unit/t_canary.py
                                ; re-derives it per geometry and refused 8704
                                ; the moment BOOT2_SECS changed
                                ;
                                ; ...AND IT MOVES WITH THE SET OF GEOMETRIES,
                                ; which is what took it from 14336 to 50176.
                                ; The band is the INTERSECTION over every
                                ; shipped disk, and 19's 1.2MB 5.25" geometry
                                ; shares none of the old one: data at LBA 29 in
                                ; runs of 30, so file sector 36 is 5 into a
                                ; FIRST half there.
                                ;
                                ; ...AND IT MOVED LAST BECAUSE `.text` SHRANK
                                ; UNDER IT. 50176 named file sector 106, at the
                                ; top of `.text` with 429 bytes above it, and
                                ; below the end of `.text` the word is in the
                                ; `.bss` zero padding where every word equals
                                ; its neighbour a sector away - so the canary
                                ; PASSES on the one fault it exists to catch.
                                ; It sat that high because the offset had to be
                                ; legal for TWO blob lengths, SPLSTARS' being
                                ; a sector longer, and that intersection is
                                ; only 106..109. One blob length (SPEC.md
                                ; 15.3.8.5) widens the band to file sectors
                                ; 21, 57 and 106..110, and 21 is the bottom of
                                ; it. The Makefile's KSIG_OFF block carries the
                                ; whole derivation
B2_KSECS    equ ((MODC_START + 511) / 512) - BOOT2_SECS  ; what is left to read

; --- A COMPRESSED KERNEL (SPEC.md 2.9.13), behind KZIP=1 ----------------------
; The image past the blob is [ KZ_HEADSEC sectors plain ][ one LZ4 stream ], and
; the three numbers below come from tools/os88kz.py on the command line because
; none of them can be computed from anything this file can see: they are
; properties of the COMPRESSED file, and the file is made after this is
; assembled. Without KZIP they are absent and every path below assembles away.
;
;   KZ_SECS    what stage 2 reads, in place of B2_KSECS
;   KZ_HEADSEC how many of those are plain - SPL_RESIDENT, and for its reason:
;              spl_tick far-calls viddet.inc while the rest is still landing,
;              so those sectors have to be at their linked addresses DURING
;              the read and not after it
;   KZ_RPARA   how far UP the packed tail lands, in paragraphs, so expanding it
;              downwards can never overtake the source (SPEC.md 20.13's trick,
;              one level down)
%ifdef KZIP
  %ifndef KZ_SECS
    %error "KZIP without KZ_SECS: tools/os88kz.py --defines is what supplies it"
  %endif
KZ_MARGIN   equ 64              ; what os88kz.py refuses a kernel over. This
                                ; stream is the CLASSIC LZ4 block - no raw
                                ; tail (SPEC.md 20.13.7) - because kz_expand
                                ; below is its own copy of the arm and reads
                                ; the standard ending, so it is the one stream
                                ; on the machine that still needs an in-place
                                ; margin; kernel/lz.inc has none
KZ_BLK      equ 0xF000          ; ...and BLK in that tool: the most one block
                                ; may produce, so kz_expand never leaves a
                                ; segment. 61,440 and not 65,536 because the
                                ; destination advances by whole paragraphs
%endif

boot2_entry:
    ; --- WE ARE ALREADY AT THE HEAP'S FLOOR (SPEC.md 2.9.5) -----------------
    ; Stage 1 reads us straight here. It used to read us to the TOP of
    ; conventional RAM, below its own stack, and this routine began by
    ; `rep movsw`-ing itself down - because the top was the only address the
    ; sector could compute, HEAP_SEG falling out of the kernel's section sizes
    ; and the sector having none of them. It is TOLD HEAP_SEG now (SPEC.md
    ; 2.7.1), so the copy is gone and with it the second BOOT2_PAD its own
    ; survival cost the .nomem bound.
    ;
    ; WHY THE FLOOR AND NOT THE TOP, which is the part that has not changed:
    ; a transient at either END of the heap strands what it leaves behind, and
    ; every claim above the blob's eventual hole is one mem_compact cannot
    ; pack past, so the freed bytes never rejoin the long run and the machine
    ; ends the boot already fragmented. At the FLOOR they do rejoin it -
    ; mem_init raises [mem_base] over us for the duration, kmain lowers it
    ; again after spl_finish and compacts, and the arena closes up (SPEC.md
    ; 50.6.3).
    ;
    ; HEAP_SEG IS ABOVE THE LOAD, ALWAYS: the image stops at MODC_START and
    ; `.lowbss` + `.vgabuf` are nobits, so there are LOW_PARA + VGABUF_PARA
    ; paragraphs of nothing between the last sector to land and us. And it is
    ; below the top of RAM by stage 1's own .nomem refusal (SPEC.md 2.7.1).
    ;
    ; CX, SI and DI are stage 1's handover and the first thing below wants
    ; CX, so the six of them are written down before anything can clobber one.
    push cs
    pop ds                      ; DS = us; the stack stays stage 1's
    cld                         ; ...and DF=0 for every string op below
    mov [b2_drive], dl
    mov [b2_heads], dh
    mov [b2_spt], cx
    mov [b2_lba0], si
    mov [b2_ksig], di
    mov [b2_t0], bp

    ; --- the text screen stage 1 used to set (SPEC.md 2.9.8) ----------------
    ; It was in the sector, and the sector spent its last bytes on SPEC.md
    ; 18.92's parameter table instead - which has to be patched before the
    ; first multi-sector read and is therefore stage 1's now. What moving it
    ; costs is that POST's own screen stays up for the blob's read rather than
    ; the kernel's first nine sectors: about 300 ms more, on a boot that is
    ; nine seconds, before anything of ours is on the glass either way.
    ;
    ; **AFTER THE HANDOVER IS STASHED, NOT BEFORE IT.** `int 10h AH=01h` sets
    ; the cursor shape from CX, so setting it up costs a `mov cx, 0x2000` -
    ; and CX is stage 1's SECTORS PER TRACK. Putting this block where it sat
    ; in the sector, at the top of this routine, made every geometry divide
    ; below use 0x2000 sectors a track. The boot got as far as a splash that
    ; never finished, which is what a load reading the wrong sectors looks
    ; like from outside.
    int 0x11                    ; clean, cursorless text screen while the
    and al, 0x30                ; sectors land; the loading screen takes over
    cmp al, 0x30                ; in graphics the moment it is resident. A
    mov ax, 0x0003              ; monochrome card has no mode 3 - equipment
    jne .tmode                  ; word bits 5:4 = 11b means mode 7 instead.
    mov al, 0x07
.tmode:
    int 0x10
    mov ah, 0x01
    mov cx, 0x2000
    int 0x10

    ; --- how far ONE int 13h may run (SPEC.md 18.91.1) ----------------------
    ; The cylinder, not the track, which on a two-headed floppy doubles it.
    ; It works because the BIOS issues READ DATA with the MULTI-TRACK bit set,
    ; so at the sector numbered EOT the FDC flips head and carries on from
    ; sector 1 of the same cylinder - and EOT is the byte the parameter table
    ; below patches. TRACKRUN=1 puts the bound back at the track.
    ;
    ; **OUT OF [b2_spt] AND [b2_heads], NEVER OUT OF CX AND DH.** They are the
    ; same numbers, and the registers holding them are three instructions from
    ; a routine that eats one: the block above sets the cursor shape, `int 10h
    ; AH=01h` takes it in CX, and this multiply read CX for one cycle of this
    ; branch (SPEC.md 18.93.3). A run bound of 0x2000 sectors is not a bound -
    ; read_run's second bound, the sectors still wanted, wins every time - so
    ; the FIRST call of the boot asks for the rest of the kernel in one int
    ; 13h, and what happens next is the BIOS's business rather than ours: this
    ; emulator errors it and 18.93's shorten-and-reload quietly rescues the
    ; boot, which is why it took a field report to find. The stash above is
    ; three words away; use it.
    mov ax, [b2_spt]            ; the LIVE bound starts at the TRACK,
    mov [b2_runmax], ax         ; which the gate below widens on an 8088. That
                                ; direction is not arbitrary and reversing it
                                ; is silent: a 286 left on the cylinder bound
                                ; still boots, and an 8088 left on the track
                                ; bound still boots - it just takes twice the
                                ; calls and the canary then compares equal and
                                ; never runs, so boot_cylrun stays 0 and
                                ; dsk_xfer spends the rest of the session
                                ; being careful for nothing

    ; --- take over the diskette parameter table (SPEC.md 18.92/18.93) -------
    ; int 1Eh is a POINTER, not a handler: it names an 11-byte table the BIOS
    ; re-reads on every floppy operation, and byte 4 of it is EOT - the last
    ; sector number the FDC may touch on a track. The IBM PC and XT ROMs say
    ; 8, from the 8-sector diskettes of DOS 1.x, and the BIOS issues READ with
    ; the multi-track bit set, so a run reaching sector 9 does not stop - it
    ; flips to the other head and returns ITS sectors, with CF=0 and the full
    ; count. Every DOS since 1982 has replaced this table at boot.
    ;
    ; The machine's own table is COPIED and one byte patched: the step rate,
    ; head load/unload, motor timings and gap lengths in it belong to these
    ; drives and are not ours to guess.
    ;
    ; No guard on the vector, deliberately: the BIOS read the BOOT SECTOR
    ; through it, so a machine that could not use it never got here.
    push ds
    push es
    xor ax, ax
    mov es, ax
    mov bl, [b2_spt]            ; EOT, banked WHILE DS IS STILL OURS - the
                                ; `lds si` below is what used to make this a
                                ; second ES=0 window of its own. BX is free:
                                ; stage 1 hands over DL, DH, CX, SI, DI and BP
    mov di, DPT_AT
    lds si, [es:0x0078]         ; DS:SI = the BIOS's table
    mov cx, 11
    cld
    rep movsb                   ; ...ES:DI = ours
    mov [es:DPT_AT + 4], bl     ; EOT = this disk's sectors per track
    mov di, 0x0078              ; ...and the VECTOR through ES, which is
    mov ax, DPT_AT              ; already 0, rather than setting DS to 0 to
    stosw                       ; reach it. cld is set above
    xor ax, ax
    stosw
    pop es
    pop ds

%ifndef TRACK_RUN
    ; --- may this machine's FDC cross a head? (SPEC.md 18.93.2) -------------
    ; `push sp` is the entire test: the 8086 and 8088 push SP as it is AFTER
    ; the decrement, every later part pushes it as it was before.
    ;
    ; It is a bet about a POPULATION, not a fact about this machine - which is
    ; why the canary below still runs and can still lower the bound. What the
    ; gate buys is that a 286 never PAYS for the discovery: it takes the track
    ; bound from the start instead of loading the whole kernel wrong and then
    ; loading it again.
    push sp
    pop ax
    cmp ax, sp
    je .trkbound                ; 286 and up: leave it at the TRACK
    mov al, [b2_heads]          ; 8086/8088: the CYLINDER, which is what the
    xor ah, ah                  ; canary below then has something to verify.
    mul word [b2_spt]           ; COMPUTED HERE and not above, because this is
    mov [b2_runmax], ax         ; the only branch that wants it - b2_run was a
.trkbound:                      ; word that carried it ten instructions, and
%endif                          ; under TRACKRUN=1 was written and never read

; -----------------------------------------------------------------------------
; The load. Both ways in - here, and the canary's shorten-and-reload below -
; re-establish the destination and the LBA, so neither carries a copy.
; -----------------------------------------------------------------------------
.reload:
    mov word [b2_dest], KERNEL_SEG
    mov ax, [b2_lba0]
    add ax, BOOT2_SECS          ; ...past ourselves: `.text` begins at the file
    mov [b2_lba], ax            ; sector after this one (SPEC.md 2.9)
%ifdef KZIP
    ; --- THE PLAIN HEAD, ITS OWN RUNS (SPEC.md 2.9.13.1) --------------------
    ; No splash tick in here and nothing lost by that: the tick does not start
    ; until SPL_RESIDENT sectors have landed, and KZ_HEADSEC is exactly that
    ; many. What it costs is one extra int 13h - the head does not end on a
    ; cylinder boundary - and what it buys is that viddet.inc is at its linked
    ; address for the whole of the read that follows, which is the long one.
    ;
    ; NOTHING BOUNDS IT BUT [b2_left], and that is read_run's second bound
    ; already. The first version capped [b2_runmax] instead - which is not a
    ; cap at all: read_run DIVIDES the LBA by it to find where the current run
    ; ends, so a borrowed value redefines the geometry rather than shortening
    ; one read.
    mov word [b2_left], KZ_HEADSEC
.head_next:
    mov es, [b2_dest]
    call read_run               ; AX = sectors it moved
    add [b2_lba], ax
    sub [b2_left], ax
    mov cl, 5
    shl ax, cl
    add [b2_dest], ax
    cmp word [b2_left], 0
    jne .head_next
    add word [b2_dest], KZ_RPARA    ; ...and the packed tail lands R up, which
                                    ; os88kz.py rounds to a SECTOR: read_run's
                                    ; DMA-page bound divides the destination's
                                    ; address by 512 and rests on it being
                                    ; 512-aligned (SPEC.md 2.9.13.1)
    mov word [b2_left], KZ_SECS - KZ_HEADSEC
%else
    mov word [b2_left], B2_KSECS
%endif

.load_next:
    mov es, [b2_dest]
    call read_run               ; AX = sectors it moved; BX is its own business
    add [b2_lba], ax
    sub [b2_left], ax
    pushf                       ; the loop test, banked past the splash call
    mov cl, 5
    shl ax, cl                  ; 0x20 paragraphs a sector...
    add [b2_dest], ax           ; ...so the destination follows the run

%ifndef BOOT_NOSPLASH
%ifdef KZIP
    mov dx, KZ_SECS - KZ_HEADSEC    ; the bar measures the TAIL, which is the
    mov ax, dx                      ; only thing this loop reads - the head is
    sub ax, [b2_left]               ; already aboard, and SPL_RESIDENT's test
                                    ; below would be a tautology here because
                                    ; KZ_HEADSEC *is* SPL_RESIDENT
%else
    mov dx, B2_KSECS            ; tick the splash once it is fully resident:
    mov ax, dx                  ; AX = sectors done, DX = total (SPEC.md 15).
    sub ax, [b2_left]           ; DERIVED from what is left rather than counted
    cmp ax, SPL_RESIDENT        ; alongside it - and a load asked for a SECOND
    jb .no_tick                 ; time re-arms the bar with [b2_left] and
%endif
    call spl_tick               ; climbs from 0 again (SPEC.md 18.93.1).
                                ; NEAR: the loading screen is in this section
                                ; (SPEC.md 2.9.4), so the pinned 0060:0008 far
                                ; entry it used to answer at is gone. What the
                                ; gate still measures is `.text`: the first
                                ; tick probes the adapter, and those routines
                                ; are the KERNEL's
%endif
.no_tick:
    popf                        ; ...and the flags `sub [b2_left], ax` set
    jne .load_next

    ; --- hand off ------------------------------------------------------------
    mov ax, KERNEL_SEG          ; the boot timer's t=0, into the fixed word the
    mov es, ax                  ; kernel keeps for it (SPEC.md 15.4). AFTER the
    mov bx, [b2_t0]             ; load, or the sectors landing here would
    mov [es:0x000C], bx         ; overwrite it

    mov ax, cs                  ; ...and WHERE WE ARE, so the kernel can reach
    mov [es:spl_fseg], ax       ; the loading screen for the rest of the boot
                                ; (SPEC.md 2.9.4). spl_step is on dsk_xfer's
                                ; per-sector path and spl_finish is kmain's
                                ; last word to it; both are far calls through
                                ; this word now, and until it is written they
                                ; refuse through COLD_SEG:mod_gone rather than
                                ; jumping into whatever offset 0 happens to be

%ifndef TRACK_RUN
    ; --- THE CANARY (SPEC.md 18.93.1) ---------------------------------------
    ; int 13h answered CF=0 and the full count for every run above. That is
    ; NOT the same as the right BYTES: a run bounded by the CYLINDER only
    ; lands in LBA order because the FDC's multi-track bit carries it onto the
    ; other head at EOT, and EOT is the byte we patched into the parameter
    ; table. A BIOS that keeps its own table ignores that patch, flips at 8
    ; instead of 9, and returns the other head's sectors - silently.
    ;
    ; So verify the TRANSFER, never the table: reading our own patch back
    ; proves only that our own write to our own RAM worked, which it always
    ; does. Failing it costs a whole second load, and that is the right price:
    ; a boot that is 2.2s slower beats a kernel that is quietly wrong.
    cmp word [b2_ksig], 0
    je .nocross                 ; no signature for this image: nothing to check
    mov ax, [b2_runmax]
    cmp ax, [b2_spt]
    je .nocross                 ; already track-bounded: no run crossed a head
    mov bx, [b2_ksig]
%ifdef KZIP
    ; THE CANARY IS UNMOVED AND ITS REASONING IS UNTOUCHED (SPEC.md 2.9.13.3).
    ; What it verifies is the TRANSFER, and its whole constraint is that its
    ; FILE SECTOR crosses a head - which is a property of the sector number and
    ; the geometry, not of what is in it. So it is still file sector 21 on
    ; every shipped disk; only the memory address moved, because the packed
    ; tail lands R paragraphs up from where the plain one did. The Makefile
    ; reads the word out of the PACKED file at the same offset.
  %if KSIG_OFF < KZ_HEADSEC * 512
    cmp [es:KSIG_OFF], bx       ; ...in the plain head, so exactly as before
  %else
    push es
    push ax                     ; **AX IS THE ANSWER, NOT A SCRATCH REGISTER.**
                                ; It was loaded from [b2_runmax] above and is
                                ; stored into [es:0x0004] below; using it to
                                ; build the tail's segment here published
                                ; KERNEL_SEG + KZ_RPARA as the run bound -
                                ; 1472 on the 360KB disk, where the cylinder
                                ; is 18. The kernel only tests the word `!= 0`
                                ; (disk.inc), so the boot was correct and the
                                ; FINDING was nonsense: SPEC.md 18.93.1's own
                                ; published number, wrong on every compressed
                                ; kernel whose KSIG sector lands past the
                                ; plain head, and silent because nothing but
                                ; tests/cylrun.py ever reads the value.
    mov ax, es                  ; ES is KERNEL_SEG, and the two adjustments
    add ax, KZ_RPARA            ; CANCEL: the tail's base is +head/16 +R and
    mov es, ax                  ; the offset inside it is KSIG_OFF - head, so
    cmp [es:KSIG_OFF], bx       ; +R and the SAME OFFSET is the same address
    pop ax
    pop es
  %endif
%else
    cmp [es:KSIG_OFF], bx
%endif
    jne .rerun
    mov [es:0x0004], ax         ; ...and tell the kernel what we learned, so
                                ; dsk_xfer needs no probe of its own. Written
                                ; ONLY here, on the one path that has seen a
                                ; head crossed and come back right: every other
                                ; way out leaves the image's own ZERO, which is
                                ; what makes the kernel's test `!= 0`
.nocross:
%endif
%ifdef KZIP
    ; --- EXPAND THE TAIL (SPEC.md 2.9.13) -----------------------------------
    ; AFTER the canary, and that ordering is the whole safety argument for an
    ; unbounded decoder: the one failure that actually happens to this read is
    ; a BIOS returning the other head's sectors, and 18.93.1 has already
    ; verified the TRANSFER by the time a byte is expanded.
    ;
    call kz_all                 ; ...and it is a routine because the HARD DISK
    push cs                     ; calls it too (2.9.13.5)
    pop ds                      ; DS back to us; ES is re-loaded below
    mov ax, KERNEL_SEG
    mov es, ax
%endif
    call spl_unhook             ; GIVE INT 08h BACK (SPEC.md 15.3.8.2). Not
                                ; tidiness: leave it and sched_init saves OUR
                                ; handler as sch_old08 and goes on calling it,
                                ; while spl_finish gives the whole blob back to
                                ; the heap (2.9.5) - so the chain would end in a
                                ; far call into freed memory. From here to
                                ; sched_init the animation is spl_paint's
                                ; fail-safe again, which is the notch
    mov dl, [b2_drive]          ; the kernel may want to know the boot drive
%ifdef BOOT_STOP
%ifdef BOOT_NOSPLASH
    mov ax, 0x0E2A              ; '*' on the text screen the splash never took
    mov bx, 7                   ; over, so a HALT is legible where a machine
    int 0x10                    ; that went round leaves nothing behind
%endif
    cli                         ; STOP one instruction short of the handoff
.stop:
    hlt
    jmp short .stop
%endif
    jmp KERNEL_SEG:0x0000

%ifndef TRACK_RUN
; --- shorten the run and load the whole kernel again (SPEC.md 18.93) --------
; ONE block, TWO ways in, and they are the two ways a crossing run can be
; wrong. The canary above catches a BIOS that flipped heads at the wrong
; sector and answered CF=0 anyway; read_run's exhausted retry catches the
; other kind - a controller that refuses a multi-track read outright and
; ERRORS. Both want the same thing: the bound the FDC cannot get wrong, and a
; second pass.
;
; ONE-SHOT by construction, because it can only be entered while [b2_runmax]
; is still wider than a track - so a genuinely dead drive reaches read_run's
; halt on the second pass instead of retrying for ever.
;
; SP is re-established rather than unwound: read_run reaches here with its own
; return address still on the stack, and stage 1 left SP at its own STACK_TOP.
.rerun:
    mov ax, [b2_spt]
    mov [b2_runmax], ax
    mov sp, B2_STACK
    jmp .reload
%endif

%ifdef KZIP
; -----------------------------------------------------------------------------
; kz_all - expand every block of the tail, in place (SPEC.md 2.9.13)
; in:  the packed tail at KERNEL_SEG + KZ_HEADSEC*512/16 + KZ_RPARA, which is
;      where BOTH loaders put it
; out: the kernel, whole, at KERNEL_SEG
; clobbers: AX BX CX DX SI DI BP DS ES, flags
;
; Block i's bytes are at (head + R) paragraphs up plus what the blocks before
; it occupy; block i lands at (head + i * KZ_BLK). Both walk by whole
; PARAGRAPHS - os88kz.py pads each block to one - so neither pointer needs an
; offset and the decoder starts every block at zero.
;
; A ROUTINE and not the straight line it began as, because the hard disk's
; boot sector calls it too (2.9.13.5) and a second copy of a decoder's driver
; loop is the shape that drifts. It is entered with DS meaning nothing.
; -----------------------------------------------------------------------------
kz_all:
    mov bx, KERNEL_SEG + (KZ_HEADSEC * 512) / 16    ; where block 0 lands...
    mov ax, bx
    add ax, KZ_RPARA                                ; ...and where its bytes are
    mov cx, KZ_NBLK
.blk:
    push cx
    push ax
    mov ds, ax
    xor si, si
    lodsw                       ; the block's packed length...
    mov bp, ax
    lodsw                       ; ...and what it expands to
    mov dx, ax
    mov es, bx
    push bx
    push bp
    call kz_expand              ; DS:SI -> ES:0, DX bytes, and DS:SI is left
    pop bp                      ; past the stream
    pop bx
    pop ax
    add bp, 4 + 15              ; the header, and up to the next paragraph
    mov cl, 4
    shr bp, cl
    add ax, bp                  ; ...the next block's bytes
    add bx, KZ_BLK / 16         ; ...and where they go
    pop cx
    dec cx
    jnz .blk
    ret

; -----------------------------------------------------------------------------
; kz_hd - the HARD DISK's way in (SPEC.md 2.9.13.5)
; in:  the WHOLE tail - the plain head AND the packed blocks - at
;      KERNEL_SEG + KZ_RPARA, which is where boot/boothd.asm read it
; out: the kernel, whole, at KERNEL_SEG. Every register but the flags is as it
;      was; DS and ES included, because the caller reads its own variables
;      after this returns and hands the kernel a BP it must not lose
;
; **THE HARD DISK NEVER ENTERS STAGE 2**, and that is the whole reason this
; exists. boot/boothd.asm is a volume boot record with its own reader: it
; loads the blob to BLOB_SEG, loads `.text` to KERNEL_SEG and jumps there,
; never touching boot2_entry (2.9.9). So a packed kernel arriving that way
; would be jumped INTO, and the sector has 23 spare bytes to do something
; about it in.
;
; What makes it fit in five of them is the ARITHMETIC AGREEING. The floppy
; reads the head to KERNEL_SEG and the blocks to KERNEL_SEG + head/16 + R; the
; hard disk reads one run to KERNEL_SEG + R, which puts the head at the bottom
; of it and the blocks at KERNEL_SEG + R + head/16 - THE SAME PLACE. So the
; whole of the difference is moving the head down, and kz_all is then the
; identical call on identical addresses. The move cannot overlap: R is 20,992
; and the head is 4,608.
; -----------------------------------------------------------------------------
kz_hd:
    push bp                     ; the caller's boot timer, which it writes into
    push ds                     ; the kernel AFTER this returns
    push es
    cld
    mov ax, KERNEL_SEG + KZ_RPARA
    mov ds, ax
    mov ax, KERNEL_SEG
    mov es, ax
    xor si, si
    xor di, di
    mov cx, (KZ_HEADSEC * 512) / 2
    rep movsw                   ; the plain head, down to where it belongs
    call kz_all
    pop es
    pop ds
    pop bp
    retf

; -----------------------------------------------------------------------------
; kz_expand - one LZ4 block (SPEC.md 2.9.13.2)
; in:  DS:SI = the block's stream, ES = where it expands to, DX = its length
; out: DS:SI one past the stream, DI = DX
; clobbers: AX, BX, CX, DI, BP, flags
;
; **UNBOUNDED, AND THAT IS A DECISION RATHER THAN AN OMISSION.** Every other
; decoder in this system is bounds-checked because every byte off a disk is
; hostile (SPEC.md 19); this one produces THE KERNEL, and the alternative to
; expanding it wrongly is jumping into it wrongly - a bound cannot make that
; safe. What can, and does, is 18.93.1's canary, which has already verified
; the transfer before this runs. So there is no offset test, no source-end
; test and no output-overflow test: the loop stops when it has produced DX
; bytes, which is what the block header said.
;
; It is a COPY of kernel/lz.inc's LZ4 arm minus those tests, and the copy is
; the point: lz.inc lives in `.cold`, which is inside the bytes being expanded.
; This one is in the blob and mem_unblob gives it back to the heap at the end
; of kmain, so it costs the running machine nothing at all.
;
; ONE SEGMENT, because os88kz.py caps a block at KZ_BLK - so none of
; SPEC.md 20.14.5's crossing machinery is here, and the whole of what that
; simplification costs is two sectors of the forty it saves.
; -----------------------------------------------------------------------------
kz_expand:
    xor di, di
.seq:
    lodsb                       ; the token: literal length in the high nibble,
    mov bl, al                  ; match length - 4 in the low one
    mov cl, 4
    shr al, cl
    xor ah, ah
    mov cx, ax
    cmp al, 15
    jne .lits
.lx:
    lodsb                       ; ...and its extension bytes, 255 at a time
    xor ah, ah
    add cx, ax
    cmp al, 255
    je .lx
.lits:
    rep movsb                   ; CX = 0 is legal and moves nothing
    cmp di, dx
    jae .done                   ; a well-formed block ENDS on a literal run
    lodsw                       ; the match offset
    mov bp, ax                  ; ...banked while its length is read, which
    mov al, bl                  ; comes out of the SOURCE and so has to happen
    and al, 0x0F                ; before DS stops being the source
    xor ah, ah
    mov cx, ax
    cmp al, 15
    jne .mat
.mx:
    lodsb
    xor ah, ah
    add cx, ax
    cmp al, 255
    je .mx
.mat:
    add cx, 4                   ; the minimum match, which the token omits
    push ds
    push si
    mov si, di
    sub si, bp
    push es
    pop ds                      ; a match reads what we have already written
    rep movsb
    pop si
    pop ds
    jmp short .seq
.done:
    ret
%endif

; -----------------------------------------------------------------------------
; read_run - read as many sectors from [b2_lba] to ES:0000 as one int 13h may
;            carry (SPEC.md 18.93). Retries three times, resetting the
;            controller between attempts, because floppy reads fail spuriously
;            often enough to matter.
; in:  ES = destination segment, [b2_left] = sectors still wanted
; out: AX = sectors actually transferred, 1..run
; clobbers: ax, bx, cx, dx, si, di
;
; A run stops at the first of three bounds, and the third is the one that is
; easy not to think of:
;   1. [b2_runmax] - the end of the cylinder, or with TRACKRUN=1 the end of
;      the track (SPEC.md 18.91.1). Either way EOT stays SPT.
;   2. the sectors still wanted
;   3. the 64KB DMA page - a single 512-aligned sector cannot cross one, a run
;      can, and the controller answers a straddle with error 09h
; -----------------------------------------------------------------------------
read_run:
    ; --- 1 and 2: the run bound, and what is left to read --------------------
    mov di, [b2_left]           ; bound 2 first, so bound 1 only has to beat it
    mov ax, [b2_lba]
    xor dx, dx
    mov bx, [b2_runmax]
    div bx                      ; div spends AX and DX and leaves BX, so the
    sub bx, dx                  ; distance to the end of the run costs one
    cmp bx, di                  ; subtract and no reload
    jae .page
    mov di, bx

    ; --- 3: the 64KB DMA page ----------------------------------------------
.page:
    mov ax, es
    mov cl, 4
    shl ax, cl                  ; the low 16 bits of the physical address
    neg ax                      ; bytes to the page end (0 = a whole page)
    jz .have
    mov cl, 9
    shr ax, cl
    cmp ax, di
    jae .have
    mov di, ax                  ; ...and never zero: every destination here is
                                ; 512-ALIGNED (ES starts at KERNEL_SEG and
                                ; advances 32 paragraphs a sector), so the
                                ; remainder is a multiple of 512 and the shift
                                ; can only reach 0 from an AX the jz has taken
.have:
%ifdef FLOPPY_ONE
    mov di, 1                   ; FLOPPY1=1: one sector a call, the transfer
%endif                          ; as it was before any of this
    mov [b2_runsz], di
    mov si, 3                   ; attempts

.attempt:
    ; LBA -> CHS, rebuilt every attempt because a controller reset owes us
    ; nothing.  sector = lba % spt + 1, head = (lba / spt) % heads,
    ;                                cylinder = (lba / spt) / heads
    ;
    ; BOTH divides are general, where the sector had `%if HEADS == 2` and a
    ; shift: one kernel serves every geometry (see the header), so the head
    ; count is a variable and cannot be tested at assembly time.
    mov ax, [b2_lba]
    xor dx, dx
    div word [b2_spt]
    inc dx
    mov cl, dl                  ; CL = sector (1-based)
    xor dx, dx
    mov bl, [b2_heads]
    xor bh, bh
    div bx
    mov ch, al                  ; CH = cylinder (low 8 bits; never over 255)
    mov dh, dl                  ; DH = head
    xor bx, bx                  ; ES:BX - the offset is always zero
    mov dl, [b2_drive]
    mov ax, [b2_runsz]          ; AL = the run...
    mov ah, 0x02                ; ...AH = 02 read
    int 0x13
    jnc .done

%ifdef BOOT_DIAG
    mov [b2_diag], ah           ; BOOTDIAG=1: the reset below destroys the
%endif                          ; status, and the status IS the diagnosis
    xor ah, ah                  ; reset and try again
    mov dl, [b2_drive]
    int 0x13
    dec si
    jnz .attempt

%ifndef TRACK_RUN
%ifndef FLOPPY_ONE
    mov ax, [b2_runmax]         ; three attempts at a run that CROSSES a head,
    cmp ax, [b2_spt]            ; then the whole load again at the track bound
    jne boot2_entry.rerun       ; (SPEC.md 18.93). A controller that will not
%endif                          ; do a multi-track read at all ERRORS rather
%endif                          ; than lying, and the canary cannot see that -
                                ; it only ever runs after a load in which every
                                ; call answered CF=0
%ifdef BOOT_DIAG
    ; Two hex digits and nothing else: 0C is a media type the drive could not
    ; identify (a 360KB disk in a 1.2MB drive), 04 a sector the FDC never found
    ; (EOT / the multi-track flip), 09 a transfer that crossed a 64KB DMA page,
    ; 80 a drive that never answered. aam splits the byte in one instruction.
    ;
    ; IT KEEPS THE CANARY NOW. This knob existed because 510 bytes would not
    ; hold both and the sector gave 18.93.1 up to pay for the digits; down here
    ; there are hundreds of bytes spare and nothing has to be traded (2.9).
    mov al, [b2_diag]
    aam 0x10
    push ax
    mov al, ah
    call .nib
    pop ax
    call .nib
    jmp short .stop
.nib:
    add al, 0x90                ; the classic six bytes: 0..15 -> '0'..'F'
    daa
    adc al, 0x40
    daa
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    ret
%endif
    mov si, b2_msg_err
.halt:                          ; write the NUL-terminated string at DS:SI via
    mov bx, 0x000F              ; BIOS teletype and STOP. BL carries the colour
.pnext:                         ; so it stays legible if the splash already
    lodsb                       ; switched us into mode 12h
    test al, al
    jz .stop
    mov ah, 0x0E
    int 0x10
    jmp short .pnext
.stop:
    cli
    hlt
    jmp short .stop

.done:
    ; CF=0 IS the BIOS saying the whole request completed, and AL is not - the
    ; same reading dsk_xfer takes (SPEC.md 18.91). On the IBM 5150 a
    ; nine-sector read MOVES ALL NINE and answers AL = 1, and trusting AL took
    ; a 16KB read from 8.29 s to 2.09 when it was fixed.
    ;
    ; `make DISKAL=1` restores the old reading, in both loops together.
%ifdef DISK_TRUST_AL
    xor ah, ah
    cmp al, 1                   ; 0 with CF=0 is trusted for one, so the loop
    adc al, 0                   ; always progresses
    cmp ax, [b2_runsz]
    jbe .out
%endif
    mov ax, [b2_runsz]          ; the whole run: CF=0 is the contract
.out:
    ret

b2_msg_err:  db 'Disk error', 0
b2_drive:    db 0
b2_heads:    db 0
b2_spt:      dw 0
b2_runmax:   dw 0
b2_runsz:    dw 0
b2_lba0:     dw 0
b2_lba:      dw 0
b2_left:     dw 0
b2_dest:     dw 0
b2_ksig:     dw 0
b2_t0:       dw 0
%ifdef BOOT_DIAG
b2_diag:     db 0
%endif
