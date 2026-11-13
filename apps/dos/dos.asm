; =============================================================================
; os8088 - apps/dos/dos.asm
;
; DOS (SPEC.md 96) - run a DOS .COM or .EXE program.
;
; NOT AN EMULATOR. This machine is an 8086 in real mode and a DOS .COM is
; machine code for the processor already running, so nothing here interprets
; anything: the package builds the memory a DOS program expects, points
; INT 21h at code of its own, and far-jumps in. What it provides is the
; operating SYSTEM the program calls, not the processor it runs on.
;
; WAVE 1 (docs/plans/DOS-EXEC-PLAN.md 11): .COM only, read-only file access,
; INT 20h and INT 21h AH=00h/02h/09h/30h/4Ch. Everything else refuses with
; DOS's own "invalid function" rather than hanging, and the window names the
; function that was asked for - SPEC.md 47's refusal, so an unsupported
; program reports its own gap.
;
; THE ORDER IN dos_run IS BINDING (SPEC.md 96.2). The disk read happens in
; the wake handler's own lock-free context, because file slots are legal
; there and a floppy read under the gfx lock is the freeze SPEC.md 7.4
; exists to avoid; the lock is taken only for the bracket itself.
;
; EVERY KERNEL FILE CALL GOES THROUGH dos_be (SPEC.md 96.4). It is one
; near-call table today with one implementation, and it is here from the
; first line because docs/plans/DOS-EXEC-PLAN.md 14 wants a mode where the
; kernel is hibernated out and the file half is served by something else. A
; table makes that a second back end; direct calls would make it a rewrite
; of every file function.
; =============================================================================
%ifdef DOSP_MAP                     ; THE FIRST PASS OF THE WINDOWED PART'S
[map all DOSP_MAP]                  ; BUILD (SPEC.md 96.54.9): the host's map is
%endif                              ; what the part names the host by, and what
                                    ; tools/os88dvwexp.py checks it against

%include "os88api.inc"
%include "doscall.inc"              ; **EARLY, AND IT HAS TO BE** (SPEC.md
                                    ; 96.44.5): in a host build this is what
                                    ; turns every core name into its slot in
                                    ; the table at CORE_ORG, and a %define
                                    ; reaches only the lines after it. Included
                                    ; at the bss table - where CORE_BSS_AT is
                                    ; first wanted - it left every call site
                                    ; above it naming a symbol that is not in
                                    ; this assembly, which reads as thirty-one
                                    ; undefined symbols and not as an ordering
                                    ; mistake
%ifdef DOSKPART
%include "kdlaunch.inc"             ; the launch block's layout and its list -
                                    ; ONE file, %include'd by this side and by
                                    ; kern_dos's entry, which is what makes the
                                    ; gather and the scatter one list. EARLY,
                                    ; because the preprocessor is sequential
                                    ; and dos_lbfill is 3,000 lines above the
                                    ; part table
%endif
%include "netpkg.inc"               ; THE SOCKET DRIVER'S OWN HEADER, for the
                                    ; NETV_RAW* verbs the packet driver rests
                                    ; on (SPEC.md 72.22, 96.23). Constants
                                    ; only at this point in the file; the code
                                    ; half is os88sock.inc at the end

; **THE PACKAGE CONTAINER IS NOT THE CORE**
; (docs/plans/KERN-DOS-PLAN.md §4.1.2). A kerndos root includes this file
; whole and is not a package at all: no header, no icon, no association
; block, and nothing at file offset 0 but a jump. Those four macros assert their own file offsets - 0, 32, 96 and 112
; - so under that root they are the only thing in 13,000 lines that cannot
; assemble, which is a finding rather than a nuisance: the WINDOW half is not
; the obstacle anybody expected it to be.
%ifndef KD_BACKEND
%ifdef DOSKPART
 %ifdef DOSTRACE
  %error "DOSKPART and DOSTRACE both want the part table and wave 5 has not merged them - build one at a time"
 %endif
 %define DOS_PARTED 1
%endif
%ifdef DOSTRACE
 %define DOS_PARTED 1
%endif
%ifdef DOS_PARTED
    ; **A PARTED BUILD, AND THE SHIPPED ONE IS NOT** (SPEC.md 96.29.1). flags
    ; bit 2 is OS88_F_PARTS, and it is behind the %ifdef for the reason the
    ; trace instrument is: a byte the shipped build pays for something it does
    ; not use is a byte the DOS program does not get. It costs MORE than its
    ; own bytes, too - tools/os88pkg.py refuses whole-file compression on a
    ; parted package, so the shipped DOS.O88 would give back 5,425 bytes of it
    ; (docs/reports/KERN-DOS-PART-COST-2026-09-14.md).
 %ifdef DOS_MEGA
    ; ...and kern_mega's says OS88_HIGH: it is a RE-HOMED program (DOSLOAD
    ; is its loader), so on kern_mega its keep - this image and its bss, the
    ; INT 21h core copied in at CORE_ORG - goes above 1MB before it runs
    ; (SPEC.md 20.12.10.9, KERN-MEGA-PLAN 14.44). kern_big's DOS.O88 is
    ; built without DOS_MEGA and stays byte-identical. OS88_HIGHMEM too
    ; (KERN-MEGA-PLAN 14.52): on kern_mega every launch is the whole
    ; machine (dos_mem_fix), so dos_run's arena claim is never reached, and
    ; what the bit moves above 1MB - the packet driver's buffers, the
    ; prompt's DIR and COPY buffers, os88ui's save-under - is only ever
    ; loaded into ES or handed to a kernel slot
    OS88_HEADER 'DOS', dos_entry, 3 | OS88_F_GLYPH | OS88_F_PARTS, OS88_HIGH | OS88_HIGHMEM
 %else
    OS88_HEADER 'DOS', dos_entry, 3 | OS88_F_GLYPH | OS88_F_PARTS
 %endif
%else
    OS88_HEADER 'DOS', dos_entry, 3 | OS88_F_GLYPH
%endif
                                        ; flags bit 0 = icon, bit 1 = the
                                        ; association block after it, bit 5
                                        ; the document glyph after THAT
                                        ; (SPEC.md 54.3.2)

%ifndef KD_BACKEND                  ; **A DOS PROGRAM IN ITS WINDOW, LIVE**
 %ifndef DOS_MEGA                   ; (dosw.inc, SPEC.md 96.54): the windowed
  %define DVW                       ; host on kern_big; kern_mega has none
 %endif                             ; (KERN-MEGA-DOS-PLAN 2.1) and kern_dos
%endif                              ; is the bracket itself
; --- WHERE THE PACKET DRIVER LIVES (SPEC.md 96.23.11) -----------------------
; THREE SHAPES and one source (apps/dos/dospkt.inc, apps/dos/dosnet.inc):
;   DOS_NETPART   kern_big's and kern_emu's DOS.O88 - the family is PART 4,
;                 read by op_self into the network claim when a run wants it
;   DOS_NONET     kern_mega's - every launch is the whole machine there
;                 (dos_mem_fix), so dos_pkt_start is unreachable and the
;                 family would be code that could only claim and free
;   DOS_NETINLINE a build with no part table (build/dos.o88, DOSTRACE) - the
;                 family in the host, as it always was
; and kern_dos (KD_BACKEND) has none of it (SPEC.md 96.43).
%ifndef KD_BACKEND
 %ifdef DOSKPART
  %ifdef DOS_MEGA
   %define DOS_NONET
  %else
   %define DOS_NETPART
   %define DOS_NETANY
  %endif
 %else
  %define DOS_NETINLINE
  %define DOS_NETANY
 %endif
%endif
; --- ...AND WHERE THE SETUP PAGE LIVES (SPEC.md 96.32.2.3) -------------------
; TWO SHAPES and one source (apps/dos/dosset.inc): DOS_SETPART on kern_big's
; and kern_emu's DOS.O88 - the page's code is PART 5, read by op_self when
; Setup opens - and inline everywhere else: kern_mega's (its host is above
; 1MB, so the bytes cost no conventional memory) and a build with no part
; table. DOS_SETEMIT is the Makefile's second assembly, which appends it.
%ifndef KD_BACKEND
 %ifdef DOSKPART
  %ifndef DOS_MEGA
   %define DOS_SETPART
  %endif
 %endif
%endif
%ifdef DOS_SETEMIT
 %ifndef DOS_SETPART
  %error "DOS_SETEMIT appends the Setup PART: it needs -DDOSKPART and not -DDOS_MEGA"
 %endif
%endif
SPD_PAINT   equ 0                   ; the Setup part's doors (dosset.inc's
SPD_PLACE   equ 1                   ; sp_door, AL): its body and furniture,
SPD_CLICK   equ 2                   ; dos_place's Setup half, a press, the
SPD_ROW     equ 3                   ; arena row alone, and Save Shortcut
SPD_SAVE    equ 4
SPH_HOST    equ 4                   ; ...and where in its head the host's
                                    ; bouncer goes, a far pointer
DOS_MWINW   equ 15                  ; `Run in a window`, in cells (96.32.2.4)
; THE DOORS' BODIES, written once: the part's sp_door runs them, and where
; the page is inline the host's call sites ARE them - so a build with no part
; pays no dispatcher (dosset.inc)
%macro SPB_PAINT 0
    push bx                         ; the body, then the furniture every
    call dos_paint_set              ; setup page shares (SPEC.md 96.32.2)
    pop bx
    call dos_paint_furn
%endmacro
%macro SPB_PLACE 0
    call dos_furn_rects             ; Save Shortcut, Return and the box...
    call dos_cmd_place              ; ...the command box on the title row...
    call dos_fld_place              ; ...the arguments box...
    call dos_senv_place             ; ...every environment row under it...
    call dos_mfld_place             ; ...and the memory block's two
    call dos_mrad_place
%endmacro
%macro SPB_CLICK 1                  ; %1 = where to go once a control took it
%ifdef DVW
    call sp_wck                     ; `Run in a window` (96.32.2.4)...
    jnc %1
%endif
%ifdef DOS_WTAIL
    call sp_hck                     ; ...`Close when done` (96.44.20)...
    jnc %1
%endif
    call dos_click_mem              ; ...the memory block's field and radio
    jnc %1                          ; CF=1 = none of those, so it is the
    call dos_fld_hit                ; arguments box or an environment row
%endmacro
; --- AND WHERE THE WINDOWED LAYER LIVES (SPEC.md 96.54.9) -------------------
; The same three shapes: DVW_SPLIT is the parted host, whose layer is PART 5
; of DOS.O88 (apps/dos/doswp.asm) and of which only doswr.inc's stubs and the
; resident pieces of dosw.inc stay here; everything else - build/dos.bin,
; DOSTRACE, kern_small's plain dos.o88, and -DDVW_NOSPLIT for an A/B - keeps
; it INLINE, which is the identity shape of apps/dos/doswabi.inc's macros.
%ifdef DVW
 %ifdef DOSKPART
  %ifndef DVW_NOSPLIT
   %define DVW_SPLIT
   %ifndef DOS_NOWTAIL              ; ...and THAT host's window is the tail of
    %define DOS_WTAIL               ; its region (DOS-HEADLESS-PLAN 4.4, SPEC.md
   %endif                           ; 96.44.19); -DDOS_NOWTAIL is the A/B
  %endif
 %endif
%endif
%ifdef DVW
%include "doswabi.inc"              ; ...and its macros, EARLY: HENT() names
%endif                              ; the layer's entries from here on
%ifdef DVW
 %define DOS_ACAP dvw_cap           ; dos_run's arena cap (SPEC.md 96.54.2)
DVW_DEFKB   equ 128                 ; ...a window's, when the user set none
DVW_STEPKB  equ 16                  ; ...and the step an AUTOMATIC limit grows
                                    ; in, to fit its program (96.54.2.1)
%if DVW_STEPKB & (DVW_STEPKB - 1) || DVW_STEPKB > 128 || DVW_DEFKB % DVW_STEPKB
 %error "dvw_regrow rounds with `and al, -DVW_STEPKB`: a power of two, a divisor of DVW_DEFKB"
%endif
%else
 %define DOS_ACAP dos_memkb
%endif
%include "dosicon.inc"          ; the icon, the association block and the
                                ; document glyph, shared with dosload.asm
%endif                              ; KD_BACKEND

; --- THE WINDOW'S TAIL (docs/plans/DOS-HEADLESS-PLAN.md 4.3, 4.4) -----------
; Everything that only SHOWS the window - the console, the prompt, the
; controls, the line editor, the paint, the menus and the windowed layer's
; resident stubs - is assembled into `.wtext`, a section placed after every
; byte of the host's own image and bss, with the window's own bss after IT.
; So the region reads `[header][core slot][the resident host][the window]`,
; and a host that never shows a window need never hold the last part.
;
; `DOS_WTAIL` is that layout. It is the parted host's on kern_big and kern_emu
; (DVW_SPLIT) and nobody else's: kern_mega's and kern_small's builds, the
; plain `build/dos.bin` and `kern_dos` assemble every macro below to nothing,
; byte for byte what they were (the plan's section 7).
;
; THE BOUNDARY IS A GATE, NOT A CONVENTION: nasm resolves a near reference
; across a section silently, and in a region with no tail the tail's address
; is somebody else's claim. So every reference from the resident host into
; the tail is spelled inside `WIN_ONLY` ... `WIN_END`, whose opening IS the
; test (`call dos_hq` / `jne`: dos_hq is the one `cmp byte [dos_head], 0`,
; which spelled at every bracket was two bytes a bracket more for a call
; and a ret on paths that run once a launch), and `tools/doswtail.py` assembles
; the host twice - the second time with the tail moved up by DOS_WSHIFT bytes
; - and FAILS THE BUILD on any byte of the resident host that moved with it
; outside such a bracket. A branch, a memory operand, an immediate and a
; table entry are all caught alike, because each is a byte that depends on
; where the tail is.
;   WIN_CODE / WIN_CODE_END   what is between them goes into the tail. A
;                             routine either side of one must END in an
;                             unconditional transfer: nothing falls across
;   WIN_ONLY / WIN_ONLYF      open a bracket (short / near skip), and
;   WIN_ELSE / WIN_END        ...its headless arm, and its close
;   WBSS name, n              a bss row of the TAIL's (HBSS's shape)
%ifdef DOS_WTAIL
 %ifndef DVW_SPLIT
  %error "DOS_WTAIL is the parted kern_big host's layout (DVW_SPLIT)"
 %endif
section .wtext follows=.text align=16
dos_wtail_beg:                      ; part 1's first byte (DOS_WTAILKB)
 %ifdef DOS_WSHIFT
    times DOS_WSHIFT db 0           ; tools/doswtail.py's second assembly
 %endif
section .text
 %macro WIN_CODE 0
section .wtext
 %endmacro
 %macro WIN_CODE_END 0
section .text
 %endmacro
 %assign DOS_WBN 0               ; brackets so far: -DDOS_WBREAK=n leaves the
 %macro WIN_ONLY 0                ; n-th one's test out, which is how the gate
  %push dwonly                    ; is shown to go red (tests/unit/t_doswtail.py)
  %assign DOS_WBN DOS_WBN + 1
  %ifndef DOS_WBREAK
    call dos_hq                     ; THE TEST IS THE BRACKET: the checker
    jne short %$end                 ; reads its range off this jump
  %elif DOS_WBN != DOS_WBREAK
    call dos_hq
    jne short %$end
  %endif
 %endmacro
 %macro WIN_ONLYF 0
  %push dwonly
  %assign DOS_WBN DOS_WBN + 1
  %ifndef DOS_WBREAK
    call dos_hq
    je short %%in
    jmp near %$end
  %elif DOS_WBN != DOS_WBREAK
    call dos_hq
    je short %%in
    jmp near %$end
  %endif
%%in:
 %endmacro
 %macro WIN_ELSE 0
  %ifnctx dwonly
   %error "WIN_ELSE outside a WIN_ONLY bracket"
  %endif
  %repl dwelse
    jmp %$fin
%$end:
 %endmacro
 %macro WIN_END 0
  %ifctx dwelse
%$fin:
  %elifctx dwonly
%$end:
  %else
   %error "WIN_END outside a WIN_ONLY bracket"
  %endif
  %pop
 %endmacro
%else
 %macro WIN_CODE 0
 %endmacro
 %macro WIN_CODE_END 0
 %endmacro
 %macro WIN_ONLY 0
  %push dwonly
 %endmacro
 %macro WIN_ONLYF 0
  %push dwonly
 %endmacro
 %macro WIN_ELSE 0             ; no tail: the window is always there, and a
  %error "a headless arm is DOS_WTAIL's: put %ifdef DOS_WTAIL round WIN_ELSE and the arm"
 %endmacro
 %macro WIN_END 0
  %pop
 %endmacro
%endif

; --- AND THE HOLE THE CORE GOES IN (SPEC.md 96.44.5) ------------------------
; A host does not contain the core; it RESERVES it. `CORE_ORG` is where the
; core's jump table sits in every host's segment, `CORE_MAX` is the budget its
; code is cut from and `CORE_BSS_SIZE` the block its state lives in - so a
; host's own code begins above all three and every core address is the same
; number wherever it is read from.
;
; ZEROS IN THE FILE COST NOTHING ON DISK: this is `OP_SEG | OP_COMP` part, and
; a run of zeros is what LZ4 is best at. What it does cost is the host's RAM,
; which is why `CORE_MAX`'s slack is a number worth keeping small.
%ifndef KD_BACKEND                  ; DOS.O88's reservation only: kern_dos makes
%ifdef DOS_EXTCORE                  ; its own in kerndos/kdos.asm, above its
                                    ; API refusal wall, and would otherwise
                                    ; make a second one here
  %if ($ - $$) > CORE_ORG
    %error "DOS.O88's own header and icon reached CORE_ORG - raise it in \
apps/dos/doscall.inc. Since SPEC.md 96.44.6 this side is what BINDS it: the \
header, the icon, the association block and the document glyph end at 128 \
and kern_dos's own fixed header is eight bytes, so CORE_ORG is cut from THIS \
reservation"
  %endif
    times CORE_ORG - ($ - $$) db 0
    times CORE_MAX + CORE_BSS_SIZE db 0
%endif                              ; DOS_EXTCORE
%endif                              ; KD_BACKEND

; DOS_CONT_W/H WERE HERE and are gone (SPEC.md 96.20.3): they were 286 and 81,
; derived by hand from a 288x100 template, and the window is adapter-sized
; now. OSAPI_WM_GEOM answers both, is correct after a resize or a drag across
; a display seam, and cannot go stale when somebody edits the template.

; --- the shell's REASON CODES (SPEC.md 96.30, 96.44.5) ----------------------
; Lifted out of `apps/dos/dosh.inc`, which is the CORE, because the window
; prompt tests DSHW_NOCMD and the core is what sets it - a constant defined on
; the core's side of the seam is a host reading across it. An equate emits
; nothing, so the container costs neither host a byte and both see one list.
DSHW_NONE   equ 0
DSHW_MEM    equ 1               ; no DOS block for the copy buffer (96.30.6)
DSHW_IO     equ 2               ; the back end refused
DSHW_NOFILE equ 3               ; nothing matched the pattern
DSHW_SELF   equ 4               ; source and destination are one file
DSHW_NOPATH equ 5               ; no such folder
DSHW_SYNTAX equ 6               ; wrong number of arguments
DSHW_REDIR  equ 7               ; a redirection target that is not NUL
DSHW_NOCMD  equ 8               ; no such verb - which from a prompt means
                                ; "try it as a program" (SPEC.md 96.33.3)
DSHW_NODRV  equ 9               ; `X:` named a drive that is not there
DSHW_BADSW  equ 10              ; a switch this DOS has not got (96.33.9)

DOS_MIN_KB  equ 64                  ; a machine that cannot offer this much has
                                    ; nothing worth running a DOS program in,
                                    ; and saying so is cheaper than a program
                                    ; that dies on its first allocation

; THE DISK CACHE IS WORTH MORE TO A DOS PROGRAM THAN THE RAM IT SITS IN
; (SPEC.md 96.24). A .COM owns every byte after its image, so the honest thing
; to ask for is "everything" - and everything includes SPEC.md 18.95's
; directory read-ahead window, which the claim used to shed to make that true.
; Measured loading Prince of Persia off a 720KB floppy on a 4.77MHz 5150, the
; cache alive against the cache shed, the program's own int 13h traffic is
; SEVEN TIMES what IBM DOS 3.30 makes on the same disk. The floor is the whole
; of the fix (SPEC.md 50.6.6): nothing at or above MEM_PG_HIGH is shed or
; dropped, so the compaction packs the window out of the way instead.
;
; A CONSTANT AND NOT A NUMBER ANYBODY MAY PICK, because the level it names has
; to be the same in both calls - the AVAIL that plans and the CLAIM that acts.
; --- what ARM 1's estimate is made of (SPEC.md 96.36.3) ---------------------
; Neither term can be asked of anything: the machine the figure describes has
; no kernel in it. So they are constants, and the honest instrument for them
; is not an assembly-time mirror - `kern_dos`'s floor moves with its own image
; and a gate on it would fail this build every time that image changed a byte
; - but a RUN: `tests/dosram.py` puts a program through arm 3 and compares
; what the page promised against the arena DOS carries home (96.41.1).
DOS_LOWKB   equ 1                   ; KB below KERNEL_SEG: the IVT, the BDA and
                                    ; the boot area. 0x0060 paragraphs is 1.5
                                    ; and this rounds DOWN, which is the
                                    ; direction an estimate should err
DOS_KDKB    equ 42                  ; ...and what `kern_dos` keeps below the
                                    ; program: its own image, the FAT window,
                                    ; the mount buffers and the stack -
                                    ; `LOW_SEG + KD_LOW_KB * 64` over there,
                                    ; in KB
%ifdef DOS_MEGA
DOS_KDKB_HMA equ 9                  ; ...on kern_mega with kern_dos in the HMA
                                    ; (SPEC.md 96.40.9): only the FAT window,
                                    ; the mount buffers and the stack stay low
DOS_KDKB_LOW equ 45                 ; ...and with it LOW (96.40.9.2): kern_mega's
                                    ; image rung, which is not kern_big's
%endif
DOS_PG_FLOOR equ MEM_PG_HIGH

; --- the arena's shape, in PARAGRAPHS (SPEC.md 96.3) -------------------------
DOS_ENVP    equ 32                  ; 512 bytes of environment block. IT WAS
                                    ; 8, which is 128 - and BLASTER= alone is
                                    ; ~24 of those, before the program's own
                                    ; path and anything the user typed
                                    ; (SPEC.md 96.20). Growing it moves the
                                    ; PSP and the program's load base, which
                                    ; is why it is a constant here and not a
                                    ; number anybody may pick
DOS_ENVMCB  equ 0                   ; para 0     : the environment's MCB
DOS_ENVSEG  equ 1                   ; para 1     : the environment itself
%if DOS_ENVSEG != 1
 %error "an `inc ax` stands for `add ax, DOS_ENVSEG` (size pass, SPEC.md 96.54.8)"
%endif
DOS_PRGMCB  equ DOS_ENVSEG+DOS_ENVP ; para 9     : the program's MCB ('Z')
DOS_PSPP    equ DOS_PRGMCB+1        ; para 10    : the PSP
DOS_IMGP    equ DOS_PSPP+16         ; para 26    : the image, at PSP:0100

; --- state -------------------------------------------------------------------
; THE ARGUMENTS BUFFER IS 128 AND THE FIELD TAKES 127 OF IT (SPEC.md 96.19).
; That is DOS's limit rather than a choice: PSP:0080 is a length byte, then
; the text, then an 0Dh, all inside 128 bytes. A field that let a 128th
; character in would be one the user could type into and not have obeyed.
DOS_TRACEN  equ 512                 ; DOSTRACE ring entries (power of two).
                                    ; **512 AGAIN, because the buffers are a
                                    ; PART now** (SPEC.md 96.29.1). It was cut
                                    ; to 256 when the trace build measured
                                    ; 61,437 of APP_MAX_SIZE's 61,440 - three
                                    ; bytes - and §96.26's cable networking
                                    ; stopped it assembling; the next cut
                                    ; after that was DOS_TRDUMPN, 256 to 240.
                                    ; Neither was a design decision, both were
                                    ; a ceiling, and the ceiling is gone: a
                                    ; part is outside the 60KB an image and
                                    ; its bss share, so this is sized by the
                                    ; failure it exists for again. That one
                                    ; makes 169 calls, so 256 was never the
                                    ; binding number - but 512 is what lets a
                                    ; run be read from its FIRST call with
                                    ; slack, and slack in a ring is the whole
                                    ; point of one
DOS_TRACE_SZ equ 32                 ; ...bytes an entry, NAMED so that the host
                                    ; side derives it rather than transcribing
                                    ; it (docs/DOS-DEBUGGING.md): every reader
                                    ; of this ring lives outside the guest, and
                                    ; a layout known in two places is one that
                                    ; decodes plausible nonsense the day it
                                    ; moves. It has moved twice already, 12 to
                                    ; 16 to 32
DOS_TRDUMPN equ 64                  ; ...and how many of them TRACE.LOG holds,
                                    ; which is separate because the RING is
                                    ; read live off a debugger and the FILE is
                                    ; what the field posts. **256 AGAIN, and
                                    ; for DOS_TRACEN's reason**: it was cut to
                                    ; 240 as the second stopgap under the same
                                    ; ceiling, its own comment saying "THE
                                    ; REAL FIX IS A PART" - which this is.
                                    ; Sixteen lines is not a quantity anybody
                                    ; chose; 256 is the ring, the reference
                                    ; tracer's own cap and this, all covering
                                    ; the same span, which is what makes two
                                    ; traces diff line for line
; --- THE PART'S OWN LAYOUT (SPEC.md 96.29.1) --------------------------------
; Two buffers in one OP_ZERO part, because a part is a claim and MEM_OWNER_MAX
; is eight of them: one 35KB row beats two rows for no gain. The offsets are
; the part's, not the package's, and everything that reads them does it
; through ES.
;
; **THE RENDERED DUMP GOES FIRST, AND THAT IS A CORRECTNESS REQUIREMENT AND
; NOT A LAYOUT TASTE.** [dos_tracei] holds the entry a result belongs to and
; spells "the call was filtered" as ZERO (it is in the bss block below, under
; that comment) - a sentinel that cost nothing while the ring was bss, because
; a bss offset is os88_image_end plus a positive displacement and can never be
; 0. In a part it can: with the ring at offset 0, ENTRY 0's index IS 0, so
; dos_tr_result reads "filtered" for it and its result is never filled. The
; symptom is a trace whose first call - and every 512th after a wrap - reads
; `axout=FFFF`, which the reader correctly renders as "this call never
; returned", about a call that returned perfectly well. Putting the dump's
; 18,432 bytes in front of the ring restores what bss gave for free, for zero
; bytes and no code.
DOS_TRD_OFF equ 0                           ; the rendered dump...
DOS_TRB_OFF equ DOS_TRDUMPN * 72            ; ...and THEN the ring
DOS_TR33_OFF equ DOS_TRB_OFF + DOS_TRACEN * DOS_TRACE_SZ
DOS_TR33_SZ equ 8                           ; count, then BX, CX and DX AS THEY
                                            ; WERE at the last call of that
                                            ; function (SPEC.md 96.10.3.1). A
                                            ; count alone answers `which` and
                                            ; the design questions are all
                                            ; `with what`: 0Ch's event MASK
                                            ; decides whether a callback is
                                            ; eligible at all, and 0Ah's BX
                                            ; picks the software cursor over
                                            ; the hardware one
DOS_TR33_CB  equ DOS_TR33_N * DOS_TR33_SZ   ; ...and ONE MORE SLOT, counting
                                            ; the callbacks DOS actually
                                            ; MADE. Without it `the program
                                            ; asked for events and did nothing
                                            ; further` has two readings - we
                                            ; never called, or we called and it
                                            ; ignored us - and they are
                                            ; opposite defects
DOS_TR33_SEQ equ DOS_TR33_CB + DOS_TR33_SZ
DOS_TR33_SEQN equ 96                        ; ...and THE ORDER, one byte a call
                                            ; (SPEC.md 96.10.3.2). A histogram
                                            ; says a program asked for four
                                            ; functions; it cannot say that it
                                            ; asked for them and then STOPPED,
                                            ; which is the whole shape of the
                                            ; failure here - Microsoft Works
                                            ; parts from a real driver's
                                            ; sequence somewhere after 0Ch and
                                            ; a count per function cannot name
                                            ; where. FIRST 96 AND THEN NOTHING:
                                            ; what is wanted is the INIT, and a
                                            ; ring would spend it on the poll
                                            ; loop that follows
DOS_TR33_BY  equ DOS_TR33_SEQ + DOS_TR33_SEQN + 2
                                            ; ...and the mouse histogram after
                                            ; it, IN THE PART and not in bss
                                            ; (SPEC.md 96.10.3): `dos_int33` is
                                            ; CORE, so a host cell is one it
                                            ; may not name at all (96.44.2
                                            ; rule 2) and a conditional DBSS
                                            ; row is refused by rule 1. What
                                            ; the core owns is the SEGMENT,
                                            ; two unconditional bytes
DOS_TRACE_BY equ DOS_TR33_OFF + DOS_TR33_BY + DOS_TRNM_N * 15 + 96
DOS_TRACE_KB equ (DOS_TRACE_BY + 1023) / 1024

DOS_TRNM_N  equ 48                  ; ...and names it keeps. It was 12, on the
                                    ; reasoning that the failure under
                                    ; investigation made exactly ONE open in a
                                    ; whole session - true then, and the next
                                    ; failure filled all twelve with `CON`
                                    ; before the interesting name arrived
                                    ; (SPEC.md 96.11.7 is why a program opens
                                    ; CON eight times). DOSTRACE-only bss, so
                                    ; the cost is a diagnostic build's alone
DOS_TR33_N  equ 32                  ; INT 33h functions counted (SPEC.md
                                    ; 96.10.3). 32 covers every function a
                                    ; real-mode driver published up to
                                    ; Microsoft 6.x bar the 0x20s, and the ones
                                    ; above are the ones no 1987 program calls;
                                    ; AX above this simply lands in the last
                                    ; bucket, which is a reading too and not a
                                    ; wild store
DOS_ARGSZ   equ 128
DOS_ARGMAX  equ 127                 ; ...what LN_MAX gets: 126 characters + NUL
DOS_PBUF    equ 80                  ; the program's own path for the environment

; --- THE WINDOW'S SETTLED SHAPE (SPEC.md 96.32) -----------------------------
; **80 COLUMNS DECIDES THE WINDOW AND NOT THE OTHER WAY ROUND.** A cell is 8px
; (SPEC.md 6) so 80 of them is 640 pixels of CONTENT - and VGA and CGA are
; both 640 pixels WIDE. The only way a window has 640 of content is with no
; side borders, and SPEC.md 11.95.3 removes them in exactly one case: a frame
; that starts at its display's first column and reaches its last. So this
; window SPANS the screen on VGA and CGA, because there is no other way to
; hold the thing it is for - and on a Hercules, 720 wide, it is SNUG instead:
; two borders round 640 of content (DOS_HERCW, SPEC.md 96.32.5).
;
; It is worth having twice over: a spanning frame's content origin is W_X
; itself rather than W_X+1 (11.95.2), so the console's rows start on column 0
; - 8-ALIGNED - and reach font_run's single-store fast path (6.1) instead of
; the erase-and-letter pair.
DOS_CONCOLS equ 80                  ; the console's columns, which is the
                                    ; requirement everything else follows from
DOS_CONROWS equ 25                  ; ...and the rows it WANTS. What it GETS is
                                    ; computed from the content box at paint
                                    ; time (dos_con_geom), because CGA cannot
                                    ; give 25 and a drag across a display seam
                                    ; re-asks the question
DOS_CONW    equ DOS_CONCOLS * 8     ; 640
DOS_HERCW   equ DOS_CONW + 2        ; 642: the Hercules frame (SPEC.md
                                    ; 96.32.5) - 80 cells and the two side
                                    ; borders a frame that does NOT span keeps
DOS_HERCX   equ (720 - DOS_HERCW) / 2   ; 39, so it is centred on 720 AND its
                                    ; content origin, x+1 = 40, is on the
                                    ; 8-pixel grid (11.94) with no snap needed
%if (DOS_HERCX + 1) % 8
%error "DOS_HERCX + 1 must be a multiple of 8 (SPEC.md 11.94, 96.32.5)"
%endif
DOS_BARH    equ 20                  ; the top bar: one 14px row of controls
                                    ; with 3 above and 3 below (96.32.1)
DOS_BARY    equ 3                   ; ...the controls' own top, from content
DOS_BARCH   equ 14                  ; ...and their height, buttons and box
                                    ; alike, so the bar reads as one row
DOS_FRAMEH  equ DOS_CONROWS * 8 + DOS_BARH + TITLE_H + 1
                                    ; = 239, and the ONLY height this window
                                    ; ever asks for: 25 rows, the bar, and the
                                    ; chrome. It fits the desktop band on every
                                    ; adapter but CGA, so on VGA the window is
                                    ; 239 of an available 436 and leaves
                                    ; desktop under it - which is what a window
                                    ; that knows its own size should do

; The arguments row, measured DOWN from the content origin. The label sits on
; DOS_LBLY and the box under it, so a 126-tall window has both inside it on a
; 640x200 CGA - which is the geometry that binds (SPEC.md 39).
; THE ENVIRONMENT PAGE (SPEC.md 96.20). Four rows because four fits a 640x200
; CGA under the status lines with the buttons still on the glass, and because
; the DOS programs DOS exists for want one or two: BLASTER= is written
; for them and a SOUND= or an MTCPCFG= is the whole of what most of the rest
; ask for. The width is what a `NAME=C:\LONGISH\PATH` needs.
DOS_ENVN    equ 4                   ; rows
DOS_ENVW    equ 48                  ; characters in one, not counting the NUL
DOS_ENVBUF  equ DOS_ENVW + 1

; --- THE SHORTCUT FILE (SPEC.md 96.21) ---------------------------------------
; A valid subset of Microsoft's Shell Link format, because the extension is
; instantly recognisable and every field we need already has a home in it:
;
;   WORKING_DIR              the folder, `\BIN`
;   RELATIVE_PATH            the program, `.\DOSARGS.COM` - valid Windows
;                            spelling AND parseable by us
;   COMMAND_LINE_ARGUMENTS   the tail
;   an ExtraData block       the environment, under a signature of our own.
;                            ExtraData is specified as extensible and unknown
;                            signatures are to be SKIPPED, so this is a legal
;                            use of the mechanism rather than a squat
;
; WE READ ONLY OUR OWN. A Windows-authored link leads with a LinkTargetIDList
; - an arbitrary shell ID list - and a LinkInfo with volume IDs, and parsing
; that from hostile floppy input is real work for no benefit: a 64-bit Windows
; cannot run a DOS program anyway, so the value of the format here is that it
; is RECOGNISED, not that it round-trips. A foreign link is refused by name.
LNK_HDR     equ 76                  ; the fixed header, 0x4C
LNK_SHOW    equ 0x3C                ; ShowCommand, a dword in that header...
LNK_SW_HEAD equ 7                   ; ...and SW_SHOWMINNOACTIVE: no window - DOS
                                    ; runs the program headless (SPEC.md 96.44.20)
DOS_HTXT    equ 25                  ; the headless host's line: a toast's 24 + NUL
LNK_F_WDIR  equ 0x10                ; LinkFlags: HasWorkingDir...
LNK_F_RELP  equ 0x08                ; ...HasRelativePath...
LNK_F_ARGS  equ 0x20                ; ...HasArguments. Deliberately NOT
                                    ; HasLinkTargetIDList or HasLinkInfo: both
                                    ; are optional, and both are the parts we
                                    ; decline to write or read
LNK_EXTSIG  equ 0xA0088088          ; OUR ExtraData block: 'os8088' shaped, in
                                    ; the range MS leaves to other producers
LNK_EXTSIG2 equ 0xA0088089          ; ...and a SECOND one, the memory settings
                                    ; (SPEC.md 96.25.2). A second BLOCK and not
                                    ; two more fields on the first, because the
                                    ; first ends in a bare NUL after a variable
                                    ; number of rows - so anything appended
                                    ; sits at an offset that depends on what
                                    ; the user typed. ExtraData is a sequence
                                    ; whose consumers SKIP signatures they do
                                    ; not know, so a second block is what the
                                    ; mechanism is for, and its fields are at a
                                    ; fixed offset inside it. An older link
                                    ; simply has not got one
LNK_EXT2SZ  equ 14                  ; size(4) + signature(4) + memkb(2) + the
                                    ; arm + the cache + the BOXES + one of
                                    ; padding - what this build WRITES
LNK_EXT2MIN equ 12                  ; ...and the shortest it READS: a link
                                    ; written before the boxes were carried
                                    ; (SPEC.md 96.25.2.1) has no byte 12, and
                                    ; its boxes keep their defaults
LNK_BX_NOHDD equ 0x01               ; byte 12's bits, each set = the box moved
LNK_BX_NONET equ 0x02               ; OFF its default, so a zero byte is the
LNK_BX_NOMOU equ 0x04               ; page as a double click opens it: Hard
                                    ; drives and Network ticked, Disable the
                                    ; mouse NOT - and NOMOU is that box ticked
LNK_BX_NOHMA equ 0x08               ; ...and kern_mega's `Load DOS in the HMA`
                                    ; (SPEC.md 96.40.9.2), ticked by default,
                                    ; so set = UNTICKED. A build with no such
                                    ; row ignores the bit, and drops it on a
                                    ; save
LNK_BX_SAVE equ 0x10                ; ...and its `Save the session to disk`
                                    ; (96.40.9.3), UNticked by default, so set
                                    ; = TICKED
LNK_BX_WIN  equ 0x20                ; ...and `Run in a window` (96.32.2.4),
                                    ; UNticked by default, so set = TICKED and
                                    ; an older link reads the default
LNK_MAX     equ 512                 ; what one may be, read or written

; --- THE PAGES (SPEC.md 96.32.2) ---------------------------------------------
; MAIN is the console view and is not part of the setup area at all: `<` and
; `>` cycle the SETUP pages and `Return` is what leaves them, so the page the
; window opens on is never one of the two the arrows reach. That is the whole
; difference from the old arrangement, where one button cycled main ->
; environment -> memory -> main and the console view was a stop on the way
; round.
DOS_PAGE_MAIN equ 0                 ; the top bar and the console band
DOS_PAGE_SET  equ 1                 ; ...arguments, one env row, and the memory
                                    ; settings on the other half (96.25)
                                    ; **AND THERE IS NO SECOND SETUP PAGE**
                                    ; (SPEC.md 96.32.2.1). The four NAME=VALUE
                                    ; rows had a page of their own because the
                                    ; window was 288px wide; at 80 columns
                                    ; they fit under the first, in the same
                                    ; left column and at the same width - so
                                    ; the page, its title, the `<` and `>`
                                    ; that cycled to it and the range they
                                    ; cycled over are all gone

; --- THE TOP BAR'S THREE CONTROLS (SPEC.md 96.32.1) --------------------------
; [ the path box .............. ] [ Environment ]        [ Run ]
;
; The BOX is the flexible one: the two buttons and the four gaps are fixed, so
; what is left over is the box's, which is 57 cells on a 640 screen and 67 on
; Hercules. That is why one helper computes all three - computing them apart
; would price the buttons twice and get the box wrong the day a label changes.
DOS_RUNW    equ 48                  ; 'Run' is 3 cells = 24px, so 12 each side
DOS_BARGAP  equ 8                   ; ...and the pad at each end and between

; --- THE SETUP AREA'S FURNITURE (SPEC.md 96.32.2) ----------------------------
; A title at the top, the body, and one row of controls along the bottom:
;
;   Setup
;   ... the page's own body, in two halves ...
;   [<] [>]                          [Save Shortcut] [Return]
;
; The furniture is drawn by ONE routine for whichever page is up, which is why
; neither page painter ends in a button any more: a control that is on every
; page and drawn by every page is a control that moves the day one painter is
; edited and the other is not.
DOS_SETPAD  equ 8                   ; the pad at a column's edge
DOS_TITY    equ 6                   ; the title's baseline, from content top
DOS_CMDX    equ 64                  ; **THE COMMAND BOX, ON THE TITLE ROW**
                                    ; (SPEC.md 96.32.2.2): the page's name is
                                    ; five cells = 40px past DOS_SETPAD, and
                                    ; this is that plus TWO cells of air - so
                                    ; the name and the box read as two things
                                    ; and not as a label with a field glued
                                    ; to it
DOS_CMDY    equ 3                   ; ...and its top, which BRACKETS the name:
                                    ; the title's 8-pixel text runs 6..13 and
                                    ; a DOS_FLDH box here runs 3..16, so the
                                    ; two sit on one line with three pixels
                                    ; either side and DOS_BODYY's 24 is clear
DOS_BODYY   equ 24                  ; ...and where a page's own body starts
DOS_FURNB   equ 4                   ; the bottom row's pad below itself
DOS_RETW    equ 64                  ; 'Return' is 6 cells = 48px
; ...and the Setup page's own two rows, from the body top
DOS_SLBL1   equ 0                   ; 'Arguments:' ...
DOS_SFLD1   equ 12                  ; ...and its box
DOS_SLBL2   equ 34                  ; 'Environment:' ...
DOS_SFLD2   equ 46                  ; ...and ITS box, which is Environment's
                                    ; own first row and not a copy (96.32.2)

; The status lines land at content+10, +22 and +36 (dos_paint marches DX down
; by 12 then 14), so the arguments row starts below THAT rather than at a
; number chosen by eye - and the whole lot has to finish inside a content box
; ~110 rows tall, which is what a 126px window leaves once the title bar has
; its 16.
DOS_LBLY    equ 52                  ; the label's baseline
DOS_FLDY    equ 64                  ; the box's top...
DOS_FLDH    equ 13                  ; ...and its height, one 8px cell + frame
DOS_FLDW    equ 256                 ; ...and its width
DOS_EROWH   equ 16                  ; ...and one row's pitch, which with four
                                    ; rows of DOS_FLDH ends at 85 - clear of
                                    ; the button row below, which a pitch of
                                    ; 18 was not
DOS_BTNW    equ 104                 ; the page buttons. 'Environment' is 11
DOS_BTNH    equ 14                  ; cells = 88px, and a label that touches
DOS_BTNY    equ 90                  ; its own frame reads as struck through
DOS_SAVW    equ 112                 ; 'Save Shortcut' is 13 cells = 104px

; THE MEMORY PAGE (SPEC.md 96.25), laid out in the same content box as the
; other two - three read-only lines, the field, the check box, and the page
; button already at DOS_BTNY. The two figures are what the user is choosing
; between, so they are on the glass rather than in the documentation.
; The memory block's rows are measured from the BLOCK's own top-left now
; (dos_mem_org), not from the content edge - it is the Setup page's right half
; rather than a page (SPEC.md 96.32.2), so a figure placed against the window
; would sit under the arguments box instead of beside it.
DOS_MEMY    equ 0                   ; the heading IS the block's first row
DOS_MARNY   equ 0                   ; **THE ARENA, AT THE TOP AND LIVE**
                                    ; (SPEC.md 96.36.3): one figure that moves
                                    ; as the options under it are worked,
                                    ; where there used to be two that stood
                                    ; for two of the three arms. IT IS ALSO
                                    ; THE COLUMN'S HEADING - 'Memory for the
                                    ; program: ~' - because a heading of its
                                    ; own is a whole ROW and this block has
                                    ; 118 pixels (96.36.2)
DOS_MRADY   equ 28                  ; the two arms' rows (SPEC.md 96.36) - and
                                    ; they start below the DIAL now, which is
                                    ; 96.36.6.3: `Disk cache` belongs to
                                    ; NEITHER arm and sat under both, where it
                                    ; read as a child of the second one
DOS_MRADP   equ 58                  ; ...and the pitch, WHICH IS A SUBSECTION.
                                    ; The group's rows are the two headings
                                    ; and everything between them belongs to
                                    ; the arm above, so a press anywhere in a
                                    ; subsection that no control of its own
                                    ; claims picks that arm - which is why the
                                    ; boxes are hit-tested FIRST (96.36.4).
                                    ; os88ui_rad centres a ring in the ROW and
                                    ; not in the pitch (13.17.5), or the two
                                    ; headings would land on the controls they
                                    ; own
DOS_MSUBX   equ 12                  ; ...and the subsections' indent, which is
                                    ; OS88UI_RDBOX so a sub-control's own box
                                    ; lines up under its arm's ring. A literal
                                    ; for DOS_MRADSZ's reason: os88ui.inc is
                                    ; included at the END of this file
%ifndef DOS_MEGA
DOS_MHDDY   equ 46                  ; [x] Hard drives (Up to NNK) - arm 0's
DOS_MNETY   equ 59                  ; [x] Network (Up to NNK)
DOS_MFLDY   equ 72                  ; Limit: [____] K
%else
DOS_MSAVY   equ 38                  ; [ ] Save the session to disk - the
                                    ; park's own option, under the line that
                                    ; says what will happen (KERN-MEGA-PLAN
                                    ; 14.43, SPEC.md 96.40.9.3). kern_mega's
                                    ; page has no radio, so its rows take the
                                    ; slack the radio's gaps left: these three
                                    ; move down 5 and the block still ends at
                                    ; the mouse row's 115, inside CGA's 118
DOS_MHDDY   equ 51
DOS_MNETY   equ 64
DOS_MFLDY   equ 77                  ; ...whose 12 rows end at 88, under HMAY
%endif
DOS_MFLDW   equ 48                  ; ...and the box's width. os88line_cols
DOS_MFLDX   equ 56                  ; resolves 48 to FIVE columns against this
DOS_MFLDKX  equ 4                   ; block's own 8-aligned origin - the four
                                    ; digits DOS_MEMMAX allows plus the cell
                                    ; the caret sits in past the last of them.
                                    ; It was 64, which is seven, so two columns
                                    ; could never be reached at all
                                    ;
                                    ; ...and DOS_MFLDKX is the gap to the `K`
                                    ; after it (SPEC.md 96.36.10.1). A unit on
                                    ; the glass, because the field takes a
                                    ; BARE number and `300` is three plausible
                                    ; quantities - KB, paragraphs or a
                                    ; percentage. 4px keeps the letter's own
                                    ; cell 8-aligned, which is font_run's fast
                                    ; path (SPEC.md 6.1)
%ifdef DOS_MEGA
DOS_MHMAY   equ 91                  ; [x] Load DOS in the HMA - kern_mega's,
                                    ; on the row its absent radio's second
                                    ; heading would have taken (96.40.9.2)
%endif
DOS_MMOUY   equ 104                 ; [ ] Disable the mouse     - arm 1's, AND
                                    ; the greyed arm's REASON, which takes the
                                    ; same row: an arm that cannot be picked
                                    ; is not offering its option either
DOS_MCACY   equ 12                  ; Disk cache: [Auto       v] - AT THE TOP,
                                    ; under the figure and ABOVE the first arm
                                    ; (SPEC.md 96.36.6.3). It floats free of
                                    ; both because it belongs to both, and at
                                    ; the BOTTOM that read as arm 1's third
                                    ; option - a control's meaning is where it
                                    ; sits, whatever the indent says
DOS_MRADB   equ 117                 ; ...and the arms' group now runs to HERE
                                    ; instead of stopping one line above the
                                    ; dial: the dial is no longer below it, so
                                    ; there is nothing left to stop short of.
                                    ; One line past the mouse row's own bottom
                                    ; (104 + 12), which keeps arm 1's band
                                    ; MIDDLE - y1 + pitch + pitch/2 = 115, how
                                    ; every kd* row clicks that arm - inside
                                    ; the rect
DOS_MCACX   equ 96                  ; ...its box, past the label - and PAST
                                    ; ALL OF IT. `Disk cache:` is eleven
                                    ; cells = 88px and this was 80, so the box
                                    ; began one cell INSIDE the label and drew
                                    ; over the colon: the field reported it as
                                    ; `no space between Disk cache and the
                                    ; dropdown` and the missing punctuation is
                                    ; the same byte. 96 is the label plus one
                                    ; cell of air
DOS_MCACW   equ 96                  ; ...and how wide: 'Off (SLOW!)' plus the
DOS_MCACH   equ 12                  ; arrow cell, and a row tall
                                    ;
                                    ; **CUT AGAINST CGA** (96.36.2), the only
                                    ; adapter that clamps this window: the
                                    ; furniture row starts 118 pixels below
                                    ; this block's own top there and 178 on
                                    ; the other two, so the cache box's last
                                    ; line is 116 and the margin is TWO. Nine
                                    ; rows do not fit 118 and eight do, which
                                    ; is why the arena row is the heading;
                                    ; tests/dosram.py is what says so on a
                                    ; machine rather than here
DOS_MEMBUF  equ 8                   ; the field's text: 5 digits + NUL, and
                                    ; room for the caret to sit past the end
DOS_MEMMAX  equ 4                   ; ...what LN_MAX gets. FOUR: 640 is three
                                    ; digits and 9999 is already past every
                                    ; address an 8086 has, so the fifth column
                                    ; could only ever hold a number the box
                                    ; would clamp anyway
DOS_MRADSZ  equ 18                  ; os88ui.inc's OS88UI_RD_SIZE, written here
                                    ; and CHECKED against it after the include
                                    ; - DOS_LNSZ's rule exactly, and for the
                                    ; same reason: the bss table is above and
                                    ; the record's owner is below
DOS_MRADSEL equ 12                  ; ...and OS88UI_RD_SEL inside it, so that
                                    ; [dos_keepc] IS the control's own pick and
                                    ; there is no second copy to keep in step.
                                    ; The field is a WORD and the pick is 0..2,
                                    ; so its low byte is the whole of it on a
                                    ; little-endian machine (SPEC.md 96.36)

; --- the two arms, which are what [dos_keepc] holds (SPEC.md 96.36) ---------
; **IT WAS THREE AND THE MIDDLE ONE WAS NOT AN ARM** (96.36.5): `Keep the disk
; cache` and `Take the disk cache too` differ in the cache and in nothing
; else, so they were one mode with a dial set two ways - and a dial with two
; positions cannot say 18 KB. The cache is its own control now and the arms
; are what they always were: inside the OS, or not.
DOS_MEM_IN    equ 0                 ; inside the OS - the default, and what a
                                    ; double click gets
DOS_MEM_WHOLE equ 1                 ; ...shut it down and take the machine
DOS_W_ASK   equ 0                   ; [dos_wok]: the destructive arm's own
DOS_W_YES   equ 1                   ; confirmation, per LAUNCH (SPEC.md 96.42).
DOS_W_NO    equ 2                   ; THREE states and not two: a refusal has to
                                    ; be REMEMBERED, or the next wake asks again
                                    ; (docs/plans/KERN-DOS-PLAN.md). GREYED
                                    ; until that is built, by dos_mem_whole
DOS_MEM_N     equ 2                 ; how many arms, for OS88UI_RD_N

; --- the disk cache dial, which is [dos_cache] (SPEC.md 96.36.6) ------------
; ONE LIST, BOTH ARMS. It was two, and the reason it was two has been
; WITHDRAWN rather than outgrown: *"inside the OS the cache is the KERNEL's one
; claim, taken at a mount and either standing or shed, so there are two
; positions and saying otherwise would be a control that rounds."* That was
; true of the kernel it was written against and SPEC.md 18.95.8 made it false -
; `OSAPI_DSK_CACHE` takes a WIDTH, so arm 0 can ask for 18 KB and be given
; exactly 18 KB. An omission whose ground is a claim about the kernel is a
; claim to re-read when the kernel changes (SPEC.md 24.5.5's rule, one
; subsystem along), and this one came back.
;
; What went with it is the whole per-arm apparatus - `[dos_cachei]`,
; `[dos_mdrwas]`, the swap, the second item table and the clamp that guarded
; two different lengths. With one list there is no pick to park.
DOS_CA_AUTO   equ 0                 ; what DOS picks - 9 KB on arm 1 today
DOS_CA_32     equ 1                 ; 7 runs
DOS_CA_18     equ 2                 ; 4 runs
DOS_CA_9      equ 3                 ; 2 runs
DOS_CA_OFF    equ 4                 ; 0 - and the label says SLOW!
DOS_CA_AUTORUN equ 2                ; ...and what Auto is worth in RUNS, which
                                    ; is kerndos/kdshim.inc's KD_RAH_KEEP.
                                    ; MIRRORED and gated in the KD_BACKEND
                                    ; build, where both names exist - the
                                    ; window half cannot see kern_dos's own
                                    ; constants and this is the one figure it
                                    ; needs from them.
                                    ; ARM 0 DOES NOT USE IT: there, Auto means
                                    ; DSK_RAH_AUTO - no ceiling of ours, the
                                    ; kernel solves its own width from the
                                    ; machine (18.95.5) - which is what the
                                    ; label says and what a box that has been
                                    ; asked for nothing should do
DOS_CA_N      equ 5                 ; how many rows, for OS88UI_DR_N
DOS_MDRSZ   equ 26                  ; os88ui.inc's OS88UI_DR_SIZE, mirrored
                                    ; here for DOS_MRADSZ's reason and checked
                                    ; against it after the include
DOS_MDRSEL  equ 12                  ; ...and OS88UI_DR_SEL inside it, so that
                                    ; [dos_cache] IS the control's own pick
DOS_MCKSZ   equ 12                  ; ...and OS88UI_CK_SIZE, for the three
                                    ; check boxes. Mirrored and gated like the
                                    ; other two

; os88line.inc is included at the END of this file (its own rule: the header
; and the icon block are at fixed offsets), and the bss table above needs its
; block size BEFORE that. So the size is written here and CHECKED against the
; real one immediately after the include - a mirrored constant with a gate on
; it, which is what this tree does everywhere two files must agree.
DOS_LNSZ    equ 20

; --- the built-in commands' sizes (apps/dos/dosh.inc, SPEC.md 96.30) --------
; HERE AND NOT IN dosh.inc: the DBSS table below sizes that file's buffers and
; `%assign` cannot forward-reference, so the numbers come before both.
DSH_LINE    equ 128                 ; DOS's own command tail is a counted byte,
                                    ; so 127 is the longest there has ever been
DSH_ARG     equ 64                  ; one argument - longer than any 8.3 path
                                    ; this DOS can walk, so a truncation here
                                    ; is a path that was going to be refused
DSH_PAT     equ 11                  ; a padded 8.3 name, the form a match is
                                    ; decided in
DSH_CPKB    equ 8                   ; the buffer COPY and TYPE share, out of
                                    ; the DOS ARENA and not the heap (SPEC.md
                                    ; 96.30.6): 8KB asked for, and the request
                                    ; halves down to ONE CLUSTER - never to a
                                    ; flat 1KB, which on a 2KB- or 4KB-cluster
                                    ; volume is a capacity OSAPI_FILE_READ_AT
                                    ; refuses outright (96.30.7). TYPE had a
                                    ; 128-byte DSH_BUF of its own and could
                                    ; therefore not read a file at all

DST_IDLE    equ 0                   ; launched with no document (wave 7's prompt)
DST_READY   equ 1                   ; a program is named and not yet run
DST_RAN     equ 2                   ; it ran; [dos_exit] is its code
DST_ERR     equ 3                   ; it did not; [dos_err] says why
DST_CPWAIT  equ 5                   ; ...and it is waiting for the heap to be
                                    ; packed (SPEC.md 96.35). A posted
                                    ; OSAPI_MEM_COMPACT's post runs at ui_task's
                                    ; step 0, and the EVT_WAKE it sends lands
                                    ; here - so this is one more state and not
                                    ; a lifecycle, which is why a stale wake
                                    ; still finds the state advanced
DST_HELD    equ 4                   ; ...and launched with Shift or Alt held:
                                    ; named and staged, and it waits for Run
                                    ; or Enter (SPEC.md 96.21.9). RAN, ERR and
                                    ; HELD are CONTIGUOUS on purpose: they are
                                    ; the states Enter may (re)start, and the
                                    ; key handler tests them as one range

; --- INT 33h's VIRTUAL SCREEN (SPEC.md 96.10), which is not the machine's ----
; A mouse driver's coordinates are a 640x200 grid whatever the adapter is
; doing, and BOTH hosts hand `dos_mou_read` that grid: the windowed DOS scales
; the kernel's pointer into it in `dos_hk_mouse` (dividing by the REAL screen,
; which is what `[dos_vw]`/`[dos_vh]` hold there - 640x480 on a VGA), and
; `kern_dos` accumulates into it directly. So the window SPEC.md 96.10.7 maps
; into is cut from these and never from `[dos_vw]`/`[dos_vh]`.
;
; **THAT IS A DEFECT THIS FILE ALREADY HAD ONCE, MEASURED**: cutting the map
; from `[dos_vw]`/`[dos_vh]` reads correctly on `kern_dos`, where they are 640
; and 200, and scales y a second time in the windowed DOS - `MOURANGE.COM`
; asked for 0..199, stood the pointer at y=60 and was answered 25.
M33_VW      equ 640
M33_VH      equ 200

; --- why it did not ----------------------------------------------------------
DER_GOTO    equ 0
DER_MEM     equ 1
DER_READ    equ 2
DER_BIG     equ 3
DER_FSX     equ 4
DER_EXE     equ 5
DER_BADEXE  equ 6
DER_FIT     equ 7                   ; ...and THIS program will not fit in the
                                    ; arena we got, which is a different
                                    ; sentence from not getting one (96.14.3)
DER_HAND    equ 8                   ; ARM 3 WAS REFUSED, AND IT IS NEVER ABOUT
                                    ; MEMORY (SPEC.md 96.40.6).
                                    ; `osapi_dos_handoff_x` refuses exactly
                                    ; twice: a null segment, and a post that is
                                    ; already standing. This said DER_MEM, so a
                                    ; machine with 425K free reported "Not
                                    ; enough memory" about the one arm that
                                    ; does no sizing at all - it tears the
                                    ; kernel out and hands the program ~600K,
                                    ; so anything DOS could launch will launch
%ifdef DVW_SPLIT                    ; kern_big's and kern_emu's only, so it
DER_DVWP    equ 9                   ; shares DER_RESET's number: the windowed
%endif                              ; part could not be had (SPEC.md 96.54.9)
DER_RESET   equ 9                   ; kern_mega (DOS_MEGA's table only): the
                                    ; session came back from a warm reset
                                    ; (KERN-MEGA-DOS-PLAN 10.3.1, W7) and the
                                    ; program never returned a code. The
                                    ; kernel's mega_wrprobe stages it as
                                    ; MPK_RESETCODE
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; -----------------------------------------------------------------------------
; dos_entry - package entry (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; ARG_FILE is READ-AND-CLEAR and its name lives in the KERNEL segment, so it
; is copied out through ES before anything else is called (SPEC.md 54.5).
; -----------------------------------------------------------------------------
%ifdef DOS_MEGA
    db 'KMEG'                       ; kern_mega's mark: kernel/megados.inc's
                                    ; mega_pkgok refuses a 'DOS' package whose
                                    ; entry is not preceded by it (12.1 item 4)
%endif
dos_entry:
    push ax
    push cx
    push dx
    push si
    push di
    call dos_be_bind            ; **THE BACK END, BEFORE ANY DOOR** (SPEC.md
                                ; 96.44.1): the core stores a DBE_* ordinal and
                                ; dos_be_go resolves it here, so a host that
                                ; did not bind would jump through a zeroed bss
    call dos_hk_bind            ; ...and the six hooks beside them (96.44.3)
%ifdef DOSKPART
    call dos_pkgwhere           ; **NOW OR NEVER** - SPEC.md 96.40, and the
%endif                          ; routine's own header says why
%ifdef DOSTRACE
    ; **op_load FIRST, BEFORE ANYTHING TOUCHES SI** (os88parts.inc rule 1):
    ; SI arrives holding an offset into the KERNEL's segment at the name of
    ; the file we came out of, and the loader reuses that buffer on the next
    ; launch - so nothing later can recover it. The pushes below would not
    ; lose it, but OSAPI_WM_CREATE would.
    ;
    ; The part is OP_OPT, so a refusal is survivable and [dos_trseg] stays 0:
    ; the trace writes nothing and the program runs. That is the right answer
    ; for an instrument on a machine it does not fit on, and it is why there
    ; is no `jc` here.
    ;
    ; **AND IT IS INSIDE THE PUSHES**, which is not in tension with rule 1:
    ; `push` does not change what it pushes, so SI still holds the kernel's
    ; pointer here - while op_load's documented clobber list is AX, BX, CX,
    ; DX, SI, DI and ES, and every one of those except BX is a register this
    ; proc owes the kernel back.
    ;
    ; **AND ES IS BANKED, WHICH IS THE ONE THAT BIT.** ES arrives holding
    ; KERNEL_SEG (SPEC.md 20.1) and OSAPI_ARG_FILE below answers with an SI
    ; into THAT segment and does not reload it - it is documented as read
    ; through the ES a package proc was entered with (SPEC.md 54.5). Every
    ; OSAPI slot preserves ES, so the entry proc can rely on it across
    ; wm_create and about_set; op_load is OUR code and clobbers it. Without
    ; this pair the name copy below reads RDSUM.COM's 13 bytes out of the
    ; PART's segment, [dos_name] is junk, and the failure surfaces four
    ; routines later as dos_be_read refusing - "It could not be read.", about
    ; a file that is perfectly readable.
    push es
    call op_load
    xor al, al
    call op_seg                     ; AX = the part's segment, or 0
    mov [dos_trseg], ax
    mov [dos_m33seg], ax            ; ...and the CORE's own copy, which is what
                                    ; `dos_int33` may name (SPEC.md 96.10.3)
    mov al, OP_CARVE                ; **AND THE PART MAY MOVE** (SPEC.md
    call op_movable                 ; 96.29.1.2, NO-PINNING 2): both words are
                                    ; banked, and dos_trmove asks op_seg for
                                    ; them again after a move. CF=1 is the
                                    ; OP_OPT refusal (no carve) and costs
                                    ; nothing. dos_run pins it for the stretch
                                    ; a program runs and the dump is written
    pop es
%endif

%ifdef DOS_WTAIL
    mov si, dos_htpl                ; HEADLESS (SPEC.md 96.44.20): a one-line
%endif                              ; window of its own, and no menu, no
    WIN_ONLY                        ; buttons, no console (DOS-HEADLESS 4.4)
    mov si, dos_tpl                 ; THE TEMPLATE IS THE VGA/CGA SIZE and
    WIN_END                         ; dos_pref is the rest (SPEC.md 96.32).
    call OSAPI_WM_CREATE            ; dos_size is GONE: it computed 90% of the
    jc .out                         ; desktop band with a mul and a div, and
    mov [dos_win], bx               ; the requirement is a COUNT of columns
    WIN_ONLYF                       ; THE WINDOW'S FURNITURE
    mov si, dos_menus               ; ...and the menu bar gains a Program menu
    call OSAPI_MENU_SET             ; (SPEC.md 96.32.3)

    push dx
    mov ax, bx                      ; **AND THE BUTTONS' GESTURE** (SPEC.md
    mov bx, dos_btrec               ; 20.5.1.3): btninit installs all THREE
    mov word [bx+OS88UI_BT_PLACE], dos_bplace   ; (a HBSS record, so its
                                    ; place proc is stored by hand: the
                                    ; library lays the page out from where
                                    ; the window IS before every hit test,
                                    ; SPEC.md 20.5.1.3.5)
    mov si, dos_onup                ; slots - the press, the release and the
    mov di, dos_ondrag              ; tracking edge - and none of them is a
    mov dx, dos_click               ; template word except the press, which
    call os88ui_btninit             ; 20.5.1.3.3's cell now lets it write
    pop dx                          ; late. dos_click is OUR own click work
    mov bx, [dos_win]               ; and the library chains to it
    WIN_END


%ifdef DOS_MEGA                     ; (kern_mega: the proc keeps dos_dsel,
    OS88_REGION_MOVABLE dos_reloc   ; dos_hk_tty's DS, KERN-MEGA-PLAN 14.54)
%else
    OS88_REGION_MOVABLE             ; **AND OUR REGION MAY MOVE** (SPEC.md
%endif
                                    ; 96.35, 66.6.1), which is the half of the
                                    ; arena recovery that is ours. The sound
                                    ; driver sits ABOVE us on the heap, so the
                                    ; hole its unmount leaves is above us too -
                                    ; and a pinned region is a wall that hole
                                    ; can never merge past, whatever the
                                    ; compactor is asked for. Measured: the
                                    ; unmount happened, [dos_drvout] read 1,
                                    ; and both mem_avail and the what-if still
                                    ; answered 435KB against 449 on the same
                                    ; machine with no card - exactly the
                                    ; driver's image plus its ring, sitting in
                                    ; a hole at the top of the heap.
                                    ; We own no worker, so there is no restart
                                    ; point to declare with it, and the proc
                                    ; the macro carries is a `ret` for the
                                    ; ordinary reason: every word that names
                                    ; this region is the kernel's
                                    ;
                                    ; **IT WAS REFUSED IN THE PACKAGE THAT
                                    ; SHIPS, AND THAT IS OVER** (SPEC.md
                                    ; 66.6.1.2). We are PART 0 of a parted
                                    ; DOS.O88 (SPEC.md 96.40.3), reached by
                                    ; `OSAPI_PKG_REHOME` - and a re-homed
                                    ; package's region is the loader's CARVE,
                                    ; re-stamped to the instance SLOT. For a
                                    ; cycle its base sat a few paragraphs
                                    ; BELOW the segment we run in - 0x8FC0
                                    ; against I_SPTR's 0x8FE0, the 512 bytes
                                    ; of cluster-alignment slack op_claim
                                    ; leaves at the head (SPEC.md 20.12.2) -
                                    ; so `mem_find_own` matched nothing, the
                                    ; declaration was refused, the wall came
                                    ; straight back and the card cost 426KB
                                    ; against 440, the image and the ring to
                                    ; the byte.
                                    ;
                                    ; The re-home TRIMS the carve to us now
                                    ; (SPEC.md 20.12.10.5): the slack is heap
                                    ; again before this entry proc runs, the
                                    ; claim's base IS `cs`, and the fence
                                    ; reaches it as it reaches any package's
                                    ; region. This package reads 445 against
                                    ; 445, and `tests/dosarena.py` asserts the
                                    ; two machines agree AND that MC_RLOC is
                                    ; non-zero - because a refusal and a
                                    ; compaction that cannot reach the hole
                                    ; are the same number and different bugs

%ifdef DOS_WTAIL
    call dos_fld_def                ; the settings' defaults, on both hosts
%endif
    WIN_ONLY
    call dos_keeph                  ; **KEEPH FIRST, THEN THE PREFERENCE.** On
    mov si, dos_pref                ; a CGA the dock's strip is the difference
    call OSAPI_WM_PREFER            ; between 15 console rows and 17, and the
                                    ; clamp the preference is put through reads
                                    ; the ceiling KEEPH raises - so asking in
                                    ; the other order asks against the low one.
                                    ; Both preserve the flags, so the CF this
                                    ; proc owes the loader rides through
    call dos_fld_init               ; the arguments field (SPEC.md 96.19)
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    call dos_con_start              ; ...AND THE CONSOLE (SPEC.md 96.33), before
                                    ; the first paint can read a screen whose
                                    ; attribute is still a zeroed bss's
%endif
    WIN_END

    mov bx, [dos_win]               ; **NAMED, not whatever the last call left
    mov ax, dos_wake                ; in BX.** The slot takes BX = the window
    call OSAPI_WM_ONWAKE            ; and AX = the handler, and this used to
                                    ; ride on a leftover: the first proc above
                                    ; it that clobbered BX registered the
                                    ; handler against another window's slot and
                                    ; every wake this package posts went
                                    ; nowhere - Run, Enter-re-run and the
                                    ; console's own launch all stuck at
                                    ; DST_READY with nothing to see
    WIN_ONLY                        ; ...and the window's two: a close that
                                    ; stops a windowed run, and the About card
%ifdef DVW
    mov ax, HENT(dvw_onclose)       ; a running program is stopped by a close,
    call OSAPI_WM_ONCLOSE           ; not orphaned behind a hidden window
%endif                              ; (SPEC.md 96.54). BX is still the window:
                                    ; every slot preserves it
    mov si, dos_about
    call OSAPI_ABOUT_SET
    WIN_END

    mov byte [dos_pvol], 0xFF       ; **THE SENTINEL, BECAUSE BSS IS ZERO AND
                                    ; ZERO IS DRIVE A:** (SPEC.md 96.6.3).
                                    ; It was `[dos_fhome]` and the same trap
                                    ; one layer along: 0xFF means the machine
                                    ; is standing NOWHERE yet, so the first
                                    ; name stands it somewhere rather than
                                    ; believing it is already on A:.
                                    ;
                                    ; **ABOVE THE ARG_FILE BRANCH AND NOT
                                    ; INSIDE ITS SUCCESS ARM** (96.6.3.1): it
                                    ; was written after the `jc .idle` below,
                                    ; so wave 7's console door - which is the
                                    ; `.idle` arm - ran with it at 0 and undid
                                    ; 96.6.3 for every program started by
                                    ; typing its name. From the console on B:,
                                    ; prince.exe asked what drive it was on,
                                    ; was told A:, and printed "Please insert
                                    ; Prince of Persia Disk 1 into Drive A:" -
                                    ; 96.6.3's own symptom, through the one
                                    ; door that skipped its one store. An
                                    ; initialiser that only one entry path
                                    ; executes is not an initialiser
%ifdef DOSKPART
    mov word [dos_kdh + KDH_CODE], KDH_NOCODE   ; **THE SAME TRAP ONE CELL
    mov word [dos_kdh + KDH_AKB], 0             ; ALONG** (SPEC.md 96.41.4).
                                    ; `dos_wake` reads this word on EVERY wake
                                    ; to ask whether a handoff has come home,
                                    ; and 0 is a legal EXIT CODE - so a zeroed
                                    ; bss says "the program came back with 0"
                                    ; to the first wake a fresh instance gets,
                                    ; which is the one that LAUNCHES it. It was
                                    ; written only where the record is BUILT
                                    ; (dos_handoff), which is after the launch
                                    ; that would have been eaten
%endif
%ifdef DOS_WIN16
    mov si, dos_doc                 ; THE LOADER ASKED (SPEC.md 124.3.1):
    cmp byte [si], 0                ; OSAPI_ARG_FILE is read-and-clear, so the
    je .idle                        ; document is the locator it banked here -
    mov dx, [si + 13]               ; name[0] = 0 for a launch with none - in
    mov bl, [si + 15]               ; OUR segment, so the copy is DS to DS and
    mov [dos_dir], dx               ; ES stays the kernel's
    mov [dos_vol], bl
    mov di, dos_name
    mov cx, 13
.cpd:
    lodsb
    mov [di], al
    inc di
    or al, al
    loopnz .cpd
%else
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the ordinary case
    jc .idle                        ; for every package and the COMMAND.COM
                                    ; door for this one (wave 7)
    mov [dos_dir], dx
    mov [dos_vol], bl
    mov di, dos_name                ; copy the name out of KERNEL_SEG first:
    mov cx, 13                      ; ES is the kernel's here and the next call
.cp:                                ; is free to move what SI points at
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
%endif

    call dos_lnk_open               ; A SHORTCUT names another program and
                                    ; carries its arguments (SPEC.md 96.21);
                                    ; anything else is the program itself
    WIN_ONLY
    call dos_path_make              ; ...and EITHER WAY the box shows the
                                    ; fully qualified path of what is about to
                                    ; run (SPEC.md 96.32.3). AFTER the link is
                                    ; opened, because a link rewrites
                                    ; [dos_name] and the path must be the
                                    ; PROGRAM's rather than the shortcut's.
                                    ; A refusal leaves the bare name there,
                                    ; which is still true and still runs
    WIN_END
    WIN_ONLY                        ; **SHIFT OR ALT HELD: NAME IT, RUN NOTHING**
    mov ah, 2                       ; (SPEC.md 96.21.9). The BIOS's shift state
    int 0x16                        ; is a LEVEL the ROM's int 09h keeps, so it
    test al, 0x0B                   ; is current whenever it is asked: bits 0-1
    jnz .held                       ; Shift, bit 3 Alt. int 16h and not 0040:0017
    WIN_END                         ; because segment 0 is NULL on kern_mega
    mov byte [dos_state], DST_READY
    mov bx, [dos_win]
    call OSAPI_WM_WAKE              ; ...and run it from the wake handler, which
    jmp .ok                   ; is the one callback without the gfx lock.
                                    ; CF=1 here means the ring was FULL and
                                    ; nothing was posted - not an error, and the
                                    ; SDK's own remedy is to kick again from the
                                    ; next callback, which dos_paint does. It
                                    ; is not hypothetical: a launch that had to
                                    ; SWEEP VOLUMES to find this package
                                    ; (SPEC.md 54.4.2) fills the ring with the
                                    ; mounts on the way, and the symptom is a
                                    ; window that sits on "Starting..." for ever

.held:                              ; everything a launch stages is staged -
    mov byte [dos_state], DST_HELD  ; the path box, a .LNK's arguments and its
    mov ax, [dos_dir]               ; settings - and the prompt stands in the
    mov [dos_curdir], ax            ; PROGRAM's folder, so SETUP or INSTALL
%ifdef DOS_WIN16                    ; typed there is the one beside it - and
    mov byte [dos_w16k], 0          ; THAT one is not the file the loader
%endif                              ; read, so the next wake asks (124.3.1)
    WIN_ONLY
    mov si, dos_c_held
    call con_say
    WIN_END
    jmp short .arm
.idle:
    mov byte [dos_state], DST_IDLE
.arm:
%ifndef KD_BACKEND
    call dos_keyarm                 ; the console: Alt+Enter from its prompt
%endif
.ok:
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    ; **THE PROMPT, HERE AND NOT IN `dos_con_start`** (SPEC.md 96.33.2.1).
    ; Both doors converge on this label and `[dos_vol]` is settled at it: the
    ; empty one left `dos_con_start`'s OSAPI_FILE_HERE answer standing, and
    ; the document one has been through `OSAPI_ARG_FILE` and `dos_lnk_open` -
    ; either of which can name another drive. Written any earlier it names the
    ; drive the PACKAGE came off, which is only the document's by luck.
    ;
    ; It self-corrected on every path that RAN something, because
    ; `dos_con_ended` writes a fresh one - so what the field saw was the case
    ; where nothing runs: a `.LNK` on B:, arm 3, and Cancel at SPEC.md 96.42's
    ; question. `dos_wholedone` records the refusal and returns with
    ; `[dos_state]` still DST_READY and nothing to undo, which is right - and
    ; left `A:\>` on the glass over a DOS standing on B:.
    WIN_ONLY
    call dos_prompt                 ; ...and BEFORE the `clc`: it spends the
    WIN_END
%endif                              ; flags, and the CF here is the loader's
    mov bx, [dos_win]
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_wake - the OSAPI_WM_ONWAKE handler (SPEC.md 74.1)
; in:  SI = our window, UI task, gfx lock NOT held
; out: nothing
;
; A wake is a KICK and a stale one is possible, so the state byte is advanced
; BEFORE the run: a second wake arriving for any reason finds DST_RAN and
; does nothing, rather than launching the program twice.
; -----------------------------------------------------------------------------
dos_wake:
%ifdef DVW
    cmp byte [dvw_run], 0           ; A PROGRAM ON THE WORKER (SPEC.md 96.54):
    je .nodvw                       ; its doors, its paint and its end are
    WIN_ONLY
    jmp HENT(dvw_wake)              ; all this wake is for, and nothing else
                                    ; may start while it runs. A TAIL CALL:
                                    ; .out is a bare `ret` and nothing is
                                    ; pushed above it (size pass, 96.54.8)
    WIN_END
.nodvw:
%endif
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    cmp byte [dos_pkgq], 0          ; **A `.O88` TYPED AT THE PROMPT** (SPEC.md
    je .nopkg                       ; 96.33.17): OSAPI_PKG_START wants the gfx
    WIN_ONLY
    jmp dos_pkg_go                  ; lock FREE and W_ONKEY holds it, so the
                                    ; console posts and this is where it lands
                                    ; (a tail call, for .out's bare `ret`)
                                    ; - the same place a DOS program's own
                                    ; launch is serviced, and for the same
                                    ; reason. It is tested FIRST and clears its
                                    ; own flag, so it neither reads nor moves
                                    ; [dos_state]: a package is not the thing
                                    ; `run it again` re-runs
    WIN_END
.nopkg:                             ; **AND THE MISS LANDS HERE AND NOT PAST
                                    ; THE BLOCK BELOW** (SPEC.md 96.41.4). It
                                    ; was `.notpkg`, which is where the STATE
                                    ; MACHINE starts - so on every wake with
                                    ; no package pending, which is every wake
                                    ; there has ever been bar one, this jump
                                    ; went straight over the return's own
                                    ; test. The exit code was poked into the
                                    ; record by a kernel that then woke this
                                    ; window, and this handler never looked
%endif
%ifdef DOSKPART
    ; **THE MACHINE WENT AWAY AND CAME BACK** (SPEC.md 96.41): between the wake
    ; that posted the handoff and this one there was a hibernation, a DOS
    ; program with the whole machine, a restart and a resume - and from here it
    ; is an ordinary wake carrying a number. The kernel poked it into the
    ; record we posted, which is ours and was in the image.
    ;
    ; It is tested BEFORE the state machine for `dos_pkgq`'s reason: this is
    ; not `run it again`, it is the answer to a run that already happened, and
    ; a DOS sitting at DST_READY would otherwise launch the program a second
    ; time on the very wake that says it finished.
    mov ax, [dos_kdh + KDH_CODE]
    cmp ax, KDH_NOCODE
    je .nocode
    mov word [dos_kdh + KDH_CODE], KDH_NOCODE   ; read once
    test ah, KDC_FAIL               ; **IT DID NOT RUN** (SPEC.md 96.40.7), and
    jz .ran                         ; the low byte means nothing. Every refusal
    and ah, 0x7F                    ; over there used to report 0xFF and land
    mov [dos_err], ah               ; here as `ended, exit code 255` - a
    mov byte [dos_state], DST_ERR   ; sentence about a run that did not happen,
    jmp short .said                 ; for a program that was never on the disk.
.ran:                               ; kern_dos sends a DER_* now, so this is one
    mov [dos_exit], al              ; `and` and a store and dos_err_line says
    mov byte [dos_state], DST_RAN   ; the words DOS already had
.said:
    ; **AND THE ARENA IT WAS GIVEN** (SPEC.md 96.41.1), which came home in the
    ; next cell. The windowed DOS cannot work this one out: `[dos_akb]` is
    ; `dos_run`'s banked figure and on this arm `dos_run` posted and returned
    ; without ever claiming, so what stands there is the last WINDOWED launch's number or
    ; nothing at all - and either reads like an answer.
    mov ax, [dos_kdh + KDH_AKB]
    mov [dos_akb], ax
    ; --- ...AND THE DRIVERS THE LAUNCH TOOK OUT (SPEC.md 96.35.5) ----------
    ; **THE DEBT `.outq` DEFERS IS PAID HERE, AND IT WAS PAID NOWHERE.**
    ; `dos_lbfill` calls `dos_drv_take` on this arm too - the BLASTER= row has
    ; to exist before it can be gathered (96.44.13) - so the launch leaves
    ; with `[hb_susp]` naming the sound card's row and `[dos_drvout]` at 1.
    ; `dos_run`'s own `.out` is the one place that pays it back and the posted
    ; path reaches none of it on purpose; this is where that path ENDS, so
    ; this is where it owes.
    ;
    ; The kernel's own reload cannot cover it and must not be made to: it puts
    ; back `[hb_drvmask]`, which is what `hbm_detach` found still MOUNTED when
    ; it swept - and a row a suspend had already unmounted is not in it. The
    ; two words are separate on purpose (kernel/hiber.inc, `hb_susp`): a
    ; hibernate can be taken while a suspend stands, and the two sets come
    ; back at different moments. This is our moment.
    ;
    ; It read as a machine that resumed perfectly and was then silent for the
    ; rest of the session, with `[dos_drvout]` stuck at 1 so the NEXT launch's
    ; `dos_drv_take` early-returned and handed `kern_dos` a stale BLASTER=.
    call dos_drv_back
    ; ...AND THE CACHE DIAL IS THE SAME BRACKET AND WAS NOT PAID HERE EITHER
    ; (SPEC.md 18.95.8). `dos_cache_arm` commands the kernel's read-ahead width
    ; before the handoff and `dos_run`'s `.out` is the only place that gives it
    ; back - which this path does not reach, exactly as it does not reach
    ; `dos_drv_back` above. On this arm the machine went away and came back
    ; from a hibernation image, so the commanded cap is IN the image: a session
    ; that ran one `.LNK` with the dial off Auto keeps that width for the rest
    ; of its life, on every volume, with nothing on the glass to say so.
    ; A resume with nothing commanded is free, the same way one with nothing
    ; suspended is.
    call dos_cache_free
%ifdef DOS_MEGA
    ; **THE PROGRAM'S LAST SCREEN, CAPTURED ON THE TRIP** (KERN-MEGA-PLAN
    ; 14.106, SPEC.md 96.34.6): kern_dos's kd_snap wrote con_scr and the
    ; record in front of it through the linear address the kernel staged, so
    ; what is left is dos_snap's TAIL - the cursor clamped, the wrap and the
    ; scroll debt spent, every row marked - and the exit line below lands
    ; under the program's last screen exactly as it does in the window
    cmp byte [dos_snrec + 2], 0
    je .nosn
    mov byte [dos_snrec + 2], 0
    mov ax, [dos_snrec]             ; AL the column, AH the row
    push ax
    xor ah, ah
    cmp al, CON_COLS - 1
    jbe .sncx
    mov al, CON_COLS - 1
.sncx:
    mov [con_cx], ax
    pop ax
    mov al, ah
    xor ah, ah
    cmp al, CON_ROWS - 1
    jbe .sncy
    mov al, CON_ROWS - 1
.sncy:
    mov [con_cy], ax
    mov byte [con_pwrap], 0
    mov word [con_scrl], 0
    call con_markall
.nosn:
%endif
    ; ...and the console says so HERE, which is where the run really ended.
    ; `dos_run`'s own `.out` cannot: the post is spent long before the program
    ; starts, so a line written there is about a launch that has not happened
    ; (SPEC.md 96.35.1). Before `dos_swap`, which is the repaint, for
    ; `dos_repaint`'s reason one arm over.
%ifdef DOS_WTAIL
    jmp dos_endk                    ; the window's end (dos_endk_w), or the
%else                               ; headless toast - a tail call: .out is ret
    call dos_con_ended              ; the exit line, the repaint and the full
    mov bx, [dos_win]               ; screen are the console's
    call dos_swap
    call dos_fsx_back               ; ...AND BACK INTO THE FULL SCREEN when the
                                    ; command was typed there (dos_endk_w)
    jmp short .out
%endif
.nocode:
%endif
    cmp byte [dos_state], DST_CPWAIT
    je .go                          ; the compaction has run and the heap is
                                    ; packed BOTH ways: dos_run picks up at the
                                    ; claim, and plain OSAPI_MEM_AVAIL is exact
%ifdef DOSKPART
    ; --- A RESUME THIS DOS NEVER SAW (SPEC.md 96.35.5) ---------------------
    ; **AFTER THE DST_CPWAIT TEST AND THAT IS THE WHOLE GUARD.** A wake can
    ; find `[dos_drvout]` set for exactly two reasons: the compaction wait,
    ; where the drivers stay out ON PURPOSE and the line above has already
    ; gone - and a handoff whose return never came through `dos_wake` at all.
    ; Every other path pairs `dos_drv_take` with `dos_drv_back` inside one
    ; `dos_run` call, which no wake can land in the middle of.
    ;
    ; That second reason is the COLD boot: a DOS program that crashes, or a
    ; machine switched off with one running, comes home by Resume rather than
    ; by the live return, and `hbm_wake` reaches `wm_wake` only when it has an
    ; exit code to deliver - so this DOS is restored onto the desktop with its
    ; flag set and no wake ever sent. The kernel unions `[hb_susp]` into its
    ; own reload for that door, so the card is already back by here; what is
    ; left is OUR flag, and left set it makes the next `dos_drv_take`
    ; early-return - a launch that runs with ~14KB less arena than the machine
    ; would give it and a BLASTER= nobody refreshed (SPEC.md 96.35, 96.44.13).
    ;
    ; It is `dos_drv_back` and not a store to the flag, so it is right whatever
    ; the other side did: with `[hb_susp]` already spent the resume puts back
    ; nothing and costs one module read, and with it standing it puts back the
    ; rows. The flag is cleared either way.
    cmp byte [dos_drvout], 0
    je .nodebt
    call dos_drv_back
.nodebt:
%endif
    cmp byte [dos_state], DST_READY
    jne .out
%ifdef DOS_WIN16
    shr byte [dos_w16k], 1      ; THE LOADER READ THIS FILE and it is no NE
    jc .w16no                   ; (SPEC.md 124.3.1): once, the launch's own
    call dos_w16_try            ; wake. SPEC.md 124.3: a WINDOWS program is
    jc .out                     ; WIN16's, asked before anything is claimed or
.w16no:                         ; taken out - and before arm 3's question, too.
                                ; Headless as well: a link's target, run with
                                ; no window, is still WIN16's (96.44.20)
%endif
%ifdef DOSKPART
    call dos_wholeask           ; SPEC.md 96.42: arm 3 with nothing to come
    jc .out                     ; back to asks FIRST, and the answer comes back
%endif                          ; through this same door
.go:
%ifndef KD_BACKEND
    call dos_keyarm                 ; a program runs here: not a forward
%endif
    mov byte [dos_state], DST_RAN
    call dos_run
.out:
    ret

%ifndef KD_BACKEND                  ; 96.43: the console is the window's
; dos_keyarm - **ASK ONCE, TO ARM THE KEY-STATE MAP** (SPEC.md 9.7): kbm_isr
; does not track a scancode until something has asked, so without this call
; neither half of 96.33.5.1 can see Alt+Enter - not the kernel's latch and
; not our own poll. The ANSWER is discarded; the asking is the whole point,
; and it is what keeps the feature costing a machine that never opens DOS
; exactly nothing. Asked when DOS opens EMPTY, a console, and when it RUNS
; a program, and not in the entry: a DOS that hands a Windows program to
; WIN16 (dos_w16_try, SPEC.md 124.3) never keeps the window, and an armed
; map is the port-60h peek kbm_isr makes on every key from then on, which
; on an 8042 that promotes its next byte at once (QEMU's) loses a byte to
; the ROM - Ctrl+End's Ctrl release, in win16file (SPEC.md 96.33.5.1).
; Preserves every register but the flags
dos_keyarm:
    push ax
    mov al, KSC_ALT
    call OSAPI_KEY_DOWN
    pop ax
    ret
%endif

; -----------------------------------------------------------------------------
; dos_run - navigate, claim, load, and take the machine
; in:  nothing; UI task, lock NOT held
; out: nothing; [dos_state] and [dos_exit]/[dos_err] are the answer
; -----------------------------------------------------------------------------
dos_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
%ifdef DOS_SETPART
    call dos_sp_drop                ; **THE SETUP PAGE'S CODE GOES FIRST**
%endif                              ; (SPEC.md 96.32.2.3): it is a PINNED claim,
                                    ; and the program's arena and the posted
                                    ; pass below should meet no wall. A page
                                    ; still up reads it again at its repaint
%ifdef DVW
    ; --- `Run in a window`, THE MODE (SPEC.md 96.32.2.4, 96.54.10) ----------
    ; The box and the menu item are one byte, read HERE so that every way into
    ; a run - the bar, Enter, the menu's `Run`, a .LNK saved with it ticked,
    ; and every program TYPED AT THE PROMPT - reaches it, and on BOTH passes
    ; of a compaction wake: dvw_want is read below this line four times
    ; before it is spent. Arm 1 ignores the box: a
    ; window cannot shut the OS down. dos_mem_fix first, so a link's arm 1
    ; that this machine demotes is arm 0 and gets its window.
    call dos_mem_fix
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    je .nobox
    cmp byte [dos_mwin + OS88UI_CK_ON], 0
    je .nobox
    mov byte [dvw_want], 1
.nobox:
%endif

    mov dx, [dos_dir]               ; ...stand where the file is (SPEC.md 19.2.1
    mov bl, [dos_vol]               ; put us there already, but a quiet GOTO is
    call dos_be_goto                ; what makes that true after any navigation)
    jnc .there
    mov al, DER_GOTO
    jmp .err
.there:

    cmp byte [dos_cpw], 0
    jne .floor                      ; THE COMPACTION WAKE resumes here: the
                                    ; buffers are claimed already, and claiming
                                    ; them twice would leak 2KB a launch
%ifdef DVW
    mov word [dvw_cap], DVW_DEFKB   ; AN AUTOMATIC LIMIT STARTS AT THE DEFAULT
%endif                              ; on each launch, and only dvw_regrow
                                    ; raises it (SPEC.md 96.54.2.1) - here,
                                    ; which the compaction wake skips, so a
                                    ; raise outlives the pass it posted
%ifndef KD_BACKEND
    WIN_ONLY
    call dos_con_starting           ; **SAID BEFORE IT HAPPENS** (SPEC.md
                                    ; 96.33.20.1), and HERE rather than at
                                    ; `.there`: everything above this point
                                    ; runs again on the compaction wake, and a
                                    ; launch that had to wait for a pass would
                                    ; announce itself twice
    WIN_END
    call dos_mem_fix                ; the arm FIRST, because dos_pkt_bufs reads
                                    ; the network part only for an arm that
                                    ; will publish it (SPEC.md 96.23.11) -
                                    ; and `.floor` asks again, which is free
    call dos_pkt_bufs               ; THE PACKET DRIVER'S BUFFERS FIRST (SPEC.md
                                    ; 96.23.7): the sizing below takes
                                    ; everything left, so a claim after it is a
                                    ; claim that always fails. It is 3KB and it
                                    ; preserves BX, so it sits in front of the
                                    ; floor rather than inside it - and it asks
                                    ; at the DEFAULT level, which for three
                                    ; kilobytes never needs a purge to answer.
                                    ; **IT IS DECLARED MOVABLE** (SPEC.md
                                    ; 96.23.7.2): everything below runs a
                                    ; compaction pass over a heap this block is
                                    ; already standing in, and pinned it
                                    ; stranded 39.5KB under itself
%ifdef DVW_SPLIT
    WIN_ONLY
    call dvw_pfetch                 ; **THE WINDOW'S PART** (SPEC.md 96.54.9),
    WIN_END
    jnc .dvwok                      ; here for the network's reason: before the
    mov al, DER_DVWP                ; arena is sized, so it is placed with the
    jmp .err                        ; part in the heap - and a REFUSED fetch
.dvwok:                             ; refuses the run, in the window, rather
                                    ; than handing the user the full screen
                                    ; they did not ask for. A run on the full
                                    ; screen drops the part instead
%endif
%endif
.floor:
    call dos_mem_fix                ; ...and the ARM is the user's, once this
                                    ; has made sure it is one the machine can
                                    ; actually carry out (SPEC.md 96.36.1)
%ifdef DOSKPART
    ; --- ARM 3: THE WHOLE MACHINE (SPEC.md 96.40) --------------------------
    ; HERE, before the arena is claimed and before a driver is unmounted: what
    ; follows posts a teardown and RETURNS, so every byte this routine would
    ; otherwise have spent is a byte the handoff would have to give back.
%ifdef DVW
    cmp byte [dvw_want], 0          ; a run in the window never takes the
    jne .notwhole                   ; whole machine (SPEC.md 96.54)
%endif
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    jne .notwhole
    call dos_handoff
    jc .nowhole                     ; POSTED - ui_task's step 0 spends it with
                                    ; nothing held, and the machine does not
                                    ; come back
    ; **AND IT LEAVES BY THE QUIET DOOR** (SPEC.md 96.35.1). This used to fall
    ; into `.out`, which runs `dos_con_ended` - so a successful post printed
    ; *"ended, exit code 000"* and the arena INTO THE CONSOLE, before the
    ; machine had been handed over and about a program that had not started.
    ; The field read it as DOS loading something in order to exit. Every
    ; reason `.outq` exists for applies here and only the `[dos_cpw]` store
    ; does not: the post is spent, and a compaction wake's flag left set would
    ; survive inside the image and make the NEXT launch in this instance skip
    ; its packet buffers.
    mov byte [dos_cpw], 0
    jmp .outq
.nowhole:
    mov al, DER_HAND                ; **AND THE REFUSAL IS NOT A MEMORY ONE**
    jmp .err                        ; (SPEC.md 96.40.6): the slot does no
                                    ; sizing, because this arm does none - the
                                    ; kernel is torn out and the program is
                                    ; handed the machine, so a program DOS
                                    ; could launch will launch. It can only
                                    ; refuse a post that is already standing
.notwhole:
%endif
    call dos_cache_arm              ; **THE DIAL IS A COMMAND** (SPEC.md
                                    ; 18.95.8), and it is sent BEFORE the first
                                    ; AVAIL below rather than left to the
                                    ; claim: `mem_claim` would shed the whole
                                    ; window anyway, one 64KB page later,
                                    ; having first refused a claim the machine
                                    ; could have met - and it can only shed the
                                    ; whole of it, where this hands back a
                                    ; WIDTH. Undone at `.out` by
                                    ; `dos_cache_free`, the same bracket as
                                    ; `dos_drv_take` above
    mov bl, DOS_PG_FLOOR            ; THE FLOOR IS THE USER'S (SPEC.md 96.25),
                                    ; and it is OURS ON EVERY RUNG now: what
                                    ; the dial wanted kept is already exactly
                                    ; what stands, so a floor of MEM_LVL_TOP
                                    ; would take back the 9 or 18 KB the user
                                    ; just asked to keep. Off has already left
                                    ; nothing at this rank, so the two cases
                                    ; that used to need two floors need one
                                    ; (50.6.6, 96.24)
.sized:
    mov al, bl                      ; ...AND IT IS SET ONCE, HERE, for this
    call OSAPI_MEM_FLOOR            ; task: every AVAIL below answers net of
                                    ; it, the posted pass drops nothing above
                                    ; it, and the claim honours it - so the
                                    ; number shown is the number handed out.
                                    ; Idempotent, which the wake relies on:
                                    ; this line runs again on the way back
                                    ; through. Lifted at .lift, whatever the
                                    ; claim answered (SPEC.md 50.6.6)
    ; --- UNMOUNT, ASK TWICE, AND COME BACK FOR THE ANSWER (SPEC.md 96.35) ---
    ; The sound driver is ~14KB at the top of the heap, and unmounting it used
    ; to happen INSIDE the fsx bracket - long after this claim - so the memory
    ; went back to a heap nobody would ask about again
    ; (docs/plans/DISK-CPU-PLAN.md 5). It comes out HERE now, and what makes
    ; the hole reachable is that a package cannot compact the heap it is
    ; standing in: OSAPI_MEM_COMPACT's post records the wish and RETURNS, and the
    ; pass runs at ui_task's step 0 with nothing held (SPEC.md 66.4.3).
%ifdef DVW
    ; **A WINDOW IS NOT THE WHOLE MACHINE** (SPEC.md 96.54.2): with no limit
    ; of the user's, a run in the window is given DVW_DEFKB and not
    ; everything, or a second DOS window has nothing left to run in. Set on
    ; every pass, the compaction wake's included; the bracket's run is
    ; unchanged, its cap being the user's own word as before. AX rides
    ; through dos_drv_take, which preserves every register.
    mov ax, [dos_memkb]
    cmp byte [dvw_want], 0          ; ...AND NO DRIVER COMES OUT FOR THE
    je .take                        ; WINDOW (SPEC.md 96.54): the desktop goes
    or ax, ax                       ; on running and its sound driver with it
    jnz .kept
    mov ax, [dvw_cap]               ; AUTOMATIC: DVW_DEFKB, or what dvw_regrow
    jmp short .kept                 ; raised it to for this program (96.54.2.1)
.take:
%endif
    call dos_drv_take               ; THE DRIVERS OUT FIRST, on every arm and
                                    ; before any claim: it is what CREATES the
                                    ; hole the no-cap arm sizes into, and the
                                    ; capped arm needs them out too - the
                                    ; program's BLASTER= is made of what they
                                    ; say on the way past (96.44.13) - and
                                    ; OSAPI_DRV_SUSPEND reads HIBER.DRV into
                                    ; the heap to do it (SPEC.md 51.11), which
                                    ; a claim of everything would leave no
                                    ; room for. From here every exit owes
                                    ; dos_drv_back, which .out does on every
                                    ; path but the posted one (96.35.5). On
                                    ; the wake it is a no-op: [dos_drvout]
%ifdef DVW
.kept:
    mov [dvw_cap], ax
%endif
    cmp byte [dos_cpw], 0
    jne .ask                        ; on the wake the heap IS packed, so plain
                                    ; avail is exact and posting again is how a
                                    ; program spins (SPEC.md 66.4.3.2)
    cmp word [DOS_ACAP], 0
    je .unmount                     ; NO CAP: we want the maximum, so the
                                    ; question is only whether a pass adds any
    push bx                         ; ...A CAP, and the cheaper road: a program
    call OSAPI_MEM_AVAIL            ; that asked for 200K on a machine with
    pop bx                          ; 300K free needs no compaction and no
                                    ; silence. Net of the floor, like every
                                    ; AVAIL on this task from .sized on
    cmp ax, [DOS_ACAP]
    jae .ask                        ; it fits: claim it and ask nothing more
.unmount:
    push bx
    call OSAPI_MEM_AVAIL
    mov [dos_akb], ax               ; what the heap gives WITHOUT a pass...
    pop bx
    push bx
    mov al, bl                      ; ...and what it would give with one, AT
    xor ah, ah                      ; THE SAME LEVEL, or the two are answers to
    call OSAPI_MEM_COMPACT          ; different questions (SPEC.md 66.4.3.2) -
    pop bx                          ; AH = MEMC_WHATIF
    cmp word [DOS_ACAP], 0
    jne .capmax
    cmp ax, [dos_akb]               ; no cap: does a pass add anything at all?
    jbe .ask                        ; no - claim what is there
    jmp short .post
.capmax:
    cmp ax, [DOS_ACAP]             ; a cap: could a pass even fill it?
    jb .lift                        ; no, and nothing else will either - CF is
                                    ; set, and .lift keeps it
.post:
    push bx
    mov al, bl                      ; AL = the shed rank the pass must respect,
    mov ah, MEMC_POST               ; which is the same promise the claim makes
    mov bx, [dos_win]
    call OSAPI_MEM_COMPACT
    pop bx
    jc .ask                         ; refused - a post of ours already stands,
                                    ; or the window is not ours. Carry on with
                                    ; the heap as it is rather than waiting for
                                    ; a wake that is not coming
    mov byte [dos_cpw], 1
    mov byte [dos_state], DST_CPWAIT
    jmp .outq                       ; **RETURN.** The pass cannot run while we
                                    ; are executing in the region it is going
                                    ; to move, and the drivers stay OUT across
                                    ; it on purpose
.ask:
    push bx
    call OSAPI_MEM_AVAIL            ; AX = the largest run a claim can HAVE at
    pop bx                          ; the floor - already net of every
                                    ; purgeable cache BELOW it and of what a
                                    ; compaction would recover (SPEC.md 50.6.3,
                                    ; 66.10.3). Nothing to compute, nothing to
                                    ; probe, and BX is an OUTPUT here
    mov dx, [DOS_ACAP]             ; ...and the user's own cap, if there is
    or dx, dx                       ; one. 0 is "as much as the machine will
    jz .cap                         ; give", which is what a double click gets
    cmp ax, dx
    jbe .cap
    mov ax, dx
.cap:
    cmp ax, DOS_MIN_KB
    jb .lift                        ; CF is set, and .lift keeps it
    mov [dos_akb], ax               ; BANKED: the claim's answer is DX and the
                                    ; slot promises nothing about AX, so the KB
                                    ; figure has to survive the call somewhere
                                    ; other than in a register
    call OSAPI_MEM_CLAIM_HI         ; AX = KB -> DX = base segment, at the
                                    ; floor .sized set - the same one the
                                    ; number above was answered at, or the plan
                                    ; is not the one the claim carries out.
                                    ; HI because a region's door is where a
                                    ; claim this size belongs (50.3.2)
.lift:
    mov al, MEM_LVL_TOP             ; THE FLOOR IS LIFTED WHATEVER THE ANSWER:
    call OSAPI_MEM_FLOOR            ; left standing it is every later claim on
                                    ; the UI task's, which is every other
                                    ; package's. The slot preserves the flags,
                                    ; so the claim's CF - or the `jb` that
                                    ; brought a refusal here - still reads
    jnc .got
.nomem:
    mov al, DER_MEM
    jmp .err
.got:
    mov [dos_arena], dx
    mov ax, [dos_akb]
    mov cl, 6
    shl ax, cl                      ; KB -> paragraphs, and AX < 1024 always
    mov [dos_apara], ax             ; (640KB is 640), so this cannot carry

    mov ax, dx                      ; the FIRST program: its PSP is the
    add ax, DOS_PSPP                ; arena's, and its name is the one the
    mov [dos_ldpsp], ax             ; desktop launched
    mov word [dos_ldname], dos_name

    call dos_fh_setup               ; ...and the file window comes OFF the top
    jc .freeerr                     ; of it before the program is ever told how
                                    ; much memory it has (SPEC.md 96.11), so
                                    ; there is no window for a program to find
                                    ; and no arithmetic for it to disagree with

    mov ax, [dos_apara]             ; ...and only NOW is the first program's
    sub ax, DOS_PSPP                ; block known: the window came off the top
    mov [dos_ldpara], ax            ; of the arena a moment ago, and a block
                                    ; sized before that would hand the program
                                    ; the file window as its own memory
    call dos_load                   ; the image, through the back end
    jc .freeerr
    call dos_is_exe                 ; ...and only NOW, because the answer is in
    jnc .isCOM                      ; the FILE and not in its name
    call dos_exe_setup              ; MZ: relocate, move down, size the block
    jnc .ready
    jmp short .freeerr              ; AL is already a DER_*
.isCOM:
    mov byte [dos_isexe], 0         ; DECIDED ON EVERY LAUNCH (SPEC.md 96.8.3):
                                    ; left at an earlier .EXE's 1, this .COM
                                    ; was entered at THAT program's CS:IP
    mov dx, [dos_imghi]             ; a .COM is ONE segment: 64KB - the PSP -
    or dx, dx                       ; the pushed word is the ceiling, so a high
    jnz .toobig                     ; word at all is a file that cannot be one
    cmp word [dos_imgsz], 0xFF00
    jbe .ready
.toobig:
    mov al, DER_BIG
    jmp short .freeerr
.ready:
%ifdef DOSTRACE
    mov al, OP_CARVE                ; THE TRACE PART IS PINNED FROM HERE TO THE
    call op_pin                     ; DUMP (SPEC.md 96.29.1.2): the program runs
                                    ; in the bracket below, its INT 21h calls
                                    ; trace into the part and claim (a mount's
                                    ; read-ahead, the file window) on the UI
                                    ; task - which is the task that compacts.
                                    ; Only DX and the flags are spent
%endif

%ifdef DVW
    xor al, al                      ; **RUN IN THE WINDOW** (SPEC.md 96.54),
    xchg al, [dvw_want]             ; read and spent in one: the worker takes
    or al, al                       ; it from here, and dvw_wake pays this
    jz .bracket                     ; routine's tail when the program ends.
    WIN_ONLY                        ; (dvw_want is never set headless)
%ifdef DVW_SPLIT
    call dvw_pin                    ; the IVT and the worker will name it
%endif
    call HENT(dvw_start)            ; No worker - the task table full - and
    jnc .outq                       ; the full screen still works, so that is
%ifdef DVW_SPLIT
    call dvw_relax                  ; (...and no run names the part after all)
%endif
    WIN_END
.bracket:                           ; what it gets. AX is spent: GFX_LOCK
%endif
    call OSAPI_GFX_LOCK             ; ...and only NOW, because fsx_run wants it
    mov ax, dos_fsx_main            ; held and nothing above this may pay for it
    mov bx, [dos_win]
    xor cx, cx                      ; no FSXF_KEEPWORKER: there is no worker
    mov byte [dos_inbr], 1          ; **WHICH SCREEN dos_tty WRITES TO** (SPEC.md
    call OSAPI_FSX_RUN              ; 96.33): the ROM's teletype in here and the
    mov byte [dos_inbr], 0          ; console outside. It is set around the
                                    ; BRACKET and not around the program,
                                    ; because dos_fsx_main's own OSAPI_FSX_MODE
                                    ; has already taken the screen by the time a
                                    ; program runs and has not given it back
                                    ; when one exits
    pushf
    call OSAPI_GFX_UNLOCK
    popf
    jnc .ran
    mov al, DER_FSX
    jmp short .freeerr
.ran:
    mov byte [dos_state], DST_RAN
    jmp short .free
.freeerr:
%ifdef DVW
    cmp byte [dvw_want], 0          ; **A WINDOW GROWS TO ITS PROGRAM** (SPEC.md
    je .noreg                       ; 96.54.2.1): a DER_FIT under an Automatic
    call HENT(dvw_regrow)           ; limit frees the arena and raises the cap
    jnc .noreg                      ; to what the header asks - and the sizing
    jmp .floor                      ; runs again, posted pass and all
.noreg:
%endif
    mov [dos_err], al
    mov byte [dos_state], DST_ERR
.free:
%ifdef DOSTRACE
    mov al, OP_CARVE                ; pinned again for the paths that never
    call op_pin                     ; reached .ready: the dump's FILE_WRITE is
    call dos_trace_dump             ; ES:BX INTO the part, across the kernel's
    mov al, OP_CARVE                ; own claims (SPEC.md 66.5.7.1) ...and the
    call op_movable                 ; field gets to read it too. MOVABLE AGAIN:
%endif                              ; no program, no dump, nothing holds it
    mov dx, [dos_arena]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
    mov word [dos_arena], 0
    jmp short .out
.err:
    mov [dos_err], al
    mov byte [dos_state], DST_ERR
.out:
%ifdef DVW
    mov byte [dvw_want], 0          ; a run that never reached the bracket
%endif                              ; does not leave the next one windowed
    mov byte [dos_cpw], 0           ; THE REAL EXIT, so the next launch in this
                                    ; instance sizes from the start again
%ifndef KD_BACKEND
    call dos_pkt_shut               ; every translated flow closed (dn_shut),
                                    ; before the driver that owns its sockets
                                    ; is resumed - and THE RAW CLAIM GOES BACK
                                    ; FIRST, on every
                                    ; path here for dos_drv_back's own reason
                                    ; (SPEC.md 96.23.5): a release by a caller
                                    ; that does not hold one is a no-op, and a
                                    ; machine left with its own stack switched
                                    ; off because a program crashed is not. It
                                    ; is BEFORE the resume because the driver
                                    ; the claim is against must still be
                                    ; mounted to hear it
%endif                              ; KD_BACKEND
    call dos_drv_back               ; ...and back again, on EVERY path through
                                    ; here including the refusals: a resume with
                                    ; nothing suspended is free and a machine
                                    ; left silent is not (SPEC.md 51.11.1)
    call dos_cache_free             ; ...and the cache likewise (SPEC.md
                                    ; 18.95.8). It is the same bracket for the
                                    ; same reason, and it is the half that had
                                    ; nowhere to live before the slot existed:
                                    ; the kernel re-claims the window at a
                                    ; MOUNT and a DOS session mounts nothing,
                                    ; so a launch on Off used to leave the
                                    ; machine with no directory cache until
                                    ; something was inserted. `.outq` skips it,
                                    ; exactly as it skips the resume: the
                                    ; compaction is going to run and then come
                                    ; back through here
%ifdef DOS_WTAIL
    call dos_endr                   ; the window's end (dos_endr_w), or the
%else                               ; headless toast
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    call dos_con_ended              ; ...and the console says what happened, which
                                    ; is where §96.32's three status lines went
                                    ; (SPEC.md 96.33): a log says it once and it
                                    ; stays said, where a sentence on the band
                                    ; was true until the next launch
%endif
    call dos_repaint                ; THE WINDOW DOES NOT REPAINT ITSELF. On the
                                    ; path that runs, fsx_restore's wm_paint_all
                                    ; (SPEC.md 53.6) happens to redraw us and
                                    ; the exit code appears - so every FAILURE
                                    ; path silently left "Starting..." on the
                                    ; glass while the real reason sat in
                                    ; [dos_err] where nobody could see it. That
                                    ; is the worst shape a refusal can have
                                    ; (SPEC.md 47): the state was right and the
                                    ; screen was a lie
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    call dos_fsx_back               ; ...AND BACK INTO THE FULL SCREEN if that
                                    ; is where the command was typed (SPEC.md
                                    ; 96.33.16). Here, on the one path every
                                    ; launch AND every refusal reaches, and
                                    ; after dos_repaint so the window under it
                                    ; is right when the bracket next comes down
%endif
%endif                              ; DOS_WTAIL
.outq:                              ; ...AND THE POSTED PATH, which reaches
                                    ; none of the above on purpose: nothing has
                                    ; ended, the drivers must stay out for the
                                    ; pass to have their space, and the window
                                    ; still says what it said (SPEC.md 96.35.1)
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)

WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
%ifdef DOS_WTAIL
; -----------------------------------------------------------------------------
; dos_endk_w - dos_wake's end, the window's (dos_endk): the exit line, the
; repaint, and BACK INTO THE FULL SCREEN when the command was typed there
; (SPEC.md 96.33.16) - the posted path leaves dos_run by `.outq`, past its own
; call, so the return is where [dos_fsxgo] is spent, or it stood until some
; later WINDOWED exit spent it and threw that console into the full screen.
; kern_mega's trip (14.106) and kern_big's shut-down arm alike (14.113)
; -----------------------------------------------------------------------------
dos_endk_w:
    call dos_con_ended
    mov bx, [dos_win]
    call dos_swap
    jmp dos_fsx_back

; dos_endr_w - dos_run's end, the window's (dos_endr): the console's account
; of the run, then dos_repaint - THE WINDOW DOES NOT REPAINT ITSELF, so every
; FAILURE path would leave "Starting..." on the glass - and back into the full
; screen, after the repaint so the window under it is right (dos_run's notes)
dos_endr_w:
    call dos_con_ended
    call dos_repaint
    jmp dos_fsx_back
%endif

; -----------------------------------------------------------------------------
; dos_repaint - the content, under a lock WE take
; in:  nothing; the wake handler's context, gfx lock NOT held (SPEC.md 74.1)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
dos_repaint:
    push ax
    push bx
    push cx
    push dx
    push si
    call OSAPI_GFX_LOCK             ; a wake handler is the one callback
    mov si, [dos_win]               ; without the lock, and it MAY take it for
                                    ; a burst it can state (SPEC.md 74.1)
    mov bx, si                      ; ...CUT TO WHAT IS VISIBLE (SPEC.md
    call OSAPI_WM_CLIP_SET          ; 96.54.10): a run in the window ends
    jc .hid                         ; behind whatever the user went on to use,
                                    ; and an unclipped repaint lands on it
    mov al, CWHITE                  ; **THE INK FIRST** (SPEC.md 96.19.5). AL
    call OSAPI_SET_COLOR            ; is the LOW BYTE of the AX that
                                    ; WM_CONTENT is about to answer x1 in, so
                                    ; setting it after was `mov al, 15` over a
                                    ; content left of 121 - a fill from x=15,
                                    ; which is the window's own left border and
                                    ; most of the desktop beside it
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    push ax
    push dx
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    pop bx                          ; ...ASKED, not two constants left over
    pop ax                          ; from a 288x100 window. The window is
    jc .nofill                      ; adapter-sized now (SPEC.md 96.20.3) and
    add cx, ax                      ; a hardcoded width is the same class of
    dec cx                          ; bug as the ink that used to be set into
    add dx, bx                      ; this AX. AX,BX,CX,DX are x1,y1,x2,y2 by
    dec dx                          ; the time this falls through
    call OSAPI_GFX_FILL
.nofill:
    mov si, [dos_win]
    call dos_paint
.hid:
    call OSAPI_GFX_UNLOCK
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
WIN_CODE_END
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_is_exe - is the loaded image an .EXE?
; in:  the image is already in the arena at PSP:0100
; out: CF=1 yes; preserves everything but the flags
;
; THE SIGNATURE DECIDES, NOT THE EXTENSION, and that is DOS's own rule rather
; than a simplification of it: INT 21h AH=4Bh reads the header and loads an
; MZ (or the rarer ZM) as a relocatable .EXE and ANYTHING ELSE as a .COM at
; PSP:0100, whatever the file is called. The extension only drives
; COMMAND.COM's search order when a bare name is typed.
;
; This is not a corner: SOPWITH2.EXE - a period game, verified on real
; hardware - has NO MZ header at all. It is a Microsoft-C-style .COM whose
; first instructions read PSP:0002 and set DS past the code, and it is named
; .EXE. Dispatching on the name refuses a file DOS runs, and this routine used
; to do exactly that, under a comment asserting the opposite rule.
; -----------------------------------------------------------------------------
dos_is_exe:
    push ax
    push es
    mov ax, [dos_ldpsp]
    add ax, 16
    mov es, ax
    mov ax, [es:0]
    cmp ax, 0x5A4D                  ; 'MZ'
    je .yes
    cmp ax, 0x4D5A                  ; 'ZM' - the same header, byte-swapped,
    je .yes                         ; which a few very early linkers emitted
    pop es
    pop ax
    clc
    ret
.yes:
    pop es
    pop ax
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_load - read the program into the arena at PSP:0100
; in:  [dos_ldpsp], [dos_ldpara], [dos_ldname]
; out: CF=0 ([dos_imgsz]/[dos_imghi] = the bytes, for a file read whole);
;      CF=1 with AL = a DER_*
;
; **AN .EXE IS READ AS FAR AS ITS LOAD MODULE AND NO FURTHER** (SPEC.md
; 96.8.2). The first granule says which kind of file this is; an `MZ` header
; then says how many 512-byte pages its header + image take, and only those
; are read - DOS's own rule, which leaves overlays and appended data on the
; disk for the program to open itself. Anything else - a .COM, or a
; COMPRESSED file, whose first bytes are the wrapper because DBE_RDAT is raw
; (SPEC.md 20.14.3) - is read WHOLE, as it always was.
; -----------------------------------------------------------------------------
dos_load:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es

                                    ; THE PROGRAM BEING LOADED, not the arena
                                    ; (SPEC.md 96.14): a child from AH=4Bh is
                                    ; loaded exactly this way into a block of
                                    ; its own. dos_ldrd's first chunk sets ES
                                    ; to [dos_ldpsp]+16, DI being 0 - which is
                                    ; the probe's buffer, dos_is_exe's ES:0 and
                                    ; .whole's ES:BX, so it is not set here
    mov si, [dos_ldname]            ; THE NAME IS AN ARGUMENT TOO: AH=4Bh loads
                                    ; a file the running program named
    xor bx, bx                      ; ES:0 is every chunk's buffer, and BX
    xor di, di                      ; stays 0 through every read below
    mov bp, 32                      ; THE PROBE: the first SECTOR, which is
    call dos_ldrd                   ; READ_AT's granule on kern_big and kern_dos
    jc .whole                       ; (SPEC.md 18.4.4.2). kern_small's is a
                                    ; CLUSTER and refuses it, and so reads the
                                    ; file whole, as it always did. 32
                                    ; paragraphs is under the smallest block
                                    ; AH=4Bh gives a child (64), so the probe
                                    ; needs no bound of its own
    call dos_is_exe                 ; CF=1 = 'MZ' at ES:0
    jnc .whole
    mov ax, [es:MZ_CP]              ; header + image in 512-byte PAGES, the
    dec ax                          ; last one partial or not: the bytes DOS
    cmp ah, 7                       ; reads, rounded up to a sector - which is
    jae .whole                      ; the granule, so no rounding is needed.
    inc ax                          ; None at all, or past 896KB, is the whole
    mov cl, 5                       ; read's and dos_exe_setup's to refuse
    shl ax, cl                      ; ...in paragraphs. The slack is the
    xchg ax, bp                     ; overlay's first bytes, inside the block
    lea ax, [bp+16]                 ; DOES THE BLOCK HOLD THE PSP AND THE
    cmp [dos_ldpara], ax            ; MODULE? Decided from the header, before
    jb .nofit                       ; anything more is read
    mov ax, bp                      ; ONE OPERATION FOR THE MODULE (SPEC.md
    sub ax, di                      ; 96.8.2.1, 12.8.10): the paragraphs left
    mov cx, 16                      ; to read, as bytes - one bar where each
    mul cx                          ; 32KB chunk's READ_AT filled it again.
    xchg ax, cx                     ; Advisory: kern_dos refuses it
    call dos_be_fsop
    call dos_ldrd                   ; DI = 32: the probe's sector is in already
    call dsh_opend                  ; ...ended, the flags kept
    jnc .out                        ; CF=0 is the answer
.rerr:
    ; --- "TOO BIG" IS NOT "COULD NOT BE READ" (SPEC.md 96.8.1) -------------
    ; `OSAPI_FILE_READ` answers `FERR_BIG` for a file larger than the buffer
    ; it was handed, and decides it FROM THE DIRECTORY ENTRY before any data
    ; I/O - so it is not a read that went wrong, it is a program that does not
    ; fit in the arena this arm gives. `.read` answers the same code for a
    ; load module past the block, decided from the header before it reads.
    cmp ax, FERR_BIG
    mov al, DER_READ                ; (a `mov`: the flags are still the cmp's)
    jne .err
.nofit:
    mov al, DER_FIT
.err:
    stc
.out:
    pop es
    pop bp
    jmp dos_popb_5                  ; DI..BX: the shared epilogue (96.54.8),
                                    ; three bytes that put dos_cpm's `align 16`
                                    ; a paragraph lower
.whole:
    mov ax, [dos_ldpara]            ; the capacity is everything from the image
    sub ax, 16                      ; to the top of ITS block, in paragraphs...
    mov dx, 16
    mul dx                          ; ...as a 32-bit byte count in DX:AX, which
    mov cx, ax                      ; is what OSAPI_FILE_READ takes in DX:CX
    call dos_be_read                ; DX:AX = bytes read; BX is still 0
    jc .rerr
    mov [dos_imgsz], ax             ; the WHOLE 32-bit size: an .EXE may be
    mov [dos_imghi], dx             ; bigger than a segment and the .COM
    jmp short .out                  ; ceiling is the .COM path's business;
                                    ; CF=0 is still the read's
%endif                              ; DOS_EXTCORE


; =============================================================================
; THE .EXE LOADER (SPEC.md 96.8)
; =============================================================================
; MZ header fields, at the front of the file as it was read in.
MZ_CBLP     equ 0x02                ; bytes used in the last 512-byte page
MZ_CP       equ 0x04                ; pages, INCLUDING the header
MZ_CRLC     equ 0x06                ; relocation entries
MZ_CPARHDR  equ 0x08                ; header size in PARAGRAPHS
MZ_MINALLOC equ 0x0A                ; paragraphs wanted beyond the image
MZ_MAXALLOC equ 0x0C                ; ...and the most it can use
MZ_SS       equ 0x0E                ; initial SS, relative to the load segment
MZ_SP       equ 0x10
MZ_IP       equ 0x14
MZ_CS       equ 0x16                ; initial CS, likewise relative
MZ_LFARLC   equ 0x18                ; where the relocation table starts
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_mzmap:                          ; dos_exe_setup's: a cell, the header
                                    ; offset that fills it
    dw dos_exe_cs
    db MZ_CS
    dw dos_exe_ip
    db MZ_IP
    dw dos_exe_ss
    db MZ_SS
    dw dos_exe_sp
    db MZ_SP
    dw dos_exe_hpara
    db MZ_CPARHDR
    dw dos_exe_nrel
    db MZ_CRLC
    dw dos_exe_rloc
    db MZ_LFARLC
    dw dos_exe_minal
    db MZ_MINALLOC
DOS_MZMAP_N equ ($ - dos_mzmap) / 3

; -----------------------------------------------------------------------------
; dos_exe_setup - turn the loaded file into a running .EXE image
; in:  the whole file is in the arena at DOS_IMGP, [dos_imgsz]/[dos_imghi] its
;      bytes
; out: CF=0 and [dos_exe_cs]/[dos_exe_ip]/[dos_exe_ss]/[dos_exe_sp] set, the
;      image moved down to DOS_IMGP; CF=1 with AL = a DER_*
;
; THE ORDER IS RELOCATE, THEN MOVE, and it is the whole reason this needs no
; scratch buffer. The relocation table lives in the HEADER, which the move is
; about to overwrite - so a loader that moves first has to copy the table out
; and then carries a bound on how many entries it can hold. The final load
; segment is known before either step (it is the PSP plus 16 paragraphs, by
; DOS's own arithmetic), so the fixups can be applied to the image WHERE IT
; STILL SITS and the table is read in place. No copy, no cap.
; -----------------------------------------------------------------------------
dos_exe_setup:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    mov ax, [dos_ldpsp]
    add ax, 16                      ; the file, header and all
    mov es, ax
    mov [dos_exe_fseg], ax

    ; --- EVERY header field, read BEFORE anything overwrites it ------------
    ; The load segment is DOS_IMGP, which is where the file already sits - so
    ; the move that strips the header lands exactly on top of it. There is no
    ; copy of these four words afterwards and no "still there above": read
    ; them now or lose them.
    ; Eight words, by TABLE: a cell and the header offset that fills it, read
    ; through CS for dos_hook_vectors' reason (the table is in the core).
    mov si, dos_mzmap
    mov cx, DOS_MZMAP_N
    cld
.mzcopy:
    cs lodsw                        ; AX = the cell...
    xchg ax, di
    cs lodsb                        ; ...AL = the header offset, all below 80h
    cbw
    xchg ax, bx
    mov ax, [es:bx]
    mov [di], ax
    loop .mzcopy

    ; --- the image's size, in bytes then paragraphs ------------------------
    mov ax, [es:MZ_CP]              ; pages INCLUDING the header. A last-page
    or ax, ax                       ; count of 0 means the last page is FULL,
    jnz .pages                      ; which is the encoding everybody forgets
.tbad:
    jmp .bad                        ; (and the way out for the jump below)
.pages:
    dec ax
    mov cx, 512
    mul cx                          ; DX:AX = the whole pages' bytes
    mov bx, [es:MZ_CBLP]
    or bx, bx
    jnz .tail
    mov bx, 512
.tail:
    add ax, bx
    adc dx, 0                       ; DX:AX = the FILE's own idea of its length
    mov bx, [dos_exe_hpara]
    mov cl, 4
    shl bx, cl                      ; header bytes - a header over 4,095
    sub ax, bx                      ; paragraphs is not a thing that exists
    sbb dx, 0
    jc .tbad

    add ax, 15                      ; ...and in paragraphs, rounded up: a
    adc dx, 0                       ; 32-bit shift right by four
    mov cx, 4
.p2:
    shr dx, 1
    rcr ax, 1
    loop .p2
    or dx, dx                       ; a paragraph count past 16 bits is more
    jnz .bad                        ; than conventional memory can hold
    mov [dos_exe_ipara], ax

    ; --- does the arena hold PSP + image + minalloc? -----------------------
    mov bx, ax
    add bx, [dos_exe_minal]
    jc .nofit
    add bx, 16
    jc .nofit
    mov ax, [dos_ldpara]            ; the program's block, in paragraphs
    cmp ax, bx
    jb .nofit

    ; --- RELOCATE, in place, BEFORE the move -------------------------------
    ; The table lives in the header the move is about to destroy, and the
    ; final load segment is known already - so the fixups go on the image
    ; WHERE IT STILL SITS and the table is read in place. That is what spares
    ; this a scratch buffer and, with it, a cap on how many entries an .EXE
    ; may have.
    mov ax, [dos_ldpsp]
    add ax, 16                      ; == the file's base: DOS puts an .EXE
    mov [dos_exe_lseg], ax          ; image 16 paragraphs past the PSP, and
    mov bp, ax                      ; that is where dos_load put it

    mov cx, [dos_exe_nrel]
    jcxz .moved
    mov si, [dos_exe_rloc]
    mov dx, [dos_exe_fseg]
    add dx, [dos_exe_hpara]         ; where the image sits RIGHT NOW
.rel:
    mov di, [es:si]                 ; the entry: offset, then segment, both
    mov ax, [es:si+2]               ; relative to the load segment
    add ax, dx                      ; ...resolved against the image's CURRENT
    mov ds, ax                      ; base, which is what lets this run first
    add [di], bp                    ; THE FIXUP
    add si, 4
    loop .rel

.moved:
    ; --- ...and only NOW move the image down over the header ---------------
    push cs
    pop ds
    mov ax, [dos_exe_fseg]
    add ax, [dos_exe_hpara]
    mov dx, [dos_exe_lseg]
    mov cx, [dos_exe_ipara]
    call dos_movedown

    push cs                         ; dos_movedown spends DS and ES
    pop ds
    mov ax, [dos_exe_lseg]          ; CS and SS are RELATIVE to the load
    add [dos_exe_cs], ax            ; segment; IP and SP are absolute
    add [dos_exe_ss], ax
    mov byte [dos_isexe], 1

    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.nofit:
    mov al, DER_FIT                 ; **NOT DER_MEM** (SPEC.md 96.14.3): the
    jmp short .fail                 ; arena was got and this program wants more
                                    ; than it holds - image + MINALLOC + PSP -
                                    ; which is a fact about the FILE. Sharing
                                    ; one sentence with "the machine has no run
                                    ; big enough" is what made a field report
                                    ; unactionable
.bad:
    mov al, DER_BADEXE
.fail:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_movedown - copy CX paragraphs from AX:0 down to DX:0
; in:  AX = source segment, DX = destination segment (BELOW it), CX = paragraphs
; out: nothing; clobbers AX, CX, DX, SI, DI, DS, ES, flags
;
; SEGMENT-STEPPED, so an image bigger than 64KB moves without a 16-bit offset
; binding - mem_bcopy's argument one layer out (SPEC.md 66.4). Forward within
; each chunk is safe because the destination is strictly below the source.
; -----------------------------------------------------------------------------
dos_movedown:
    cld
.chunk:
    jcxz .done
    push cx
    cmp cx, 0x800                   ; 2,048 paragraphs = 32KB, so the word
    jbe .last                       ; count below cannot leave a word
    mov cx, 0x800
.last:
    mov ds, ax
    mov es, dx
    push cx
    xor si, si
    xor di, di
    shl cx, 1                       ; paragraphs -> words, 8 words a paragraph.
    shl cx, 1                       ; THREE SINGLE-BIT SHIFTS and not `mov cl,
    shl cx, 1                       ; 3 / shl cx, cl`: the count being shifted
    rep movsw                       ; IS CX, so loading CL destroys its low
    pop cx                          ; byte first. 64 paragraphs became 3, and
                                    ; 48 bytes of a 1KB image moved - which
                                    ; looks like a loader that placed the image
                                    ; wrong rather than one that truncated it
    add ax, cx                      ; ...and both segments step by what moved
    add dx, cx
    pop bx
    sub bx, cx
    mov cx, bx
    jmp short .chunk
.done:
    ret
%endif                              ; DOS_EXTCORE
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
%ifdef DOS_WTAIL
; -----------------------------------------------------------------------------
; dos_fsx_cone - Full Screen's fsx proc, RESIDENT (DOS-HEADLESS-PLAN 4.5)
;
; `dos_fsx_con` is the console's, so it is the tail's - and an OSAPI_FSX_RUN
; proc above I_SIZE is REFUSED by the kernel's fence (inst_pkg_fence), I_SIZE
; being the resident host's image + bss alone. So the slot is handed this,
; which is below it, and the jump is bracketed like every other way into the
; tail: a headless host never runs Full Screen, having no console to show.
; -----------------------------------------------------------------------------
dos_fsx_cone:
    WIN_ONLY
    jmp dos_fsx_con
    WIN_END
    ret

; =============================================================================
; THE HEADLESS HOST (SPEC.md 96.44.20, docs/plans/DOS-HEADLESS-PLAN.md 5)
;
; A .LNK saved with `Close when done` (ShowCommand = 7) is read by the LOADER,
; which marks part 1 - the window - lazy, so this region never holds it, and
; writes [dos_head]. What is left of a window is what OSAPI_FSX_RUN and
; OSAPI_WM_WAKE need one for: a frame and one line, `Starting NAME`, drawn
; by the four routines below, which name nothing of the tail. The program
; runs in the bracket (or on the whole machine), and its end is a toast for a
; non-zero exit or a refusal, and the instance closing.
; =============================================================================
; dos_fld_def - the settings' defaults: the arm, the cache's dial and the
; Hard Drives box. Everything else's default is the zero the bss already
; holds. dos_entry calls it on BOTH hosts, before the window's dos_fld_init
; (whose field blocks are the tail's) - one copy of the three stores
dos_fld_def:
    mov word [dos_keepc], DOS_MEM_IN
    mov word [dos_cache], DOS_CA_AUTO
    mov byte [dos_mhdd + OS88UI_CK_ON], 1
    ret

; dos_hpaint - W_PAINT: `Starting NAME` and the ground beside it, each pixel
; once (SPEC.md 6.1) - two opaque runs, the name's on the cell grid the first
; leaves, and one fill to the right edge. The content is exactly the line's
; height (dos_htpl), so there is no ground under it. SI = the window, the gfx
; lock held
dos_hpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call OSAPI_WM_GEOM              ; CX = the content's width
    mov di, cx
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    add di, ax
    dec di                          ; DI = the right edge
    mov cx, ax
    mov si, dos_hs_start
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    add cx, 8 * (dos_hs_end - dos_hs_start)
    mov si, dos_name
    call OSAPI_FONT_RUN
    call OSAPI_FONT_WIDTH           ; AX = the name's pixels
    add ax, cx                      ; AX = past the text
    cmp ax, di
    ja .out
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    mov bx, dx                      ; BX = top
    add dx, 7                       ; DX = the line's bottom
    mov cx, di
    call OSAPI_GFX_FILL             ; ...beside the line
.out:
    jmp dos_hpop_6                  ; the host's shared epilogue (96.54.8)

; dos_hcat - append DS:SI's NUL string at ES:DI = DS:DI, NUL included; DI is
; left ON the NUL, so the next one overwrites it
dos_hcat:
    lodsb
    stosb
    or al, al
    jnz dos_hcat
    dec di
    ret

; dos_hq - ZF = 0 headless (DOS-HEADLESS-PLAN 4.4): every WIN_ONLY bracket's
; test, and tools/doswtail.py finds the brackets by their calls to it
dos_hq:
    cmp byte [dos_head], 0
    ret

; dos_endk / dos_endr - a run is over, from dos_wake's side (the program came
; back to a wake: the bracket's resume, kern_dos's return) and from dos_run's.
; Each is one bracket: the window's own end is the tail's (dos_endk_w,
; dos_endr_w), a headless host's is dos_hend, which both fall into
dos_endk:
    WIN_ONLY
    jmp dos_endk_w
    WIN_END
dos_endr:
    WIN_ONLY
    jmp dos_endr_w
    WIN_END
; dos_hend - a headless run is over: say so if it is worth saying, and go.
; A toast is COPIED by the kernel, so the instance may close while it is up
; (OSAPI_TOAST); OSAPI_WM_CLOSE is deferred to the next UI pass, so this
; returns and draws nothing after it. The line is built in dos_lbuf: the
; .LNK's buffer, which a headless host is done with once dos_entry has read
; the shortcut (a Save Shortcut is a window's)
dos_hend:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, dos_lbuf
    cmp byte [dos_state], DST_ERR
    jne .ran
    xor bx, bx                      ; a refusal's short form, words FIRST so
    mov bl, [dos_err]               ; the 24 cells the bar shows lose the name
    cmp bl, DOS_HERN                ; and not the reason. Past the list: the
    jb .ix                          ; first reads as well as any (nothing sets
    xor bl, bl                      ; one today)
.ix:
    mov bl, [dos_hero + bx]
    lea si, [dos_hers + bx]
    call dos_hcat
    jmp short .nm
.ran:
    mov al, [dos_exit]
    or al, al
    jz .close                       ; exit 0 is not news
    mov si, dos_hs_exit
    call dos_hcat                   ; (lodsb: AL is spent)
    mov al, [dos_exit]
    mov bl, 10
    xor cx, cx
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
.d:                                 ; the code's digits, least first, onto the
    xor ah, ah                      ; stack - no leading zeros
    div bl
    push ax                         ; AH = the digit
    inc cx
    or al, al
    jnz .d
.e:
    pop ax
    mov al, ah
    add al, '0'
    stosb
    loop .e
.nm:
    mov si, dos_hs_sep
    call dos_hcat
    mov si, dos_name
    call dos_hcat
    mov si, dos_lbuf
    xor cx, cx
    call OSAPI_TOAST
.close:
    mov bx, [dos_win]
    call OSAPI_WM_CLOSE
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)

dos_htpl:
    dw 0, MBAR_H, 8 * DOS_HTXT, 8 + TITLE_H + 1
    dw dos_ttl, dos_hpaint, 0, 0
dos_ttl:      db 'DOS', 0           ; ...the window's caption too: resident for
                                    ; both templates, and the menu bar's head
dos_hs_start: db 'Starting '
dos_hs_end:   db 0
dos_hs_exit:  db 'Exit ', 0
dos_hs_sep:   db ': ', 0
dos_hero:                           ; DER_* in order: each one's short form,
    db dos_her0 - dos_hers          ; a toast's head, as an offset into the
    db dos_her1 - dos_hers          ; list - which holds each form ONCE
    db dos_her2 - dos_hers
    db dos_her3 - dos_hers
    db dos_her4 - dos_hers          ; DER_FSX: the screen was in use
    db dos_her5 - dos_hers          ; DER_EXE
    db dos_her5 - dos_hers          ; DER_BADEXE
    db dos_her1 - dos_hers          ; DER_FIT
    db dos_her8 - dos_hers          ; DER_HAND
    db dos_her4 - dos_hers          ; DER_DVWP
DOS_HERN    equ $ - dos_hero
dos_hers:
dos_her0:     db 'No folder', 0
dos_her1:     db 'No memory', 0
dos_her2:     db 'Read error', 0
dos_her3:     db 'Too large', 0
dos_her4:     db 'Refused', 0
dos_her5:     db 'Bad .EXE', 0
dos_her8:     db 'Busy', 0
%endif

; =============================================================================
; THE BRACKET (SPEC.md 96.2)
; =============================================================================
; -----------------------------------------------------------------------------
; dos_fsx_main - the fullscreen bracket's body
; in:  SI = our window ptr, DS = CS = our segment, ES = KERNEL_SEG, task 0,
;      the gfx lock HELD; a near proc with a near ret (SPEC.md 53.1)
; out: nothing
;
; THE FSX_MODE CALL IS NOT OPTIONAL. fsx_restore skips vid_setmode when no
; mode was ever set (SPEC.md 53.6), and a DOS program sets its own through
; the ROM behind our back - so without this the desktop comes back into
; whatever mode the program left it in.
; -----------------------------------------------------------------------------
dos_fsx_main:
    ; STKBALANCE-OK: the `retf` below is a JUMP INTO THE PROGRAM and not a
    ; return - the two words under it are the far address of PSP:0000 that a
    ; .COM is entered with, placed on the PROGRAM's stack and consumed by the
    ; program's own exit. Control comes back to dos_prog_done, which is
    ; INSIDE this routine, after dos_terminate has restored SS:SP; so the
    ; `ret` that ends it really is at entry depth, and the walker is counting
    ; a frame that belongs to a different stack.
    push ds
    pop es                          ; FSI is ours, and OSAPI_FSX_MODE takes
    mov di, dos_fsi                 ; ES:DI like every other buffer slot
    mov al, FSXM_TEXT80
    call OSAPI_FSX_MODE
                                    ; a refusal is survivable: the screen is
                                    ; already the desktop's mode and the
                                    ; program will draw on it. Not worth
                                    ; abandoning the run for

    jc .noseed                      ; --- THE CONSOLE ONTO THE PROGRAM'S
                                    ; SCREEN (SPEC.md 96.34.4). The mode set
                                    ; CLEARED, so without this a program starts
                                    ; on a blank screen and the command line
                                    ; that launched it is not above its output
    WIN_ONLY                        ; the console onto the screen is the
                                    ; window's: a headless run starts clean
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    mov bx, [dos_win]
    call OSAPI_FSX_CAPS             ; DL = the DISPLAY's own kind, which
    mov [con_tkind], dl             ; osapi_video cannot answer for a window
    mov ax, [dos_fsi + FSI_SEG]     ; that is not on the primary (53.7.1)
    mov [con_tseg], ax
    mov byte [con_tcur], 0xFF       ; dos_fsx_con's reason (96.33.5)
    call con_tx_ice
    cmp word [con_cx], 0            ; **A FRESH LINE FIRST** (96.34.4): a shell
    je .seedy                       ; echoes the newline you pressed, and a
    mov al, 13                      ; DOUBLE-CLICK leaves the cursor at the end
    call con_write                  ; of the idle prompt - so without this the
    mov al, 10                      ; program's first line lands ON `A:\>`
    call con_write
.seedy:
    call con_markall
    call dos_fsx_owed               ; ...the same renderer Full Screen uses
    mov dh, [con_cy]                ; AND THE ROM'S OWN CURSOR WITH IT: the
    mov dl, [con_cx]                ; teletype reads 0040:0050, so a program's
    xor bh, bh                      ; first write would otherwise land on row 0
    mov ah, 0x02                    ; and overwrite the history just painted
    int 0x10
%endif
    WIN_END
.noseed:

    call OSAPI_VIDEO                ; AX = width, BX = height: INT 33h's scale
    mov [dos_vw], ax                ; (SPEC.md 96.10). Asked ONCE, here, and
    mov [dos_vh], bx                ; not per call - it cannot change inside a
                                    ; bracket and a divide is 80+ clocks
    call dos_m33_wall               ; ...and the INT 33h window opens onto all
                                    ; of it (96.10.7), for a program that polls
                                    ; 03h without resetting first

                                    ; (the drivers are already out: dos_run
                                    ; took them before the arena, on every arm
                                    ; - SPEC.md 96.35 - and the environment
                                    ; below reads the BLASTER= they left)
    call dos_save_machine
    call dos_build_psp
    call dos_hook_vectors
%ifndef KD_BACKEND
    call dos_pkt_start              ; ...AND THE PACKET DRIVER (SPEC.md 96.23),
                                    ; after dos_hook_vectors because it takes a
                                    ; vector of its own and after
                                    ; dos_save_machine because the whole IVT is
                                    ; banked by then - so the unhook is the
                                    ; restore, the way every other vector's is
%endif

    mov ax, [dos_arena]             ; the DTA starts at PSP:0080, which is the
    add ax, DOS_PSPP                ; command tail's own 128 bytes - DOS puts
    mov [dos_dtaseg], ax            ; it there and a program that never calls
    mov word [dos_dta], 0x80        ; AH=1Ah relies on it
    mov ax, [dos_dir]               ; ...and we start where the launch put us.
    mov [dos_curdir], ax            ; NOT a root: a program launched from a
    mov al, [dos_vol]               ; subdirectory can walk out of it, like it
    call dos_drv_bank               ; would under DOS (SPEC.md 96.6.1)
    call dos_date_init              ; the RTC once, or the kernel's fallback

    ; --- into the program --------------------------------------------------
    ; SS:SP is banked in OUR segment, reached through CS by the INT 21h
    ; terminate path, which runs on the program's stack with DS unknown.
    mov ax, ss
    mov [dos_sv_ss], ax
    mov [dos_sv_sp], sp
    mov [dos_dstk], sp              ; ...and the gate's own mark beside it
                                    ; (SPEC.md 96.7.2). dos_prog_enter sets it
                                    ; exactly a moment later, so this is the
                                    ; INVARIANT rather than the value: every
                                    ; program's SS takes the swap, and a zero
                                    ; here would put SP at the top of the
                                    ; segment

    call dos_prog_enter             ; ...and away (SPEC.md 96.14): the same
                                    ; door AH=4Bh's child goes through
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_prog_done:                      ; the INT 21h terminate path jumps here,
                                    ; having already put SS:SP back
    call dos_fh_sweep               ; EVERY HANDLE CLOSED, as DOS closes them
                                    ; for a terminating process (SPEC.md 96.52)
    cmp word [dos_hkv + DHK_SNAP], 0 ; **THE LAST SCREEN FIRST** (SPEC.md 96.34):
    je .nosnap                       ; the BDA's mode byte and cursor are the
    call word [dos_hkv + DHK_SNAP]   ; PROGRAM's until dos_restore_machine runs,
.nosnap:                             ; and the regen buffer still holds its text
    call dos_unhook_vectors
    call dos_restore_machine
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; =============================================================================
; THE MACHINE-STATE LEDGER (SPEC.md 96.5)
; =============================================================================
; The saves land in OUR OWN BSS and never in the arena: the arena is the
; program's to scribble on, and a save area the program can corrupt is worse
; than none, because it fails at restore time when nothing can be done.
; -----------------------------------------------------------------------------
dos_save_machine:
    push ax
    push cx
    push si
    push di
    push ds
    push es

    cld
    xor ax, ax                      ; the whole IVT. A list of vectors to
    mov ds, ax                      ; remember would be wrong for the ones we
    push cs                         ; do not know about, and a DOS program
    pop es                          ; hooks vectors as a matter of routine
    xor si, si
    mov di, dos_ivt
    mov cx, 512
    rep movsw

    mov ax, 0x40                    ; ...and the whole BDA, saved so the list
    mov ds, ax                      ; that comes BACK can be audited against
    xor si, si                      ; what actually changed
    mov di, dos_bda
    mov cx, 128
    rep movsw

    ; --- THE VECTORS WE DO NOT PROVIDE, HONESTLY NULL (SPEC.md 96.5.1) -----
    ; Banking the whole table is right for everything the machine really
    ; answers, and wrong for one thing: a vector os8088 has NEVER installed
    ; still holds whatever the boot left in it, which is a pointer into the
    ; HEAP. A DOS program asks "is there a mouse driver?" by reading INT 33h
    ; and testing it for non-null - so a stale pointer answers YES, and the
    ; program then CALLS it, into our heap, at whatever that memory happens
    ; to be. NULL is the truthful answer and the one DOS gives on a machine
    ; with no driver loaded. The bank above already holds the old value, so
    ; the restore puts it back untouched.
    xor ax, ax
    mov ds, ax
    mov [0x33*4], ax
    mov [0x33*4+2], ax

    push cs
    pop ds
    in al, 0x21                     ; the 8259 masks: a program that masks IRQs
    mov [dos_pic1], al              ; and does not put them back leaves us with
    in al, 0xA1                     ; no timer
    mov [dos_pic2], al

    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_restore_machine - the named list, and the four bytes that are ZEROED
; -----------------------------------------------------------------------------
dos_restore_machine:
    push ax
    push cx
    push dx
    push si
    push di
    push ds
    push es

    cld
    push cs
    pop ds
    mov al, [dos_pic1]              ; the masks first: everything below runs
    out 0x21, al                    ; with the machine's own interrupt set back
    mov al, [dos_pic2]
    out 0xA1, al

    ; --- PIT channel 0 back to the kernel's, WHICH IS NOT THE ROM'S --------
    ; A program that wanted a fast timer took the scheduler's quantum with it,
    ; so the divisor has to come back - and it did. **THE MODE DID NOT**
    ; (SPEC.md 96.5.3): this wrote 0x36, which is the ROM's mode 3, where
    ; `sched_init` writes 0x34 for mode 2 and SPEC.md 8.1 says why - the IRQ
    ; rate is the same either way, but mode 3 decrements the counter by TWO
    ; and wraps it twice a period, so `65536 - count` stops being an elapsed
    ; time. That is what `sch_pit_now` and `sch_account` read: the Task
    ; Manager's CPU shares, the window animations' clock and the sound
    ; driver's note deadlines, all of them off a latch that no longer means
    ; what they think.
    ;
    ; It is ONE BYTE and it was never about the DOS handoff: this routine runs
    ; on every WINDOWED program's exit too, so an ordinary machine had been
    ; running its scheduler's clock in the wrong mode since the first DOS
    ; program anyone opened. The core is shared, so the same byte is what
    ; `kern_dos` writes on the way out of the whole-machine arm.
    mov al, 0x34                    ; ch0, lo/hi, MODE 2, and the divisor below
    out 0x43, al
    xor al, al
    out 0x40, al
    out 0x40, al

    mov ax, 0x40                    ; --- the BDA's named list (SPEC.md 96.5) -
    mov es, ax
    mov si, dos_bdalist
.bda:
    mov di, [si]                    ; offset in the BDA, 0xFFFF ends the list
    cmp di, 0xFFFF
    je .bdadone
    mov cx, [si+2]                  ; bytes, always even
    add si, 4
    push si
    mov si, di
    add si, dos_bda                 ; ...from our own copy
    shr cx, 1
    rep movsw
    pop si
    jmp short .bda
.bdadone:
    xor ax, ax                      ; the keyboard flag bytes are ZEROED and
    mov [es:0x17], ax               ; not restored: they say which keys are
    mov [es:0x96], ax               ; HELD, and the honest answer on the way
                                    ; back is that none is. A restored phantom
                                    ; Ctrl makes every menu behave oddly until
                                    ; the user happens to press and release it,
                                    ; and a stuck ScrollLock silently disables
                                    ; the keypad-5 mouse hatch (SPEC.md 9.6.4)

    xor ax, ax                      ; --- and the IVT LAST, under cli ---------
    mov es, ax                      ; nothing below may take an interrupt
    mov si, dos_ivt                 ; through a half-restored table
    xor di, di
    mov cx, 512
    cli
    rep movsw
    sti

    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_hook_vectors / dos_unhook_vectors
;
; The unhook is a no-op by design: dos_restore_machine puts the WHOLE IVT
; back, which covers our own vectors and every one the program installed. It
; is a named step so the bracket reads in the order it happens.
; -----------------------------------------------------------------------------
dos_hook_vectors:
    push ax
    push bx
    push dx
    push si
    push es
    cld
    xor ax, ax
    mov es, ax
    cli
    ; --- ONE TABLE FOR EVERY VECTOR WE OWN (SPEC.md 96.5.2) -----------------
    ; It was eleven `mov word [es:n*4], handler / mov [es:n*4+2], cs` pairs -
    ; twelve bytes a vector - and a loop for the iret ones beside them. The
    ; table carries a vector a byte, with bit 7 set when a NEW handler word
    ; follows it, so a run of vectors that share one handler (the fourteen
    ; irets, 25h/26h) costs a byte each. The order is the order they were
    ; written in and none of it is visible: IF is 0 from the `cli` above to
    ; the `sti` below, so no interrupt can see a half-written table.
    ;
    ; CS AND NOT DS FOR THE TABLE. In the parted build (SPEC.md 96.44) this
    ; body is in the CORE and DS addresses the HOST's bss - `[dos_arena]`
    ; below is exactly that - so a table in our own image is reachable only
    ; through CS, which is the same segment either way.
    mov si, dos_ivtab
.hk:
    cs lodsb                            ; AL = a vector, 0 = the end
    or al, al
    jz .hkd
    jns .hkv                            ; bit 7 clear: the handler in DX
    and al, 0x7F
    push ax
    cs lodsw                            ; ...set: a new handler follows
    xchg ax, dx
    pop ax
.hkv:
    cbw                                 ; every vector here is below 80h
    xchg ax, bx
    shl bx, 1
    shl bx, 1
    mov [es:bx], dx
    mov [es:bx+2], cs
    jmp short .hk
.hkd:
    mov [dos_m33hk], al                 ; **A FRESH TABLE HOOKS NOTHING YET**
                                        ; (AL = 0, the table's own end)
                                        ; (SPEC.md 96.54.8): dos_m33_arm hooks
                                        ; IRQ0 once, on a program's first
                                        ; 0Ch, and the flag outlived the run
                                        ; that set it - so the NEXT run's
                                        ; handler was never hooked, in the
                                        ; bracket (whose restore puts the
                                        ; whole IVT back) and in a window
                                        ; alike. Cleared with the table it
                                        ; describes
    call dos_m33_hidden                 ; **AND THE CURSOR STARTS AWAY**
                                        ; (SPEC.md 96.10.5): the show counter
                                        ; is -1, which is not the zero a bss
                                        ; arrives as - 0 means VISIBLE, so a
                                        ; bracket that skipped this would put
                                        ; a cursor on the screen of a program
                                        ; that never asked for one
    sti

    mov ax, [dos_arena]                 ; ...and INT 12h's own source, so "how
    add ax, [dos_apara]                 ; much memory is there" agrees with the
    mov bx, 0x40                        ; PSP and the MCB chain (SPEC.md 96.3).
    mov es, bx                          ; Safe because the kernel reads int 12h
    mov cl, 6                           ; exactly twice in the tree - once at
    shr ax, cl                          ; boot, once in the Task Manager, which
    mov [es:0x13], ax                   ; cannot run inside a bracket
    pop es
    pop si
    pop dx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_unhook_vectors:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_iret:
    iret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_int24:                              ; DOS's critical-error contract: AL = 3
    mov al, 3                           ; is FAIL, which turns a dead drive into
    iret                                ; a failed call instead of an "Abort,
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
                                        ; Retry, Fail?" nobody can answer

; -----------------------------------------------------------------------------
; dos_int25 - INT 25h and INT 26h, ABSOLUTE DISK READ and WRITE, refused
;
; THE RETURN IS A `retf` AND THAT IS THE WHOLE POINT (SPEC.md 96.5.2.2). These
; two are the only INT 21h-era calls that do not `iret`: DOS leaves the FLAGS
; the `int` pushed ON THE STACK and the caller pops them itself, so a handler
; that irets here unbalances the caller's stack by two bytes - a fault that
; lands somewhere else entirely and looks like anything but this.
;
; A refusal rather than a body. The public disk surface of this DOS is file-
; and volume-level on purpose, and a program writing raw sectors underneath a
; mounted volume with a live cache is the one thing there is no safe answer
; to. An honest error is an answer a disk utility can print; address zero is
; not.
; -----------------------------------------------------------------------------
dos_int25:
    mov ax, 0x0C01                      ; AH = INT 24h's `general failure`,
    stc                                 ; AL = the device driver's `bad
    retf                                ; command`
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_int29 - INT 29h, FAST CONSOLE OUTPUT: AL is the character
;
; DOS's own CON driver writes through this, and so does any program that wants
; the cheapest documented way to put a character up. IBM DOS 3.30's is a BIOS
; teletype (`mov ah, 0Eh / int 10h`); ours goes to `dos_tty`, which is where
; AH=02h goes, so the two agree about where the console is. An `iret` here
; would have been silence rather than a crash, which is the harder bug.
;
; It preserves everything, as DOS's does.
; -----------------------------------------------------------------------------
dos_int29:
    push ax                             ; AL is the argument and `push` does
    push bx                             ; not touch it
    push cx
    push dx
    push si
    push di
    push es
    push ds
    push cs
    pop ds
    call dos_tty
    pop ds
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; Every vector DOS owns, and last the ones a real IBM DOS 3.30 fills with
; one `iret` - measured rather than listed out of a book.  Read through CS:
; see dos_hook_vectors.
dos_ivtab:
    ; WHAT EACH VECTOR WAS, AND WHY (SPEC.md 96.5.2):
    ;   20h  terminate, the CP/M door
    ;   21h  ...and the one that matters
    ;   22h  terminate address - a program may read it out of its own PSP
    ;   24h  critical error: FAIL, never retry
    ;   2Fh  the MULTIPLEX interrupt, which is how a program finds XMS (SPEC.md
    ;        96.15) - and, unhooked, is how it finds whatever the ROM left there
    ;   33h  ...and the MOUSE (SPEC.md 96.10), which costs us a translation and
    ;        not a driver: the kernel's own ISR keeps mouse_x/y/btn fresh for
    ;        the whole bracket (SPEC.md 53.1)
    ;   25h, 26h  ABSOLUTE DISK READ and WRITE: a refusal, and refusing is
    ;        exactly why they cannot be `dos_iret` - both return with the
    ;        FLAGS still pushed
    ;   27h  TSR: nothing can stay resident past the bracket, so it is a
    ;        terminate - which is what DOS's own 27h is, a jump to AH=31h
    ;   29h  FAST CONSOLE OUTPUT, which DOS's own CON driver writes through
    ;   23h  Ctrl-Break, and then THE REST OF THE BLOCK A REAL DOS OWNS.
    ;        Measured on IBM DOS 3.30: 28h, 2Ah..2Eh, 32h and 34h..3Eh all
    ;        point at ONE `iret` inside IBMDOS. The eleven from 34h up are the
    ;        8087 EMULATOR's, which every Borland- and Microsoft-compiled
    ;        program reads before installing its own. 2Eh is COMMAND.COM's
    ;        undocumented back door rather than an iret, and an iret is the
    ;        safe approximation: it does nothing where DOS would run a
    ;        command, which beats running the vector table as code. Left at
    ;        0000:0000 they are not `unimplemented`, they are A JUMP TO
    ;        ADDRESS ZERO
    db 0x80 | 0x20
    dw dos_int20
    db 0x80 | 0x21
    dw dos_int21
    db 0x80 | 0x22
    dw dos_int22
    db 0x80 | 0x24
    dw dos_int24
    db 0x80 | 0x2F
    dw dos_int2f
    db 0x80 | 0x33
    dw dos_int33
    db 0x80 | 0x25
    dw dos_int25
    db 0x26
    db 0x80 | 0x27
    dw dos_int20
    db 0x80 | 0x29
    dw dos_int29
    db 0x80 | 0x23
    dw dos_iret
    db 0x28                             ; DOS idle
    db 0x2A                             ; network / critical section - the one
                                        ; BOLOBALL asks (SPEC.md 96.5.2.1)
    db 0x2B, 0x2C, 0x2D                 ; DOS reserved
    db 0x2E                             ; COMMAND.COM's back door
    db 0x32                             ; reserved
    db 0x34, 0x35, 0x36, 0x37, 0x38     ; the 8087 emulator's eleven
    db 0x39, 0x3A, 0x3B, 0x3C, 0x3D
    db 0x3E
    db 0

; =============================================================================
; THE ARENA (SPEC.md 96.3)
; =============================================================================
; -----------------------------------------------------------------------------
; dos_build_psp - the MCB chain, the environment and the PSP
; in:  [dos_arena], [dos_apara], [dos_imgsz]
; out: [dos_prgsp] = the program's initial SP; preserves nothing but segments
;
; Two blocks, because a .COM is given everything: an 'M' for the environment
; and a 'Z' for the program, which runs to the top of the claim. PSP:0002 is
; the paragraph past it, which is the first of the four ways a DOS program
; asks how much memory it has (docs/plans/DOS-EXEC-PLAN.md 2.1).
; -----------------------------------------------------------------------------
dos_build_psp:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cld
    mov dx, [dos_arena]

    ; --- the environment's MCB, and the environment --------------------------
    mov ax, dx
    mov es, ax
    xor di, di
    mov al, 'M'                     ; 'M' = a block with another after it
    stosb
    mov ax, dx
    add ax, DOS_PSPP
    stosw                           ; owner: the PSP that owns it
    mov ax, DOS_ENVP
    stosw                           ; size, in paragraphs
    mov cx, 11                      ; the three reserved bytes and the 8-byte
    xor al, al                      ; name field DOS 4 added - zeroed, which is
    rep stosb                       ; what a block with no name looks like

    mov ax, dx                      ; the environment itself: the variables,
    add ax, DOS_ENVSEG              ; then a NUL to end the set, then the count
    mov es, ax                      ; word and the program's own path - which
    xor di, di                      ; is what DOS 3+ puts there and what a
    cld                             ; program looks for when it wants to know
                                    ; where it came from
    cmp byte [dos_blaster], 0       ; BLASTER= is the one variable the MACHINE
    je .envuser                     ; contributes (SPEC.md 96.17), and it is
    mov si, dos_blaster             ; here only when a sound driver was
.envb:                              ; unloaded a moment ago and told us where
    lodsb                           ; its card was
    stosb
    or al, al
    jnz .envb
.envuser:
    ; --- and whatever the user typed (SPEC.md 96.20) -------------------------
    ; TWO ROWS ARE SKIPPED RATHER THAN EMITTED, and each would break the set
    ; in a different way:
    ;   EMPTY  - a bare NUL is what ENDS the environment, so four rows with
    ;            the second blank would hide the third and fourth from every
    ;            program that reads it.
    ;   NO '=' - DOS's own parser splits on it, so a row without one is a
    ;            variable with no name and nothing could ever look it up.
    push cx
    mov word [dos_erp], dos_ebuf
    mov cx, DOS_ENVN
.envrow:
    mov si, [dos_erp]
    cmp byte [si], 0
    je .envnext                     ; empty
    call dos_has_eq
    jc .envnext                     ; no '='
    mov si, [dos_erp]
.envcp:
    lodsb
    stosb
    or al, al
    jnz .envcp
.envnext:
    add word [dos_erp], DOS_ENVBUF
    loop .envrow
    pop cx
.envend:
    ; --- ...AND NEVER AN EMPTY SET (SPEC.md 96.44.13.1) ----------------------
    ; **A REAL DOS HAS NO SUCH THING**: COMMAND.COM puts `COMSPEC=` in every
    ; environment it hands out, so the first byte a program reads there is a
    ; letter and never the set's own terminator. This DOS's set is BLASTER=
    ; plus whatever the user typed, and on a machine with no sound card and an
    ; empty Environment page that is NOTHING - a block that begins with the NUL
    ; that ends it.
    ;
    ; That is well formed and it stops a program dead, because the rows are the
    ; ROAD to the program's own path: DOS 3 puts the path after the set's
    ; terminating NUL and a count word (96.19.3), so a program WALKS the
    ; variables to reach it. Prince of Persia's walk is `cmp byte [es:0],0 /
    ; jz skip`, and it then cannot tell which directory it came from.
    ;
    ; So a set that would be empty gets `PATH=` instead. It is the honest row
    ; to pick: DOS always has one, an EMPTY value is a true statement about
    ; this machine, and nothing will try to execute it - where a `COMSPEC=`
    ; pointing at a COMMAND.COM that is not on any disk here invites a program
    ; to shell out and fail somewhere further away.
    or di, di                       ; DI is the write cursor and the set began
    jnz .envend2                    ; at ZERO, so nothing written = an empty set
    mov si, dos_s_epath
.envp:
    lodsb
    stosb
    or al, al
    jnz .envp
.envend2:
    xor al, al
    stosb                           ; ...and the NUL that ends the SET
    mov ax, 1
    stosw
    call dos_envpath                ; ...and the program's own PATH, which is
    mov si, dos_pbuf                ; a real one since SPEC.md 19.2.4 - it was
.env:                               ; a bare 8.3 name while no package could
    lodsb                           ; name the folder it was launched from
    stosb
    or al, al
    jnz .env

    ; --- the program's MCB ---------------------------------------------------
    mov ax, dx
    add ax, DOS_PRGMCB
    mov es, ax
    xor di, di
    mov al, 'Z'                     ; 'Z' = the last block in the chain
    stosb
    mov ax, dx
    add ax, DOS_PSPP
    stosw
    mov ax, [dos_apara]
    sub ax, DOS_PSPP                ; everything from the PSP to the top
    stosw
    mov cx, 11
    xor al, al
    rep stosb

    ; --- the PSP -------------------------------------------------------------
    call dos_psp_make               ; ...which is a routine of its own, because
                                    ; AH=4Bh's child needs one too (SPEC.md
                                    ; 96.14) and it is not at the arena's base
    jmp dos_popa_7                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fcb_arg - the next word of the tail, parsed into one of the PSP's FCBs
; in:  DS:SI = the tail (ours), ES = the PSP, DI = 5Ch or 6Ch
; out: SI past the word; every other register preserved
;
; AH=29h's own parse (SPEC.md 96.28), blanks skipped first as COMMAND asks it
; to: `FOO.TXT Z:BAR` is drive 0 and FOO TXT, then drive 26 and BAR - and
; nothing at all is drive 0 and eleven blanks, the shape 96.21.6 is about.
; -----------------------------------------------------------------------------
dos_fcb_arg:
    push ax
    push bx
    push cx
    push di
.sk:
    lodsb
    cmp al, ' '
    je .sk
    cmp al, 9
    je .sk
    dec si
    mov di, dos_pfcb
    call dos_int21.pf_core
    pop di
    push di
    push si
    mov si, dos_pfcb
    mov cx, 12
    cld
    rep movsb
    pop si
    pop di
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_jft_sync - the PSP's job file table, rewritten from our own handle table
;
; 0FFh is FREE and anything else is an index into the open-file table DOS keeps
; and we do not, so an open handle publishes its OWN NUMBER - the one thing
; about it that is certainly true (SPEC.md 96.21.4). All twenty are rewritten
; rather than poked one at a time, because a derived table that is only
; corrected where somebody remembered to correct it goes stale, and a stale
; 0FFh on a handle the program is holding is a worse answer than the zero this
; replaces.
; -----------------------------------------------------------------------------
dos_jft_sync:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov es, [dos_ldpsp]
    mov si, dos_fhtab
    mov di, 0x18 + DOS_FH0
    mov cx, DOS_NFH
    mov bl, DOS_FH0
.one:
    mov al, 0xFF
    test byte [si+FH_FLAGS], FHF_USED
    jz .put
    mov al, bl
.put:
    mov [es:di], al
    inc di
    inc bl
    add si, FH_SIZEOF
    loop .one
    pop es
    pop di
    pop si
    jmp dos_popa_3                  ; the shared epilogue (96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_psp_make - a PSP at [dos_ldpsp], for a block of [dos_ldpara] paragraphs
; in:  [dos_ldpsp], [dos_ldpara], [dos_parent] (0 = nobody)
; out: [dos_prgsp] = the .COM stack offset; every register preserved
; -----------------------------------------------------------------------------
dos_psp_make:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    mov ax, [dos_ldpsp]
    mov es, ax
    xor di, di
    mov cx, 128                     ; zero it first: every field this does not
    xor ax, ax                      ; set is a field a program may read, and
    cld
    rep stosw                       ; zero is the answer DOS leaves in most
    xor di, di                      ; ...and DI is the PSP's base for the
                                    ; stores below: `[es:di+n]` is a byte
                                    ; shorter than `[es:n]` for every one
                                    ; that is not AX (SPEC.md 96.21.4's
                                    ; fields, in the same order as before)
    mov word [es:di+0x00], 0x20CD   ; INT 20h, so a .COM that plain `ret`s
                                    ; lands here and terminates
    mov ax, es                      ; = [dos_ldpsp], a byte shorter
    add ax, [dos_ldpara]
    mov [es:0x02], ax               ; the paragraph past the block - mechanism 1

    ; --- the CP/M block at +05, WHOSE ADDRESS IS ALSO A FIELD ---------------
    ; The five bytes are a far call, and the word inside it is published in
    ; its own right: [PSP:0006] is "how many bytes are there in this segment",
    ; which is one of the four ways a DOS program asks how much memory it has
    ; (SPEC.md 96.21.4) and the only one that costs it no call at all. Writing
    ; the 9Ah and leaving the address zero - which is what this did - says
    ; ZERO BYTES AVAILABLE and points the call at 0000:0000.
    ;
    ; DOS picks the SEGMENT half so that segment:size addresses its own
    ; dispatcher, which is what lets one five-byte field carry two answers. We
    ; have a dispatcher at PSP:0050 - the `int 21h`/`retf` gate four lines down
    ; - so the segment is (PSP + 5) - size/16 and the call lands on it exactly.
    ; MEASURED against IBM DOS 3.30: 0FEF0h for a block of 64KB or more
    ; (SPEC.md 96.21.4).
    mov byte [es:di+0x05], 0x9A
    mov ax, [dos_ldpara]
    cmp ax, 0x20                    ; a block too small to hold a PSP and the
    jb .cpmnone                     ; field's own bias cannot answer at all
    cmp ax, 0x1000
    jb .cpmhave
    mov ax, 0x1000                  ; a SEGMENT is 64KB however big the block
.cpmhave:
    mov cl, 4
    shl ax, cl                      ; paragraphs -> bytes, and 1000h shifted is
    sub ax, 0x110                   ; 0 - which is the wrap that MAKES it 0FEF0h
    mov bx, ax
    shr bx, cl                      ; (CL is still 4)
    mov [es:0x06], ax
    ; A REAL CP/M ENTRY, ON EVERY HOST (KERN-MEGA-DOS-PLAN 10.5 defect 3 and
    ; its owner's question). PSP:0050 is the DOS 2+ far gate (`int 21h /
    ; retf`), which takes AH where CP/M passes CL and returns FAR to PSP:000A,
    ; a data field - so a CALL 5 aimed at it never served CP/M on any kernel.
    ; dos_cpm below is paragraph-aligned, so it is CS:dos_cpm written with the
    ; size field as the offset: segment = CS + dos_cpm/16 - offset/16.
    ;   * It fits a word: the windowed DOS (a heap region, above 64KB).
    ;   * It goes below 0: kern_dos low in memory. The segment wraps, and the
    ;     call reaches dos_cpm through the 1MB wrap - which is where the old
    ;     field's own wrap took a low PSP, and how IBM DOS 3.30's CALL 5 works.
    ;   * It goes above FFFFh: kern_mega's HMA run, where CS = FFFFh. A block
    ;     whose offset reaches back far enough still fits; a smaller one keeps
    ;     the old field and its wrap, the one case the HMA cannot express.
    mov dx, dos_cpm
    shr dx, cl                      ; (CL is still 4)
    mov ax, cs
    add ax, dx
    jc .cpmhi                       ; CS + dos_cpm/16 past FFFFh
    sub ax, bx                      ; (a borrow is the wrap, and is taken)
    jmp short .cpmset
.cpmhi:
    sub ax, bx
    jc .cpmset                      ; borrowed back under 10000h: it fits
    mov ax, es                      ; ...and otherwise the old field
    add ax, 5
    sub ax, bx
.cpmset:
    mov [es:0x08], ax
.cpmnone:
%ifndef DOS_MEGA
    mov word [es:di+0x0A], dos_int22 ; the terminate address, which DOS copies
    mov [es:di+0x0C], cs            ; out of the vectors it is about to hook
    mov word [es:di+0x0E], dos_iret
    mov [es:di+0x10], cs
    mov word [es:di+0x12], dos_int24
    mov [es:di+0x14], cs
%else                               ; ...and on the HMA run a CHILD's exit
    mov ax, dos_int22               ; restores 22h-24h from these, so they are
    call dos_hvec                   ; slots too (dos_hvec)
    mov [es:di+0x0A], ax
    mov [es:di+0x0C], dx
    mov ax, dos_iret
    call dos_hvec
    mov [es:di+0x0E], ax
    mov [es:di+0x10], dx
    mov ax, dos_int24
    call dos_hvec
    mov [es:di+0x12], ax
    mov [es:di+0x14], dx
%endif
    mov ax, [dos_parent]            ; the PARENT's PSP, which AH=4Bh's child
    or ax, ax                       ; reads to find who launched it
    jnz .haveparent
    mov ax, [dos_ldpsp]             ; A PROGRAM WITH NO PARENT IS ITS OWN, which
.haveparent:                        ; is what DOS does for COMMAND.COM and what
    mov [es:0x16], ax               ; makes a walk up the chain TERMINATE. Zero
                                    ; does not: a walker that follows it reads
                                    ; the interrupt vector table as a PSP

    ; --- the job file table, and the two words that point at it -------------
    ; The twenty bytes at PSP:0018 are how a program asks whether a handle is
    ; open without making a call (SPEC.md 96.21.4). 0FFh is FREE and anything
    ; else is an index into the open-file table DOS keeps - so the zeroes the
    ; wipe above leaves say all twenty are OPEN and that they all share one
    ; file. The five devices take the indices IBM DOS 3.30 gives them,
    ; measured; every other entry starts free and dos_jft_sync keeps it true.
    mov di, 0x18
    mov cx, 20
    mov al, 0xFF
    rep stosb                       ; ...which leaves DI at 2Ch: the base for
                                    ; every store below, a byte shorter each
    mov word [es:di-0x2C+0x18], 0x0101  ; 0, 1: stdin and stdout, the console
    mov word [es:di-0x2C+0x1A], 0x0001  ; 2: stderr, the same device, and
                                        ; 3: AUX
    mov byte [es:di-0x2C+0x1C], 0x02    ; 4: PRN
    mov word [es:di-0x2C+0x32], 20      ; ...and its size and address, which is
    mov word [es:di-0x2C+0x34], 0x0018  ; how a program with more than twenty
    mov ax, [dos_ldpsp]                 ; files open finds the table that
    mov [es:di-0x2C+0x36], ax           ; replaced it
    mov ax, 0xFFFF                      ; the previous PSP: DOS 3 leaves
    mov [es:di-0x2C+0x38], ax           ; FFFF:FFFF and a program may test for it
    mov [es:di-0x2C+0x3A], ax
    mov ax, [dos_arena]
    inc ax
    mov [es:di-0x2C+0x2C], ax       ; ...and one environment, shared: a child
                                    ; inherits the parent's, which is the
                                    ; default AH=4Bh's block asks for with a 0
    mov word [es:di-0x2C+0x50], 0x21CD  ; INT 21h / RETF, the DOS 2+ call gate
    mov byte [es:di-0x2C+0x52], 0xCB
    call dos_psp_tail               ; THE ARGUMENTS (SPEC.md 96.19), or the
                                    ; empty tail this used to write flat
    mov si, dos_args                ; ...and the two FCBs: the first two words
    mov di, 0x5C                    ; of the tail PARSED into them, as COMMAND
    call dos_fcb_arg                ; does (SPEC.md 96.56.3) - and an empty
    mov di, 0x6C                    ; tail parses to drive 0 and eleven
    call dos_fcb_arg                ; blanks, not to zero (96.21.6). An EXEC's
                                    ; child takes its block's instead

    ; --- the stack, AND ONLY A .COM HAS ONE HERE (SPEC.md 96.3.1) ------------
    ; An .EXE brings its own SS:SP out of its header and DOS does not touch
    ; it. This ran for both kinds, and for an .EXE `PSP:FFFC` is not a stack
    ; top - it is **64KB into the program's own image**, so every .EXE bigger
    ; than that had two bytes of itself zeroed at load.
    ;
    ; Test Drive III is 137,845 bytes and the word landed in the middle of a
    ; routine: `mov [0B85Eh], bh` became `mov [0005Eh], bh`, which is two
    ; bytes shorter, so every instruction boundary after it moved - and three
    ; instructions later the 8086 met `C0`, an UNDOCUMENTED alias for `RET
    ; imm16`, which popped a byte pair as an address and added 2274h to SP.
    ; The program ran for two minutes before reaching that routine, and what
    ; the field saw was a freeze at the menu.
    cmp byte [dos_isexe], 0
    jne .nostk
    mov ax, [dos_ldpara]            ; a .COM gets SP at the top of its own
    cmp ax, 0x1000                  ; 64KB when the block holds one, and the
    jb .small                       ; top of the block when it does not
    mov bx, 0xFFFE
    jmp short .sp
.small:
    mov cl, 4
    shl ax, cl
    xchg ax, bx                     ; (AX is dead from here: one byte)
    dec bx
    dec bx
.sp:
    dec bx                          ; ...and the 0 word DOS pushes, which is
    dec bx
    mov [dos_prgsp], bx             ; the offset half of that PSP:0000 return
    mov word [es:bx], 0
.nostk:
    pop es
    pop di
    jmp dos_popa_4                  ; the shared epilogue (96.54.8)

%ifdef DOS_MEGA
; dos_hvec - AX = a handler in this segment: DX:AX = the far address to
; PUBLISH for it (KERN-MEGA-DOS-PLAN 10.5 defect 4). On the HMA run that is
; the host's low trampoline, through `dos_hslot`, so a vector, a PSP field or
; an entry point a program is handed re-proves A20 before it reaches FFFF;
; everywhere else it is CS:AX. DS = this segment; nothing else is touched.
dos_hvec:
    mov dx, cs
    cmp dx, 0xFFFF
    jne .r
    cmp word [dos_hslot], 0
    je .r
    call [dos_hslot]
.r:
    ret

%endif

; dos_cpm - CP/M's CALL 5 (KERN-MEGA-DOS-PLAN 10.5 defect 3), on every host:
; the far call dos_psp_make aims at it from PSP:5. Entered with CS = whatever
; segment that call reached it through and IP = the PSP's size field, so the
; body is POSITION-INDEPENDENT and names nothing by address. CL = the function
; (CP/M's 0..24h, which DOS's AH numbering shares); anything above answers
; AL = 0, as DOS does. On the stack are the far return the PSP:5 call pushed
; (PSP:000A) and, above it, the program's own NEAR return - so the way back is
; that near address under the PSP's CS: the far return's IP is overwritten
; with it, and `retf 2` drops it.
    align 16
dos_cpm:
    cmp cl, 0x24
    ja .none
    mov ah, cl
    int 0x21
    jmp short .ret
.none:
    xor al, al
.ret:
    push bp
    mov bp, sp                      ; [bp+2] IP 000Ah, [bp+4] the PSP,
    push ax                         ; [bp+6] the near return
    mov ax, [bp+6]
    mov [bp+2], ax
    pop ax
    pop bp
    retf 2
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_ldrd - paragraphs [DI, BP) of the program's file, to [dos_ldpsp]+16+DI
; in:  DI and BP whole sectors (DI <= BP), SI = the name, BX = 0
; out: CF=0 and DI = BP, or the file's end; CF=1 with AX = FERR_*. ES = the
;      last chunk's segment, [dos_ldpsp]+16+DI as it was - set before the read,
;      so on CF=1 too, and dos_load relies on the first one's
;
; dos_load's (SPEC.md 96.8.2), in chunks of 32KB at most so a chunk lands in
; one segment; a file that ends early ends the loop, as the whole read's short
; file always has. The caller bounds BP by the block. It is a routine of its
; own and not a local label so that it sits AFTER `dos_cpm`'s `align 16`: in
; front of it, its bytes were paid for again in padding.
; -----------------------------------------------------------------------------
dos_ldrd:
    mov cx, bp
    sub cx, di                      ; CF=0: DI <= BP
    jbe .ret
    cmp ch, 8                       ; 2,048 paragraphs at most
    jb .chunk
    mov cx, 0x800
.chunk:
    push cx
    lea ax, [di+16]                 ; the buffer is ES:0 a chunk at a time, so
    add ax, [dos_ldpsp]             ; no offset binds past 64KB
    mov es, ax
    mov ax, 16
    mul cx
    xchg ax, cx                     ; CX = the chunk in bytes, <= 32KB
    mov ax, 16
    mul di                          ; DX:AX = the file offset
    call dos_be_rdat                ; DX:AX = delivered, 0 = at the end
    pop cx
    jc .ret
    or ax, dx                       ; 0 delivered is the end, CF=0
    jz .ret
    add di, cx
    jmp short dos_ldrd
.ret:
    ret

; -----------------------------------------------------------------------------
; dos_popa_N / dos_popb_N - the core's two shared epilogues (SPEC.md 96.54.8,
; os88api.inc's OS88_POPLADDER). Ten routines ended in the same seven or six
; pops and a `ret`; each ends in a near jump here instead - three bytes for
; eight or seven. Two ladders because the two shapes cannot share one: the
; six-register routines push BX..ES and not AX, and their `ret` comes straight
; after `pop bx`. A near jump costs no stack, so no slice is deeper for it.
; Placed AFTER dos_cpm's `align 16`, where its bytes are not paid for twice.
; -----------------------------------------------------------------------------
    OS88_POPLADDER dos_popa, ax, bx, cx, dx, si, di, es
    OS88_POPLADDER dos_popb, bx, cx, dx, si, di, es
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; =============================================================================
; INT 20h / INT 21h (SPEC.md 96.7)
; =============================================================================
; Entered on the PROGRAM's stack with the program's segment registers, so the
; first thing either does is reach its own data through CS - and INT 21h's
; first thing is to GET OFF that stack (SPEC.md 96.7.2), because a program may
; have shrunk its block to within a word or two of its own SP and what is
; directly below it is then the free block's MCB.
;
; The carry flag a DOS call returns is the one in the FLAGS image the `int`
; pushed, not the live one, so the refusal path edits [bp+8] rather than
; executing `stc` - which the `iret` would discard. Since the swap that image
; is a REPLICA on our stack and the epilogue writes it back.
; -----------------------------------------------------------------------------
dos_int20:
    xor al, al                      ; INT 20h is AH=4Ch with a zero code, and
    jmp dos_terminate               ; DOS treats them as the same exit
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_int22:                          ; the terminate ADDRESS: a child process
    xor al, al                      ; returning here is an exit too, and wave 1
    jmp dos_terminate               ; has no children to send
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
%ifdef DOS_PMGATE
; dos_csel - THE CORE'S DATA SELECTOR, in WIN16's kern_mega arm alone
; (DOS_PMGATE, the Makefile's $(MEGAAPPDIR)/doscore.bin; SPEC.md 124.31.7):
; there the core runs in PROTECTED mode under a read-only CODE selector, so
; every `[cs:]` cell below was a #GP kern_mega completed - fifteen an int 21h,
; 7,187 for Write's open of a 20 KB file (KERN-MEGA-PLAN 14.164). WIN16's
; host writes it once, through the block's data half (w16dosh.asm's
; w16dh_init); the gate loads DS from it and keeps it for the whole call.
; DOS.O88's and kern_dos's cores (real mode for every int 21h a program makes)
; are assembled without it and are byte for byte what they were
dos_csel    dw 0
%endif

dos_int21:
    ; --- ONTO OUR OWN STACK BEFORE A SINGLE WORD IS PUSHED (SPEC.md 96.7.2) --
    ; This used to `sti` and then build its whole frame on the PROGRAM's
    ; stack, and a real DOS switches to an internal stack at its own first
    ; instruction - so what a DOS program's stack ever carries is the three
    ; words the `int` pushed and nothing else.
    ;
    ; THE DIFFERENCE CORRUPTS THE ARENA. `AH=4Ah` with `BX = SS + 2 - PSP` is
    ; the standard shrink idiom, and the free MCB `dos_mcb_split` then cuts
    ; sits at the paragraph past the block - which for a program whose SP is
    ; only a paragraph or two above SS is the sixteen bytes DIRECTLY BELOW its
    ; own stack pointer. DOS puts its own three words in that block's
    ; RESERVED bytes, which nothing reads; six more words and a handler on top
    ; of them land on the signature, the owner and the size. The Playroom's
    ; launcher is the report: `AH=4B00` of PLAYEGA.EXE answered `AX=0008`,
    ; "not enough memory", with 434 KB free and a chain whose last MCB read
    ; `sig=00 own=6C8D size=F323` - this gate's own frame.
    ;
    ; IF IS 0 FROM THE GATE AND STAYS 0 UNTIL THE SWAP IS DONE, which is what
    ; makes one cell per value enough: nothing can nest while interrupts are
    ; off, and every cell is spent before the `sti`.
%ifdef DOS_PMGATE
    ; WIN16's PROTECTED-MODE core (dos_csel, above): DS is ours from here to
    ; the `iret`, the program's banked in dos_gds - one word on the
    ; program's stack for the moment, where WIN16's host has just had one
    ; (w16dh_i21's AX); a Windows program's stack has no arena under it
    push ds
    mov ds, [cs:dos_csel]
    pop word [dos_gds]
    mov [dos_gax], ax
    mov [dos_gbp], bp
    mov bp, sp
    mov ax, [bp]
    mov [dos_gip], ax
    mov ax, [bp+2]
    mov [dos_gcs], ax
    mov ax, [bp+4]
    mov [dos_gfl], ax
    mov ax, ss
    mov [dos_gss], ax
%else
    mov [cs:dos_gax], ax
    mov [cs:dos_gbp], bp
    mov bp, sp                      ; the three words the `int` pushed, read
    mov ax, [bp]                    ; while the program's stack is still the
    mov [cs:dos_gip], ax            ; one BP addresses - there is no way to
    mov ax, [bp+2]                  ; index off SP on an 8086
    mov [cs:dos_gcs], ax
    mov ax, [bp+4]
    mov [cs:dos_gfl], ax
    mov ax, ss
    mov [cs:dos_gss], ax
%endif
    cmp ax, [cs:dos_sv_ss]          ; ALREADY ON IT: a program's ISR or INT
    je .keepsp                      ; 33h callback inside a live call, or one
                                    ; under dos_be_go's own swap. [dos_dstk]
                                    ; is the live frame's TOP, so loading it
                                    ; would push over that frame; the SP we
                                    ; arrived on is below it (SPEC.md 96.7.2)
    mov ax, [cs:dos_sv_ss]          ; THE STACK dos_be_go ALREADY BORROWS: the
    mov ss, ax                      ; UI task's in DOS.O88 (SPEC.md 96.4.1)
    mov sp, [cs:dos_dstk]           ; and kern_dos's own where there is no
                                    ; kernel, so nothing new is claimed
.keepsp:
    push word [cs:dos_gss]          ; the program's stack, banked on OURS, so
    push bp                         ; a child's INT 21h nests - and its SP is
                                    ; in BP already, which is a cell and two
                                    ; stores not spent
    push word [cs:dos_dstk]
    mov al, [cs:dos_onprog]
    xor ah, ah
    push ax
%ifdef DOS_PMGATE
    mov byte [dos_onprog], 0
%else
    mov byte [cs:dos_onprog], 0     ; **dos_be_go MUST NOT SWAP AGAIN**: it
%endif
                                    ; would swap UPWARDS, to [dos_sv_sp],
                                    ; straight through the frame below. That
                                    ; byte's only consumer is dos_be_go, which
                                    ; is what makes borrowing it honest
    push word [cs:dos_gfl]          ; ...and the `int`'s three words
    push word [cs:dos_gcs]          ; REPLICATED, so [bp+4], [bp+6] and [bp+8]
    push word [cs:dos_gip]          ; mean what they always meant and not one
    mov ax, [cs:dos_gax]            ; handler changes
    mov bp, [cs:dos_gbp]

    sti                             ; DOS runs its calls with interrupts on
    push bp
%ifdef DOS_PMGATE
    push word [dos_gds]             ; (DS is ours already)
%else
    push ds
%endif
    mov bp, sp                      ; [bp]=DS [bp+2]=BP [bp+4]=IP [bp+6]=CS
    push si                         ; [bp+8]=FLAGS, all on OUR stack now
    push di                         ; ...and [bp-2]=SI [bp-4]=DI [bp-6]=ES
    push es                         ; [bp-8]=DX, banked here rather than per
    push dx                         ; handler (SPEC.md 96.7.1)
%ifndef DOS_PMGATE
    push cs
    pop ds
%endif
%ifdef DOSTRACE
    call dos_trace
%endif
    cmp word [dos_hkv + DHK_POLL], 0 ; **THE THIRD POLL** (SPEC.md 96.23.4): a
    je .nopkt                        ; client doing file I/O between receives
    call word [dos_hkv + DHK_POLL]   ; drains here. One compare on a path that
.nopkt:                              ; is already a dispatch, and it costs a
                                     ; host with no packet driver nothing

    ; --- THE DISPATCH IS A TABLE (SPEC.md 96.7.3) ---------------------------
    ; It was forty-eight `cmp ah, n / je` pairs, and every handler is further
    ; than a short jump away, so each `je` assembled as `jne +3 / jmp near` -
    ; 384 bytes, and a function's cost was its POSITION in the list: AH=06h,
    ; the console poll a game spins on, paid forty-five compares and
    ; forty-five TAKEN jumps before it was found. The table is one cost for
    ; every function and is smaller. AH=02h and AH=09h are tested first,
    ; ahead of it, because console output is the call a program makes most and
    ; they were 3rd and 4th in the old list - they are 1st and 2nd now, so not
    ; one function the old list put early AND that a program calls in a loop
    ; got slower. BX is the only register the lookup needs, and it goes back
    ; with the `xchg` that leaves the handler's address on top of the stack
    ; for the `ret` to take - so every handler sees exactly the registers it
    ; saw before, and [bp-10] is the slot below the gate's last bank ([bp-8]).
    cmp ah, 0x02
    je .putc
    cmp ah, 0x09
    je .puts
    cmp ah, DOS_T21_N
    jae .hifn
    push bx
    mov bl, ah
    xor bh, bh
    shl bx, 1
    mov bx, [.t21 + bx]
    xchg bx, [bp-10]
    ret
.hifn:                              ; above the table: 62h (get PSP), and 68h
    cmp ah, 0x62                    ; and 6Ah (commit and extended commit)
    je .getpsp                      ; are the only three,
    cmp ah, 0x68
    je .hidsk                       ; and 69h between them is not a call this
    cmp ah, 0x6A                    ; DOS answers
    jne .bad
.hidsk:
    jmp .dskreset
.getpsp:
    ; AH=62h, and AH=51h through the table: out BX = the RUNNING program's
    ; PSP (SPEC.md 96.55). Unimplemented, it fell to .bad and BX came back
    ; as whatever the program had in it - and DOS sets no carry for this
    ; call, so nobody looks: IBM DOS 3.30's XCOPY.EXE stores BX as its PSP
    ; segment, parses its command tail out of that, and prints "Invalid
    ; parameter" for every command line it is given.
    mov bx, [dos_ldpsp]
    jmp .ok

.term0:
    ; AH=00h IS `INT 20h` THROUGH THE OTHER DOOR, and its exit code is ZERO
    ; rather than AL (SPEC.md 96.5.2.3). AL is an argument to no part of it,
    ; so whatever the program last had there was being reported as the code -
    ; and the way a program reaches AH=00h by accident is a corrupted AH,
    ; which is to say with a leftover in AL. That is precisely how BOLOBALL
    ; reported `Exit code 002` in the field and `004` here off the same
    ; defect: 96.7.1.3.1's fabricated `AH=41h` failed with error 2 on a hard
    ; disk and 3 on a floppy, `inc ax` carried the difference into AL, and the
    ; window printed it as if the program had chosen it.
    xor al, al
.term:
    mov sp, bp                      ; THE FRAME, not the top of the stack: the
    pop ds                          ; gate banks three registers below `bp` now
    pop bp                          ; (SPEC.md 96.7.1) and a bare pop pair here
    jmp dos_terminate               ; took two of them instead. AL is the code

.putc:
    mov al, dl
    call dos_tty
    jmp .ok

.puts:
    push ds                         ; the string is the PROGRAM's, at DS:DX, so
    mov ds, [bp]                    ; its DS addresses it and not ours - OFF THE
    push si                         ; FRAME and not off the top of the stack,
    mov si, dx                      ; which is three registers deeper than it
                                    ; was (SPEC.md 96.7.1). Ours is banked
                                    ; because .ok's own work is DS-relative
.sloop:
    mov al, [si]
    cmp al, '$'
    je .sdone
    inc si
    call dos_tty
    jmp short .sloop
.sdone:
    pop si
    pop ds
    jmp .ok
; ...and THE TABLE, here rather than beside the lookup so that the two
; `je`s ahead of it stay SHORT: .putc and .puts are the next thing past the
; dispatch, so a function that is neither falls through two untaken branches
; instead of jumping over two expanded ones.
.t21:                               ; DOS_T21_N words, 00h up: the handler,
    dw .term0, .getce, .putc, .bad, .bad, .bad, .dconio, .getc   ; 00h
    dw .getc, .puts, .bad, .kbhit, .flushin, .dskreset, .seldrv, .fcbop ; 08h
    dw .fcbcl, .fcbop, .fcbop, .fcbop, .fcbrd, .fcbrd, .fcbop, .fcbop ; 10h
    dw .bad, .curdrv, .setdta, .bad, .bad, .bad, .bad, .bad   ; 18h
    dw .bad, .fcbrd, .fcbrd, .fcbop, .fcbsr, .setvec, .bad, .fcbrd ; 20h
    dw .fcbrd, .parsefcb, .getdate, .setdate, .gettime, .settime, .setvfy, .getdta ; 28h
    dw .ver, .term, .bad, .brk, .bad, .getvec, .dfree, .swch   ; 30h
    dw .bad, .mkdir, .rmdir, .chdir, .create, .open, .close, .read   ; 38h
    dw .write, .unlink, .seek, .getattr, .ioctl, .bad, .bad, .getcwd   ; 40h
    dw .alloc, .free, .resize, .exec, .term, .retcode, .ff, .fn   ; 48h
    dw .bad, .getpsp, .bad, .bad, .getvfy, .bad, .rename ; 50h: 51h is 62h
DOS_T21_N equ ($ - .t21) / 2        ; ...or .bad for a function we lack

.getce:
    call dos_getkey                 ; AH=01h echoes what it read; AH=07h and
    push ax                         ; AH=08h do not, and 07h additionally does
    call dos_tty                    ; not check for Ctrl-Break - a distinction
    pop ax                          ; wave 1 has nothing to make
    jmp .ok
.getc:
    call dos_getkey
    jmp .ok
.kbhit:
    mov ah, 1                       ; AH=0Bh: FFh if a character is waiting,
    int 0x16                        ; 00h if not - the poll a program spins on
    mov al, 0
    jz .khdone
    mov al, 0xFF
.khdone:
    jmp .ok

.write:
    ; AH=40h: BX = handle, CX = bytes, DS:DX = the buffer, and DS is the
    ; PROGRAM's. Handles 1 and 2 are the console, which is the ROM teletype
    ; here; a real file wants the write wave (DOS-EXEC-PLAN 11 wave 5), and
    ; refusing is what keeps a save from reporting success.
    cmp bx, 2
    ja .fwrite                      ; ...and anything above them is a file
    or bx, bx
    jz .bad                         ; handle 0 is stdin: writing to it is not
.wcon:                              ; ...and a CON opened by name lands here
    push si                         ; too (SPEC.md 96.11.7)
    push cx
    mov si, dx
    mov ds, [bp]                    ; THE PROGRAM'S DS, off the frame - [bp] and
                                    ; NOT [bp+2]: the prologue is `push bp /
                                    ; push ds / mov bp, sp`, so the DS push is
                                    ; the one BP lands on. BP is SS-relative by
                                    ; default, which is right - the frame is on
                                    ; the program's own stack
    jcxz .wdone
.wloop:
    mov al, [si]
    inc si
    push cx
    call dos_tty
    pop cx
    loop .wloop
.wdone:
    pop ax                          ; AX = the byte count, which is what a
    pop si                          ; caller checks against CX
    jmp .ok
; --- the file handles (SPEC.md 96.11) ---------------------------------------
; EVERY ONE OF THESE BANKS BX, because it is the handle a program keeps there
; across a read loop and DOS preserves every register but a call's documented
; outputs. dos_fh_slot spends it turning a handle into an index.
.fherr:                             ; AL = a DOS error code
    pop bx
    call dos_fh_leave               ; ...and off the drive the name named, if
    xor ah, ah                      ; it named one (SPEC.md 96.6.2). BOTH exits
    jmp .badax                      ; carry it, so no error path can forget
.fnoent:
    mov al, 2                       ; file not found
    jmp short .fherr
.fmany:
    mov al, 4                       ; too many open files
    jmp short .fherr
.fhbad:
    mov al, 6                       ; invalid handle
    jmp short .fherr
.fhacc:
    mov al, 5                       ; access denied - and it is the honest
    jmp short .fherr                ; answer for every write this layer cannot
                                    ; make (SPEC.md 96.11.2)
.fhok:
    pop bx
    call dos_fh_leave
    jmp .ok

.fhabs:
    ; A LEADING "\" ON A FILE NAME names the program's root, and this wave
    ; resolves every name in the directory it is STANDING in - so below the
    ; root it would be the wrong folder. Refused, honestly, rather than
    ; answered from the wrong place (SPEC.md 96.12.2).
    ; **AND SINCE SPEC.md 96.12.3 IT HAS NOTHING TO REFUSE.** The guard above
    ; was: an absolute name below the root would resolve in the wrong folder,
    ; so refuse it. dos_fh_enter now WALKS one - to the volume root, and then
    ; down whatever folder part the name carried - so "\NAME" and
    ; "\DIR\NAME" both resolve where they say from wherever we are standing.
    ; The call sites are left in place rather than deleted: they are the four
    ; handlers that care, and if a future wave finds a name shape the walk
    ; cannot take, this is where it says so.
    clc
    ret

.open:
    ; AH=3Dh: DS:DX = an ASCIZ name, AL = the access mode; out AX = a handle.
    ; THE MODE IS NOT HONOURED and the handle is read-only whatever it says,
    ; because OSAPI_FILE_APPEND refuses a file whose size is not a cluster
    ; multiple - so there is no in-place write to give. A program that opens
    ; for writing gets its refusal at the WRITE, naming the call, rather than
    ; at the open naming nothing.
    mov [dos_opmode], al            ; BANKED HERE AND NOWHERE LATER:
    push bx                         ; dos_fh_name spends AL on the name it
    call dos_fh_name                ; copied, so the mode is gone by the time
    jc .fherr                       ; the record exists to put it in
    call .fhabs
    jc .fhpath0
    call dos_fh_isdev               ; A DEVICE IS NOT ON THE DISK (96.11.7)
    jnc .opdev
    call dos_fh_stat                ; fills [dos_fent]
    jc .fnoent
    call dos_fh_new                 ; BX = the handle, SI = the record, zeroed
    jc .fmany
    call dos_fh_setname
    call dos_fh_here                ; THE VOLUME AND FOLDER THE NAME LANDED
                                    ; ON (dos_fh_at), which is
                                    ; where every later read of this handle
                                    ; goes. **IT IS `[dos_pvol]` AND NOT
                                    ; `[dos_vol]`** (SPEC.md 96.48.3): this
                                    ; used to read the PROGRAM's drive on the
                                    ; reasoning that `dos_fh_enter` was still
                                    ; standing there, which stopped being true
                                    ; the moment a name stopped moving the
                                    ; program - and `A:AONLY.TXT` opened from
                                    ; B: then recorded B: and read the wrong
                                    ; disk on every refill (96.6.2)
    mov ax, [dos_fent+18]
    mov [si+FH_SIZE], ax
    mov ax, [dos_fent+20]
    mov [si+FH_SIZE+2], ax
    mov byte [si+FH_FLAGS], FHF_USED
    mov al, [dos_opmode]            ; **THE ACCESS MODE IS HONOURED NOW**
    and al, 7                       ; (SPEC.md 96.11.6): 1 or 2 asked to write,
    jz .opro                        ; and OSAPI_FILE_WRITE_AT can overwrite what
    cmp al, 2                       ; the file already owns. Mode 0 stays
    ja .opro                        ; read-only and 3..7 is not an access mode
    or byte [si+FH_FLAGS], FHF_WRITE | FHF_INPLC | FHF_MADE
                                    ; **AND `MADE` AT THE OPEN** (96.11.6.3):
                                    ; it means "the file is on the disk, so a
                                    ; flush APPENDS rather than replacing",
                                    ; which is true of an OPENED file from the
                                    ; first instruction. `.iappend` used to
                                    ; assert it instead, and that is a LIE on
                                    ; a created handle reaching the same arm
                                    ; through the gap above: nothing has been
                                    ; flushed, so the file does not exist, and
                                    ; the append would go onto nothing
.opro:
    test byte [dos_fent+22], OSAPI_FIND_CZ
    jz .opdone
    ; A COMPRESSED FILE CANNOT BE READ THROUGH THE WINDOW AT ALL: READ_AT is
    ; raw (SPEC.md 20.14.3), so it would deliver the wrapper. Small ones are
    ; read WHOLE instead, which expands - and the margin is the SDK's own,
    ; the packed bytes landing high in the buffer and expanding downwards.
    mov ax, [si+FH_SIZE+2]
    or ax, ax
    jnz .opbig
    mov ax, [si+FH_SIZE]
    add ax, 128
    jc .opbig
    cmp ax, [dos_wbytes]
    ja .opbig
    or byte [si+FH_FLAGS], FHF_WHOLE
.opdone:
    call dos_jft_sync               ; ...the PSP's own view of it (96.21.4)
    mov ax, bx                      ; AX = the handle
    jmp .fhok
.opbig:
    mov byte [si+FH_FLAGS], 0       ; hand the slot back: a refused open must
    jmp .fhacc                      ; not spend one - a NEAR jump now, the
                                    ; device arm below having moved .fhacc out
                                    ; of a short one's reach

.opdev:
    ; **A CHARACTER DEVICE TAKES A REAL SLOT** (SPEC.md 96.11.7), and that is
    ; the whole point rather than an implementation detail: Microsoft Works
    ; opens CON over and over AT ONE CALL SITE until DOS refuses, to find out
    ; how many handles it has left. Answering with one of the five standard
    ; handles would hand it the same number for ever and the loop would never
    ; end; answering `file not found` - which is what a name resolved through
    ; the directory gets - tells it the answer is ZERO, and it then refuses to
    ; open its own files (docs/FIELD-NOTES.md 43).
    ;
    ; AL is the DOS_DEV_* code and dos_fh_new preserves it.
    call dos_fh_new                 ; BX = the handle, SI = the record, zeroed
    jc .fmany0
    call dos_fh_setname             ; the name is the DEVICE's, which is what
                                    ; AH=44h and a debugger want to see
    mov [si+FH_VOL], al             ; ...and the code, where a file keeps its
                                    ; volume: a device has none (96.11.7)
    mov byte [si+FH_FLAGS], FHF_USED | FHF_DEV
                                    ; NOT FHF_WRITE, whatever the mode said:
                                    ; that bit makes the CLOSE create a file,
                                    ; and .fwrite tests FHF_DEV before it
    call dos_jft_sync
    mov ax, bx
    jmp .fhok

; --- NEAR DOORS to the shared exits (SPEC.md 96.7.3): one `jmp near` here lets
; the error branches around it be short, where each was `jcc +3 / jmp near`
.fherr0: jmp .fherr
.fmany0: jmp .fmany
.fhpath0: jmp .fhpath

.create:
    ; AH=3Ch: DS:DX = an ASCIZ name, CX = attributes; out AX = a handle. The
    ; file is not touched until the first window flushes, which is also what
    ; makes the truncate free - the first flush REPLACES.
    push bx
    call dos_fh_name
    jc .fherr0
    call .fhabs
    jc .fhpath0
    call dos_fh_new
    jc .fmany0
    call dos_fh_setname
    call dos_fh_here                ; ...and the same for a file just CREATED
                                    ; (SPEC.md 96.48.3)
    mov byte [si+FH_FLAGS], FHF_USED | FHF_WRITE
    call dos_jft_sync
    mov ax, bx
    jmp .fhok

.close:
    ; AH=3Eh: BX = the handle.
    push bx
    call dos_fh_slot                ; SI = the record, BX = its index
    jc .fhbad
    call dos_fh_close
    jc .fherr0
    call dos_jft_sync
    xor ax, ax
    jmp .fhok

.read:
    ; AH=3Fh: BX = the handle, CX = bytes, DS:DX = the buffer; out AX = the
    ; bytes delivered, 0 meaning end of file.
    push bx
    call dos_fh_slot
    jc .fhrdev
    test byte [si+FH_FLAGS], FHF_DEV ; ...and one opened BY NAME reads the same
    jnz .fhrdeof                     ; way (SPEC.md 96.11.7)
    call dos_fh_rdloop              ; AX = delivered
    jc .fherr0
    jmp .fhok
.fhrdev:
    cmp bx, DOS_FH0                 ; a DEVICE handle: stdin has no line editor
    jae .fhbad1                      ; here, so it is at end of file, which is
.fhrdeof:                           ; what a program reading it will act on
    xor ax, ax
    jmp .fhok

.fwrite:
    ; AH=40h with a file handle: BX, CX and DS:DX as .write's.
    push bx
    call dos_fh_slot
    jc .fhbad1
    test byte [si+FH_FLAGS], FHF_DEV ; ...OR A DEVICE, which is neither a file
    jnz .fwdev                       ; nor one of the five (SPEC.md 96.11.7)
    test byte [si+FH_FLAGS], FHF_WRITE
    jz .fhacc1
    or byte [si+FH_FLAGS], FHF_WROTE ; **AH=44h's BIT 6 IS THE ONLY READER**
                                    ; (SPEC.md 96.7.1.2), and it is set where
                                    ; the write is ACCEPTED rather than where
                                    ; it succeeds: DOS clears that bit for a
                                    ; CX=0 write too (96.11.6.2's truncate),
                                    ; which moves no bytes at all, and SI is
                                    ; the record only here
    test byte [si+FH_FLAGS], FHF_INPLC
    jnz .fwinpl                     ; an AH=3Dh handle OVERWRITES (96.11.6)

    ; --- BEFORE the end, AT it, or PAST it - three answers (96.11.6.3) ------
    ; It was `jne .fhacc` twice, which is not an ordering test at all: it
    ; refused a write PAST the end exactly as it refused one BEHIND it, and
    ; those are opposite cases. Behind WAS §96.11.2's real refusal - a write
    ; into the middle of a file this handle created, which the append path
    ; cannot make - and is an overwrite after a flush now (.fwback,
    ; 96.11.6.4). PAST is a GAP, which 96.11.6.1 already lays, and Microsoft
    ; Works reported it as `Cannot write file`: create, seek to 0x180 on the
    ; empty file, write there, which is that section's own first bullet -
    ; pre-allocating a header to patch later.
    mov ax, [si+FH_POS+2]           ; the HIGH words first, so this is an
    cmp ax, [si+FH_SIZE+2]          ; UNSIGNED 32-BIT compare and not two
    ja .fwgap                       ; equality tests wearing one
    jb .fwback
    mov ax, [si+FH_POS]
    cmp ax, [si+FH_SIZE]
    ja .fwgap
    jb .fwback
    call dos_fh_wrloop
    jc .fherr1
    jmp .fhok
.fwback:
    ; **BEHIND THE END IS AN OVERWRITE NOW** (SPEC.md 96.11.6.4), and it is
    ; 96.52's partial flush made on purpose: the accumulator goes out as the
    ; append it is - the first flush CREATES the file - and the handle becomes
    ; a VIEW, so dos_fh_wiloop overwrites in place and hands the rest of a
    ; write that runs past the end back to the accumulator at .iappend.
    ; Paintbrush writes a bitmap's body and then seeks to 0 for its header.
    call dos_fh_flush
    jc .fherr1
    or byte [si+FH_FLAGS], FHF_INPLC
.fwgap:
    ; ...and the gap arm is dos_fh_wiloop's `.ihole`, which is chosen on the
    ; same compare and needs no flag: it lays [SIZE, POS), rewinds, and hands
    ; the program's own bytes back to the append accumulator through
    ; `.iappend`. FHF_INPLC is NOT set here - `.ihstep` takes it for the
    ; length of a chunk and `.iappend` gives it back, and a created handle
    ; that kept it would flush through WRITE_AT onto a file that does not
    ; exist yet.
.fwinpl:
    call dos_fh_wiloop              ; ...and it may not GROW one: the loop
    jc .fherr1                       ; stops at the end of file and answers
    jmp .fhok                       ; the short count, which is what DOS
                                    ; answers for a full disk

.fwdev:
    ; A DEVICE WRITE IS ACCEPTED WHOLE (SPEC.md 96.11.7). CON goes to the
    ; teletype, which is what handles 1 and 2 already do; the other three are
    ; devices this machine has not got, and DOS's own answer for a printer
    ; nobody has plugged in is to accept the bytes. A REFUSAL is the wrong
    ; answer: a program that cannot print usually cannot carry on either.
    pop bx                          ; the handle back, undoing .fwrite's push
    cmp byte [si+FH_VOL], DOS_DEV_CON
    je .wcon
    mov ax, cx                      ; NUL, PRN, AUX: every byte accounted for
    jmp .ok                         ; and none of them written

; ...the same, for the handlers either side of it
.fherr1: jmp .fherr
.fhacc1: jmp .fhacc
.fhbad1: jmp .fhbad

.unlink:
    ; AH=41h: DS:DX = an ASCIZ name.
    push bx
    call dos_fh_name
    jc .fherr1
    call .fhabs
    jc .fhpath
    push si
    mov si, dos_fname
    call dos_be_delete
    pop si
    jc .dlerr
    xor ax, ax
    jmp .fhok
.dlerr:
    cmp ax, FERR_NOENT              ; THE NAME IS NOT THERE, and DOS says 2 for
    jne .fhacc1                      ; that (SPEC.md 96.11.9) - `.rnerr` above
    mov al, 2                       ; already makes this exact distinction for
    jmp .fherr                      ; AH=56h, and AH=41h was sending every
                                    ; refusal to 5. Microsoft Works unlinks the
                                    ; backup name before each save, so it asks
                                    ; this question on every Save As and got
                                    ; "access denied" about a file that simply
                                    ; was not there

.seek:
    ; AH=42h: AL = the origin, BX = the handle, CX:DX = a SIGNED offset; out
    ; DX:AX = the new position.
    push bx
    mov ah, al                      ; AL is about to be spent
    call dos_fh_slot
    jc .fhbad1
    cmp ah, 2
    ja .fhacc1
    or ah, ah
    jnz .skcur
    xor ax, ax                      ; origin 0: from the start
    xor bx, bx
    jmp short .skadd
.skcur:
    dec ah
    jnz .skend
    mov ax, [si+FH_POS]             ; origin 1: from here
    mov bx, [si+FH_POS+2]
    jmp short .skadd
.skend:
    mov ax, [si+FH_SIZE]            ; origin 2: from the end
    mov bx, [si+FH_SIZE+2]
.skadd:
    add ax, dx
    adc bx, cx
    mov [si+FH_POS], ax
    mov [si+FH_POS+2], bx
    mov dx, bx
    mov [bp-8], dx                  ; **AN ANSWER, SO IT WRITES THE BANKED
    jmp .fhok                       ; SLOT** (SPEC.md 96.7.1) - the way AH=35h
                                    ; and AH=2Fh already return ES

; --- vectors, drives and the DTA (SPEC.md 96.12) -----------------------------
.setvec:
    ; AH=25h: AL = the interrupt, DS:DX = the handler. It goes STRAIGHT into
    ; the live IVT, which is safe because the whole table is banked at bracket
    ; entry and put back at the end (SPEC.md 96.5) - so a program may hook
    ; anything it likes and the machine still comes back.
    push bx
    push es
    xor bx, bx
    mov es, bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [es:bx], dx
    ; ...and the PROGRAM's DS, off the frame - THROUGH THE STACK AND NOT
    ; THROUGH AX (SPEC.md 96.7.1.3). AH=25h answers nothing, so AX is the
    ; program's: `mov ax, [bp]` handed it back its own DS, and the idiom that
    ; catches it is `mov ax, 2534h / int 21h / inc ax / loop` - eight vectors
    ; installed by INCREMENTING AX, which is how Borland's 8087 emulator
    ; hooks 34h..3Bh and so how every Turbo-compiled program starts. The
    ; second pass then ran with a fabricated AH: 41h DELETE FILE and then
    ; 00h TERMINATE, which is BOLOBALL dying before its first frame (96.7.1.3.1).
    ; The push/pop pair assembles to the same SEVEN bytes as the two moves.
    push word [bp]
    pop word [es:bx+2]
    pop es
    pop bx
    jmp .ok
.getvec:
    ; AH=35h: AL = the interrupt; out ES:BX = its handler.
    push ax
    xor bx, bx
    mov es, bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [es:bx+2]
    mov bx, [es:bx]
    mov [bp-6], ax                  ; ...the banked ES, as .getdta above
    pop ax
    jmp .ok
.curdrv:
    mov al, [dos_vol]               ; AH=19h: 0 = A. The map is the identity
    jmp .ok                         ; with our hole in it (SPEC.md 96.6), and
                                    ; it MOVES now - which is what makes the
                                    ; select-then-ask idiom above truthful
.seldrv:
    ; AH=0Eh: DL = the drive to select; out AL = how many there are.
    ;
    ; IT ACTUALLY SWITCHES NOW (SPEC.md 96.6.1). It used to answer the count
    ; and stay put, which is not a small gap: the way a program finds out
    ; whether a drive exists is to select it and then ask AH=19h where it
    ; ended up, so a select that silently does nothing reports EVERY drive as
    ; invalid - including the ones that are there.
    call dos_drv_sel
    call dos_drv_count
    jmp .ok
.dfree:
    ; AH=36h: DL = the drive - 0 the default, 1 = A. Out AX = sectors per
    ; cluster, BX = free clusters, CX = bytes per sector, DX = total clusters;
    ; AX = FFFFh for an INVALID DRIVE, which is the answer that matters
    ; (SPEC.md 96.27).
    ;
    ; **IT IS THE REFUSAL THAT WAS THE DEFECT, not the absence.** Unimplemented,
    ; this fell to .bad and returned AX=1 with CF - and DOS does not use CF
    ; here at all, so an installer read "one sector per cluster" and three
    ; stale registers. Prince of Persia's INSTALL selects C:, is told it is
    ; standing on C:, asks this, and prints "Invalid drive letter" (96.27.1).
    ;
    ; **AND IT ASKS ABOUT THE DRIVE, NOT ABOUT WHAT IS MOUNTED** (SPEC.md
    ; 96.27.2). OSAPI_VOL_STAT answers for the volume the MACHINE stands on,
    ; and since 96.48 `AH=0Eh` moves only the PROGRAM - the mount waits for
    ; the next name. So the installer's select-C:-then-ask was answered with
    ; the FLOPPY it had just read its source list off: 106 free clusters of
    ; 1KB, "Not enough space on destination disk". `dos_fh_stand` puts the
    ; machine on the drive asked about, in that drive's folder, and moves the
    ; program nowhere - which is also what DOS does: AH=36h changes no drive.
    push bx
    push si
    push di
    push es
    sub dl, 1                       ; 1-based -> a volume index, and CF = it
    jnc .df1                        ; was 0, the drive the program is on
    mov dl, [dos_vol]
.df1:
    cmp dl, DVOL_MAX
    jae .dfbad                      ; past the array dos_fh_stand indexes
    call dos_fh_stand               ; THE MACHINE onto it (CF = not there)
    jc .dfbad
    call dos_be_vstat               ; AX = sectors per cluster, BX = free
                                    ; clusters, CX = bytes per sector, DX =
                                    ; total clusters - AH=36h's own four, which
                                    ; is the slot's shape BECAUSE this is its
                                    ; caller (SPEC.md 18.4.6). THROUGH THE BACK
                                    ; END (96.4.1): the slot reads the FAT,
                                    ; which is disk work on the program's stack
    jc .dfbad
    mov [bp-8], dx                  ; DX is an ANSWER (SPEC.md 96.7.1)...
    jmp short .dft
.dfbad:
    mov ax, 0xFFFF                  ; DOS's own "invalid drive", and a value a
.dft:                               ; program can test even when it ignores the
    pop es                          ; flag - which for this call every program
    pop di                          ; does, DOS never setting CF here. BX is
    pop si                          ; the answer's, or undefined with FFFFh,
    pop dx                          ; as DOS leaves it: the banked one is
    jmp .ok                         ; dropped into DX, which .ok reloads

.setdta:
    mov [dos_dta], dx               ; AH=1Ah: DS:DX, and DS is the program's -
    push word [bp]                  ; which for every real program is the PSP
    pop word [dos_dtaseg]           ; it already runs on. AX COMES BACK, as
                                    ; DOS gives it (96.56.3)
    jmp .ok
.getdta:
    mov bx, [dos_dta]               ; AH=2Fh: out ES:BX - and ES is written into
    push word [dos_dtaseg]          ; the gate's banked slot, which .leave
    pop word [bp-6]                 ; restores from (SPEC.md 96.7.1); AX is
                                    ; the program's, as for 1Ah
    jmp .ok

; --- find first / find next (SPEC.md 96.12.1) --------------------------------
.ff:
    ; AH=4Eh: DS:DX = the pattern, CX = the attribute mask.
    push bx
    call dos_fh_name                ; the pattern travels the same road a name
    jc .fherr6                       ; does, wildcards and all
    call dos_dta_seg                ; ES:DI = the caller's DTA, and DI STAYS
    mov [es:di+DTA_MASK], cl        ; ...the mask FIRST: the rep movsb below
    mov al, [dos_pvol]              ; spends CX (SPEC.md 96.12.1)
    mov [es:di+DTA_VOL], al         ; ...and the volume dos_fh_name put us on,
                                    ; which is where the MACHINE is standing
                                    ; and not where the program is (96.48.3):
                                    ; AH=4Fh reads this back into [dos_fdrv]
                                    ; so a walk started on B: carries on there
    mov word [es:di+DTA_ORD], 0     ; there: .fstep below wants the DTA's BASE,
    push si                         ; and a stosw/rep movsb pair would leave it
    push di                         ; fifteen bytes along - which reads the
    inc di                          ; ordinal out of the pattern
    inc di
    mov si, dos_fname
    mov cx, 13                      ; the pattern lives in the DTA, so AH=4Fh
    cld                             ; needs no state of ours at all. DOS keeps
    rep movsb                       ; it there and so does this
    pop di
    pop si
    ; A SEARCH THAT MATCHED NOTHING ANSWERS 18, THE SAME AS ONE THAT RAN OUT,
    ; and the distinction this once drew is one DOS does not (SPEC.md
    ; 96.12.1.2). MEASURED, by running one binary under IBM DOS 3.30 and under
    ; this DOS: a name that is not there in a directory that IS answers 0012h,
    ; and 0002h is not what DOS says for it at all. What DOS answers 3 for is
    ; the DIRECTORY not being there, which `dos_fh_name` above has already
    ; refused by the time this runs.
    call dos_find_step
    jc .ffnone
    xor ax, ax
    jmp .fhok
.ffnone:
    mov al, 18
.fherr6: jmp .fherr                 ; ...and a near door for .ff/.fn (96.7.3)
.fn:
    ; AH=4Fh: everything it needs is in the DTA AH=4Eh filled.
    push bx
    call dos_dta_seg
    mov al, [es:di+DTA_VOL]         ; ...the volume included: a walk started on
    mov [dos_fdrv], al              ; B: carries on there whatever the program
    call dos_fh_enter               ; has done to its own drive since, and
    jc .fherr6                       ; .fhok/.fherr bring us home
    call dos_find_step              ; CF=1 with AL = 18 (no more files)
    jc .fherr6
    xor ax, ax
    jmp .fhok

; --- AH=29h: parse a filename into an FCB (SPEC.md 96.28) --------------------
.parsefcb:
    ; in:  DS:SI = the name, ES:DI = the FCB, AL = the parse flags
    ; out: AL = 0 no wildcards / 1 wildcards / FFh a drive past the last, SI
    ;      advanced past what was parsed, ES:DI untouched.
    ;
    ; IT SETS NO CARRY, and that is the whole reason it is here. Unimplemented
    ; it fell to .bad, which answers CF=1 with AX=0001 - and a program reading
    ; AL, which for this call every program does, is told its plain name HAD
    ; WILDCARDS IN IT. Measured over eleven inputs under IBM DOS 3.30
    ; (tests/dostrap/parsefcb.asm): CF is clear on every one, the invalid
    ; drive included. SPEC.md 96.22's shape for the fourth time in this DOS.
    ;
    ; THE SEGMENTS ARE WHY IT IS THREE PHASES. The name is in the program's
    ; DS, the FCB in the program's ES, and the separator table in OURS - so it
    ; copies in, parses at home, and copies out, rather than juggling two
    ; overrides through a loop that also has to index a table.
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- phase 1: the name, out of the program's segment ------------------
    push ds
    pop es                          ; ES = ours for the copy in
    mov ds, [bp]                    ; DS = the program's
    xor cx, cx                      ; CX counts the LEADING BLANKS, which DOS
.pf_blank:                          ; skips and which still count towards SI
    mov al, [si]
    cmp al, ' '
    je .pf_bs
    cmp al, 9
    jne .pf_copy
.pf_bs:
    inc si
    inc cx
    cmp cx, DOS_PFIN
    jb .pf_blank                    ; a string of nothing but blanks parses to
                                    ; a blank FCB, which is what it is
.pf_copy:
    mov di, dos_pfbuf
    mov dx, cx                      ; DX = the blanks, banked across the copy
    mov cx, DOS_PFIN
.pf_cp:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    or al, al
    loopnz .pf_cp
    mov byte [es:di-1], 0           ; TERMINATED WHATEVER CAME IN: a name with
                                    ; no NUL in DOS_PFIN bytes is not a name
    push es
    pop ds                          ; ...and home, where the table lives

    ; --- phase 2: the parse, entirely in our own segment ------------------
    mov si, dos_pfbuf
    mov di, dos_pfcb
    call .pf_core
    sub si, dos_pfbuf               ; how far the parse got, plus the blanks
    add si, dx                      ; phase 1 skipped - an ADVANCE and not a
    add [bp-2], si                  ; pointer, so it is ADDED to the banked
                                    ; slot, which still holds the SI the
                                    ; program came in with (SPEC.md 96.7.1).
                                    ; Storing it flat left every caller's SI
                                    ; pointing at the same low address

    ; --- phase 3: the twelve bytes, into the program's FCB ----------------
    mov es, [bp-6]                  ; ES = the program's, as it passed it
    mov di, [bp-4]                  ; ...and DI, which is NOT ours to move
    mov si, dos_pfcb
    mov cx, 12
    cld
    rep movsb

    mov al, bh
    xor ah, ah
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    jmp .ok                         ; .ok and NOT .badax: there is no carry in
                                    ; this call's contract at all

; --- .pf_core - DS:SI a name -> DS:DI's twelve FCB bytes; out BH = AL's answer
; and SI past what was parsed. A routine since SPEC.md 96.56.3: the PSP's two
; FCBs are this parse of the command tail (dos_fcb_arg), as COMMAND's are
.pf_core:
    xor bh, bh                      ; BH = the answer, BL = "a ? was stored"
    xor bl, bl
    xor al, al                      ; the drive byte: 0 = the one we are on
    cmp byte [si+1], ':'
    jne .pf_drvset
    mov al, [si]
    cmp al, 'a'
    jb .pf_dup
    cmp al, 'z'
    ja .pf_dup
    sub al, 32
.pf_dup:
    sub al, 'A' - 1                 ; the FCB numbers A: as 1, not as 0
    inc si
    inc si
    cmp al, DVOL_MAX
    jbe .pf_drvset
    mov bh, 0xFF                    ; past the last drive - and the byte STILL
.pf_drvset:                         ; goes in, measured: Z: writes 26
    mov [di], al
    inc di
    mov cx, 8                       ; the name...
    call .pf_field
    mov cx, 3                       ; ...and the extension, which is there only
    cmp byte [si], '.'              ; if a dot says so
    jne .pf_noext
    inc si
    call .pf_field
    jmp short .pf_ans
.pf_noext:
    mov al, ' '                     ; no dot, so the extension is three blanks
.pf_nex:
    mov [di], al
    inc di
    loop .pf_nex
.pf_ans:
    or bh, bh
    jnz .pf_out                     ; FFh beats everything
    mov bh, bl                      ; ...otherwise 1 if any ? landed, else 0
.pf_out:
    ret

; --- .pf_field - CX bytes of one FCB field at DS:DI from DS:SI --------------
; Stops at a separator, blank-pads what is left, and expands `*` to `?` for
; the rest of the field - which is the FCB's own convention and not ours: DOS
; answers `*.*` with eleven question marks. BL is set when one lands.
.pf_field:
    mov al, [si]
    call .pf_sep
    jc .pf_fpad
    inc si
    cmp al, '*'
    je .pf_fstar
    cmp al, 'a'
    jb .pf_fst
    cmp al, 'z'
    ja .pf_fst
    sub al, 32                      ; AN FCB MATCHES A DIRECTORY ENTRY, which
.pf_fst:                            ; is upper - and Prince's installer really
    cmp al, '?'                     ; does pass "B:Prince.exe"
    jne .pf_fnq
    mov bl, 1
.pf_fnq:
    mov [di], al
    inc di
    loop .pf_field
    ret
.pf_fstar:
    mov al, '?'
    mov bl, 1
.pf_fss:
    mov [di], al
    inc di
    loop .pf_fss
    ret
.pf_fpad:
    mov al, ' '
.pf_fps:
    mov [di], al
    inc di
    loop .pf_fps
    ret

; --- .pf_sep - does AL end an FCB field? CF=1 if it does --------------------
.pf_sep:
    cmp al, ' '
    jbe .pf_syes                    ; the NUL, the tab, a control byte and the
    push cx                         ; blank itself all end it
    push si
    mov si, .pf_seps
    mov cx, DOS_PFSEPN
.pf_sscan:
    cmp al, [si]
    je .pf_shit
    inc si
    loop .pf_sscan
    pop si
    pop cx
    clc
    ret
.pf_shit:
    pop si
    pop cx
.pf_syes:
    stc
    ret
; The characters DOS ends an FCB field on, above the blank - the ones at or
; below it are covered by one compare. `.` is in here because .pf_field has to
; stop on it; the caller is what looks for it and starts the extension.
; A LOCAL LABEL, and that is not a style choice: a global one here re-scopes
; every `.local` in dos_int21 below it, and the whole dispatch stops resolving.
.pf_seps:   db '.', ':', ';', ',', '=', '+', '"', '/', '\\', '[', ']', '|'
            db '<', '>'

; --- AH=56h: rename (SPEC.md 96.31) ------------------------------------------
.rename:
    ; in: DS:DX = the old name, ES:DI = the new one - and note the SECOND is
    ; in the program's ES, which is why dos_fh_core takes a far pointer.
    ;
    ; EVERY RULE BELOW IS MEASURED, under IBM DOS 3.30, by the same binary
    ; (tests/dostrap/renref.asm) - and two of them are not what a reading of
    ; the call would give you:
    ;
    ;   * THE TWO NAMES MUST RESOLVE TO THE SAME DRIVE, and an unqualified one
    ;     means the CURRENT drive - not the OTHER NAME's. "B:X.TXT" -> "Y.TXT"
    ;     standing on A: is 11h, not same device. A handler that resolved the
    ;     new name against wherever the old one lives renames happily on B:.
    ;   * A PATH IN THE NEW NAME IS A MOVE, and DOS does it: "\Y.TXT" succeeds
    ;     and the file is in the root afterwards. OSAPI_FILE_RENAME rewrites a
    ;     directory entry WHERE WE STAND (SPEC.md 18.4), so that is the one
    ;     shape refused here rather than half-done.
    ;
    ; AX is junk on success in DOS (the row that worked reports 0012h), so
    ; only CF carries the answer and zero is as good as anything.
    push bx
    mov al, [dos_vol]               ; THE ENTRY VOLUME, banked before anything
    mov [dos_rnvol], al             ; moves us: it is what an unqualified name
                                    ; means, for BOTH names

    push ax                         ; --- the OLD name, parsed and NOT entered
    mov ax, [bp]
    mov [dos_fnseg], ax
    pop ax
    push di
    mov di, dos_fname
    call dos_fh_core
    pop di
    jc .fherr2
    call .rndrv                     ; AL = the drive it means
    mov [dos_rndrv], al
    mov al, [dos_fabs]
    mov [dos_rnabs], al

    push ax                         ; --- ...and the NEW one, out of its ES
    mov ax, [bp-6]                  ; (SPEC.md 96.7.1's banked slot)
    mov [dos_fnseg], ax
    pop ax
    push di
    push dx
    mov dx, di
    mov di, dos_fname2
    call dos_fh_core
    pop dx
    pop di
    jc .fherr2
    call .rndrv
    cmp al, [dos_rndrv]
    je .rnone
    mov al, 0x11                    ; "not same device" - measured, and the
    jmp .fherr                      ; only code DOS has for this

.rnone:
    cmp byte [dos_fabs], 0          ; a leading separator on EITHER name is a
    jne .rnroot                     ; move - unless we are standing in the
.rnone2:                            ; root already, where it names this very
    cmp byte [dos_rnabs], 0         ; folder and the move is a rename
    jne .rnroot2
.rngo:
    mov [dos_fdrv], al              ; ...and now stand on it. .fhok/.fherr are
    call dos_fh_enter               ; what come home (SPEC.md 96.6.2)
    jc .fherr2
    push si
    push di
    mov si, dos_fname
    mov di, dos_fname2
    call dos_be_rename
    pop di
    pop si
    jc .rnerr
    xor ax, ax
    jmp .fhok
.rnroot:
    cmp word [dos_curdir], 0
    jne .rnmove
    jmp short .rnone2
.rnroot2:
    cmp word [dos_curdir], 0
    jne .rnmove
    jmp short .rngo
.rnmove:
    mov al, 5                       ; THE MOVE DOS WOULD MAKE, refused: this
    jmp .fherr                      ; layer rewrites an entry where it stands
                                    ; and cannot re-link one into another
                                    ; folder. "Access denied" is the honest
                                    ; code for a change we cannot make
                                    ; (SPEC.md 96.11.2's own reasoning)
.rnerr:
    cmp ax, FERR_NOENT              ; the source is not there: DOS says 2, and
    jne .fhacc2                      ; everything else this can fail with - the
    mov al, 2                       ; target existing included - it says 5 for
.fherr2: jmp .fherr                 ; ...and a near door for the handlers
.fhacc2: jmp .fhacc                 ; around it, as above

; --- .rndrv - the drive a just-parsed name MEANS ----------------------------
; out: AL = [dos_fdrv], or the volume we entered on when the name named none.
; clobbers: AL, flags
.rndrv:
    mov al, [dos_fdrv]
    cmp al, 0xFF
    jne .rnd
    mov al, [dos_rnvol]
.rnd:
    ret

; --- directories (SPEC.md 96.12.2) -------------------------------------------
.mkdir:
    push bx
    call dos_fh_name
    jc .fherr2
    push si
    mov si, dos_fname
    call dos_be_mkdir
    pop si
    jc .fhacc2
    xor ax, ax
    jmp .fhok
.rmdir:
    push bx
    call dos_fh_name
    jc .fherr2
    push si
    mov si, dos_fname
    xor al, al                      ; STRICT: remove it only if it is empty,
    call dos_be_rmdir               ; which is the one AH=3Ah means. The
    pop si                          ; recursive form is a different call and
    jc .fhacc2                       ; DOS does not have it
    xor ax, ax
    jmp .fhok
.chdir:
    push bx
    call dos_fh_name
    jc .fherr2
    call dos_cd_go
    jc .fhpath
    mov byte [dos_fhkeep], 1        ; KEEP the walk: dos_fh_enter may have moved
    xor ax, ax                      ; us into the folder part of "\A\B" and
    jmp .fhok                       ; dos_cd_go the rest of the way, which
                                    ; together IS the chdir (SPEC.md 96.12.3)
.fhpath:
    mov al, 3                       ; path not found
    jmp .fherr
.getcwd:
    ; AH=47h: DL = the drive (0 = current, 1 = A), DS:SI = a 64-byte buffer.
    ; The path goes in WITHOUT its leading backslash, which is DOS's shape.
    ;
    ; IT IS OSAPI_FILE_PATH's ANSWER NOW, not a string this DOS maintained on
    ; the way down. The buffer, the level tables and the depth counter all
    ; existed because a package could not ask where it was standing; SPEC.md
    ; 19.2.4 is that question, and answering it here deletes the bookkeeping
    ; AND the 8-level limit that came with it.
    push bx
    mov [dos_cwdst], si
    mov byte [dos_cwdrv], 0xFF      ; ...AND THE SAME SENTINEL HERE, for the
    mov al, dl                      ; same reason: 0 is drive A:, so asking
                                    ; AH=47h about A: from anywhere else set
                                    ; this to "never left" and dos_cw_back
                                    ; stayed there (96.6.3)
    or al, al
    jz .cw_here                     ; 0 is "the one I am on"
    dec al                          ; ...otherwise DOS counts A: as 1 here
    cmp al, [dos_vol]
    je .cw_here
    cmp al, DVOL_MAX
    jae .fhpath
    ; ANOTHER DRIVE: stand there, ask, and come back. A program asks this far
    ; less often than it asks about the drive it is on, and the alternative is
    ; a second copy of every drive's path kept up to date for the one call
    ; that reads it.
    mov [dos_cwdrv], al
    mov dl, al
    call dos_drv_sel
    mov al, [dos_cwdrv]
    cmp al, [dos_vol]
    jne .fhpath                     ; dos_drv_sel left us where we were, so
                                    ; that drive is not there
.cw_here:
    push si
    push di
    push es
    push cx                         ; **AND CX, WHICH IS A MEASURED DEFECT**
                                    ; (SPEC.md 96.7.1.2): the call below spends
                                    ; it on the buffer length and AH=47h has no
                                    ; CX output at all, so a program that kept
                                    ; a count there across "where am I" got the
                                    ; length of our path buffer back. IBM DOS
                                    ; 3.30, asked the same question by the same
                                    ; binary, gives CX back
    mov dl, [dos_vol]               ; **STAND WHERE THE PROGRAM IS FIRST**
    call dos_fh_stand               ; (SPEC.md 96.52): a refill or a flush
    jc .cw_bad                      ; leaves the machine in the HANDLE's folder
                                    ; and nothing walks it home, so asking the
                                    ; machine without this answers SUB for a
                                    ; program in the root that read SUB\X.DAT.
                                    ; Two compares when it is already there;
                                    ; DX goes back from the frame at .ok
    push ds
    pop es
    mov di, dos_pbuf
    mov cx, DOS_PBUF
    call dos_be_path                ; THE BACK END AND NOT THE SLOT: this runs
    jc .cw_bad                      ; inside the bracket, where SS is the DOS
                                    ; program's (SPEC.md 96.4.1), and dsk_path
                                    ; reaches dsk_secbuf through SS
    mov si, dos_pbuf
    cmp byte [si], '\'
    jne .cw_copy
    inc si                          ; DOS's shape carries no leading separator
.cw_copy:
    mov di, [dos_cwdst]
    mov es, [bp]                    ; the buffer is the PROGRAM's
    cld
.cw_byte:
    lodsb
    stosb
    or al, al
    jnz .cw_byte
    pop cx
    pop es
    pop di
    pop si
    call dos_cw_back
    mov ax, 0x0100                  ; DOS 3+ leaves AX = 0100h here, and at
    jmp .fhok                       ; least one program checks it
.cw_bad:
    pop cx
    pop es
    pop di
    pop si
    call dos_cw_back
    jmp .fhpath

; --- the date and the time (SPEC.md 96.13) ----------------------------------
.getdate:
    ; AH=2Ah: out CX = year, DH = month, DL = day, AL = the day of the week.
    call dos_date_roll
    mov cx, [dos_dy]
    mov dh, [dos_dm]
    mov dl, [dos_dd]
    call dos_dow                    ; AL = 0 Sunday .. 6 Saturday
    mov [bp-8], dx                  ; DH/DL are the answer (SPEC.md 96.7.1)
    jmp .ok
.setdate:
    ; AH=2Bh: CX = year, DH = month, DL = day; out AL = 0 or FFh.
    cmp cx, 1980
    jb .dbad
    cmp cx, 2099
    ja .dbad
    or dh, dh
    jz .dbad
    cmp dh, 12
    ja .dbad
    or dl, dl
    jz .dbad
    cmp dl, 31
    ja .dbad
    mov [dos_dy], cx                ; ...into OUR copy, which is where DOS
    mov [dos_dm], dh                ; keeps it too on a machine with no clock
    mov [dos_dd], dl                ; chip (SPEC.md 96.13)
    xor al, al
    jmp .ok
.dbad:
    mov al, 0xFF
    jmp .ok
.swch:
    ; AH=37h AL=0: out DL = the switch character, '/' (COMMAND reads it so:
    ; DIR/W). Changing it, and DOS 2's AL=2/3, are not ours: AL=FFh, DOS's
    ; own "invalid subfunction", with no carry (SPEC.md 96.56)
    mov dl, '/'
    or al, al
    jnz .dbad
    jmp short .dlok
.brk:
    ; AH=33h, Ctrl-Break: AL=0 asks (out DL), AL=1 sets from DL (SPEC.md
    ; 96.56). This DOS checks for no break, so the state is OFF and a set is
    ; taken and changes nothing. **AH COMES BACK**: WordPerfect 4.1 asks,
    ; then `mov al, 1 / int 21h` on the AH it was handed back - and .bad's
    ; AX=0001 made that AH=00h, TERMINATE, exit code 0 and not a word said.
    cmp al, 1
    ja .dbad
    je .brkok                       ; a set: DX is the program's own
    mov dl, 0
.dlok:
    mov [bp-8], dl                  ; DL is the answer, into the banked DX
.brkok:
    jmp .ok
;
; --- THE FCB FILE CALLS (SPEC.md 96.56.1, 96.56.2, 96.56.3) ------------------
; An FCB is a HANDLE in DOS's reserved bytes: the open is AH=3Dh on the name
; the FCB spells, its handle goes at FCB+18h, and the close and the block moves
; are AH=3Eh and AH=42h + 3Fh/40h on it - each a NESTED INT 21h, which the gate
; takes on the stack it is already on (96.7.2). The search, the delete, the
; rename and the size are AH=4Eh/4Fh, 41h and 56h on the same name. So nothing
; below knows a file system, and an open FCB costs a handle out of the
; program's twenty, as under DOS 3. An extended FCB (FFh, five bytes and an
; attribute) is read past, and its attribute is what a find, a make and a
; delete are asked with.
.fcbop:
    ; AH=0Fh open, 16h make (or empty), 11h/12h find, 13h delete, 17h rename,
    ; 23h size: DS:DX = the FCB; out AL = 0, or FFh (no such file, or it
    ; cannot be made, removed or renamed). Every nested call finds into a
    ; find record of OUR OWN on the stack, so the program's DTA is written by
    ; 11h/12h alone, and in DOS's layout rather than ours (96.56.3)
    push bx
    push ax
    push cx
    call .fcbptr                    ; DS:SI = the FCB, AL its attribute -
    push ax                         ; NOT into the FCB: 17h's new name is
    mov bx, ax                      ; there (BH = the call, BL the attribute)
    push word [cs:dos_dtaseg]
    push word [cs:dos_dta]
    sub sp, 60                      ; [sp] = "D:NAMEXXXX.EXT",0, [sp+16] the
    mov di, sp                      ; find record, [sp+60] the program's DTA
    lea ax, [di+16]
    mov [cs:dos_dta], ax
    mov [cs:dos_dtaseg], ss
    push ss
    pop es
    push si
    call .fspell
    pop si
    mov di, [cs:dos_dta]            ; DI = the find record from here on, and
    mov cl, bl                      ; SI the FCB: the gate gives both back
    and cx, 0x7F                    ; across every nested call. CX = the
    push ss                         ; attribute, 0 unless extended
    pop ds
    mov dx, sp
    cmp bh, 0x13
    je .fodel
    cmp bh, 0x17
    je .foren
    cmp bh, 0x16
    je .foopen
    cmp bh, 0x0F
    je .foopen
    mov ah, 0x4E                    ; 11h, 12h and 23h are a find
    int 0x21
    jc .foend
    cmp bh, 0x12
    jne .fo8
    mov es, [bp]                    ; 12h: the walk again, and on from where
    mov ax, [es:si+0x1C]            ; the last 11h/12h on this FCB left it
    mov [di+DTA_ORD], ax
    mov ah, 0x4F
    int 0x21
    jc .foend
.fo8:
    cmp bh, 0x23
    jne .fook
    mov ds, [bp]                    ; 23h: the size in RECORDS, a part one
    mov cx, [si+0x0E]               ; counted, at the FCB's own record size -
    or cx, cx                       ; 0 meaning 128, MEASURED
    jnz .fz1
    mov cl, 0x80
.fz1:
    mov ax, [ss:di+DTA_SIZE]
    mov dx, [ss:di+DTA_SIZE+2]
    dec cx
    add ax, cx
    adc dx, 0
    inc cx
    xchg ax, bx
    xchg ax, dx
    xor dx, dx
    div cx
    xchg ax, bx
    div cx
    mov [si+0x21], ax               ; THREE bytes at any record size: IBM DOS
    mov [si+0x23], bl               ; 3.30 leaves +24h as it was
    jmp short .fook
.foopen:
    cmp bh, 0x16                    ; 0Fh -> 3Dh, open read AND write, as an
    mov ax, 0x3C02                  ; FCB is; 16h -> 3Ch, CX the attribute
    adc ah, 0
    int 0x21
    jc .foend
    mov ds, [bp]
    xchg bx, ax
    mov [si+0x18], bx               ; THE HANDLE, in DOS's reserved bytes
    xor ax, ax
    mov [si+0x0C], ax               ; current block 0, records of 128
    mov word [si+0x0E], 128
    mov [si+0x14], ax               ; the date and the time: this DOS's
    mov [si+0x16], ax               ; directory has none to give (96.12.1)
    mov ax, 0x4202                  ; the size, from the end. Nothing reads
    xor cx, cx                      ; the position: every move seeks first
    cwd
    int 0x21
    mov [si+0x10], ax
    mov [si+0x12], dx
.fook:
    clc
.foend:                             ; CF = refused; DS:SI may be anything
    sbb al, al                      ; AL = FFh, or 0
    mov ds, [bp]                    ; (SI is the FCB on every path here)
    jnz .fo4
    cmp byte [si], 0                ; the drive NAMED: 0 means the current
    jne .fo6                        ; one, and DOS writes the one it means
    mov al, [cs:dos_vol]
    inc ax
    mov [si], al
    xor al, al
.fo6:
    mov bx, [ss:di+48]              ; BH = the call, BL the attribute
    cmp bh, 0x11
    je .fsconv
    cmp bh, 0x12
    je .fsconv
.fo4:
    add sp, 60
    pop word [cs:dos_dta]
    pop word [cs:dos_dtaseg]
    pop cx                          ; (the call and the attribute)
    pop cx
    jmp .fcbret
.fsconv:
    ; DOS's FCB search leaves the drive and a DIRECTORY ENTRY in the DTA -
    ; name 11, attribute, ten reserved, time, date, cluster 0, size - behind
    ; FFh, five zeros and the attribute asked for when the FCB is extended
    mov ax, [ss:di+DTA_ORD]         ; where the walk is, for the next 12h
    mov [si+0x1C], ax
    mov bh, [si]                    ; the drive
    mov si, di
    les di, [ss:si+44]              ; the program's DTA, banked above ours
    push ss
    pop ds
    or bl, bl
    jns .fs0
    mov al, 0xFF
    stosb
    xor ax, ax
    stosw
    stosw
    stosb
    mov al, bl
    and al, 0x7F
    stosb
.fs0:
    mov al, bh
    stosb
    push si
    add si, DTA_NAME
    call .f11
    pop si
    mov al, [si+DTA_ATTR]
    stosb
    mov cx, 10
    xor ax, ax
    rep stosb
    add si, DTA_TIME
    movsw                           ; the time and the date
    movsw
    stosw                           ; cluster 0
    movsw                           ; the size
    movsw
    jmp short .fo4                  ; AL = 0
.fodel:
    ; 13h: every file the name MATCHES, '?' and all - DOS's delete takes a
    ; pattern where AH=41h takes a name. So: find the first match, remove it,
    ; ask again from the top (a removal moves what a find-next would count);
    ; one that will not go is stepped past with a find-next, so the walk ends
    mov bl, 0xFF                    ; FFh until something is removed
.fd1:
    push cx
    mov ah, 0x4E
    int 0x21
.fdhit:
    pop cx
    jc .fd3
    push dx
    call .fdname
    mov ah, 0x41
    int 0x21
    pop dx
    jc .fdnext
    xor bx, bx
    jmp short .fd1
.fdnext:
    push cx
    mov ah, 0x4F
    int 0x21
    jmp short .fdhit
.fd3:
    add bl, 1                       ; CF = nothing removed (or renamed)
.foend1:
    jmp .foend
.foren:
    ; 17h: every file the old name (FCB+1) MATCHES, renamed to the new one at
    ; FCB+11h - where a '?' keeps the old name's own letter, so FCBW?.TMP to
    ; FCBV?.TMP renames FCBW1 to FCBV1. On the FCB's one drive, as DOS does;
    ; a rename refused (the new name is there) ends it with FFh. The walk is
    ; 4Eh/4Fh: a rename leaves the entry where it is, so it counts the same
    mov bl, 0xFF
    mov ah, 0x4E
.frn1:
    push cx
    int 0x21
    pop cx
    jc .fd3
    push cx
    push si
    push di
    lea si, [di+DTA_NAME]           ; the old name, eleven letters at
    sub di, 11                      ; [sp+5]...
    call .f11
    pop di
    pop si
    mov ds, [bp]
    push si
    mov al, [si]                    ; ...behind the FCB's drive at [sp+4]
    mov [es:di-12], al
    add si, 0x11
    push di
    sub di, 11
    mov cx, 11
.frm:
    lodsb                           ; ...and the new name's letters over it,
    cmp al, '?'                     ; but for a '?'
    je .frq
    mov [es:di], al
.frq:
    inc di
    loop .frm
    pop di
    pop si
    push si
    push di
    push ss
    pop ds
    lea si, [di-12]
    sub di, 16
    call .fspell                    ; "D:NEWNAME.EXT" at [sp], which spells
    pop di                          ; into its own source from below and
    pop si                          ; never overtakes it
    pop cx
    call .fdname                    ; DS:DX = the old name
    push di
    sub di, 16                      ; ES:DI = the new
    mov ah, 0x56
    int 0x21
    pop di
    jc .foend1                      ; refused: FFh, and no further
    xor bx, bx
    mov ah, 0x4F
    jmp short .frn1
.fdname:                            ; out DX = the find record's name, with
    lea dx, [di+DTA_NAME]           ; the spelled name's drive in front of
    cmp byte [di-15], ':'           ; it if that had one (over the size's top
    jne .fdn1                       ; word, which nothing reads)
    dec dx
    dec dx
    mov ax, [di-16]
    mov [di+DTA_NAME-2], ax
.fdn1:
    ret
.fspell:                            ; DS:SI = a drive byte and eleven letters
    lodsb                           ; -> ES:DI "D:NAME.EXT",0, blanks dropped;
    or al, al                       ; drive 0 is the current one, no letter
    jz .fo1
    add al, 'A' - 1
    mov ah, ':'
    stosw
.fo1:
    mov cl, 8
    call .fcbcp
    mov al, '.'
    stosb
    mov cl, 3
    call .fcbcp
    cmp byte [es:di-1], '.'         ; no extension: "NAME", not "NAME."
    jne .fo2
    dec di
.fo2:
    mov byte [es:di], 0
    ret
.f11:                               ; DS:SI "NAME.EXT",0 -> ES:DI, eleven
    push di                         ; letters blank-padded; DI past them
    mov cx, 11
    mov al, ' '
    rep stosb
    pop di
    push di
.f1:
    lodsb
    or al, al
    jz .f3
    cmp al, '.'
    jne .f2
    pop di
    push di
    add di, 8
    jmp short .f1
.f2:
    stosb
    jmp short .f1
.f3:
    pop di
    add di, 11
    ret
.fcbcl:
    ; AH=10h: DS:DX = the FCB; out AL = 0, or FFh
    push bx
    push ax
    call .fcbptr
    mov bx, [si+0x18]
    mov ah, 0x3E
    int 0x21
    sbb al, al
    mov byte [si+0x19], 0xFF        ; the handle is no longer the FCB's: a
                                    ; second close or a stale read answers
                                    ; error 6 rather than reach the file the
                                    ; number names next (96.56.1)
.fcbret:                            ; AL = the answer, and the AH that was asked
    pop bx
    mov ah, bh
    pop bx
    jmp .ok
.fcbrd:
    ; AH=27h read, AH=28h write: CX = records, DS:DX = the FCB, its random
    ; record at +21h, the data the DTA's (AH=1Ah). Out AL = 0 all of them, 1
    ; end of file (read) or disk full (write), 3 a read's last record partial
    ; and zero-filled; CX = the records moved, and the random record moved on
    ; by that many, block and current with it (MEASURED, 96.56.3). A write of
    ; 0 records sets the file's length there, which is AH=40h's CX = 0
    ; (96.11.6.2) and so needs nothing of its own.
    ;
    ; AH=21h/22h and AH=14h/15h are ONE record through the same body (96.56.2,
    ; 96.56.3): 21h/22h at the random record, which they do not move, and
    ; 14h/15h at block x 128 + current, which they advance - the random
    ; record banked at FCB+1Ch and given back. All four set block and current
    ; from where they ended, and none touches CX. A write puts the file's
    ; size in the FCB, as DOS keeps it there.
    push bx
    push ax
    push cx                         ; the program's CX
    cmp ah, 0x27
    jae .fr0
    mov cx, 1                       ; 14h, 15h, 21h, 22h: one record
.fr0:
    push cx                         ; the records asked
    call .fcbptr
    mov [si+0x1A], ah               ; the call, in DOS's reserved bytes
    mov al, ah                      ; FCB+1Bh = 1 to READ, 0 to WRITE: each
    cmp al, 0x16                    ; pair is a read and a write, and once
    adc al, 0                       ; 14h/15h move up one the write is the
    and al, 1                       ; EVEN one of each
    mov [si+0x1B], al
    cmp ah, 0x15
    ja .frs
    les ax, [si+0x21]               ; 14h/15h: the random record out of the
    mov [si+0x1C], ax               ; way (FCB+1Ch..1Fh are DOS's and unused)
    mov [si+0x1E], es
    call .fcbcur
    mov [si+0x21], ax
    mov [si+0x23], dx
.frs:
    mov di, [si+0x0E]               ; DI = the record size, 0 meaning 128
    or di, di
    jnz .fr1
    mov di, 128
    mov [si+0x0E], di
.fr1:
    mov ax, [si+0x21]               ; CX:DX = record * size: four bytes of
    mul di                          ; record below 64, three at or above it
    push ax
    mov cx, dx
    mov ax, [si+0x23]
    cmp di, 64
    jb .fr2
    xor ah, ah
.fr2:
    mul di
    add cx, ax
    pop dx
    mov bx, [si+0x18]
    mov ax, 0x4200
    int 0x21
    mov ax, [cs:dos_dta]            ; **THE DTA'S SEGMENT BOUNDS IT** (96.56.3):
    neg ax                          ; AX = the bytes left in it, FFFFh at
    jnz .frw0                       ; offset 0 - one call's most
    dec ax
.frw0:
    xor dx, dx
    div di                          ; AX = the records that fit
    pop cx                          ; the records asked
    cmp ax, cx
    jb .frw1                        ; more than fit: AL = 2, and nothing moves
    push cx
    xchg ax, cx
    mul di                          ; < 64KB now
    xchg cx, ax                     ; CX = the bytes asked for
    mov ah, 0x40                    ; 3Fh to read, 40h to write
    sub ah, [si+0x1B]
    push ds
    lds dx, [cs:dos_dta]
    int 0x21
    jnc .fr3
    xor ax, ax
.fr3:
    push ds
    pop es
    add dx, ax                      ; ES:DX = past what came
    push dx
    xor dx, dx
    div di                          ; AX = whole records, DX = the partial
    mov cx, di
    sub cx, dx                      ; ...and what it lacks of a record
    pop di
    pop ds                          ; the FCB's segment again
    pop bx                          ; the records asked
    or dx, dx
    jz .fr4
    cmp byte [si+0x1B], 0
    je .frfull                      ; a write is short only when the disk is
    inc ax                          ; a partial is a record read, padded
    push ax
    xor al, al
    rep stosb
    pop ax
    mov dl, 3
    jmp short .fr5
.frw1:
    xor ax, ax                      ; no record moved, and AL = 2: a wrap in
    mov dl, 2                       ; the DTA reads and writes nothing
    jmp short .fr5
.frfull:
    xor dx, dx                      ; ...full, which is AL = 1
.fr4:
    cmp ax, bx
    je .fr5                         ; DL = 0: all of them
    inc dx                          ; DL = 1: the file ended
.fr5:
    cmp byte [si+0x1B], 0           ; a write: the file's size into the FCB,
    jne .fr6                        ; from its end
    push ax
    push dx
    mov bx, [si+0x18]
    mov ax, 0x4202
    xor cx, cx
    cwd
    int 0x21
    mov [si+0x10], ax
    mov [si+0x12], dx
    pop dx
    pop ax
.fr6:
    add [si+0x21], ax
    adc word [si+0x23], 0
    xchg cx, ax
    mov al, dl
    pop dx                          ; the program's CX
    cmp byte [si+0x1A], 0x27
    jae .fq1                        ; 27h/28h answer in CX
    xchg cx, dx                     ; DX = 0 or 1 record moved
    cmp byte [si+0x1A], 0x21
    jb .fq1
    sub [si+0x21], dx               ; 21h/22h: the random record stays put
    sbb word [si+0x23], 0
.fq1:
    les bx, [si+0x21]               ; block and current from where it ended:
    mov dx, es
    mov [si+0x20], bl               ; current = record mod 128...
    and byte [si+0x20], 0x7F
    shl bx, 1                       ; ...block = record / 128
    rcl dx, 1
    mov bl, bh
    mov bh, dl
    mov [si+0x0C], bx
    cmp byte [si+0x1A], 0x15
    ja .fcbrj
    les bx, [si+0x1C]               ; 14h/15h: the random record given back
    mov [si+0x21], bx
    mov [si+0x23], es
.fcbrj:
    jmp .fcbret
.fcbsr:
    ; AH=24h: the random record from the current block and record
    push bx
    push ax
    call .fcbptr
    call .fcbcur
    mov [si+0x21], ax
    mov [si+0x23], dx
    xor al, al
    jmp .fcbret
.fcbcur:                            ; DX:AX = block x 128 + current record
    mov ax, [si+0x0C]
    xor dx, dx
    mov dl, ah
    mov ah, al
    xor al, al
    shr dx, 1
    rcr ax, 1
    or al, [si+0x20]
    ret
.fcbptr:                            ; out DS:SI = the FCB the program named,
    mov ds, [bp]                    ; past an extended one's seven bytes; AL
    mov si, [bp-8]                  ; = 0, or an extended FCB's attribute
    mov al, 0                       ; with bit 7 set. AH is kept
    cld                             ; (the program's DF is not ours)
    cmp byte [si], 0xFF
    jne .fp1
    mov al, [si+6]
    or al, 0x80
    add si, 7
.fp1:
    ret
.fcbcp:                             ; CL name bytes DS:SI -> ES:DI, no blanks
    xor ch, ch
.fc1:
    lodsb
    cmp al, ' '
    je .fc2
    stosb
.fc2:
    loop .fc1
    ret
.setvfy:
    and al, 1                       ; AH=2Eh: AL = 0/1, kept for AH=54h to
    mov [dos_vfy], al               ; give back. The write path has no
    jmp .ok                         ; verify to switch: a flag, as DOS keeps
.getvfy:
    mov al, [dos_vfy]               ; AH=54h: out AL
    jmp .ok
.gettime:
    ; AH=2Ch: out CH = hours, CL = minutes, DH = seconds, DL = hundredths.
    call dos_date_roll
    call dos_time_now
    mov [bp-8], dx                  ; DH/DL are the answer (SPEC.md 96.7.1)
    jmp .ok
.settime:
    ; AH=2Dh: CH/CL/DH/DL as above; out AL = 0 or FFh.
    cmp ch, 23
    ja .tbad
    cmp cl, 59
    ja .tbad
    cmp dh, 59
    ja .tbad
    cmp dl, 99
    ja .tbad
    call dos_time_set
    xor al, al
    jmp .ok
.tbad:
    mov al, 0xFF
    jmp .ok

; --- AH=4Bh: load and run a CHILD (SPEC.md 96.14) ---------------------------
.retcode:
    ; AH=4Dh: out AL = the child's exit code, AH = how it ended (0 = normally).
    mov al, [dos_chexit]
    xor ah, ah
    jmp .ok

.exec:
    ; AL = 0 load-and-execute, DS:DX = the name, ES:BX = the parameter block.
    push bx
    or al, al
    jnz .exbadfn                    ; AL=1 (load, do not run) and AL=3 (an
                                    ; overlay) are different shapes and neither
                                    ; is built (SPEC.md 96.14.2)

    ; --- COMMAND.COM IS NOT A FILE HERE (SPEC.md 96.30) --------------------
    ; Before the name is parsed, before the arena is asked for anything: a
    ; shell-out runs one built-in command and comes straight back. Nothing is
    ; loaded, so dos_inchild is not set and the one-level rule below does not
    ; apply - a program may shell out as often as it likes.
    call dsh_isshell
    jc .noshell
    call dsh_tail                   ; ES:BX is still the parameter block
    mov [dos_chexit], al            ; ...and AH=4Dh is where the caller reads
    xor ax, ax                      ; the command's own verdict
    pop bx
    jmp .ok
.noshell:
    cmp byte [dos_inchild], 0
    jne .exnest                     ; ONE level, and it is a decision - see
                                    ; SPEC.md 96.14.1
    mov [dos_xparm], bx             ; the parameter block, banked while the
    mov [dos_xparms], es            ; name is copied out of the same segment
    call dos_fh_name
    jc .exerr
    call .fhabs
    jc .fhpath

    call dos_exec_load              ; block, load, relocate, PSP, command tail
    jc .exerr                       ; AL is a DOS code
    call dos_fh_leave               ; ...and HOME BEFORE THE CHILD RUNS: under
                                    ; DOS, EXEC "B:FOO" does not leave the
                                    ; program on B:, and the restore at .fhok
                                    ; is on the far side of the whole child
                                    ; (SPEC.md 96.6.2)

    ; --- into the child ----------------------------------------------------
    ; THE `call` BELOW IS THE RETURN PATH. dos_terminate cannot jump to a
    ; label in here - a global one would re-scope every local label after it -
    ; so the child's exit puts SP back one word BELOW what is banked here and
    ; `ret`s, landing on the word this call is about to push.
    ;
    ; **AND BP IS THE CHILD'S TO DESTROY** (SPEC.md 96.14.4). The gate's whole
    ; epilogue is BP-relative - `mov si,[bp-2]`, `mov es,[bp-6]`, `mov sp,bp`
    ; - and `dos_prog_enter` never sets BP, so a child that leaves it alone
    ; hands the PARENT'S OWN BP back by accident and a child that uses it
    ; loads the parent's SP out of rubble. The machine then walks off the end
    ; of memory: measured on the Playroom, whose launcher EXECs a 114KB game
    ; and whose exit left the CPU marching through CFCFh with the screen
    ; blank. It is why `tests/dosexec.py` passed for a year - its child is a
    ; probe that never touches BP.
    push bp                         ; ...so the bank below is taken AFTER this
    mov ax, ss                      ; push, and the child's `ret` lands on the
    mov [dos_psv_ss], ax            ; `pop` rather than on the call site
    mov [dos_psv_sp], sp
    mov byte [dos_inchild], 1
    call dos_prog_enter             ; ...and comes back HERE when it exits
    pop bp
    call dos_exec_unload            ; the child's block, back to the chain
    xor ax, ax
    jmp .fhok
.exbadfn:
    mov al, 1                       ; "invalid function"
    jmp short .exerr
.exnest:
    mov al, 8                       ; "not enough memory", which is the honest
                                    ; DOS answer for a child that cannot run
.exerr:                             ; AL = a DOS code: .fherr clears AH itself,
    jmp .fherr                      ; so this is a near door and nothing more

.resize:
    ; AH=4Ah: ES = the block, BX = the paragraphs wanted (SPEC.md 96.9).
    call dos_mcb_resize
    jc .badax3
    jmp .ok

.alloc:
    ; AH=48h: BX = paragraphs wanted; out AX = the segment. A refusal answers
    ; the LARGEST available in BX, which is how a program asks "how much is
    ; there" - BX=FFFFh is that question and must get a truthful number.
    call dos_mcb_alloc
    jc .badax3
    jmp .ok

.free:
    ; AH=49h: ES = a segment we handed out.
    call dos_mcb_free
    jc .badax3
    jmp .ok

.ver:
    mov ax, 0x1E03                  ; AL = 3, AH = 30: DOS 3.30 (SPEC.md
    xor bx, bx                      ; 96.21.7). The version is a SETTING and
    xor cx, cx                      ; not a constant the day a program wants
    jmp .ok                         ; 5.00 - reporting a version whose
                                    ; functions we lack is worse than reporting
                                    ; a lower one, because a program branches
                                    ; on it (DOS-EXEC-PLAN 12 q1) - and 3.31
                                    ; was half a release above the machine this
                                    ; DOS is measured against, for no feature
                                    ; it has. BX and CX are the OEM and serial,
                                    ; and 0/0 is what IBM DOS 3.30 answers too

.badax3: jmp .badax                 ; a near door, as above

.dskreset:
    ; AH=0Dh - DISK RESET: flush what is buffered and forget the rest. There
    ; is one buffer here, the write window, and flushing it is the whole of
    ; what this call can mean - a program issues it precisely so that what it
    ; has written is on the disk before it does something else. It used to
    ; fall to `.bad` and answer `invalid function`, which Works's trace
    ; against IBM DOS 3.30 showed as an answer DOS does not give (96.11.8).
    ; **DOS RETURNS NOTHING AND CANNOT FAIL**, so a flush that refuses is
    ; swallowed rather than reported: there is no register to report it in,
    ; and the close will try again and has somewhere to say so.
    ; **AND AH=68h/6Ah, COMMIT FILE, IS THE SAME CALL** (SPEC.md 96.53):
    ; one window and one held stream on the machine, so committing one
    ; handle's writes and committing all of them are the same two steps.
    push ax                         ; (AX comes back, AH above all: 96.56.3)
    call dos_fh_flush               ; ONE window, so this IS "flush everything"
    call dos_fh_cmall               ; ...and the held stream's FAT and entry
    pop ax
    jmp .ok

.ioctl:
    ; AH=44h - IOCTL, and only the two sub-functions a C runtime asks
    ; (SPEC.md 96.22). AL=00h is "WHAT IS THIS HANDLE?", and it is the call a
    ; library makes on the handle it has just opened, before it reads a byte.
    ; A shim that refuses it hands back CF=1 with DX UNTOUCHED - so the
    ; library tests bit 7 of whatever the program happened to leave in DL and,
    ; when that bit is set, concludes a data file is a character device like
    ; CON: it stops seeking and stops sizing, and the program reports its own
    ; files missing having successfully opened every one of them.
    or al, al
    je .ioc_get
    cmp al, 0x01
    je .ioc_set
    cmp al, 0x08
    je .ioc_rem
    jmp .bad                        ; the block-device sub-functions are a
.ioc_get:                           ; different feature, refused by name
    cmp bx, DOS_FH0
    jae .ioc_file
    ; THE FIVE STANDARD HANDLES ARE NOT ALL THE CONSOLE, and answering as
    ; though they were is what a real DOS does not do (SPEC.md 96.22.1).
    ; Measured against IBM DOS 3.30 on the same machine: handle 3 is AUX and
    ; answers 80C0h, handle 4 is PRN and answers A0C0h - bit 13 is the
    ; printer's "output until busy" - and only 0, 1 and 2 are the console's
    ; 80D3h. A C runtime classifies all five at start-up, so telling it the
    ; printer is a console is a wrong answer it keeps.
    mov dx, 0x80D3
    cmp bx, 3
    jb .ioc_done                    ; 0, 1, 2: the console
    mov dx, 0x80C0
    je .ioc_done                    ; 3: AUX
    mov dx, 0xA0C0                  ; 4: PRN
    jmp short .ioc_done
.ioc_file:
    push bx                         ; dos_fh_slot spends BX and SI, and both
    push si                         ; are the program's here - and SI STAYS
    call dos_fh_slot                ; alive across the reads below, which is
    jc .ioc_fbad                    ; what the two fixes here both needed
    mov dl, [si+FH_VOL]             ; **THE HANDLE'S OWN DRIVE, NOT DOS'S**
    and dl, 0x3F                    ; (SPEC.md 96.7.1.2). Bits 0-5 are the drive
    xor dh, dh                      ; the FILE is on and [dos_vol] is where the
                                    ; PROGRAM is standing; they are the same
                                    ; number until a program opens `B:NAME`
                                    ; from A:, and IBM DOS 3.30 answers 0041h
                                    ; for exactly that - the file's drive, with
                                    ; DOS on A:. BIT 7 CLEAR = a file,
                                    ; which is the whole question asked
    test byte [si+FH_FLAGS], FHF_WROTE
    jnz .ioc_fok                    ; **BIT 6 IS "HAS NOT BEEN WRITTEN
    or dl, 0x40                     ; THROUGH"** and it is the row SPEC.md
.ioc_fok:                           ; 96.7.1.1 recorded and could not fix: it
    pop si                          ; needed a per-handle flag, and FH_FLAGS
    pop bx                          ; had a spare bit. Measured on the same
    jmp short .ioc_done             ; binary: 0041h freshly opened, 0041h
.ioc_fbad:                          ; freshly CREATED, 0001h once written
    pop si
    pop bx
    jmp short .ioc_bad
.ioc_done:
    mov ax, dx                      ; DOS answers AX = DX here too, and a
    mov [bp-8], dx                  ; library may read either (SPEC.md 96.7.1)
    jmp .ok
.ioc_set:
    or dh, dh                       ; DH must be zero: anything else is a
    jne .ioc_bad                    ; device request, and we have no device
    jmp .ok
.ioc_rem:
    ; AL=08h - DOES THIS DRIVE USE REMOVABLE MEDIA? BL = the drive, 0 being
    ; the default one; out AX = 0 removable, 1 fixed. It is the question a
    ; program asks before it CACHES a directory, and refusing it is what
    ; Works's trace against IBM DOS 3.30 showed as the first differing answer
    ; (SPEC.md 96.22.2). OSAPI_VOL_KIND's VK_REMOVABLE/VK_FIXED are the same
    ; two values in the same order, so the door's answer IS this one.
    mov al, bl
    or al, al
    jnz .ioc_rdrv
    mov al, [dos_vol]               ; 0 = "the drive I am standing on"
    inc al
.ioc_rdrv:
    dec al                          ; 1 = A:, and a volume index is 0-based
    call dos_be_vkind               ; out CF=1 no such volume, else AL = VK_*
    jc .ioc_rbad
    xor ah, ah                      ; ...and DX is NOT this call's answer, so
    jmp .ok                         ; [bp-8] is left alone: only AX is
.ioc_rbad:                          ; published, which .ok does
    mov ax, 15                      ; invalid drive, which is what DOS answers
    jmp .badax                      ; for a letter it has no volume for
.ioc_bad:
    mov ax, 6                       ; invalid handle
    jmp .badax

.getattr:
    ; AH=43h - the attribute pair. AL=00h is how a program asks "IS THIS FILE
    ; THERE?" without opening it, so refusing it answers "no" for every file
    ; on the disk.
    or al, al
    je .att_get
    cmp al, 0x01
    je .att_set
    jmp .bad
.att_get:
    push bx
    call dos_fh_name
    jc .fherr4
    call .fhabs
    jc .fhpath4
    cmp byte [dos_fname], 0         ; **A ROOT PARSES TO NO NAME AT ALL** and
    je .att_dir                     ; is a DIRECTORY, not a missing file
                                    ; (SPEC.md 96.12.4): `A:\` is what
                                    ; Microsoft Works asks about before it
                                    ; saves to another drive, and answering 2
                                    ; is `Directory not found` on the glass
    call dos_fh_stat                ; the same lookup AH=3Dh opens through, so
    jc .att_dirq                    ; the two can never disagree about a name
    mov cx, 0x20                    ; ARCHIVE. SPEC.md 19 keeps no attribute of
    mov ax, cx                      ; its own and this is what an ordinary
    jmp .fhok                       ; readable file reads as everywhere. .fhok
                                    ; AND NOT .ok: these two were the only
                                    ; dos_fh_name callers that popped BX and
                                    ; left by the front door, which since
                                    ; SPEC.md 96.6.2 is also the door that
                                    ; comes off the named drive
.att_dirq:
    call dos_att_isdir              ; ...and a FOLDER by that name is the other
    jc .fnoent4                      ; thing AH=43h is asked about. Only a name
.att_dir:                           ; that is NEITHER is `file not found`
    mov cx, 0x10                    ; DIRECTORY, which is the bit every caller
    mov ax, cx                      ; tests
    jmp .fhok
.att_set:
    push bx
    call dos_fh_name                ; it still has to NAME something real...
    jc .fherr4
    call .fhabs
    jc .fhpath4
    call dos_fh_stat
    jc .fnoent4
    xor ax, ax                      ; ...and the new attributes are then
    jmp .fhok                       ; DROPPED rather than refused: there is
                                    ; nowhere to keep them, and a program that
                                    ; sets ARCHIVE on a file it has just
                                    ; written must not fail for it

.fherr4: jmp .fherr                 ; near doors, as above
.fhpath4: jmp .fhpath
.fnoent4: jmp .fnoent

.dconio:
    ; AH=06h - direct console I/O. DL=FFh asks for a character WITHOUT
    ; waiting, and answers ZF=1 when there is none: a flag in the pushed
    ; image, like the carry, and not a live one.
    cmp dl, 0xFF
    je .dcin
    mov al, dl
    call dos_tty
    jmp .ok
.dcin:
    mov ah, 1
    int 0x16
    jz .dcnone
    xor ah, ah
    int 0x16                        ; AL = the character, and TAKE it
    and word [bp+8], 0xFFBF         ; ZF=0: there was one
    jmp .ok
.dcnone:
    xor al, al
    or word [bp+8], 0x40            ; ZF=1: nothing waiting
    jmp .ok

.flushin:
    ; AH=0Ch - throw away what has been typed ahead, then BE the function in
    ; AL. Only the input calls are legal there; DOS does the flush either way
    ; and ignores anything else, which is what a program relies on when it
    ; clears the buffer with AL=0 before asking a question.
    push ax
.fl_loop:
    mov ah, 1
    int 0x16
    jz .fl_done
    xor ah, ah
    int 0x16
    jmp short .fl_loop
.fl_done:
    pop ax
    mov ah, al
    cmp ah, 0x01
    je .getce5
    cmp ah, 0x06
    je .dconio
    cmp ah, 0x07
    je .getce5
    cmp ah, 0x08
    je .getce5
    jmp .ok                         ; flushed, and the rest is not ours
.getce5: jmp .getce                 ; a near door, as above

.bad:
    mov [dos_badfn], ah             ; the window NAMES it (SPEC.md 47): an
    mov ax, 1                       ; unsupported program reports its own gap
.badax:
    mov dx, [bp-8]                  ; BEFORE the trace, so the trace records
%ifdef DOSTRACE                     ; what the PROGRAM is about to see
    stc                             ; ...as this exit is about to return it
    call dos_tr_result
%endif
    or word [bp+8], 1               ; CF=1 in the RETURNED flags
    jmp short .leave
.ok:
    mov dx, [bp-8]
%ifdef DOSTRACE
    clc
    call dos_tr_result
%endif
    and word [bp+8], 0xFFFE         ; CF=0 in the returned flags
.leave:
    ; STKBALANCE-OK: the gate banks SI, DI and ES below `bp` and unwinds them
    ; with `mov sp, bp` rather than three pops, so every exit reads +3 to a
    ; walker that counts pushes. That is the POINT of the arrangement - the
    ; frame is restored from `bp`, so the gate's promise does not rest on
    ; every handler below it being balanced (SPEC.md 96.7.1).
    ;
    ; SI, DI AND ES GO BACK, AND THAT IS NOT TIDINESS (SPEC.md 96.7.1).
    ; No INT 21h function returns SI or DI, so a program keeps live pointers
    ; in them across a call - and the file handlers here use SI as the address
    ; of the handle record and hand it back, so an `open` returned a pointer
    ; into OUR OWN table in a register the program was still using. ES is the
    ; same guarantee with two documented exceptions, and AH=35h and AH=2Fh
    ; make theirs by writing the BANKED slot rather than the live register -
    ; the way the carry flag is already returned.
    ;
    ; **AND SO DOES DX, WHICH IS THE SAME DEFECT MEASURED RATHER THAN
    ; REASONED ABOUT** (SPEC.md 96.7.1.1). DX was left live because five
    ; functions answer in it, which is four more than ES has - so the
    ; argument above was made and then not applied. Diffed against a real
    ; IBM DOS 3.30 running the same program off the same disk, AH=3Dh
    ; destroyed DX on **37 calls out of 37** and AH=43h on 7 of 7, where DOS
    ; preserves DS:DX on both: `dos_fh_stat` answers the file's size in
    ; DX:AX and `.open` simply never puts it back. A program that keeps a
    ; pointer in DX across an `open` - which every program may, because DOS
    ; lets it - reads our file size as an address. The five that really do
    ; answer in DX (42h, 36h, 2Ah, 2Ch, 44h) write the banked slot, exactly
    ; as AH=35h does for ES.
    mov si, [bp-2]
    mov di, [bp-4]
    mov es, [bp-6]
    mov sp, bp                      ; ...and whatever depth a handler left at,
%ifdef DOS_PMGATE
    mov ds, [cs:dos_csel]           ; (a handler may have left any DS)
    pop word [dos_gds]
%else
    pop ds                          ; so the gate's promise does not rest on
%endif
    pop bp                          ; every one of them being balanced

    ; --- AND BACK ONTO THE PROGRAM'S STACK TO `iret` (SPEC.md 96.7.2) -------
    ; Our stack now holds the three replicas, then the four words the
    ; prologue banked. Only the FLAGS word travels: the handlers edit it
    ; rather than executing `stc`, which an `iret` would discard, so it is
    ; written into the frame the program's own `int` left and the return is
    ; taken there.
    cli                             ; **BEFORE THE FIRST SCRATCH STORE**, not
                                    ; merely around the SS:SP pair: the cells
                                    ; below are one deep, exactly as in the
                                    ; prologue, and the prologue is safe only
                                    ; because the gate is entered with IF
                                    ; already 0. Here it is 1, and DOS is no
                                    ; more re-entrant than a real one - but
                                    ; this is fifteen instructions and closing
                                    ; the window costs nothing
%ifdef DOS_PMGATE
    mov [dos_gax], ax               ; (DS ours: dos_csel)
    pop ax
    pop ax
    pop ax
    mov [dos_gfl], ax
    pop ax
    mov [dos_onprog], al
    pop ax
    mov [dos_dstk], ax
    mov [dos_gbp], bp
    pop bp
    pop ax
    mov ss, ax
    mov sp, bp
    mov ax, [dos_gfl]
    mov [bp+4], ax
    mov bp, [dos_gbp]
    mov ax, [dos_gax]
    mov ds, [dos_gds]               ; the program's DS, last
    iret
%else
    mov [cs:dos_gax], ax
    pop ax                          ; the replica IP and CS are ours and go
    pop ax                          ; nowhere: the program's own pair is still
    pop ax                          ; on its stack, untouched
    mov [cs:dos_gfl], ax            ; ...but the ANSWER's flags do travel
    pop ax
    mov [cs:dos_onprog], al
    pop ax
    mov [cs:dos_dstk], ax           ; the depth a child's exit gives back
    mov [cs:dos_gbp], bp            ; BP is the program's again by now, and it
    pop bp                          ; is where the program's SP comes back to
    pop ax
    mov ss, ax                      ; SS and SP as a pair, as everywhere else
                                    ; in this file - and IF has been 0 since
                                    ; the top of this block
    mov sp, bp                      ; ...and BP is the frame the store below
                                    ; wants anyway, so nothing is re-derived
    mov ax, [cs:dos_gfl]
    mov [bp+4], ax                  ; the FLAGS the `iret` is about to pop
    mov bp, [cs:dos_gbp]
    mov ax, [cs:dos_gax]
    iret                            ; ...which puts IF back with them
%endif
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_terminate - back to the bracket, on our own stack
; in:  AL = the exit code
; out: never returns
;
; **WHICH STACK IT ARRIVES ON DEPENDS ON THE DOOR AND IT DOES NOT MATTER**:
; AH=4Ch comes through dos_int21, which since SPEC.md 96.7.2 has already
; swapped to ours, while INT 20h and INT 22h jump straight here on the
; program's. Nothing below reads the stack it is standing on - both arms load
; SS:SP from a banked pair before they push anything.
; -----------------------------------------------------------------------------
dos_terminate:
    cli
    mov [cs:dos_exit], al           ; through CS: DS is the program's and the
    cmp byte [cs:dos_inchild], 0
    je .top
    ; --- A CHILD (SPEC.md 96.14): back to the parent, not out of the bracket.
    ; SP goes one word BELOW what AH=4Bh banked, because the `call
    ; dos_prog_enter` it made pushed exactly that word - so the `ret` here
    ; lands inside the handler with no global label to jump to.
    mov [cs:dos_chexit], al
    mov byte [cs:dos_inchild], 0
    mov ax, [cs:dos_psv_ss]
    mov ss, ax
    mov ax, [cs:dos_psv_sp]
    dec ax
    dec ax
    mov sp, ax
    sti
    push cs
    pop ds
    call dos_exec_back
    ret
.top:
    mov ax, [cs:dos_sv_ss]          ; stack is about to stop existing
    mov ss, ax
    mov sp, [cs:dos_sv_sp]
    mov byte [cs:dos_onprog], 0     ; back on the UI task's own stack, so a
    sti                             ; back-end call stops borrowing it
    push cs                         ; ...and back into dos_fsx_main's flow with
    pop ds                          ; our own DS, which every proc below wants
    jmp dos_prog_done
%endif                              ; DOS_EXTCORE

; -----------------------------------------------------------------------------
; dos_tty - one character to the screen, through the ROM
; in:  AL = the character
; out: nothing; preserves everything but the flags
; -----------------------------------------------------------------------------
%ifdef DOSTRACE
; EVERY INT 21h CALL, INTO A RING THE HOST READS - not onto the screen.
;
; The first version of this printed AH through the ROM teletype, which is
; unusable for exactly the programs worth tracing: a game SETS A MODE and owns
; every pixel (SPEC.md 53.7), so the characters land in a framebuffer nobody
; can read back as text, and the 60-call cap ran out during the C runtime's
; own start-up. A ring in .bss costs the traced program nothing, survives the
; mode change, and is read off the guest with the package's own segment - the
; way every other host-side probe in this tree reads package state.
;
; AH and AL both, because the sub-function is the interesting half of 44h,
; 43h, 42h and 4Eh alike. DOS_TRACEN entries, wrapping, with the TOTAL kept
; separately so a reader can tell a wrapped ring from a short one.
; --- dos_tr_name_in - bank [dos_fname], up to DOS_TRNM_N of them -----------
; Every name the program hands the file API, in order. DS is ours here (the
; caller has just restored it) and every register must survive.
dos_tr_name_in:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov al, [dos_trnmi]
    cmp al, DOS_TRNM_N
    jae .out                        ; keep the FIRST ones: the interesting
    inc byte [dos_trnmi]            ; open is early and the tail is noise
    mov bl, al
    xor bh, bh
    mov ax, 13
    mul bx
    mov di, ax
    add di, dos_trnm
    push ds
    pop es
    mov si, dos_fname
    mov cx, 13
    cld
    rep movsb
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

dos_tr_name: db 'TRACE.LOG', 0
dos_tr_hdr:  db 'os8088 DOS INT 21h trace', 13, 10
             db 'AX BX CX DX > AXout/CF, oldest first. TOTAL/WRAP ', 0

; --- AL -> two hex digits at DI; AX and the flags preserved -----------------
dos_tr_hex2:
    push ax
    push ax
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call dos_hexd
    mov [es:di], al
    inc di
    pop ax
    and al, 0x0F
    call dos_hexd
    mov [es:di], al
    inc di
    pop ax
    ret

; --- AX -> four hex digits at DI -------------------------------------------
dos_tr_hex4:
    push ax
    mov al, ah
    call dos_tr_hex2
    pop ax
    call dos_tr_hex2
    ret
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; -----------------------------------------------------------------------------
; dos_trace_dump - the ring, as TEXT, into TRACE.LOG beside the program
;
; The ring is unreadable from inside the guest - a DOS program owns the screen
; - and reading it off the host needs a debugger the field does not have. So
; DOS writes it: one file, plain text, in the directory the program was
; launched from, replaced on every run.
;
; UI-TASK CONTEXT, which is why it hangs off the end of the bracket and not
; off dos_terminate: the file API is the UI task's (SPEC.md 20.6 rule 7) and
; the program's own stack is gone by here anyway.
; -----------------------------------------------------------------------------
dos_trace_dump:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [dos_trseg], 0         ; no part, nothing to dump (SPEC.md
    je .nopart                      ; 96.29.1)
    mov es, [dos_trseg]             ; **ES IS THE PART FOR THE WHOLE ROUTINE**,
                                    ; which is what makes this cheap: the ring
                                    ; is read through it, the text is written
                                    ; through it, and OSAPI_FILE_WRITE takes
                                    ; ES:BX already - so handing 18KB of
                                    ; rendered log to the file system costs
                                    ; not one instruction more than it did
                                    ; when the buffer was our own bss

    mov di, dos_trdump
    mov si, dos_tr_hdr
.hdr:
    lodsb
    or al, al
    jz .hdrend
    mov [es:di], al
    inc di
    jmp short .hdr
.hdrend:
    mov ax, [dos_tracen]
    call dos_tr_hex4
    mov byte [es:di], '/'
    inc di
    mov ax, [dos_tracew]
    call dos_tr_hex4
    mov byte [es:di], 13
    inc di
    mov byte [es:di], 10
    inc di

    ; WHERE THE OLDEST ENTRY IS depends on whether the ring has wrapped: a
    ; short run starts at 0, a wrapped one starts at the write index.
    mov cx, [dos_tracen]
    cmp cx, DOS_TRACEN
    jbe .short
    mov cx, DOS_TRACEN
    mov bx, [dos_tracew]
    jmp short .go
.short:
    xor bx, bx
.go:
    cmp cx, DOS_TRDUMPN             ; the FILE holds no more than the ring
    jbe .fits
    mov cx, DOS_TRDUMPN
.fits:
    and bx, (DOS_TRACEN * DOS_TRACE_SZ) - DOS_TRACE_SZ
    or cx, cx
    jz .write
    xor dx, dx                      ; DX = entries on this line
.ent:
    mov ax, [es:bx+dos_traceb]         ; AX=
    call dos_tr_hex4
    mov byte [es:di], ' '
    inc di
    mov ax, [es:bx+dos_traceb+2]       ; BX=
    call dos_tr_hex4
    mov byte [es:di], ' '
    inc di
    mov ax, [es:bx+dos_traceb+4]       ; CX=
    call dos_tr_hex4
    mov byte [es:di], ' '
    inc di
    mov ax, [es:bx+dos_traceb+6]       ; DX=
    call dos_tr_hex4
    mov byte [es:di], '>'              ; ...and what it ANSWERED
    inc di
    mov ax, [es:bx+dos_traceb+8]
    call dos_tr_hex4
    mov byte [es:di], '/'
    inc di
    mov ax, [es:bx+dos_traceb+10]
    call dos_tr_hex4
    mov byte [es:di], '/'              ; ...and ES:BX, which for AH=35h, 48h and
    inc di                          ; 2Fh IS the answer and AX is not
    mov ax, [es:bx+dos_traceb+14]
    call dos_tr_hex4
    mov byte [es:di], ':'
    inc di
    mov ax, [es:bx+dos_traceb+12]
    call dos_tr_hex4
    mov byte [es:di], '@'              ; ...and WHO CALLED, which is what a pair
    inc di                          ; of traces that diverge with no call in
    mov ax, [es:bx+dos_traceb+18]      ; between is read on
    call dos_tr_hex4
    mov byte [es:di], ':'
    inc di
    mov ax, [es:bx+dos_traceb+16]
    call dos_tr_hex4
    mov byte [es:di], '/'
    inc di
    mov ax, [es:bx+dos_traceb+20]
    call dos_tr_hex4
    mov byte [es:di], 13
    inc di
    mov byte [es:di], 10
    inc di
    add bx, DOS_TRACE_SZ
    and bx, (DOS_TRACEN * DOS_TRACE_SZ) - DOS_TRACE_SZ
    dec cx                          ; ...and NOT `loop`: the body outgrew its
    jz .write                       ; own short displacement when the entry
    jmp .ent                        ; learned to say who called
.write:
    mov byte [es:di], 13
    inc di
    mov byte [es:di], 10
    inc di
    ; --- and the NAMES, one per line ------------------------------------
    mov si, dos_trnm
    mov cl, [dos_trnmi]
    xor ch, ch
    or cx, cx
    jz .nonames
.nm:
    push cx
    mov cx, 13
.nmc:
    lodsb
    or al, al
    jz .nmpad
    mov [es:di], al
    inc di
    loop .nmc
    jmp short .nmeol
.nmpad:
    dec cx                          ; step over the rest of the fixed field
    jz .nmeol
    add si, cx
.nmeol:
    mov byte [es:di], 13
    inc di
    mov byte [es:di], 10
    inc di
    pop cx
    loop .nm
.nonames:
    mov cx, di
    sub cx, dos_trdump              ; CX = how much of it there is
    mov bx, dos_trdump              ; ES IS ALREADY THE PART, so the `push ds /
    mov si, dos_tr_name             ; pop es` that used to be here is gone -
    xor dx, dx                      ; the slot's buffer argument was ES:BX all
    call OSAPI_FILE_WRITE           ; along (os88api.inc). Creates or REPLACES,
                                    ; in the directory the program was
                                    ; launched from
.nopart:
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)
%endif                              ; KD_BACKEND

dos_trace:
    cmp ah, 0x02                    ; NOT the console writers. A 110-character
    je .skip                        ; message is 110 entries, which is a whole
    cmp ah, 0x09                    ; ring of noise standing exactly where the
    je .skip                        ; calls that CAUSED it used to be - and the
    cmp ah, 0x06                    ; message can be read off the screen
    je .skip
    cmp word [dos_trseg], 0         ; **NO PART, NO TRACE** (SPEC.md 96.29.1).
    je .skip                        ; It is OP_OPT, so a machine that could
                                    ; not spare 35KB runs the program with the
                                    ; instrument silent - which is the whole
                                    ; point of an optional part, and far
                                    ; better than 35KB of stores into segment
                                    ; zero
    push ax
    push bx
    push si
    push es
    mov es, [dos_trseg]             ; ES IS THE PART from here to the pop, and
                                    ; every store below carries the override.
                                    ; ES on entry is the CLIENT's - the gate
                                    ; banked the program's at [bp-6] - so it
                                    ; is ours to borrow and must be put back
    mov si, [dos_tracew]
    and si, (DOS_TRACEN * DOS_TRACE_SZ) - DOS_TRACE_SZ  ; the ring's byte index, entry-aligned - a
    add si, dos_traceb              ; power-of-two stride so this is an AND
    mov [dos_tracei], si            ; where any other size needs a divide
    mov [es:si], ax                    ; AX carries the function AND its
    mov [es:si+2], bx                  ; sub-function; the other three carry what
    mov [es:si+4], cx                  ; it is ABOUT - a handle, a count, an
    mov [es:si+6], dx                  ; offset, a name's address. All four are
                                    ; still the caller's: `push` does not
                                    ; change what it pushes
    mov word [es:si+8], 0xFFFF         ; ...no result yet, so a call that never
    mov word [es:si+10], 0xFFFF        ; returned is visible as one
    mov word [es:si+12], 0xFFFF
    mov word [es:si+14], 0xFFFF

    ; --- WHO CALLED, which is the question AH and its arguments cannot answer.
    ; Two runs that make the same calls with the same arguments and then
    ; diverge have already diverged somewhere with no call in it, and the only
    ; thing that says where is the CS:IP the `int` pushed. CS is recorded raw
    ; and the reader subtracts the PSP, so the offset compares across two
    ; machines that loaded the program at different addresses.
    mov ax, [bp+4]
    mov [es:si+16], ax                 ; the return IP - one instruction past the
    mov ax, [bp+6]                  ; `int 21h` that got here
    mov [es:si+18], ax                 ; ...and its CS
    mov ax, [bp]                    ; ...and DS, BP: BOTH OFF THE FRAME, and
    mov [es:si+20], ax                 ; the live registers are NOT them -
    mov ax, [bp+2]                  ; dos_int21 pushed its own over the
    mov [es:si+26], ax                 ; caller's before this was reached
    pop ax                          ; SI is the caller's, under the push above
    push ax
    mov [es:si+22], ax
    mov [es:si+24], di                 ; DI is untouched from the gate
    mov ax, ss                      ; SS is still the program's - a DOS call
    mov [es:si+28], ax                 ; runs on the caller's stack
    lea ax, [bp+10]                 ; ...at the SP the `int` was taken on
    mov [es:si+30], ax

    add word [dos_tracew], DOS_TRACE_SZ
    inc word [dos_tracen]           ; ...and the TOTAL, which does not wrap
    pop es
    pop si
    pop bx
    pop ax
    ret                             ; ...and NOT into .skip below, which would
                                    ; zero the entry pointer this just set
.skip:
    mov word [dos_tracei], 0        ; a filtered call must not overwrite the
    ret                             ; RESULT of the one before it

; --- dos_tr_result - what the call answered, into its own entry -------------
; in: AX = the answer, CF as it will be returned. Called from the two exits.
;
; ES AND BX ARE RECORDED TOO, and they are not padding. AX and the carry are
; the answer to most calls and to some they are not the answer at all:
; AH=35h's is ES:BX, AH=48h's block is AX with the FAILURE size in BX, and
; AH=2Fh's DTA is ES:BX as well. A ring that logs only AX reads those three as
; "returned 0, no error" - which is how a trace can be complete, correct, and
; silent about the value the program actually branched on.
dos_tr_result:
    push si
    push ax
    push es
    pushf                           ; CF IS THE SUBJECT here, and `or si, si`
    mov si, [dos_tracei]            ; two lines down would destroy it
    or si, si
    jz .out                         ; filtered, or no call in flight
    mov es, [dos_trseg]             ; the part (SPEC.md 96.29.1), and no guard
                                    ; of its own: [dos_tracei] is only ever
                                    ; SET by dos_trace, which refuses without
                                    ; a segment, so a zero there already means
                                    ; there is no entry to finish
    mov [es:si+8], ax
    mov word [es:si+10], 0
    mov [es:si+12], bx                 ; ...the OTHER answer, whole
    mov ax, [bp-6]                  ; ES as the PROGRAM will get it, off the
    mov [es:si+14], ax                 ; gate's banked slot and not the live
                                    ; register a handler happens to have left
    pop ax                          ; ...the flags, back off the stack
    push ax
    test al, 1                      ; CF is bit 0 of the low half
    jz .out
    mov word [es:si+10], 1
.out:
    popf
    pop es
    pop ax
    pop si
    ret
%endif
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_getkey - one character from the ROM
; in:  nothing; out: AL = the character (0 for an extended key's first half)
;
; A POLL AND NOT int 16h AH=00h, and the reason is the MOUSE. INT 33h's press
; and release counts are accumulated by whatever reads the state (SPEC.md
; 96.10.1), so a program parked in a blocking key read is the one place a
; click can happen with nothing looking - and "press a key or click" is a
; prompt DOS programs write. AH=00h is itself a spin on the BIOS buffer's head
; and tail, so sampling round it costs a machine that has ALREADY borrowed the
; screen nothing at all, and buys the wait its edges.
; -----------------------------------------------------------------------------
dos_getkey:
    push bx                         ; BX because a ROM can eat it, and CX/DX
    push cx                         ; because the mouse sample below answers in
    push dx                         ; them - DOS preserves every register but a
.poll:                              ; call's documented outputs, and INT 21h
                                    ; here hands back whatever a handler left
    mov ah, 1
    int 0x16                        ; ZF=0 with a key waiting, and it is NOT
    jnz .take                       ; consumed by the check
    call dos_mou_read               ; ...so keep the button edges alive
    jmp short .poll
.take:
    xor ah, ah
    int 0x16                        ; AL = the character, AH = the scan code.
    pop dx                          ; An extended key answers AL = 0 and DOS
    pop cx                          ; makes the caller ask twice for the scan;
    pop bx                          ; wave 1 hands back the 0 and no more
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_tty:
    push ax
    push bx
    ; --- THE HOST MAY TAKE IT (SPEC.md 96.44.3) ----------------------------
    ; `DHK_TTY` answers CF=0 when it has put the character somewhere of its
    ; own - DOS's console, which is where AH=02h/09h/40h go while the
    ; window is up (96.33). `kern_dos` sets no hook: the bracket is up from
    ; `kd_entry` to `int 19h`, the program owns the screen, and the ROM's
    ; teletype below is the only arm there has ever been on that host.
    ;
    ; A HOOK AND NOT A `%ifndef`, because the core is assembled ONCE (96.44):
    ; a build-time arm would be DOS.O88's or kern_dos's, never both.
    ; **`cs:` AND IT IS NOT DECORATION** (SPEC.md 96.33): AH=09h, AH=40h and
    ; AH=02h all reach here with DS holding the PROGRAM's segment, off the
    ; gate's frame - so a DS-relative read of our own word lands in the
    ; program's image at the same offset. MEASURED TWICE: first as a probe's
    ; `COUNT ` and `TERM ` labels vanishing while their VALUES survived, and
    ; then, when the hook replaced the `%ifndef` and the override was dropped
    ; with it, as a CALL through a word of the program's own data - a machine
    ; running wild in RAM with kern_dos loaded perfectly at KD_SEG and the
    ; screen never written at all.
    cmp word [cs:dos_hkv + DHK_TTY], 0
    je .rom
    call word [cs:dos_hkv + DHK_TTY]
    jnc .out
.rom:
    mov ah, 0x0E                    ; the machine's own screen: inside the
    mov bx, 0x0007                  ; bracket that is what the program sees,
    int 0x10                        ; and outside it there is no mode for an
.out:                               ; int 10h to write to at all
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE

; =============================================================================
; THE BACK END (SPEC.md 96.4)
; =============================================================================
; One near-call table, one implementation. It is here from the first line
; because docs/plans/DOS-EXEC-PLAN.md 14 wants a mode in which the kernel is
; hibernated out and the file half is served by something else; a table makes
; that a second back end where direct calls would make it a rewrite of every
; file function. The system already does this twice one layer down - DSV_BLK
; for a volume's blocks (SPEC.md 18.7) and DSV_FS for a whole file system
; (SPEC.md 51.8) - and this is the same want one layer up.
;
; NO INT 21h HANDLER MAY CALL AN OSAPI_* FILE SLOT DIRECTLY. That is the
; whole discipline, and it is the only thing wave 1 owes the later phase.
;
; THE ENTRIES BELOW REACH dos_be_go WITH A NEAR JUMP AND NOT A SHORT ONE, for
; one byte each and no cliff: at 17 doors the FIRST entry's short jump was 126
; of its 127, so the eighteenth would not assemble - and what it says is
; "short jump is out of range" on a line nobody touched.
; -----------------------------------------------------------------------------
DBE_GOTO    equ 0                   ; DX = dir cluster, BL = volume
DBE_READ    equ 2                   ; SI = name, ES:BX = buffer, DX:CX = cap
DBE_FIND    equ 4                   ; CX = ordinal, ES:DI = OSAPI_FIND_SZ buf
DBE_RDAT    equ 6                   ; SI = name, ES:BX = buf, CX = cap,
                                    ;   DX:AX = offset; out DX:AX = delivered
DBE_WRITE   equ 8                   ; SI = name, ES:BX = bytes, DX:CX = count
DBE_APPEND  equ 10                  ; SI = name, ES:BX = bytes, CX = count
DBE_DELETE  equ 12                  ; SI = name
DBE_DFREE   equ 14                  ; out BX = SECTORS per cluster
DBE_MKDIR   equ 16                  ; SI = a name in the current directory
DBE_RMDIR   equ 18                  ; SI = a name, AL = 0 strict
DBE_XCAPS   equ 20                  ; out AX = extended-memory KB
DBE_XALLOC  equ 22                  ; DX:AX = bytes; out DX:AX = a linear base
DBE_XFREE   equ 24                  ; DX:AX = a base
DBE_XCOPY   equ 26                  ; ES:SI, DX:AX, CX, DI (SPEC.md 96.15)
DBE_RENAME  equ 28                  ; SI = the old name, DI = the new, both in
                                    ; the CURRENT directory (SPEC.md 96.31)
DBE_COPY    equ 30                  ; SI = the name, BL/DX = the source place,
                                    ; BH/CX = the destination's (SPEC.md 22.24)
DBE_MOVE    equ 32                  ; the same registers, the same engine's
                                    ; MOVE verb: re-linked on one volume,
                                    ; copied and deleted otherwise. FERR_FULL
                                    ; means the engine could not claim ITS
                                    ; buffer (inside a bracket the heap is the
                                    ; program's, 96.30.6) and nothing was
                                    ; written - the caller may stream with its
                                    ; own
DBE_PATH    equ 34                  ; ES:DI = a buffer, CX = its size; out CX =
                                    ; the length (SPEC.md 19.2.4)
DBE_VSTAT   equ 36                  ; out AX/BX/CX/DX = AH=36h's four (18.4.6)
DBE_WRAT    equ 38                  ; SI = name, ES:BX = bytes, CX = count,
                                    ; DX:AX = the offset (SPEC.md 18.4.7)
DBE_HERE    equ 40                  ; out DX = where this instance stands,
                                    ; BL = its drive (SPEC.md 19.2.4)
DBE_VKIND   equ 42                  ; AL = a volume index; out CF=1 no such
                                    ; volume, else AL/AH = VK_*/VT_*
DBE_RSEQ    equ 44                  ; DBE_RDAT's registers + DI = a 16-byte
                                    ; READ_SEQ cursor in DS (SPEC.md 18.4.8.1)
DBE_WSEQ    equ 46                  ; SI = name, ES:BX = bytes, CX = count
                                    ; (0 = commit the held stream), AL =
                                    ; WSEQF_*, DI = the token in and out
                                    ; (SPEC.md 18.4.9)
DBE_FSOP    equ 48                  ; DX:CX = one operation's bytes, 0 = its
                                    ; end (OSAPI_FS_OP, SPEC.md 12.8.10). A
                                    ; host with no file widget refuses it -
                                    ; kern_dos does - and it is advisory, so
                                    ; the caller reads no answer
DBE_NENT    equ 25

; --- THE HOST'S HOOKS (SPEC.md 96.44.3) -------------------------------------
; Where the CORE would otherwise have to know which host it is in. Each is a
; word in `dos_hkv`, zero when the host does not want it, and the core calls
; it where a `%ifndef KD_BACKEND` used to stand - which is what lets the core
; be assembled ONCE. `dos_bevec`'s shape (96.44.1) with a different table, and
; the reason it is a second table is that a door must always be answered and a
; hook may always be absent.
DHK_SNAP    equ 0                   ; the last screen, before the restore (96.34)
DHK_POLL    equ 2                   ; the packet driver's third poll (96.23.4)
DHK_TTY     equ 4                   ; AH=02h/09h: CF=0 = the host took it (96.33)
DHK_MOUSE   equ 6                   ; INT 33h's state; absent = a still pointer
DHK_CLAIM   equ 8                   ; AX = KB -> DX = segment, CF=1 refused; the
                                    ; host's HEAP, which only the windowed one
                                    ; has (96.44.6). ABSENT is not a refusal to
                                    ; be retried - it is "there is no heap in
                                    ; this host", and `dsh_bufget` reads it as
                                    ; a reason to take the arena arm it always
                                    ; takes here anyway
DHK_FREE    equ 10                  ; DX = a segment DHK_CLAIM answered with
DHK_TXT     equ 12                  ; the TEXT SCREEN the mouse cursor may draw
                                    ; on: out ES = the segment, BX = columns,
                                    ; DX = rows, CF=1 = there is none. **THE
                                    ; WINDOWED HOST MUST NOT HAVE THIS ONE**
                                    ; (SPEC.md 96.10.5.1): there the OS owns
                                    ; every pixel and B800 is the kernel's, so
                                    ; the cursor is a `kern_dos` capability and
                                    ; the absent hook is how that is spelled
DHK_TSET    equ 14                  ; AH=2Dh's step, DX:AX signed ticks, before
                                    ; 0040:006C is written: kern_dos moves the
                                    ; handover count by it (KERN-MEGA-PLAN
                                    ; 14.114). Absent = the host does not read
                                    ; the count as time (the windowed DOS)
%ifdef DOS_MEGA
DHK_XUNP    equ 16                  ; **A CHILD, UNPACKED BY THE HMA HOST**
                                    ; (SPEC.md 96.40.9.1): a launcher's EXEPACK
                                    ; child runs its own unpacker below 64KB
                                    ; with A20 open, which fails - so the HMA
                                    ; kern_dos unpacks it in place first, the
                                    ; same `kd_xunp` as the top-level program.
                                    ; Best effort: absent, or refused, the
                                    ; child's own unpacker runs (SPEC.md
                                    ; 96.40.9.1). Only kern_mega's HMA host
                                    ; fills it; it is `%ifdef DOS_MEGA` so
                                    ; kern_big's core is byte-identical
DHK_NENT    equ 9
%else
DHK_NENT    equ 8
%endif
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_be_goto:
    ; **THE ONE PLACE THAT RECORDS WHERE THE MACHINE IS** (SPEC.md 96.48).
    ; Every physical move goes through this door, so nothing else can forget
    ; and there is a single cell that could lie. The record is written AFTER
    ; the move and only on success: a refused goto leaves the machine where
    ; it was, and `0xFF` while the call is in flight means a failure that
    ; jumps out of `dos_be_go` cannot leave a confident wrong answer behind.
    push ax
    mov byte [dos_pvol], 0xFF
    mov word [dos_betgt], DBE_GOTO
    call dos_be_go
    jc .out
    mov [dos_pvol], bl              ; neither store touches the flags, and
    mov [dos_pdir], dx              ; `pop` does not either - so CF is the
.out:                               ; back end's own answer at the `ret`
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_read:
    mov word [dos_betgt], DBE_READ
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_find:
    mov word [dos_betgt], DBE_FIND
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_rdat:
    mov word [dos_betgt], DBE_RDAT
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_write:
    mov word [dos_betgt], DBE_WRITE
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_append:
    mov word [dos_betgt], DBE_APPEND
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_delete:
    mov word [dos_betgt], DBE_DELETE
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_dfree:
    mov word [dos_betgt], DBE_DFREE
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_mkdir:
    mov word [dos_betgt], DBE_MKDIR
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_rmdir:
    mov word [dos_betgt], DBE_RMDIR
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_xcaps:
    mov word [dos_betgt], DBE_XCAPS
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_xalloc:
    mov word [dos_betgt], DBE_XALLOC
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_xfree:
    mov word [dos_betgt], DBE_XFREE
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_rename:
    mov word [dos_betgt], DBE_RENAME
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
; (dos_be_copy WAS HERE: nothing in the core ever called it, so the thunk is
; gone. DBE_COPY's slot and both hosts' dos_k_copy stay, because the slot
; numbers are the back end's ABI and a door is cheaper than a renumbering)
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_move:
    mov word [dos_betgt], DBE_MOVE
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_path:
    mov word [dos_betgt], DBE_PATH
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_vstat:
    mov word [dos_betgt], DBE_VSTAT
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_wrat:
    mov word [dos_betgt], DBE_WRAT
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
; --- THE TWO QUERY DOORS (SPEC.md 96.4.2) ----------------------------------
; Neither does disk I/O, so neither NEEDS dos_be_go's stack swap - and both
; are here anyway, because the rule this block states is not about the swap.
; It is that the INT 21h core reaches the file system through ONE list, which
; is what makes docs/plans/KERN-DOS-PLAN.md a port of that list and not a hunt
; through the core for stragglers. Wave 2 found these three call sites by
; walking the call graph from the interrupt entries, and they were the whole
; of what was outside.
dos_be_here:
    mov word [dos_betgt], DBE_HERE
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_vkind:
    mov word [dos_betgt], DBE_VKIND
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_rseq:
    mov word [dos_betgt], DBE_RSEQ
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_wseq:
    mov word [dos_betgt], DBE_WSEQ
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_fsop:
    mov word [dos_betgt], DBE_FSOP
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_xcopy:
    mov word [dos_betgt], DBE_XCOPY
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_be_go - the one door, and it SWAPS THE STACK (SPEC.md 96.4.1)
; in:  [dos_betgt] = the dos_k_* to run; every register is its argument
; out: whatever the slot answers, flags included
;
; A KERNEL FILE CALL MAY NOT RUN ON THE DOS PROGRAM'S STACK. Inside the
; bracket SS is the program's - a segment in the middle of the arena - and
; every os8088 context has SS = LOW_SEG (SPEC.md 2.1); the scheduler tests
; for exactly that and declines to switch when it does not hold (SPEC.md 8.5),
; which is safe for a short call and is not what a multi-sector disk write is.
; The symptom is the whole reason this comment is long: OSAPI_FILE_WRITE was
; ENTERED and never came back, the bracket was torn down, and the window said
; `Exit code 000` - a program that ran and exited cleanly, from the outside.
;
; So the call runs on the UI TASK's own stack, which is where the rest of the
; package's file work already runs - [dos_sv_ss]/[dos_sv_sp], banked by
; dos_fsx_main at the deepest point it reaches, so what is reused below that
; point is stack nothing else is holding.
;
; OUTSIDE the bracket it must NOT swap: dos_run's own load is already on that
; stack and [dos_sv_sp] is not yet a number. [dos_onprog] is the test, set at
; the jump into the program and cleared by dos_terminate with SS:SP.
;
; **AND INSIDE AN INT 21h IT MUST NOT SWAP EITHER, WHICH IS THE SAME TEST.**
; Since SPEC.md 96.7.2 the gate has already brought us here, at a DEEPER
; offset than [dos_sv_sp] - so a swap would be upwards, through the frame the
; gate has just built. The gate reads 0 into [dos_onprog] for its whole
; duration and puts it back on the way out, which needs no second flag
; because this is that byte's only consumer.
; -----------------------------------------------------------------------------
dos_be_go:
    ; --- THE ORDINAL BECOMES AN ADDRESS HERE (SPEC.md 96.44.1) -------------
    ; **THE CORE MAY NOT NAME A `dos_k_*`.** Each host has its own back end -
    ; DOS.O88's goes to `OSAPI_*` cells and `kern_dos`'s straight to the disk
    ; layer - so a door that stored the address of one could only ever be
    ; assembled INTO that host. It stores `DBE_*` instead and the host fills
    ; `dos_bevec` before the first call, which is the whole of what makes the
    ; core one object joined to either (docs/plans/KERN-DOS-PLAN.md 4.1.3).
    ;
    ; NINE BYTES, ONCE, rather than two per door: BX is an INPUT to five of
    ; the twenty-two (DBE_READ's buffer among them) and AX to DBE_RDAT, so
    ; the lookup cannot live in the stubs without a push and a pop in each.
    push bx
    mov bx, [dos_betgt]
    mov bx, [dos_bevec + bx]
    mov [dos_betgt], bx
    pop bx
    cmp byte [dos_onprog], 0
    je .direct
    cli                             ; SS and SP move as a pair, as everywhere
    mov [dos_bk_ss], ss             ; else in this file: an interrupt between
    mov [dos_bk_sp], sp             ; them lands on a stack that is half of
    mov ss, [dos_sv_ss]             ; each
    mov sp, [dos_sv_sp]
    sti
    call word [dos_betgt]
    pushf                           ; the answer's FLAGS are on the UI stack
    pop word [dos_beflg]            ; and the `ret` below is on the program's,
    cli                             ; so they are banked ACROSS the swap and
    mov ss, [dos_bk_ss]             ; not carried on either
    mov sp, [dos_bk_sp]
    sti
    push word [dos_beflg]
    popf
    ret
.direct:
    jmp word [dos_betgt]
%endif                              ; DOS_EXTCORE

; --- the os8088 implementation ------------------------------------------------
%ifndef KD_BACKEND                  ; kerndos/kdback.inc is the OTHER one
                                    ; (docs/plans/KERN-DOS-PLAN.md §3)
%include "dosbe.inc"               ; the doors and dos_be_bind (SPEC.md 124.21.1)


; -----------------------------------------------------------------------------
; dos_hk_bind - the six hooks DOS.O88 wants (SPEC.md 96.44.3)
; in:  nothing; out: nothing, every register preserved
;
; Beside `dos_be_bind` and called with it. `kern_dos` binds its own at its
; entry (kerndos/kdentry.inc: the mouse's two, the HMA image's DHK_XUNP and,
; on kern_mega, DHK_SNAP's kd_snap - SPEC.md 96.34.6); every cell nobody
; binds stays the zero a bss arrives as, which is what "this host does not
; want it" is spelled as.
; -----------------------------------------------------------------------------
dos_hk_bind:
    push ax
%ifdef DOS_WTAIL
    mov word [dos_hkv + DHK_POLL], dos_hk_poll
    WIN_ONLY                        ; the program's last screen and its INT
    mov word [dos_hkv + DHK_SNAP], dos_snap ; 21h output are the CONSOLE's,
    mov word [dos_hkv + DHK_TTY],  dos_hk_tty ; and a headless host has none:
    WIN_END                         ; 0 = the ROM's teletype (DOS-HEADLESS 2)
%else
    mov word [dos_hkv + DHK_SNAP], dos_snap
    mov word [dos_hkv + DHK_POLL], dos_hk_poll
    mov word [dos_hkv + DHK_TTY],  dos_hk_tty
%endif
    mov word [dos_hkv + DHK_MOUSE], dos_hk_mouse
    mov word [dos_hkv + DHK_TXT],  dos_hk_txt
    mov word [dos_hkv + DHK_CLAIM], dos_hk_claim
    mov word [dos_hkv + DHK_FREE],  dos_hk_free
%ifdef DOS_MEGA
    mov [dos_dsel], ds              ; ...and the DS dos_hk_tty loads (14.54)
%endif
    pop ax
    ret

; --- dos_hk_claim / dos_hk_free - DHK_CLAIM/DHK_FREE: the host's heap --------
; in:  AX = KB; out: DX = the base segment, CF=1 refused / in: DX = it back
;
; The kernel's heap, which is the windowed host's and no other's: under
; `kern_dos` the program owns everything above the floor and there is nothing
; to claim from, which is why these two are a HOOK and not a door (SPEC.md
; 96.44.6). They were the last four `OSAPI_*` far calls in the core.
dos_hk_claim:
    call OSAPI_MEM_CLAIM            ; AX = KB -> DX = the base segment
    ret
dos_hk_free:
    call OSAPI_MEM_FREE             ; DX = the segment
    ret

; --- dos_hk_poll - DHK_POLL: drain the packet driver if there is one ---------
dos_hk_poll:
%ifdef DOS_NETINLINE
    cmp byte [dos_pkt_raw], 0
    je .no
    call dos_pkt_poll
.no:
%elifdef DOS_NETPART
    cmp word [dos_pkt_bseg], 0      ; the claim IS the code (SPEC.md 96.23.11):
    je .no                          ; no claim, nothing to drain, and the
    push ax                         ; part's own poll asks [dos_pkt_raw]
    mov al, PN_D_POLL
    call far [dos_pkt_door]
    pop ax
.no:
%endif
    ret

; --- dos_hk_tty - DHK_TTY: the console, when the window is up (SPEC.md 96.33)-
; out: CF=0 = taken, CF=1 = the caller's ROM teletype should have it
;
; **`cs:` AND IT IS NOT DECORATION**: AH=09h, AH=40h and AH=02h all reach here
; with DS holding the PROGRAM's segment, off the gate's frame - so a
; DS-relative read of our own byte lands in the program's image at the same
; offset. MEASURED: a probe's `COUNT ` and `TERM ` labels vanished and their
; VALUES survived, because AH=09h was reading a program byte that happened to
; be 0 and AH=02h was reading ours.
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dos_hk_tty:
    cmp byte [cs:dos_inbr], 0
    jne .rom                        ; inside the bracket the ROM's teletype IS
    push ds                         ; the machine's own screen
%ifdef DOS_MEGA
    mov ds, [cs:dos_dsel]           ; ...and the console arm needs OUR DS for
%else                               ; real, con_scr and every byte of the
    push cs                         ; library being DS-relative. **kern_mega
    pop ds                          ; LOADS IT, it does not copy CS**: there
%endif                              ; CS is a read-only CODE selector, and
                                    ; `push cs / pop ds` made con_write's
                                    ; first store a #GP that mega_gpw
                                    ; completed - ONE PER BYTE PRINTED, every
                                    ; DIR row and TYPE of the prompt's
                                    ; (KERN-MEGA-PLAN 14.54)
    call con_write
%ifdef DVW
    mov byte [dvw_dirty], 1         ; the worker's tick posts the paint (DS
                                    ; is ours from the push above)
%endif
                                    ; IT DRAWS NOTHING HERE: a verb that
                                    ; printed a directory would take the gfx
                                    ; lock per character. dos_con_run spends
                                    ; the marks once, after the verb returned
    cmp byte [dos_fsxup], 0         ; **FULL SCREEN STREAMS A LINE AT A TIME**
    je .nofs                        ; (SPEC.md 96.33.11): there the same paint
    cmp al, 10                      ; is a rep movsw of eighty words, so the
    jne .nofs                       ; argument that forbids it in a window does
    call dos_fsx_owed               ; not reach it. LF and only LF
.nofs:
    pop ds
    clc
    ret
.rom:
    stc
    ret
%ifdef DOS_MEGA
; dos_dsel - OUR DATA SELECTOR, for dos_hk_tty's `mov ds, [cs:dos_dsel]`:
; written through DS by dos_hk_bind (which runs with it) and again by
; dos_reloc when a conventional region moves; read through CS, which is
; readable. A pool region keeps its selectors when it moves (mx_compactf),
; so only the conventional fallback ever rewrites it (KERN-MEGA-PLAN 14.54)
dos_dsel    dw 0
dos_reloc:                          ; BX = the base it WAS at, DX = where it
    push ds                         ; is now - on kern_mega a conventional
    mov ds, dx                      ; segment is its own DATA selector
    mov [dos_dsel], dx
    pop ds
    ret
%endif

; --- dos_hk_txt - DHK_TXT: the text screen the mouse cursor may draw on ------
; out: ES = the segment, BX = columns, DX = rows, CF=1 = there is none
;
; **THIS HOST HAS ONE TOO, and 96.10.5.1 first said it did not** - on the
; reasoning that in the window the OS owns every pixel and `B800` is the
; kernel's. That is true OUTSIDE the fullscreen bracket and a DOS program is
; NEVER outside it: `dos_fsx_main` calls `OSAPI_FSX_MODE` with `FSXM_TEXT80`
; before the program is entered, and the kernel hands back the surface in
; `dos_fsi` - which is the very screen `con_tx_ice` has been writing the DOS
; console into since 96.34.4.
;
; **AND THE BDA WOULD HAVE BEEN THE WRONG SOURCE HERE**, which is why this is
; a hook and not one shared body: `OSAPI_FSX_CAPS` answers the DISPLAY's own
; kind for a window that is not on the primary (53.7.1), so the BDA describes
; the machine where FSI describes the surface this bracket was given.
; `kern_dos` has no bracket and reads the BDA; this host has a bracket and
; reads its record. Two hosts, two bodies, one core (96.44.3).
WIN_CODE_END
dos_hk_txt:
    mov ax, [dos_fsi + FSI_SEG]
    or ax, ax
    jz .no                          ; no bracket up: the record is still the
                                    ; zero a bss arrives as
    cmp byte [dos_fsi + FSI_BPP], 0 ; TEXT is bpp 0 (apps/os88api.inc). A
    jne .no                         ; GRAPHICS bracket is function 09h's
                                    ; bitmap cursor, which is a different
                                    ; feature and not this one
    mov bx, [dos_fsi + FSI_W]       ; COLUMNS in a text mode
    or bh, bh                       ; ...and it must fit a byte, because the
    jnz .no                         ; core's `row * columns` is a `mul bl`
    or bl, bl
    jz .no
    mov dx, [dos_fsi + FSI_H]       ; ROWS
    or dx, dx
    jz .no
    mov es, ax
    clc
    ret
.no:
    stc
    ret

; --- dos_hk_mouse - DHK_MOUSE: BX/CX/DX from the kernel's own pointer --------
dos_hk_mouse:
    push ax
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = the buttons - and
    mov bl, al                      ; mouse_btn's bits ARE INT 33h's, bit 0
    xor bh, bh                      ; left and bit 1 right (SPEC.md 9), so the
    push bx                         ; mask needs no translation at all
    push dx                         ; the y the x scale below is about to eat
    mov ax, cx
    mov cx, [dos_vw]
    jcxz .nox                       ; a zero divisor cannot happen and must not
    mov bx, 640                     ; take the axis with it: leave x as it is
    mul bx                          ; DX:AX = x * 640, and x < vw always, so
    div cx                          ; the quotient is < 640 and cannot overflow
.nox:
    mov cx, ax
    pop ax                          ; y
    push cx                         ; ...and the scaled x, which is AX's next
    mov cx, [dos_vh]
    jcxz .noy
    mov bx, 200
    mul bx
    div cx
.noy:
    mov dx, ax
    pop cx
    pop bx
    pop ax
    ret

    DOS_BETAB                       ; dos_betab: (apps/dos/dosbe.inc)

%include "dosw.inc"                 ; the live window (SPEC.md 96.54)
%include "doswr.inc"                ; ...and, parted, its resident half (96.54.9)
; dos_hpop_N - the WINDOWED HOST's shared epilogue (SPEC.md 96.54.8): nine of
; its routines ended in the same seven pops and a `ret`. The core has its own
; two (dos_popa/dos_popb) - it is assembled apart and cannot name this one.
    OS88_POPLADDER dos_hpop, ax, bx, cx, dx, si, di, es
%include "doswm.inc"                ; ...and its mouse (SPEC.md 96.54.4)
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; =============================================================================
; THE WINDOW
; =============================================================================
; -----------------------------------------------------------------------------
; dos_paint - W_PAINT
; in:  SI = window ptr; the gfx lock is held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dos_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

%ifdef DVW
    call dos_wlabel                 ; the menu's mode mark (SPEC.md 96.54.10)
%endif
    mov bx, si                      ; THE BAND FIRST, on every page: it is a
    call dos_con_geom               ; property of the WINDOW and not of the
                                    ; page, and a painter that asked for it
                                    ; per-page would answer differently on two
                                    ; of them after a resize (SPEC.md 96.32)
    cmp byte [dos_page], DOS_PAGE_MAIN
    je .mainpage
    mov bx, si                      ; --- a SETUP page
%ifdef DOS_SETPART
    mov al, SPD_PAINT               ; (the PART's, SPEC.md 96.32.2.3)
    call dos_spdo
    jnc .card                       ; ...and the About card over it, which is
                                    ; the window's and not the page's
    mov byte [dos_page], DOS_PAGE_MAIN  ; **A PAGE WHOSE CODE CANNOT BE HAD
%else                               ; IS THE MAIN PAGE** - the toast said why
    SPB_PAINT
    jmp .card
%endif
.mainpage:

    cmp byte [dos_state], DST_READY  ; THE RE-KICK (SPEC.md 74.1): the kernel
%ifdef DVW
    je .kick                         ; (...and a run in the window: its wake
    cmp byte [dvw_run], 0            ; draws what this expose's cut paint
    je .nokick                       ; could not, SPEC.md 96.54.10)
.kick:
%else
    jne .nokick                      ; keeps at most one queued wake per window,
%endif
    mov bx, si                       ; so this is free when one is already
    call OSAPI_WM_WAKE               ; waiting and is the difference between a
.nokick:                             ; full ring costing a frame and costing
                                     ; the whole launch
    ; --- THE TOP BAR, ALWAYS (SPEC.md 96.32.1) -------------------------------
    mov bx, si
    call dos_bar_rects
    push bx
    mov si, dos_pln
    call os88line_draw              ; the path box: empty IS a state, so it is
                                    ; drawn at DST_IDLE like every other one
    mov bx, dos_btrec               ; **AND IT SAYS `Setup`** (SPEC.md
    mov al, DOS_BT_ENV              ; 96.32.2.1): it opened a two-page area
    call os88ui_btn                 ; - the record carries the rect, the label
    mov al, DOS_BT_RUN              ; and the pressed look, so a repaint
    call os88ui_btn                 ; mid-press agrees with the glass
    pop bx

%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    ; --- ...AND THE CONSOLE, which is what the band is (SPEC.md 96.33) -------
    ; **A FULL PAINT OWES EVERY ROW**: there is no next pass that comes back
    ; for one, so it marks the lot and then spends the scroll debt rather than
    ; leaving it - con_scrollpaint would blit a screen that has just been drawn
    ; from the buffer and mark only the rows it vacated (telnet's te_screen
    ; carries the same pair and the same reason).
    call con_takescroll             ; ...FIRST (SPEC.md 96.54.14): con_takerow
    call con_markall                ; refuses a row while a debt stands, so
    call con_rows_owed              ; the debt is dropped and then every row
%endif                              ; drawn, which is the same screen
.card:
    ; --- AND THE ABOUT CARD LAST, OVER WHICHEVER PAGE IT WAS (SPEC.md 96.51) -
    ; Both branches arrive here - the setup page by `jmp .card` above and the
    ; main page by falling through - because the card is a property of the
    ; WINDOW and not of the page, exactly as the band above is.
    ;
    ; **BX COMES FROM [dos_win] AND NOT FROM SI**: this proc is entered with
    ; SI = the window, but the main page's `os88line_draw` loads SI with
    ; dos_pln on the way past, so by here it is a string. [dos_win] is the one
    ; window this package has and dos_entry wrote it.
    cmp byte [dos_abon], 0
    je .out
    mov bx, [dos_win]
    mov si, dos_ablines
    call os88ui_about_d             ; the _d entry: the kernel's region is
.out:                               ; armed, and re-arming would throw this
    pop di                          ; paint's damage rect away
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_fmt_exit - stamp the exit code into dos_l2_ran
; in:  nothing; out: SI = the line
;
; THE EXIT REPORT IS THE TAIL'S (DOS-HEADLESS-PLAN 1.2's X bucket): this,
; dos_err_line and the DER_* sentences have one caller, the console's
; dos_con_ended, and a headless host says its end in a toast of its own
; (dos_hend) - so 544 bytes a headless region never ran are not in it
; -----------------------------------------------------------------------------
dos_fmt_exit:
    push ax
    push di
    mov al, [dos_exit]
    mov di, dos_exitd
    xor ah, ah
    mov cl, 100
    div cl                          ; AL = hundreds, AH = the rest
    add al, '0'
    mov [di], al
    mov al, ah
    xor ah, ah
    mov cl, 10
    div cl
    add al, '0'
    mov [di+1], al
    add ah, '0'
    mov [di+2], ah
    mov si, dos_l2_ran
    pop di
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_err_line - SI = the sentence for [dos_err]
; THE WINDOW'S, with dos_fmt_fn and dos_hexd below it (SPEC.md 96.44.15): its
; one caller is dosc.inc's dos_con_ended, so it is no longer a core entry.
; -----------------------------------------------------------------------------
dos_err_line:
    push ax
    push bx
    mov al, [dos_err]
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov si, [dos_errs + bx]         ; AX = the code TWICE from here, AH = 0
    cmp al, DER_FSX * 2             ; the unsupported-function case borrows the
    jne .nofn                       ; refusal line and stamps the number, so
    call dos_fmt_fn                 ; "it asked for AH=3Dh" is on the glass
.nofn:                              ; (AX preserved)
%ifdef DVW
    cmp al, DER_FIT * 2             ; **A WINDOW'S 128K IS NOT THE MACHINE**
    jne .out                        ; (SPEC.md 96.54.2): the default cap was
    cmp word [dos_memkb], 0         ; in force when there is no limit of the
    jne .out                        ; user's and the cap is still DVW_DEFKB -
    cmp word [dvw_cap], DVW_DEFKB   ; unraised (a .COM, a compressed file,
    jne .out                        ; kern_small), the machine not being short
    mov si, dos_e_wfit              ; (96.54.2.1). "Shut down the OS" is the
                                    ; bracket's way out, not this one's
%endif
.out:
    pop bx
    pop ax
    ret

dos_fmt_fn:
    push ax
    push di
    mov al, [dos_badfn]
    mov di, dos_fnd
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    call dos_hexd
    mov [di], al
    mov al, ah
    and al, 0x0F
    call dos_hexd
    mov [di+1], al
    mov si, dos_e_fn
    pop di
    pop ax
    ret

dos_hexd:
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 7
.out:
    ret

; -----------------------------------------------------------------------------
; dos_about / dos_abdismiss - the standard About card (SPEC.md 12.2, 20.5.1.1,
; 96.51)
;
; **THIS WAS A BARE `ret`**, and that is worth a line rather than a silent fix:
; the registration below `OSAPI_ABOUT_SET` has always been there, so the kernel
; has always drawn `About DOS...` into the bar - and picking it did nothing at
; all. A handler that returns is indistinguishable from a handler that drew
; something small, which is why nobody reported it; what was behind the item
; was the CREDIT, which is the whole argument of SPEC.md 20.5.1.1.
;
; The HANDLER entry (os88ui_about, not the _d one): ui_dispatch takes the gfx
; lock and far-calls us with NO clip region armed (SPEC.md 11.3), so the widget
; arms one itself. dos_paint uses the other entry.
; -----------------------------------------------------------------------------
dos_about:
    push bx
    push si
    mov byte [dos_abon], 1
    mov bx, si                      ; SI = our window on entry
    mov si, dos_ablines
    call os88ui_about
    pop si
    pop bx
    ret

; Any key or click takes it down. CF = 1 means the event was the card's and the
; caller must not also act on it - a keystroke that dismisses must not reach
; the path box, and a click that dismisses must not also hit a button.
dos_abdismiss:
    cmp byte [dos_abon], 0
    je .none
    mov byte [dos_abon], 0
    mov si, [dos_win]               ; NOT the SI we were called with: the
    call dos_paint                  ; console is what the card covered and
    stc                             ; dos_paint is the only thing that knows
    ret                             ; how to put a page back
.none:
    clc
    ret

; --- the card's lines (SPEC.md 20.5.1.1, 96.51) ------------------------------
; SIX lines. The content is DOS_CONW = 640 px = 80 cells on VGA and CGA and 720
; on Hercules, against a widest line of 34, so no adapter clamps this card and
; nothing is split across two lines.
dos_ablines:
    dw dos_ab1, dos_ab2, dos_ab3, dos_ab4, dos_ab5, dos_ab6, 0
dos_ab1:     db 'DOS for os8088', 0
dos_ab2:     db 0
dos_ab3:     db 'Runs .COM and .EXE programs', 0
dos_ab4:     db 'natively - this machine IS an 8086', 0
dos_ab5:     db 0
dos_ab6:     db 'Contributed by Elendilon', 0

; -----------------------------------------------------------------------------
; dos_swap - the other page, onto the glass
; in:  BX = the window; the gfx lock is held
;
; **THIS IS THE ONE PLACE A GROUND FILL IS RIGHT** (SPEC.md 96.20.1), and it
; is worth saying which case it is rather than leaving the next reader to
; wonder whether 13.14.6 was forgotten. That rule forbids erasing what you are
; about to draw again - a keystroke, a caret, a status line. Here the ENTIRE
; content is replaced by something else, so nothing is drawn twice: every
; pixel is either the new page's or the ground it needed anyway, and there is
; no window in which the old content is gone and the new is not yet there,
; because both happen under one lock before the caller returns.
;
; The alternative - painting the new page over the old and hoping it covers -
; is what leaves the tail of a longer line behind.
; -----------------------------------------------------------------------------
dos_swap:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx
    call OSAPI_WM_CONTENT           ; AX = left, DX = top
    mov si, ax
    mov di, dx
    pop bx
    push bx
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    jc .out2
    mov ax, si                      ; ...the whole of it, once
    mov bx, di
    add cx, si
    dec cx
    add dx, di
    dec dx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR            ; the SLOTS and not os88ui.inc's UI_*
    pop ax                          ; macros: that file is included at the END
    call OSAPI_GFX_FILL             ; of this one (its own rule - the header
    mov al, CBLACK                  ; and the icon are at fixed offsets), so
    call OSAPI_SET_COLOR            ; its macros are not defined up here
.out2:
    pop bx
    mov si, bx                      ; dos_paint takes the window in SI, which
    call dos_paint                  ; is how the kernel calls it
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_con_geom - where the console band is, and how big (SPEC.md 96.32)
; in:  BX = the window; the gfx lock held or not, like every wm_ reader
; out: [dos_conx]/[dos_cony] = its top-left in SCREEN pixels,
;      [dos_concols]/[dos_conrows] = its size in CELLS; CF=1 = the window is
;      not visible and none of the four was written; every register preserved
;
; **THIS IS THE SEAM THE CONSOLE READS.** Four words rather than four return
; registers, because the answer is wanted by a painter, a writer and a
; scroller and none of them wants to carry it through a call.
;
; ROWS ARE COMPUTED AND ARE NEVER A CONSTANT. DOS_CONROWS is what this window
; ASKS for; CGA cannot give it - 640x200 leaves 160 rows of content even over
; the dock, which is 17 cells once the bar has taken its 20 - and a drag onto
; a display of another kind re-applies the preference (SPEC.md 11.100.1), so
; the question is asked at every paint rather than answered once at entry.
;
; THE X IS 8-ALIGNED AND THAT IS NOT TIDINESS (SPEC.md 6.1). font_run's
; single-store fast path needs the pen on a multiple of 8; a spanning window's
; content origin already is one (11.95.2); so the centring margin is rounded
; DOWN to a multiple of 8 rather than halved exactly. It is 0 on every
; adapter in this tree - Hercules too, since its frame stopped spanning and
; became snug round the band (SPEC.md 96.32.5) - and the margin is there for a
; window a user or a display has made wider than the band.
; -----------------------------------------------------------------------------
dos_con_geom:
    push ax
    push cx
    push dx
    call OSAPI_WM_GEOM              ; CX = content width, DX = its height
    jc .out                         ; not visible: nothing to answer about
    push cx                         ; **THE BOX IS BANKED ACROSS THE SHIFTS**,
    push dx                         ; and it has to be: CL is the only shift
                                    ; count an 8086 has (`shr ax, imm` other
                                    ; than 1 is not in the instruction set),
                                    ; and CX is the content WIDTH - so `mov
                                    ; cl, 3` over a width of 640 makes it 515,
                                    ; the slack below goes negative, and the
                                    ; band's x comes out 32704. Measured,
                                    ; first run
    mov ax, cx                      ; --- the columns ---
    mov cl, 3
    shr ax, cl                      ; content width in whole CELLS
    cmp ax, DOS_CONCOLS
    jbe .cols                       ; narrower than 80: it gets what there is,
    mov ax, DOS_CONCOLS             ; which no adapter here does and a clamped
.cols:                              ; window might
    mov [dos_concols], ax
    mov cl, 3
    shl ax, cl                      ; ...and back to pixels, to centre it
    pop dx                          ; the height...
    pop cx                          ; ...and the width, both back
    sub cx, ax                      ; CX = the slack: 0 on every adapter
    shr cx, 1                       ; (96.32.5), more on a wider window
    and cx, 0xFFF8                  ; **DOWN to a multiple of 8** (SPEC.md
    push cx                         ; 6.1), and the WHOLE word: a display wide
                                    ; enough to make the margin exceed 255
                                    ; does not exist in this tree, and `and
                                    ; cl` would be wrong on the one that did
    mov cx, dx                      ; --- and the rows ---
    sub cx, DOS_BARH
    jbe .norows                     ; a window clamped shorter than its own bar
    mov ax, cx
    mov cl, 3
    shr ax, cl
    mov [dos_conrows], ax
    jmp short .haverows
.norows:
    mov word [dos_conrows], 0
.haverows:
    pop cx                          ; --- the origin: the content's, plus the
    call OSAPI_WM_CONTENT           ; margin across and the bar down ---
    add ax, cx
    mov [dos_conx], ax
    add dx, DOS_BARH
    mov [dos_cony], dx
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    call dos_con_pub                ; ...and the console is TOLD, every time
%endif
    clc
.out:
    pop dx
    pop cx
    pop ax
    ret

%ifndef KD_BACKEND                  ; 96.43: the console is the window's
; -----------------------------------------------------------------------------
; dos_con_pub - hand the four words to os88con.inc (SPEC.md 96.33.1)
; in:  the four above, filled; out: nothing; every register preserved
;
; A SEPARATE PROC because it is a different STATEMENT: the four words are this
; DOS's answer about its own window and the seven below are the library's
; contract, so the mapping between them is in one place and reads as a mapping.
;
; **AND IT RUNS AT EVERY PAINT, WHICH IS THE POINT** (SPEC.md 11.96.12): a
; window can be resized, dragged across a display seam or land on an adapter of
; another depth, and a MOVED window calls no paint proc at all - so the console
; is told where it is rather than remembering.
; -----------------------------------------------------------------------------
dos_con_pub:
    push ax
    push bx                         ; **OSAPI_VIDEO ANSWERS THE HEIGHT IN BX**,
    push cx                         ; and dos_con_geom's contract is that every
    push dx                         ; register survives it - which its callers
                                    ; rely on, BX being the window they were
                                    ; asking about
    mov ax, [dos_conx]
    mov [con_px], ax                ; the pen: 8-aligned already, which
    mov ax, [dos_cony]              ; OSAPI_GFX_BLIT1 requires (SPEC.md 5.4.2)
    mov [con_oy], ax
    mov word [con_topy], 0          ; the band's origin IS the text's: this DOS
                                    ; keeps its chrome above the band, where
                                    ; Telnet measures from the window
    mov ax, [dos_concols]
    mov [con_vcols], ax
    mov ax, [dos_conrows]
    cmp ax, CON_ROWS
    jbe .rows
    mov ax, CON_ROWS                ; a window taller than the buffer shows the
.rows:                              ; buffer, not more of it
    mov [con_vrows], ax
    mov cx, [con_cy]                ; **THE WINDOW FOLLOWS THE CURSOR** (SPEC.md
    inc cx                          ; 96.33.8). It was CON_ROWS - vrows, the
    sub cx, ax                      ; bottom of the BUFFER, under a reasoning
    jnb .top                        ; that is half right - the interesting row
    xor cx, cx                      ; is the one the prompt is on, and the
.top:                               ; prompt is only on the LAST row once the
                                    ; console has filled. Before that it is near
                                    ; the top and the bottom is blank, which on
                                    ; CGA is the whole visible band: 200 pixels
                                    ; leave 17 rows of 25, so vtop was 8 with
                                    ; every live row above it and the field
                                    ; reported an empty console. No upper clamp
                                    ; is needed - cy is at most CON_ROWS-1 - and
                                    ; the two rules agree exactly once the
                                    ; buffer starts scrolling under a pinned
                                    ; cursor, which is why typing anything first
                                    ; hid this
    cmp cx, [con_vtop]
    je .mono                        ; unmoved, and that is the common case
    jb .back
    push ax                         ; **DOWN IS A SCROLL, NOT A REPAINT.** A
    mov ax, cx                      ; viewport moving down by n looks exactly
    sub ax, [con_vtop]              ; like a buffer scrolling up by n, and this
    add [con_scrl], ax              ; runs immediately before con_scrollpaint -
    pop ax                          ; so the existing blit spends it and the
    jmp short .settop               ; revealed bottom row is already marked by
.back:                              ; con_markcur. UP has no such equivalent and
    call con_markall                ; no teletype makes one: only con_clear's
.settop:                            ; home can, and it markalls anyway
    mov [con_vtop], cx
.mono:
    call OSAPI_VIDEO                ; DH = bits per pixel, 4 or 1 - the PRIMARY's
    cmp dh, 1                       ; (osapi_video's own contract), which on an
    mov dh, 0                       ; extended desktop is the wrong question for
    ja .colour                      ; the far display and is stated rather than
    mov dh, 1                       ; hidden, exactly as te_layout states it
.colour:
    mov [con_mono], dh
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endif
; -----------------------------------------------------------------------------
; dos_bar_rects - the top bar's three controls (SPEC.md 96.32.1)
; in:  BX = the window
; out: dos_pln's LN_X1..LN_Y2, dos_erect and dos_rrect filled; every register
;      preserved
;
; ONE HELPER FOR THREE RECTS, because they share a line and an arithmetic: the
; two buttons and the four gaps are fixed and the BOX is what is left over, so
; computing them apart would price the buttons twice and get the box wrong the
; day somebody lengthens a label.
; -----------------------------------------------------------------------------
dos_bar_rects:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, dos_btrec               ; **THE PAGE'S OWN PAIR, WHERE THE RECTS
    mov word [si+OS88UI_BT_RECTS], dos_erect
    mov word [si+OS88UI_BT_LABELS], dos_bt_barl
    mov word [si+OS88UI_BT_N], 2    ; ARE COMPUTED**, and that is the whole
                                    ; rule: this routine is what the PAINTER
                                    ; calls and what dos_place calls, so a
                                    ; record aimed here is aimed on every path
                                    ; that can draw or hit-test. Aiming it in
                                    ; dos_place instead left BT_N at 0 on the
                                    ; paint path, and os88ui_btn draws nothing
                                    ; for an index past the live count - two
                                    ; buttons that simply were not there
    call OSAPI_WM_GEOM              ; CX = content width
    jc .out
    mov di, cx                      ; DI = it, across the call below
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    add dx, DOS_BARY
    mov bx, dx
    add bx, DOS_BARCH - 1           ; BX = the row's last line: a rect is
                                    ; INCLUSIVE here, which is gfx_frame's
                                    ; convention and os88line.inc's
    mov si, dos_pln                 ; --- the box, from the left pad to
    mov cx, ax                      ;     whatever the buttons leave ---
    add cx, DOS_BARGAP
    mov [si+LN_X1], cx
    mov [si+LN_Y1], dx
    mov [si+LN_Y2], bx
    mov cx, ax
    add cx, di
    sub cx, DOS_BARGAP + DOS_RUNW + DOS_BARGAP + DOS_BTNW + DOS_BARGAP + 1
    mov [si+LN_X2], cx
    mov si, dos_erect               ; --- 'Environment', beside it ---
    add cx, DOS_BARGAP + 1
    mov [si+0], cx
    mov [si+2], dx
    mov [si+6], bx
    add cx, DOS_BTNW - 1
    mov [si+4], cx
    mov si, dos_rrect               ; --- and 'Run', right-anchored ---
    mov cx, ax
    add cx, di
    sub cx, DOS_BARGAP + DOS_RUNW
    mov [si+0], cx
    mov [si+2], dx
    mov [si+6], bx
    add cx, DOS_RUNW - 1
    mov [si+4], cx
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_path_make - [dos_vol]/[dos_dir]/[dos_name] -> the box's text
; out: [dos_path] = `X:\DIR\NAME.EXT`; CF=1 = the path could not be had and
;      the BARE NAME is there instead; every register preserved
;
; **UI TASK, AND IT TOUCHES THE DISK.** OSAPI_FILE_PATH answers about where the
; machine IS STANDING (SPEC.md 19.2.4), so this stands where the file is
; first - which is why it is called ONCE, from the association, and never from
; a painter.
;
; The fallback is dos_envpath's and for its reason: a name with no path in
; front of it is still the thing the user recognises, and an empty box would
; be a LIE, because empty means the internal COMMAND.COM (96.32.1).
; -----------------------------------------------------------------------------
dos_path_make:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; OSAPI_FILE_PATH writes to ES:DI
    mov dx, [dos_dir]
    mov bl, [dos_vol]
    call dos_be_goto
    jc .bare
    mov di, dos_path
    mov al, [dos_vol]
    add al, 'A'                     ; [dos_vol] is 0-based and DOS counts A: as
    mov [di], al                    ; 1 in AH=47h, which is why that handler
    inc di                          ; decrements and this one does not
    mov al, ':'
    mov [di], al
    inc di
    mov cx, DOS_PBUF - 2 - 13       ; what is left once the drive and the
    call OSAPI_FILE_PATH            ; longest 8.3 name plus its separator are
    jc .bare                        ; accounted for - it REFUSES rather than
    add di, cx                      ; truncating, so this is the whole check
    cmp cx, 1
    jbe .name                       ; the root already ends in its separator
    mov al, '\'
    mov [di], al
    inc di
.name:
    mov si, dos_name
.nm:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .nm
    call .sync
    clc
    jmp short .out
.bare:
    mov si, dos_name
    mov di, dos_path
.bn:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .bn
    call .sync
    stc
.out:
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)

; --- .sync - the FIELD, re-measured from the text just written -------------
; **INSIDE THE PROC AND NOT BESIDE ONE CALLER** (SPEC.md 96.33.10.1).
; dos_fld_init empties the box and resyncs it, so LN_LEN is 0 until something
; re-measures - and this routine writes the BUFFER. The console door had a
; resync four lines outside it and the entry proc's association arm did not,
; so a .COM opened by double-click put a perfect path in [dos_path] and drew
; an EMPTY field: measured at LN_LEN = 0 with the buffer holding
; `B:\DOSHELLO.COM`.
;
; BEFORE the clc/stc, because CF is this routine's answer. os88line_resync
; reads a buffer and writes three words and has no opinion about the carry,
; but a call between the flag and the `ret` is a defect waiting for its first
; clobber.
.sync:
    push si
    mov si, dos_pln
    call os88line_resync
    pop si
    ret

; -----------------------------------------------------------------------------
; dos_path_take - the box's text -> [dos_vol]/[dos_dir]/[dos_name]
; out: CF=0 and the three set; CF=1 = it does not resolve, and all three are
;      UNTOUCHED so a bad edit loses nothing; every register preserved
;
; **THE RESOLVER WAS ALREADY IN THE FILE**, which is the argument for doing
; this now rather than inventing a path layer: dos_walk_pbuf stands at an
; absolute path from the volume root and answers its cluster, because AH=3Dh
; has to open \PRINCE\LEVEL.DAT. So a typed path costs the SPLIT and nothing
; else.
;
; Three shapes, and the third is the one that needs saying:
;   X:\DIR\NAME.EXT   the drive, the walk, the name - the ordinary case
;   \DIR\NAME.EXT     the same on the volume we are already on
;   NAME.EXT          no separator at all: the folder we are STANDING in, so
;                     a user who clears the box back to a name does not have
;                     to retype the path. With a drive letter and no
;                     separator it is that volume's ROOT, because a per-drive
;                     current directory is a thing this DOS does not keep
; -----------------------------------------------------------------------------
dos_path_take:
    mov byte [dos_ptres], 0         ; the ordinary door COMMITS
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, dos_path
    jmp short dos_pt_body

; -----------------------------------------------------------------------------
; dos_path_take_si - the same resolver, on TEXT YOU NAME, without committing
; in:  SI -> the text; out: CF=0 with [dos_tname], [dos_pgdir] and [dos_pgvol]
;      set; CF=1 = it does not resolve. Every register preserved.
;
; **THE PACKAGE DOOR MAY NOT WRITE THE PATH BOX** (SPEC.md 96.33.17): the box
; holds what `Run` re-runs AS A DOS PROGRAM, so a `.O88` in it arms a button
; that must then refuse it. So the text is a parameter and the answer lands in
; cells of its own - `.commit` is where the two doors part, one instruction
; before the three DOS-program stores.
; -----------------------------------------------------------------------------
dos_path_take_si:
    mov byte [dos_ptres], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
dos_pt_body:
    mov [dos_ptbase], si            ; **THE BUFFER IS A PARAMETER NOW**, so the
                                    ; drive test below has to ask about the
                                    ; TEXT and not about dos_path
    cmp byte [si], 0
    je .no                          ; empty IS a state (the internal
                                    ; COMMAND.COM) and it is not a program
    mov al, [si+1]
    cmp al, ':'
    jne .novol
    mov al, [si]
    call dos_upc
    sub al, 'A'
    cmp al, 26
    jae .no
    mov [dos_tvol], al
    inc si
    inc si
    jmp short .havevol
.novol:
    mov al, [dos_vol]
    mov [dos_tvol], al
.havevol:
    mov bx, si                      ; BX = where the path part starts
    xor di, di                      ; DI = the LAST separator, 0 = none
.scan:
    mov al, [si]
    or al, al
    jz .split
    cmp al, '\'
    jne .next
    mov di, si
.next:
    inc si
    jmp short .scan
.split:
    or di, di
    jnz .name                       ; a separator: DI is where it was
    cmp bx, [dos_ptbase]            ; **ASKED OF THE TEXT, NOT OF dos_path**:
    je .keepdir                     ; this compare means *was there a drive
                                    ; letter?*, which it answered by knowing
                                    ; where its own buffer starts. Pointed at
                                    ; any other string it FAILS, the
                                    ; drive-with-no-separator arm below runs,
                                    ; and `.name`'s `inc si` eats the name's
                                    ; FIRST CHARACTER - `NOTEPAD` resolving as
                                    ; `OTEPAD` (SPEC.md 96.33.21)
                                    ; ...none, and no drive either: the folder
    mov di, bx                      ; we stand in. A DRIVE with no separator
    dec di                          ; makes the directory part EMPTY, which
                                    ; dos_walk_pbuf reads as that volume's root
                                    ; - and DI is the COLON, so `.name`'s `inc`
                                    ; lands on the name's first character. It
                                    ; was the name itself, and `B:DOSHELLO.COM`
                                    ; ran `B:\OSHELLO.COM` (SPEC.md 96.33.21)
.name:
    mov si, di
    inc si                          ; the name starts one past the separator
    jmp short .takename
.keepdir:
    mov si, bx
.takename:
    push di                         ; **THE SEPARATOR, BANKED ACROSS THE COPY**
    push si                         ; - DI is the only pointer the copy has and
    mov di, dos_tname               ; the walk below needs to know where the
    mov cx, 13                      ; last one was
.cn:
    mov al, [si]
    or al, al
    jz .cnend
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .cn
    pop si
    pop di
    jmp short .no                   ; over 12 characters: not an 8.3 name, and
.cnend:                             ; nothing this DOS can open
    mov byte [di], 0
    pop si
    pop di                          ; the separator again
    cmp byte [dos_tname], 0
    je .no                          ; a path ending in a separator names a
                                    ; FOLDER and this DOS runs programs
    or di, di
    jz .here                        ; no separator: the folder we STAND in
    mov si, bx                      ; --- the directory part, into dos_pbuf ---
    mov cx, di
    mov di, dos_pbuf
.cd:
    cmp si, cx
    jae .cdend                      ; up to but NOT including the separator,
    mov al, [si]                    ; so `\A\B.COM` walks `\A` and `\B.COM`
    mov [di], al                    ; walks the root
    inc si
    inc di
    cmp di, dos_pbuf + DOS_PBUF - 1
    jb .cd
    jmp short .no                   ; longer than the buffer
.cdend:
    mov byte [di], 0
    push dx                         ; **STAND THE MACHINE, do not move DOS**
    mov dl, [dos_tvol]              ; (SPEC.md 96.48.2). This used to set
    call dos_fh_stand               ; `[dos_vol]` for the walk and put it back
    pop dx                          ; if the walk refused, because the walk read
    jc .no                          ; `[dos_vol]` - it reads `[dos_pvol]` now,
    call dos_walk_pbuf              ; so the drive to walk on is said by
    jc .no                          ; standing on it. The rollback goes with it:
    mov [dos_pgdir], dx             ; nothing was changed, so a typo leaves the
    jmp short .commit               ; DOS exactly where it was - and the arm
                                    ; falls into `.commit` rather than past it,
                                    ; because `[dos_vol]` is no longer already
                                    ; set by the time it gets there
.here:
    ; **THE FOLDER WE STAND IN IS [dos_curdir], NOT [dos_dir]** (SPEC.md
    ; 96.33.13).  This arm left [dos_dir] alone, which is right for the door it
    ; was written for - the Run button on a DOS that has never moved - and
    ; wrong the moment the CONSOLE exists: `CD` goes through dos_cd_go, which
    ; writes [dos_curdir] and nothing else, so from the second directory onward
    ; every bare name resolved against the LAUNCH folder.  `CD SBEEPS` then
    ; `sb` looked in the volume root and answered "Bad command or file name"
    ; about a file DIR had just listed.
    ;
    ; The two are genuinely different (96.6.1): [dos_dir] is where the package
    ; was launched from and survives a program's own chdir, and [dos_curdir] is
    ; where DOS is now.
    mov dx, [dos_curdir]
    mov [dos_pgdir], dx
.commit:
    mov al, [dos_tvol]
    mov [dos_pgvol], al             ; both arms answer HERE first...
    cmp byte [dos_ptres], 0
    jne .resonly                    ; ...and resolve-only stops one instruction
    mov dx, [dos_pgdir]             ; short of the DOS program's three stores
    mov [dos_dir], dx
    mov [dos_vol], al
.commit2:
    mov si, dos_tname               ; ...and the NAME last, so every refusal
    mov di, dos_name                ; above leaves all three as they were
.cm:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .cm
.resonly:                           ; the resolve-only answer IS [dos_tname],
    clc                             ; which is where the copy above reads from
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; -----------------------------------------------------------------------------
; dos_keeph - a CGA window may hang over the dock (SPEC.md 11.93)
; in:  BX = the window; out: FLAGS PRESERVED - the CF wm_create left is the
;      loader's answer and still has to ride out of dos_entry
;
; IT ANSWERS BOTH WAYS, which is br_keeph's own hard-won note: a KEEPH left
; set on a VGA raises the height ceiling by the dock's rows on a screen with
; no shortage of them, and this is reachable from a resize where the adapter
; can have gone the other way.
;
; **IT WAS MARKED CORE AND IT IS PURE WINDOW WORK** (SPEC.md 96.44.6). Its
; only caller in 13,000 lines is `dos_entry`'s, which is inside this same
; block, and nothing in the core names it - so its entry in
; `apps/dos/doscents.inc` was a table row nobody crossed. What it cost while
; it sat there was the two `OSAPI_*` far calls below, which were TWO of the
; SIX that made 96.40.2's refusal wall necessary.
; -----------------------------------------------------------------------------
dos_keeph:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push bx                         ; **BX IS THE WINDOW AND THE ANSWER COMES
    call OSAPI_WM_DISPLAY           ; BACK IN IT** - and it is the card this
    pop bx                          ; window is ON, not the primary (SPEC.md
                                    ; 39.16.4), because a drag across a seam is
                                    ; exactly when the answer changes
    xor al, al
    cmp dl, VID_CGA
    jne .set
    inc al
.set:
    call OSAPI_WM_KEEPH
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; dos_sav_done - the dialog's completion (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = OUR window ptr, DI = the chosen name IN
;      KERNEL_SEG (ES points there); UI task, gfx lock HELD, the dialog window
;      already destroyed
;
; **A CANCELLED DIALOG NEVER GETS HERE**, so there is no flag to test - which
; the first version of this did, on a CF the kernel never set.
;
; **THE NAME IS THE KERNEL'S**, so it is copied out before anything else is
; called: the next slot is free to move what DI points at.
;
; IT MUST REPAINT. The kernel does not repaint after a callback returns and
; the window under the dialog has just been uncovered by wm_destroy.
; -----------------------------------------------------------------------------
dos_sav_done:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, si                      ; bank our window: SI is about to be ours
    push di
    mov si, di
    mov di, dos_wname               ; ...the name, out of KERNEL_SEG. **NOT
                                    ; dos_sbuf**: dos_lnk_build below calls
                                    ; dos_lnk_rel, which stages `.\NAME.EXT`
                                    ; there - so the write got `.\DOSARGS.COM`
                                    ; as its FILENAME and answered FERR_NAME,
                                    ; which is a dialog that opens, closes and
                                    ; writes nothing
    mov cx, 13
.cp:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    or al, al
    loopnz .cp
    mov byte [di], 0
    pop di

    push ds                         ; the bytes were built by dos_sav_go,
    pop es                          ; before the dialog moved us
    mov si, dos_wname
    mov bx, dos_lbuf
    mov cx, [dos_lend]
    xor dx, dx
    call dos_be_write
.paint:
    push ds
    pop es
    mov si, [dos_win]
    call dos_paint                  ; the dialog's window was destroyed over
                                    ; ours and nothing else will put it back
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)

; --- THE SETUP PAGE, INLINE (SPEC.md 96.32.2.3) -------------------------------
; Where the page is a PART (DOS_SETPART) this file names none of it: the
; -DDOS_SETEMIT assembly includes dosset.inc at the END, in a section of its
; own, and the Makefile cuts it off. Everywhere else it is here, as it was.
WIN_CODE_END
%ifndef DOS_SETPART
%include "dosset.inc"
%endif
; -----------------------------------------------------------------------------
; dos_mem_whole - may the program have the WHOLE machine? (SPEC.md 96.36.1)
; out: CF = 1 it may not, and SI = the reason to put on the glass
;      CF = 0 it may, and SI = 0. Every other register preserved.
;
; **THE QUESTION IS "AM I THE PARTED PACKAGE", AND IT IS ASKED OF THE IMAGE.**
; The arm hands the machine to `kern_dos`, which ships as part 2 of DOS.O88
; (SPEC.md 96.44.5), so a build with no part table cannot do it whatever the
; machine underneath is - which is SPEC.md 47 rule 5's "grey a FACT" rather
; than a guess about hardware.
;
; THE ANSWER IS NOW YES ON EVERY SHIPPED DISK. It was no for four waves:
; $(SYSROOT) carried the plain compressed package because §96.44.4's parted
; shape was RAW, and the arm was live on a gate disk alone. §96.44.5's four
; pieces put a 2,092-byte loader in front of three compressed parts and the
; Makefile flipped, at 17 clusters of the 360KB system disk and +750 ms a
; launch - the two extra reads being this file's own host image and part 1.
; So the greyed arm is what a build WITHOUT `-DDOSKPART` still shows, and
; `tests/kdhand.py` is what would go red if a disk lost the part again.
;
; It runs on EVERY PAINT (SPEC.md 47 rule 5's corollary), so it must stay
; cheap: two instructions today, and a byte somebody else already computed
; when it is not.
; -----------------------------------------------------------------------------
dos_mem_whole:
%ifdef DOSKPART
    ; **THE PREDICATE IS ONE COMPARE AND IT IS A FACT** (SPEC.md 47 rule 5):
    ; the part table's length word is what os88pkg.py wrote, so a build
    ; carrying kern_dos says a number here and one that does not says zero.
    ; A machine cannot be asked whether it has a feature its own image was
    ; not built with.
    cmp word [dos_kdrow + OP_R_LEN], 0
    je .no
    xor si, si
    clc
    ret
.no:
%endif
    WIN_ONLY                        ; the reason is the Setup page's, and a
    mov si, dos_l_memw3             ; headless host has none (CF is the
    WIN_END                         ; answer dos_mem_fix reads)
    stc
    ret

%endif                              ; KD_BACKEND
%ifdef DOSKPART
; =============================================================================
; ARM 3: HANDING THE MACHINE TO kern_dos (SPEC.md 96.40)
; =============================================================================
; docs/plans/KERN-DOS-PLAN.md §7. DOS.O88 fills a record in its OWN image, the
; kernel records where it lies and returns, and `ui_task`'s step 0 spends the
; post with nothing held: the drivers go, the screen goes, the kernel goes,
; and what is left is `kern_dos` with a DOS program in 560 KB of it.
;
; **THE RECORD IS IN THE IMAGE AND NOT ON A STACK**, which SPEC.md 96.40 says
; in as many words: `osapi_dos_handoff` keeps a FAR POINTER rather than a copy,
; so the 543 bytes stay ours and are read long after this routine returns.
; -----------------------------------------------------------------------------

; -----------------------------------------------------------------------------
; dos_pkgwhere - our own file, and the folder it is in. CALLED FROM dos_entry
;                AND FROM NOWHERE ELSE, because both facts exist only there.
; out: [dos_pkgname], [dos_pkgdir], [dos_pkgvol]; every register preserved
;
; SI arrives at the entry proc holding an offset into the KERNEL's segment at
; the name of the file we came out of (SPEC.md 20.12 rule 1), and the loader
; reuses that buffer on the next launch.
;
; THE FOLDER IS THE SAME KIND OF NOW-OR-NEVER, and it is the half that is easy
; to miss. An instance's file calls resolve in its own launched-from directory
; (SPEC.md 19.2.1), which is where it stands at THIS instruction and nowhere
; later: dos_run's own GOTO stands it where the DOCUMENT is, and on the
; arrangement a user really has - DOS.O88 in APPS/ on the boot disk, the .COM
; on a floppy in B: - that is a different volume entirely.
; -----------------------------------------------------------------------------
dos_pkgwhere:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    ; ES arrives holding KERNEL_SEG (SPEC.md 20.1) and SI points into it; DS
    ; is ours and DI is about to. So the two segment registers SWAP, and
    ; neither of them is a constant this code may assume.
    mov ax, es                  ; AX = KERNEL_SEG
    push ds
    pop bx                      ; BX = ours
    mov ds, ax
    mov es, bx
    mov di, dos_pkgname
    mov cx, 13
    rep movsb
    pop es
    pop ds
    push ds
    push es
    call dos_be_here            ; DX = the folder we stand in, BL = its drive
    jc .out
    mov [dos_pkgdir], dx
    mov [dos_pkgvol], bl
.out:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_handoff - fill the record and post it
; out: CF=0 posted (this run is over); CF=1 refused, and the caller falls back
; -----------------------------------------------------------------------------
dos_handoff:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                      ; every store below is ours

    mov di, dos_kdh
    mov word [di+KDH_MAGIC], KDH_SIG & 0xFFFF
    mov word [di+KDH_MAGIC+2], KDH_SIG >> 16
    mov al, [dos_hlprow + OP_R_KIND]    ; the FORMAT the stub decodes with
    mov [di+KDH_FMT], al            ; (SPEC.md 96.44.18): the loader wrote the
                                    ; table's OP_T_FMT into HELP's kind byte,
                                    ; and one file has one format
    mov al, [dos_pkgvol]
    mov [di+KDH_VOL], al
    mov ax, [dos_pkgdir]
    mov [di+KDH_DIR], ax

    push di                     ; our own file name, as the loader gave it
    add di, KDH_NAME
    mov si, dos_pkgname
    mov cx, 13
    rep movsb
    pop di

%ifdef DOS_MEGA
    mov si, dos_kdrow               ; WHICH kern_dos: the HMA one, or with the
    cmp byte [dos_mhma + OS88UI_CK_ON], 0   ; box unticked the LOW one, part 3
    jne .hma                        ; (SPEC.md 96.40.9.2)
    mov si, dos_kdlrow
.hma:
 %define DOS_KDR si
%else
 %define DOS_KDR dos_kdrow
%endif
    mov ax, [DOS_KDR + OP_R_OFF]    ; the part's first file PARAGRAPH, which is
    mov [di+KDH_POFF], ax           ; what os88pkg.py wrote there
    mov ax, [DOS_KDR + OP_R_ZKB]    ; ...and on an OP_COMP row this word is the
    mov [di+KDH_PLEN], ax           ; PACKED length (SPEC.md 20.12.7)
    mov word [di+KDH_PLEN+2], 0
    mov ax, [DOS_KDR + OP_R_LEN]
    mov [di+KDH_ULEN], ax
    mov word [di+KDH_ULEN+2], 0
    mov ax, [dos_corerow + OP_R_OFF]    ; ...AND THE CORE PART'S THREE (SPEC.md
    mov [di+KDH_COFF], ax               ; 96.44.5.4): kern_dos's image has a
    mov ax, [dos_corerow + OP_R_ZKB]    ; hole at CORE_ORG and the stub fills it
    mov [di+KDH_CPLEN], ax              ; from here, because by then there is no
    mov word [di+KDH_CPLEN+2], 0        ; file layer left to ask
    mov ax, [dos_corerow + OP_R_LEN]
    mov [di+KDH_CULEN], ax
    mov word [di+KDH_CULEN+2], 0
    mov word [di+KDH_CORG], CORE_ORG    ; ...and where it goes in that segment
    mov ax, [dos_win]               ; ...and the way home, which only we know:
    mov [di+KDH_WIN], ax            ; the window to wake and the cell the
    mov word [di+KDH_CODE], KDH_NOCODE  ; restored kernel puts the code in
    mov word [di+KDH_AKB], 0        ; (docs/plans/KERN-DOS-PLAN.md 8) - and the
                                    ; ARENA beside it (SPEC.md 96.41.1), which
                                    ; only the other host can work out. On the
                                    ; arm with no fixed disk nothing ever reads
                                    ; any of them, and they cost the record 6
                                    ; bytes

    call dos_lbfill
    mov si, dos_kdh
    push ds
    pop es                      ; ES:SI is the record, in OUR segment
    mov al, 2                   ; ...and verb 2 of the door the drivers went
    call OSAPI_DRV_SUSPEND      ; through: the WHOLE machine (SPEC.md 51.11,
                                ; 96.40). CF=1 = a post already stands
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_hasfixed - is there a disk this session could come back FROM? (96.42)
; out: CF = 0 there is one, CF = 1 there is not; every register preserved
;
; **IT HAS TO AGREE WITH `hb_pick` AND THAT IS WHY IT EXCLUDES `VT_FILE`**
; (SPEC.md 47 rule 5: what greys, what is asked and what is written cannot
; disagree). A redirected volume answers VK_FIXED and is somebody else's disk
; over a cable - no sectors of its own, so nothing can write an image to it and
; read it back with no operating system, which is exactly what the kernel's own
; predicate says by refusing `DVK_FILE`.
;
; A WALK AND NOT A CACHE: a hard disk arrives when the Control Panel mounts
; `HDD.DRV`, which is an ordinary thing to do mid-session, and this is asked
; ONCE PER LAUNCH rather than once per paint.
; -----------------------------------------------------------------------------
dos_hasfixed:
    push ax
    push bx
    xor bl, bl
.vol:
    mov al, bl
    push bx
    call dos_be_vkind           ; AL = VK_*, AH = VT_*; CF = no such volume
    pop bx
    jc .next
    cmp al, VK_FIXED
    jne .next
    cmp ah, VT_FILE
    je .next                    ; hb_pick1's own test, one segment out
    pop bx
    pop ax
    clc
    ret
.next:
    inc bl
    cmp bl, DVOL_MAX
    jb .vol
    pop bx
    pop ax
    stc
    ret

%ifdef DOS_MEGA
; dos_msav_di - DI = OS88UI_DIS when there is no fixed disk the save box's
; image could go to (dos_hasfixed, hb_pick's own test), else 0. Every other
; register preserved. Asked per paint and per press: a walk of eight rows,
; and a disk mounted mid-session ungreys it at the next paint
dos_msav_di:
    xor di, di
    call dos_hasfixed
    jnc .out
    mov di, OS88UI_DIS
.out:
    ret
%endif

; -----------------------------------------------------------------------------
; dos_wholeask - THE ONE DELIBERATELY DESTRUCTIVE THING THIS DOS DOES (96.42)
; in:  the wake handler's context - UI task, the gfx lock NOT held
; out: CF = 1 an alert is up and the launch is SUSPENDED; CF = 0 carry on
;
; docs/plans/KERN-DOS-PLAN.md 9: on a machine with no fixed disk there is
; nothing to come back to, so every open window and every unsaved document goes
; and the machine restarts when the program exits. It is offered rather than
; greyed - the user wants the memory and that is a real want - and it is
; offered with the safe answer on the ring (OS88UI_ADANGER), at the moment of
; launch, in its own window.
;
; **HERE AND NOT AT THE RUN BUTTON**, which is where it was nearly put: a
; `.LNK` carries the arm (SPEC.md 96.21) and an association launch never passes
; through `dos_go` at all, so a shortcut written on a machine WITH a hard disk
; would have ended the session on a machine without one with nothing asked.
; `dos_wake` is the one door every launch comes through.
;
; The gfx lock is TAKEN rather than assumed: `os88ui_ask` wants it held and a
; wake handler does not have it (SPEC.md 74.1).
; -----------------------------------------------------------------------------
dos_wholeask:
%ifdef DOS_MEGA
    clc                         ; kern_mega PARKS the session above 1MB
    ret                         ; (KERN-MEGA-DOS-PLAN 2.4): it comes back with
%endif                          ; or without a fixed disk, so there is no loss
                                ; to warn about
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    jne .no
    cmp byte [dos_wok], DOS_W_YES
    je .no                      ; asked and confirmed for THIS launch
    cmp byte [dos_wok], DOS_W_NO
    je .hold                    ; **ASKED AND REFUSED, AND THE REFUSAL HAS TO
                                ; STICK.** The launch is a posted WAKE and a
                                ; wake is a kick (SPEC.md 74.1): tearing the
                                ; alert down repaints, and DOS is still
                                ; DST_READY when the next one lands - so a
                                ; two-state flag put the question straight back
                                ; up and Cancel could not be answered at all
    call dos_hasfixed
    jnc .no                     ; there is one: the session comes back (96.41)
                                ; and there is nothing to warn about
    WIN_ONLY                    ; the question is a WINDOW's dialog, and a
    push ax                     ; headless shortcut saved with arm 3 IS the
    push bx                     ; confirmation: the user ticked both boxes
    push si                     ; and saved (DOS-HEADLESS-PLAN 5)
    push di
    call OSAPI_GFX_LOCK
    mov al, OS88UI_ADANGER
    mov bx, [dos_win]
    mov si, dos_s_wholeq
    mov di, dos_wholedone
    call os88ui_ask
    call OSAPI_GFX_UNLOCK
    pop di
    pop si
    pop bx
    pop ax
%ifdef DOS_WTAIL
    jnc .hold                   ; CF = 1: REFUSED - one was already up and has
    WIN_END                     ; been raised. Falling through to the launch
.no:                            ; there would run the program behind its own
    clc                         ; question. (A headless host lands here too:
    ret                         ; its shortcut is the confirmation)
.hold:
    stc
    ret
%else                           ; (the same, in the order every other build
    jc .no                      ; has always assembled it)
    WIN_END
.hold:
    stc
    ret
.no:
    clc
    ret
%endif

; dos_wholedone - the alert's answer. AL = 0 Cancel, 1 Proceed, or
;                 OS88UI_ACANCEL for a dismissal; the lock is NOT held
dos_wholedone:
    mov byte [dos_wok], DOS_W_NO
    cmp al, 1
    jne .out                    ; Cancel, Esc, the close box: [dos_state] was
                                ; never advanced, so DOS is still READY and
                                ; there is nothing to undo - but the REFUSAL is
                                ; recorded, because the next wake would
                                ; otherwise ask again
    mov byte [dos_wok], DOS_W_YES
    mov bx, [dos_win]           ; ...and round again, through the one door
    call OSAPI_WM_WAKE
.out:
    ret

dos_s_wholeq: db 'Open windows are lost. Proceed?', 0

; -----------------------------------------------------------------------------
; dos_lbfill - the launch block, into the record (kerndos/kdlaunch.inc)
; out: every register preserved
;
; The GATHER half of the gather/scatter kern_dos's entry does: one list,
; walked in the other direction, each side summing its own copy so a field
; added to one and not the other is refused at the entry rather than scattered
; into the wrong place.
; -----------------------------------------------------------------------------
dos_lbfill:
    ; **THE BLASTER ROW HAS TO EXIST BEFORE IT CAN BE GATHERED** (SPEC.md
    ; 96.44.13). `[dos_blaster]` is filled by `dos_drv_take`, out of the
    ; record `OSAPI_DRV_SUSPEND` gives back for the sound class - and on this
    ; path `dos_drv_take` runs only in `.unmount`, the arm taken when the
    ; program does NOT fit. The source says so in as many words a few hundred
    ; lines up: *"it fits - and the sound driver is never touched"*. So a
    ; program that fits handed `kern_dos` an EMPTY string, `dos_build_psp`
    ; over there emitted no rows, and the environment block a DOS program was
    ; given began with its own terminator.
    ;
    ; **An empty environment is not a cosmetic loss.** DOS 3 puts the
    ; program's own path after the block's terminating NUL and a count word
    ; (96.19.3), and a program walks the rows to REACH it. Prince of Persia's
    ; walk is `cmp byte [es:0],0 / jz skip` - measured, at `B791` in its own
    ; image - so an empty block means it never finds its path, and it says
    ; *"Unable to find necessary files. Please start program from the default
    ; drive and directory."* about files it could have opened.
    ;
    ; `dos_drv_take` is idempotent - `[dos_drvout]` is its own guard - and the
    ; drivers cannot survive this arm anyway: `kern_dos` replaces the kernel
    ; they are loaded into. So the call belongs here, where the row is about
    ; to be read, and not only on the arm that happens to need the memory.
    call dos_drv_take
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es

    mov di, dos_kdh + KDH_LB
    mov word [di+KDL_MAGIC], KDL_SIG & 0xFFFF
    mov word [di+KDL_MAGIC+2], KDL_SIG >> 16
    mov word [di+KDL_VER], KDL_VER_NOW
    mov ax, KDLF_REBOOT                     ; W6 is the other arm: with no
                                            ; hibernation image to come back
                                            ; to, the machine restarts (§9)
%ifdef DOS_MEGA
    cmp byte [dos_lfix], 0                  ; LOADFIX (kerndos/kdlaunch.inc),
    je .nolfix                              ; once: the next launch is an
    or ax, KDLF_LOADFIX                     ; ordinary one unless it says so
    mov byte [dos_lfix], 0
.nolfix:
    cmp byte [dos_mhma + OS88UI_CK_ON], 0   ; ...and kern_dos LOW, so the
    jne .hmadone                            ; KERNEL's stub expands it at
    or ax, KDLF_LOWDOS                      ; 0060:0000 and not in the HMA
.hmadone:                                   ; (SPEC.md 96.40.9.2)
    cmp byte [dos_msav + OS88UI_CK_ON], 0   ; ...and the image before the
    je .savdone                             ; park (96.40.9.3). HIBER.DRV asks
    or ax, KDLF_SAVE                        ; hb_pick itself, so a tick a
.savdone:                                   ; shortcut carried here parks
%endif
    cmp byte [dos_mmou+OS88UI_CK_ON], 0     ; ...and the page's own two bits
    je .wantmou                             ; (SPEC.md 96.36.8, 96.36.6): the
    or ax, KDLF_NOMOUSE                     ; pointer, which the KERNEL has to
.wantmou:                                   ; refuse because KDL_MOUBASE is its
    push bx                                 ; field and not ours...
    mov bl, [dos_cache]
    xor bh, bh
    mov bl, [bx + dos_ca_runs]              ; ...and the read-ahead's width, as
    inc bx                                  ; RUNS + 1 so that a zero means
    mov cl, KDLF_RAHSH                      ; kern_dos's own default. Auto
    cmp byte [dos_cache], DOS_CA_AUTO       ; sends the zero rather than the
    je .rahdone                             ; number behind it, so the day
    shl bx, cl                              ; KD_RAH_KEEP moves the two sides
    and bx, KDLF_RAHM                       ; do not have to move together
    or ax, bx
.rahdone:
    pop bx
    mov [di+KDL_FLAGS], ax
%ifdef DOS_MEGA
    mov ax, [dos_kdlrow + OP_R_OFF]         ; ...and WHERE THE LOW kern_dos IS,
    mov [di+KDL_L3OFF], ax                  ; whichever one is posted: the HMA
    mov ax, [dos_kdlrow + OP_R_ZKB]         ; image reads part 3 itself for an
    mov [di+KDL_L3ZB], ax                   ; EXEPACK program its unpacker
                                            ; refuses (SPEC.md 96.40.9.1)
    mov word [di+KDL_SNAPLIN], dos_snrec    ; ...and the CONSOLE, for the trip
    mov byte [di+KDL_SNAPHI], 0             ; to seed and capture (KERN-MEGA-
    mov al, [con_cx]                        ; PLAN 14.106): the record's
    mov ah, [con_cy]                        ; OFFSET, which the kernel makes
    mov [dos_snrec], ax                     ; linear, the cursor kern_dos
    mov word [dos_snrec + 2], 0             ; seeds the screen under, and the
                                            ; flag clear until it captures
%endif
    mov word [di+KDL_TLEN], KDL_MINE
    mov word [di+KDL_WAKE + 2], 0           ; ...and the live resume's, whose
                                            ; zero SEGMENT is what kd_resume
                                            ; reads as "this kernel does not
                                            ; offer one" (96.49)
    mov byte [di+KDL_NVOL], 0               ; **AND THE VOLUME COUNT** (96.46):
                                            ; the table is the KERNEL's, and
                                            ; zero is how a sender that cannot
                                            ; fill it says so - where a zeroed
                                            ; TABLE would read as eight copies
                                            ; of BIOS unit 0, every letter
                                            ; pointing at A:
    mov word [di+KDL_MOUBASE], 0            ; ...and the mouse's two, which are
                                            ; the KERNEL's like KDL_UNIT and
                                            ; KDL_DPT: DOS.O88 zeroes them so a
                                            ; kernel that does not patch them
                                            ; hands over a still pointer rather
                                            ; than a wild UART base (96.45)
    mov word [di+KDL_UNIT], 0               ; kern_dos mounts by VOLUME, and
                                            ; the volume is one of the fields

    ; **THE MACHINE'S OWN KB, OUT OF THE BDA** (0040:0013, int 12h's answer).
    ; Not OSAPI_SYS_KB: what kern_dos wants is CONVENTIONAL memory as the ROM
    ; reports it, which is the number a real DOS uses and the one the arena's
    ; ceiling is cut from.
    push ds
    mov ax, 0x0040
    mov ds, ax
    mov ax, [0x0013]
    pop ds
    mov [di+KDL_CAP], ax

    add di, KDL_BODY
%macro KDL_F 2
    mov si, %1
    mov cx, %2
    rep movsb
%endmacro
    KDL_FIELDS
%unmacro KDL_F 2
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

%assign KDL_ACC 0
%macro KDL_F 2
    %assign KDL_ACC KDL_ACC + %2
%endmacro
    KDL_FIELDS
%unmacro KDL_F 2
KDL_MINE equ KDL_ACC
%if KDL_ACC + KDL_BODY > KDL_SIZE
  %error "DOS.O88's launch block outgrew KDL_SIZE"
%endif
%endif                                  ; DOSKPART
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; -----------------------------------------------------------------------------
; dos_mem_fix - force [dos_keepc] to an arm this machine will actually honour
; in:  nothing; out: nothing, every register preserved
;
; **CONSUMER THREE** of dos_mem_whole (SPEC.md 47 rule 4, 96.36.1). A greyed
; arm refuses a CLICK - os88ui_radhit swallows the press - and that is the
; whole of what the control can do: it cannot refuse a FILE. [dos_keepc]
; arrives from a .LNK another machine wrote (96.25.2), so the range clamp
; there is not enough on its own, DOS_MEM_WHOLE being a legal value this
; machine may still not be able to carry out.
;
; It DEMOTES rather than refusing, to the arm below - which is what the user
; would have got before that arm existed - and it writes the pick BACK, so the
; page comes up showing what the machine will really do rather than an arm it
; is quietly ignoring.
;
; The high byte goes with it, because OS88UI_RD_SEL is a WORD and [dos_keepc]
; is its low half (96.36): every writer here is a byte writer, so one place
; has to own the other half or a link's 0x0102 draws a dot on no row at all.
; -----------------------------------------------------------------------------
dos_mem_fix:
    push si
    mov byte [dos_keepc+1], 0
%ifdef DOS_MEGA
    ; **ON kern_mega THERE IS ONE ARM** (docs/plans/completed/KERN-MEGA-DOS-PLAN.md
    ; 12.1 item 1): the program cannot run inside a protected-mode kernel, so
    ; every launch is the whole machine and the page draws no radio at all.
    ; A pick from a .LNK written on kern_big is not demoted, it is PROMOTED.
    mov byte [dos_keepc], DOS_MEM_WHOLE
    pop si
    ret
%endif
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    jb .out
    ja .demote                      ; ...and anything ABOVE the last arm is a
                                    ; link that was never ours
    call dos_mem_whole
    jnc .out
.demote:
    mov byte [dos_keepc], DOS_MEM_IN    ; ...back inside the OS, which is the
                                        ; arm every machine can do (96.36.1)
.out:
    pop si
    ret

; -----------------------------------------------------------------------------
; dos_cache_arm - hand the dial to the kernel (SPEC.md 18.95.8)
; preserves every register; the flags are NOT preserved
;
; Half a bracket. `dos_cache_free` is the other half and `dos_run`'s `.out`
; owes it on every path but the posted one - the same bracket, and for the same
; reason, as `dos_drv_take` / `dos_drv_back` beside it: what a launch does to
; the machine has to be undone by the launch and not by the next mount.
;
; Auto sends the SENTINEL and not `dos_ca_runs`' row for it. That row is
; `KD_RAH_KEEP`, which is what Auto means on the arm where `kern_dos` owns the
; cache; here the kernel does, and Auto means *you decide* (SPEC.md 18.95.5).
; -----------------------------------------------------------------------------
dos_cache_arm:
    push ax
    push bx
    mov al, DSK_RAH_AUTO
    cmp byte [dos_cache], DOS_CA_AUTO
    je .say
    mov bl, [dos_cache]
    xor bh, bh
    mov al, [bx + dos_ca_runs]
.say:
    call OSAPI_DSK_CACHE
    pop bx
    pop ax
    ret

; dos_cache_free - ...and give it back, which also RE-TAKES a cache the launch
; shed. Nothing else would: the kernel re-claims at a MOUNT (SPEC.md 18.95.5.1)
; and a DOS session mounts nothing, so without this a machine that ran one
; program with the dial on Off has no directory cache until something is
; inserted or navigated to.
dos_cache_free:
    push ax
    mov al, DSK_RAH_AUTO
    call OSAPI_DSK_CACHE
    pop ax
    ret

; in:  AX = 0..65535, DI -> five bytes inside a literal; clobbers nothing
;
; Into the LITERAL rather than into a buffer, which is dos_fmt_exit's shape
; one line along: the line is drawn by one opaque font_run and a number
; assembled anywhere else would need a second store to get there.
; -----------------------------------------------------------------------------
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dos_mem_num:
    mov cx, 5                       ; the arena's field: five digits, which is
    jmp short dos_mem_numn          ; every KB figure a machine under 1MB has
dos_mem_num2:
    mov cx, 2                       ; ...and a DRIVER CLASS's, which is `(NNK)`
dos_mem_numn:                       ; in a check box's own label. The widths are
    push ax                         ; fixed because the strings around them are
    push bx
    push cx
    push dx
    push di
    add di, cx                      ; the units digit, and work backwards
    dec di
    mov bx, 10
.d:
    xor dx, dx
    div bx                          ; AX = quotient, DX = this digit
    add dl, '0'
    mov [di], dl
    dec di
    dec cx
    jz .out
    or ax, ax
    jnz .d
.blank:
    mov byte [di], ' '              ; a LEADING BLANK and not a zero: the two
    dec di                          ; lines sit under one another and 00419
    loop .blank                     ; reads as a different quantity
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mem_take - commit the memory BLOCK: the limit field and the arm
; out: nothing; every register preserved
;
; EMPTY IS ZERO AND ZERO IS "ALL", which is what makes the field need no
; second control: a user who wants the machine's own answer clears the box.
; Anything that is not a digit ends the number, so a half-typed entry is the
; digits in front of it rather than a refusal in the middle of typing.
;
; **AND THE ARM GOES WITH IT** (SPEC.md 96.36.1). This is the block's one
; commit point - all four callers are leaving the page or launching - so it is
; where the pick meets the machine, and the page comes back showing what will
; really happen rather than an arm being quietly ignored.
;
; **THE PARSE IS A SEPARATE ENTRY BECAUSE THE FIGURE IS LIVE** (SPEC.md
; 96.36.10). Typing in the field has to move the arena row under it, and that
; is a read of the digits and nothing else: committing the ARM on a keystroke
; would make the radio's pick take effect halfway through the user changing
; their mind about something else.
; -----------------------------------------------------------------------------
dos_mem_take:
    call dos_mem_parse
    jmp  dos_mem_fix                ; ...and the arm, which is the other half
                                    ; of this block (SPEC.md 96.36.1)

; -----------------------------------------------------------------------------
; dos_mem_parse - the limit field's digits -> [dos_memkb] (0 = no limit)
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_mem_parse:
    push ax
    push bx
    push cx
    push si
    xor ax, ax
    mov si, dos_mbuf
    mov bx, 10
.d:
    mov cl, [si]
    cmp cl, '0'
    jb .out
    cmp cl, '9'
    ja .out
    mul bx
    sub cl, '0'
    xor ch, ch
    add ax, cx
    inc si
    jmp short .d
.out:
    mov [dos_memkb], ax
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mem_put - [dos_memkb] -> the limit field's text (0 = empty)
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_mem_put:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    mov byte [dos_mbuf], 0
    mov ax, [dos_memkb]
    or ax, ax
    jz .sync
    mov di, dos_mbuf + DOS_MEMMAX   ; right to left into the buffer, then
    mov byte [di], 0                ; shuffled down - five digits is not worth
    mov bx, 10                      ; a second pass over
    mov cx, DOS_MEMMAX
.d:
    xor dx, dx
    div bx
    add dl, '0'
    dec di
    mov [di], dl
    dec cx
    jz .move
    or ax, ax
    jnz .d
.move:
    mov si, di
    mov di, dos_mbuf
.m:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .m
.sync:
    mov si, dos_mln
    call os88line_resync
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_key - W_ONKEY. The field owns a keystroke it can use; everything else
; comes back here (os88line_key's CF is that split).
; in:  AL = ascii, AH = scan, SI = the window
; -----------------------------------------------------------------------------
dos_key:
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    ; --- THE ABOUT CARD EATS THE KEY THAT TAKES IT DOWN (SPEC.md 96.51) ------
    ; ABOVE the Alt+Enter test below, which is otherwise "above everything":
    ; a card that is up is what the user is looking at, so the keystroke that
    ; dismisses it must not also go full screen, and must not reach the path
    ; box either. CF = 1 from here means it was ours.
    call dos_abdismiss
    jc .done
%ifdef DVW_SPLIT
    cmp byte [dvw_run], 0           ; **A RUNNING PROGRAM OWNS THE KEYS**, and
    je .nodvw                       ; what it does with them - the ring, the
    call hent_dvw_key               ; frozen test, Alt+Enter's post - is the
    jmp .done                       ; PART's (SPEC.md 96.54.9): its cells
.nodvw:                             ; decide it. CF=0 (.done's clc)
%elifdef DVW
    cmp byte [dvw_run], 0           ; **A RUNNING PROGRAM OWNS THE KEYS**
    je .nodvw                       ; (SPEC.md 96.54): into its ring, which
    cmp ax, KSC_ENTER << 8          ; its INT 16h drains - but ALT+ENTER is
    jne .dvkey                      ; the full screen (96.54.1), for a running
    mov byte [dvw_fsreq], 1         ; program and a frozen one alike, entered
    call OSAPI_WM_WAKE              ; from the wake: the bracket wants the
    jmp short .dvtook               ; lock free and this holds it. BX = SI =
.dvkey:                             ; the window, since the entry's `mov bx,
    cmp byte [dvw_frz], 0           ; si`. A frozen program takes no keys
    jne .dvtook
    call dvw_kput
.dvtook:
    jmp .done                       ; CF=0, the key is consumed (.done's clc)
.nodvw:
%endif
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    ; --- ALT+ENTER IS FULL SCREEN, ABOVE EVERYTHING (SPEC.md 96.33.5.1) -----
    ; AX = 0x1C00 is the kernel's synthesised keystroke (SPEC.md 9.7.1) - no
    ; XT ROM enqueues this combination at all, so int 16h never carries it and
    ; kbd_track latches the scancode instead. HERE, in front of dos_place and
    ; the focus test, because every field below this would otherwise get first
    ; refusal on it: the path box holds the caret on a fresh window and
    ; os88line_key's own extended arm reads AL = 0 keys.
    cmp ax, KSC_ENTER << 8
    jne .notfull
    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .no                         ; a setup page has no screen to take, which
    call dos_defocus                ; is dos_oncmd's own test - and the caret
    mov si, [dos_win]               ; goes the same way the MENU ITEM sends it
    call dos_fsx                    ; ...and the very same proc, so the key and
    jmp .done                       ; the item cannot drift apart
.notfull:
%endif
    ; **NO DST_IDLE GATE.** It used to refuse every key with nothing named,
    ; because the only field was the arguments one; the MAIN page's field is
    ; the PATH BOX now and an empty one is the state where typing matters most
    ; (SPEC.md 96.32.1).
    call dos_place                   ; every control of this page, so a hit and
                                     ; a draw cannot disagree
    call dos_focused                 ; SI = whoever has the caret, or 0
    or si, si
    jz .nofield                      ; **NOT `.no`** - `.nofield` is where
                                     ; ENTER is handled, and its own comment
                                     ; below says why: the run used to sit
                                     ; under the focus test, so pressing a
                                     ; button and then Enter did nothing at
                                     ; all. Jumping past it with no field
                                     ; focused puts that back
.have:
    cmp byte [si+LN_FOCUS], 0
    je .nofield
    mov dx, [si+LN_VIEW]             ; bank what os88line_edit compares against
    mov [dos_lnv], dx                ; - IN MEMORY, because AL is the KEYSTROKE
    mov dx, [si+LN_LEN]              ; and loading the view into AX would eat it
    mov [dos_lnl], dx
    mov dx, [si+LN_CAR]              ; ...AND THE CARET, which is the third
    mov [dos_lnc], dx                ; thing a key moves: the bar is a 1px fill
                                     ; and a cell the repaint skips KEEPS it,
                                     ; so every backspace used to leave one
                                     ; standing (SPEC.md 83.1.1)
    call os88line_key                ; CF=0 = the field used it. IT DOES NOT
    jnc .edited                      ; DRAW - its header says "redraw the
                                     ; field", and the redraw is the caller's
    ; --- ENTER IN THE PATH BOX IS `Run` (SPEC.md 96.32.1.1) ------------------
    ; It used to fall through to .nofield, where Enter means §96.19.4's "run it
    ; again" and is reached only from DST_RAN or DST_ERR - so on a fresh window
    ; a typed path and an Enter did NOTHING AT ALL: no launch, no error, no
    ; repaint. A user cannot tell a field that refused them from one that is
    ; not wired up. The SAME dos_go the button calls, so the two cannot drift;
    ; every other field keeps 96.19.4's meaning, which is what the arguments
    ; row on the setup page needs.
    cmp al, 13
    jne .nofield
    cmp si, dos_pln
    jne .nofield
    call dos_go
    jmp short .done
.edited:
    mov ax, [dos_lnv]
    mov bx, [dos_lnl]
    mov dx, [dos_lnc]
    call os88line_edit               ; EDIT and not DRAW: typing the 21st
    add [dos_ncell], cx              ; character must not repaint twenty that
    inc word [dos_nkey]              ; did not change (SPEC.md 96.19.1)
    cmp si, dos_mln                  ; ...AND THE LIMIT MOVES THE FIGURE ABOVE
    jne .done                        ; IT (SPEC.md 96.36.10). Every other field
    call dos_mem_parse               ; on this page redraws itself and nothing
%ifdef DOS_SETPART                  ; else; this one is a TERM of the arena
    mov al, SPD_ROW
    call dos_spdo
%else
    call dos_mem_row
%endif
                                     ; row, and its value was reaching
                                     ; [dos_memkb] only at dos_mem_take, whose
                                     ; four callers are all leaving the page or
                                     ; launching - so the user typed a cap
                                     ; while WATCHING the number it caps and
                                     ; the number ignored them. One row, not a
                                     ; repaint: dos_mem_row is the same single
                                     ; line the dial redraws (96.36.6.3)
    jmp short .done

    ; --- ENTER RUNS IT AGAIN (SPEC.md 96.19.4) -------------------------------
    ; **REACHED WHETHER OR NOT A FIELD HAS THE CARET**, which is not where this
    ; started: the run used to sit under the focus test, so pressing Done and
    ; then Enter did nothing at all. A window whose only action key works only
    ; while a particular box is focused is one the user thinks is broken.
.nofield:
    cmp al, 13
    jne .console                     ; every other key is the prompt's, if the
                                     ; prompt is on this page
    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .rerun                       ; a setup page has no console on it, so
    cmp word [dos_cmdn], 0           ; Enter there means only the re-run
    jne .console                     ; **A TYPED LINE WINS OVER THE RE-RUN.**
                                     ; Enter on an EMPTY prompt is still
                                     ; §96.19.4's "run it again", which is what
                                     ; the dosargs row drives; Enter on a line
                                     ; the user has typed may not silently
                                     ; re-launch the last program instead
    cmp byte [dos_state], DST_RAN    ; RAN, ERR or HELD (96.21.9): a held
    jb .console                      ; launch's Enter on an empty line is the
    cmp byte [dos_state], DST_HELD   ; start it was spared
    jbe .again
    jmp short .console               ; ...and with nothing to re-run it is the
                                     ; prompt's again, which is a fresh line
                                     ; and what DOS does with a bare Enter
.rerun:
    cmp byte [dos_state], DST_RAN    ; only from a program that has FINISHED -
    jb .no                           ; DST_READY is one already queued and a
    cmp byte [dos_state], DST_HELD   ; second wake would run it twice - or
    ja .no                           ; one held at its launch (96.21.9)
.again:
    mov byte [dos_state], DST_READY
    mov bx, [dos_win]
    call OSAPI_WM_WAKE               ; ...and dos_wake does the rest, exactly
    jmp short .done                  ; as it did for the launch
.console:
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .no                          ; the console is the MAIN page's band
    call dos_con_key                 ; CF=1 = not the console's either, and the
    jnc .done                        ; scan code may still be somebody's
%endif
.no:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; dos_click - W_ONCLICK. A press inside the field takes the caret; a press
; outside it gives the caret up.
; in:  CX = x, DX = y, SI = the window
; -----------------------------------------------------------------------------
dos_click:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si
    call dos_abdismiss              ; ...and the click likewise: dismissing the
    jc .out                         ; card is not also a button press or a
                                    ; field focus (SPEC.md 96.51)
    ; **NO DST_IDLE GATE** (SPEC.md 96.32.1): an idle DOS is the internal
    ; COMMAND.COM and its bar is live, where the old main page had nothing on
    ; it but an arguments field for a program that did not exist.
    call dos_place                  ; every control of this page, once

    ; --- EVERY BUTTON ON EITHER PAGE, ARMED AND NOT FIRED --------------------
    ; SPEC.md 13.6: a button has no safe prefix action, so it wants the
    ; RELEASE. This used to be four os88ui_bhit ladders that ACTED here, which
    ; meant a mis-aimed press on 'Run' ran, and nothing on the glass ever said
    ; a control was being held. os88ui_btnpress arms and draws it down; the
    ; action is in dos_onup (SPEC.md 20.5.1.3).
    ;
    ; The caret is NOT given up here. A press that turns out to be a cancel
    ; must leave the field exactly as it was, so dos_defocus moved to the
    ; release beside the action it belongs to.
    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .setup
    mov si, dos_pln                 ; the bar's box is all that is left there
    jmp .field

    ; --- THE SETUP PAGE: its own controls, the buttons having had the press --
.setup:
%ifdef DOS_SETPART
    mov al, SPD_CLICK               ; every control the page has, the
    call dos_spdo                   ; part's (SPEC.md 96.32.2.3)
%else
    SPB_CLICK .wl
%endif
.wl:
%ifdef DVW
    call dos_wlabel                 ; the box, the arm or `Close when done`
%endif                              ; may have moved (SPEC.md 96.54.10)
    jmp .out

    ; --- one field, hit-tested ----------------------------------------------
.field:
    call os88line_hit               ; CF=0 = inside
    jc .away
    cmp byte [si+LN_FOCUS], 0
    jne .move                       ; already ours: just move the caret
    call dos_defocus                ; ...and nobody else keeps theirs
    mov byte [si+LN_FOCUS], 1
    call os88line_draw              ; ...and the frame gains its caret
    jmp short .out
.move:
    call dos_caret_to               ; ...and the BAR follows it (96.32.4)
    jmp short .out
.away:
    cmp byte [si+LN_FOCUS], 0
    je .out
    mov byte [si+LN_FOCUS], 0
    mov ax, [si+LN_CAR]             ; ...its index, as above
    call os88line_caroff            ; ONE CELL, not the field (SPEC.md 13.14.6)
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dos_ondrag - W_ONDRAG (SPEC.md 13.8.2): the held button tracks the pointer
; in:  CX = x, DX = y (SCREEN), SI = the window; UI task, gfx lock held
; out: nothing; preserves all registers
;
; The window can have been dragged since the press, so the rects are placed
; again before the record is asked - CX/DX are already screen coordinates,
; which is what os88ui_btndrag wants.
; -----------------------------------------------------------------------------
dos_ondrag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, dos_btrec               ; (dos_bplace lays the page out first)
    call os88ui_btndrag             ; redraws only if the answer CHANGED
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_onup - W_ONMOUSEUP (SPEC.md 13.7): the button FIRES here
; in:  CX = x, DX = y (SCREEN, and possibly outside the window), SI = window
; out: nothing; preserves all registers
;
; Every one of these four actions used to run from dos_click, on the PRESS.
; A mis-aimed press on 'Run' ran the command; there was no way to change your
; mind once the button was down, and nothing on the glass said it was down at
; all. os88ui_btnup answers only for a press and a release on the SAME
; control, so sliding off is now the cancel it always should have been.
;
; THE CARET IS GIVEN UP HERE and not at the press, which is where dos_defocus
; used to sit. A control that consumes a click owes the focused field its
; caret back (SPEC.md 13.14.6) - but only if it actually fires, because a
; press the user slid away from must leave the field exactly as it was.
; -----------------------------------------------------------------------------
dos_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, dos_btrec               ; the window can have moved (13.7):
    call os88ui_btnup               ; dos_bplace lays the page out from where
                                    ; it is NOW. AX = what FIRED, 0 = cancelled
    or ax, ax
    jz .out
    mov bx, si                      ; **BX BACK TO THE WINDOW BEFORE ANYTHING
                                    ; ACTS.** Every one of the four actions
                                    ; below reaches dos_swap, dos_go or
                                    ; dos_sav_go, and all three take the
                                    ; WINDOW in BX - dos_swap's first move is
                                    ; OSAPI_WM_CONTENT with it. Left pointing
                                    ; at the record they got a garbage window,
                                    ; WM_CONTENT answered ~0, and the Setup
                                    ; page painted its controls over the menu
                                    ; bar and the desktop. The press handler
                                    ; this code came out of held the window in
                                    ; BX throughout, which is why its own
                                    ; os88ui_bhit calls were each bracketed
                                    ; with push bx / pop bx

    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .setup
    cmp al, DOS_BT_ENV
    jne .run
    call dos_set_open               ; Setup, the bar's button
    jmp short .out
.run:
    call dos_defocus
    call dos_go                     ; Run: what the box names, as it stands
    jmp short .out
.setup:
    cmp al, DOS_BT_RET
    jne .sav
    call dos_defocus
    call dos_mem_take               ; **THE LIMIT IS READ ON THE WAY OUT**, so
                                    ; a number typed with nothing pressed after
                                    ; it is still the setting - a field that
                                    ; commits only on Enter loses what was
                                    ; typed, silently
    mov byte [dos_page], DOS_PAGE_MAIN
    call dos_swap
%ifdef DOS_SETPART
    call dos_sp_drop                ; ...and the page's code goes back with
%endif                              ; it (SPEC.md 96.32.2.3)
    jmp short .out
.sav:
    call dos_defocus                ; ...as Run does, and for its reason
    call dos_mem_take               ; ...and the shortcut carries what is in
%ifdef DOS_SETPART                 ; the boxes NOW, for the same reason -
    mov al, SPD_SAVE                ; the writer being the Setup part's
    call dos_spdo
%else
    call dos_sav_go
%endif
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_set_open - the main page gives way to Setup: the bar's button and the
; menu's 'Environment' both. in: BX = the window; out: nothing, every register
; as dos_swap leaves it
; -----------------------------------------------------------------------------
dos_set_open:
    call dos_defocus                ; the caret does not follow us off the page
%ifdef DOS_SETPART
    call dos_sp_get                 ; **THE PAGE'S CODE, READ NOW** (SPEC.md
    jc .r                           ; 96.32.2.3) - or not opened, and the
%endif                              ; menu bar says why
%ifdef DOS_WTAIL
%ifdef DOS_WIN16                    ; (no WIN16 - kern_small - no NE to grey for)
    call dos_hne_ask                ; ...and is the program an NE (96.44.20)
%endif
%endif
    mov byte [dos_page], DOS_PAGE_SET
    jmp dos_swap
%ifdef DOS_SETPART
.r:
    ret
%endif

; -----------------------------------------------------------------------------
; dos_defocus - no field on either page keeps the caret
; dos_defocus_but - ...except the one in SI
; Each drops ONE CELL per field that had it, never a repaint (SPEC.md 13.14.6).
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_caret_to - move a focused field's caret to the press (SPEC.md 96.32.4)
; in:  SI = the block, CX = x, DX = y; the gfx lock is held
; out: os88line_click's CF; every register preserved
;
; `os88line_click` sets `LN_CAR` and `LN_FOCUS` and draws NOTHING - it is state,
; not pixels, and the caller owns the glass. Every other package in the tree
; does own it: the browser and telnet repaint the whole field, ftpd moves the
; one cell. THIS PACKAGE DID NEITHER at all three of its call sites, so a click
; in the middle of a line moved the caret and left the bar standing where it
; was - reported from the field as exactly that.
;
; ONE CELL EACH WAY and not `os88line_draw` (SPEC.md 13.14.6): `os88line_click`
; cannot scroll - it clamps to `LN_LEN` and never writes `LN_VIEW` - so the only
; pixels that change are the cell losing the bar and the cell gaining it.
; -----------------------------------------------------------------------------
dos_caret_to:
    push ax
    mov ax, [si+LN_CAR]             ; the OLD one off - one opaque cell, which
    call os88line_caroff            ; puts the character under it back
    call os88line_click
    pushf                           ; ...and the new one on, wherever the clamp
    mov ax, [si+LN_CAR]             ; left it. The flags are the ANSWER here, so
    call os88line_caron             ; they cross the second draw banked
    popf
    pop ax
    ret

dos_defocus:
    push si
    xor si, si
    call dos_defocus_but
    pop si
    ret

dos_defocus_but:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, si
    mov si, dos_pln                 ; the PATH BOX is a field like any other
    call .one                       ; (SPEC.md 96.32.1) and the caret must not
    mov si, dos_ln                  ; survive a move into the setup area
    call .one
    mov si, dos_mln                 ; ...the memory page's too: "no field on
    call .one                       ; EITHER page" is now three pages, and a
    mov cx, DOS_ENVN                ; caret left behind on a page nobody can
    mov si, dos_eln                 ; see still takes the keystrokes
.e:
    call .one
    add si, DOS_LNSZ
    loop .e
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.one:
    cmp si, di
    je .skip
    cmp byte [si+LN_FOCUS], 0
    je .skip
    mov byte [si+LN_FOCUS], 0
    push cx
    mov ax, [si+LN_CAR]             ; **THE CARET'S INDEX, WHICH IS AN
                                    ; ARGUMENT** - os88line_caroff takes the
                                    ; buffer position in AX and this passed
                                    ; whatever happened to be there, so the
                                    ; cell it repainted was not the cell with
                                    ; the bar in it. The focus byte went to 0
                                    ; and the bar stayed on the glass, which
                                    ; is how a field the user had left kept
                                    ; showing a caret
    call os88line_caroff
    pop cx
.skip:
    ret

; -----------------------------------------------------------------------------
; dos_focused - SI = the field with the caret, or SI = 0
; -----------------------------------------------------------------------------
; **IT KNOWS NOTHING ABOUT PAGES NOW**, and that is what the Setup page cost:
; it holds THREE fields where every other page held one, so a routine that
; picked a field from the page number would have had to pick between three of
; them - and the answer it wants is already in the blocks. Only one can be
; focused at a time (dos_defocus_but is what guarantees it), so walking the
; whole set is both shorter and correct on a page nobody has invented yet.
dos_focused:
    push cx
    mov si, dos_pln                 ; the path box, then the arguments, then
    cmp byte [si+LN_FOCUS], 0       ; the memory limit...
    jne .got
    mov si, dos_ln
    cmp byte [si+LN_FOCUS], 0
    jne .got
    mov si, dos_mln
    cmp byte [si+LN_FOCUS], 0
    jne .got
    mov si, dos_eln                 ; ...and the four environment rows, whose
    mov cx, DOS_ENVN                ; first one is also the Setup page's
.e:
    cmp byte [si+LN_FOCUS], 0
    jne .got
    add si, DOS_LNSZ
    loop .e
    xor si, si
.got:
    pop cx
    ret

; -----------------------------------------------------------------------------
; dos_oncmd - the Program menu (SPEC.md 12.2, 96.32.3)
; in:  AL = the item, AH = the menu, SI = the owning window; UNDER THE GFX
;      LOCK, on the UI task - which is a click handler's context exactly, so
;      both items call the routines the bar's buttons call
; out: nothing
;
; IT MIRRORS THE BAR AND NOTHING ELSE. The two things this window can be ASKED
; to do are run the program and open the setup area, so those are the two
; items - a menu that offered a third would be offering something the window
; cannot do from its own face.
; -----------------------------------------------------------------------------
dos_oncmd:
    push ax
    push bx
    mov bx, si
%ifdef DVW
    cmp al, 3                       ; 'Run in Window' is a MODE (SPEC.md
    jne .notw                       ; 96.54.10): it flips the Setup box's byte,
    xor byte [dos_mwin + OS88UI_CK_ON], 1   ; which is what dos_run reads on
    call dos_wlabel                 ; every launch - the prompt's too - and
    cmp byte [dos_page], DOS_PAGE_MAIN  ; runs nothing. Live during a run: it
    je .out                         ; says what the NEXT launch does. Greyed
    call dos_swap                   ; where the box is (dos_wlabel), so no
    jmp short .out                  ; test here; on Setup the page repaints
.notw:                              ; so the box follows the menu
    cmp byte [dvw_run], 0           ; nothing else on this menu while a
    jne .out                        ; program is running in the window
%endif
    or al, al
    jnz .notrun
    call dos_defocus                ; ...the caret too, exactly as the BUTTON
                                    ; does: the menu item is the same action
                                    ; and owes the same thing
    call dos_go                     ; 'Run', which is the bar's button
    jmp short .out
.notrun:
    cmp al, 1
    jne .full
.env:
    mov bx, [dos_win]               ; 'Environment', which is the bar's other
    call dos_set_open               ; button, and opens the page the same way
    jmp short .out
.full:
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .out                        ; the console is the MAIN page's band, and
    call dos_defocus                ; a setup page has no screen to take
    mov si, [dos_win]
    call dos_fsx                    ; 'Full Screen' (SPEC.md 96.33.5)
%endif
.out:
    pop bx
    pop ax
    ret

%ifdef DVW
; -----------------------------------------------------------------------------
; dos_wlabel - `Run in Window` as the mode stands (SPEC.md 96.54.10)
; in:  nothing; out: nothing, every register preserved
;
; The item's first byte is a GUTTER, `*` ticked and a space cleared (Desk
; Clock's mark), and it greys where the Setup box greys: arm 1, and `Close
; when done`. Run at every paint and every Setup press, so the item cannot
; disagree with the box.
; -----------------------------------------------------------------------------
dos_wlabel:
    push ax
    mov ax, dos_l_wrun0
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    je .set
%ifdef DOS_WTAIL
    cmp byte [dos_mhead + OS88UI_CK_ON], 0
    jne .set
%endif
    mov al, ' '
    cmp byte [dos_mwin + OS88UI_CK_ON], 0
    je .mk
    mov al, '*'
.mk:
    mov [dos_l_wrun], al
    mov ax, dos_l_wrun
.set:
    mov [dos_items_prog + 6], ax
    pop ax
    ret
%endif
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; -----------------------------------------------------------------------------
; dos_path_args - the path box holds `PATH ARGUMENTS`: split it (SPEC.md 96.32.1.1)
; in:  nothing; out: nothing; every register preserved
;
; Everything up to the FIRST SPACE is the path and everything after it is the
; argument text, which is exactly what dos_con_prog does one door along - so
; the same line typed at the prompt and typed into the box gets the same
; launch, those two being the same sentence in two places. An 8.3 name cannot
; contain a space, so the split can never cut a path in half; and os88line_key
; takes 0x20..0x7E, so there is no tab in a field to split on either.
;
; **IT MOVES THE TAIL RATHER THAN READING PAST IT.** The arguments field is
; what the program is given (96.19), what Save Shortcut writes (96.21) and
; what `run it again` re-runs, so a tail left in the path box would be a launch
; nobody could repeat and a shortcut that recorded half of it. After the split
; the box holds the path alone and the field holds the arguments, which is also
; the only feedback that anything was understood.
;
; With no space in the box it does NOTHING, which is what every other door
; needs: an association, a shortcut and the console each fill the box with a
; resolved path and the field separately, and a split that fired on those would
; clear arguments they had just set.
; -----------------------------------------------------------------------------
dos_path_args:
    push ax
    push cx
    push si
    push di
    mov si, dos_path
.find:
    mov al, [si]
    or al, al
    jz .none
    cmp al, ' '
    je .split
    inc si
    jmp short .find
.split:
    mov byte [si], 0                ; the path ends here...
    inc si
.skip:
    cmp byte [si], ' '
    jne .copy
    inc si
    jmp short .skip
.copy:
    mov di, dos_args                ; ...and the rest is the tail, bounded the
    mov cx, DOS_ARGMAX              ; way dos_con_prog bounds its own
.c:
    mov al, [si]
    or al, al
    jz .cend
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .c
.cend:
    mov byte [di], 0
    mov si, dos_pln                 ; both boxes re-measured from their text,
    call os88line_resync            ; because a field whose LN_LEN disagrees
    mov si, dos_ln                  ; with its buffer draws the old length
    call os88line_resync
.none:
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_go - RUN what the box names, with the setup as it stands (SPEC.md 96.32.1)
; in:  BX = the window, gfx lock HELD (a click callback's context)
; out: nothing; [dos_state] and a posted wake are the answer
;
; **IT DOES NOT RUN THE PROGRAM**, it asks for one: the run happens in the
; wake handler, which is the one callback without the gfx lock (SPEC.md 74.1)
; and the only context allowed to take the machine. That is why Enter and this
; button can share a routine at all.
;
; An EMPTY box is not a refusal - it is the internal COMMAND.COM (96.32.1),
; which has no display yet - so it does nothing and says nothing, where a path
; that does not resolve is the user's to fix and the window tells them.
; -----------------------------------------------------------------------------
dos_go:
    push ax
    push bx
%ifdef DVW
    cmp byte [dvw_run], 0           ; one program a window (SPEC.md 96.54)
    jne .out
%endif
    cmp byte [dos_path], 0
    je .out                         ; the internal interface: not yet ours
    call dos_path_args              ; **AND THE BOX MAY HOLD ARGUMENTS** (SPEC.md
                                    ; 96.32.1.1): the resolver has no opinion
                                    ; about spaces, so `B:\BIN\FOO.COM /M` was
                                    ; an 8.3 name of `FOO.COM /M` and the answer
                                    ; was "Its folder could not be opened" -
                                    ; about the one part of the line that was
                                    ; right
    call dos_mem_take               ; whatever the boxes hold NOW, both of them
    call dos_path_take
    jc .nopath
%ifdef DOSKPART
    mov byte [dos_wok], DOS_W_ASK   ; A NEW LAUNCH IS A NEW QUESTION (96.42:
                                ; 96.42): the confirmation is about THIS
                                ; program ending the session, so an answer
                                ; given for the last one does not carry
%endif
    mov byte [dos_state], DST_READY
    mov bx, [dos_win]
    call OSAPI_WM_WAKE              ; CF=1 = the ring was full, which dos_paint
                                    ; re-kicks; it is not an error (SPEC.md
                                    ; 74.1) and there is nothing to report
    mov bx, [dos_win]
    call dos_swap
    jmp short .out
.nopath:
    mov byte [dos_err], DER_GOTO    ; "Its folder could not be opened", which
    mov byte [dos_state], DST_ERR   ; is what an unresolvable path IS
    mov bx, [dos_win]
    call dos_swap
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_place - put every control of the page that is up where the window is now
; in:  BX = the window; every register preserved
;
; **ONE CALL BEFORE ANY HIT TEST AND ANY DRAW**, which is fm_hit's discipline
; (SPEC.md 22) applied to a window with three pages: a rect that is computed
; in the painter and computed again in the click handler is a rect that drifts
; the day one of the two is edited. Here the click handler does not compute at
; all - it places, then tests what was placed.
; -----------------------------------------------------------------------------
dos_place:
    push ax
    push bx
    push cx
    push dx                         ; **DX IS THE CLICK'S Y** and every placer
                                    ; below goes through OSAPI_WM_GEOM, which
                                    ; RETURNS the content box in CX and DX. A
                                    ; caller that placed and then hit-tested
                                    ; was testing against the content height
    push si
    push di
    cmp byte [dos_page], DOS_PAGE_MAIN
    jne .setup
    call dos_bar_rects              ; the path box and the bar's two buttons
    jmp short .out
.setup:
%ifdef DOS_SETPART
    mov al, SPD_PLACE               ; Save Shortcut and Return, the command
    call dos_spdo                   ; box, the arguments and environment
                                    ; boxes and the memory block's two - the
                                    ; Setup part's to place (96.32.2.3)
%else
    SPB_PLACE
%endif
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dos_bplace - OS88UI_BT_PLACE (SPEC.md 20.5.1.3.5): BX = the record, SI = the
; window; every register preserved. dos_place with the window where it wants
; it, and nothing else: a window dragged by its title is REPLAYED and not
; painted, so the press, the drag and the release each get the page laid out
; from where the window IS - which is also what aims the record at the page's
; own pair (dos_bar_rects / dos_furn_rects) before the library reads it
dos_bplace:
    push bx
    mov bx, si
    call dos_place
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_fld_init - the field's block, once, at entry
; -----------------------------------------------------------------------------
dos_fld_init:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, dos_pln                 ; THE PATH BOX FIRST (SPEC.md 96.32.1) -
    mov ax, dos_path                ; it is the one control on the main page
    mov [si+LN_BUF], ax             ; and the one an association fills in
    mov word [si+LN_MAX], DOS_PBUF - 1
    mov byte [si+LN_FOCUS], 0
    mov byte [dos_path], 0          ; ...empty, which IS the internal
    call os88line_resync            ; COMMAND.COM and not an absence

    mov si, dos_ln
    mov ax, dos_args
    mov [si+LN_BUF], ax
    mov word [si+LN_MAX], DOS_ARGMAX
    mov byte [si+LN_FOCUS], 0
    mov byte [dos_args], 0
    call os88line_resync            ; LN_LEN/LN_CAR/LN_VIEW from the text

    mov si, dos_eln                 ; ...and the four environment rows
    mov bx, dos_ebuf
    mov cx, DOS_ENVN
.e:
    mov [si+LN_BUF], bx
    mov word [si+LN_MAX], DOS_ENVBUF
    mov byte [si+LN_FOCUS], 0
    mov byte [bx], 0
    push cx
    call os88line_resync
    pop cx
    add si, DOS_LNSZ
    add bx, DOS_ENVBUF
    loop .e

    mov si, dos_mln                 ; ...and the memory limit (SPEC.md 96.25).
    mov ax, dos_mbuf                ; THE DEFAULTS ARE SET HERE and not in the
    mov [si+LN_BUF], ax             ; bss table, because -f bin zeroes nothing
    mov word [si+LN_MAX], DOS_MEMMAX  ; and "keep the cache" is a 1
    mov byte [si+LN_FOCUS], 0
    mov byte [dos_mbuf], 0
    mov word [dos_memkb], 0         ; 0 = as much as the machine will give,
%ifndef DOS_WTAIL                   ; (DOS_WTAIL: dos_entry's dos_fld_def has
    mov word [dos_keepc], DOS_MEM_IN    ; set these three on both hosts)
                                    ; which is what a double click gets.
                                    ; A WORD: OS88UI_RD_SEL is one (96.36)
    mov word [dos_cache], DOS_CA_AUTO   ; ...and the cache's dial, the same way
                                    ; - one list on both arms (96.36.6), so
                                    ; there is no parked second pick
    mov byte [dos_mhdd + OS88UI_CK_ON], 1   ; ...and the three boxes, which are
%endif
    mov byte [dos_mnet + OS88UI_CK_ON], 0   ; ...but NETWORK CLEARED (SPEC.md
                                    ; 96.36.7.4, the owner's: "if they want
                                    ; network, they have to go ask for it") -
                                    ; and the other two TICKED: a tick is
    mov byte [dos_mmou + OS88UI_CK_ON], 0   ; "leave it as it is" on the two
%ifdef DVW
    mov byte [dos_mwin + OS88UI_CK_ON], 0   ; (and Run in a window CLEARED:
%endif                                      ; the bracket is the default, 96.32.2.4)
%ifdef DOS_MEGA
    mov byte [dos_mhma + OS88UI_CK_ON], 1   ; (and kern_dos in the HMA, which
                                            ; is the arena kern_mega shipped)
    mov byte [dos_msav + OS88UI_CK_ON], 0   ; ...and the park alone: the image
%endif                                      ; costs the trip its write
                                    ; drivers and "do something" on the mouse,
                                    ; so the default state of all three is the
                                    ; machine as the user already has it
    call os88line_resync

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_lnk_open - was this instance handed a .LNK? If so, BECOME what it names
; in:  dos_name = what we were launched with, [dos_dir]/[dos_vol] its folder
; out: nothing; on any refusal the state is left exactly as it was
;
; A shortcut is READ HERE AND NOT IN THE WAKE, because everything downstream -
; the window's own caption, the arguments field, the environment page - wants
; the TARGET's name rather than the link's, and the wake is where the program
; is already being launched.
;
; **A REFUSAL LEAVES THE LINK'S OWN NAME IN PLACE**, so a corrupt or foreign
; .LNK produces the ordinary "it is not a program" failure a moment later,
; with the file the user actually double-clicked named in the window. The
; alternative - a half-applied link - is a window naming a program the user
; never chose.
; -----------------------------------------------------------------------------
WIN_CODE_END
dos_lnk_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, dos_name
    call dos_is_lnk
    jc .out
    mov dx, [dos_dir]               ; ...stand where the link is, exactly as
    mov bl, [dos_vol]               ; dos_run does before loading a program
    call dos_be_goto
    jc .out
    push ds
    pop es
    mov si, dos_name                ; ...and read it whole, in one go
    mov bx, dos_lbuf
    xor dx, dx                      ; DX:CX is the capacity, and a link that
    mov cx, LNK_MAX                 ; needs more than 512 is not one of ours
    call dos_be_read
    jc .out
    mov cx, ax                      ; AX = bytes delivered
    call dos_lnk_parse              ; ...which rewrites dos_name on success
    jc .out
%ifdef DOS_WIN16
    mov byte [dos_w16k], 0          ; the loader read the LINK, not what it
%endif                              ; names: the wake asks again (124.3.1)
    WIN_ONLY
    call dos_fld_reload             ; the fields show what the link carried
    WIN_END
.out:
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)

; --- dos_is_lnk - does the NUL 8.3 name at SI end in .LNK? ------------------
; out: CF=0 yes. Case-insensitive: a FAT name is upper case and this one came
; off a disk, but a name staged by something else need not have.
dos_is_lnk:
    push ax
    push si
.f:
    cmp byte [si], 0
    je .no
    cmp byte [si], '.'
    je .dot
    inc si
    jmp short .f
.dot:
    mov al, [si+1]
    call dos_upc
    cmp al, 'L'
    jne .no
    mov al, [si+2]
    call dos_upc
    cmp al, 'N'
    jne .no
    mov al, [si+3]
    call dos_upc
    cmp al, 'K'
    jne .no
    cmp byte [si+4], 0
    jne .no
    pop si
    pop ax
    clc
    ret
.no:
    pop si
    pop ax
    stc
    ret
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_upc:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret
%endif                              ; DOS_EXTCORE
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; --- dos_fld_reload - the fields show what is in the buffers now ------------
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dos_fld_reload:
    push bx
    push cx
    push si
    mov si, dos_pln                 ; ...the path box too, since a shortcut
    call os88line_resync            ; names a program (SPEC.md 96.32.3)
    mov si, dos_ln                  ; RESYNC and not SET: a shortcut writes
    call os88line_resync            ; dos_args and dos_ebuf DIRECTLY, and those
    mov si, dos_eln                 ; ARE these fields' own LN_BUFs - so there
    mov cx, DOS_ENVN                ; is nothing to copy from, and `set` copied
.e:                                 ; from a DI nobody had loaded, straight
    push cx                         ; over the arguments it was called to show
    call os88line_resync
    pop cx
    add si, DOS_LNSZ
    loop .e
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_lnk_parse - dos_lbuf holds CX bytes of a .LNK; take it apart
; out: CF=0 and dos_name/dos_args/dos_ebuf filled; CF=1 = not one of OURS
;
; **IT REFUSES A FOREIGN LINK BY NAME AND THAT IS DELIBERATE** (SPEC.md
; 96.21.1). A Windows-authored shortcut leads with a LinkTargetIDList - an
; arbitrary shell ID list - and a LinkInfo carrying volume IDs, and parsing
; those from hostile floppy input is real work for no benefit: a modern
; Windows cannot run a DOS program anyway, so what the format buys here is
; that it is RECOGNISED, not that it round-trips. The flags word says which
; it is in one compare.
;
; EVERY LENGTH IS CHECKED AGAINST WHAT IS LEFT, not against the buffer: a
; count that runs past the end of a SHORT file would otherwise read whatever
; follows it in our own image (SPEC.md 20.8 rule 2).
; -----------------------------------------------------------------------------
WIN_CODE_END
dos_lnk_parse:
    push ax
    push bx
    push dx
    push si
    push di
    mov [dos_lend], cx
    cmp cx, LNK_HDR + 6
    jb .no                          ; too short to be a link at all
    cmp word [dos_lbuf], LNK_HDR    ; HeaderSize, the format's own magic
    jne .no
    cmp word [dos_lbuf+2], 0
    jne .no
    cmp byte [dos_lbuf+4], 0x01     ; ...and the CLSID's first two bytes, which
    jne .no                         ; is as much of a 16-byte constant as is
    cmp byte [dos_lbuf+5], 0x14     ; worth comparing to refuse a wrong file
    jne .no
%ifdef DOS_WTAIL
    WIN_ONLY                        ; (the box is the tail's: a headless host
    xor al, al                      ; already knows, the loader having read it)
    cmp word [dos_lbuf+LNK_SHOW], LNK_SW_HEAD   ; **ShowCommand = 7 IS CLOSE
    jne .show                       ; WHEN DONE** (SPEC.md 96.44.20): the
    inc ax                          ; header's own field, which the loader
.show:                              ; reads too - SW_SHOWMINNOACTIVE, no window
    mov [dos_mhead + OS88UI_CK_ON], al
    WIN_END
%endif
    mov ax, [dos_lbuf+20]           ; LinkFlags
    test ax, 0x03                   ; HasLinkTargetIDList | HasLinkInfo
    jnz .no                         ; ...a Windows-authored one. Refused.
    mov bx, ax
    mov si, LNK_HDR                 ; SI walks the buffer as an OFFSET, so one
                                    ; bound test serves every field
    mov byte [dos_pbuf], 0
    test bx, LNK_F_WDIR
    jz .norel
    push di                         ; WORKING_DIR -> dos_pbuf, and it is USED
    push dx                         ; (dos_lnk_cd below). A shortcut whose
    mov di, dos_pbuf                ; whole point is sitting where the user put
    mov dx, DOS_PBUF                ; it cannot resolve its program relative to
    call dos_lnk_takeb              ; ITSELF
    pop dx
    pop di
    jc .no
.norel:
    test bx, LNK_F_RELP
    jz .noargs
    call dos_lnk_take               ; RELATIVE_PATH -> dos_sbuf
    jc .no
    call dos_lnk_name               ; ...its last component -> dos_name
    jc .no
.noargs:
    test bx, LNK_F_ARGS
    jz .extra
    mov di, dos_args
    mov dx, DOS_ARGSZ
    call dos_lnk_takeb              ; COMMAND_LINE_ARGUMENTS -> dos_args
    jc .no
.extra:
    call dos_lnk_ext                ; ...and our own block, if it is there
    call dos_lnk_cd                 ; ...and stand where the link says, which
                                    ; makes [dos_dir] the TARGET's folder
                                    ; rather than the link's
    clc
    jmp short .out
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_lnk_cd - walk to the link's WORKING_DIR and make it [dos_dir]
;
; DOWN FROM THE VOLUME ROOT, one component at a time - the only direction a
; package can walk (SPEC.md 19.2.4: dsk_find drops the dot links, so there is
; no up). OSAPI_FILE_GOTO_QM is the move and it is A WORD inside the volume we
; are already on, so the walk costs its directory reads and no mounts at all.
;
; A REFUSAL IS NOT FATAL: [dos_dir] keeps the link's own folder, which is
; where a shortcut saved beside its program resolves anyway. What the user
; then sees is the ordinary "it could not be read", naming the program.
; -----------------------------------------------------------------------------
dos_lnk_cd:
    push ax
    push bx
    push dx
    push si
    cmp byte [dos_pbuf], 0
    je .out                         ; no working directory in the link

    ; --- WHERE IT SAYS, AND THEN WHERE IT IS (SPEC.md 96.21.2.1) -----------
    ; BH is the drive the .LNK ITSELF was read from - `dos_lnk_open`'s own
    ; input - and it is BOTH the second try and the ONLY try for a link
    ; written before the drive was recorded, whose path begins at `\`.
    mov bh, [dos_vol]
    mov bl, bh
    cmp byte [dos_pbuf+1], ':'
    jne .try
    mov al, [dos_pbuf]
    call dos_upc
    sub al, 'A'
    cmp al, DVOL_MAX
    jae .strip                      ; a letter no volume here can have: the
    mov bl, al                      ; fallback is the only answer left
.strip:
    ; **THE DRIVE COMES OFF THE FRONT, AND THE PATH MOVES DOWN TWO.** The walk
    ; is `dos_walk_pbuf`, which is a PUBLISHED core entry (SPEC.md 96.44.5) and
    ; takes no pointer - it reads `dos_pbuf` itself - so the buffer is the
    ; argument and the prefix has to leave it. `dos_walk_at` is the one that
    ; takes SI, and it is not on `doscents.inc`'s list; appending it there to
    ; save six bytes would grow the core ABI for ever (its own rule is APPEND,
    ; never insert) when the same six bytes here answer it once. The link
    ; written before 96.21.2.1 carries no prefix, so it never reaches this and
    ; both shapes walk identical code.
    push di
    mov si, dos_pbuf + 2
    mov di, dos_pbuf
.sh:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .sh
    pop di
.try:
    call dos_lnk_walk
    jnc .got
    cmp bl, bh
    je .out                         ; that WAS the fallback - one try, spent
    mov bl, bh                      ; ...and again on the drive it is ON, which
    call dos_lnk_walk               ; is what makes a disk moved between drives
    jc .out                         ; keep working
.got:
    mov [dos_dir], dx               ; A REFUSAL IS NOT FATAL: [dos_dir] and
    mov [dos_vol], bl               ; [dos_vol] keep the link's own folder, and
                                    ; the user sees the ordinary "it could not
                                    ; be read" naming the program. THE VOLUME
                                    ; GOES WITH THE FOLDER now: a qualified
                                    ; link may name another drive, where
                                    ; before this it could only ever mean the
                                    ; one it was sitting on
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; --- dos_lnk_walk - stand on volume BL and walk dos_pbuf from its ROOT ------
; out: CF=0 with DX = the folder's cluster; CF=1 = no such volume, or a
;      component is not there - and where the machine stands is then undefined,
;      which is why the caller's second try stands again rather than walking on
dos_lnk_walk:
    push ax
    push si
    mov dl, bl
    call dos_fh_stand               ; **THE MACHINE, NOT DOS** (SPEC.md
    jc .no                          ; 96.48.2), exactly as dos_path_take does
    call dos_walk_pbuf              ; ...and from the volume ROOT, which the
    pop si                          ; stand above has just made [dos_pvol]
    pop ax
    ret
.no:
    pop si
    pop ax
    stc
    ret
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_walk_pbuf - stand at the absolute path in dos_pbuf, from the volume ROOT
; out: CF=0 with DX = the cluster it ended on (0 = the root); CF=1 = some
;      component does not exist, and where the machine stands is then undefined
; clobbers: DX and the flags, nothing else
;
; DOWN AND ONLY DOWN, one component at a time, because that is the only
; direction a package has: dsk_find drops the on-disk dot links, so
; OSAPI_FILE_FIND never reports '..' and nothing outside the kernel can walk
; upward at all (SPEC.md 19.2.4). Every move is OSAPI_FILE_GOTO_QM, which
; inside the volume we are already on is a WORD and no I/O - so the walk costs
; its directory reads and no mounts.
;
; It is what makes '..' possible WITHOUT a descent stack: ask where we are,
; drop the last component, and re-descend to what is left. That has no depth
; limit, needs nothing remembered, and is right after a drive switch - which a
; recorded stack would not have been.
; -----------------------------------------------------------------------------
dos_walk_pbuf:
    mov si, dos_pbuf                ; the absolute form, from the volume root
    mov al, 1
    jmp short dos_walk_at
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
; dos_walk_at - ...and the general one, which SPEC.md 96.12.3 needs: AL=1 walks
; from the volume's root and AL=0 from WHERE WE ARE STANDING, so the folder
; part of `SUB\FILE.DAT` resolves without pretending it is absolute. SI is the
; path. Everything else is dos_walk_pbuf's, unchanged.
dos_walk_at:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    or al, al
    jz .comp                        ; relative: start from here
    xor dx, dx                      ; ...the volume root, first
    mov bl, [dos_pvol]              ; **THE VOLUME WE ARE STANDING ON, not the
                                    ; one the PROGRAM is on** (SPEC.md 96.48).
                                    ; `dos_fh_stand` has already put us on the
                                    ; drive the name named, and the walk is
                                    ; relative to that - where `[dos_vol]` is
                                    ; the drive the program would come back
                                    ; to. They were the same thing while
                                    ; `dos_fh_enter` switched the program too,
                                    ; and `A:\*.*` from a program on B: is
                                    ; what the difference looks like:
                                    ; `tests/dosdrv.py` caught it finding B:'s
                                    ; directory under A:'s name
    call dos_be_goto                ; the back end, for dos_lnk_find's reason
    jc .no
.comp:
    cmp byte [si], '\'
    jne .name
    inc si
    jmp short .comp
.name:
    cmp byte [si], 0
    je .here                        ; ...every component walked
    mov di, dos_cname
    mov cx, 12
.c:
    mov al, [si]
    or al, al
    jz .cend
    cmp al, '\'
    je .cend
    mov [di], al
    inc si
    inc di
    loop .c
.cend:
    mov byte [di], 0
    call dos_lnk_find               ; DX = its cluster
    jc .no
    mov bl, [dos_pvol]              ; ...and the same for every component
    call dos_be_goto                ; ...a WORD inside this volume
    jc .no
    jmp short .comp
.here:
    call dos_be_here                ; DX = where we ended up
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; --- dos_lnk_find - the folder called dos_cname here; DX = its cluster ------
dos_lnk_find:
    push ax
    push cx
    push si
    push di
    xor cx, cx
.l:
    mov di, dos_fbuf
    push ds
    pop es
    call dos_be_find                ; THE BACK END AND NOT THE SLOT (SPEC.md
    jc .no                          ; 96.4.1): inside the fsx bracket SS is the
                                    ; DOS program's, and a directory walk is
                                    ; exactly the multi-sector kernel call that
                                    ; may not run there. dos_cd_go's own
                                    ; component scan has always gone through
                                    ; dos_be_find, which is why AH=3Bh worked
                                    ; while this did not
    cmp word [dos_fbuf+14], OSAPI_FT_DIR
    jne .l
    mov si, dos_fbuf
    mov di, dos_cname
    call dos_ceq
    jc .l
    mov dx, [dos_fbuf+16]
    pop di
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop cx
    pop ax
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; --- dos_ceq - SI vs DI, NUL strings, case-insensitive. CF=0 = equal --------
; NOT dos_streq, which already exists here and answers in ZF against ES:DI -
; this one is DS-relative on both sides and folds case, because a FAT name is
; upper and a link's stored one need not be.
dos_ceq:
    push ax
    push si
    push di
.c:
    mov al, [si]
    call dos_upc
    mov ah, al
    mov al, [di]
    call dos_upc
    cmp al, ah
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .c
.yes:
    pop di
    pop si
    pop ax
    clc
    ret
.no:
    pop di
    pop si
    pop ax
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; --- dos_lnk_take - one StringData -> dos_sbuf, NUL-terminated --------------
dos_lnk_take:
    push di
    push dx
    mov di, dos_sbuf
    mov dx, 20
    call dos_lnk_takeb
    pop dx
    pop di
    ret

; --- dos_lnk_takeb - one StringData -> ES:DI (DS), DX = its capacity --------
dos_lnk_takeb:
    push ax
    push bx
    push cx
    push di
    mov ax, [dos_lend]
    sub ax, si
    cmp ax, 2
    jb .no
    mov cx, [dos_lbuf+si]
    inc si
    inc si
    mov bx, [dos_lend]
    sub bx, si
    cmp cx, bx
    ja .no
    mov bx, dx
    dec bx                          ; ...room for the NUL we add
    cmp cx, bx
    ja .no                          ; REFUSED, never truncated (SPEC.md 47)
    push si
.cp:
    jcxz .done
    mov al, [dos_lbuf+si]
    mov [di], al
    inc si
    inc di
    dec cx
    jmp short .cp
.done:
    mov byte [di], 0
    pop ax                          ; the SI we pushed; SI is already advanced
    pop di
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop di
    pop cx
    pop bx
    pop ax
    stc
    ret

; --- dos_lnk_name - the last component of dos_sbuf -> dos_name --------------
dos_lnk_name:
    push ax
    push bx
    push si
    push di
    mov bx, dos_sbuf                ; find the last separator...
    mov si, bx
.f:
    mov al, [si]
    or al, al
    jz .at
    cmp al, '\'
    jne .n
    mov bx, si
    inc bx
.n:
    inc si
    jmp short .f
.at:
    mov si, bx
    mov di, dos_name
    mov cx, 13
.c:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jz .ok
    dec cx
    jnz .c
    mov byte [di-1], 0              ; a name longer than an 8.3 one is not one
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret
.ok:
    cmp byte [dos_name], 0
    je .bad
    pop di
    pop si
    pop bx
    pop ax
    clc
    ret
.bad:
    pop di
    pop si
    pop bx
    pop ax
    stc
    ret

; --- dos_lnk_ext - find OUR ExtraData block and load the rows ---------------
; A block we do not recognise is STEPPED OVER by its own size, which is what
; the format is for. A missing block is not an error: a link written by
; anything else simply carries no environment.
dos_lnk_ext:
    push ax
    push bx
    push cx
    push di
.blk:
    mov ax, [dos_lend]
    sub ax, si
    cmp ax, 8
    jb .out                         ; no room for another block header
    mov ax, [dos_lbuf+si]           ; BlockSize, low word
    cmp ax, 4
    jb .out                         ; ...the terminal value
    mov bx, [dos_lend]
    sub bx, si
    cmp ax, bx
    ja .out                         ; a size past the end: stop, do not trust
    mov cx, [dos_lbuf+si+4]         ; the signature
    mov di, [dos_lbuf+si+6]
    cmp di, LNK_EXTSIG >> 16        ; both of ours share a high word
    jne .next
    cmp cx, LNK_EXTSIG & 0xFFFF
    jne .m
    call dos_lnk_rows
    jmp short .next                 ; ...AND KEEP WALKING: there are two of our
.m:                                 ; blocks now, and a link written by an
    cmp cx, LNK_EXTSIG2 & 0xFFFF    ; older build has only the first
    jne .next
    cmp ax, LNK_EXT2MIN
    jb .next                        ; short: not one of ours, whatever it says
    call dos_lnk_memr
.next:
    add si, ax
    jmp short .blk
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; --- dos_lnk_memr - the memory block at SI -> [dos_memkb] / [dos_keepc] -----
; The size was checked against LNK_EXT2SZ before the call and the block's own
; size was checked against what is LEFT of the file before that, so both reads
; are inside the buffer by construction (SPEC.md 20.8 rule 2).
;
; THE CAP IS CLAMPED and the choice is FORCED INTO RANGE, because this is a
; file somebody else may have written: a cap of 0xFFFF is harmless (dos_run
; takes the smaller of it and what the machine offers) but an arm of 0x7F
; would put OS88UI_RD_SEL on a row that does not exist, and os88ui_radhit only
; repaints the row that LOST the pick - so there would be no way back to a
; legal one (SPEC.md 96.25.2).
;
; **RANGE IS NOT ENOUGH**: DOS_MEM_WHOLE is legal and may still be a thing
; this machine cannot do, which is dos_mem_fix's half (96.36.1). The two are
; separate because they refuse for different reasons - one is a malformed
; file and the other is an honest file on the wrong machine.
dos_lnk_memr:
    push ax
    mov ax, [dos_lbuf+si+8]
    mov [dos_memkb], ax
    mov al, [dos_lbuf+si+10]
    cmp al, DOS_MEM_N
    jb .set
    mov al, DOS_MEM_IN              ; anything else means the default, which is
.set:                               ; the one a double click gets
    mov [dos_keepc], al
    mov al, [dos_lbuf+si+11]        ; ...and the cache dial, out of what was
                                    ; the pad byte (SPEC.md 96.36.6). A LINK
                                    ; WRITTEN BEFORE IT EXISTED READS ZERO,
                                    ; which is Auto - the setting such a link
                                    ; was saved with
    cmp al, DOS_CA_N                ; **CLAMPED**, against the one list both
    jb .cset                        ; arms now show (96.36.6): a byte off a
    xor al, al                      ; disk is hostile input, and a pick past
.cset:                              ; the end would draw a caption out of
    mov [dos_cache], al             ; whatever follows the table. Out of range
    mov byte [dos_cache+1], 0       ; is Auto, the setting a link that never
                                    ; carried one was saved with
    cmp word [dos_lbuf+si], LNK_EXT2SZ  ; ...and the boxes, WHEN THE BLOCK
    jb .nobox                       ; HAS THEM (SPEC.md 96.25.2.1). The size
    mov al, [dos_lbuf+si+12]        ; word was bounded against what is left of
    call dos_lnk_boxes              ; the file by dos_lnk_ext, so byte 12 is
.nobox:                             ; inside it; an older, 12-byte link keeps
                                    ; the defaults dos_fld_init set
    call dos_mem_fix
    WIN_ONLY
    call dos_mem_put                ; ...and the field shows what the link said
    WIN_END
    pop ax
    ret

; --- dos_lnk_boxes - byte 12 of the memory block, AL -> the three boxes -----
; One bit per box and each is a plain 0/1 into OS88UI_CK_ON, so any byte is
; legal and the unknown bits are ignored: nothing here can put a control in a
; state it cannot draw (SPEC.md 20.8 rule 2).
dos_lnk_boxes:
    push ax
    mov ah, al
    and al, LNK_BX_NOHDD            ; hard drives: set = UNTICKED
    xor al, LNK_BX_NOHDD
    mov [dos_mhdd + OS88UI_CK_ON], al
    mov al, ah
    and al, LNK_BX_NONET            ; network: set = UNTICKED
    xor al, LNK_BX_NONET
    shr al, 1
    mov [dos_mnet + OS88UI_CK_ON], al
    mov al, ah
    and al, LNK_BX_NOMOU            ; the mouse: set = TICKED
    shr al, 1
    shr al, 1
    mov [dos_mmou + OS88UI_CK_ON], al
%ifdef DVW
    mov al, ah                      ; Run in a window: set = TICKED
    and al, LNK_BX_WIN              ; (SPEC.md 96.32.2.4)
    jz .nowin
    mov al, 1
.nowin:
    mov [dos_mwin + OS88UI_CK_ON], al
%endif
%ifdef DOS_MEGA
    mov al, ah                      ; the HMA: set = UNTICKED (96.40.9.2)
    and al, LNK_BX_NOHMA
    xor al, LNK_BX_NOHMA
    shr al, 1
    shr al, 1
    shr al, 1
    mov [dos_mhma + OS88UI_CK_ON], al
    mov al, ah                      ; ...and the save box: set = TICKED
    and al, LNK_BX_SAVE             ; (96.40.9.3)
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    mov [dos_msav + OS88UI_CK_ON], al
%endif
    pop ax
    ret

; --- dos_lnk_rows - the NUL-separated set at SI+8 -> dos_ebuf ---------------
dos_lnk_rows:
    push ax
    push bx
    push cx
    push si
    push di
    add si, 8
    mov bx, dos_ebuf
    mov cx, DOS_ENVN
.row:
    cmp si, [dos_lend]
    jae .out
    cmp byte [dos_lbuf+si], 0
    je .out                         ; the bare NUL that ends the set
    mov di, bx
    mov ax, DOS_ENVW
.cp:
    cmp si, [dos_lend]
    jae .out
    push ax
    mov al, [dos_lbuf+si]
    mov [di], al
    inc si
    inc di
    or al, al
    pop ax
    jz .done
    dec ax
    jnz .cp
    mov byte [di], 0                ; a row longer than a row is cut HERE and
.done:                              ; nowhere else - it is our own file and
    add bx, DOS_ENVBUF              ; the field is what bounds it on the way in
    loop .row
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_has_eq - does the NUL string at SI carry an '='?
; out: CF=0 yes, CF=1 no; SI and every register preserved
; -----------------------------------------------------------------------------
dos_has_eq:
    push si
.c:
    cmp byte [si], 0
    je .no
    cmp byte [si], '='
    je .yes
    inc si
    jmp short .c
.yes:
    pop si
    clc
    ret
.no:
    pop si
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_psp_tail - the user's arguments into the new PSP's command tail
; in:  ES = the PSP's segment; out: nothing, every register preserved
;
; THE DOS FORMAT IS A LENGTH BYTE, THE TEXT, AND AN 0Dh (SPEC.md 96.19), and
; the length counts the text alone. A program that parses its own arguments
; finds the terminator; one that uses PSP:0080 as a counted string finds the
; count. Both are wrong about the other, so both are written.
;
; It is BOUNDED AT SOURCE rather than here: dos_args is 128 bytes and the line
; field's LN_MAX is 127 + the NUL, because the tail plus its count and its 0Dh
; have to live inside the PSP's 128. That is DOS's limit and not ours, which
; is why the field REFUSES the 128th character rather than this routine
; truncating a line the user can see (SPEC.md 47).
; -----------------------------------------------------------------------------
dos_psp_tail:
    push ax
    push cx
    push si
    push di
    mov si, dos_args
    xor cx, cx
.len:
    cmp byte [si], 0
    je .got
    inc si
    inc cx
    cmp cx, 126
    jb .len
.got:
    mov es:[0x80], cl               ; ...the count DOS puts there
    mov di, 0x81
    mov si, dos_args
    cld
    jcxz .term
    push cx
    rep movsb                       ; DS:SI is ours, ES:DI the PSP's
    pop cx
.term:
    mov byte [es:di], 0x0D
    pop di
    pop si
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_envpath - the program's own path, for the tail of the environment
; out: dos_pbuf = `\DIR\NAME.EXT`, NUL-terminated; every register preserved
;
; DOS 3+ puts the program's full path after the environment's terminating NUL
; and a count word, and a program looks there when it wants to know where it
; came from. This wrote a bare 8.3 name until SPEC.md 19.2.4 existed, because
; no package could name the folder it was launched from.
;
; A REFUSAL IS NOT FATAL HERE. If the slot cannot answer - a corrupt chain, a
; buffer too small - the name alone goes in, which is exactly what this wrote
; before and is better than nothing: a program that cannot find its own
; directory falls back on the current one, and every program has that path.
; -----------------------------------------------------------------------------
dos_envpath:
    push ax
    push cx
    push si
    push di
    ; --- THE DRIVE FIRST, BECAUSE DOS ALWAYS HAS ONE (SPEC.md 96.19.3.1) -----
    ; Measured against IBM DOS 3.30, which writes `B:\BIN\DOSARGS.COM` where
    ; this wrote `\BIN\DOSARGS.COM`. A program that reads its own path to find
    ; its FILES then has no drive to look on, and what it does with that is its
    ; own business - Prince of Persia falls back to A: and puts up "Please
    ; insert Prince of Persia Disk 1 into Drive A:" about a disk that is in B:
    ; and open.
    mov al, [dos_vol]
    add al, 'A'                     ; SPEC.md 96.6's map is the identity
    mov [dos_pbuf], al
    mov byte [dos_pbuf+1], ':'
    mov di, dos_pbuf + 2
    mov cx, DOS_PBUF - 2
    call dos_be_path                ; ES is the CALLER's DS here: an X cell
    jc .bare                        ; sets it (SPEC.md 19.2.4)
    mov di, dos_pbuf + 2
    add di, cx                      ; ...to the NUL it wrote
    cmp cx, 1
    jbe .name                       ; the root already ends in its separator
    mov byte [di], '\'
    inc di
.name:
    mov si, dos_name                ; ...and the program's own 8.3 name
.nm:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .nm
    jmp short .out
.bare:
    mov si, dos_name                ; no path: `X:NAME`, which is DRIVE-RELATIVE
    mov di, dos_pbuf + 2            ; and still a thing a program can resolve -
.bn:                                ; and it keeps the drive, which is the half
    lodsb                           ; that turned out to matter
    mov [di], al
    inc di
    or al, al
    jnz .bn
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; =============================================================================
; DATA
; =============================================================================
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dos_tpl:
    dw DOS_HERCX, MBAR_H, DOS_CONW, DOS_FRAMEH
                                    ; x, y, w, h (SPEC.md 96.32). On VGA and
                                    ; CGA wm_fit clamps x to 0, because a
                                    ; 640-wide frame has nowhere else to be,
                                    ; and that is THE WHOLE TRICK: the span
                                    ; test 11.95.3 applies is "starts at its
                                    ; display's first column and reaches its
                                    ; last", so the clamp is what buys the two
                                    ; border pixels back and puts the content
                                    ; origin on column 0. On Hercules the frame
                                    ; is DOS_HERCW and this x CENTRES it
                                    ; (SPEC.md 96.32.5). The WIDTH is
                                    ; per-adapter and dos_pref says so; this
                                    ; row is the VGA/CGA one
    dw dos_ttl, dos_paint, dos_key, 0   ; W_ONCLICK is installed by
                                    ; os88ui_btninit (SPEC.md
                                    ; 20.5.1.3.3), so the buttons see
                                    ; the press before dos_click does

    OS88_PREFER dos_pref, DOS_CONW, DOS_FRAMEH,  DOS_HERCW, DOS_FRAMEH,  DOS_CONW, 300
                                    ; **A REAL WIDTH AND A GENEROUS HEIGHT**,
                                    ; which is OSAPI_WM_PREFER's own advice
                                    ; (SPEC.md 11.100.1): the width is what
                                    ; matters and is almost never clamped.
                                    ; Hercules is NOT given its full 720 any
                                    ; more (SPEC.md 96.32.5): 80 cells and two
                                    ; borders, snug, where the spanning frame
                                    ; left 40 px of empty window each side of
                                    ; the band. The CGA row asks
                                    ; for 300 and is clamped to what the band
                                    ; plus the dock's strip allows - 17 rows
                                    ; instead of 25 - so this package never
                                    ; has to know the dock exists

    ; --- THE MENU (SPEC.md 12.2), which this package has never had ---------
    OS88_MENUSET dos_menus, dos_ttl, dos_oncmd
%ifdef DVW
        OS88_MENU dos_m_prog, dos_items_prog, 4
%else
        OS88_MENU dos_m_prog, dos_items_prog, 3
%endif
    OS88_MENUSET_END dos_menus

dos_m_prog: db 'Program', 0
dos_items_prog: dw dos_l_run, dos_l_tset, dos_l_full
%ifdef DVW
                dw dos_l_wrun
dos_l_wrun: db ' Run in Window', 0  ; SPEC.md 96.54.10: a MODE, its first
                                    ; byte the mark (dos_wlabel)
dos_l_wrun0: db MENU_DIS, 'Run in Window (see Setup)', 0
%endif

%ifndef DOS_WTAIL                   ; (DOS_WTAIL: beside dos_htpl, resident)
dos_ttl:    db 'DOS', 0
%endif
dos_l_args: db 'Arguments:', 0
dos_l_envb: db 'Environment:', 0    ; ...and the rows under it are NAME=VALUE,
                                    ; one to a line (SPEC.md 96.32.2.1)
dos_l_retb: db 'Return', 0          ; --- the setup area's furniture (96.32.2)
%ifdef DOS_SETPART
TOASTRUN dos_s_nosetp               ; the part's refusal (96.32.2.3), whose
            db 'Cannot open '       ; last word is the page's own name
%endif
dos_l_tset: db 'Setup', 0           ; ...and its name, which the title row
                                    ; shows and the bar's own button carries -
                                    ; one page, one word (SPEC.md 96.32.2.1)
%ifdef DOS_SETPART
TOASTEND
%endif
dos_l_run:  db 'Run', 0             ; --- and the top bar's (96.32.1)
dos_l_full: db 'Full Screen', 0     ; ...and the console's own (96.33.5)
dos_l_savb: db 'Save Shortcut', 0

; --- the two button GROUPS' label arrays (SPEC.md 20.5.1.3) ------------------
; One array per page, in the same order as the rects each page's group holds,
; because os88ui_btn indexes both with the one number. dos_place points the
; record at the pair that is up.
dos_bt_barl: dw dos_l_tset, dos_l_run       ; DOS_BT_ENV, DOS_BT_RUN
dos_bt_setl: dw dos_l_retb, dos_l_savb      ; DOS_BT_RET, DOS_BT_SAV
; --- the memory page (SPEC.md 96.25) -----------------------------------------
; The two figures are PATCHED IN PLACE by dos_mem_num and drawn as part of one
; opaque font_run, which is dos_fmt_exit's shape: a number assembled anywhere
; else needs a second store to reach the line, and a second pass over the
; pixels is what SPEC.md 6.1 exists to stop.
dos_l_meml: db 'Limit:', 0
dos_l_memk: db 'K', 0                ; ...and the UNIT, drawn hard against the
                                     ; field's right edge (SPEC.md 96.36.10.1).
                                     ; The field takes a bare number and `300`
                                     ; is three plausible quantities on this
                                     ; page alone - KB, paragraphs, or a
                                     ; percentage of the arena above it
dos_l_memc: db 'Disk cache:', 0
%ifdef DOS_MEGA
dos_l_mpark: db 'Parks the OS above 1MB', 0  ; the one arm kern_mega has
dos_l_mhma:  db 'Load DOS in the HMA', 0     ; ...and where kern_dos runs
dos_l_msav:  db 'Save the session to disk', 0    ; ...and HIBERNAT.IMG first
%endif
; --- THE ARENA, drawn as ONE opaque run with its digits inside it -----------
; dos_mem_num patches [dos_marn] in place and the line is drawn in a single
; font_run, which is dos_fmt_exit's shape: a number assembled anywhere else
; needs a second store to reach the line, and a second pass over the pixels is
; what SPEC.md 6.1 exists to stop.
;
; **`Estimated` IS THE WORD AND IT REPLACED A `~`** (SPEC.md 96.36.3): the row
; used to read `Memory for the program: ~NNNNN K`, which says the same thing
; in punctuation a reader has to interpret. It also said `for the program`,
; which invites the question *which program* - the answer being the one this
; DOS is about to run, under whatever is ticked below. One value, one name for
; it, and no word about arms: the user is picking options, not picking arms.
dos_l_marn: db 'Est DOS Ram (Current Machine):'
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
dos_marn:   db '     KB ', 0       ; ...and the cell after the KB is a `+`
                                     ; for an AUTOMATIC window's figure
                                     ; (SPEC.md 96.54.2.1), a blank otherwise
                                     ; - never a NUL, or a shorter opaque run
                                     ; would leave the last `+` standing. The
                                     ; label gave its trailing blank for it, so
                                     ; the run is no wider than it was: the
                                     ; five-wide field pads with blanks anyway
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
; --- the two arms, and the words under the greyed one (SPEC.md 96.36) -------
dos_mem_items:
    dw dos_l_memc1
    dw dos_l_memc2
dos_l_memc1: db 'Inside the OS', 0
dos_l_memc2: db 'Shut down the OS', 0
dos_l_memw3: db 'not in this build yet', 0
DOS_MEMI_N  equ 16                  ; the LONGEST arm's length, for the click
                                    ; rect. A literal because the labels are,
                                    ; and font_width would be a call per paint
                                    ; to re-derive a constant

; --- arm 0's two driver boxes (SPEC.md 96.36.7) -----------------------------
; **THE CAPTION AND THE ARENA TERM ARE TWO DIFFERENT NUMBERS**, which is not
; where this started: they were ONE figure, read once, on 47 rule 5's grounds
; that what a box says it gives back and what the arena moves by must not
; disagree. They still must not - and they do not, because they are answers to
; two different questions. The caption is the CLASS's ceiling (DRVCK_ALL, SPEC
; 51.12.1) and the arena term is what is mounted HERE, so a machine with no
; card reads `Network (Up to 39K)` beside a box that adds nothing when it is
; cleared. Fed one figure the caption read `(Up to  0K)` on exactly that
; machine - true, and indistinguishable from a page whose arithmetic died.
dos_l_mhdd: db 'Hard drives (Up to '
dos_mhddk:  db '  K)', 0             ; TWO digits and not three: these are the
dos_l_mnet: db 'Network (Up to '     ; CLASS ceilings now (SPEC.md 51.12.1), so
dos_mnetk:  db '  K)', 0             ; they are build-time constants and a
                                     ; three-wide field only pads them over.
                                     ; tests/unit/t_drvmem.py is what says a
                                     ; class total still fits two - it already
                                     ; re-derives every drv_memk term, and a
                                     ; third digit would be dropped in SILENCE
                                     ; by dos_mem_numn's `dec cx / jz .out`
%ifndef DOS_MEGA
DOS_MCKW    equ 23                  ; the longest of the two, in cells
%else
DOS_MCKW    equ 24                  ; ...and kern_mega's save box, a cell more
%endif
                                    ;
                                    ; **`Up to`, because the box is a REQUEST**
                                    ; (SPEC.md 96.36.7.1): it is never greyed
                                    ; now, so on a machine with no card it is
                                    ; ticked, live, and worth nothing - and
                                    ; those two facts have to be sayable at
                                    ; once. Saying it needs the CLASS's figure
                                    ; and not this machine's, which is what
                                    ; DRVCK_ALL is for (SPEC.md 51.12.1).
                                    ;
                                    ; TWO DIGITS. It was three, on the argument
                                    ; that the slot SUMS a class so a second
                                    ; driver could put the figure past 99 - and
                                    ; that argument was sound about a number
                                    ; nobody could bound. It is a BUILD-TIME
                                    ; ceiling now, so it CAN be bounded, and
                                    ; tests/unit/t_drvmem.py bounds it: the
                                    ; file that already re-derives every
                                    ; drv_memk term from the drivers' own
                                    ; sources asserts each class total fits
                                    ; two. A host-side check costs no bytes and
                                    ; fails the build the day it stops being
                                    ; true, which is strictly better than a
                                    ; blank column waiting for a driver that
                                    ; may never arrive

; --- ...and arm 1's one box (SPEC.md 96.36.8) -------------------------------
; **THE MOUSE IS NOT FREE ON A 4.77 MHz MACHINE**: `mou_isr` redraws the
; pointer out of the interrupt on every packet, and a DOS program that never
; asks INT 33h anything is paying for a cursor nobody reads. Off, DOS.O88
; hands `kern_dos` a zero KDL_MOUBASE, which kdmouse.inc already reads as
; "no mouse: nothing is hooked and INT 33h answers the still pointer".
dos_l_mmou: db 'Disable the mouse', 0
%ifdef DVW
dos_l_mwin: db 'Run in a window', 0 ; ...and the furniture row's (96.32.2.4)
%if $ - dos_l_mwin - 1 != DOS_MWINW
 %error "DOS_MWINW is the label's length in cells: it sizes the box's rect"
%endif
%ifdef DOS_WTAIL
dos_l_mhead: db 'Close when done', 0 ; ...and beside it (SPEC.md 96.44.20)
%if $ - dos_l_mhead - 1 != DOS_MWINW
 %error "Close when done is placed with DOS_MWINW: the two labels are one width"
%endif
%endif
%endif

; --- the disk cache's two lists (SPEC.md 96.36.6) ---------------------------
; ARM 1's is the whole ladder, because `kd_giveback` sheds a rung at a time
; and every width on it is a width the cache still works at. Arm 0's is two,
; because the kernel's claim is taken at a mount and is either standing or
; shed - see 96.36.6 for why the missing three are not a rounding.
dos_ca_items:
    dw dos_l_ca0
    dw dos_l_ca1
    dw dos_l_ca2
    dw dos_l_ca3
    dw dos_l_ca4
dos_l_ca0: db 'Auto', 0
dos_l_ca1: db '32K', 0
dos_l_ca2: db '18K', 0
dos_l_ca3: db '9K', 0
dos_l_ca4: db 'Off (SLOW!)', 0
; ...and what each row is worth in RUNS, which is what the launch block
; carries and what dos_cache_kb turns into the KB on the glass.
WIN_CODE_END
dos_ca_runs:
    db DOS_CA_AUTORUN               ; Auto - kern_dos's own KD_RAH_KEEP
    db 7                            ; 32K
    db 4                            ; 18K
    db 2                            ; 9K
    db 0                            ; Off
dos_lnk_root: db '\', 0

%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; --- the BDA's RESTORE list (SPEC.md 96.5): offset, bytes, 0xFFFF ends it ----
; Every span here is a field the KERNEL reads, or one whose stale value would
; point the ROM at memory we are about to free. Everything absent is absent on
; purpose, and the two that matter are 0040:003F (the floppy motor state
; dsk_fdd_probe calls the only place the current state exists) and 0040:006C
; (the tick spl_clock and SOUND.DRV's pm_ticks read) - restoring either would
; hand a live reader a statement that is not true of the machine.
dos_bdalist:
    dw 0x0000, 16                   ; COM and LPT port tables - NET.DRV finds
                                    ; the parallel port through the LPT half
    dw 0x0010, 2                    ; the equipment word the kernel WRITES to
                                    ; match the adapter it chose
    dw 0x0013, 2                    ; conventional memory KB - ours
    dw 0x001A, 4                    ; the keyboard buffer head and tail, which
                                    ; kbd_ovflow reads on every int 09h
    dw 0x0072, 2                    ; the soft-reset flag
    dw 0x0080, 4                    ; the keyboard buffer BOUNDS - a TSR that
                                    ; enlarges the buffer repoints these into
                                    ; memory we are about to free, and the ROM
                                    ; keeps writing keystrokes there
    dw 0x0098, 10                   ; the int 15h wait-flag pointer, count and
                                    ; flag - the same trap in another field
    dw 0xFFFF, 0
%endif                              ; DOS_EXTCORE
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
WIN_CODE                            ; the exit report's words are the tail's
                                    ; (dos_fmt_exit's note, DOS-HEADLESS 1.2)

; **THE SIX STATUS LINES ARE GONE, AND THAT IS SPEC.md 96.33 ARRIVING.** They
; stood in the console's band under a comment saying they would go when it
; came: an idle pair, `Starting...`/`Reading it...`, `The program has
; finished.` and `Could not run it.` A console is a LOG, so what happened is
; said once by dos_con_ended and stays said, where a sentence drawn on the band
; was true only until the next launch overwrote it - and `Starting...` was
; never readable at all, the bracket taking the screen in the same slice.
;
; `Exit code ` survives because dos_fmt_exit stamps the digits INTO it, and
; both readers want them: the console's line says `ended, exit code 002`.
dos_l2_ran:  db 'Exit code '
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
dos_exitd:   db '000', 0
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
; **THE REFUSAL SENTENCES ARE THE WINDOW'S** (SPEC.md 96.44.15). `dos_err_line`
; is the one reader of this table and its one caller is the console's
; `dos_con_ended`; the core only ever stores a DER_* into [dos_err], and
; `kern_dos` sends its DER_* HOME in KDH_CODE for this window to say. So the
; table, the sentences and the routine that picks one are window half, which
; takes them out of the core part and out of `kern_dos`'s image both.
dos_errs:
    dw dos_e_goto, dos_e_mem, dos_e_read, dos_e_big, dos_e_fsx
    dw dos_e_badexe                 ; DER_EXE: NOTHING SETS IT any more - the
                                    ; DOS has run an .EXE since SPEC.md 96.8 -
                                    ; and its sentence, `.EXE is not supported
                                    ; yet.`, was 27 bytes no path could reach.
                                    ; The SLOT stays, because DER_* is an index
    dw dos_e_badexe, dos_e_fit, dos_e_hand
%ifdef DOS_MEGA
    dw dos_e_reset                  ; DER_RESET: kern_mega's W7
%endif
%ifdef DVW_SPLIT
    dw dos_e_dvwp                   ; DER_DVWP (SPEC.md 96.54.9)
%endif
dos_e_goto:  db 'Its folder could not be opened.', 0
dos_e_mem:   db 'Not enough memory.', 0
dos_e_hand:  db 'A handover is already under way.', 0
dos_e_read:  db 'It could not be read.', 0
dos_e_big:   db 'Too large for one segment.', 0
dos_e_fsx:   db 'The screen is already in use.', 0
dos_e_badexe: db 'Its .EXE header is malformed.', 0
%ifdef DOS_MEGA
dos_e_reset: db 'The machine was restarted while it ran.', 0
%endif
dos_e_fit:   db 'Not enough RAM - try Setup, "Shut down the OS".', 0
%ifdef DVW
dos_e_wfit:  db 'Too big for a window', 0x27, 's 128K - raise Setup', 0x27, 's Memory limit.', 0
%ifdef DVW_SPLIT
dos_e_dvwp:  db 'Run in Window could not load its part of DOS.O88.', 0
%endif
%endif
                                    ; **IT SAID DOS 3.30'S OWN WORDS**,
                                    ; `Program too big to fit in memory.`,
                                    ; measured at COMMAND.COM offset 2436 -
                                    ; and matching COMMAND.COM was a nicety
                                    ; this sentence cannot afford (SPEC.md
                                    ; 96.8.1). It is not a DOS program's
                                    ; output, it is DOS.O88 refusing to start
                                    ; one, and DOS.O88 knows something DOS
                                    ; never did: there is another arm with
                                    ; ~600KB in it, one page away. A user told
                                    ; `too big to fit` closes the window; one
                                    ; told where the room is opens Setup
WIN_CODE_END
dos_s_blast: db 'BLASTER=A', 0      ; dos_blaster_set's, which is the window's
                                    ; too (SPEC.md 96.44.15)
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_s_epath: db 'PATH=', 0        ; **NEVER AN EMPTY SET** (SPEC.md
                                  ; 96.44.13.1): a real DOS always has a
                                  ; row, so a program that walks the set
                                  ; to reach its own path always can
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_mlen:    db 31,28,31,30,31,30,31,31,30,31,30,31
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_dowt:    db 0,3,2,5,0,3,5,1,4,6,2,4    ; Sakamoto's month table
%endif                              ; DOS_EXTCORE
%ifndef KD_BACKEND                  ; dos_fmt_fn's, and so the window's
WIN_CODE                            ; ...and the tail's (DOS-HEADLESS 1.2)
dos_e_fn:    db 'It asked for INT 21h AH='  ; (SPEC.md 96.44.15)
dos_fnd:     db '00h.', 0
WIN_CODE_END
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)


; =============================================================================
; INT 33h - THE MOUSE (SPEC.md 96.10)
; =============================================================================
; NOT A DRIVER. os8088's own mouse ISR runs for the whole bracket and keeps
; mouse_x/y/btn fresh (SPEC.md 53.1 - it never draws, because the gfx lock is
; held), so what a DOS program needs is the INT 33h SHAPE over numbers that
; are already being maintained. CuteMouse is a driver and we do not need one.
;
; THE VIRTUAL SCREEN IS 640x200 IN MICKEY UNITS, which is INT 33h's own
; convention: positions go in and out doubled horizontally in modes narrower
; than 640, and every caller expects a 0..639 x 0..199 range whatever the
; card is. os8088's pointer lives on the DESKTOP's geometry - 640x480 on VGA,
; 720x348 on Hercules - so the translation is a scale, and it is done with a
; multiply and a divide rather than a table because the desktop's size is a
; run-time fact (SPEC.md 39.2) and not one of three constants.
;
; FUNCTIONS 5 AND 6 NEED EDGES, and a handler that only runs when the program
; calls it can only see the transitions its polls straddle (SPEC.md 96.10.1).
; Answering "0 presses" would be honest and would also break the common case -
; a program whose whole click detection IS function 5 - so the counts are
; accumulated on EVERY state read, function 3's poll feeding them as much as
; function 5's own call does. A click shorter than the program's poll interval
; is lost and no shim can do better without an ISR of its own.
;
; WHAT IS NOT HERE, and is named rather than silently wrong: function 0Bh's
; MICKEY COUNTERS. mou_apply consumes the raw deltas into a screen-clamped
; position and keeps no accumulator, so a relative count can only be derived
; from position changes - which loses every mickey spent while the pointer is
; against an edge. Absolute programs (menus, CAD, paint packages) do not care;
; a mouselook does. docs/plans/DOS-EXEC-PLAN.md 9.1 prices the kernel-side fix
; at about ten resident bytes and leaves it as a decision rather than taking it.
; BX, CX AND DX ARE OUTPUTS AND ARE NOT SAVED, which is the whole difference
; between this prologue and INT 21h's a few hundred lines up. INT 33h answers
; in registers rather than in the caller's FLAGS, so a handler that restores
; them the way an ISR normally would returns the caller its own arguments back
; - "bx=65532" for a reset that set BX to 2, and every position exact in AX
; and garbage everywhere else. The paths that are not asked for them simply do
; not write them, which is what a real driver's "undefined" means.
dos_int33:
    sti
    push bp
    push ds
    push cs
    pop ds
    push si
    push di

%ifdef DOSTRACE
    ; --- THE HISTOGRAM (SPEC.md 96.10.3), and NOTHING when the trace is off.
    ; It is here and not in the ring because the ring's every host-side reader
    ; decodes an entry as an INT 21h call - and the question this answers
    ; wants no ordering and no arguments: WHICH mouse functions does the
    ; program ask for at all. A program that never calls 33h reads as 32
    ; zeroes, which is a finding; one that calls 0x0C and then waits reads as
    ; a single 1 in a bucket we answer `not supported` to, which is a
    ; different finding, and no amount of reading our own INT 21h trace
    ; separates the two.
    ;
    ; **THE BUCKETS ARE IN THE PART AND THE SEGMENT IS THE CORE'S.** This
    ; routine is core (96.44), so 96.44.2 rule 2 forbids it a host bss cell
    ; and rule 1 forbids a DBSS row that only one build emits - which between
    ; them leave exactly this shape. `[dos_m33seg]` is two unconditional core
    ; bytes; a host that does not set it counts nothing and stores nowhere.
    push si
    push es
    mov si, [dos_m33seg]
    or si, si
    jz .tr33out                     ; no part: the trace is silent and the
    mov es, si                      ; program runs, which is what OP_OPT means
    mov si, ax
    cmp si, DOS_TR33_N              ; anything above lands in the top bucket,
    jb .tr33in                      ; which is a reading and not a wild store
    mov si, DOS_TR33_N - 1
.tr33in:
    shl si, 1                       ; ...times DOS_TR33_SZ, which is 8 - three
    shl si, 1                       ; shifts of ONE, the only shift an 8086 has
    shl si, 1
    add si, DOS_TR33_OFF
    cmp byte [es:si], 0xFF          ; saturating: a poll loop must not wrap the
    je .tr33arg                     ; count round to zero and read as "never
    inc byte [es:si]                ; called"
.tr33arg:
    mov [es:si+2], bx               ; ...AND WHAT IT WAS ASKED (96.10.3.1) -
    mov [es:si+4], cx               ; the last call wins, which is right for an
    mov [es:si+6], dx               ; init sequence that calls each once
    mov si, [es:DOS_TR33_OFF + DOS_TR33_SEQ + DOS_TR33_SEQN]
    cmp si, DOS_TR33_SEQN           ; ...AND THE ORDER, until it is full
    jae .tr33out                    ; (96.10.3.2)
    mov [es:si+DOS_TR33_OFF+DOS_TR33_SEQ], al
    inc si
    mov [es:DOS_TR33_OFF + DOS_TR33_SEQ + DOS_TR33_SEQN], si
.tr33out:
    pop es
    pop si
%endif

    or ax, ax
    jz .reset
    cmp ax, 1
    je .show                       ; show/hide: A TEXT CURSOR, and only where
    cmp ax, 2                      ; the host has a text screen to draw on
    je .hide                       ; (SPEC.md 96.10.5) - in the windowed DOS
    cmp ax, 3                      ; DHK_TXT is absent and both are the no-op
    je .pos                        ; they were
    cmp ax, 4
    je .none0                       ; set position: warping the host pointer is
    cmp ax, 5                      ; the kernel's, and a program that borrowed
    je .press                      ; the screen has not borrowed the arrow
    cmp ax, 6
    je .release
    cmp ax, 7
    je .xwin                       ; set the X / Y WINDOW (SPEC.md 96.10.7).
    cmp ax, 8                      ; These WERE no-ops - "we clamp nothing,
    je .ywin                       ; the host's pointer is already inside the
                                   ; screen" - and the pointer being inside
                                   ; THE SCREEN is not the claim the program
                                   ; is making: it has just said what its own
                                   ; coordinate system is. They still LEAVE AX
                                   ; ALONE, which is 96.10.6 and is what
                                   ; Microsoft Works reads as "is there a
                                   ; mouse"
    cmp ax, 0x0A
    je .tcur
    cmp ax, 0x0B
    je .motion
    cmp ax, 0x0C
    je .setevt
    cmp ax, 0x14
    je .swpevt
.none0:                            ; THE EPILOGUE, TWICE (it is five bytes):
    pop di                         ; this copy is in short reach of the chain
    pop si                         ; and the handlers just below it, which
    pop ds                         ; each paid a near `jmp` - or a `jcc +3 /
    pop bp                         ; jmp near` - to reach the one at the foot
    iret
.pos:                              ; ...and the two READS live beside it, 03h
    call dos_mou_read              ; first - the poll a program makes every
    jmp short .none0               ; frame. BX = buttons, CX = x, DX = y
.motion:
    call dos_mou_delta             ; CX = dx, DX = dy since the last call -
    jmp short .none0               ; derived, see the header

.show:
    ; AX=0001h. **THE COUNTER IS NOT A FLAG** (SPEC.md 96.10.5): it starts at
    ; -1, `02h` takes a level and this releases one, so a program that hid
    ; twice must show twice. It saturates at 0 rather than climbing, which is
    ; what every driver does and what stops a show-happy program needing as
    ; many hides to put it away.
    cmp byte [dos_m33shw], 0
    jge .shown                     ; SIGNED: -1 is 0FFh, and `jae` here reads
    inc byte [dos_m33shw]          ; it as the largest byte there is
.shown:
    cmp word [dos_hkv + DHK_TXT], 0
    je .shpaint                    ; ...AND HOOK THE TICK, but only where
    call dos_m33_arm               ; there is something for it to move. `0Ch`
                                   ; is not the only way to need it - a
                                   ; program that shows the cursor and
                                   ; installs no handler still wants its
                                   ; pointer to move while it is busy - and in
                                   ; the WINDOWED DOS there is no cursor at
                                   ; all, so hooking IRQ0 there would buy
                                   ; nothing and cost the DOS task's slice a
                                   ; frame every tick. One-shot, so this and
                                   ; `0Ch` compose
.shpaint:
    call dos_m33_paint
    jmp short .none0
.hide:
    ; AX=0002h, and it has NO FLOOR on purpose - a program that hides five
    ; times has asked for five shows, and clamping here would put the cursor
    ; back up in the middle of its drawing.
    dec byte [dos_m33shw]
    call dos_m33_wipe
    jmp short .none0
.tcur:
    ; AX=000Ah: BX = 0 SOFTWARE (CX = the screen mask, DX = the cursor mask),
    ; BX = 1 HARDWARE - a CRTC scan-line pair, which is the machine's own text
    ; caret and not something a POINTER may take over. The hardware arm is
    ; ignored rather than refused, for `.none`'s reason: a program that asks
    ; for a shape and is refused still expects a cursor.
    or bx, bx
    jnz .none0
    pushf                          ; one critical section over all four, for
    cli                            ; `dos_m33_paint`'s reason one level up
    call dos_m33_wipe              ; off FIRST, with the masks it went on with
    mov [dos_m33sm], cx            ; - restoring under the new pair would put
    mov [dos_m33cm], dx            ; back a cell that was never there
    call dos_m33_paint
    popf
    jmp short .none0

.xwin:
    ; AX=0007h: CX and DX are the lowest and highest x the program will use.
    ; **BANKED AND PUT BACK**, because 96.10.6's rule outlives this: a
    ; function with no documented return value comes back with AX as it went
    ; in, and Microsoft Works reads AL as its "mouse present" flag.
    push ax
    mov ax, M33_VW
    mov di, dos_m33x0              ; ...and 07h and 08h are ONE body over the
    jmp short .win                 ; pair of words each sets: x0,x1 then y0,y1
.ywin:
    push ax                        ; AX=0008h, the same on the other axis
    mov ax, M33_VH
    mov di, dos_m33y0
.win:
    call dos_m33_win               ; (DI survives it, and the gate banks DI)
    mov [di], cx
    mov [di+2], dx
    pop ax
    jmp short .none0
.reset:
    call dos_m33_wall              ; a reset opens the window to the whole
                                   ; virtual screen (96.10.7), which is the
                                   ; state a driver powers up in
    call dos_m33_hidden            ; a reset puts the cursor away and takes the
                                   ; masks back to the pair a driver powers up
                                   ; with (SPEC.md 96.10.5)
    call dos_mou_zero              ; a reset clears the edge state with it
    call dos_m33_drop              ; ...and the EVENT HANDLER, which is what a
    mov ax, 0xFFFF                 ; real driver's reset does (SPEC.md 96.10.4)
    mov bx, 2                      ; a mouse IS installed - and it is, whatever
    jmp .out                       ; the machine has, because the kernel found
                                   ; one at boot or the pointer would not move
.press:
    mov di, dos_mou_pc             ; 05h and 06h are ONE body over a base: the
    jmp short .prel                ; counts, then x, then y, laid out the same
.release:                          ; way for both (the DBSS rows say so)
    mov di, dos_mou_rc
.prel:
    push di                        ; (DI is banked by the gate, so it is ours)
    mov si, bx                     ; the button asked about, banked before the
    call dos_mou_read              ; read overwrites BX with the live mask
    pop di
    mov ax, bx
    and si, 1                      ; two buttons, so anything else is button 1
    add si, di
    mov bl, [si]
    mov byte [si], 0               ; reading a count CONSUMES it, which is why
    xor bh, bh                     ; it is a count and not a flag
    mov cx, [di+2]                 ; ...and where that edge last landed
    mov dx, [di+4]
    jmp short .out
.setevt:
    ; AX=000Ch: ES:DX = the handler, CX = the events it wants (SPEC.md
    ; 96.10.4). It answers nothing, which is why the machinery below has to
    ; be right the first time: a program that installs one and is never
    ; called has been told a mouse exists and then never hears from it again,
    ; and cannot tell that from no mouse at all. Microsoft Works is that
    ; program - it calls 00h, 08h, 0Ah and this, and then NOTHING.
    ; **THE THREE STORES ARE ONE**, and `dos_int33` `sti`'d at its own first
    ; instruction, so the tick below can land between any two of them: an
    ; offset stored against the PREVIOUS segment is a far call into whatever
    ; is there. It is four bytes to make it impossible.
    pushf
    cli
    mov [dos_m33h], dx
    mov [dos_m33h+2], es
    mov [dos_m33m], cx
    popf
    jcxz .evdrop                   ; a mask of nothing IS the uninstall, and so
    cmp word [dos_m33h+2], 0       ; is a segment of zero: both are how a
    je .evdrop                     ; program takes its handler back before it
    call dos_m33_arm               ; frees the code under it
    jmp short .evout
.evdrop:
    call dos_m33_drop
.evout:
    xor ax, ax                     ; 0Ch returns nothing; AX is not a status
    jmp short .out
.swpevt:
    ; AX=0014h: the same, and it hands the OLD one back in ES:DX/CX. Fifteen
    ; bytes on top of 0Ch, and it is what a program that installs a handler
    ; around one operation uses to put the previous one back.
    push cx
    push dx
    push es
    pushf                          ; the OUT and the IN are one section, for
    cli                            ; .setevt's reason
    mov cx, [dos_m33m]
    mov dx, [dos_m33h]
    mov ax, [dos_m33h+2]
    mov [dos_m33om], cx
    mov [dos_m33oh], dx
    mov [dos_m33oh+2], ax
    pop es
    pop dx
    pop cx
    mov [dos_m33h], dx
    mov [dos_m33h+2], es
    mov [dos_m33m], cx
    popf
    jcxz .swdrop
    cmp word [dos_m33h+2], 0
    je .swdrop
    call dos_m33_arm
    jmp short .swout
.swdrop:
    call dos_m33_drop
.swout:
    mov cx, [dos_m33om]
    mov dx, [dos_m33oh]
    mov ax, [dos_m33oh+2]
    mov es, ax
    xor ax, ax
    jmp short .out
.none:
    ; **AX IS LEFT EXACTLY AS IT CAME IN, and that is SPEC.md 96.10.6.** This
    ; used to be `xor ax, ax` under a comment reading *"INT 33h's not
    ; supported"* - and INT 33h HAS NO SUCH CONVENTION. A function that
    ; documents no output leaves the registers alone, so a real driver comes
    ; back with AX still holding the function number, and zeroing it is not a
    ; polite refusal: it is an ANSWER, to a question the caller may be asking.
    ;
    ; MICROSOFT WORKS IS THAT CALLER and it cost this DOS the whole feature
    ; (docs/FIELD-NOTES.md 54). Its mouse init is
    ;
    ;       mov ax, 8 / int 33h        ; set the Y range - no return value
    ;       mov [98CAh], al            ; ...AND THAT IS THE `mouse present`
    ;                                  ; FLAG
    ;
    ; so our zero told it there was no mouse, at the END of an init sequence
    ; every call of which we had answered correctly. It then never asks for a
    ; cursor (01h) and never polls the position (03h) - while the event
    ; handler it installed at 0Ch keeps being called, so the buttons work and
    ; nothing is ever drawn. A defect that presents as HALF a working mouse.
.out:
    pop di
    pop si
    pop ds
    pop bp
    iret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_arm - hook IRQ0 so the event handler has something to be called from
; clobbers: nothing
;
; **THE TICK IS THE ONLY THING THAT FIRES** (SPEC.md 96.10.4.1). A real mouse
; driver dispatches from its OWN interrupt - the serial port's, or the aux
; port's - and this DOS has neither: the kernel owns both ISRs and keeps
; `mouse_x`/`mouse_y`/`mouse_btn` fresh for the whole bracket (SPEC.md 53.1),
; which is what makes function 3 exact and costs a translation instead of a
; driver. What it does not give is a moment of OUR code running, and a
; callback needs one.
;
; Everything else DOS gets control at is the program calling us, and a
; program that installed a handler is precisely the program that has stopped
; calling: Works installs one and never touches INT 33h again. `dos_getkey`
; is no better - that is INT 21h's key read, and an application with a mouse
; polls INT 16h itself.
;
; So: chain IRQ0. 18.2 Hz is coarser than a serial mouse's ~40 and it is what
; an 8088-era program gets from a tick; for a menu-driven application it is
; the difference between a mouse and none. Chaining the MOUSE IRQ would be
; better and is not free - the kernel knows which one it is and we would have
; to ask, and a PS/2 mouse is a different line again (SPEC.md 9.9).
;
; THE HOOK IS ONE-SHOT and the unhook is `dos_restore_machine`'s, which puts
; the WHOLE IVT back (96.5) - so a second 0Ch re-arms nothing and there is no
; way to leave a vector pointing into a bracket that has ended.
; -----------------------------------------------------------------------------
dos_m33_arm:
    cmp byte [dos_m33hk], 0
    jne .out
    push ax
    push es
    pushf                           ; pushf/cli/popf and never cli/sti - this
    cli                             ; is reached from `dos_int33`, whose own
    xor ax, ax                      ; caller's IF is not ours to set
    mov es, ax
    mov ax, [es:0x08*4]             ; whatever is there NOW, which on a machine
    mov [dos_m33old], ax            ; with the packet driver up is its own tick
    mov ax, [es:0x08*4+2]           ; (96.23.4) - two hooks compose, because
    mov [dos_m33old+2], ax          ; both chain what they found
%ifndef DOS_MEGA
    mov word [es:0x08*4], dos_m33_tick
    mov [es:0x08*4+2], cs
%else
    push dx
    mov ax, dos_m33_tick
    call dos_hvec                   ; a tick that finds A20 shut must not
    mov [es:0x08*4], ax             ; land in wrapped low memory
    mov [es:0x08*4+2], dx
    pop dx
%endif
    mov byte [dos_m33hk], 1
    popf
    pop es
    pop ax
.out:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_drop - forget the handler; the VECTOR stays hooked
; clobbers: nothing
;
; Unhooking would mean putting back a vector that a third party may have
; hooked since, which is the classic way to lose an interrupt chain. The
; dispatcher's first test is the handler's segment, so a dropped handler costs
; a compare and a jump per tick and nothing else.
; -----------------------------------------------------------------------------
dos_m33_drop:
    pushf
    cli
    mov word [dos_m33h+2], 0        ; the SEGMENT first: it is the dispatcher's
    mov word [dos_m33h], 0          ; own test, so a tick landing mid-drop sees
    mov word [dos_m33m], 0          ; a handler that is already gone
    popf
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_tick - IRQ0, chained, and the program's callback out of it
;
; **THE CHAIN GOES FIRST**, `dos_pkt_tick`'s rule (96.23.4.1) and for its
; reason: the tick reaches the kernel at the depth it always did.
;
; **IT DOES NOT GO THROUGH `dos_mou_read`.** That routine feeds
; `dos_mou_edge`, which is the press/release accumulator functions 5 and 6
; consume (96.10.1) - and this runs from an interrupt that can land in the
; middle of `dos_int33` doing exactly that, `dos_int33` having `sti`'d at its
; own first instruction. A dispatcher that shared the accumulator would eat a
; click the program was about to be told about, intermittently. So it asks
; the host hook directly and keeps its OWN last-seen state, which nothing
; else reads.
;
; **IT RUNS ON THE PROGRAM'S STACK, and that is the contract rather than a
; shortcut** (96.10.4.2): the callback is program code entered at interrupt
; time, which is what installing one means, and IRQ0 already lands on that
; stack every tick. A private stack would cost 256 bytes of the core's bss in
; every build, resident, to defend against a program whose stack cannot take
; an interrupt - and such a program is already broken on any machine.
; -----------------------------------------------------------------------------
dos_m33_tick:
    pushf                           ; the chain, exactly as an `int` would
    call far [cs:dos_m33old]        ; have entered it - and through CS, since
                                    ; DS is the interrupted program's
    push ax
    push ds
    push cs
    pop ds
    call dos_m33_paint              ; **THE COARSE UPDATE POINT** (SPEC.md
                                    ; 96.10.5.2), and it is BEFORE the handler
                                    ; test on purpose: a program that shows
                                    ; the cursor and then computes for a
                                    ; second has installed no handler and
                                    ; polls nothing, and the tick is the only
                                    ; thing left that can move its pointer
    call dos_m33_evt
    pop ds
    pop ax
    iret

; -----------------------------------------------------------------------------
; dos_m33_evt - the event handler's dispatch, without the tick around it
; in:  DS = ours; out: every register and the flags but IF preserved
;
; A PROC OF ITS OWN BECAUSE A WINDOW HAS A SECOND CALLER (SPEC.md 96.54.4):
; a program in its window that waits on a key spends its ticks ASLEEP with
; its world out, so IRQ0 lands on the desktop's vector and `dos_m33_tick`
; never runs - the windowed host calls this as its worker wakes instead. The
; host side is the same contract as the tick's: on the program's stack, with
; the program's world in.
; -----------------------------------------------------------------------------
dos_m33_evt:
    cmp word [dos_m33h+2], 0        ; no handler: two instructions a tick
    je .ret0
    cmp byte [dos_m33bsy], 0        ; a callback that ran long enough to be
    je .call                        ; interrupted by the next tick
.ret0:                              ; a `ret` HERE and not a jump to the one
    ret                             ; below: that is past a short jcc's reach,
.call:                              ; which on an 8086 is five bytes, not two
    mov byte [dos_m33bsy], 1

    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    xor bx, bx                      ; the LIVE pointer, from the host (96.44.3)
    xor cx, cx
    xor dx, dx
    cmp word [dos_hkv + DHK_MOUSE], 0
    je .nothing
    call word [dos_hkv + DHK_MOUSE] ; BX = buttons, CX = x, DX = y

    ; --- which of INT 33h's five conditions have happened since last time ---
    xor ax, ax
    cmp cx, [dos_m33lx]
    jne .moved
    cmp dx, [dos_m33ly]
    je .nomove
.moved:
    or al, 1
.nomove:
    mov ah, [dos_m33lb]             ; BL = the live mask, AH = the last one
    test bl, 1                      ; (BL and not a copy in SI: four bytes,
                                    ; which kept the core in CORE_MAX at the
                                    ; merge of SPEC.md 96.8.2 and 96.54.4)
    jz .lup
    test ah, 1
    jnz .rbtn
    or al, 2                        ; left DOWN
    jmp short .rbtn
.lup:
    test ah, 1
    jz .rbtn
    or al, 4                        ; left UP
.rbtn:
    test bl, 2
    jz .rup
    test ah, 2
    jnz .evdone
    or al, 8                        ; right DOWN
    jmp short .evdone
.rup:
    test ah, 2
    jz .evdone
    or al, 16                       ; right UP
.evdone:
    ; --- the mickeys, DERIVED from the position (96.10.2's last bullet) -----
    mov si, cx
    sub si, [dos_m33lx]
    mov di, dx
    sub di, [dos_m33ly]

    ; --- and the state is banked BEFORE the call, never after: the callback
    ; may take longer than a tick, and a second one that recomputed against
    ; the old position would report the same movement twice
    mov [dos_m33lx], cx
    mov [dos_m33ly], dx
    mov [dos_m33lb], bl

    xor ah, ah
    and ax, [dos_m33m]              ; only what the program asked for
    jz .nothing
%ifdef DOSTRACE
    ; --- AND DOS COUNTS ITS OWN CALLBACKS (SPEC.md 96.10.3.1), because
    ; "the program asked for events and then did nothing" has two readings and
    ; they are opposite defects: we never called it, or we called and it
    ; ignored us. Nothing in the INT 21h ring or the per-function histogram
    ; can tell them apart - a callback makes no INT 21h call and is not an
    ; INT 33h call either.
    push bx
    push es
    mov bx, [dos_m33seg]
    or bx, bx
    jz .cbdone
    mov es, bx
    inc word [es:DOS_TR33_OFF + DOS_TR33_CB]
.cbdone:
    pop es
    pop bx
%endif
    push ax                         ; THE POSITION IN THE PROGRAM'S WINDOW
    call dos_m33_fit                ; (SPEC.md 96.10.7), so the callback and
    pop ax                          ; 03h agree - after the banking and the
                                    ; mickeys, which stay in host units
    call far [dos_m33h]             ; AX = the events, BX = the buttons,
                                    ; CX/DX = where, SI/DI = the mickeys, and
                                    ; DS = OURS, which is the driver's own -
                                    ; a handler sets up its own from CS
.nothing:
    mov byte [dos_m33bsy], 0        ; free BEFORE the pops: a tick landing in
    jmp dos_popa_7                  ; them nests shallower than the callback
                                    ; above already did (size pass, 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_paint - put the mouse cursor where the pointer is, or take it off
; clobbers: nothing
;
; **THE WHOLE OF THE DRAWING RULE IS ONE LINE** (SPEC.md 96.10.5):
; `(cell AND screen_mask) XOR cursor_mask`, written into the text cell under
; the pointer. There is no bitmap, no sprite and no shape - a text-mode mouse
; cursor is an attribute the driver flips, which is why `0Ah` hands over two
; masks and nothing else.
;
; **IT IS `kern_dos`'s CAPABILITY AND NOT DOS.O88's**, and `DHK_TXT` is how
; that is spelled (96.10.5.1): in the windowed host the OS owns every pixel
; and B800 is the kernel's, so the absent hook refuses and nothing is drawn.
; The core is assembled once and both hosts read the same code.
;
; **A CURSOR THAT HAS NOT MOVED IS NOT REDRAWN.** That is not an optimisation:
; between two of our paints the program may have written the cell itself, and
; re-saving what is there would bank OUR OWN inverted cell as the thing to
; restore - after which the inversion is permanent and travels with the
; pointer. Returning early leaves the program's screen alone.
; -----------------------------------------------------------------------------
dos_m33_paint:
    ; **THE HOST TEST IS FIRST AND BEFORE ANY PUSH**, because in the WINDOWED
    ; DOS this routine is on a hot path and can never do anything: it is
    ; reached from `dos_mou_read`, which is every `dos_getkey` poll, and there
    ; is no text screen there to draw on and so nothing that could be left
    ; behind to wipe. Three instructions rather than thirty (96.10.5.1).
    cmp word [dos_hkv + DHK_TXT], 0
    je .none
    pushf                           ; **THE TICK PAINTS TOO, SO THIS IS A
    cli                             ; CRITICAL SECTION** (SPEC.md 96.10.5.2).
    push ax                         ; `dos_int33` `sti`s at its first
    push bx                         ; instruction, so IRQ0 can land between
    push cx                         ; the store that says WHERE the cursor is
    push dx                         ; and the one that says WHAT WAS UNDER IT
    push si                         ; - after which the wipe restores a cell
    push di                         ; from the wrong place and leaves a
    push es                         ; character the program never wrote.
                                    ; pushf/cli/popf and never cli/sti: this
                                    ; is reached from an ISR as well as from
                                    ; the program
    cmp byte [dos_m33shw], 0
    jl .hide                        ; hidden, so make sure nothing is left
                                    ; behind - a hide is a paint that wipes
    call dos_m33_where              ; CX = x, DX = y, and NOT through
    mov si, cx                      ; `dos_mou_read`: that feeds the edge
    mov di, dx                      ; accumulator functions 5 and 6 consume
    mov cl, 3                       ; (96.10.4.1), and this is called from the
    shr si, cl                      ; tick as well as from the program
    shr di, cl                      ; ...INT 33h's units are a 640x200 virtual
                                    ; screen whatever the text mode is, so a
                                    ; cell is 8 of them on BOTH axes
    call word [dos_hkv + DHK_TXT]   ; ES = the segment, BX = columns, DX = rows
    jc .hide
    cmp si, bx                      ; a pointer at the very edge rounds to a
    jb .colok                       ; cell that is one past the last one, and
    mov si, bx                      ; a wild store into the program's memory
    dec si                          ; is not a rounding error
.colok:
    cmp di, dx
    jb .rowok
    mov di, dx
    dec di
.rowok:
    mov ax, di
    mul bl                          ; AX = row * columns; AH is 0 coming in,
    add ax, si                      ; rows being at most 50
    shl ax, 1                       ; ...and a cell is two bytes
    mov si, ax
    mov ax, es
    cmp ax, [dos_m33dv]             ; ALREADY THERE? then touch nothing at all
    jne .move
    cmp si, [dos_m33do]
    je .out
.move:
    call dos_m33_wipe               ; off where it was, first
    mov [dos_m33dv], es
    mov [dos_m33do], si
    mov ax, [es:si]
    mov [dos_m33dc], ax             ; ...and what was under it
    and ax, [dos_m33sm]
    xor ax, [dos_m33cm]
    mov [es:si], ax
    jmp short .out
.hide:
    call dos_m33_wipe
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
.none:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_wipe - take the cursor off the glass, if it is on it
; clobbers: nothing
;
; **IT CHECKS BEFORE IT RESTORES** (SPEC.md 96.10.5.3). A software cursor
; cannot see the program's own writes - a DOS application draws its screen by
; storing into B800 and tells nobody - so the cell we saved may since have
; been replaced. Putting our copy back there would leave a character the
; program never wrote, at a place the pointer has left: the artefact the
; reporter describes under CTMOUSE as *"it doesn't always invert it correctly
; in works"*.
;
; The test is exact and costs eight bytes: recompute what we WROTE, and
; restore only if that is still what is there. A program that redrew the cell
; keeps its own content and we simply forget ours.
; -----------------------------------------------------------------------------
dos_m33_wipe:
    pushf                           ; nests correctly inside `dos_m33_paint`'s
    cli                             ; own: the `popf` puts back the IF that
    push ax                         ; was there, which there is 0
    push si
    push es
    mov ax, [dos_m33dv]
    or ax, ax
    jz .out                         ; nothing drawn, which is every call in the
                                    ; windowed DOS and most of them elsewhere
    mov es, ax
    mov si, [dos_m33do]
    mov ax, [dos_m33dc]
    and ax, [dos_m33sm]             ; what we PUT there, derived rather than
    xor ax, [dos_m33cm]             ; banked - the masks cannot change under
    cmp ax, [es:si]                 ; us without `0Ah`, which repaints
    jne .gone
    mov ax, [dos_m33dc]
    mov [es:si], ax
.gone:
    mov word [dos_m33dv], 0
.out:
    pop es
    pop si
    pop ax
    popf
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_hidden - the cursor's power-on state: away, and the default masks
; clobbers: nothing
;
; Called at the bracket's IVT install and again by function 0, which is what a
; real driver's reset does. `77FF`/`7700` is the pair every text-mode driver
; powers up with - AND 77FF keeps the character and drops blink and intensity,
; XOR 7700 then swaps foreground and background, which is the inverse-video
; block a DOS program's user recognises as the mouse.
;
; **THE COUNTER IS -1 AND NOT 0**, which is the whole reason this routine
; exists rather than a `DBSS` row being left at the zero bss arrives as: 0
; means VISIBLE, so a bracket that forgot this would put a cursor on the
; screen of a program that never asked for one.
; -----------------------------------------------------------------------------
dos_m33_hidden:
    pushf
    cli
    call dos_m33_wipe               ; with the masks it is CURRENTLY wearing,
    mov byte [dos_m33shw], 0xFF     ; before they go back to the defaults
    mov word [dos_m33sm], 0x77FF
    mov word [dos_m33cm], 0x7700
    popf
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_where - the pointer, WITHOUT the edge accounting
; out: BX = the button mask, CX = x, DX = y; CF undefined
;
; `dos_mou_read` is this plus `dos_mou_edge`, and the split exists because the
; two callers that must NOT accumulate are the ones that run behind the
; program's back: the IRQ0 dispatcher (96.10.4.1) and the cursor. An edge
; consumed here is a click functions 5 and 6 never report.
; -----------------------------------------------------------------------------
dos_m33_where:
    xor bx, bx
    xor cx, cx
    xor dx, dx
    cmp word [dos_hkv + DHK_MOUSE], 0
    je .out
    call word [dos_hkv + DHK_MOUSE]
.out:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_wall - open the window to the whole virtual screen (SPEC.md 96.10.7)
; clobbers: nothing, not even the flags
;
; The state a driver powers up in, and what `00h` puts back. Both hosts call it
; beside their `[dos_vw]`/`[dos_vh]` stores as well, because a program that
; never resets and goes straight to `03h` would otherwise read a window of
; `0..0` - which is not a smaller answer, it is zero for ever.
; -----------------------------------------------------------------------------
dos_m33_wall:
    mov word [dos_m33x0], 0
    mov word [dos_m33x1], M33_VW - 1
    mov word [dos_m33y0], 0
    mov word [dos_m33y1], M33_VH - 1
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_win - order one axis's pair and clamp it to the virtual screen
; in:  CX, DX = the two ends as the program gave them; AX = that axis's SIZE
;      on INT 33h's VIRTUAL screen (M33_VW / M33_VH, never [dos_vw]/[dos_vh])
; out: CX <= DX, both inside 0..AX-1
; clobbers: AX, flags
;
; A DRIVER SWAPS A REVERSED PAIR rather than refusing it, and the clamp is
; what lets `dos_m33_fit` divide by a constant without checking anything: a
; window inside the virtual screen can never be wider than it.
; -----------------------------------------------------------------------------
dos_m33_win:
    cmp cx, dx
    jbe .ord
    xchg cx, dx
.ord:
    dec ax                          ; the highest legal coordinate
    cmp cx, ax
    jbe .lo
    mov cx, ax
.lo:
    cmp dx, ax
    jbe .hi
    mov dx, ax
.hi:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_m33_fit - the host's pointer, in the PROGRAM's units (SPEC.md 96.10.7)
; in:  CX = x, DX = y, in the host's 640x200 virtual units
; out: CX, DX inside the window 07h and 08h set
; clobbers: AX, BX, flags
;
; **MAP, NOT CLAMP, AND THE REASON IS THE POINTER.** A real driver integrates
; mickeys and has no idea where the arrow really is, so clamping is the only
; thing it can do; this DOS is handed an ABSOLUTE position over the very glass
; the program is drawing on, so the useful answer is that same physical point
; in the program's own units. Clamping would pin Battle Chess's cursor at its
; right edge for the whole right half of the desk.
;
; THE FULL WINDOW IS THE IDENTITY and costs one compare per axis, which is
; every program that never calls 07h and every one that asks for the whole
; screen - so nothing that works today pays for a `mul`/`div` pair.
;
; The y is banked across the x map because `mul` writes DX, which is the y this
; routine has to answer - the same clobber the header above this one names.
; -----------------------------------------------------------------------------
dos_m33_fit:
    push bx
    push dx
    mov ax, [dos_m33x1]
    sub ax, [dos_m33x0]
    inc ax                          ; AX = the window's width
    cmp ax, M33_VW
    je .xdone
    mul cx                          ; DX:AX = the host's x times that width...
    mov bx, M33_VW
    div bx                          ; ...over the virtual screen's, so the
    add ax, [dos_m33x0]             ; quotient is under the width and fits AX
    mov cx, ax
.xdone:
    pop dx
    mov ax, [dos_m33y1]
    sub ax, [dos_m33y0]
    inc ax
    cmp ax, M33_VH
    je .ydone
    mul dx
    mov bx, M33_VH
    div bx
    add ax, [dos_m33y0]
    mov dx, ax
.ydone:
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mou_zero - forget the edge state (function 0)
; clobbers: nothing
; -----------------------------------------------------------------------------
dos_mou_zero:
    mov byte [dos_mou_lb], 0
    mov word [dos_mou_pc], 0       ; both counts are one word apiece in pairs,
    mov word [dos_mou_rc], 0       ; so two stores clear four bytes
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mou_read - the pointer, in INT 33h's 640x200 virtual units
; out: BX = the button mask (bit 0 left, bit 1 right), CX = x, DX = y
; clobbers: flags
;
; The multiply EATS DX, which is the y this routine has to answer, so y is
; banked across it - and the divide's remainder lands there too. Both are the
; kind of clobber that reads as a mouse that only works horizontally.
; -----------------------------------------------------------------------------
dos_mou_read:
    ; --- THE HOST OWNS THE POINTER (SPEC.md 96.44.3) ------------------------
    ; `DHK_MOUSE` fills BX/CX/DX with the button mask and the position in INT
    ; 33h units; a host that sets none has a mouse that never moves and is
    ; never pressed, which is a state a program can read (96.10) and exactly
    ; what `kern_dos` has today.
    ;
    ; It is reached on the KEY POLL - `dos_getkey` samples the mouse between
    ; `int 16h` checks so that "press a key or click" keeps its edges - so
    ; this is not a corner: it is every DOS program that waits for input.
    push ax
    call dos_m33_where              ; the host read, which `dos_m33_tick` and
                                    ; the cursor share (96.10.4.1)
    call dos_m33_fit                ; ...AND INTO THE PROGRAM'S OWN WINDOW
                                    ; (SPEC.md 96.10.7). It is here and not in
                                    ; `dos_m33_where` because the TEXT CURSOR
                                    ; is drawn from that one and is ours: its
                                    ; cell arithmetic is in the host's units
                                    ; whatever the program's are
    call dos_mou_edge               ; every state read feeds functions 5 and 6
    call dos_m33_paint              ; ...AND MOVES THE CURSOR. This is the fine
                                    ; update point and the tick is the coarse
                                    ; one: a program waiting on a key polls
                                    ; through here (96.10.5.2), which is most
                                    ; of its idle time and far better than
                                    ; 18.2 Hz
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mou_edge - accumulate the press/release counts 5 and 6 answer
; in:  BL = the live button mask, CX = x, DX = y (INT 33h units)
; out: nothing, every register preserved
;
; The press POSITION is latched once and shared between the two buttons rather
; than kept per button (SPEC.md 96.10.1): a caller that asks button 1 where
; button 0 went down is answered the wrong point, and a caller with one button
; in play - in practice, all of them - is answered exactly.
; -----------------------------------------------------------------------------
dos_mou_edge:
    push ax
    push bx
    mov al, [dos_mou_lb]
    mov [dos_mou_lb], bl
    xor al, bl                      ; AL = the bits that CHANGED since the last
    jz .out                         ; read, which is the whole of the history
    mov bh, al                      ; a polled shim can have
    and bh, bl                      ; ...of which these went DOWN
    jz .up
    mov [dos_mou_px], cx
    mov [dos_mou_py], dx
    test bh, 1
    jz .d1
    inc byte [dos_mou_pc]
.d1:
    test bh, 2
    jz .up
    inc byte [dos_mou_pc+1]
.up:
    not bl
    and al, bl                      ; ...and these went UP
    jz .out
    mov [dos_mou_rx], cx
    mov [dos_mou_ry], dx
    test al, 1
    jz .u1
    inc byte [dos_mou_rc]
.u1:
    test al, 2
    jz .out
    inc byte [dos_mou_rc+1]
.out:
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mou_delta - function 0Bh, derived from the position
; out: CX = dx, DX = dy since the last call
; clobbers: BX, flags
; -----------------------------------------------------------------------------
dos_mou_delta:
    push ax
    call dos_mou_read
    mov ax, cx
    sub cx, [dos_mou_lx]
    mov [dos_mou_lx], ax
    mov ax, dx
    sub dx, [dos_mou_ly]
    mov [dos_mou_ly], ax
    pop ax
    ret
%endif                              ; DOS_EXTCORE

; =============================================================================
; THE MCB CHAIN (SPEC.md 96.9)
; =============================================================================
; A real first-fit allocator over the blocks dos_build_psp laid out, because a
; stub is not enough for anything compiled: a C runtime's startup SHRINKS its
; own block with AH=4Ah and then asks for its heap with AH=48h, and a 48h that
; always refuses leaves malloc returning NULL for ever. SOPWITH 7.F15 does
; exactly that and then spins - one write to the console and no further DOS
; call at all, which is what an unchecked allocation failure looks like from
; outside.
;
; An MCB is 16 bytes at the paragraph BEFORE the block it describes:
;   +0  byte  'M' = another follows, 'Z' = the last one
;   +1  word  the owning PSP, 0 = free
;   +3  word  the block's size in paragraphs
;   +5  11    reserved, and DOS 4's 8-byte name
MCB_SIG     equ 0
MCB_OWN     equ 1
MCB_SZ      equ 3
MCB_M       equ 'M'
MCB_Z       equ 'Z'
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mcb_split - make BX paragraphs of the block at ES, freeing the rest
; in:  ES = the MCB, BX = the paragraphs to keep
; out: nothing; the block is shortened and a free MCB follows it
; clobbers: AX, CX, DX, flags
;
; Only splits when there is room for the new header AND at least one paragraph
; under it: a zero-length free block is a chain entry nothing can ever use and
; one more thing for every later walk to step over.
; -----------------------------------------------------------------------------
dos_mcb_split:
    push es
    push dx                         ; DX IS THE CALLER'S AND MUST SURVIVE. Not
    mov cx, [es:MCB_SZ]             ; tidiness: dos_mcb_alloc walks the chain in
    sub cx, bx                      ; DX and forms its ANSWER from it after
    jbe .out                        ; calling here, so a split that moved DX
    dec cx                          ; handed the program a segment one whole
    jz .out                         ; block high - inside the FREE remainder
                                    ; this call had just cut (SPEC.md 96.9.1)
    mov al, [es:MCB_SIG]            ; the tail inherits our end-of-chain flag
    mov dx, es
    mov [es:MCB_SZ], bx
    mov byte [es:MCB_SIG], MCB_M    ; ...and we are no longer the last
    add dx, bx
    inc dx                          ; the new header sits past our block
    mov es, dx
    mov [es:MCB_SIG], al
    mov word [es:MCB_OWN], 0        ; free
    mov [es:MCB_SZ], cx
.out:
    pop dx
    pop es
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mcb_join - swallow the RUN of free blocks above ES into it
; in:  ES = an MCB whose owner is 0
; out: its MCB_SZ and MCB_SIG updated; every register preserved
;
; **A FREE NEIGHBOUR IS A RUN AND NOT A BLOCK** (SPEC.md 96.9.2). Without this
; the arena's free space FRAGMENTS PERMANENTLY under the one pattern every
; memory manager uses - shrink, grow, shrink, grow - because each shrink cuts
; a new tail and each grow can absorb only the one immediately above it. The
; ceiling then RATCHETS DOWN: measured on Commander Keen 2 under kern_dos, the
; allocator granted 0x78C0 paragraphs (483 KB) and later refused 0x6900
; (420 KB), a SMALLER block than one it had already given, answering BX =
; 0x6180 - the previous high-water mark rather than the arena.
;
; It is DOS's own placement. Real DOS coalesces during the ALLOCATION WALK
; rather than in `AH=49h`, which is why `dos_mcb_free` still does not and why
; its note about that is only true with this here.
; -----------------------------------------------------------------------------
dos_mcb_join:
    push ax
    push bx
    push cx
    push dx
    push es
    mov dx, es                      ; DX = our own MCB's paragraph
.l:
    mov es, dx                      ; ...re-read each lap: we grow as we eat
    cmp byte [es:MCB_SIG], MCB_Z
    je .done                        ; we are the last: there is nothing above
    mov cx, [es:MCB_SZ]
    mov bx, dx
    add bx, cx
    inc bx                          ; the MCB above, header and all
    mov es, bx
    cmp byte [es:MCB_SIG], MCB_M
    je .sig
    cmp byte [es:MCB_SIG], MCB_Z
    jne .done                       ; a trampled chain: leave it exactly as
.sig:                               ; found, for dos_mcb_alloc's .broken arm
    cmp word [es:MCB_OWN], 0
    jne .done                       ; in use: this is where the run ends
    mov ax, [es:MCB_SZ]
    add ax, cx
    inc ax
    mov bl, [es:MCB_SIG]            ; its end-of-chain flag comes down with it
    mov es, dx
    mov [es:MCB_SZ], ax
    mov [es:MCB_SIG], bl
    jmp short .l
.done:
    pop es
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mcb_alloc - AH=48h
; in:  BX = paragraphs wanted
; out: CF=0 and AX = the block's segment; CF=1 with AX = 8 and BX = the
;      largest free block there is
; -----------------------------------------------------------------------------
dos_mcb_alloc:
    push cx
    push dx
    push si
    push es
    xor cx, cx                      ; CX = the largest seen, for the refusal
    mov dx, [dos_arena]             ; the chain starts at the arena's floor
.scan:
    mov es, dx
    cmp byte [es:MCB_SIG], MCB_M
    je .live
    cmp byte [es:MCB_SIG], MCB_Z
    jne .broken                     ; a chain a program has trampled: refuse
.live:                              ; rather than walk into the heap
    cmp word [es:MCB_OWN], 0
    jne .next
    call dos_mcb_join               ; ...and a free block is the whole RUN of
    mov si, [es:MCB_SZ]             ; them (SPEC.md 96.9.2)
    cmp si, cx
    jbe .notbig
    mov cx, si                      ; remember the largest free
.notbig:
    cmp si, bx
    jb .next
    call dos_mcb_split              ; it fits: keep BX and free the rest
    mov ax, [dos_arena]
    add ax, DOS_PSPP
    mov [es:MCB_OWN], ax            ; ...owned by the program's PSP
    mov ax, dx
    inc ax                          ; the block is the paragraph after its MCB
    pop es
    pop si
    pop dx
    pop cx
    clc
    ret
.next:
    cmp byte [es:MCB_SIG], MCB_Z
    je .nomem
    add dx, [es:MCB_SZ]
    inc dx
    jmp short .scan
.nomem:
    mov bx, cx                      ; the truthful largest, which is what a
    mov ax, 8                       ; BX=FFFFh probe is asking for
    jmp short .fail
.broken:
    xor bx, bx
    mov ax, 7                       ; "memory control blocks destroyed"
.fail:
    pop es
    pop si
    pop dx
    pop cx
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mcb_free - AH=49h
; in:  ES = a segment this allocator handed out
; out: CF=0 freed; CF=1 with AX = 9 (invalid block address)
;
; It marks the block free and does NOT coalesce. DOS does not coalesce here
; either - it does it on the next alloc's walk - and since SPEC.md 96.9.2 so
; do we, `dos_mcb_join` running at both the allocator's scan and the resize's
; grow. The second half of this note USED TO SAY that a program freeing two
; neighbours and asking for their sum was asking for something DOS would also
; refuse, and that was wrong in the way that matters: DOS grants it, we did
; not, and the gap is invisible until a program shrinks and grows.
; -----------------------------------------------------------------------------
dos_mcb_free:
    push dx
    push es
    mov dx, es
    dec dx                          ; the MCB is the paragraph before it
    mov es, dx
    cmp byte [es:MCB_SIG], MCB_M
    je .ok
    cmp byte [es:MCB_SIG], MCB_Z
    jne .bad
.ok:
    mov word [es:MCB_OWN], 0
    pop es
    pop dx
    clc
    ret
.bad:
    pop es
    pop dx
    mov ax, 9
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_mcb_resize - AH=4Ah
; in:  ES = the block, BX = the paragraphs wanted
; out: CF=0 resized; CF=1 with AX = 8 and BX = the most it could have, or
;      AX = 9 for a block that is not one of ours
;
; GROWING is refused unless the free block immediately above is big enough,
; which is DOS's own rule: a block only ever grows into its own neighbour.
; -----------------------------------------------------------------------------
dos_mcb_resize:
    push cx
    push dx
    push es
    mov dx, es
    dec dx
    mov es, dx
    cmp byte [es:MCB_SIG], MCB_M
    je .known
    cmp byte [es:MCB_SIG], MCB_Z
    jne .bad
.known:
    mov cx, [es:MCB_SZ]
    cmp bx, cx
    jbe .shrink
    ; --- grow: only into a FREE neighbour --------------------------------
    cmp byte [es:MCB_SIG], MCB_Z
    je .nofit                       ; nothing above us at all
    push es
    push dx
    add dx, cx
    inc dx
    mov es, dx                      ; the block above
    cmp word [es:MCB_OWN], 0
    jne .nofit2
    call dos_mcb_join               ; ...and every free block above THAT
    mov ax, [es:MCB_SZ]
    add ax, cx
    inc ax                          ; ...absorbed, header and all
    mov ch, [es:MCB_SIG]            ; ITS end-of-chain flag, banked in a
                                    ; register the two pops below do not
                                    ; touch. It was DL, one instruction in
                                    ; front of `pop dx` - so the byte written
                                    ; back was the LOW HALF OF THIS BLOCK'S
                                    ; OWN SEGMENT, and dos_mcb_split then
                                    ; handed that to the tail it cut. A chain
                                    ; ending in 1Ch instead of 'Z' is refused
                                    ; whole by dos_mcb_alloc's .broken arm,
                                    ; which answers BX=0: "0 KBytes is
                                    ; Available", with 319 KB free behind it
    pop dx
    pop es
    cmp bx, ax
    ja .nofitax
    mov [es:MCB_SZ], ax             ; take the whole neighbour, then give back
    mov [es:MCB_SIG], ch            ; what is not wanted
    call dos_mcb_split
    jmp short .done
.nofit2:
    pop dx
    pop es
.nofit:
    mov ax, cx
.nofitax:
    mov bx, ax                      ; the most it could have had
    pop es
    pop dx
    pop cx
    mov ax, 8
    stc
    ret
.shrink:
    call dos_mcb_split
.done:
    pop es
    pop dx
    pop cx
    clc
    ret
.bad:
    pop es
    pop dx
    pop cx
    mov ax, 9
    stc
    ret

; --- bss offsets, as a RUNNING TOTAL so the size cannot disagree with the -----
; fields (the Arkanoid %assign pattern). DOS_BSS_SIZE was written by hand once
; and was 6 bytes short of dos_bda's end, which the loader would have answered
; by zeroing less than we write - and a write past our bss is a write past our
; REGION, which is somebody else's heap claim.
%ifdef DOSTRACE
; =============================================================================
; THE TRACE BUFFERS ARE A PART (SPEC.md 96.29.1)
; =============================================================================
; **WHAT THIS BUYS IS THE CEILING, NOT THE ARENA**, and saying so is the whole
; of why it is worth doing. The two buffers are 35,092 bytes - the ring and
; the rendered dump - and in bss they were 80% of APP_MAX_SIZE, which is a
; HARD 60KB and cannot be raised at all: a package's offsets are 16 bits. The
; trace arm was measured at 61,437 of 61,440 - THREE BYTES - so §96.26's
; cable networking stopped the instrument assembling, and the only row that
; noticed was `dosdbg` quoting the assembler about a probe.
;
; As a part they are outside the image and outside that cap: image + bss goes
; 49,199 -> 23,379, which is the same 38% the SHIPPED build sits at. The ring
; can go back to 512 entries and the next feature in this file has room.
;
; It does NOT give the DOS program memory back, and the arithmetic is worth
; writing down rather than discovering: 35,092 bytes of bss become a 35KB
; CLAIM plus the parts standard's own 1,080 image bytes, so the arena is
; ~1,900 bytes WORSE. That is a fair price for 25,820 bytes under an absolute
; ceiling, in a build nothing ships - and the shipped build is BYTE-IDENTICAL,
; because every line of this is behind the %ifdef.
;
; OP_ZERO, so there is no disk in it at all: os88pkg.py writes a row that asks
; for KB and nothing else, and `make` puts not one extra byte on a floppy.
; OP_OPT, so a machine that cannot spare 35KB still RUNS the program - the
; trace simply refuses, which is what an instrument owes a machine it cannot
; fit on.
%define OP_BSS_AT dos_hbss       ; 96.44.2: the host's block, not the core's
%define OP_ONMOVE dos_trmove     ; 96.29.1.2: the part MOVES (20.12.12)
%include "os88parts.inc"
OS88_PARTS_BEGIN 1
  OS88_PART OP_ASSET, OP_ZERO | OP_OPT, DOS_TRACE_KB
OS88_PARTS_END

%endif
%endif                              ; DOS_EXTCORE

%ifdef DOSTRACE
%ifdef OP_ONMOVE
; (HOST code, so outside the core span above: it reads [dos_trseg], a host
; bss cell, which SPEC.md 96.44.2 forbids a core proc to name)
; dos_trmove - THE TRACE PART MOVED (SPEC.md 96.29.1.2, NO-PINNING 2). The
; standard's op_carvemv has already stored [op_base]; BX = was, DX = now,
; under a relocation proc's rules (66.3). Two words were banked from op_seg
; and nothing else names the part: [dos_trseg] (the host's, which
; tools/os88dosdbg.py reads afresh at every poll) and [dos_m33seg] (the
; core's copy, 96.10.3). Every routine that touches the part loads ES from
; one of them on entry - dos_trace, dos_tr_result, dos_int33's buckets and
; dos_trace_dump - and no program is ever handed a pointer into it: it is
; data, not code, and no vector points at it
dos_trmove:
    xor al, al
    call op_seg
    mov [dos_trseg], ax
    mov [dos_m33seg], ax
    ret
%endif
%endif

; -----------------------------------------------------------------------------
; kern_dos, AS A PART (docs/plans/KERN-DOS-PLAN.md §4.1)
; -----------------------------------------------------------------------------
; The whole of `kern_dos` - the DOS core over the kernel's disk layer - as one
; `OP_ASSET` row that `os88pkg.py` appends and compresses.
;
; **THE STANDARD'S CODE IS NOT EMITTED AND THAT IS THE POINT.** `op_load` reads
; a part into a claim, and this part is never read into one - the argument in
; docs/plans/KERN-DOS-PLAN.md §4.1.1 is that the handoff walks its bytes into
; EXTENTS while the file layer is
; still alive and the stub reads them with `int 13h`, because by then the heap
; has been given away (docs/plans/KERN-DOS-PLAN.md §4.1.1). So what this build
; needs out of the standard is the
; eighteen bytes of TABLE - `os88pkg.py` fills the offset and the length in -
; and `OS88_PARTS_END_TABLE` is the documented way to close one without the
; 800-byte body (apps/os88parts.inc, where the C SDK needs the same split).
;
; It is behind `-DDOSKPART` while wave 5 is unfinished: a shipped DOS.O88
; carrying it would cost the 360KB system disk 43 of its 53 free clusters for
; a feature that cannot yet be reached, and docs/plans/KERN-DOS-PLAN.md
; docs/plans/KERN-DOS-PLAN.md §4.1.3.1's four-piece shape is what it should
; cost instead.
%ifdef DOSKPART
; **THE TABLE IS THE LOADER'S NOW AND THE ROW IS IN OUR bss** (SPEC.md
; 96.44.4). This arm carried the part table itself until W9c, when the file's
; IMAGE became `apps/dos/dosload.asm` - which reads DOS.O88 out of part 0 and
; then ceases to exist, taking its table with it. So the three numbers the
; handoff wants arrive the way SPEC.md 20.12.10.2 says they should: the loader
; writes the row into the head of this bss before it re-homes, and the kernel
; does not zero a part.
;
; IT IS THE `OP_ROW` VERBATIM, so every read below is spelled as it was when
; the table was here - only the base moved. `os88parts.inc` is still included
; for `OP_R_OFF`/`OP_R_LEN`/`OP_R_ZKB`; with no `OS88_PARTS_BEGIN` it emits
; nothing at all, which is the property that lets the constants be shared
; without the 800-byte body.
%include "os88parts.inc"
dos_kdrow   equ dos_hbss + DOS_B_KDROW   ; the HOST's block (SPEC.md 96.44.5)
dos_corerow equ dos_hbss + DOS_B_COREROW
%ifdef DOS_WIN16
dos_doc     equ dos_hbss + DOS_B_DOC     ; the loader's hand-on (124.3.1)
dos_w16k    equ dos_hbss + DOS_B_W16K
%endif
%ifdef DOS_MEGA
dos_kdlrow  equ dos_hbss + DOS_B_KDLROW
%endif
dos_hlprow  equ dos_hbss + DOS_B_HLPROW  ; HELP's text (DOS-PARTS-PLAN 1.1)
%ifdef DOS_NETPART
dos_netrow  equ dos_hbss + DOS_B_NETROW  ; ...and the network part's (96.23.11)
%endif
%ifdef DOS_SETPART
dos_setrow  equ dos_hbss + DOS_B_SETROW  ; ...and the Setup page's (96.32.2.3)
%endif
%ifdef DVW_SPLIT
dos_dvwrow  equ dos_hbss + DOS_B_DVWROW  ; ...and the window's (96.54.9)
%endif
%ifndef KD_BACKEND
%include "os88pself.inc"            ; op_self, which reads it: the parts
                                    ; standard's OP_SELF read, opted into

; -----------------------------------------------------------------------------
; dos_pself - op_self, standing in DOS.O88's own folder and back again
; in:  SI = the row (the loader wrote the stream's FORMAT into its kind byte),
;      AH = the least KB the claim must be (0 = what the read needs)
; out: CF=0, DX = the claim, CX = the part's length; CF=1 = not had
; clobbers: AX, BX, SI, DI, BP, ES, flags
;
; Our file is in OUR folder and the prompt stands in its own, so the instance
; goes there and back (dos_pkgwhere banked it). HELP's (DOS-PARTS-PLAN 1.1)
; and the network part's (SPEC.md 96.23.11) - one copy of the walk.
; dos_pself0 is the same with AH = 0, HELP's and the Setup part's
; -----------------------------------------------------------------------------
dos_pself0:
    xor ah, ah                      ; no floor: the part and nothing else
dos_pself:
    push ax                         ; the floor, across the kernel's answer
    call OSAPI_FILE_HERE            ; where the prompt stands
    pop ax
    push bx
    push dx
    push ax
    mov dx, [dos_pkgdir]
    mov bl, [dos_pkgvol]
    call OSAPI_FILE_GOTO_QM         ; where DOS.O88 is (AX = its answer)
    pop ax
    jc .home
    mov di, dos_pkgname
    mov al, [si+OP_R_KIND]          ; the FORMAT: dsl_entry wrote it there
    call op_self                    ; DX = the claim, CX = its length
.home:
    mov bp, dx
    pop dx
    pop bx
    pushf
    call OSAPI_FILE_GOTO_QM         ; ...and back
    popf
    mov dx, bp
    ret

%ifdef DOS_SETPART
; =============================================================================
; THE SETUP PAGE'S HOST HALF (SPEC.md 96.32.2.3): the part is far code with
; OUR DS, read when Setup opens and freed when it closes. This is all of it
; that is resident - a bouncer, a fetch, a free and a door.
; =============================================================================
; dos_sp_bounce - the part's way back in: `call SPH(x)` stored x in
; [dos_sp_tgt] and far-called here. Nothing is touched on the way, so the
; routine's registers and FLAGS are the part's answer exactly.
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dos_sp_bounce:
    call [dos_sp_tgt]
    retf

; -----------------------------------------------------------------------------
; dos_sp_get - make sure the Setup part is held
; out: CF = 0 it is; CF = 1 it could not be had, and the menu bar says so.
;      Every register preserved
;
; **THE CLAIM STAYS PINNED** while it is held (SPEC.md 96.32.2.3): it is CODE,
; and its own return addresses are on the stack across every kernel call it
; makes - os88ui's save-under claim among them - so a block that moved would
; be returned into where it used to be.
; -----------------------------------------------------------------------------
dos_sp_get:
    cmp word [dos_spdoor+2], 0
    jne .have                       ; (CF = 0 out of the compare's jne)
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, DOS_PG_FLOOR            ; THE WALL IT IS, PRICED (SPEC.md
    call OSAPI_MEM_COMPACT          ; 96.32.2.3.1): the what-if before the
    push ax                         ; claim, against the one after it below
    mov si, dos_setrow
    call dos_pself0                 ; DX = the claim, CX = the part's length
    pop cx                          ; CX = the what-if with no part held
    jc .no
    mov ax, DOS_PG_FLOOR            ; ...and with it: a PINNED claim, so the
    call OSAPI_MEM_COMPACT          ; plan packs around it and whatever lies
    sub cx, ax                      ; under it is out of the arena. dos_run
    jae .wall                       ; drops it first, so the page owes the
    xor cx, cx                      ; program the difference back - which
.wall:                              ; dos_mem_arena adds to every reading
    mov [dos_spwall], cx
    mov [dos_spdoor+2], dx          ; the offset word is 0 for ever (bss), and
                                    ; the way back is the part's to write
                                    ; (sp_door: its offset is assembled in)
    jmp short .out
.no:
    push ds
    pop es
    mov si, dos_s_nosetp            ; `Cannot open Setup` (SPEC.md 47: a
    xor cx, cx                      ; refusal says so where the user is)
    call OSAPI_TOAST
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.have:
    ret

%ifdef DOS_WTAIL
%ifdef DOS_WIN16
; -----------------------------------------------------------------------------
; dos_hne_ask - is the program the Setup page is about a WINDOWS one? (SPEC.md
; 96.44.20). `Close when done` greys for an NE: the program is WIN16's and
; opens in WIN16's window (SPEC.md 124.3.1), so a DOS with no window says
; nothing about it - a headless host hands it on as well. Asked as the page
; opens, of the program DOS last named - the one a shortcut or a run set -
; and a box the link left ticked is cleared with it. The answer is KEPT
; against the locator it was asked of (`[dos_hnek]`, dos_pgdoc's shape), so
; flipping between the pages reads the header once and not every time: the
; question is a DFREE and a cluster or two on the disk. Preserves every
; register
; -----------------------------------------------------------------------------
dos_hne_ask:
    pushf
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov si, dos_name                ; the same program, folder and volume as
    mov di, dos_hnek                ; last time: the same answer
    mov cx, 13
    repe cmpsb
    jne .ask
    mov ax, [dos_dir]
    cmp ax, [dos_hnek + 13]
    jne .ask
    mov al, [dos_vol]
    cmp al, [dos_hnek + 15]
    je .known
.ask:
    mov si, dos_name                ; ...else ask, and keep what was asked
    mov di, dos_hnek
    mov cx, 13
    rep movsb
    mov ax, [dos_dir]
    stosw
    mov al, [dos_vol]
    stosb
    mov byte [dos_hne], 0
%ifdef DOS_WIN16                        ; no WIN16 to hand an NE to, no NE
    cmp byte [dos_name], 0
    je .known
    call dos_w16_isne
    jnc .known
    mov byte [dos_hne], 1
%endif
.known:
    cmp byte [dos_hne], 0
    je .r
    mov byte [dos_mhead + OS88UI_CK_ON], 0
.r:
    pop es
    pop di
    pop si
    pop cx
    pop ax
    popf
    ret
%endif                              ; DOS_WIN16: without WIN16 a program is
%endif                              ; never one it runs, so nothing greys
WIN_CODE_END

; dos_sp_drop - give the part back, if it is held. Every register preserved;
; the flags are not
dos_sp_drop:
    push dx
    xor dx, dx
    xchg dx, [dos_spdoor+2]
    or dx, dx
    jz .out
    call OSAPI_MEM_FREE
.out:
    pop dx
    ret

; dos_spdo - door AL of the Setup part (dosset.inc's sp_door), fetching it
; first if it is not held. out: CF = 1 it could not be had and nothing ran
WIN_CODE                            ; the window's (DOS-HEADLESS-PLAN 4.4)
dos_spdo:
    call dos_sp_get
    jc .r
    call far [dos_spdoor]
.r:
    ret
WIN_CODE_END
%endif                              ; DOS_SETPART
%endif
%endif

%include "dosnetabi.inc"             ; the cable translation's numbers, EARLY
                                    ; (SPEC.md 96.26) - its code is dosnet.inc
                                    ; at the end, and the bss table below
                                    ; cannot see an equ from there

%include "dospktabi.inc"             ; the packet driver's numbers and the
                                    ; network part's ABI (SPEC.md 96.23.11)
%if DOS_ENVN != PN_ENVN || DOS_ENVBUF != PN_ENVBUF
 %error "dospktabi.inc's PN_ENVN/PN_ENVBUF must match DOS_ENVN/DOS_ENVBUF: the network part walks the environment rows by them (SPEC.md 96.23.11)"
%endif

; **THE CHAIN STARTS PAST THE PARTS STANDARD'S OWN BSS** in the trace build
; (SPEC.md 96.29.1). os88parts.inc puts its OP_BSS bytes at OP_BSS_AT, which
; defaults to `os88_image_end` - exactly where this chain starts - so with
; `DB` at 0 the two OVERLAP, silently and completely. What it looked like:
; op_load ran perfectly (op_allkb 35, op_optok 1, op_base and dos_trseg both
; 0x2600) and NOTHING traced, because `op_name` and the first words of this
; table are the same bytes and each was writing over the other. The tell is
; that `op_name` read `&\0S.O88` - a package name with op_base's low word
; sitting on top of its first two characters.
;
; os88parts.inc's own usage note says this in as many words - "OUR words come
; first, yours follow them, and OS88_BSS is told the sum" - and it is one
; line, once, rather than a term on every symbol.
; **THE PARTS STANDARD'S OWN WORDS ARE THE HOST'S** (SPEC.md 96.44.2): only a
; host has parts, so its 86 bytes go at the head of the HOST's block and not
; the core's - which is `OP_BSS_AT`'s whole reason for being a `%define` the
; carrier may point (os88parts.inc's own note). Putting them in the core's
; block instead made `CORE_BSS_SIZE` a figure that depended on whether this
; was a DOSTRACE build, so the diagnostic build and the shipped one disagreed
; about where every core cell was.
; --- WHERE THE CORE'S CELLS LIVE (SPEC.md 96.44.5) --------------------------
; `DOS_CBASE` is the base every `DBSS` cell is measured from, and it is a
; CONSTANT in every build: the core is assembled once and joined to either
; host, so its state cannot sit at "wherever this host's image happens to
; end". It is directly above the core's own code, which `apps/dos/doscall.inc`
; fixes at `CORE_ORG + CORE_MAX`.
;
; `HBSS` cells keep `os88_image_end` - a host's own state belongs in a host's
; own bss, and the kernel zeroes that block on an ordinary launch.
%ifndef DOS_EXTCORE
  %ifndef DOS_CORE_ROOT
    %define DOS_CORE_INLINE 1       ; neither half of the split: the whole file
  %endif                            ; in one image, which is what ships today
%endif
%if 0
%elifdef DOS_EXTCORE
DOS_CBASE   equ CORE_BSS_AT         ; the core is a PART: its cells are at the
%elifdef DOS_CORE_ROOT              ; one address every host reserves
DOS_CBASE   equ CORE_BSS_AT
%else
; **THE CORE IS INLINE IN THIS BUILD**, which the shipped `build/dos.o88`
; still is - it carries no part table at all, so there is no core part to
; join and nothing reserves CORE_BSS_AT. Its cells go where they always went,
; in this package's own bss, and `dos_hbss` steps over them.
DOS_CBASE   equ os88_image_end
%endif

%assign DB 0
%ifdef DOSTRACE
%assign HB OP_BSS
%else
%assign HB 0
%endif
; **TWO ACCUMULATORS, AND THE SPLIT IS THE CORE'S ABI** (SPEC.md 96.44.2).
; DBSS is the CORE's state and HBSS the HOST's, because W8 gated twenty-nine
; rows out of `kern_dos` and every cell after them then sat at a different
; offset in the two builds - which a core assembled ONCE cannot survive. The
; core's cells are based at `os88_image_end` and are the same offsets in every
; host; the host's are based above them, at `dos_hbss`, and a host may have as
; many or as few as it likes.
;
; A ROW STAYS WHERE IT IS. Nothing is reordered: which macro a row uses is the
; whole of the split, so the table still reads in subject order.
%macro DBSS 2
    %1 equ DB
    %assign DB DB + %2
%endmacro
%macro HBSS 2
    %1 equ HB
    %assign HB HB + %2
%endmacro
; ...and a THIRD, for the window's tail (DOS-HEADLESS-PLAN 4.4): a row only
; the window reads, based past the tail's code at `dos_wbss` so that a host
; with no tail holds none of it - its cell is `DOS_WB + DOS_B_X`. With no
; tail it IS an HBSS row, in place, and DOS_WB is dos_hbss.
%assign WB 0
%macro WBSS 2
 %ifdef DOS_WTAIL
    %1 equ WB
    %assign WB WB + %2
 %else
    HBSS %1, %2
 %endif
%endmacro
%ifdef DOS_WTAIL
 %define DOS_WB dos_wbss           ; ...and the base a WBSS row's cell is at
%else
 %define DOS_WB dos_hbss
%endif

; --- AND THE VERY FIRST ROW IS THE LOADER'S (SPEC.md 96.44.4) ---------------
; `apps/dos/dosload.asm` writes the `kern_dos` part's table row into the head
; of this bss before it re-homes and disappears, and it does that WITHOUT a
; constant: the kernel's own header field says where a part's bss begins
; (`LD_H_IMG`), so the block has to be at offset ZERO of it. That is why this
; row is here rather than anywhere it would read more naturally.
;
; IT IS `HBSS` AND IT IS THE FIRST ONE, which is exactly what the loader
; needs: since 96.44.5 the core's cells are based at `DOS_CBASE` and the
; HOST's at `os88_image_end`, so the first `HBSS` row IS offset zero of the
; package's bss - the one address the loader can name without a constant. It
; was `DBSS` for one wave, when host cells sat past `CORE_BSS_SIZE`, and it
; cost `kern_dos` eight bytes it never read. It does not any more.
;
; IT IS AN `OP_ROW` AND NOT A STRUCT OF OUR OWN: DOS.O88 read
; `[dos_kdrow + OP_R_OFF]` when the part table was in its own image, so a
; verbatim copy leaves all four of those read sites spelled as they were and
; moves only the base.
    HBSS DOS_B_KDROW, 8          ; kind, flags, dw off, dw len, dw zkb
    HBSS DOS_B_COREROW, 8        ; ...and the CORE part's, beside it - the stub
                                 ; reads both (SPEC.md 96.44.5.4)
%ifdef DOS_MEGA
    HBSS DOS_B_KDLROW, 8         ; ...and kern_mega's LOW kern_dos, part 3,
                                 ; beside them (SPEC.md 96.40.9.2)
%endif
%ifdef DOS_WIN16                 ; ...and the DOCUMENT, after them (SPEC.md
    HBSS DOS_B_DOC, 16           ; 124.3.1): the loader asked OSAPI_ARG_FILE,
    HBSS DOS_B_W16K, 1           ; which is read-and-clear, so it is here or
%endif                           ; nowhere - and 1 = it was read and is no NE
%ifdef DOSKPART
    HBSS DOS_B_HLPROW, 8         ; ...and HELP's text's row, LAST of the
%endif                           ; loader's block (DOS-PARTS-PLAN 1.1): its
                                 ; kind byte carries the stream's format
%ifdef DOS_NETPART
    HBSS DOS_B_NETROW, 8         ; ...and the NETWORK part's after it (SPEC.md
%endif                           ; 96.23.11), the same shape
%ifdef DOS_SETPART
    HBSS DOS_B_SETROW, 8         ; ...and the SETUP page's after THAT (SPEC.md
%endif                           ; 96.32.2.3), the same shape again
; --- DOS_PART_DVW's row: the windowed layer (SPEC.md 96.54.9) --------------
%ifdef DVW_SPLIT
    HBSS DOS_B_DVWROW, 8         ; ...and the WINDOW's after that, the same
%endif                           ; shape again (dosload.asm's DSLH_DVWROW)
; --- end DOS_PART_DVW's row -------------------------------------------------
%ifdef DOS_WTAIL
    HBSS DOS_B_HEAD, 1           ; **NON-ZERO = HEADLESS** (DOS-HEADLESS-PLAN
%endif                           ; 4.4): the tail is not in this region, and
                                 ; every WIN_ONLY bracket tests this byte
    ; --- THE CONSOLE BAND (SPEC.md 96.32), four words dos_con_geom fills and
    ;     the console wave reads. Not banked "for one paint" like the line
    ;     above: they are the answer to a question about the WINDOW, so
    ;     anything that wants them asks for them again rather than trusting a
    ;     paint to have run.
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    WBSS DOS_B_CONX,  2          ; the band's left in SCREEN px, 8-aligned
    WBSS DOS_B_CONY,  2          ; ...its top, below the bar
    WBSS DOS_B_CONCOLS, 2        ; ...and its size in CELLS: 80, and the rows
    WBSS DOS_B_CONROWS, 2        ; are 25 or whatever the adapter allows
    ; --- THE TOP BAR (SPEC.md 96.32.1) --------------------------------------
    WBSS DOS_B_PLN,   DOS_LNSZ   ; the path box's os88line block...
    WBSS DOS_B_PATH,  DOS_PBUF   ; ...and its text, which is the only place
                                 ; this DOS has ever held a PATH rather than
                                 ; a volume, a cluster and an 8.3 name
    WBSS DOS_B_ERECT, 8          ; 'Environment' and 'Run', four words each
    WBSS DOS_B_RRECT, 8
    WBSS DOS_B_TVOL,  1          ; dos_path_take's scratch: the parsed volume
    WBSS DOS_B_TNAME, 13         ; and name, held apart until the walk has
                                 ; agreed, so a typo cannot half-commit
DOS_BTREC_SZ equ 18              ; **A MIRROR OF os88ui.inc's OS88UI_BT_SIZE**,
                                 ; and it has to be one: this block is laid
                                 ; out thousands of lines before os88ui.inc is
                                 ; included, so the real constant is not
                                 ; defined yet. The %if beside that include
                                 ; fails the BUILD if the two ever disagree,
                                 ; which is the only thing that makes a second
                                 ; copy of a number safe
    ; --- THE SETUP AREA'S FURNITURE (SPEC.md 96.32.2), four words each -------
    WBSS DOS_B_TRECT, 8          ; 'Return' and 'Save Shortcut', and they are
    WBSS DOS_B_SRECT, 8          ; ADJACENT ON PURPOSE (SPEC.md 20.5.1.3): a
                                 ; button group's rects must be contiguous
                                 ; because os88ui_btnpress walks them, so this
                                 ; pair is the Setup page's group and the pair
                                 ; above is the bar's. dos_srect used to sit
                                 ; nine declarations further down
    WBSS DOS_B_LNV,   2          ; the field's view and length as they were
    WBSS DOS_B_LNL,   2          ; before a keystroke (os88line_edit's inputs)
    WBSS DOS_B_LNC,   2          ; ...and its caret, which is the third of them
    WBSS DOS_B_NCELL, 2          ; glyph cells the edits have redrawn...
    WBSS DOS_B_NKEY,  2          ; ...over this many keystrokes
    WBSS DOS_B_PAGE,  1          ; which page is up (DOS_PAGE_*)
    WBSS DOS_B_BTREC, DOS_BTREC_SZ  ; the standard button record (20.5.1.4),
                                 ; REPOINTED per page by dos_place - one
                                 ; record, because only one page is ever up.
                                 ; DOS_B_SRECT is NOT here any more: it moved
                                 ; up beside DOS_B_TRECT, a button group's
                                 ; rects having to be contiguous
    WBSS DOS_B_ABON,  1          ; the About card is up (SPEC.md 96.51)
%endif
    DBSS DOS_B_ERP,   2          ; the environment row being emitted
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    HBSS DOS_B_LBUF,  LNK_MAX    ; a shortcut, read or written
    HBSS DOS_B_SBUF,  20         ; ...and `.\NAME.EXT` while one is built
%endif
    DBSS DOS_B_CNAME, 14         ; one component of a link's working directory
    DBSS DOS_B_FBUF,  24         ; ...and OSAPI_FIND_SZ while it is looked up
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    WBSS DOS_B_WNAME, 14         ; ...and the name a Save dialog chose, which
                                 ; may NOT share dos_sbuf: the builder uses it
    HBSS DOS_B_LEND,  2          ; how many bytes of dos_lbuf are real
%endif
    DBSS DOS_B_EBUF,  DOS_ENVN * DOS_ENVBUF   ; the environment rows...
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    WBSS DOS_B_ELN,   DOS_ENVN * DOS_LNSZ     ; ...and their field blocks
%endif
    DBSS DOS_B_ARGS,  DOS_ARGSZ  ; the user's arguments, NUL-terminated
    DBSS DOS_B_PBUF,  DOS_PBUF   ; ...and the program's own path, for the env
    WBSS DOS_B_LN,    DOS_LNSZ  ; the arguments field's block (os88line.inc)
    DBSS DOS_B_HKV,   DHK_NENT * 2  ; **THE HOST'S HOOKS** (96.44.3): zero is
                                 ; "this host does not want it", which is what
                                 ; a zeroed bss already says
    DBSS DOS_B_BEVEC, DBE_NENT * 2  ; **THE BACK END, AS ADDRESSES** (96.44.1):
                                 ; DBE_* indexes this and the HOST fills it,
                                 ; because the core is one object and there are
                                 ; two back ends behind it
    HBSS DOS_B_MEMKB, 2          ; SPEC.md 96.25: the arena cap in KB, 0 = as
                                 ; much as the machine will give
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    HBSS DOS_B_MRAD,  DOS_MRADSZ ; ...and the two-arm radio's own record
                                 ; (os88ui.inc), whose SEL word IS the setting
                                 ; - os88ui_radhit moves it and redraws the two
                                 ; rows that changed, so a copy here would be a
                                 ; second truth (SPEC.md 96.36)
    HBSS DOS_B_MDR,   DOS_MDRSZ  ; the disk cache's drop-down, the same way
                                 ; (96.36.6): its SEL word IS [dos_cache]
    HBSS DOS_B_MHDD,  DOS_MCKSZ  ; the two driver boxes (SPEC.md 96.36.7):
    HBSS DOS_B_MNET,  DOS_MCKSZ  ; Hard Drives and Network, arm 0's
    HBSS DOS_B_MMOU,  DOS_MCKSZ  ; ...and Disable the mouse, arm 1's (96.36.8)
%ifdef DVW
    HBSS DOS_B_MWIN,  DOS_MCKSZ  ; ...and Run in a window, the furniture
%endif                           ; row's (96.32.2.4)
%ifdef DOS_WTAIL
    WBSS DOS_B_MHEAD, DOS_MCKSZ  ; ...and Close when done, beside it (SPEC.md
%ifdef DOS_WIN16                 ; 96.44.20) - the TAIL's: only the Setup page
    WBSS DOS_B_HNE,   1          ; shows it and the .LNK reader's store is a
    WBSS DOS_B_HNEK,  16         ; window's; 1 = the target is an NE (WIN16's),
%endif                           ; asked of the locator in HNEK (name, folder,
%endif                           ; volume)
%ifdef DOS_SETPART
    HBSS DOS_B_SPDOOR, 4         ; the Setup part's door, 0:claim - its
                                 ; segment word 0 = not held (96.32.2.3)
    HBSS DOS_B_SPTGT, 2          ; ...and the host routine its bouncer calls
    HBSS DOS_B_SPWALL, 2         ; ...and the KB its claim walls off (96.32.2.3.1)
%endif
%ifdef DOS_MEGA
    HBSS DOS_B_MHMA,  DOS_MCKSZ  ; ...and Load DOS in the HMA (96.40.9.2)
    HBSS DOS_B_MSAV,  DOS_MCKSZ  ; ...and Save the session to disk (96.40.9.3)
%endif
    HBSS DOS_B_MSKB,  SYSKB_SIZE ; OSAPI_SYS_KB's buffer, for arm 1's own
                                 ; estimate (SPEC.md 96.36.3)
    HBSS DOS_B_MHKB,  2          ; word: what each class is holding, out of
    HBSS DOS_B_MNKB,  2          ; OSAPI_DRV_CLASSK - read once per place and
    HBSS DOS_B_MSNK,  2          ; ...and the SOUND driver's, which has no box
                                 ; of its own (96.36.7.2) and is a term in the
                                 ; figure whatever the user has ticked. NOT
                                 ; `MSKB` - that is dos_skbuf, OSAPI_SYS_KB's
                                 ; buffer, and the obvious abbreviation for
                                 ; this one collides with it
                                 ; used by the label AND by the arena figure,
                                 ; which must not disagree
    WBSS DOS_B_MBUF,  DOS_MEMBUF ; the limit field's text...
    WBSS DOS_B_MLN,   DOS_LNSZ   ; ...and its os88line block
    WBSS DOS_B_MX,    2          ; the memory page's content origin, banked
    WBSS DOS_B_MY,    2          ; for one paint
    WBSS DOS_B_MSX,   2          ; ...and A PRESS's own x and y, banked for one
    WBSS DOS_B_MSY,   2          ; click (SPEC.md 96.36.4). It used to ride in
                                 ; DI and BP, which worked while the only
                                 ; control on the block was os88ui_rad - every
                                 ; register preserved - and stopped working the
                                 ; moment there were five: os88ui_drpress zeroes
                                 ; DI on the way past and dos_mck_di ANSWERS in
                                 ; it. Four bytes of the image, and a register
                                 ; a library call cannot reach
%endif
    HBSS DOS_B_WIN,   2
    HBSS DOS_B_STATE, 1
    DBSS DOS_B_ERR,   1
    DBSS DOS_B_EXIT,  1
    DBSS DOS_B_BADFN, 1
    HBSS DOS_B_DIR,   2
    DBSS DOS_B_VOL,   1
    DBSS DOS_B_PAD,   1
    DBSS DOS_B_NAME,  16
    DBSS DOS_B_ARENA, 2
    DBSS DOS_B_APARA, 2
    HBSS DOS_B_AKB,   2
    DBSS DOS_B_IMGSZ, 2
    DBSS DOS_B_PRGSP, 2
    DBSS DOS_B_SVSS,  2
    DBSS DOS_B_SVSP,  2
    DBSS DOS_B_PIC1,  1
    DBSS DOS_B_PIC2,  1
    DBSS DOS_B_ISEXE, 1
%ifdef DOSKPART                 ; ...and nothing when arm 3 is not built in
    HBSS DOS_B_PKGNAME, 13      ; OUR file's name, banked at dos_entry
    HBSS DOS_B_PKGDIR,  2       ; ...and the folder we were launched from
    HBSS DOS_B_PKGVOL,  1       ; ...on that volume
    HBSS DOS_B_KDH,     KDH_SIZE ; the record osapi_dos_handoff keeps a far
    HBSS DOS_B_WOK,     1       ; 1 = the destructive arm has been confirmed
                                ; for THIS launch (SPEC.md 96.42)
%endif                          ; POINTER to (SPEC.md 96.40), so it is OURS
%ifdef DOSTRACE                 ; ...and NOTHING when it is off: the ring is
    HBSS DOS_B_TRACEN, 2        ; 514 bytes, and an instrument that costs the
    HBSS DOS_B_TRACEW, 2        ; shipped build anything is one that gets
                                ; ...and the RING is not here: it is in the
                                ; part (SPEC.md 96.29.1), with the rendered
                                ; dump beside it. 35,092 bytes that used to be
                                ; 80% of APP_MAX_SIZE
    HBSS DOS_B_TRACEI, 2                ; the entry a result belongs to, 0 =
                                        ; the call was filtered out
    HBSS DOS_B_TRNM,   DOS_TRNM_N * 13      ; the NAMES the program passed
    HBSS DOS_B_TRNMI,  1                    ; ...and how many, capped
    HBSS DOS_B_TRSEG,  2        ; THE PART'S SEGMENT, banked by dos_entry.
                                ; 0 = the part was refused (it is OP_OPT), and
                                ; dos_trace tests it: an instrument that
                                ; cannot fit writes NOTHING rather than
                                ; writing to segment zero. It is also what the
                                ; HOST reads - tools/os88dosdbg.py needs one
                                ; word to find the ring, where op_seg's
                                ; arithmetic would have to be reimplemented
                                ; outside the guest
%endif
    DBSS DOS_B_VW,    2
    DBSS DOS_B_VH,    2
    DBSS DOS_B_MLX,   2
    DBSS DOS_B_MLY,   2
    DBSS DOS_B_MLB,   1        ; the button mask the last state read saw
    DBSS DOS_B_MPC,   2        ; press counts, one BYTE per button
    DBSS DOS_B_MPX,   2        ; where the last press landed, shared
    DBSS DOS_B_MPY,   2        ; between the buttons (SPEC.md 96.10.1)
    DBSS DOS_B_MRC,   2        ; ...and release counts, and the same pair.
    DBSS DOS_B_MRX,   2        ; **COUNT, X, Y IN THAT ORDER FOR BOTH**: INT
    DBSS DOS_B_MRY,   2        ; 33h's 05h and 06h are one body over a base
    DBSS DOS_B_M33H,   4        ; the INT 33h EVENT HANDLER (SPEC.md 96.10.4),
                                ; segment 0 = none...
    DBSS DOS_B_M33M,   2        ; ...and the events it asked for
    DBSS DOS_B_M33OH,  4        ; the one function 14h displaced, staged across
    DBSS DOS_B_M33OM,  2        ; the swap because ES:DX/CX are the ANSWER
    DBSS DOS_B_M33OLD, 4        ; whatever was on IRQ0 when we hooked it
    DBSS DOS_B_M33LX,  2        ; where the pointer was at the LAST DISPATCH -
    DBSS DOS_B_M33LY,  2        ; ours alone, never dos_mou_edge's, which
    DBSS DOS_B_M33LB,  1        ; functions 5 and 6 consume (96.10.4.1)
    DBSS DOS_B_M33HK,  1        ; 1 = IRQ0 is hooked, and it is hooked ONCE
    DBSS DOS_B_M33BSY, 1        ; 1 = a callback is running below us
    DBSS DOS_B_M33SHW, 1        ; --- THE TEXT CURSOR (SPEC.md 96.10.5) -------
                                ; the SHOW COUNTER, and it starts at -1 rather
                                ; than 0: `01h` releases one nesting level and
                                ; `02h` takes one, so a program that hid twice
                                ; must show twice. Visible is exactly 0
    DBSS DOS_B_M33X0,  2        ; --- THE PROGRAM'S OWN WINDOW (SPEC.md
    DBSS DOS_B_M33X1,  2        ; 96.10.7): the range 07h and 08h set, which
    DBSS DOS_B_M33Y0,  2        ; every 03h answer is expressed in. Four words
    DBSS DOS_B_M33Y1,  2        ; and not a flag: a program is entitled to ask
                                ; for any window inside the virtual screen,
                                ; and Battle Chess asks for 0..319 x 0..199
                                ; because that is its mode 13h screen
    DBSS DOS_B_M33SM,  2        ; the SCREEN mask, ANDed into the cell...
    DBSS DOS_B_M33CM,  2        ; ...and the CURSOR mask, XORed after it. Both
                                ; are STATE and not constants: Microsoft Works
                                ; sets 77FF/7700 at startup and then 80FF/F000
                                ; twice more (96.10.5)
    DBSS DOS_B_M33DV,  2        ; where the cursor IS on the glass - segment,
    DBSS DOS_B_M33DO,  2        ; offset, and the cell as it was before we
    DBSS DOS_B_M33DC,  2        ; wrote. Segment 0 = nothing is drawn
    DBSS DOS_B_M33SEG, 2        ; **WHERE THE MOUSE HISTOGRAM LIVES** (SPEC.md
                                ; 96.10.3), 0 = nowhere. UNCONDITIONAL and in
                                ; the CORE's block, which is what 96.44.2's
                                ; first two rules leave: `dos_int33` is core,
                                ; so it may not name a host cell, and a row
                                ; only the DOSTRACE build emits would move
                                ; every core cell after it. Two bytes in every
                                ; build buy an instrument that costs the
                                ; shipped one no code at all - the 32 buckets
                                ; are in the trace PART, at DOS_TR33_OFF
    DBSS DOS_B_IMGHI, 2
    DBSS DOS_B_XFSEG, 2
    DBSS DOS_B_XLSEG, 2
    DBSS DOS_B_XHPAR, 2

; =============================================================================
; FILE HANDLES (SPEC.md 96.11)
; =============================================================================
; os8088 HAS NO FILE HANDLE ANYWHERE. The whole published API is by NAME and
; by WHOLE FILE - read a file into a buffer you sized, write or replace one
; from a buffer - so the handle layer is built here, out of those pieces, and
; docs/plans/DOS-EXEC-PLAN.md 6.3 is the design record for why it is here
; rather than in the kernel.
;
; What makes it work at all is ONE WINDOW: a buffer carved off the top of the
; arena before the program is told how much memory it has, holding a
; cluster-aligned span of one file. A read inside the window is a `movsb`; a
; read outside it refills with OSAPI_FILE_READ_AT, whose offset and capacity
; must BOTH be cluster multiples, which is the whole reason the window is
; aligned and cluster-sized rather than 512 bytes or "big".
;
; ONE handle owns the window at a time. Two open files interleaved thrash it
; and are correct; the alternative is a buffer per handle and the arena is the
; program's, not ours.
;
; WRITES ARE SEQUENTIAL, and that is the API's shape rather than a shortcut:
; OSAPI_FILE_APPEND refuses a file whose size is not a whole number of
; clusters, so the only append that can ever work is one onto a file this
; layer itself wrote in whole windows. A create-then-write-then-close is
; therefore exact. A write anywhere else REFUSED (SPEC.md 96.11.2) rather
; than reporting a success it did not have, until 96.11.6's in-place writes
; and 96.11.6.4's flush-then-overwrite gave each case a write that is real.
DOS_FH0     equ 5                   ; 0..4 are the five devices DOS opens for
DOS_NFH     equ 8                   ; every process; files start after them
FH_NAME     equ 0                   ; char[13], NUL-terminated
FH_FLAGS    equ 13
FH_POS      equ 14                  ; dword
FH_SIZE     equ 18                  ; dword
FH_VOL      equ 22                  ; the VOLUME the name is resolved against
                                    ; (SPEC.md 96.6.2). A handle is a name
                                    ; here and not a file, so without this a
                                    ; read re-resolves it wherever the program
                                    ; happens to be standing - which is how a
                                    ; copy off B: onto C: reads the
                                    ; destination back into itself
FH_DIR      equ 23                  ; ...and the FOLDER, a word: where the
                                    ; machine stood when the name resolved.
                                    ; Without it a name opened as SUB\X.DAT
                                    ; was re-resolved in the drive's CURRENT
                                    ; folder at every read and flush - the
                                    ; wrong file, from the first byte
                                    ; (docs/plans/completed/DOS-STREAM-PLAN.md 3.1)
FH_TOK      equ 25                  ; the WRITE_SEQ token a flush last got
                                    ; back, 0 = never streamed (SPEC.md 96.53)
FH_CUR      equ 27                  ; the READ_SEQ cursor, FSEQ_SIZE bytes: a
                                    ; refill steps one FAT link from where the
                                    ; last one read instead of walking from
                                    ; the front. dos_fh_new's zeroing is what
                                    ; keeps a reused slot's cursor from naming
                                    ; the file before (SPEC.md 18.4.8.1)
FH_SIZEOF   equ 27 + FSEQ_SIZE
; --- FH_FLAGS: ONE BYTE, SEVEN BITS, AND THEY GO IN ORDER (SPEC.md 96.11.10)
; **NOTHING ELSE MAY BE DEFINED BETWEEN THEM.** This block used to have the
; four DOS_DEV_* codes sitting in the middle of it, and the reader that added
; FHF_DEV looked at the line above the hole - 16 - and wrote 32, seven lines
; away from the FHF_WROTE that already owned it. `tests/unit/t_bits.py` is the
; gate now, and the ORDER is what makes it readable without one.
FHF_USED    equ 1
FHF_WRITE   equ 2                   ; opened by AH=3Ch: writes are accepted
FHF_MADE    equ 4                   ; ...and at least one window has been
                                    ; flushed, so the next one APPENDS
FHF_WHOLE   equ 8                   ; a COMPRESSED file, read whole and
                                    ; expanded: the window is the file and
                                    ; never refills (SPEC.md 96.11.1)
FHF_INPLC   equ 16                  ; opened by AH=3Dh for writing: the file
                                    ; EXISTS, so a write OVERWRITES through
                                    ; OSAPI_FILE_WRITE_AT and never moves the
                                    ; size (SPEC.md 96.11.6)
FHF_WROTE   equ 32                  ; AH=40h has been made on this handle, so
                                    ; AH=44h's bit 6 - "has NOT been written
                                    ; through" - is now CLEAR (SPEC.md
                                    ; 96.7.1.2). NOT FHF_MADE, which means a
                                    ; window has been FLUSHED: a program that
                                    ; writes eight bytes and asks has written,
                                    ; and nothing has reached the disk
FHF_DEV     equ 64                  ; a CHARACTER DEVICE and not a file at
                                    ; all (SPEC.md 96.11.7): CON, NUL, PRN or
                                    ; AUX, opened by name through AH=3Dh. The
                                    ; record holds no position, no size and no
                                    ; volume - FH_VOL carries the DOS_DEV_*
                                    ; code instead, a device having no volume
                                    ; for it to mean anything else about
                                    ; **IT WAS 32, WHICH FHF_WROTE ALREADY
                                    ; OWNED** (SPEC.md 96.11.10): the FIRST
                                    ; AH=40h on any handle then made it read
                                    ; as a device, so every later read
                                    ; answered end of file and every later
                                    ; write was ACCEPTED AND DISCARDED with
                                    ; the full count reported. That is how
                                    ; Microsoft Works saved a document with
                                    ; its 384-byte header all zeroes

DOS_DEV_CON equ 0                   ; ...and the four device names, in
DOS_DEV_NUL equ 1                   ; dos_devtab's order, because the index IS
DOS_DEV_PRN equ 2                   ; the code
DOS_DEV_AUX equ 3
DOS_NDEV    equ 4

DOS_PFIN    equ 24                  ; AH=29h reads at most this much of the
                                    ; program's name: "D:NNNNNNNN.EEE" is 14,
                                    ; so it is a whole one with room over
DOS_PFSEPN  equ 14                  ; ...and the separators above the blank
%ifndef KD_BACKEND                  ; ...unless a kerndos root is providing the
                                    ; back end, because it %includes this file
                                    ; whole OVER the kernel's own disk layer
                                    ; and that layer names DVOL_MAX first
                                    ; (docs/plans/KERN-DOS-PLAN.md §4).
                                    ; KD_BACKEND and not `%ifndef DVOL_MAX`:
                                    ; nasm's %ifndef tests for a MACRO, and an
                                    ; `equ` is a symbol - so the obvious guard
                                    ; compiles and does nothing
DVOL_MAX    equ DVOL_CAP            ; MIRRORS the kernel's WIDEST arm, which is
                                    ; `kernel/disk.inc`'s 8 (it is 4 on
                                    ; kern_small) and which `DVOL_CAP` already
                                    ; is - so the two are one number and the
                                    ; table below cannot be sized from the
                                    ; wrong one again (SPEC.md 96.44.2.1).
                                    ; **IT WAS 6, AND 6 IS NEITHER**: the
                                    ; comment said `assoc.inc` and that file
                                    ; has not owned the constant for some time.
                                    ; It cost the INLINE DOS two drives of
                                    ; reach on a kern_big machine, and it cost
                                    ; the PARTED DOS the whole console
                                    ; launcher, `apps/dos/doscore.asm` saying 8
                                    ; where this said 6.
                                    ; **AND IT STAYED HIDDEN BECAUSE THE
                                    ; PARTED PACKAGE RODE A GATE DISK**: no row
                                    ; on it ever typed a name at the prompt, so
                                    ; the first thing that found this was
                                    ; SPEC.md 96.40.3 pointing `$(SYSROOT)` at
                                    ; that package - ten console rows went red
                                    ; at once, on one cause.
                                    ; It is still a CAPACITY rather than a fact
                                    ; about the machine, and every use of it
                                    ; below is bound-checked - so a kernel that
                                    ; grows a ninth volume costs this DOS reach
                                    ; and can never cost it a write past its
                                    ; own bss, which is somebody else's heap
                                    ; claim
%endif
DOS_WKB     equ 8                   ; the window's floor in KB; a volume whose
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
                                    ; cluster is bigger gets a window of one
                                    ; cluster instead, because READ_AT cannot
                                    ; be asked for less

; -----------------------------------------------------------------------------
; dos_fh_setup - size and place the window, and take it OUT of the arena
; in:  [dos_arena] and [dos_apara] are set; called from dos_run
; out: CF=0, [dos_apara] reduced; CF=1 with AL = DER_MEM if the arena cannot
;      spare it
; -----------------------------------------------------------------------------
dos_fh_setup:
    push bx
    push cx
    push dx

    push di
    push es
    push ds
    pop es
    mov di, dos_fhtab               ; a launch starts with nothing open, and
    mov cx, FH_SIZEOF * DOS_NFH + DVOL_CAP  ; (and no cluster asked: dos_vcb)
                                    ; saying so costs six bytes against a
    xor al, al                      ; stale handle surviving into a second run
    cld
    rep stosb
    pop es
    pop di

    mov byte [dos_wown], 0xFF       ; nobody owns it yet
    mov word [dos_wlen], 0
    mov byte [dos_wdirty], 0
    mov byte [dos_wfill], 0

    call dos_be_dfree               ; BX = SECTORS per cluster, and this is the
    jc .guess                       ; O(clusters) call the SDK warns about, so
    or bx, bx                       ; it is asked ONCE per launch and never in
    jnz .got                        ; a loop
.guess:
    mov bx, 2                       ; a volume that will not say: 1KB, which is
.got:                               ; the 360KB floppy's own and is a multiple
    mov cl, 9                       ; of every smaller one
    shl bx, cl                      ; sectors -> bytes: it sizes the window,
                                    ; and each file's own volume answers for
                                    ; its arithmetic (dos_fh_cb, 96.11.11)
    mov ax, DOS_WKB * 1024
    cmp bx, ax                      ; a cluster larger than the floor IS the
    jbe .round                      ; window: READ_AT cannot be asked for a
    mov ax, bx                      ; capacity that is not a multiple of one
    jmp short .have
.round:
    xor dx, dx                      ; ...otherwise the largest multiple of the
    div bx                          ; cluster that fits the floor, which for
    mul bx                          ; every power-of-two cluster IS the floor
.have:
    mov [dos_wbytes], ax
    mov cl, 4
    shr ax, cl                      ; bytes -> paragraphs; the window is always
    mov cx, ax                      ; a cluster multiple and so paragraph-round

    mov ax, [dos_apara]
    sub ax, cx
    jc .nomem
    cmp ax, DOS_PSPP + 0x100        ; 4KB past the PSP, or there is no program
    jb .nomem                       ; worth starting
    mov [dos_apara], ax
    add ax, [dos_arena]
    mov [dos_wseg], ax              ; ...which is the paragraph the arena now
    clc                             ; ends at, so the program can never see it
    jmp short .out
.nomem:
    mov al, DER_MEM
    stc
.out:
    pop dx
    pop cx
    pop bx
    ret
%endif                              ; DOS_EXTCORE

; =============================================================================
; FIND, DIRECTORIES AND THE CWD (SPEC.md 96.12)
; =============================================================================
; The DTA's first 21 bytes are the driver's own by DOS's own definition, and
; the whole walk lives there - the ordinal and the pattern - so AH=4Fh needs
; no state in the package at all, and two programs, or one program with two
; DTAs, cannot tread on each other. DOS does exactly this, for exactly that.
DTA_ORD     equ 0                   ; word: the kernel ordinal to ask next
DTA_PAT     equ 2                   ; char[13]: the pattern, as it was given
%if DTA_PAT != 2
 %error "`inc di` twice stands for `add di, DTA_PAT` (size pass, SPEC.md 96.54.8)"
%endif
DTA_MASK    equ 15                  ; byte: the ATTRIBUTE MASK AH=4Eh was
                                    ; given, which DOS also keeps in the
                                    ; reserved head of the DTA. AH=4Fh needs
                                    ; it and is handed nothing but the DTA
DTA_VOL     equ 16                  ; byte: ...and the VOLUME it was walking,
                                    ; for the same reason one level along - a
                                    ; walk that began on B: continues on B:
                                    ; however the program has moved since
                                    ; (SPEC.md 96.6.2)
DTA_ATTR    equ 21                  ; ...and from here it is DOS's PUBLISHED
DTA_TIME    equ 22                  ; layout, which the program reads
DTA_DATE    equ 24
DTA_SIZE    equ 26                  ; dword
DTA_NAME    equ 30                  ; char[13], NUL-terminated
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_dta_seg - ES:DI = the caller's DTA
; out: ES:DI; clobbers nothing else
; -----------------------------------------------------------------------------
dos_dta_seg:
    mov di, [dos_dta]
    mov es, [dos_dtaseg]
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_find_step - one step of a walk, into the DTA at ES:DI
; in:  ES:DI = the DTA, its ordinal and pattern set
; out: CF=0 and the DTA filled; CF=1 with AL = 18, "no more files"
; -----------------------------------------------------------------------------
dos_find_step:
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [dos_dtasv], di             ; the caller's DTA, banked: the kernel's
    mov [dos_dtasvs], es            ; own record has to be read through OUR
    mov cx, [es:di+DTA_ORD]         ; ES and there is only one
.next:
    push ds
    pop es
    mov di, dos_fent
    call dos_be_find
    jc .none
    mov [dos_ford], cx
    cmp word [dos_fent+14], OSAPI_FT_UP
    je .next                        ; '..' is SYNTHESIZED (SPEC.md 19.5) and is
                                    ; not a file a DOS program can be shown
    mov di, [dos_dtasv]
    mov es, [dos_dtasvs]
    inc di
    inc di
    mov si, dos_fent
    call dos_wild                   ; DS:SI the name, ES:DI the pattern
    mov cx, [dos_ford]
    jne .next
    ; --- the name matches; does the ATTRIBUTE MASK allow it? ----------------
    ; AH=4Eh's CX is a mask and this used to ignore it, which is not a
    ; refinement: a program asking "what is this disk called" got handed the
    ; first ORDINARY FILE on it. Prince of Persia asks exactly that - mask 08h,
    ; pattern ????????.??? - to check it is running from its own floppy, and a
    ; disk with no label must answer NO MORE FILES. It got FAT.DAT and 28 more
    ; and refused to start (SPEC.md 96.12.1).
    mov di, [dos_dtasv]
    mov es, [dos_dtasvs]
    push ax
    mov al, [es:di+DTA_MASK]
    test al, 0x08
    jnz .skipit                     ; A VOLUME LABEL SEARCH matches the label
                                    ; and nothing else. A package cannot see
                                    ; one at all - the kernel reports labels to
                                    ; a DRIVER only - so the honest answer is
                                    ; the one a label-less disk gives anyway
    test byte [dos_fent+13], 0x10
    jz .allowed
    test al, 0x10
    jz .skipit                      ; a directory the caller did not ask for
.allowed:
    pop ax
    ; --- a hit ---------------------------------------------------------------
    mov [es:di+DTA_ORD], cx
    mov al, [dos_fent+13]
    mov [es:di+DTA_ATTR], al
    mov word [es:di+DTA_TIME], 0    ; the kernel's find record carries no
    mov word [es:di+DTA_DATE], 0    ; timestamp (SPEC.md 96.12.1), and 0 is
    mov ax, [dos_fent+18]           ; what an unset one looks like to DOS
    mov [es:di+DTA_SIZE], ax
    mov ax, [dos_fent+20]
    mov [es:di+DTA_SIZE+2], ax
    add di, DTA_NAME
    mov si, dos_fent
    mov cx, 13
    cld
    rep movsb
    clc
    jmp short .out
.skipit:
    pop ax
    jmp .next                       ; CX is already [dos_ford], which is what
                                    ; .next asks for
.none:
    mov al, 18
    stc
.out:
    jmp dos_popb_6                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_wild - does the 8.3 name at DS:SI match the pattern at ES:DI?
; out: ZF=1 on a match; every register preserved
;
; BOTH SIDES ARE EXPANDED TO THE ELEVEN-BYTE 8.3 FORM first - eight of name,
; three of extension, space-padded - because that is the only shape in which
; DOS's two wildcards mean what everyone expects. `*` fills THE REST OF ITS
; OWN FIELD and stops at the dot, so `*.TXT` matches `A.TXT` and not
; `A.TXTX`; and `?` stands for one character OR for the padding past a short
; name, which is why `A???????.TXT` finds `A.TXT`.
; -----------------------------------------------------------------------------
dos_wild:
    push ax
    push cx
    push si
    push di
    push ds
    push es

    push es                         ; the pattern's far pointer, banked while
    push di                         ; the NAME is expanded out of our own DS
    push ds
    pop es
    mov di, dos_w83a
    call dos_83
    pop si                          ; ...and now the pattern, whose segment is
    pop ds                          ; the program's
    push cs
    pop es
    mov di, dos_w83b
    call dos_83
    push es                         ; both buffers are ours, so both segments
    pop ds                          ; are too

    mov si, dos_w83a
    mov di, dos_w83b
    mov cx, 8
    call dos_wfld
    jne .out
    mov si, dos_w83a + 8
    mov di, dos_w83b + 8
    mov cx, 3
    call dos_wfld
.out:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_wfld - one 8.3 FIELD: CX bytes at DS:SI against the pattern at ES:DI
; out: ZF=1 on a match; clobbers AX, CX, SI, DI
; -----------------------------------------------------------------------------
dos_wfld:
    mov al, [es:di]
    cmp al, '*'
    je .yes                         ; the rest of this field, whatever it is
    cmp al, '?'
    je .step
    cmp al, [si]
    jne .no
.step:
    inc si
    inc di
    loop dos_wfld
.yes:
    xor al, al                      ; ZF=1
    ret
.no:
    mov al, 1
    or al, al                       ; ZF=0, and `or al, al` on a 0 would not
    ret                             ; say so - which is why AL is loaded first
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_83 - the NUL-terminated name at DS:SI into eleven bytes at ES:DI
; out: nothing; DI is left where it started. Clobbers AX, CX, SI
; -----------------------------------------------------------------------------
dos_83:
    push di
    push di
    mov al, ' '
    mov cx, 11
    cld
    rep stosb
    pop di
    push di
    mov cx, 8
.name:
    lodsb
    or al, al
    jz .done
    cmp al, '.'
    je .ext
    jcxz .name                      ; past eight: read on, store nothing
    stosb
    dec cx
    jmp short .name
.ext:
    pop di
    push di
    add di, 8
    mov cx, 3
.eloop:
    lodsb
    or al, al
    jz .done
    jcxz .eloop
    stosb
    dec cx
    jmp short .eloop
.done:
    pop di
    pop di
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_att_isdir - does [dos_fname] name a FOLDER in the directory we stand in?
; out: CF=0 yes, CF=1 no. Preserves every register.
;
; `dos_cd_go`'s `.named` scan with the walk taken out: it answers the question
; instead of acting on it, which is what AH=43h wants (SPEC.md 96.12.4).
; OSAPI_FILE_FIND is by ORDINAL, so this is a walk of the directory and not a
; lookup - the same cost `.named` has always paid, and paid once per AH=43h on
; a name that is not a file.
; -----------------------------------------------------------------------------
dos_att_isdir:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es                          ; dos_be_find answers into ES:DI, and the
                                    ; buffer is ours
    xor cx, cx
.scan:
    mov di, dos_fent
    call dos_be_find
    jc .no
    cmp word [dos_fent+14], OSAPI_FT_DIR
    jb .scan                        ; a file is not a folder
    mov si, dos_fname
    mov di, dos_fent
    call dos_streq
    jne .scan
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_cd_go - AH=3Bh's body: stand in the directory [dos_fname] names
; out: CF=1 if there is no such directory, or it is out of reach
;
; THE LAUNCH DIRECTORY IS THE PROGRAM'S ROOT (SPEC.md 96.12.2). Nothing above
; it is reachable, `\` means it, and AH=47h answers a path relative to it.
; -----------------------------------------------------------------------------
dos_cd_go:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es

    cmp word [dos_fabs], 1          ; "\" or "\NAME": from the VOLUME's root -
    jne .rel                        ; and "\A\B" from where dos_fh_enter's walk
                                    ; of "\A" ended: [dos_fhpath], the byte
                                    ; after, is set (SPEC.md 96.12.3). There
                                    ; is no jail any more - a program
    xor dx, dx                      ; launched from a subdirectory may leave it
    mov bl, [dos_vol]               ; and may change drives, as it would under
    call dos_be_goto                ; DOS (SPEC.md 96.6.1)
    jc .no
    mov [dos_curdir], dx
    cmp byte [dos_fname], 0
    je .same                        ; a bare "\" is the whole request
.rel:
    mov al, [dos_fname]
    or al, al
    jz .same
    cmp al, '.'
    jne .named
    mov al, [dos_fname+1]
    or al, al
    jz .same                        ; "." is where we already are
    cmp al, '.'
    jne .named
    cmp byte [dos_fname+2], 0
    jne .named

    ; --- ".." IS A RE-DESCENT, and that is the whole trick ----------------
    ; A package cannot walk up: dsk_find drops the on-disk dot links, so
    ; OSAPI_FILE_FIND never reports '..'. What it CAN do since SPEC.md 19.2.4
    ; is ask where it is standing - so up is "take the path, drop the last
    ; component, walk down to what is left". No stack, no depth limit, and
    ; correct across a drive switch, which a recorded stack would not have
    ; been.
    cmp word [dos_curdir], 0
    je .same                        ; at a root, and DOS ignores '..' there
    mov di, dos_pbuf                ; (pbuf is free here: the link's working
    mov cx, DOS_PBUF                ; directory and the environment's program
    call dos_be_path                ; path are both spent before the program
    jc .no                          ; runs, and this only happens while it does)
    add di, cx                      ; ...CX is the length, so DI is the NUL
.strip:
    cmp di, dos_pbuf
    jbe .cut
    dec di
    cmp byte [di], '\'
    jne .strip
.cut:
    mov byte [di], 0                ; "\A\B" -> "\A", and "\A" -> ""
    call dos_walk_pbuf
    jc .no
    mov [dos_curdir], dx
    jmp short .same

.named:
    xor cx, cx
.scan:
    mov di, dos_fent
    call dos_be_find
    jc .no
    cmp word [dos_fent+14], OSAPI_FT_DIR
    jb .scan                        ; a file is not somewhere to stand
    mov si, dos_fname
    mov di, dos_fent
    call dos_streq
    jne .scan
    mov dx, [dos_fent+16]
    mov bl, [dos_vol]
    call dos_be_goto
    jc .no
    mov [dos_curdir], dx
.same:
    clc
    jmp short .out
.no:
    stc
.out:
    jmp dos_popa_7                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; =============================================================================
; DRIVES (SPEC.md 96.6.1)
; =============================================================================
; AH=0Eh used to answer the COUNT and never move, on the reasoning that a
; program reads the count far more often than it changes drives and that a
; real change wanted a directory walk this DOS did not have. The first half is
; true and is why the no-op path below is still free; the second stopped being
; true at SPEC.md 19.2.4.
;
; WHAT A STUB COSTS IS NOT THE SWITCH, IT IS THE ANSWER TO THE NEXT QUESTION.
; The way a program finds out whether a drive exists is to select it and then
; ask AH=19h where it is - so a select that silently does nothing reports
; every drive as invalid, including the ones that are there. That is what an
; installer moving from B: to a mounted C: was told.
; -----------------------------------------------------------------------------

; --- dos_cw_back - undo AH=47h's temporary visit to another drive -----------
; A no-op when it never left, which is the ordinary case.
dos_cw_back:
    push ax
    push dx
    mov al, [dos_cwdrv]
    cmp al, 0xFF
    je .out
    cmp al, [dos_vol]
    jne .out
    mov dl, [dos_dvfrom]
    call dos_drv_sel
.out:
    pop dx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; --- dos_drv_bank - remember where drive AL is standing ---------------------
dos_drv_bank:
    push ax
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [dos_curdir]
    mov [bx+dos_dvcwd], ax
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; --- dos_drv_recall - stand drive AL where it last was (0 = its root) -------
dos_drv_recall:
    push ax
    push bx
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov ax, [bx+dos_dvcwd]
    mov [dos_curdir], ax
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; --- dos_drv_count - how many volumes there are, probed ONCE ----------------
; out: AL = the count; every other register preserved
dos_drv_count:
    push bx
    push cx
    mov al, [dos_ndrv]
    or al, al
    jnz .out                    ; PROBED ONCE and remembered: AH=0Eh is called
    xor bl, bl                  ; for its count far more often than to change
    xor cl, cl                  ; drives, and six far calls an ask would be a
.probe:                         ; poll nobody asked for
    mov al, bl
    push bx
    push cx
    call dos_be_vkind
    pop cx
    pop bx
    jc .gap
    inc cl
.gap:
    inc bl
    cmp bl, DVOL_MAX
    jb .probe
    or cl, cl
    jnz .have
    mov cl, 1                   ; a machine with no volume at all still has a
.have:                          ; drive as far as a DOS program is concerned
    mov [dos_ndrv], cl
    mov al, cl
.out:
    pop cx
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_drv_sel - AH=0Eh's body: stand on drive DL
; in:  DL = the drive, 0 = A
; out: nothing. The drive is UNCHANGED if DL names no volume, which is what
;      makes the AH=19h that follows a truthful answer either way
; clobbers: nothing but the flags
; -----------------------------------------------------------------------------
dos_drv_sel:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp dl, [dos_vol]
    je .out                     ; already there, and this is the common case:
                                ; no volume probe, no mount, nothing
    cmp dl, DVOL_MAX
    jae .out                    ; past our own array: refused rather than
                                ; written past (see DVOL_MAX)
    mov [dos_dvtgt], dl
    mov al, dl
    call dos_be_vkind           ; CF=1 = there is no such volume, and that is
    jc .out                     ; the whole of "invalid drive letter"
    mov al, [dos_vol]
    mov [dos_dvfrom], al
    call dos_drv_bank           ; bank where we are...
    mov al, [dos_dvtgt]
    mov [dos_vol], al
    call dos_drv_recall         ; ...and stand where that drive last was
                                ; **AND NO MOUNT** (SPEC.md 96.48). It used to
                                ; `dos_be_goto` here and roll the whole switch
                                ; back if that refused, which is a volume
                                ; mount on every drive change a program makes
                                ; for its own reasons. `dos_be_vkind` above is
                                ; the existence test and it costs no disk;
                                ; the mount happens at the next NAME, where a
                                ; volume that cannot be reached refuses with
                                ; the same code 3 - which is also nearer to
                                ; what a real DOS does, AH=0Eh touching no
                                ; drive at all
.out:
    jmp dos_popa_7                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE

; =============================================================================
; THE DATE AND THE TIME (SPEC.md 96.13)
; =============================================================================
; There is NO date or time slot in the SDK at all, so this is the one group
; that goes to the ROM and the BDA directly - which is legitimate here and
; nowhere else: inside the bracket the machine is ours (SPEC.md 53.1), and it
; is the same place DOS gets them.
;
; THE TIME IS THE BIOS TICK COUNT AT 0040:006C, read DIRECTLY rather than
; through int 1Ah AH=00h, and that is a correctness choice and not a shortcut:
; AH=00h CLEARS the midnight-rollover flag as it answers, and the kernel's own
; clock is chained to the same counter (SPEC.md 8.5) - so asking the ROM would
; consume, once a day, the very event the kernel needs to advance ITS date.
; Reading the four bytes has no side effect at all, and midnight is detected
; here by the count going BACKWARDS, which needs nobody's flag.
;
; THE DATE IS OURS TO KEEP, which is what DOS does on a machine with no clock
; chip: the RTC is asked once at bracket entry and believed only if it answers
; something possible, and AH=2Bh writes into the same copy.
CLK_DEF_Y   equ 2026                ; MIRRORED from kernel/clock.inc, so a DOS
CLK_DEF_M   equ 7                   ; program and the menu bar agree about a
CLK_DEF_D   equ 4                   ; machine that has no clock to ask.
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
                                    ; tests/unit/t_mirror.py is what keeps them
                                    ; equal, because nothing else would notice

; -----------------------------------------------------------------------------
; dos_ticks - the BIOS tick count
; out: DX:AX; every other register preserved
; -----------------------------------------------------------------------------
dos_ticks:
    push bx
    push es
    mov bx, 0x40
    mov es, bx
    mov ax, [es:0x6C]
    mov dx, [es:0x6E]
    pop es
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_date_init - believe the RTC, or the kernel's fallback
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_date_init:
    push ax
    push bx
    push cx
    push dx
    mov word [dos_dy], CLK_DEF_Y
    mov byte [dos_dm], CLK_DEF_M
    mov byte [dos_dd], CLK_DEF_D

    mov ah, 0x04                    ; the RTC's date, BCD, AT and later
    int 0x1A
    jc .stamp                       ; no clock chip: the fallback stands
    mov al, ch                      ; ...AND A 5150's ROM DOES NOT SET CF FOR A
    call dos_unbcd                  ; FUNCTION IT HAS NEVER HEARD OF, so every
    mov bl, 100                     ; field below is checked for a value that
    mul bl                          ; is merely POSSIBLE before any of it is
                                    ; (A BYTE MULTIPLY: `mul bx` writes DX, and
                                    ; DH/DL are the month and the day still to
                                    ; be read - every RTC date fell back to 4
                                    ; July, SPEC.md 96.13.1)
    mov [dos_tmp1], ax              ; believed - which is the only thing
    mov al, cl                      ; standing between a garbage register and a
    call dos_unbcd                  ; date the program will stamp on its files
    add [dos_tmp1], ax
    mov ax, [dos_tmp1]
    cmp ax, 1980
    jb .stamp
    cmp ax, 2099
    ja .stamp
    mov [dos_tmp2], ax
    mov al, dh
    call dos_unbcd
    or al, al
    jz .stamp
    cmp al, 12
    ja .stamp
    mov [dos_tmp3], al
    mov al, dl
    call dos_unbcd
    or al, al
    jz .stamp
    cmp al, 31
    ja .stamp
    mov [dos_dd], al
    mov al, [dos_tmp3]
    mov [dos_dm], al
    mov ax, [dos_tmp2]
    mov [dos_dy], ax
.stamp:
    call dos_ticks                  ; the count midnight is measured against
    mov [dos_lasttl], ax
    mov [dos_lastth], dx
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_unbcd - AL from packed BCD to binary
; out: AX = 0..99; clobbers nothing else
; -----------------------------------------------------------------------------
dos_unbcd:
    push bx
    push cx
    mov bh, al
    and bh, 0x0F
    mov cl, 4
    shr al, cl
    mov ah, 10
    mul ah                          ; AX = tens * 10
    add al, bh
    adc ah, 0
    pop cx
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_date_roll - has midnight passed since anyone last looked?
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_date_roll:
    push ax
    push dx
    call dos_ticks
    cmp dx, [dos_lastth]
    jb .rolled                      ; the count going BACKWARDS is midnight,
    ja .keep                        ; and it needs no flag from the ROM
    cmp ax, [dos_lasttl]
    jae .keep
.rolled:
    call dos_date_inc
.keep:
    mov [dos_lasttl], ax
    mov [dos_lastth], dx
    pop dx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_date_inc - one day on
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_date_inc:
    push ax
    push bx
    mov al, [dos_dd]
    inc al
    mov bl, [dos_dm]
    xor bh, bh
    dec bx
    mov ah, [bx+dos_mlen]
    cmp bl, 1                       ; February
    jne .chk
    test word [dos_dy], 3           ; the century rule cannot bite between 1980
    jnz .chk                        ; and 2099 - 2000 is a leap year by BOTH
    inc ah                          ; tests - so `and 3` is exact here
.chk:
    cmp al, ah
    jbe .store
    mov al, 1
    mov bl, [dos_dm]
    inc bl
    cmp bl, 12
    jbe .mok
    mov bl, 1
    inc word [dos_dy]
.mok:
    mov [dos_dm], bl
.store:
    mov [dos_dd], al
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_dow - the day of the week, Sakamoto's method
; out: AL = 0 Sunday .. 6 Saturday; clobbers AH
; -----------------------------------------------------------------------------
dos_dow:
    push bx
    push cx
    push dx
    mov cx, [dos_dy]
    mov bl, [dos_dm]
    xor bh, bh
    cmp bl, 3
    jae .nm
    dec cx                          ; January and February belong to the year
.nm:                                ; before, which is what makes the table work
    dec bx
    mov al, [bx+dos_dowt]
    xor ah, ah
    mov [dos_acc], ax
    mov al, [dos_dd]
    xor ah, ah
    add [dos_acc], ax
    add [dos_acc], cx
    mov ax, cx
    shr ax, 1
    shr ax, 1
    add [dos_acc], ax               ; + y/4
    mov ax, cx
    xor dx, dx
    mov bx, 100
    div bx
    sub [dos_acc], ax               ; - y/100
    mov ax, cx
    xor dx, dx
    mov bx, 400
    div bx
    add [dos_acc], ax               ; + y/400
    mov ax, [dos_acc]
    xor dx, dx
    mov bx, 7
    div bx
    mov ax, dx                      ; the remainder IS the day
    pop dx
    pop cx
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_time_now - the tick count as DOS's four fields
; out: CH = hours, CL = minutes, DH = seconds, DL = hundredths
;
; THE COUNT IS HALVED FIRST, because there are 65,543.4 ticks in an hour and
; that does not fit a 16-bit divisor - half of it does. What the whole chain
; costs in accuracy is about a second at the end of an hour, which is the
; same order as the drift a PC's own tick clock has against the wall: the
; divisors here are 32,772 / 1,092 / 18.2 against true values of 32,771.7 /
; 1,092.39 / 18.2065.
; -----------------------------------------------------------------------------
dos_time_now:
    push ax
    push bx
    call dos_ticks
    shr dx, 1
    rcr ax, 1
    mov bx, 32772
    div bx                          ; AX = hours, DX = half-ticks left over
    mov [dos_tmp1], al
    mov ax, dx
    shl ax, 1                       ; ...whole ticks again, 0..65,542
    xor dx, dx
    mov bx, 1092
    div bx
    cmp ax, 59
    jbe .m
    mov ax, 59                      ; 1092 against a true 1092.39 can round the
.m:                                 ; last minute of an hour up to 60
    mov [dos_tmp2], al
    mov ax, dx
    mov bx, 10
    mul bx
    mov bx, 182                     ; ticks in a second, times ten
    div bx
    cmp ax, 59
    jbe .s
    mov ax, 59
.s:
    mov [dos_tmp3], al
    mov ax, dx
    mov bx, 100
    mul bx
    mov bx, 182
    div bx
    mov dl, al                      ; hundredths
    mov dh, [dos_tmp3]
    mov ch, [dos_tmp1]
    mov cl, [dos_tmp2]
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_time_set - AH=2Dh's body: the four fields back into the tick count
; in:  CH = hours, CL = minutes, DH = seconds
; out: nothing; every register preserved
;
; It writes 0040:006C, so a DOS program may set the clock, read it back and
; agree with itself. Nothing puts the count back - dos_bdalist leaves it out
; on purpose (SPEC.md 96.5, 96.13.2) - and in a window nothing needs to: the
; kernel keeps its clock off [ticks].
;
; **UNDER kern_dos THE WAY HOME READS IT, so the STEP goes to the host
; first** (DHK_TSET, KERN-MEGA-PLAN 14.114): DX:AX = the new count less the
; old, signed. The way home measures how long the program had the machine as
; 0040:006C now less the count at the handover (SPEC.md 96.49.5), and a
; program that set the clock BACK made that negative - which reads as a
; midnight crossed, so the session came home a day on. kern_dos moves the
; handover count by the same step, and the difference is the run again.
; The windowed host binds nothing.
; -----------------------------------------------------------------------------
dos_time_set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push dx                         ; DH = seconds
    push cx                         ; CL = minutes
    mov al, ch
    cbw                             ; (CH <= 23)
    mov bx, 32772
    mul bx
    shl ax, 1
    rcl dx, 1                       ; hours, in ticks
    xchg si, ax
    mov di, dx
    pop ax                          ; AL = minutes
    xor ah, ah
    mov bx, 1092
    mul bx
    add si, ax
    adc di, dx
    pop ax
    mov al, ah                      ; AL = seconds
    xor ah, ah
    mov bx, 182
    mul bx
    mov bl, 10
    div bx                          ; (DX = 0 after a mul this small)
    add si, ax
    adc di, 0

    mov bx, 0x40
    mov es, bx
    mov ax, si
    mov dx, di
    sub ax, [es:0x6C]
    sbb dx, [es:0x6E]               ; DX:AX = the step
    cmp word [dos_hkv + DHK_TSET], 0
    je .nohk
    call word [dos_hkv + DHK_TSET]
.nohk:
    mov [es:0x6C], si
    mov [es:0x6E], di
    mov [dos_lasttl], si            ; ...and midnight is measured from here now
    mov [dos_lastth], di
    jmp dos_popa_7                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; =============================================================================
; AH=4Bh - LOADING AND RUNNING A CHILD (SPEC.md 96.14)
; =============================================================================
; -----------------------------------------------------------------------------
; dos_prog_enter - hand the CPU to the program at [dos_ldpsp]
; in:  [dos_ldpsp], [dos_isexe], [dos_prgsp] or the [dos_exe_*] four
; out: NEVER RETURNS BY FALLING OUT. dos_terminate is how control comes back,
;      and for a child it comes back to the word the CALLER's `call` pushed.
;
; The same door for the launched program and for AH=4Bh's child, which is
; what stops the two drifting: a .COM is entered at PSP:0100 and NOT PSP:0000
; - the first 256 bytes ARE the PSP and its first two are the `CD 20` a
; program's own `ret` lands on, so jumping to 0 runs that INT 20h and, from
; outside, is indistinguishable from a program that exited 0 having printed
; nothing.
; -----------------------------------------------------------------------------
dos_prog_enter:
    mov dx, [dos_ldpsp]             ; the PSP, which is DS and ES for both
    cmp byte [dos_isexe], 0         ; kinds (SPEC.md 96.3)
    je .com
    mov bx, [dos_exe_sp]            ; an .EXE brings its OWN stack, out of the
    mov cx, [dos_exe_ss]            ; header and relocated with everything else
    mov si, [dos_exe_cs]
    mov di, [dos_exe_ip]
    jmp short .go
.com:
    mov bx, [dos_prgsp]             ; a .COM runs on the PSP's own segment
    mov cx, dx
    mov si, dx
    mov di, 0x100
.go:
    mov byte [dos_onprog], 1        ; from here until dos_terminate, a kernel
                                    ; call has to borrow a stack (SPEC.md 96.4.1)
    mov [dos_dstk], sp              ; ...AND WHERE THE GATE'S FRAME GOES
                                    ; (SPEC.md 96.7.2). EXACT rather than a
                                    ; reservation: this is the SP of whoever
                                    ; is jumping into the program, so it is by
                                    ; construction below everything they still
                                    ; hold - the top-level case is
                                    ; dos_fsx_main's own depth and the AH=4Bh
                                    ; case is the parent's live handler frame,
                                    ; with no slice constant to size
    cli                             ; SS and SP are loaded as a pair, always:
    mov ss, cx                      ; an interrupt between them lands on a
    mov sp, bx                      ; stack that is half of each
    sti
    mov ds, dx
    mov es, dx
    xor ax, ax                      ; AL/AH = the two FCB drive checks: FFh
    cmp byte [es:0x5C], DVOL_MAX    ; for a drive past the last, as DOS does
    jbe .d1                         ; (MEASURED, `FOO Z:BAR` enters with
    mov al, 0xFF                    ; AX=FF00h, SPEC.md 96.56.3)
.d1:
    cmp byte [es:0x6C], DVOL_MAX
    jbe .d2
    mov ah, 0xFF
.d2:
    push si
    push di
    retf
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_exec_load - give the child a block, load it into it, and build its PSP
; in:  [dos_fname], [dos_xparm]/[dos_xparms] = the parameter block
; out: CF=0 with everything set for dos_prog_enter; CF=1 with AL = a DOS code
; -----------------------------------------------------------------------------
dos_exec_load:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov ax, [dos_ldpsp]             ; BANK THE PARENT. It is still the running
    mov [dos_ppsp], ax              ; program, and everything below is about to
    mov [dos_parent], ax            ; describe the child instead
    mov ax, [dos_ldpara]
    mov [dos_ppara], ax
    mov ax, [dos_prgsp]
    mov [dos_pgpar], ax
    mov al, [dos_isexe]
    mov [dos_pexe], al

    mov bx, 0xFFFF                  ; THE LARGEST FREE BLOCK, asked for the way
    call dos_mcb_alloc              ; a program asks: 0FFFFh cannot be granted,
    jnc .nomem                      ; so the refusal is the answer and BX is it
    or bx, bx
    jz .nomem
    cmp bx, 64                      ; a PSP and a KB, or there is no point
    jb .nomem
    call dos_mcb_alloc              ; ...and now for real
    jc .nomem
    mov [dos_chblk], ax
    mov [dos_ldpsp], ax
    mov [dos_ldpara], bx
    mov word [dos_ldname], dos_fname

    call dos_load
    jc .noent
    call dos_is_exe
    jnc .com
    call dos_exe_setup              ; sets [dos_isexe] itself
    jc .bad
%ifdef DOS_MEGA
    ; **AN EXEPACK CHILD, UNPACKED BY THE HOST** (SPEC.md 96.40.9.1). The HMA
    ; host runs with A20 open, so a child's own unpacker wraps below 64KB; the
    ; hook is `kd_xunp`, which unpacks it in place against the same [dos_exe_*]
    ; `dos_exe_setup` just filled. Best effort - absent (DOS.O88, kern_big) or
    ; a child it refuses runs its own unpacker, which works when the allocator
    ; placed it at or above 64KB. The hook preserves every register.
    cmp word [dos_hkv + DHK_XUNP], 0
    je .psp
    call word [dos_hkv + DHK_XUNP]
%endif
    jmp short .psp
.com:
    mov byte [dos_isexe], 0
    cmp word [dos_imghi], 0         ; a .COM is ONE segment
    jne .bad
    cmp word [dos_imgsz], 0xFF00
    ja .bad
.psp:
    call dos_psp_make
    call dos_exec_tail
    clc
    jmp short .out
.nomem:
    call dos_exec_back              ; the parent, whole again: a refusal must
    mov al, 8                       ; not leave the machine describing a child
    jmp short .err                  ; that never ran
.noent:
    call dos_exec_back
    mov al, 2
    jmp short .err
.bad:
    call dos_exec_unload
    call dos_exec_back
    mov al, 11                      ; "invalid format", which is DOS's own
.err:
    stc
.out:
    jmp dos_popb_6                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_exec_back - the parent is the running program again
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_exec_back:
    push ax
    mov word [dos_ldname], dos_name
    mov ax, [dos_ppsp]
    mov [dos_ldpsp], ax
    mov ax, [dos_ppara]
    mov [dos_ldpara], ax
    mov ax, [dos_pgpar]
    mov [dos_prgsp], ax
    mov al, [dos_pexe]
    mov [dos_isexe], al
    mov word [dos_parent], 0
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_exec_unload - the child's block, back to the chain
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_exec_unload:
    push ax
    push es
    mov ax, [dos_chblk]
    or ax, ax
    jz .out
    mov es, ax
    call dos_mcb_free
    mov word [dos_chblk], 0
.out:
    pop es
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_exec_tail - the parameter block's command tail into the child's PSP:0080
; out: nothing; every register preserved
;
; A zero SEGMENT means no tail, and the empty one dos_psp_make already wrote
; stands. The length byte is clamped to 126 because the tail plus its own
; count and the 0Dh have to live inside the PSP's 128.
; -----------------------------------------------------------------------------
dos_exec_tail:
    push ax
    push cx
    push si
    push di
    push ds
    push es
    mov es, [dos_xparms]
    mov di, [dos_xparm]
    mov ax, 0x5C                    ; THE BLOCK'S TWO FCBs, at +6 and +0Ah,
.fcb:                               ; into 5Ch and 6Ch - TWELVE bytes each and
    push di                         ; not sixteen: MEASURED, a child sees
    push es                         ; 5Ch+12..15 as zero whatever the block's
    lds si, [es:di+6]               ; FCB holds there (SPEC.md 96.56.3)
    mov es, [cs:dos_ldpsp]
    mov di, ax
    mov cx, 12
    cld
    rep movsb
    pop es
    pop di
    add di, 4
    add al, 0x10
    cmp al, 0x6C
    jbe .fcb
    sub di, 8
    mov si, [es:di+2]
    mov ax, [es:di+4]
    or ax, ax
    jz .none
    mov ds, ax
    mov ax, [cs:dos_ldpsp]          ; through CS: DS is the tail's now
    mov es, ax
    mov di, 0x80
    cld
    lodsb
    cmp al, 126
    jbe .len
    mov al, 126
.len:
    mov cl, al
    xor ch, ch
    stosb
    jcxz .term
    rep movsb
.term:
    mov al, 0x0D
    stosb
.none:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE

; =============================================================================
; XMS - EXTENDED MEMORY (SPEC.md 96.15)
; =============================================================================
; A DOS program finds extended memory by asking the MULTIPLEX interrupt
; whether an XMS driver is there (int 2Fh AX=4300h), then asking the same
; interrupt for its entry point (AX=4310h) and far-calling that. So what is
; needed is the SHAPE of HIMEM.SYS over os8088's own pool, which the kernel
; already publishes as four slots: OSAPI_XMEM_CAPS, _ALLOC, _FREE and _COPY.
; No driver change, and no second allocator.
;
; THEY ARE UI-TASK SLOTS, so every one of them goes through dos_be_go and runs
; on the UI task's own stack (SPEC.md 96.4.1) - the same rule the file calls
; live under, and for the same reason.
;
; A HANDLE IS OURS, not the kernel's. OSAPI_XMEM_ALLOC answers a 32-bit linear
; base and XMS handles are 16-bit, so the table below is the mapping. It is
; small on purpose: the SDK's own advice is to take one big block and
; subdivide it rather than take many, and a program that wants more handles
; than this is a program that would exhaust the kernel's table too.
XMS_NH      equ 8
XH_BASE     equ 0                   ; dword: what the kernel handed back
XH_KB       equ 4                   ; word
XH_USED     equ 6
XH_LOCK     equ 7                   ; byte: lock count (kern_mega's 0Ch/0Dh)
XH_SIZE     equ 8
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_int2f - the multiplex interrupt
; -----------------------------------------------------------------------------
dos_int2f:
    sti
    push bp
    push ds
    mov bp, sp
    push cs
    pop ds
    cmp ax, 0x4300
    je .installed
    cmp ax, 0x4310
    je .entry
    pop ds                          ; EVERY OTHER MULTIPLEX NUMBER IS ANSWERED
    pop bp                          ; "nobody is here", which is AL = 0 and is
    xor al, al                      ; what an unhooked 2Fh cannot say: before
    iret                            ; this the vector was the ROM's or nothing
.installed:
    call dos_xms_kb                 ; AX = KB the pool can hand out
    or ax, ax
    jz .absent                      ; NO STORE IS NOT AN XMS DRIVER (SPEC.md
    mov al, 0x80                    ; 96.15.1): on the 8088 this project is
    jmp short .out                  ; calibrated against there is none, and a
.absent:                            ; program that is told "yes" and then
    xor al, al                      ; refused every call is worse off than one
.out:                               ; told "no" and using conventional memory
    pop ds
    pop bp
    iret
.entry:
%ifndef DOS_MEGA
    mov bx, dos_xms_ent
    pop ds
    pop bp
    push cs
    pop es
    iret
%else
    push ax
    push dx
    mov ax, dos_xms_ent
    call dos_hvec                   ; ES:BX, through a slot on the HMA run
    mov bx, ax
    mov es, dx
    pop dx
    pop ax
    pop ds
    pop bp
    iret
%endif
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_xms_kb - the pool's free KB, through the back end
; out: AX = KB; clobbers nothing else
; -----------------------------------------------------------------------------
dos_xms_kb:
    push bx
    push cx
    push dx
    call dos_be_xcaps
    jnc .ok
    xor ax, ax
.ok:
    pop dx
    pop cx
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_xms_ent - the XMS entry point itself, FAR CALLED by the program
; in:  AH = the function; out: AX = 1 done / 0 refused with BL = the code
; -----------------------------------------------------------------------------
dos_xms_ent:
    push bp
    push ds
    mov bp, sp
    push cs
    pop ds

%ifdef DOS_MEGA
    ; kern_mega's door is the one a 286 DOS extender meets, and KERN-MEGA-DOS-
    ; PLAN 10.4 found what it lacked: the largest free block (08h's AX is not
    ; the total once a block sits mid-arena), the block's size kept so a move
    ; is bounded by its HANDLE (0Bh), and lock, unlock, information and
    ; resize (0Ch-0Fh), which answered "not implemented"
    cmp ah, 0x08
    je .mquery
    cmp ah, 0x09
    je .malloc
    cmp ah, 0x0C
    jb .mstd
    cmp ah, 0x0F
    ja .mstd
    jmp .mlock
.mstd:
%endif
    cmp ah, 0x00
    je .ver
    cmp ah, 0x08
    je .query
    cmp ah, 0x09
    je .alloc
    cmp ah, 0x0A
    je .free
    cmp ah, 0x0B
    je .move
    cmp ah, 0x03                    ; the four A20 calls. WE DO NOT FIGHT OVER
    jb .nope                        ; A20 (SPEC.md 96.15.2): the kernel's own
    cmp ah, 0x07                    ; memory above 1MB is reached by the same
    ja .nope                        ; BIOS path, so the line is already however
    mov ax, 1                       ; it needs to be and a program toggling it
    xor bl, bl                      ; is told yes and changes nothing
    jmp .out
.nope:
    xor ax, ax
    mov bl, 0x80                    ; "not implemented", which is XMS's own
    jmp .out
.ver:
    mov ax, 0x0300                  ; XMS 3.0
    xor bx, bx
    xor dx, dx                      ; ...and NO HMA: the high memory area is a
    jmp .out                        ; 286 addressing trick and this is an 8086
                                    ; contract (SPEC.md 96.15.2)
.query:
    call dos_xms_kb
    mov dx, ax                      ; total free
    mov bl, 0                       ; ...and the largest, which for one pool
    or ax, ax                       ; with one free run is the same number
    jnz .out
    mov bl, 0xA0                    ; "all extended memory is allocated"
    jmp .out
.alloc:
    ; DX = KB wanted; out DX = the handle
    call dos_xms_new                ; SI = a free row, AX = its handle
    jc .nohand
    push si
    mov ax, dx
    mov dx, 1024
    mul dx                          ; DX:AX = bytes, and 64MB is the ceiling a
    call dos_be_xalloc              ; 16-bit KB count can even ask for
    pop si
    jc .noroom
    mov [si+XH_BASE], ax
    mov [si+XH_BASE+2], dx
    mov byte [si+XH_USED], 1
.ahandle:
    mov dx, si
    sub dx, dos_xmstab
    mov ax, XH_SIZE
    push bx
    mov bx, ax
    mov ax, dx
    xor dx, dx
    div bx
    inc ax                          ; handles are 1-based: 0 means CONVENTIONAL
    pop bx                          ; memory in a move block
    mov dx, ax
    mov ax, 1
    xor bl, bl
    jmp .out
.nohand:
    xor ax, ax
    mov bl, 0xA1                    ; "all handles are in use"
    jmp .out
.noroom:
    xor ax, ax
    mov bl, 0xA0
    jmp .out
.free:
    ; DX = the handle
    mov ax, dx
    call dos_xms_row                ; SI = its row
    jc .badh
%ifdef DOS_MEGA
    cmp byte [si+XH_LOCK], 0        ; a LOCKED block is not freed (XMS ABh)
    je .mfree
    jmp .mlocked
.mfree:
%endif
    mov ax, [si+XH_BASE]
    mov dx, [si+XH_BASE+2]
    call dos_be_xfree
    mov byte [si+XH_USED], 0
    mov ax, 1
    xor bl, bl
    jmp .out
.badh:
    xor ax, ax
    mov bl, 0xA2                    ; "invalid handle"
    jmp .out
.move:
    ; DS:SI = a sixteen-byte move block, and DS is the CALLER's
    call dos_xms_move
    jc .mvbad
    mov ax, 1
    xor bl, bl
    jmp short .out
.mvbad:
    xor ax, ax                      ; BL is dos_xms_move's own
.out:
    pop ds
    pop bp
    retf
%ifdef DOS_MEGA
.mquery:                            ; 08h: AX = largest, DX = total, KB
    push cx
    push si
    push di
    xor di, di
    call dos_be_xcaps               ; AX = total, DI = largest (kern_dos)
    jnc .mqok
    xor ax, ax
    xor di, di
.mqok:
    mov dx, ax
    mov ax, di
    pop di
    pop si
    pop cx
    xor bl, bl
    or dx, dx
    jnz .out
    mov bl, 0xA0                    ; "all extended memory is allocated"
    jmp short .out
.malloc:                            ; 09h, with the block's KB kept
    push dx
    call dos_xms_new                ; SI = a free row
    jc .mnoh
    push si
    mov ax, dx
    mov dx, 1024
    mul dx
    call dos_be_xalloc
    pop si
    jc .mnor
    mov [si+XH_BASE], ax
    mov [si+XH_BASE+2], dx
    pop word [si+XH_KB]
    mov byte [si+XH_USED], 1
    mov byte [si+XH_LOCK], 0
    jmp .ahandle
.mnoh:
    pop dx
    jmp .nohand
.mnor:
    pop dx
    jmp .noroom
.mlock:                             ; 0Ch-0Fh: DX = the handle
    push ax
    mov ax, dx
    call dos_xms_row                ; SI = its row
    pop ax
    jnc .mrow
    jmp .badh
.mrow:
    cmp ah, 0x0D
    je .munl
    jb .mlk
    cmp ah, 0x0E
    je .minfo
    cmp byte [si+XH_LOCK], 0        ; 0Fh, BX = the new KB: a SHRINK is a new
    jne .mlocked                    ; figure for the bound and nothing moves;
    cmp bx, [si+XH_KB]              ; a growth is refused, the block having
    ja .mbig                        ; neighbours in the arena
    mov [si+XH_KB], bx
    jmp short .mok
.mbig:
    xor ax, ax
    mov bl, 0xA0
    jmp .out
.mlocked:
    xor ax, ax
    mov bl, 0xAB                    ; "the block is locked"
    jmp .out
.mlk:                               ; 0Ch: DX:BX = its linear address
    cmp byte [si+XH_LOCK], 0xFF
    je .mlkov
    inc byte [si+XH_LOCK]
    mov bx, [si+XH_BASE]
    mov dx, [si+XH_BASE+2]
    mov ax, 1
    jmp .out
.mlkov:
    xor ax, ax
    mov bl, 0xAC                    ; "lock count overflow"
    jmp .out
.munl:                              ; 0Dh
    cmp byte [si+XH_LOCK], 0
    je .munlno
    dec byte [si+XH_LOCK]
.mok:
    mov ax, 1
    xor bl, bl
    jmp .out
.munlno:
    xor ax, ax
    mov bl, 0xAA                    ; "the block is not locked"
    jmp .out
.minfo:                             ; 0Eh: BH = locks, BL = free handles,
    push cx                         ; DX = KB
    push si
    mov si, dos_xmstab
    mov cx, XMS_NH
    xor bl, bl
.mfh:
    cmp byte [si+XH_USED], 0
    jne .mfn
    inc bl
.mfn:
    add si, XH_SIZE
    loop .mfh
    pop si
    pop cx
    mov bh, [si+XH_LOCK]
    mov dx, [si+XH_KB]
    mov ax, 1
    jmp .out
%endif
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_xms_new - the first free handle row
; out: CF=0 with SI = the row; CF=1 if the table is full
; -----------------------------------------------------------------------------
dos_xms_new:
    push cx
    mov si, dos_xmstab
    mov cx, XMS_NH
.scan:
    cmp byte [si+XH_USED], 0
    je .got
    add si, XH_SIZE
    loop .scan
    stc
    jmp short .out
.got:
    clc
.out:
    pop cx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_xms_row - the row handle AX names
; out: CF=0 with SI = the row; CF=1 if it is not an open handle
; -----------------------------------------------------------------------------
dos_xms_row:
    push ax
    push bx
    or ax, ax
    jz .no
    cmp ax, XMS_NH
    ja .no
    dec ax
    mov bl, XH_SIZE
    mul bl
    mov si, ax
    add si, dos_xmstab
    cmp byte [si+XH_USED], 0
    je .no
    pop bx
    pop ax
    clc
    ret
.no:
    pop bx
    pop ax
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

%ifdef DOS_MEGA
; dos_xms_fits - SI = a handle's row, DX:AX = an offset into it: CF=1 when
; offset + [dos_xmlen] runs past the block's [XH_KB] (XMS 0Bh's A4h/A6h; a
; move bounded by the ARENA could reach the next handle's block). Clobbers
; AX, DX
dos_xms_fits:
    push bx
    push cx
    add ax, [dos_xmlen]
    adc dx, [dos_xmlen+2]
    jc .no
    mov bx, [si+XH_KB]              ; BX:CX = the block's bytes
    mov cx, bx
    push cx
    mov cl, 6
    shr bx, cl
    pop cx
    push ax
    mov ax, cx
    mov cl, 10
    shl ax, cl
    mov cx, ax
    pop ax
    cmp dx, bx
    ja .no
    jb .yes
    cmp ax, cx
    ja .no
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop cx
    pop bx
    ret
%endif

; -----------------------------------------------------------------------------
; dos_xms_move - AH=0Bh's body
; in:  the caller's DS:SI -> the move block; [bp] = the caller's DS
; out: CF=0 done; CF=1 with BL = an XMS error code
;
; ONE END MUST BE CONVENTIONAL. OSAPI_XMEM_COPY moves between a conventional
; address and a linear extended one, in either direction, and has no
; extended-to-extended form at all - so a move with two extended ends is
; REFUSED rather than bounced through a buffer the program did not give us.
; -----------------------------------------------------------------------------
dos_xms_move:
    push ax
    push cx
    push dx
    push di
    push es
    push ds

    mov ds, [bp]                    ; the block is the CALLER's
    mov ax, [si]                    ; the length, which must be even
    mov dx, [si+2]
    mov [cs:dos_xmlen], ax
    mov [cs:dos_xmlen+2], dx
    test al, 1
    jnz .badlen
    mov ax, [si+4]
    mov [cs:dos_xmsh], ax           ; the source handle...
    mov ax, [si+6]
    mov [cs:dos_xmso], ax           ; ...and its offset, far or linear
    mov ax, [si+8]
    mov [cs:dos_xmso+2], ax
    mov ax, [si+10]
    mov [cs:dos_xmdh], ax
    mov ax, [si+12]
    mov [cs:dos_xmdo], ax
    mov ax, [si+14]
    mov [cs:dos_xmdo+2], ax
    push cs
    pop ds

    mov ax, [dos_xmsh]
    or ax, ax
    jz .fromconv
    mov ax, [dos_xmdh]
    or ax, ax
    jnz .bothx
    ; --- extended -> conventional ------------------------------------------
    mov ax, [dos_xmsh]
    call dos_xms_row
    jc .badsh
%ifdef DOS_MEGA
    mov ax, [dos_xmso]              ; ...and the move inside the HANDLE
    mov dx, [dos_xmso+2]
    call dos_xms_fits
    jc .badso
%endif
    mov ax, [si+XH_BASE]
    mov dx, [si+XH_BASE+2]
    add ax, [dos_xmso]
    adc dx, [dos_xmso+2]
    mov [dos_xmlin], ax
    mov [dos_xmlin+2], dx
    mov ax, [dos_xmdo]              ; the conventional end, as a far pointer
    mov dx, [dos_xmdo+2]
    mov word [dos_xmdir], 1
    jmp short .run
.fromconv:
    mov ax, [dos_xmdh]
    or ax, ax
    jz .bothc
    call dos_xms_row
    jc .baddh
%ifdef DOS_MEGA
    mov ax, [dos_xmdo]
    mov dx, [dos_xmdo+2]
    call dos_xms_fits
    jc .baddo
%endif
    mov ax, [si+XH_BASE]
    mov dx, [si+XH_BASE+2]
    add ax, [dos_xmdo]
    adc dx, [dos_xmdo+2]
    mov [dos_xmlin], ax
    mov [dos_xmlin+2], dx
    mov ax, [dos_xmso]
    mov dx, [dos_xmso+2]
    mov word [dos_xmdir], 0
.run:
    mov [dos_xmcon], ax             ; ES:SI for the copy, kept whole because
    mov [dos_xmcon+2], dx           ; the chunk loop below moves BOTH ends
.chunk:
    mov ax, [dos_xmlen]
    mov dx, [dos_xmlen+2]
    mov cx, ax
    or dx, dx
    jnz .big
    or cx, cx
    jz .done
    cmp cx, 32768
    jbe .go
.big:
    mov cx, 32768                   ; the slot's own ceiling, and the reason it
.go:                                ; has one is its interrupts-off window
    push cx
    mov es, [dos_xmcon+2]
    mov si, [dos_xmcon]
    mov ax, [dos_xmlin]
    mov dx, [dos_xmlin+2]
    mov di, [dos_xmdir]
    call dos_be_xcopy
    pop cx
    jc .ioerr
    sub [dos_xmlen], cx             ; ...and every pointer forward by what went
    sbb word [dos_xmlen+2], 0
    add [dos_xmlin], cx
    adc word [dos_xmlin+2], 0
    add [dos_xmcon], cx             ; a 32KB step cannot carry a paragraph-
    jnc .chunk                      ; aligned offset past 64KB by more than
%ifdef DOS_MEGA                     ; one segment's worth
    cmp byte [dos_xmdir], 2         ; ...unless the source is LINEAR, which on
    jne .segstep                    ; kern_mega it is for an extended-to-
    inc word [dos_xmcon+2]          ; extended move (SPEC.md 96.15.4): then the
    jmp short .chunk                ; carry is one 64KB step of the high word
.segstep:
%endif
    add word [dos_xmcon+2], 0x1000
    jmp short .chunk
.done:
    clc
    jmp short .out
.bothc:
    mov bl, 0x8E                    ; conventional to conventional: a program
    jmp short .err                  ; with two far pointers can `movsb`
%ifdef DOS_MEGA
    ; --- extended -> extended, kern_mega only (SPEC.md 96.15.4) -------------
    ; kern_dos's transport is int 15h AH=87h, which moves between two linear
    ; addresses directly, so the refusal below is lifted there: DI = 2 says
    ; "ES:SI is the source's LINEAR address, ES its high word" and the chunk
    ; loop above steps it linearly. The windowed DOS never runs on kern_mega
    ; (KERN-MEGA-DOS-PLAN 12.1 item 1), so OSAPI_XMEM_COPY never sees a 2
.bothx:
    mov ax, [dos_xmdh]
    call dos_xms_row
    jc .baddh
    mov ax, [dos_xmdo]
    mov dx, [dos_xmdo+2]
    call dos_xms_fits
    jc .baddo
    mov ax, [si+XH_BASE]
    mov dx, [si+XH_BASE+2]
    add ax, [dos_xmdo]
    adc dx, [dos_xmdo+2]
    mov [dos_xmlin], ax             ; the destination, linear
    mov [dos_xmlin+2], dx
    mov ax, [dos_xmsh]
    call dos_xms_row
    jc .badsh
    mov ax, [dos_xmso]
    mov dx, [dos_xmso+2]
    call dos_xms_fits
    jc .badso
    mov ax, [si+XH_BASE]
    mov dx, [si+XH_BASE+2]
    add ax, [dos_xmso]
    adc dx, [dos_xmso+2]            ; ...and the source, linear, into the
    mov word [dos_xmdir], 2         ; slot the conventional end takes
    jmp .ovl                        ; ...unless it overlaps (below the ret)
%else
.bothx:
    mov bl, 0x8E                    ; ...and extended to extended has no slot
    jmp short .err
%endif
.badsh:
    mov bl, 0xA3
    jmp short .err
%ifdef DOS_MEGA
.badso:
    mov bl, 0xA4                    ; "invalid source offset"
    jmp short .err
.baddo:
    mov bl, 0xA6                    ; "invalid destination offset"
    jmp short .err
%endif
.baddh:
    mov bl, 0xA5
    jmp short .err
.badlen:
    push cs
    pop ds
    mov bl, 0xA7
    jmp short .err
.ioerr:
    mov bl, 0xA8
.err:
    stc
.out:
    pop ds
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret
%ifdef DOS_MEGA
.ovl:
    ; AN OVERLAP WITH THE SOURCE FIRST is a move XMS GUARANTEES (function
    ; 0Bh), and one forward pass of 87h - itself a forward `rep movsw` -
    ; copies the first words over the rest (KERN-MEGA-DOS-PLAN 10.4 item 1).
    ; So it goes TAIL FIRST, in chunks no longer than the distance between
    ; the ends, each of which then cannot overlap itself
    mov bx, [dos_xmlin]
    mov cx, [dos_xmlin+2]
    sub bx, ax
    sbb cx, dx                      ; CX:BX = destination - source
    jb .fwd                         ; the destination first: forward is right
    cmp cx, [dos_xmlen+2]
    ja .fwd
    jb .back
    cmp bx, [dos_xmlen]
    jae .fwd                        ; ...and so is no overlap at all
.back:
    or cx, cx
    jnz .bcap32
    cmp bx, 32768
    jbe .bcap
.bcap32:
    mov bx, 32768
.bcap:
    cmp bx, 1                       ; ONE BYTE APART cannot be split into word
    je .bkerr                       ; moves (KERN-MEGA-DOS-PLAN 10.5's review):
                                    ; A8h, XMS's own "invalid overlap"
    and bl, 0xFE                    ; whole words: 87h moves words
    jz .fwd                         ; the same address: nothing moves
    mov [dos_xmcon], ax             ; the source's base, kept
    mov [dos_xmcon+2], dx
.bk:
    mov ax, [dos_xmlen]
    mov dx, [dos_xmlen+2]
    mov cx, bx
    or dx, dx
    jnz .bkn
    or ax, ax
    jz .bkd
    cmp ax, cx
    jae .bkn
    mov cx, ax
.bkn:
    sub [dos_xmlen], cx             ; what is left is the head; this chunk is
    sbb word [dos_xmlen+2], 0       ; the bytes after it
    mov ax, [dos_xmcon]
    mov dx, [dos_xmcon+2]
    add ax, [dos_xmlen]
    adc dx, [dos_xmlen+2]
    mov es, dx
    mov si, ax
    mov ax, [dos_xmlin]
    mov dx, [dos_xmlin+2]
    add ax, [dos_xmlen]
    adc dx, [dos_xmlen+2]
    mov di, 2
    push bx
    push cx
    call dos_be_xcopy
    pop cx
    pop bx
    jc .bkerr
    jmp short .bk
.bkd:
    jmp .done
.bkerr:
    jmp .ioerr
.fwd:
    jmp .run
%endif
%endif                              ; DOS_EXTCORE


; =============================================================================
; THE DRIVERS, OUT OF THE WAY (SPEC.md 96.17)
; =============================================================================
; A DOS program that wants the Sound Blaster wants to program it ITSELF -
; reset the DSP, set its own IRQ and DMA, own the card completely - and
; os8088's SOUND.DRV is in the way of that in three separate ways: it owns an
; IRQ vector, it owns its DMA channel (1, or an ESS's 3 - SPEC.md 34.5.8),
; and its refill worker is TF_SERVICE, so it KEEPS RUNNING inside the
; bracket by design (SPEC.md 53.2) and can feed
; the DSP while the DOS program is resetting it.
;
; SPEC.md 51.11 is the door: one call, and every driver that owns hardware is
; unloaded - service table, worker, vector, memory and all. Two things follow
; that are worth saying here rather than leaving to be discovered:
;
; THE DRIVER IS NOT MOUNTED ONLY BY SYSTEM.CFG. SPEC.md 51.3.1's boot sniff
; runs an OPL timer dance and sets the sound row's want bit, so a machine with
; a card and NO SYSTEM.CFG AT ALL mounts the driver - which is to say the
; common case on a machine with a sound card is that the driver IS there.
;
; RESUME IS CALLED ON EVERY EXIT PATH, including the ones that refuse before
; the bracket ever opened, because a resume with nothing suspended is free and
; a machine left silent is not. SPEC.md 51.11.1 puts that rule on the caller
; and this is the caller.


%ifndef KD_BACKEND                  ; **NOT UNDER kern_dos** (SPEC.md 96.43):
                                    ; there is no driver there to be a packet
                                    ; driver over.  Everything below reaches
                                    ; the card through OSAPI_DRV_CALL and its
                                    ; buffers through OSAPI_MEM_CLAIM, and
                                    ; 96.40.2's refusal table answers both with
                                    ; CF=1 - so the family does not misbehave
                                    ; on that build, it REFUSES, and what is
                                    ; cut is code that could only ever refuse
; =============================================================================
; THE PACKET DRIVER (SPEC.md 96.23)
; =============================================================================
; A Crynwr packet driver over ETHER.DRV's raw verbs (SPEC.md 72.22). What is
; published is an INTERFACE - a vector in 60h..80h whose handler carries
; `PKT DRVR` at offset 3, and a dozen functions through AH - and what consumes
; it is a DOS application bringing its own TCP/IP.
;
; ETHER.DRV hooks no interrupt vector at all, so there is no IRQ to arbitrate
; and no ISR to hand over: this is a translation and not a negotiation. The
; price is that receive is a POLL underneath an UP-CALL, which is 96.23.4.

%ifdef DOS_NETINLINE
%include "dospkt.inc"                ; the family itself (SPEC.md 96.23.11)
%endif

%ifdef DOS_NONET
; --- kern_mega: NO packet driver (SPEC.md 96.23.11) --------------------------
; Every launch there is the whole machine (dos_mem_fix), and kern_dos has no
; packet driver (96.43) - so dos_pkt_start was unreachable and the family was
; code that could only claim 3KB and give it back. The four the host calls
; answer `no wire` and do nothing.
dos_pkt_want:
    xor cx, cx
    stc
    ret
dos_pkt_bufs:
dos_pkt_start:
dos_pkt_shut:
    ret
%else

%ifdef DOS_NETPART
; -----------------------------------------------------------------------------
; dos_pkt_start - publish the interface: the PART's (SPEC.md 96.23.11), PINNED
;                 first, because from here the program's vector and INT 08h
;                 point into the claim and nothing could re-aim a copy of them
; in:  nothing; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_pkt_start:
    cmp word [dos_pkt_bseg], 0
    je .none                        ; no wire, or a run that never read it
    push ax
    xor ax, ax
    call dos_pkt_rloc               ; pinned until dos_pkt_shut frees it
    mov al, PN_D_START
    call far [dos_pkt_door]
    pop ax
.none:
    ret
%endif

; -----------------------------------------------------------------------------
; dos_pkt_shut - every translated flow closed, the handles and the raw claim
;                given back, and THEN the claim itself (SPEC.md 96.23.5, 96.23.7)
; in:  nothing; out: nothing; every register preserved
;
; ONE CALL AT BOTH OF dos_run's exits and dvw's: it was `dn_shut` then this,
; at each site, and the part's door does the pair (96.23.11). The kernel
; frees a dead instance's claims anyway, so the free is about handing memory
; back MID-SESSION - which is exactly what a DOS window that stays open after
; a run is.
; -----------------------------------------------------------------------------
dos_pkt_shut:
%ifdef DOS_NETPART
    cmp word [dos_pkt_bseg], 0
    je .nobuf
    push ax
    xor ax, ax                      ; pinned for the call INTO it: a run that
    call dos_pkt_rloc               ; refused before dos_pkt_start left it
    mov al, PN_D_SHUT               ; movable
    call far [dos_pkt_door]
    pop ax
%else
    call dn_shut                    ; before the driver that owns its sockets
    call dos_pkt_rawdrop            ; is resumed (the callers' order)
    cmp word [dos_pkt_bseg], 0
    je .nobuf
%endif
    push dx
    mov dx, [dos_pkt_bseg]
    call OSAPI_MEM_FREE
    mov word [dos_pkt_bseg], 0
    pop dx
.nobuf:
    ret

; -----------------------------------------------------------------------------
; dos_pkt_bufs - the two frame buffers, and WHY THEY ARE CLAIMED HERE
;
; **BEFORE THE ARENA, OR NOT AT ALL** (SPEC.md 96.23.7). §96.3 claims
; OSAPI_MEM_AVAIL's whole answer for the program, with no arithmetic between
; the two calls - so by the time a client calls access_type there is no heap
; left and a claim then would always be refused. Taking it here means
; OSAPI_MEM_AVAIL simply answers 3KB less, which is the honest trade and the
; one the DOS program can see.
;
; A MACHINE WITH NO CARD CLAIMS NOTHING, which is the whole point of moving
; them out of bss: 3,028 bytes of a package's bss are zeroed into its heap
; claim at every launch, on every machine, whether or not there is a wire.
; out: [dos_pkt_bseg] = the claim, or 0
; -----------------------------------------------------------------------------
dos_pkt_bufs:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
%ifdef DOS_NETPART
    push si
    push bp
%endif
    mov word [dos_pkt_bseg], 0
    mov byte [dos_pkt_xl], 0
%ifdef DOS_NETPART
    ; --- **NOT FOR A RUN THAT WILL NEVER PUBLISH** (SPEC.md 96.23.11) -------
    ; Reading the part is a disk read, so it is spent only on a run that will
    ; reach dos_pkt_start: not a run in the window (96.54 publishes no packet
    ; driver) and not the whole machine (kern_dos has none, 96.43). dos_run
    ; has fixed the arm before this, so the arm tested is the arm that runs.
%ifdef DVW
    cmp byte [dvw_want], 0
    jne .out
%endif
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    je .out
%endif
    call dos_pkt_want               ; CX = the KB, AL = the translation flag -
    jc .out                         ; ONE reader for the size and the route, so
    mov [dos_pkt_xl], al            ; the figure on the Memory page and the
                                    ; claim taken here cannot disagree
                                    ; (SPEC.md 96.23.7.1, 47 rule 5)
%ifdef DOS_NETPART
    ; --- **THE CODE AND THE BUFFERS ARE ONE CLAIM** (SPEC.md 96.23.11) ------
    ; op_self expands the part into the head of a claim of at least CX KB -
    ; the part's image, its cells and the buffers, which is what
    ; dos_pkt_want sized from the row - so the network spends ONE of the
    ; eight owner claims and [dos_pkt_bseg] still names the whole of it.
    push cx
    mov ah, cl                      ; the floor (a dozen KB: one byte)
    mov si, dos_netrow
    call dos_pself                  ; DX = the claim, CX = the part's length
    pop ax                          ; AX = the KB
    jc .out                         ; **A REFUSAL IS SURVIVABLE**, as the
    mov [dos_pkt_bseg], dx          ; claim's always was: the program runs
                                    ; and nothing is published
    mov es, dx                      ; ...and everything PAST the image is
    mov di, cx                      ; zeroed, for 96.23.7's reason - and
    mov cl, 10                      ; because op_self leaves the stream's
    shl ax, cl                      ; tail in it
    sub ax, di                      ; (dos_pkt_want sized the claim from this
    mov cx, ax                      ; very length, so it is never negative)
    xor al, al
    cld
    rep stosb
    mov al, PN_D_INIT               ; the part's own start: the route, the
    mov ah, [dos_pkt_xl]            ; class it answers on, and where the
    mov bl, [net_cls]               ; environment rows are (OS88LISTEN=,
    mov dx, ds                      ; 96.26.8) - then dn_init on the cable
    mov si, dos_ebuf
    call far [dos_pkt_door]
    cmp byte [dos_pkt_xl], 0
    je .decl
%else
    mov ax, cx
    push cx
    call OSAPI_MEM_CLAIM
    pop cx                          ; (the size, for the zeroing below)
    jc .out                         ; **A REFUSAL IS SURVIVABLE**: the program
    mov [dos_pkt_bseg], dx          ; still runs, the interface is simply not
                                    ; published (dos_pkt_start tests this)
    ; --- AND IT IS ZEROED, which is not tidiness ---------------------------
    ; A heap claim arrives with whatever was last in it, and this one's bytes
    ; go ON THE WIRE: ne_tx pads a short frame but does not touch the length
    ; the caller gave, so a send that staged nothing would put 42 bytes of
    ; somebody else's heap onto the network. It is also where every dn_*
    ; counter, flow row and name slot now lives, and those are read before
    ; they are written.
    mov es, dx
    xor di, di
    mov ax, cx
    mov cl, 10
    shl ax, cl                      ; KB -> bytes; the claim is in KB and the
    mov cx, ax                      ; loop is in bytes
    xor al, al
.z:
    mov [es:di], al
    inc di
    loop .z
    cmp byte [dos_pkt_xl], 0
    je .decl
    call dn_init                    ; ...and THEN the translation's own start,
                                    ; which copies the two MACs in and needs
                                    ; the claim to exist (SPEC.md 96.26.6)
%endif
%ifdef DOSNET_CARD
    mov bh, DRVC_NET                ; **AND THE CARD'S RING POOL, NOW** (SPEC.md
    mov bl, NETV_STATE              ; 72.13.4): ETHER.DRV claims it at a
    call OSAPI_DRV_CALL             ; package's first non-raw verb, and the
                                    ; translation makes its first only once
                                    ; the program runs - in an arena that has
                                    ; taken the heap. NET.DRV, the wire this
                                    ; arm stands in for, holds its buffers
                                    ; from load, so this is the cable's
                                    ; precondition made true on the card
%endif
.decl:
    ; --- **AND IT MAY MOVE** (SPEC.md 66.2, 96.23.7.2) ---------------------
    ; A claim is born PINNED, and this one is taken BEFORE the floor, before
    ; the unmount and before dos_run's posted compaction - so it lands on top
    ; of whatever purgeable caches happen to be sitting at the heap's floor at
    ; that instant, and the pass then purges them out from under it. Left
    ; pinned it is a WALL with a hole under it that the arena - which is ONE
    ; run - cannot use: measured on a 530KB heap with a card, the page
    ; promised 442KB and the program got 400, the 42 being this block's own 3
    ; and 39.5KB of stranded floor.
    ;
    ; ONE WORD NAMES IT and every reader loads [dos_pkt_bseg] fresh, which is
    ; the whole of why the proc is two instructions. In the PART the claim
    ; holds code, and it is declared only HERE - after PN_D_INIT has returned
    ; - so no compaction can move code that is running (SPEC.md 96.23.11).
    mov ax, dos_pkt_reloc
    call dos_pkt_rloc
.out:
%ifdef DOS_NETPART
    pop bp
    pop si
%endif
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_pkt_want - WHETHER the buffers will be claimed, and for how much
; out: CF = 0 and CX = the KB, AL = 1 if this is the translation route;
;      CF = 1 = nothing will be claimed
; clobbers: AX, CX (the outputs), flags
;
; **ONE READER, because two questions are asked of it** (SPEC.md 96.23.7.1):
; `dos_pkt_bufs` takes the claim and `dos_mem_arena` has to subtract it from
; the figure on the glass, and a figure computed from a second opinion is how
; SPEC.md 96.36.7 came to promise memory the launch never handed over.
;
; **A CARD THE LAUNCH IS ABOUT TO UNMOUNT IS NOT A WIRE** (96.36.7.3). The
; Memory page's `Network` box is a request to let DRVC_NET go, and
; `dos_drv_take` grants it BEFORE the program is loaded - so buffers claimed
; for a packet driver with no card behind it are three kilobytes spent on an
; interface that cannot be published. The cable is DRVC_FILE and has no box,
; so that route is unaffected.
; -----------------------------------------------------------------------------
dos_pkt_want:
    push bx
    ; --- **THE BOX GATES BOTH ROUTES** (SPEC.md 96.36.7.4) ------------------
    ; Cleared - which is the default now - means no packet driver at all, on
    ; the card and on the cable alike, so neither the claim nor the part is
    ; ever read. Asked FIRST, so a cleared box costs no net_find either.
    cmp byte [dos_mnet + OS88UI_CK_ON], 0
    je .no
    call net_find                   ; **EITHER WIRE** (SPEC.md 96.26.1): the
    jc .no                          ; CARD if there is one and the CABLE if
                                    ; there is not - net_find's own preference
                                    ; order, for its own reason
    ; --- WHICH ROUTE, AND IT IS DECIDED BY WHICH WIRE ----------------------
    ; A card carries frames, so the packet driver hands the client's frames
    ; straight to it. The cable carries SOCKETS and no frames at all (SPEC.md
    ; 72.22.3), so on that wire the frames are TRANSLATED - the endpoint in
    ; dosnet.inc terminates the client's TCP and re-opens it as a socket
    ; (96.26.3).
%ifndef DOSNET_CARD                 ; ...and this knob forces the translation
    cmp byte [net_cls], DRVC_NET    ; where a card is present, which is the
    jne .xlate                      ; only way it can be DRIVEN until the
                                    ; harness has a cable partner
                                    ; (DOS-CABLE-NET-PLAN 7.0).
                                    ; **DRVC_NET IS THE CARD.** The cable is
                                    ; DRVC_FILE - it moved there when it
                                    ; started serving a volume (SPEC.md 62.9)
                                    ; and the comment at the constant's own
                                    ; definition still says "the parallel
                                    ; link", which is how this compare got
                                    ; written the wrong way round once
    call dos_spmask                 ; BL = what the launch will really let go
    test bl, 1 << DRVC_NET
    jnz .no                         ; ...and the card is one of them
%ifdef DOS_NETPART
    mov cx, PKB_CARDB               ; ...and the two want DIFFERENT amounts
%else
    mov cx, PKB_CARDKB
%endif
    xor al, al
    jmp short .size
%endif
.xlate:
%ifdef DOS_NETPART
    mov cx, PKB_XLB                 ; the translation wants the staging frame
%else
    mov cx, PKB_XLKB
%endif
    mov al, 1                       ; and its own state as well
.size:
%ifdef DOS_NETPART
    ; --- ...AND IN THE PART, THE CODE AND ITS CELLS TOO (SPEC.md 96.23.11) ---
    ; The claim is the part's image (the row's own length, which is where the
    ; part's cells begin), PN_STATE of cells, and the buffers - in BYTES until
    ; here, so the one rounding is the claim's own.
    add cx, [dos_netrow + OP_R_LEN]
    jc .no
    add cx, PN_STATE + 1023
    jc .no
    mov cl, ch
    xor ch, ch
    shr cl, 1
    shr cl, 1                       ; CX = KB
%endif
    pop bx
    clc
    ret
.no:
    xor cx, cx
    pop bx
    stc
    ret

; -----------------------------------------------------------------------------
; dos_pkt_reloc - the packet buffers have moved (SPEC.md 66.2)
; in:  BX = the base they WERE at, DX = the one they are at now; DS = CS =
;      ours, ES = KERNEL_SEG
; out: nothing
;
; Every reader of the claim loads [dos_pkt_bseg] fresh - `mov es,
; [dos_pkt_bseg]` at the four frame sites, `mov ds, [dos_pkt_bseg]` at the two
; translation ones, and dos_pkt_tick's own `mov ax, [dos_pkt_bseg]` - so there
; is exactly one word to write and no derived segment anywhere to recompute.
; The offsets inside it (PKB_RX, PKB_STK, the dn_* rows) are OFFSETS and do
; not move with the base.
; -----------------------------------------------------------------------------
dos_pkt_reloc:
    mov [dos_pkt_bseg], dx
    ret

; -----------------------------------------------------------------------------
; dos_pkt_rloc - declare the buffers movable (AX = dos_pkt_reloc) or pin them
;                again (AX = 0)
; in:  AX; out: nothing; every register AND the flags preserved
;
; **THE PIN IS NOT TIDINESS: THE PRIVATE STACK IS IN THIS CLAIM.**
; `dos_pkt_tick` switches SS:SP into it from inside the `int 08h` chain the
; moment [dos_pkt_raw] is set, and a compaction runs a `rep movsw` with
; interrupts on - so a block that moved with the ISR's stack in it would
; return through a frame the copy had just walked past. [dos_pkt_raw] is
; exactly the ISR's own test, so it is exactly the right bracket: the buffers
; are movable for the whole of dos_run's pass, which is where the 39.5KB is,
; and pinned for the handle's lifetime, which is where the danger is.
; -----------------------------------------------------------------------------
dos_pkt_rloc:
    pushf
    push ax
    push dx
    mov dx, [dos_pkt_bseg]
    or dx, dx
    jz .out                         ; refused, or already given back
    call OSAPI_MEM_MOVABLE
.out:
    pop dx
    pop ax
    popf
    ret


%endif                              ; DOS_NONET
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
; -----------------------------------------------------------------------------
; dos_drv_take - the hardware drivers, out of the way; BLASTER= from what they
;                say on the way past
; in:  inside the bracket, on the exclusive task
; out: nothing; [dos_blaster] is a string or an empty one
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_spmask - OSAPI_DRV_SUSPEND's mask, out of the Memory page's check boxes
; out: BL = the bitmap; every other register preserved
;
; SPEC.md 51.11.4: BL names the classes this call lets go of ANYWAY, bit
; `1 << class`, and 0 is the sweep's own default - `DRVC_DISK`, `DRVC_FILE` and
; `DRVC_NET` left standing because a fullscreen program normally wants them.
; An UNTICKED box is the user saying this program does not, which is the whole
; reason the slot takes a mask (SPEC.md 96.36.7): `dos_mem_arena` already adds
; that class's KB to the figure it prints, and without this the figure was a
; promise the launch did not keep.
;
; **ARM 0 ONLY, AND THAT IS NOT TIDINESS.** The boxes are `DOS_MEM_IN`'s
; controls and are greyed on the other arm - but arm 1 SHUTS THE OS DOWN, and
; the way back is a hibernation image on a fixed disk (SPEC.md 87.2). Letting
; `DRVC_DISK` go there takes `hb_pick`'s volume out from under the image it is
; about to write, on the machine whose hard disk is driver-backed. The other
; arm needs no mask anyway: it gives the program the whole machine, so every
; driver goes regardless of what is named here.
; -----------------------------------------------------------------------------
dos_spmask:
    push ax
    xor bl, bl
    cmp byte [dos_keepc], DOS_MEM_IN
    jne .out
    cmp byte [dos_mhdd + OS88UI_CK_ON], 0
    jne .net
    ; **...UNLESS THE PROGRAM ITSELF IS ON ONE** (SPEC.md 96.36.7.3).
    ; `dos_run` takes the drivers out BEFORE `dos_load`, so letting `DRVC_DISK`
    ; go when DOS is standing on a driver-backed volume unmounts the disk
    ; the program is about to be read off. DOS would then refuse its own
    ; launch with DER_READ, for a request it could simply not grant - and
    ; SPEC.md 96.36.7.1 is explicit that the tick is a REQUEST rather than a
    ; report, so one that cannot be met is not met. A BIOS-reached hard disk
    ; (VT_BIOS, VK_FIXED) is not this class's at all and is unaffected.
    mov al, [dos_vol]
    call OSAPI_VOL_KIND         ; AH = VT_BIOS / VT_DRIVER / VT_FILE
    jc .hdd                     ; no such volume: nothing to protect
    cmp ah, VT_DRIVER
    je .net                     ; ours - keep the class, and say nothing: the
                                ; figure below asks THIS routine, so the page
                                ; cannot promise what the launch will not do
.hdd:
    or bl, 1 << DRVC_DISK
.net:
    cmp byte [dos_mnet + OS88UI_CK_ON], 0
    jne .out
    or bl, 1 << DRVC_NET
.out:
    pop ax
    ret

dos_drv_take:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp byte [dos_drvout], 0
    jne .out                        ; **ALREADY OUT** (SPEC.md 96.35): dos_run
                                    ; takes them once, before the arena, and
                                    ; the arm-3 path (dos_lbfill) asks again on
                                    ; its own way - a second ask would find
                                    ; nothing suspended and wipe the BLASTER=
                                    ; the first went and asked the card for
    mov byte [dos_blaster], 0
    push ds
    pop es
    mov di, dos_dqbuf
    mov al, 1
    call dos_spmask                 ; ...and the skip list, WHICH IS THE PAGE'S
                                    ; OWN BOXES (SPEC.md 51.11.4, 96.36.7.3).
                                    ; It was `xor bl, bl` - the default list,
                                    ; where the hard disk, the RAM disk and the
                                    ; card all stay - under a comment saying
                                    ; the boxes were what set bits here. They
                                    ; never did: the slot shipped and the
                                    ; caller was never wired to it, so an
                                    ; unticked box added its class's KB to the
                                    ; figure on the glass and changed nothing
                                    ; about the launch
    call OSAPI_DRV_SUSPEND          ; CX = records
    jc .out                         ; nothing moved: HIBER.DRV could not be
                                    ; read (SPEC.md 51.11), and there is
                                    ; nothing to put back either
    mov byte [dos_drvout], 1
    jcxz .out
    mov si, dos_dqbuf
.rec:
    cmp byte [si+DQ_CLASS], DRVC_SOUND
    je .sound
    add si, DQ_SIZE
    loop .rec
    jmp short .out
.sound:
    call dos_blaster_set
.out:
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)

; -----------------------------------------------------------------------------
; dos_drv_back - ...and back again
; out: nothing; every register preserved
; -----------------------------------------------------------------------------
dos_drv_back:
    push ax
    push bx
    push cx
    push dx                         ; DX AND SI ARE THE SLOT'S TO CLOBBER and
    push si                         ; the header above has always claimed
                                    ; otherwise. It was true at the two call
                                    ; sites in `dos_run`, which banks both at
                                    ; its own entry, and `dos_wake` banks
                                    ; nothing - so the third one below makes
                                    ; the sentence load-bearing
    push di
    push es
    push ds
    pop es
    xor di, di
    xor al, al
    xor bl, bl                      ; the resume puts back what [hb_susp] says
                                    ; and reads no mask - but the register is
                                    ; stored either way, so a stale one would
                                    ; outlive the call that meant it
    call OSAPI_DRV_SUSPEND
    mov byte [dos_drvout], 0
    jmp dos_hpop_7                  ; the host's shared epilogue (96.54.8)

; -----------------------------------------------------------------------------
; dos_blaster_set - "BLASTER=A220 I5 D1 T4" from the record at SI
; in:  SI = a DQ record whose class is DRVC_SOUND
; out: nothing; [dos_blaster] written
;
; THE WINDOW'S, and dos_hex3/dos_dec2 with it (SPEC.md 96.44.15): its one
; caller is dos_drv_out above. The CELL it writes is the core's, because the
; core's environment builder reads it - and `kern_dos` is handed the row the
; DOS.O88 built here (KDL_F dos_blaster), so it never needed the writer.
;
; THE TYPE IS DERIVED FROM THE DSP VERSION, which is what every DOS program
; that reads this variable expects: 1 is an original Sound Blaster, 3 a 2.0,
; 4 a Pro and 6 an SB16. Getting it wrong does not stop a program running -
; almost all of them only parse A, I and D - but a program that picks its
; stereo path off T would pick the wrong one.
; -----------------------------------------------------------------------------
dos_blaster_set:
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    pop es
    cld
    mov di, dos_blaster
    mov bx, si
    mov si, dos_s_blast             ; "BLASTER=A"
.hdr:
    lodsb
    or al, al
    jz .port
    stosb
    jmp short .hdr
.port:
    mov ax, [bx+DQ_A]               ; the base port, in hex as DOS writes it
    call dos_hex3
    mov al, ' '
    stosb
    mov al, [bx+DQ_B]               ; THE IRQ, IF THERE IS ONE TO HAVE. The
    cmp al, 0xFF                    ; driver finds it at attach (SPEC.md
    je .noirq                       ; 34.5.7) but a card refused its line
    push ax                         ; (34.5.6) answers none - and a BLASTER=
    mov al, 'I'                     ; naming the
    stosb                           ; WRONG line sends a program to wait on an
    pop ax                          ; interrupt that never comes, where its
    call dos_dec2                   ; absence sends it to its own default and
    mov al, ' '                     ; lets it own the guess (SPEC.md 96.17.1)
    stosb
.noirq:
    mov al, 'D'
    stosb
    mov al, [bx+DQ_B+1]
    call dos_dec2
    mov al, ' '
    stosb
    mov al, 'T'
    stosb
    mov al, [bx+DQ_C+1]             ; the DSP's MAJOR version
    cmp al, 4
    jb .t3
    mov al, '6'                     ; 4.xx is an SB16
    jmp short .temit
.t3:
    cmp al, 3
    jb .t2
    mov al, '4'                     ; 3.xx is a Pro
    jmp short .temit
.t2:
    cmp al, 2
    jb .t1
    mov al, '3'                     ; 2.xx
    jmp short .temit
.t1:
    mov al, '1'                     ; ...and anything older is the original
.temit:
    stosb
    xor al, al
    stosb
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_hex3 - AX's low twelve bits as three hex digits at ES:DI (DI advanced)
; -----------------------------------------------------------------------------
dos_hex3:
    push ax
    push bx
    push cx
    push dx
    mov dx, ax
    mov cx, 3
.next:
    mov ax, dx
    push cx
    dec cx
    shl cx, 1
    shl cx, 1                   ; CL = 8, then 4, then 0
    shr ax, cl
    pop cx
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    stosb
    loop .next
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_dec2 - AL (0..99) as one or two digits at ES:DI
; An SB16 can be on IRQ 10, so one digit is not enough and the second one is
; four instructions.
; -----------------------------------------------------------------------------
dos_dec2:
    push ax
    push bx
    cmp al, 99                      ; A TWO-DIGIT EMITTER MUST NEVER EMIT A
    jbe .ok                         ; LETTER, and this one did: handed 255 -
    mov al, 99                      ; which is the sound driver's "no IRQ
.ok:                                ; discovered yet" - it divided to 25 and 5
    cmp al, 10                      ; and wrote '0'+25, so a BLASTER= read
    jb .one                         ; `I5` where it meant 255
    xor ah, ah
    mov bl, 10
    div bl
    push ax
    add al, '0'
    stosb
    pop ax
    mov al, ah
.one:
    add al, '0'
    stosb
    pop bx
    pop ax
    ret
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_setname - [dos_fname] into the record at SI
; in:  SI = the record; out: nothing, every register preserved
; -----------------------------------------------------------------------------
dos_fh_setname:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov di, si
    add di, FH_NAME
    mov si, dos_fname
    mov cx, 13
    cld
    rep movsb
    pop es
    pop di
    pop si
    pop cx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_close - AH=3Eh's body: the record at SI, index BL, is closed
; out: CF=0; CF=1 with AL = a DOS error code, the record freed either way
;
; ONE BODY FOR THE CALL AND FOR THE EXIT (SPEC.md 96.52): a process that
; terminates has every handle closed for it, which is DOS's rule and was not
; this DOS's - the dirty window a program left behind was never written, and
; the next launch's dos_fh_setup threw it away. dos_fh_sweep is the exit's
; loop over this.
; -----------------------------------------------------------------------------
dos_fh_close:
    cmp bl, [dos_wown]
    jne .clnw
    call dos_fh_flush
    jnc .clnw
    push ax                         ; **AND THE HOLD STILL ENDS** (SPEC.md
    call dos_fh_commit              ; 96.53): this record is freed below, so
    pop ax                          ; nothing - not even dos_fh_sweep - could
    stc                             ; reach its token again, and every window
    jmp short .out                  ; before the failed one stays pending. The
                                    ; flush's AL is the answer
.clnw:
    test byte [si+FH_FLAGS], FHF_WRITE
    jz .done
    test byte [si+FH_FLAGS], FHF_MADE | FHF_INPLC
    jnz .done                       ; ...OR OPENED, WHICH IS THE SAME ANSWER
                                    ; HERE and is why this is one immediate
                                    ; rather than a second test: an AH=3Dh
                                    ; handle never created anything, so
                                    ; "created and never written" is not a
                                    ; state it can be in - and touching its
                                    ; file would truncate the one it just
                                    ; overwrote in place (SPEC.md 96.11.6)
    call dos_fh_touch               ; created, never written: DOS leaves a
    jc .out                         ; zero-length file and so does this
.done:
    call dos_fh_commit              ; ...and its held stream, if it has one
.out:
    mov byte [si+FH_FLAGS], 0       ; flags only: CF is the answer
    ret

; dos_fh_commit - the held stream of the record at SI, if it ever streamed,
; ends: its FAT, its link and its entry go to the disk (SPEC.md 96.53).
; dos_fh_cmall ends whichever stream is held - AH=0Dh's and AH=68h's.
; out: CF=0; CF=1 with AL = 5. clobbers: AX
;
; ONLY THE STREAMING HANDLE'S OWN CLOSE, because the hold is the MACHINE's,
; one at a time: a program that opens and closes read files while it writes
; one output - a compiler with its includes - would otherwise end its output's
; hold at every close, and every flush after would be a cold one.
dos_fh_commit:
    cmp word [si+FH_TOK], 0
    je dos_fh_cmret                 ; never streamed: CF=0 off the compare
dos_fh_cmall:
    push cx
    push di
    xor cx, cx                      ; CX = 0 is WRITE_SEQ's CLOSE
    call dos_be_wseq
    pop di
    pop cx
    jnc dos_fh_cmret
    mov al, 5
dos_fh_cmret:
    ret

; dos_fh_sweep - close every handle still open, as DOS does at a terminate.
; No error goes anywhere: there is no program left to tell.
; clobbers: AX, BX, SI
dos_fh_sweep:
    mov si, dos_fhtab
    xor bx, bx
.l:
    test byte [si+FH_FLAGS], FHF_USED
    jz .n
    call dos_fh_close
.n:
    add si, FH_SIZEOF
    inc bx
    cmp bl, DOS_NFH
    jb .l
    ret

; -----------------------------------------------------------------------------
; dos_fh_touch - make the zero-length file the record at SI names
; in:  SI = the record; out: CF=1 with AL = a DOS error code
; -----------------------------------------------------------------------------
dos_fh_touch:
    call dos_fh_at                  ; where the NAME was resolved, not where
    jc .err                         ; the machine happens to stand now
    push bx
    push cx
    push dx
    push si
    push es
    push ds
    pop es                          ; a count of 0 reads no buffer, but ES:BX
    xor bx, bx                      ; still has to be an address
    xor cx, cx
    xor dx, dx
    add si, FH_NAME
    call dos_be_write
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    jc .err
    or byte [si+FH_FLAGS], FHF_MADE
    clc
    ret
.err:
    mov al, 5
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_rdloop - AH=3Fh's body
; in:  SI = the record, CX = the bytes wanted, DX = the offset in the
;      PROGRAM's segment, [bp] = its DS
; out: CF=0 with AX = the bytes delivered (0 = end of file); CF=1 with AL = a
;      DOS error code
;
; THE COUNT LIVES IN BX and not in CX, because dos_fh_fill answers a chunk
; size in CX and `rep movsb` eats it - a loop counter in the same register as
; the primitive's answer is one that reads as a short read.
; -----------------------------------------------------------------------------
dos_fh_rdloop:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov di, dx                      ; ES:DI walks the PROGRAM's buffer
    mov es, [bp]
    xor dx, dx                      ; ...and DX counts what has been delivered

    mov ax, [si+FH_SIZE]            ; NEVER PAST THE END, which is what turns a
    sub ax, [si+FH_POS]             ; read loop round: DOS answers short and
    mov bx, ax                      ; then 0, and a program reads until 0
    mov ax, [si+FH_SIZE+2]
    sbb ax, [si+FH_POS+2]
    jc .rdone                       ; the position is past the size
    jnz .rcap                       ; 64KB or more left, so the ask binds
    cmp bx, cx
    jae .rcap
    mov cx, bx
.rcap:
    mov bx, cx
.rchunk:
    or bx, bx
    jz .rdone
    call dos_fh_fill                ; AX = the offset in the window, CX = what
    jc .rerr                        ; is there
    jcxz .rdone                     ; end of file inside the walk
    cmp cx, bx
    jbe .rcopy
    mov cx, bx
.rcopy:
    push cx
    push si
    push ds
    mov si, ax
    mov ax, [dos_wseg]
    mov ds, ax
    cld
    rep movsb                       ; the window -> the program's buffer, and
    pop ds                          ; DI is left advanced, which is the point
    pop si
    pop cx
    sub bx, cx
    add dx, cx
    add [si+FH_POS], cx
    adc word [si+FH_POS+2], 0
    jmp short .rchunk
.rdone:
    mov ax, dx
    clc
    jmp short .rout
.rerr:
    stc
.rout:
    jmp dos_popb_6                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_wrloop - AH=40h's body for a file handle
; in:  SI = the record, CX = the bytes, DX = the offset in the PROGRAM's
;      segment, [bp] = its DS
; out: CF=0 with AX = the bytes written; CF=1 with AL = a DOS error code
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_fh_shrink - make the file end at the handle's position (SPEC.md 96.11.6.2)
; in:  SI = the record, FH_POS < FH_SIZE
; out: CF=0; CF=1 with AL = a DOS error code
;
; **A FULL REWRITE THROUGH TODAY'S MACHINERY, AND NOT A KERNEL TRUNCATE.**
; Nothing published can make a file smaller, and the slot that would was
; written, assembled and measured at 271 bytes of .cold plus a cell and a
; thunk - costed in docs/plans/DOS-EXEC-PLAN.md and deliberately not taken,
; because a DOS package growing the resident kernel a couple of hundred bytes
; at a time is how a machine that boots on 128KB stops doing so. So the prefix
; is copied out under a temporary name and the two are swapped, which is what a
; DOS utility does by hand - and every door it needs was already here:
; read-at, write, append, delete and rename.
;
; Three arms, cheapest first, and the first two are not micro-optimisations:
; the general one wants the kept prefix's own size in FREE SPACE, which is the
; one thing a real truncate never asks for.
;
;   - nothing kept          -> dos_fh_touch, the zero-length replace a 3Ch
;                              handle that writes nothing already leaves;
;   - a prefix that FITS    -> one read and one replace: no temporary, no free
;     the window               space, and no window where the file is missing;
;   - otherwise             -> the copy.
;
; WHAT THE COPY COSTS is worth stating rather than discovering: it reads and
; writes every kept byte, so truncating a 200KB file is a 200KB copy where DOS
; rewrites one directory entry, and it needs that much room on the volume. It
; also has one instant - between the delete and the rename - where the data
; exists only under the temporary name. Both are the price of not spending the
; kernel bytes, and both are absent from the two arms above.
dos_trncn: db 'OS88TRNC.$$$', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
; -----------------------------------------------------------------------------
dos_fh_shrink:
    push bx
    push cx
    push dx
    push di
    push es

    call dos_fh_flush               ; the window is about to describe a file
    jc .terr                        ; that has been rewritten under it
    call dos_fh_commit              ; ...and it reads its own prefix BY NAME
    jc .terr
    mov byte [dos_wown], 0xFF
    mov byte [dos_wfill], 0
    mov word [dos_wlen], 0

    call dos_fh_at                  ; THE BYTES GO WHERE THE FILE IS (96.6.2),
    jc .terr                        ; and one stand covers the whole rewrite
    call .body
    jc .terr

    mov ax, [si+FH_POS]             ; the record last, as everywhere else here
    mov [si+FH_SIZE], ax
    mov ax, [si+FH_POS+2]
    mov [si+FH_SIZE+2], ax
    clc
    jmp short .tout
.terr:
    mov al, 5                       ; access denied - DOS's own answer for a
    stc                             ; write that could not be made
.tout:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; --- the rewrite, standing on the file's own volume -------------------------
.body:
    mov ax, [si+FH_POS]
    or ax, [si+FH_POS+2]
    jnz .b0
    jmp dos_fh_touch                ; nothing kept: the zero-length replace
.b0:
    ; --- THE KERNEL TRUNCATES, where it can (SPEC.md 18.4.7.5) -------------
    ; OSAPI_FILE_WRITE_AT with CX=0 ends the file at a CLUSTER boundary, so
    ; the cluster the new end falls in is read into the window first, the file
    ; is cut at its start, and the kept part of it goes back on as an append
    ; - which the cut size, a cluster multiple, is exactly what APPEND wants.
    ; No temporary, no free space, and no instant where the data is under
    ; another name. kern_small has no such door and a redirected volume
    ; refuses it: either answers CF=1 BEFORE touching the file, and the three
    ; arms below are what they always were
    ;
    ; **BUT THE CUT COMMITS BEFORE THE APPEND**, so an append that fails
    ; leaves the file at C. That is only worth it where the other arm is the
    ; copy, or where there is no append: a prefix that FITS the window and
    ; does not end on a cluster goes to .b1's one replace, which a failure
    ; leaves the old file whole
    cmp word [si+FH_POS+2], 0
    jne .bcut
    call dos_fh_cb
    dec ax
    test ax, [si+FH_POS]            ; a cluster multiple: the cut is all of it
    jz .bcut
    mov ax, [si+FH_POS]
    cmp ax, [dos_wbytes]
    jbe .b1
.bcut:
    call dos_fh_cb
    neg ax
    and ax, [si+FH_POS]             ; C: the new end, rounded down to a
    mov dx, [si+FH_POS+2]           ; cluster (a power of two)
    push ax
    push dx
    call .brd                       ; the cluster C starts, into the window
    pop dx
    pop ax
    jc .b1
    push ax
    push si
    add si, FH_NAME
    xor cx, cx
    call dos_be_wrat                ; ...and the file ENDS at C
    pop si
    pop bx
    jc .b1
    mov cx, [si+FH_POS]
    sub cx, bx                      ; the kept part of that cluster
    jcxz .bz                        ; (CF=0: `sub` did not borrow)
    mov al, 1
    push si                         ; push/pop and not `sub si`: the CF .bput
    add si, FH_NAME                 ; answers is the whole verdict
    call .bput
    pop si
.bz:
    ret
.b1:
    cmp word [si+FH_POS+2], 0
    jne .bcopy
    mov ax, [si+FH_POS]
    cmp ax, [dos_wbytes]
    ja .bcopy
    xor ax, ax                      ; the prefix fits: read it whole...
    xor dx, dx
    call .brd
    jc .bret
    mov cx, [si+FH_POS]             ; ...and put it back as the WHOLE file
    xor al, al
    push si
    add si, FH_NAME
    call .bput
    pop si
    ret

.bcopy:
    push si                         ; a stale temporary is not an error - it is
    mov si, dos_trncn               ; what a machine that lost power mid-shrink
    call dos_be_delete              ; leaves behind
    pop si
    mov word [dos_trnof], 0
    mov word [dos_trnof+2], 0
.bcl:
    mov ax, [si+FH_POS]             ; what is left of the prefix...
    mov dx, [si+FH_POS+2]
    sub ax, [dos_trnof]
    sbb dx, [dos_trnof+2]
    mov cx, ax
    or dx, dx
    jnz .bcfull
    jcxz .bcdone
    cmp cx, [dos_wbytes]
    jbe .bcgo
.bcfull:
    mov cx, [dos_wbytes]            ; ...clamped to one window, which is a
.bcgo:                              ; cluster multiple and so is every offset
    push cx                         ; below it - which is what keeps APPEND's
    mov ax, [dos_trnof]             ; own precondition true right up to the
    mov dx, [dos_trnof+2]           ; last chunk (18.4.4)
    call .brd
    pop cx
    jc .bret
    mov al, 1
    mov dx, [dos_trnof]
    or dx, [dos_trnof+2]
    jnz .bcap
    xor al, al                      ; the FIRST chunk replaces; the rest append
.bcap:
    push cx                         ; **BY THE CHUNK AND NOT BY THE WINDOW.**
    push si                         ; The last chunk is SHORT, so advancing by
    mov si, dos_trncn               ; [dos_wbytes] steps past the prefix's end,
    call .bput                      ; the remaining count goes NEGATIVE and
    pop si                          ; reads as a whole window - which appends
    pop cx                          ; a chunk onto a temporary whose size has
    jc .bret                        ; stopped being a cluster multiple, and
    add [dos_trnof], cx             ; APPEND refuses it (18.4.4). It fails one
    adc word [dos_trnof+2], 0       ; iteration after the mistake, as a write
    jmp short .bcl                  ; error on a file nothing was wrong with
.bcdone:
    push si                         ; the swap. DELETE THEN RENAME is forced -
    add si, FH_NAME                 ; a rename onto a name that exists refuses -
    call dos_be_delete              ; so there is one instant where the data is
    pop si                          ; only under the temporary name
    jc .bret
    push si
    push di
    mov di, si
    add di, FH_NAME
    mov si, dos_trncn
    call dos_be_rename
    pop di
    pop si
.bret:
    ret

; --- .brd - [dos_wbytes] of the file at DX:AX, into the window --------------
.brd:
    push si
    push bx
    push cx
    push es
    mov bx, [dos_wseg]
    mov es, bx
    xor bx, bx
    mov cx, [dos_wbytes]
    add si, FH_NAME
    call dos_be_rdat
    pop es
    pop cx
    pop bx
    pop si
    ret

; --- .bput - CX bytes of the window onto the name at SI; AL != 0 = append ----
.bput:
    push si
    push bx
    push es
    mov bx, [dos_wseg]
    mov es, bx
    xor bx, bx
    or al, al
    jnz .bpa
    xor dx, dx
    call dos_be_write
    jmp short .bpo
.bpa:
    call dos_be_append
.bpo:
    pop es
    pop bx
    pop si
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_wiloop - AH=40h on an AH=3Dh handle: OVERWRITE (SPEC.md 96.11.6)
; in:  SI = the record, CX = bytes, DX = the program's buffer offset
; out: CF=0 with AX = bytes taken (0 = at the end of file); CF=1 with AL = a
;      DOS error
;
; dos_fh_rdloop WITH THE COPY REVERSED, and that is the whole of it: the
; window is a VIEW here rather than dos_fh_wrloop's accumulator, so
; dos_fh_fill already puts it over the position, cluster-aligned, and hands
; back the offset into it. The bytes go in, the window is marked dirty, and
; dos_fh_flush writes the WHOLE window back at [dos_wbase].
;
; IT STOPS AT THE END OF FILE AND ANSWERS THE SHORT COUNT. A write that would
; grow the file is not one OSAPI_FILE_WRITE_AT can make (18.4.7), and a short
; count is what DOS itself answers when the disk fills - so a program that
; checks its return value learns the truth and one that does not is no worse
; off than on a full disk.
; -----------------------------------------------------------------------------
dos_fh_wiloop:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov di, dx                      ; DI walks the PROGRAM's buffer
    mov bx, cx                      ; BX = what is still to go
    xor dx, dx                      ; DX counts what has gone in
    ; --- a seek PAST the end is a GAP, and it is laid FIRST (96.11.6.1) -----
    ; At the TOP rather than down in .igrow, because a write of ZERO bytes is
    ; how DOS spells "the file ends HERE" (96.11.6.2) and .ichunk's own
    ; `or bx, bx` returns before the gap has been looked at. It is the same
    ; test either way: past the end at entry is the only way .igrow could ever
    ; have seen one, the position only moving forward from here.
    test byte [si+FH_FLAGS], FHF_WHOLE
    jnz .ichunk                     ; a COMPRESSED file is the window (96.11.1)
    mov ax, [si+FH_POS+2]
    cmp ax, [si+FH_SIZE+2]
    ja .ihole0
    jb .ishort
    mov ax, [si+FH_POS]
    cmp ax, [si+FH_SIZE]
    ja .ihole0
    je .ichunk
.ishort:                            ; BEFORE the end, where a count of zero is
    or bx, bx                       ; "the file ends HERE" (96.11.6.2) and
    jnz .ichunk                     ; anything else is an ordinary overwrite
    call dos_fh_shrink
    jc .ierr0
    jmp .idone                      ; DX is still 0, and 0 is what DOS answers
.ierr0: jmp .ierr                   ; near doors (SPEC.md 96.7.3), so the
.ihole0: jmp .ihole                 ; error and hole branches here are short
.ichunk:
    or bx, bx
    jz .idone
    call dos_fh_fill                ; AX = the offset into the window, CX =
    jc .ierr0                       ; the bytes of the file that live there
    jcxz .igrow                     ; at the end of file: is there room to GROW
.iroom:                             ; into what the file already owns?
    cmp cx, bx
    jbe .icopy
    mov cx, bx
.icopy:
    push cx
    push si
    push ds
    mov si, di                      ; source: the program's buffer...
    mov di, ax                      ; ...destination: inside the window
    mov ax, [dos_wseg]
    mov es, ax
    cld
    cmp byte [dos_wfil], 0          ; **THE GAP HAS NO SOURCE** (96.11.6.1):
    je .icmov                       ; laying it is this copy with the move
    xor al, al                      ; made a STORE, which is what makes a seek
    rep stosb                       ; past the end reuse of everything here
    jmp short .icput                ; rather than a path of its own
.icmov:
    mov ds, [bp]                    ; dos_fh_wrloop's frame rule, and for its
    rep movsb                       ; reason: [bp] is the program's own DS
.icput:
    pop ds
    cmp di, [dos_wlen]              ; DI is the window offset PAST the copy, so
    jbe .inowid                     ; this is max(extent, offset + copied) -
    mov [dos_wlen], di              ; the growth arm's only way to widen it,
.inowid:                            ; and a no-op for a write inside the file
    mov di, si                      ; the source pointer, advanced by the copy
    pop si
    pop cx
    mov byte [dos_wdirty], 1
    sub bx, cx
    add dx, cx
    add [si+FH_POS], cx             ; ...and the SIZE only where the write has
    adc word [si+FH_POS+2], 0       ; passed it, which is the growth arm below
    push ax                         ; **DX IS THE RUNNING TOTAL** and AX is
    push dx                         ; about to be the answer: the 32-bit
    mov ax, [si+FH_POS]             ; compare below needs both, so both are
    mov dx, [si+FH_POS+2]           ; banked. Measured as a 16-byte write
    cmp dx, [si+FH_SIZE+2]          ; answering a SHORT count
    jb .inogrow
    ja .isetsz
    cmp ax, [si+FH_SIZE]
    jbe .inogrow
.isetsz:
    mov [si+FH_SIZE], ax
    mov [si+FH_SIZE+2], dx
.inogrow:
    pop dx
    pop ax
    jmp short .ichunk
.idone:
    mov ax, dx
    clc
    jmp .iout
; --- ...at the end of file: grow into the cluster the file ALREADY OWNS -----
; OSAPI_FILE_WRITE_AT reaches the end of what is allocated and no further
; (SPEC.md 18.4.7.2), which is exactly the point at which the size becomes a
; cluster multiple - and that is OSAPI_FILE_APPEND's own precondition. So the
; two compose: this arm fills the last cluster's slack, and everything past it
; is the append accumulator's ordinary business.
.igrow:
    test byte [si+FH_FLAGS], FHF_WHOLE
    jnz .idone                      ; a COMPRESSED file is the window (96.11.1)
                                    ; and cannot be grown a byte at a time
    mov ax, [si+FH_SIZE+2]          ; only at the very END of the file, which
    cmp ax, [si+FH_POS+2]           ; by here is the only place it can be: a
    jne .idone                      ; gap was laid at the top and the position
    mov ax, [si+FH_SIZE]            ; only moves forward. AX ends as the SIZE,
    cmp ax, [si+FH_POS]             ; which is what the arithmetic below wants
    jne .idone

    ; **AND THE ARITHMETIC IS 16-BIT, WHICH IS NOT A SHORTCUT.** [dos_wbase] is
    ; the position rounded DOWN to a cluster and the position is the file's
    ; end, so the end lies inside this very cluster: what the file owns past it
    ; is the rest of THIS cluster and nothing else. So the allocated end is one
    ; cluster on, or exactly here - never a 32-bit quantity, and never DX.
    ;
    ; DX IS THE RUNNING TOTAL of bytes placed, and the first version of this
    ; spent it on the high half of an allocated end that could not need one.
    ; .iappend then added ZERO to what the append took, and a 512-byte write
    ; across the boundary answered 16 - the count a caller reads as a full disk.
    sub ax, [dos_wbase]             ; AX = size - base, 0 .. cluster-1
    jz .iappend                     ; exactly on a cluster boundary: the file
                                    ; owns nothing past its end, and its size
                                    ; is already the multiple APPEND wants
    call dos_fh_cb                  ; ...otherwise the rest of this cluster,
    sub ax, [dos_wlen]              ; less what the window already holds
    jbe .iappend
    mov cx, ax                      ; CX = the room, which .iroom clamps to
    mov ax, [dos_wlen]              ; what is actually left to write - so the
    jmp .iroom                      ; window's extent is grown by the COPY and
                                    ; not by the room offered
.iappend:                           ; the last cluster is FULL, so the size is
                                    ; a cluster multiple and the append path's
                                    ; own precondition holds (18.4.4)
    call dos_fh_flush               ; ...the view first: the accumulator wants
    jc .ierr                        ; an empty window
    and byte [si+FH_FLAGS], ~FHF_INPLC
                                    ; **AND `MADE` IS NOT ASSERTED HERE** any
                                    ; more (96.11.6.3): an OPENED file has it
                                    ; from the open, and a CREATED one reaching
                                    ; this arm through the gap has flushed
                                    ; nothing yet - so setting it would send
                                    ; the first flush to dos_be_append with no
                                    ; file to append to
    mov word [dos_wlen], 0
    push dx                         ; the running total, across the accumulator
    mov cx, bx                      ; what is still to go...
    mov dx, di                      ; ...from where the copy reached
    call dos_fh_wrloop              ; AX = what it took (it preserves DX, but
    pop dx                          ; DX is its INPUT here, so the total rides
    jc .ierr                        ; on the stack rather than in it)
    add ax, dx                      ; ...plus what this loop had already placed
    clc
    jmp short .iout

; --- ...or a seek left a GAP behind it (SPEC.md 96.11.6.1) ------------------
; Laying [SIZE, POS) is the same operation as writing the program's bytes
; there and differs in the SOURCE alone - so this rewinds the position to the
; end of the file and calls ITSELF with the fill flag armed. The slack arm,
; the append accumulator, the window, the flush and the short-count answer are
; all the ones above, and the recursion is one deep by construction: the inner
; call runs with POS == SIZE, which is the case that never reaches here.
.ihole:
    mov ax, [si+FH_POS]
    sub ax, [si+FH_SIZE]
    mov [dos_gapn], ax
    mov ax, [si+FH_POS+2]
    sbb ax, [si+FH_SIZE+2]
    mov [dos_gapn+2], ax
    mov ax, [si+FH_SIZE]            ; ...and each chunk carries the position
    mov [si+FH_POS], ax             ; back up, so when the gap is spent it is
    mov ax, [si+FH_SIZE+2]          ; exactly where the seek left it and no
    mov [si+FH_POS+2], ax           ; target has to be banked
.ihstep:
    call dos_fh_flush               ; **THE WINDOW IS A VIEW AGAIN**, and the
    jc .ierr                        ; ORDER is the whole of it: .iappend takes
    or byte [si+FH_FLAGS], FHF_INPLC ; the flag off to hand the rest of a write
                                    ; to the accumulator, so every step back
                                    ; into the slack arm - the next chunk of
                                    ; the gap, and the program's own bytes
                                    ; after it - wants it back. But what the
                                    ; accumulator holds was APPENDED, and
                                    ; flipping the flag over a dirty window
                                    ; sends those bytes out as a WRITE_AT past
                                    ; the file's allocated end, which is the
                                    ; one thing 18.4.7 refuses. So it goes out
                                    ; first, as the append it is, and the flag
                                    ; moves over an EMPTY window
    mov cx, 0x8000                  ; a chunk is a COUNT, so it is 16-bit; the
    cmp word [dos_gapn+2], 0        ; gap a 42h can name is not
    jne .ihgo
    mov cx, [dos_gapn]
    jcxz .ihend
    cmp cx, 0x8000
    jbe .ihgo
    mov cx, 0x8000
.ihgo:
    push bx                         ; the program's own write, untouched...
    push dx
    push di
    mov byte [dos_wfil], 1          ; ...while the two copy sites STORE
    call dos_fh_wiloop
    mov byte [dos_wfil], 0
    pop di
    pop dx
    pop bx
    jc .ierr
    sub [dos_gapn], ax
    sbb word [dos_gapn+2], 0
    or ax, ax
    jnz .ihstep
    jmp .idone                ; the disk would not take the gap, so
                                    ; nothing of the program's bytes can go in
                                    ; either: 0 with CF=0 is what a full disk
                                    ; has always answered
.ihend:
    jmp .ichunk                     ; spent - and the position is the seek's,
                                    ; so the write is now an ordinary one
.ierr:
    stc
.iout:
    jmp dos_popb_6                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
dos_fh_wrloop:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    mov di, dx                      ; DI walks the PROGRAM's buffer
    mov bx, cx                      ; BX = what is still to go
    xor dx, dx                      ; DX counts what has gone in
.wchunk:
    or bx, bx
    jz .wdone
    call dos_fh_take                ; for a write the window is an accumulator
    jc .werr                        ; rather than a view
    mov cx, [dos_wbytes]
    sub cx, [dos_wlen]
    jnz .wroom
    call dos_fh_flush               ; full - and a full window is a cluster
    jc .werr                        ; multiple, which is what keeps the next
    mov cx, [dos_wbytes]            ; APPEND legal
.wroom:
    cmp cx, bx
    jbe .wcopy
    mov cx, bx
.wcopy:
    push cx
    push si
    push ds
    mov si, di                      ; source: the program's buffer
    mov di, [dos_wlen]              ; destination: the window's free end, read
    mov ax, [dos_wseg]              ; while DS is still OURS
    mov es, ax
    cld
    cmp byte [dos_wfil], 0          ; ...and the gap reaches the accumulator
    je .wcmov                       ; too, whenever it is longer than the last
    xor al, al                      ; cluster's slack (96.11.6.1)
    rep stosb
    jmp short .wcput
.wcmov:
    mov ds, [bp]
    rep movsb
.wcput:
    pop ds
    mov di, si                      ; the source pointer, advanced by the copy
    pop si
    pop cx
    add [dos_wlen], cx
    mov byte [dos_wdirty], 1
    sub bx, cx
    add dx, cx
    add [si+FH_POS], cx             ; a sequential write moves both, and they
    adc word [si+FH_POS+2], 0       ; stay equal, which is the invariant
    add [si+FH_SIZE], cx            ; .fwrite refuses on
    adc word [si+FH_SIZE+2], 0
    jmp short .wchunk
.wdone:
    mov ax, dx
    clc
    jmp short .wout
.werr:
    stc
.wout:
    jmp dos_popb_6                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_isdev - is the name in [dos_fname] a CHARACTER DEVICE? (SPEC.md 96.11.7)
; out: CF=0 with AL = the DOS_DEV_* code; CF=1 = an ordinary file name, AL kept
; clobbers: the flags, and AL only on the CF=0 path
;
; The name has already been through dos_fh_core, so it is BARE, UPPER CASE and
; has lost its drive letter and its folder part - which is exactly the shape a
; device test wants, and is why this is four compares rather than a parser.
;
; **AN EXTENSION IS IGNORED**, because DOS ignores it: `CON.TXT` is the
; console and always has been, which is the rule behind every "you cannot
; call a file CON" a DOS user has ever met. The test is therefore "the three
; letters, then a NUL or a dot".
; -----------------------------------------------------------------------------
dos_fh_isdev:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, dos_devtab
    xor dl, dl                      ; DL = the code, which is the ROW
.one:
    mov di, dos_fname
    mov cl, 3                       ; every device name here is three letters
.cmp:
    mov bl, [si]
    inc si
    cmp bl, [di]
    jne .next
    inc di
    dec cl
    jnz .cmp
    mov bl, [di]                    ; ...and the name has to END there
    or bl, bl
    jz .yes
    cmp bl, '.'
    je .yes
.next:
    inc dl
    mov al, dl                      ; SI may have stopped mid-name, so the next
    mov cl, 2                       ; row is computed rather than walked to
    shl al, cl                      ; (the rows are four bytes each)
    xor ah, ah
    mov si, dos_devtab
    add si, ax
    cmp dl, DOS_NDEV
    jb .one
    stc
    jmp short .out
.yes:
    mov al, dl
    clc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; --- the names it knows (SPEC.md 96.11.7) ----------------------------------
; FOUR BYTES A ROW so the row index IS the DOS_DEV_* code and the walk above
; can COMPUTE the next row rather than walk to it - it stops mid-name on a
; mismatch. Three letters and a pad byte; the pad is never compared, the
; length being the constant 3.
;
; IT LIVES HERE, past the `ret` and inside the core's own gate, and not with
; DOS.O88's other literals: those are in the WINDOW half (`%ifndef
; KD_BACKEND`), which the core build gates out - so a table put beside them
; assembles for DOS.O88 and leaves kern_dos with an undefined symbol.
dos_devtab: db 'CON', 0
            db 'NUL', 0
            db 'PRN', 0
            db 'AUX', 0

; -----------------------------------------------------------------------------
; dos_fh_slot - the record for handle BX
; in:  BX = a DOS handle
; out: CF=0 with SI = the record and BX = the index 0..DOS_NFH-1; CF=1 if it
;      is not an open file handle
; -----------------------------------------------------------------------------
dos_fh_slot:
    push ax
    cmp bx, DOS_FH0
    jb .no
    cmp bx, DOS_FH0 + DOS_NFH
    jae .no
    sub bx, DOS_FH0
    mov ax, FH_SIZEOF
    mul bl
    mov si, ax
    add si, dos_fhtab
    test byte [si+FH_FLAGS], FHF_USED
    jz .no
    mov [dos_fhix], bl              ; THE INDEX LIVES HERE and not in a
    pop ax                          ; register: the window's owner is asked
    clc                             ; for at four call sites down two levels,
    ret                             ; and threading BL through all of them is
                                    ; how one of them ends up holding a count
.no:
    pop ax
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_new - the first free handle
; out: CF=0 with BX = the DOS handle and SI = the record, zeroed; CF=1 if the
;      table is full
; -----------------------------------------------------------------------------
dos_fh_new:
    push ax
    push cx
    push di
    push es
    push ds
    pop es
    mov si, dos_fhtab
    xor bx, bx
.scan:
    test byte [si+FH_FLAGS], FHF_USED
    jz .free
    add si, FH_SIZEOF
    inc bx
    cmp bx, DOS_NFH
    jb .scan
    stc
    jmp short .out
.free:
    mov [dos_fhix], bl
    ; --- AND THE WINDOW CANNOT SURVIVE ITS FILE (SPEC.md 96.11.5) ----------
    ; dos_fh_take decides whether the window already holds the right bytes by
    ; comparing this record's INDEX with the window's owner - so a handle that
    ; is closed and another opened lands on the same index, `take` says "mine",
    ; and the new file is read out of the old one's window. The close has
    ; already flushed anything dirty, so disowning is the whole of it.
    cmp bl, [dos_wown]
    jne .nowin
    mov byte [dos_wown], 0xFF
    mov byte [dos_wfill], 0
    mov word [dos_wlen], 0
.nowin:
    mov di, si                      ; a reused slot must not inherit a stale
    mov cx, FH_SIZEOF               ; name or position from the last program's
    xor al, al                      ; file
    cld
    rep stosb
    add bx, DOS_FH0
    clc
.out:
    pop es
    pop di
    pop cx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_name - copy a program's ASCIZ path into [dos_fname], 8.3 and upper
; in:  DX = the offset in the PROGRAM's segment, [bp] = its DS
; out: CF=0; CF=1 with AL = a DOS error code for a path this wave cannot walk
; clobbers: nothing else
;
; A DRIVE LETTER IS TAKEN OFF THE NAME AND OBEYED (SPEC.md 96.6.2), not
; thrown away: it goes in [dos_fdrv] and dos_fh_enter below stands on that
; volume for the length of the call.  A SUBDIRECTORY is refused with "path not
; found" rather than silently opened in the current one, which would hand the
; program the wrong file under the right name.
;
; IT USED TO BE DROPPED, on the reasoning that "a program that names its own
; drive is naming ours" - which was true of a DOS that had one volume and
; stopped being true the day AH=0Eh really switched (SPEC.md 96.6.1).  What it
; cost is in tests/dostrap/drvname.asm: standing on B:, this DOS answered
; A:*.* with B:'s own directory and answered C:*.* on a machine that HAS no
; C:, both of them reporting success.
; -----------------------------------------------------------------------------
dos_fh_name:
    push di
    push ax                         ; ...AND PUT AX BACK BEFORE THE CALL: this
    mov ax, [bp]                    ; routine's own answer on CF=1 is AL, so the
    mov [dos_fnseg], ax             ; segment travels in a word rather than in
    pop ax                          ; the register that carries the error code
    mov di, dos_fname
    call dos_fh_core
    jc .nout
%ifdef DOSTRACE
    call dos_tr_name_in             ; WHICH FILE - AH/AL alone cannot say, and
%endif                              ; that is the question a field trace asks
    call dos_fh_enter               ; ...and WHICH DRIVE, which is the same
    jc .nout                        ; question one level up (SPEC.md 96.6.2)
    clc
.nout:
    pop di
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_core - the parse itself, from ANY segment into ANY buffer of ours
; in:  [dos_fnseg]:DX = the ASCIZ name, DI = a 13-byte buffer in OUR segment
; out: CF=0 with the name copied, [dos_fdrv] the drive it named (0xFF = none)
;      and [dos_fabs] whether it carried a leading separator; CF=1 with AL = a
;      DOS error code for a path this wave cannot walk
; clobbers: AL
;
; IT IS SEPARATE FROM dos_fh_name BECAUSE THERE ARE THREE CALLERS AND ONLY ONE
; OF THEM IS AN INT 21h ARGUMENT. AH=56h's second name is in the program's ES
; rather than its DS, and the built-in commands (SPEC.md 22.24) parse names
; out of OUR OWN segment - so the source is a far pointer and the destination
; is a parameter, and dos_fh_name is what adds the trace hook and the drive
; bracket on top for the calls that want them.
; -----------------------------------------------------------------------------
dos_fh_core:
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, dx
    mov ds, [es:dos_fnseg]          ; ...wherever the name really is
    mov cx, 13

    mov byte [es:dos_fdrv], 0xFF    ; "C:NAME" - the letter comes off the name
    cmp byte [si+1], ':'            ; and goes in [dos_fdrv], where dos_fh_enter
    jne .nodrv                      ; picks it up. A LETTER IS NOT CHECKED HERE:
    mov al, [si]                    ; anything that is not a volume falls out of
    inc si                          ; range and dos_fh_enter refuses it with the
    inc si
    cmp al, 'a'                     ; code a real DOS gives, which is 3 and not
    jb .drvup                       ; 15 (measured - drvname.asm under IBM DOS
    cmp al, 'z'                     ; 3.30)
    ja .drvup
    sub al, 32
.drvup:
    sub al, 'A'
    mov [es:dos_fdrv], al
.nodrv:
    cmp byte [si], '.'              ; ".\NAME" and "./NAME"
    jne .nodot
    cmp byte [si+1], '\'
    je .skip2
    cmp byte [si+1], '/'
    jne .nodot
.skip2:
    inc si
    inc si
.nodot:
    mov byte [es:dos_fabs], 0       ; A LEADING SEPARATOR IS STRIPPED AND
    cmp byte [si], '\'              ; REMEMBERED (SPEC.md 96.12.2): "\" is the
    je .abs                         ; program's own root, and "\NAME" is one
    cmp byte [si], '/'              ; step down from it. An EMBEDDED one is
    jne .copy                       ; still refused below - this wave stands in
.abs:                               ; one directory at a time
    inc si
    mov byte [es:dos_fabs], 1
.copy:
    call dos_fh_split               ; ...AND THE FOLDER PART COMES OFF HERE
                                    ; (SPEC.md 96.12.3). It used to be refused
                                    ; with code 3, which is what stopped a
                                    ; program opening `B:\PRINCE\PRINCE.DAT` -
                                    ; a path it built itself out of AH=47h's
                                    ; own answer, and the shape every Microsoft
                                    ; C program uses
    ;
    ; --- TWO FIELDS WITH A CEILING EACH, NOT ONE BUFFER WITH A LENGTH -------
    ; This counted the 13 bytes of the buffer and refused a name that filled
    ; it. **DOS DOES NOT HAVE THAT BOUND** (SPEC.md 96.12.5): its parser fills
    ; an eleven-byte FCB-shaped field and DISCARDS what will not fit, so
    ; `plysample.bin` is `PLYSAMPL.BIN` and not an error. Measured on IBM DOS
    ; 3.30 with tests/dostrap/longname.asm, which opens the same file by five
    ; spellings and is handed a handle for every one of them.
    ;
    ; The Playroom is the report: PLAYEGA.EXE opens `B:plysample.bin`, gets
    ; code 3, prints `FILE ERROR` and terminates abnormally - which reads as a
    ; program that could not start rather than one file it could not reach.
    ;
    ; CX IS RE-ARMED HERE BECAUSE THE SPLIT SPENDS IT SCANNING, and it is
    ; armed twice now: once for each field.
    mov cx, 8                       ; ...the STEM
.copy2:
    lodsb
    cmp al, '\'                     ; anything left here is a separator
    je .path                        ; dos_fh_split could not remove, which
    cmp al, '/'                     ; means the FOLDER part did not fit - a
    je .path                        ; different failure from a long file name,
    or al, al                       ; and still a refusal
    jz .term
    cmp al, '.'
    je .dot
    call dos_fh_up
    stosb
    loop .copy2
.eat8:
    ; --- eight already: EAT to the dot, which is what DOS discards ----------
    lodsb
    cmp al, '\'
    je .path
    cmp al, '/'
    je .path
    or al, al
    jz .term
    cmp al, '.'
    jne .eat8
.dot:
    mov al, '.'
    stosb
    mov cx, 3                       ; ...and the EXTENSION, the same way
.copy3:
    lodsb
    cmp al, '\'
    je .path
    cmp al, '/'
    je .path
    or al, al
    jz .term
    call dos_fh_up
    stosb
    loop .copy3
.eat3:
    lodsb                           ; a fourth extension character goes the
    cmp al, '\'                     ; same way a ninth stem one does
    je .path
    cmp al, '/'
    je .path
    or al, al
    jnz .eat3
.term:
    xor al, al                      ; 8 + '.' + 3 + NUL is exactly the 13 bytes
    stosb                           ; the buffer holds, so the worst case fits
    jmp short .done
.path:
    mov al, 3
.bad:
    push es
    pop ds
    stc
    jmp short .out
.done:
    push es
    pop ds
    clc
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_up - AL to upper case, because an 8.3 name on the disk is upper case
; out: AL; every other register and the flags' meaning to the caller untouched
;
; A CALL AND NOT A MACRO, for once, because the two field loops above are the
; only callers and a name is parsed once per INT 21h - where an inlined copy
; would be six bytes in each of two places for no measurable call at all.
; -----------------------------------------------------------------------------
dos_fh_up:
    cmp al, 'a'
    jb .out
    cmp al, 'z'
    ja .out
    sub al, 32
.out:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_split - take the FOLDER PART off the name at DS:SI (SPEC.md 96.12.3)
; in:  DS:SI = what is left of the name, ES = ours
; out: SI past the last separator, [es:dos_fhpath] = 1 if there was one and
;      dos_fpbuf holds it, upper-cased and NUL-terminated
; clobbers: AX, BX, CX, SI, flags
;
; THE LAST SEPARATOR IS THE SPLIT, which is the whole of it: everything before
; it is a folder path for dos_fh_enter to walk and everything after is the 8.3
; name to resolve there. A path too long for the buffer is left alone, so the
; copy loop above still refuses it with code 3 rather than walking half of one.
; -----------------------------------------------------------------------------
dos_fh_split:
    push di
    push si                         ; ...and SI walks the scan, because on an
    mov byte [es:dos_fhpath], 0     ; 8086 only BX, BP, SI and DI index
    xor bx, bx                      ; BX = characters before the last separator
    xor cx, cx                      ; CX = how far we have looked
.scan:
    mov al, [si]
    or al, al
    jz .scand
    cmp al, '\'
    je .sep
    cmp al, '/'
    jne .next
.sep:
    mov bx, cx
    inc bx
.next:
    inc si
    inc cx
    cmp cx, DOS_PBUF - 1
    jb .scan
.scand:
    pop si                          ; the start again
    or bx, bx
    jz .out                         ; no folder part, and the common case
    mov byte [es:dos_fhpath], 1
    mov di, dos_fpbuf
    mov cx, bx
    dec cx                          ; the separator itself is not part of it
.cp:
    jcxz .cpd
    mov al, [si]
    cmp al, 'a'
    jb .st
    cmp al, 'z'
    ja .st
    sub al, 32
.st:
    mov [es:di], al
    inc si
    inc di
    dec cx
    jmp short .cp
.cpd:
    mov byte [es:di], 0
    inc si                          ; ...and SI past the separator, which is
.out:                               ; where the 8.3 name starts
    pop di
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_enter - stand the MACHINE where this name resolves, for one call
; out: CF=0, having moved or not; CF=1 with AL = 3 for a drive that is not
;      there
; clobbers: AL, which dos_fh_name has already spent on the name it copied
;
; UNDER DOS A DRIVE LETTER IN A NAME DOES NOT CHANGE THE DEFAULT DRIVE - it
; selects which drive's current directory the name is resolved against, and
; AH=0Eh alone moves the program.  Our back end resolves against whatever is
; MOUNTED, so "resolve elsewhere" is spelled "go there" - and since SPEC.md
; 96.48 it is NOT spelled "and come back": the pair the name resolves against
; is compared with where the machine is standing (`[dos_pvol]`/`[dos_pdir]`),
; so a program that names one drive over and over moves once and not once a
; call.  `dos_fh_leave` no longer restores anything but the folder.
;
; THE CODE FOR A DRIVE THAT IS NOT THERE IS 3 AND NOT 15, which is measured
; rather than reasoned: IBM DOS 3.30 answers AH=4Eh on C: with AX=0003 CF=1 on
; a machine with no hard disk (tests/dostrap/drvname.asm).  15 is what a
; program gets from calls that take a drive NUMBER, and this is not one.
; -----------------------------------------------------------------------------
dos_fh_stand:
    ; --- put the MACHINE on drive DL, in THAT DRIVE's folder (SPEC.md 96.48)
    ; in:  DL = the volume; out: CF=1 = it could not be reached
    ; clobbers: nothing but the flags
    ;
    ; The folder is `[dos_curdir]` for the drive the program is logically on
    ; and the per-drive bank for any other, which is the same pair `AH=0Eh`
    ; moves between - so a name resolves against exactly what a real DOS
    ; would resolve it against, whichever volume happens to be mounted.
    push ax
    push bx
    push dx
    mov bl, dl
    xor bh, bh
    shl bx, 1
    mov ax, [bx+dos_dvcwd]
    cmp dl, [dos_vol]
    jne .have
    mov ax, [dos_curdir]            ; the live drive's folder is not banked
.have:                              ; until something switches away from it
    cmp dl, [dos_pvol]
    jne .go
    cmp ax, [dos_pdir]
    je .ok                          ; **THE TEST THIS SECTION IS ABOUT**: the
                                    ; machine is already standing exactly here,
                                    ; so there is nothing to try
.go:
    mov bl, dl
    mov dx, ax
    call dos_be_goto                ; CF is the answer...
.ok:                                ; ...and a compare that matched left CF=0,
    pop dx                          ; so neither path needs its own tail: the
    pop bx                          ; DOS core's budget (CORE_MAX) is what
    pop ax                          ; paid for AH=47h's stand and the close's
    ret                             ; commit (SPEC.md 96.52, 96.53)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_fh_enter:
    push dx
    mov dl, [dos_fdrv]
    cmp dl, 0xFF
    jne .named
    mov dl, [dos_vol]               ; no letter: the drive the PROGRAM is on,
                                    ; which is not necessarily the one the
                                    ; machine is standing on (SPEC.md 96.48)
.named:
    cmp dl, DVOL_MAX
    jae .bad                        ; past the array - and a non-letter lands
                                    ; here too, 'C'-'A' being the only shape
                                    ; that does not
    call dos_fh_stand
    jc .bad                         ; the volume is not there, and that is the
                                    ; whole of "invalid drive" (SPEC.md 96.6.1)
.none:
    ; --- ...AND THE FOLDER, if the name carried one (SPEC.md 96.12.3) -----
    ; AFTER the drive switch and not before: `B:\PRINCE\X` names a folder on
    ; B:, so walking it while standing on A: would resolve the wrong disk -
    ; which is dos_fh_enter's own ordering rule one level down.
    mov byte [dos_fhkeep], 0
    mov byte [dos_fhmoved], 0
    cmp byte [dos_fhpath], 0
    jne .dowalk
    cmp byte [dos_fabs], 0
    je .nowalk                      ; a bare name, and the common case
    mov byte [dos_fpbuf], 0         ; "\NAME" is the volume ROOT and no
.dowalk:                            ; components, which the walk below does by
                                    ; going to the root and finding nothing to
                                    ; descend - so an absolute name resolves
                                    ; where it says whatever folder we are in
    mov ax, [dos_curdir]            ; banked BEFORE the walk, because
    mov [dos_fhcwd], ax             ; dos_fh_leave is what puts it back and a
    mov byte [dos_fhmoved], 1       ; name must not move the program
    mov si, dos_fpbuf
    mov al, [dos_fabs]
    call dos_walk_at
    jc .nofold
.nowalk:
    pop dx
    clc
    ret
.nofold:
    call dos_fh_home                ; a half-walked path leaves us somewhere
    mov al, 3                       ; the caller never asked to be
    pop dx
    stc
    ret
.bad:
    mov al, 3
    pop dx
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; dos_fh_home - back to the folder dos_fh_enter banked, if it moved us
; clobbers: nothing (AX, BX, DX and the flags are restored)
dos_fh_home:
    cmp byte [dos_fhmoved], 0
    je .out
    pushf
    push ax
    push bx
    push dx
    mov byte [dos_fhmoved], 0
    mov dx, [dos_fhcwd]
    mov bl, [dos_vol]
    call dos_be_goto
    mov [dos_curdir], dx
    pop dx
    pop bx
    pop ax
    popf
.out:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_vol_to - stand on volume AL
; in:  AL = the volume, 0 = A
; out: CF=0 with AL = the volume we WERE on, for the caller to hand back;
;      CF=1, AL untouched, if that volume is not there
; clobbers: nothing else
;
; dos_fh_enter is this with [dos_fdrv]'s policy on top and one place to come
; back to.  The window routines want the bare mechanism instead: they bracket
; a single back-end call, they are reached with no name in hand, and their
; volume is the HANDLE's rather than the call's.
; -----------------------------------------------------------------------------
dos_vol_to:
    ; **THE PROGRAM AND THE MACHINE** (SPEC.md 96.48). `dos_drv_sel` stopped
    ; mounting, so a caller that is about to make a back-end call has to say
    ; so: the read or write resolves where the MACHINE is standing, and
    ; moving only `[dos_vol]` would read the right name off the wrong disk.
    ; `dos_vol_park` is the other half for a caller that is coming HOME - it
    ; restores the program's drive and leaves the machine where the work was,
    ; which is what makes a read loop on one file cost one mount and not one
    ; a call.
    call dos_vol_park
    jc .out
    push ax                         ; AL is the volume we came FROM and the
    push dx                         ; caller banks it; neither pop touches the
    mov dl, [dos_vol]               ; flags, so CF is the stand's own answer
    call dos_fh_stand
    pop dx
    pop ax
.out:
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; --- dos_vol_park - the PROGRAM's drive alone; the machine stays put -------
dos_vol_park:
    push dx
    mov dl, al
    mov al, [dos_vol]
    cmp dl, al
    je .same                        ; already there: the common case, and it
    push ax                         ; costs one compare
    call dos_drv_sel
    pop ax
    cmp dl, [dos_vol]
    jne .no
.same:
    pop dx
    clc
    ret
.no:
    pop dx
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_at - stand the MACHINE where the file of the record at SI is
; out: CF=0; CF=1 = that volume or folder cannot be reached
; clobbers: nothing but the flags
;
; **THE RECORD'S OWN FOLDER, NOT THE DRIVE'S CURRENT ONE** (SPEC.md 96.52).
; A handle used to keep only its volume and re-resolve its bare name in that
; drive's current folder at every refill and flush, so SUB\X.DAT opened from
; the root read the root's X.DAT - or nothing - from its first byte on. The
; record keeps [dos_pdir] from the open now (dos_fh_here), and this stands
; exactly there. It never moves the PROGRAM's drive, so there is nothing to
; walk home afterwards; and when the machine already stands there it is two
; compares, which is the read loop's common case.
; -----------------------------------------------------------------------------
dos_fh_at:
    push ax
    push bx
    push dx
    mov bl, [si+FH_VOL]
    mov dx, [si+FH_DIR]
    cmp bl, [dos_pvol]
    jne .go
    cmp dx, [dos_pdir]
    je .ok
.go:
    call dos_be_goto                ; CF is the answer, and the matched compare
.ok:                                ; left CF=0 (dos_fh_stand's shape)
    pop dx
    pop bx
    pop ax
    ret

; dos_fh_cb - AX = the cluster, in bytes, of the volume the record at SI is on
; (SPEC.md 96.11.11). Asked of that volume the first time a launch needs it -
; standing the machine on the file, so OSAPI_FILE_DFREE answers for it - and
; kept in dos_vcb as the cluster's bytes / 256. A volume that will not say is
; taken as 1KB, as dos_fh_setup takes one, and asked again next time. Keeps
; everything else but the flags
dos_fh_cb:
    push bx
    mov bl, [si+FH_VOL]
    mov bh, 0                       ; BX = the volume, all the way down
    mov ah, [bx+dos_vcb]
    or ah, ah
    jnz .have
    call dos_fh_at
    jc .guess
    push dx
    push bx
    call dos_be_dfree               ; BX = sectors per cluster
    mov ah, bl
    pop bx
    pop dx
    jc .guess
    shl ah, 1                       ; sectors * 512 = (sectors * 2) * 256
    jz .guess                       ; (none, or a 64KB one no AX can say)
    mov [bx+dos_vcb], ah            ; kept
    jmp short .have
.guess:
    mov ah, 4                       ; 1KB, not kept
.have:
    xor al, al
    pop bx
    ret

; dos_fh_here - the record at SI resolves where the machine stands now: the
; open's and the create's, after dos_fh_enter has walked the name's folder
; clobbers: AX
dos_fh_here:
    mov al, [dos_pvol]
    mov [si+FH_VOL], al
    mov ax, [dos_pdir]
    mov [si+FH_DIR], ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_leave - back to the drive dos_fh_enter left, if it left one
; clobbers: nothing, flags included
;
; IT IS AT .fhok AND .fherr, the two exits every file handler funnels through,
; so a handler that grows a new error path cannot forget it.
; -----------------------------------------------------------------------------
dos_fh_leave:
    ; **IT NO LONGER GOES HOME** (SPEC.md 96.48). The drive half used to
    ; `dos_drv_sel` back to where the call found us, which on a program that
    ; names another drive - `B:DATAA.DAT` from a program standing on A: - is
    ; a COMPLETE VOLUME MOUNT per call, paired with the one `dos_fh_enter`
    ; already paid. Where the machine stands is a cache, not something a
    ; program can observe, so it is left where the name put it and the next
    ; `dos_fh_stand` moves it only if that name needs it elsewhere.
    ;
    ; The FOLDER half stays: it fires only for a name that carried a path
    ; (`[dos_fhmoved]`), which is not the hot case, and it keeps
    ; `[dos_curdir]` honest for `AH=47h` without a second mechanism.
    cmp byte [dos_fhkeep], 0        ; AH=3Bh sets this: a chdir's whole purpose
    jne .nofold                     ; is to leave the program somewhere else,
    call dos_fh_home                ; so the folder walk must NOT be undone
.nofold:
    mov byte [dos_fhkeep], 0
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_stat - find [dos_fname] in the current directory
; out: CF=0 with DX:AX = its size and BL = OSAPI_FIND_CZ bits; CF=1 if there
;      is no such file
; -----------------------------------------------------------------------------
dos_fh_stat:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    xor cx, cx
.next:
    mov di, dos_fent
    call dos_be_find
    jc .no
    cmp word [dos_fent+14], OSAPI_FT_DIR
    jae .next                       ; a folder is not a file, and '..' is not
    mov si, dos_fname               ; either
    mov di, dos_fent
    call dos_streq
    jne .next
    mov ax, [dos_fent+18]
    mov dx, [dos_fent+20]
    mov bl, [dos_fent+22]
    pop es
    pop di
    pop si
    pop cx
    clc
    ret
.no:
    pop es
    pop di
    pop si
    pop cx
    stc
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_streq - compare the NUL-terminated strings at DS:SI and ES:DI
; out: ZF=1 equal; clobbers nothing but the flags
; -----------------------------------------------------------------------------
dos_streq:
    push si
    push di
    push ax
.loop:
    mov al, [si]
    cmp al, [es:di]
    jne .out
    or al, al
    jz .out
    inc si
    inc di
    jmp short .loop
.out:
    pop ax
    pop di
    pop si
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_flush - write the window out if it is dirty
; out: CF=0; CF=1 with AL = a DOS error code. Preserves everything else.
;
; THE FIRST FLUSH REPLACES AND EVERY ONE AFTER IT APPENDS, which is exactly
; what makes AH=3Ch's truncate free and what keeps OSAPI_FILE_APPEND's
; cluster-multiple rule satisfied: a window is a cluster multiple by
; construction, so the file's size is one until the LAST flush - which is
; allowed to be short because nothing appends after it.
; -----------------------------------------------------------------------------
dos_fh_flush:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp byte [dos_wdirty], 0
    je .ok
    mov bl, [dos_wown]
    cmp bl, 0xFF
    je .ok
    mov cx, [dos_wlen]
    jcxz .clean

    xor bh, bh
    mov al, FH_SIZEOF
    mul bl
    mov si, ax
    add si, dos_fhtab
    call dos_fh_at                  ; THE BYTES GO WHERE THE FILE IS, not where
    jc .err                         ; the program is standing: a copy off B:
                                    ; onto C: writes with B: current every
                                    ; other window (SPEC.md 96.6.2)
    mov al, [si+FH_FLAGS]
    add si, FH_NAME
    mov bx, [dos_wseg]
    mov es, bx
    xor bx, bx
    test al, FHF_INPLC
    jnz .inplace                    ; an AH=3Dh handle OVERWRITES (96.11.6)
    test al, FHF_MADE
    jnz .append
    xor dx, dx
    call dos_be_write
    jc .errv
    or byte [si+FH_FLAGS], FHF_MADE ; (FH_NAME is 0: SI is the record)
    jmp short .acc
.append:
    mov al, WSEQF_HELD              ; ONE HELD STREAM a file (SPEC.md 96.53):
    mov di, [si+FH_TOK]             ; the data goes down every window and the
    call dos_be_wseq                ; FAT and the entry once, at the commit -
    mov [si+FH_TOK], di             ; which is when DOS writes them too
    jc .errv
.acc:
    call dos_fh_cb                  ; **A PARTIAL FLUSH MAKES THE FILE A VIEW**
    dec ax                          ; (SPEC.md 96.52): another handle took the
    test ax, [dos_wlen]             ; window mid-cluster, so the file now ends
    jz .home                        ; off a cluster boundary and the next
    or byte [si+FH_FLAGS], FHF_INPLC ; append would be refused. In place, the
                                    ; next write fills the last cluster's
                                    ; slack and hands back to the accumulator
                                    ; at the boundary - .ihstep's move
    jmp short .home
.inplace:
                                    ; THE COUNT GOES OVER EXACT and nothing is
                                    ; rounded here: a window that ends at or
                                    ; past the file's size is 18.4.7.2's loose
                                    ; case, so the kernel rounds the TRANSFER
                                    ; and takes the exact count as the new
                                    ; size. Rounding it here would move the
                                    ; size by up to 511 bytes the program
                                    ; never wrote
    mov ax, [dos_wbase]
    mov dx, [dos_wbase+2]
    call dos_be_wrat
    jc .errv
.home:                              ; the machine stays where the write went
.clean:                             ; (SPEC.md 96.48)
    mov byte [dos_wdirty], 0
    mov word [dos_wlen], 0
.ok:
    clc
    jmp short .out
.errv:
.err:
    mov byte [dos_wdirty], 0        ; do not retry it for ever - one write that
    mov word [dos_wlen], 0          ; will not go is reported once
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov al, 5                       ; "access denied", which is what DOS
    stc                             ; answers for a write that will not go
    ret
.out:
    jmp dos_popa_7                  ; the shared epilogue (SPEC.md 96.54.8)
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_take - give the window to the handle [dos_fhix] names
; out: CF=1 with AL = a DOS error if the previous owner would not flush
; -----------------------------------------------------------------------------
dos_fh_take:
    push bx
    mov bl, [dos_fhix]
    cmp bl, [dos_wown]
    je .mine
    call dos_fh_flush
    jc .out
    mov bl, [dos_fhix]
    mov [dos_wown], bl
    mov word [dos_wlen], 0
    mov word [dos_wbase], 0
    mov word [dos_wbase+2], 0
    mov byte [dos_wfill], 0         ; ...and the window holds nothing of the
.mine:                              ; new owner's file yet
    clc
.out:
    pop bx
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fh_fill - make the window cover the handle's current position
; in:  SI = the record ([dos_fhix] is its index)
; out: CF=0 with AX = the offset into [dos_wseg] and CX = the bytes available
;      there, CX = 0 at end of file. CF=1 with AL = a DOS error.
;      Preserves BX, DX, SI, DI, ES.
;
; AX AND NOT DI, because the caller's DI is where the bytes are GOING and a
; source handed back in it costs a shuffle at every call site.
; -----------------------------------------------------------------------------
dos_fh_fill:
    push bx
    push dx
    push di
    push es

    call dos_fh_take
    jc .errp0

    cmp byte [dos_wfill], 0         ; is the position already inside it?
    je .refill
    mov ax, [si+FH_POS]
    mov dx, [si+FH_POS+2]
    sub ax, [dos_wbase]
    sbb dx, [dos_wbase+2]
    jc .refill                      ; before the window
    or dx, dx
    jnz .refill                     ; more than 64KB past it
    cmp ax, [dos_wlen]
    jae .refill
    mov cx, [dos_wlen]
    sub cx, ax
    jmp .okp

.errp0: jmp .errp                   ; a near door (SPEC.md 96.7.3)
.refill:
    cmp byte [dos_wdirty], 0        ; **A DIRTY WINDOW GOES OUT FIRST** (SPEC.md
    je .rfclean                     ; 96.11.6): for a READ the window is a view
    call dos_fh_flush               ; and never dirty, but an in-place write
    jc .errp0                       ; makes it an accumulator for its own file -
.rfclean:                           ; and a write that spans two windows would
                                    ; otherwise have the second refill discard
                                    ; the first one's bytes
    call dos_fh_commit              ; **A HELD STREAM READ BACK BY ITS OWN
    jc .errp0                       ; HANDLE IS COMMITTED FIRST** (SPEC.md
                                    ; 96.53): the entry keeps its last
                                    ; committed size until then, and a read by
                                    ; name stops at the entry's size
    test byte [si+FH_FLAGS], FHF_WHOLE
    jnz .whole                      ; the window IS the file on that arm, and
                                    ; it is re-read rather than kept because
                                    ; another handle may have taken it
    call dos_fh_cb                  ; the cluster-aligned base under the
    neg ax                          ; position. The cluster is a power of two,
    and ax, [si+FH_POS]             ; so the mask is its own negation - and it
    mov dx, [si+FH_POS+2]           ; is the FILE's volume's (SPEC.md 96.11.11)
    mov [dos_wbase], ax
    mov [dos_wbase+2], dx

    push si
    mov bx, [dos_wseg]              ; THE BYTES COME FROM WHERE THE FILE IS,
    mov es, bx                      ; not from where the program is standing
    xor bx, bx                      ; (SPEC.md 96.6.2)
    mov cx, [dos_wbytes]
    lea di, [si+FH_CUR]             ; ...through the handle's own cursor
    mov word [dos_fvtgt], DBE_RSEQ  ; AN ORDINAL, not an address (96.44.1):
                                    ; .onvol stores this into [dos_betgt]
                                    ; and dos_be_go is what resolves it
    call .onvol                     ; out DX:AX = the bytes delivered, 0 at or
    pop si                          ; past the end
    jc .eof
    jmp short .got
.whole:
    mov word [dos_wbase], 0
    mov word [dos_wbase+2], 0
    push si
    mov bx, [dos_wseg]
    mov es, bx
    xor bx, bx
    mov cx, [dos_wbytes]
    xor dx, dx
    add si, FH_NAME
    mov word [dos_fvtgt], DBE_READ  ; EXPANDS on the way in (SPEC.md 20.14),
    call .onvol                      ; which is the whole reason this arm exists
    pop si
    jc .eof
.got:
    or dx, dx
    jz .short
    mov ax, [dos_wbytes]            ; a delivery bigger than the window cannot
.short:                             ; happen, and clamping is two bytes
    cmp ax, [dos_wbytes]
    jbe .set
    mov ax, [dos_wbytes]
.set:
    mov [dos_wlen], ax
    mov byte [dos_wfill], 1
    or ax, ax
    jz .eof
    mov ax, [si+FH_POS]
    sub ax, [dos_wbase]
    cmp ax, [dos_wlen]
    jae .eof
    mov cx, [dos_wlen]
    sub cx, ax
    jmp short .okp
.eof:
    xor cx, cx
    xor ax, ax
.okp:
    clc
    jmp short .outp
.errp:
    stc
.outp:
    pop es
    pop di
    pop dx
    pop bx
    ret

; --- .onvol - dos_be_go, standing where the WINDOW OWNER's file is ----------
; [dos_fvtgt] is the back end's target and SI the record (dos_fh_at).  It is
; one routine rather than two brackets because dos_be_rdat and dos_be_read
; differ in that word alone, and a bracket written twice is one that gets
; fixed once.
;
; THE TARGET IS COPIED IN AFTER THE SWITCH AND NOT BEFORE, which is the whole
; reason it travels in a word of its own: dos_drv_sel mounts through
; dos_be_goto, and dos_be_goto's first act is to write [dos_betgt]. Setting
; the target first and then switching ran every cross-volume READ as a
; DIRECTORY GOTO - which returns, so the caller read a byte count out of
; whatever it left in DX:AX and called the file empty.
.onvol:
    call dos_fh_at                  ; SI = the record: where its FILE is
    jc .onbad
    push ax                         ; ...and only now, with every mount the
    mov ax, [dos_fvtgt]             ; switch needed already made. BP IS NOT A
    mov [dos_betgt], ax             ; SCRATCH REGISTER HERE - it is the INT 21h
    pop ax                          ; frame, and [bp] is the program's own DS
    jmp dos_be_go                   ; ...and the machine stays on the file's,
                                    ; so a read loop over one file mounts once
                                    ; (SPEC.md 96.48). The PROGRAM's drive was
                                    ; never moved, so nothing walks home
.onbad:                             ; THE VOLUME IS GONE - the floppy came out
    ret                             ; between the open and the read.  The
                                    ; caller reads this as end of file, which
                                    ; is the honest half of it: no bytes, and
%endif                              ; DOS_EXTCORE
                                    ; no lie about which ones

    DBSS DOS_B_XNREL, 2
    DBSS DOS_B_XRLOC, 2
    DBSS DOS_B_XMINA, 2
    DBSS DOS_B_XIPAR, 2
    DBSS DOS_B_XCS,   2
    DBSS DOS_B_XIP,   2
    DBSS DOS_B_XSS,   2
    DBSS DOS_B_XSP,   2
    HBSS DOS_B_FSI,   FSI_SIZE
    DBSS DOS_B_WSEG,  2        ; the file window (SPEC.md 96.11): the
    DBSS DOS_B_WBYTES,2        ; paragraph it starts at and its size, which
                               ; is a multiple of the launch volume's cluster
    DBSS DOS_B_WOWN,  1        ; the handle index holding it, 0xFF = nobody
    DBSS DOS_B_WFILL, 1        ; ...and whether it holds anything of that
    DBSS DOS_B_WDIRTY,1        ; file, which "0 bytes at offset 0" cannot say
    DBSS DOS_B_WPAD,  1
    DBSS DOS_B_WBASE, 4        ; the file offset it starts at
    DBSS DOS_B_WLEN,  2        ; ...and the valid bytes in it
    DBSS DOS_B_FNAME, 16       ; the 8.3 name a call named
    DBSS DOS_B_FENT,  OSAPI_FIND_SZ
    DBSS DOS_B_BKSS,  2
    DBSS DOS_B_BKSP,  2
    DBSS DOS_B_DSTK,  2        ; ...and the GATE's own swap (SPEC.md 96.7.2):
    DBSS DOS_B_GAX,   2        ; the offset the next entry starts at, and the
    DBSS DOS_B_GBP,   2        ; cells the swap itself runs out of. Seven
    DBSS DOS_B_GIP,   2        ; words, read and written with IF=0, which is
    DBSS DOS_B_GCS,   2        ; what makes ONE copy of each of them enough -
    DBSS DOS_B_GFL,   2        ; nothing can nest while interrupts are off,
    DBSS DOS_B_GSS,   2        ; and by the time they are they are spent
    DBSS DOS_B_GSP,   2
    DBSS DOS_B_BETGT, 2        ; the back end's own three words
    DBSS DOS_B_BEFLG, 2
    DBSS DOS_B_ONPRG, 1
    DBSS DOS_B_ONPAD, 1
    DBSS DOS_B_FHIX,  1        ; the window owner's index, set by every
    DBSS DOS_B_FHPAD, 1        ; slot resolution rather than threaded
    DBSS DOS_B_FHTAB, FH_SIZEOF * DOS_NFH
    DBSS DOS_B_VCB,   DVOL_CAP ; per volume: sectors a cluster, 0 = not yet
                               ;   asked (dos_fh_cb). AFTER the table, so
                               ;   dos_fh_setup's one rep stosb clears both
    DBSS DOS_B_DTA,   2        ; the Disk Transfer Area a find fills,
    DBSS DOS_B_DTASEG,2        ; PSP:0080 until AH=1Ah moves it
    DBSS DOS_B_DTASV, 2        ; ...banked while the kernel's own record is
    DBSS DOS_B_DTASVS,2        ; read through OUR ES
    DBSS DOS_B_FORD,  2        ; the ordinal a find walk resumes from
    DBSS DOS_B_W83A,  11       ; the two 8.3 forms dos_wild compares
    DBSS DOS_B_W83B,  11
    DBSS DOS_B_W83P,  2
    DBSS DOS_B_PARENT, 2       ; the PSP that launched the running program,
    DBSS DOS_B_INCHLD, 1       ; 0 at the top level (SPEC.md 96.14)
    DBSS DOS_B_XPAD,  1
    DBSS DOS_B_PSVSS, 2        ; ...and the stack it was on when it did
    DBSS DOS_B_PSVSP, 2
    DBSS DOS_B_PPSP,  2        ; the parent's own PSP/block, to put back
    DBSS DOS_B_PPARA, 2
    DBSS DOS_B_PGPAR, 2
    DBSS DOS_B_PEXE,  1
    DBSS DOS_B_PEPAD, 1
    DBSS DOS_B_CHEXIT, 1       ; the child's code, for AH=4Dh
    DBSS DOS_B_CHPAD, 1
    DBSS DOS_B_CHBLK, 2        ; the block it was given, to hand back
    HBSS DOS_B_DQBUF, DQ_SIZE * DQ_MAXREC  ; what the drivers said on their
                               ; way out (96.17)
    DBSS DOS_B_BLAST, 32       ; "BLASTER=A220 I5 D1 T4", or empty
    DBSS DOS_B_XMSTAB, XH_SIZE * XMS_NH  ; the XMS handle table (96.15)
    DBSS DOS_B_XMLEN, 4        ; ...and AH=0Bh's move, unpacked out of the
    DBSS DOS_B_XMSH,  2        ; caller's sixteen-byte block
    DBSS DOS_B_XMSO,  4
    DBSS DOS_B_XMDH,  2
    DBSS DOS_B_XMDO,  4
    DBSS DOS_B_XMLIN, 4
    DBSS DOS_B_XMCON, 4
    DBSS DOS_B_XMDIR, 2
    DBSS DOS_B_XPARM, 2        ; AH=4Bh's parameter block, banked while the
    DBSS DOS_B_XPARMS, 2       ; name is copied out of the same segment
    DBSS DOS_B_LDNAME, 2       ; ...and which FILE it comes from
    DBSS DOS_B_LDPSP, 2        ; the PSP of the program being LOADED, and
    DBSS DOS_B_LDPAR, 2        ; its block - not always the arena's
    DBSS DOS_B_DY,    2        ; the date we keep (SPEC.md 96.13)
    DBSS DOS_B_DM,    1
    DBSS DOS_B_DD,    1
    DBSS DOS_B_LTL,   2        ; the tick count midnight is measured against
    DBSS DOS_B_LTH,   2
    DBSS DOS_B_TMP1,  2        ; the clock arithmetic's fields, held in memory
    DBSS DOS_B_TMP2,  2        ; rather than in registers a divide needs back
    DBSS DOS_B_TMP3,  2
    DBSS DOS_B_ACC,   2
    DBSS DOS_B_FABS,  1        ; did the name carry a leading separator?
    DBSS DOS_B_FHPATH,  1      ; ...and a folder part: THE BYTE AFTER, which
                               ; dos_cd_go reads with it as one word
    DBSS DOS_B_PFBUF, DOS_PFIN + 1  ; AH=29h's copy of the program's name...
    DBSS DOS_B_PFCB,  12            ; ...and the twelve bytes it hands back
    DBSS DOS_B_FNAME2, 16      ; AH=56h's SECOND name (SPEC.md 96.31)
    DBSS DOS_B_RNVOL,  1       ; ...the volume it was ASKED on, which is what
    DBSS DOS_B_RNDRV,  1       ; an unqualified name means; the old name's
    DBSS DOS_B_RNABS,  1       ; drive; and whether it carried a separator
    DBSS DOS_B_VFY,    1       ; AH=2Eh's verify flag (96.56.3)
    DBSS DOS_B_FNSEG, 2        ; the segment dos_fh_core reads a name FROM
    DBSS DOS_B_FDRV,  1        ; ...and the DRIVE it named, 0xFF = none
; --- WHERE THE MACHINE IS STANDING, which is not where the PROGRAM is ------
; SPEC.md 96.48. `[dos_vol]`/`[dos_curdir]` are what a program can observe -
; AH=19h and AH=47h answer them and a bare name resolves against them - and
; these two are the CACHE: whichever volume and folder the last `dos_be_goto`
; actually reached. A name moves the machine and never the program, so the
; machine may be left where the last name put it and moved only when the next
; one needs it elsewhere. 0xFF = nowhere yet, which forces the first stand.
    DBSS DOS_B_PVOL,  1
    DBSS DOS_B_PDIR,  2
    DBSS DOS_B_FVTGT, 2        ; the back end call a bracketed read makes
    DBSS DOS_B_OPMODE, 1       ; AH=3Dh's access mode, banked (96.11.6)
    DBSS DOS_B_WFIL,  1        ; (FVVOL, FVSV and WVSV went with dos_fh_at:
                               ; a handle stands by its own record now and
                               ; never moves the program's drive, so there
                               ; is nothing to bank.) WFIL was a PAD: the gap's
                               ; "the source is a fill byte, not the
                               ; program's buffer" flag is free (96.11.6.1)
    DBSS DOS_B_GAPN,  4        ; ...and what is left of the gap to lay
    DBSS DOS_B_TRNOF, 4        ; ...and how far a shrink's rewrite has got
    HBSS DOS_B_CPW,   1        ; 1 = dos_run is resuming on the compaction's
                               ; own wake (SPEC.md 96.35.1)
    HBSS DOS_B_DRVOUT, 1       ; ...and 1 = the drivers are already suspended
    DBSS DOS_B_CURDIR, 2       ; the cluster we are standing in
; --- WHERE EACH DRIVE IS STANDING (SPEC.md 96.6.1) -------------------------
; DOS keeps a current directory per drive, and here that is one CLUSTER each
; and nothing else. It is that small because there is no jail: every drive's
; root is its volume's root, so a slot nobody has touched is 0 - which .bss
; already is, and which is exactly right. No "has this been initialised" flag,
; because there is no state a fresh drive could be in other than its root.
    DBSS DOS_B_DVCWD,  2 * DVOL_CAP  ; **`DVOL_CAP`, NOT `DVOL_MAX`** (SPEC.md
                                     ; 96.44.2.1): the reach is a per-host
                                     ; number and this table is the CORE's bss,
                                     ; so its width has to be one both halves
                                     ; read from apps/dos/doscall.inc
    DBSS DOS_B_DVTGT,  1            ; the drive a switch is going TO, banked
                                    ; because OSAPI_VOL_KIND promises nothing
                                    ; about DX
    DBSS DOS_B_DVFROM, 1            ; the drive a switch is leaving, for its
                                    ; own rollback
    DBSS DOS_B_NDRV,   1            ; the count AH=0Eh answers, probed once
    DBSS DOS_B_CWDST,  2            ; AH=47h: the program's buffer, banked
                                    ; across OSAPI_FILE_PATH, which wants ES:DI
                                    ; for its OWN answer
    DBSS DOS_B_CWDRV,  1            ; ...and the drive it was asked about
    DBSS DOS_B_IVT,   1024
    DBSS DOS_B_BDA,   256
; --- THE PACKET DRIVER (SPEC.md 96.23) ---------------------------------------
; None of this is resident: a package's bss is zeroed into its heap claim at
; launch and goes back when the window closes, so what it costs is a DOS
; session's memory and not a machine's.
%ifdef DOS_NETINLINE                ; 96.43: no driver, so no packet driver;
                                    ; and in a PART the cells are the claim's
                                    ; (SPEC.md 96.23.11)
    HBSS DOS_B_PKTVEC,  1           ; the vector we took, 0 = none
    HBSS DOS_B_PKTRAW,  1           ; 1 = we hold NETV_RAW
    HBSS DOS_B_PKTBSY,  1           ; the poll's re-entrancy guard
    HBSS DOS_B_PKTMODE, 1           ; what set_rcv_mode was told
    HBSS DOS_B_PKTHTAB, PKT_NHAND * PKT_HSIZE
    HBSS DOS_B_PKTMAC,  6           ; the station address, banked by the claim
    HBSS DOS_B_PKTCVEC, 4           ; the client receiver being called
    HBSS DOS_B_PKTCHAND, 2          ; ...and the handle both calls carry
    HBSS DOS_B_PKTOLD08, 4          ; the tick we chain
    HBSS DOS_B_PKTSSS,  2           ; the program's stack, banked across a poll
    HBSS DOS_B_PKTSSP,  2
    HBSS DOS_B_PKTCAN,  2           ; **A CANARY UNDER THE PRIVATE STACK**, and
                                    ; it is here because the alternative was a
                                    ; HANG: the two words above are what the
                                    ; program's SS:SP is banked in, they sit
                                    ; directly below the stack, and an overflow
                                    ; writes a bogus stack back and takes the
                                    ; machine with it. The translation made the
                                    ; chain under this poll much deeper than
                                    ; §96.23.4.1 sized it for
    HBSS DOS_B_PKTCDS,  2           ; the CLIENT's DS, captured at the gate
                                    ; (SPEC.md 96.23.9) - NOT read back out of
                                    ; the stack frame, which is what the first
                                    ; version did and got wrong
    HBSS DOS_B_PKTSTAT, 24          ; six dwords, get_statistics' own order
; --- AND NOTHING FOR THE WIRE ITSELF (SPEC.md 96.23.7) -----------------------
; Every frame, the staging copy, the poll's private stack and the whole of the
; cable translation's state are in the NETWORK CLAIM - see PKB_* above. They
; were 4,052 bytes of this table, which every DOS window paid for on every
; machine, including the 128KB one that ships no network driver at all.
%endif
%ifdef DOS_NETPART
    HBSS DOS_B_PKTDOOR, 2           ; the far door's OFFSET, always 0, and...
%endif
%ifdef DOS_NETANY                   ; inline or a part: the host keeps these
    HBSS DOS_B_PKTBSEG, 2           ; **THE FRAME BUFFER IS A HEAP CLAIM**
                                    ; (SPEC.md 96.23.7) and this is its segment,
                                    ; 0 = none. They were 3,028 bytes of bss,
                                    ; which every DOS window paid for on every
                                    ; machine - including every machine with no
                                    ; card in it
    HBSS DOS_B_PKTXL,   1           ; **WHICH PATH, and it is NOT the same
                                    ; question as which CLASS** (SPEC.md
                                    ; 96.26.1): the translation calls sockets
                                    ; on whatever [net_cls] says, so forcing
                                    ; the path on a card machine - which is
                                    ; how it is tested at all - must not
                                    ; forge the class underneath it
%endif                              ; DOS_NONET
; --- the built-in commands' own state (SPEC.md 96.30, apps/dos/dosh.inc) -----
    DBSS DOS_B_SHLINE,  DSH_LINE    ; the command tail, unpacked
    DBSS DOS_B_SHVERB,  DSH_ARG     ; the verb, upper-cased
    DBSS DOS_B_SHA1,    DSH_ARG     ; ...and its two arguments
    DBSS DOS_B_SHA2,    DSH_ARG
    DBSS DOS_B_SHSPEC,  DSH_ARG     ; one of them, being taken apart
    DBSS DOS_B_SHLEAF,  DSH_ARG     ; ...into a folder and this
    DBSS DOS_B_SHDNAM,  DSH_ARG     ; the destination's name, empty = keep
    DBSS DOS_B_SHRTGT,  DSH_ARG     ; what a `>` named
    DBSS DOS_B_SHPAT,   DSH_PAT     ; the pattern, padded to eleven...
    DBSS DOS_B_SHNM11,  DSH_PAT     ; ...and the candidate, the same way
    DBSS DOS_B_SHFNAM,  16          ; the match's own name
    DBSS DOS_B_SHFND,   OSAPI_FIND_SZ
    DBSS DOS_B_SHNUM,   12          ; a count, as digits - TWELVE since DIR,
                                    ; whose sizes are 32-bit: ten digits and a
                                    ; NUL, and dsh_num32 writes the NUL at +11
    DBSS DOS_B_SHCPHEAP, 1          ; the buffer came off the HEAP and not the
                                    ; DOS arena, which is the prompt's arm
                                    ; (SPEC.md 96.30.7.2) - recorded rather
                                    ; than re-derived, so the put cannot free
                                    ; the other one
    DBSS DOS_B_SHQUIET, 1           ; the line was redirected
    DBSS DOS_B_SHASDIR, 1           ; try the whole spec as a folder
    DBSS DOS_B_SHDEL,   1           ; ...and delete the source after
    DBSS DOS_B_SHDIROP, 1           ; 0 MD, 1 RD, 2 CD
    DBSS DOS_B_SHSKIP,  2           ; matches to pass over
    DBSS DOS_B_SHN,     2           ; ...and how many were done
    DBSS DOS_B_SHOFF,   4           ; TYPE's offset, 32 bits
    DBSS DOS_B_SHBVOL,  1           ; where we were standing before a verb
    DBSS DOS_B_SHBCLUS, 2
    DBSS DOS_B_SHRDRV,  1           ; what dsh_resolve answered
    DBSS DOS_B_SHRCLUS, 2
    DBSS DOS_B_SHSDRV,  1           ; the source place...
    DBSS DOS_B_SHSCLUS, 2
    DBSS DOS_B_SHDDRV,  1           ; ...and the destination's
    DBSS DOS_B_SHDCLUS, 2
    DBSS DOS_B_SHCNAME, DSH_ARG     ; the name this file is written under
    DBSS DOS_B_SHCPSEG, 2           ; the copy buffer's DOS block...
    DBSS DOS_B_SHCPKB,  2           ; ...and how many KB it turned out to be
    DBSS DOS_B_SHMADE,  1           ; the destination has been created
    DBSS DOS_B_SHGOT,   2           ; bytes in the buffer this pass
    DBSS DOS_B_SHRCUR,  FSEQ_SIZE   ; COPY's READ_SEQ cursor and its HELD
    DBSS DOS_B_SHWTOK,  2           ; WRITE_SEQ token (SPEC.md 96.53)
    DBSS DOS_B_SHWHY,   1           ; DSHW_*: WHICH refusal, for a debugger
    ; --- DIR's SWITCHES, and the state a SUSPENDED listing resumes from -----
    ; (SPEC.md 96.33.9). /P cannot wait for a key: dos_con_key holds the gfx
    ; lock, so a built-in that blocked there would freeze the whole machine
    ; until a human pressed something. It emits one page and returns instead,
    ; and these four are what the next keystroke picks the walk up from.
    DBSS DOS_B_SHDSW,   1           ; DIR's switch bits: DSW_P, DSW_W
    DBSS DOS_B_SHMORE,  1           ; 1 = a listing is suspended mid-page
    DBSS DOS_B_SHORD,   2           ; ...the ORDINAL to resume at (19.7.1),
                                    ; never a cursor: the walk re-goto's and
                                    ; re-reads, so a floppy change between
                                    ; pages costs a wrong listing and not a
                                    ; wrong SECTOR
    DBSS DOS_B_SHLN,    2           ; lines put on this page so far
    DBSS DOS_B_SHWCOL,  1           ; /W: which of the five columns is next
    DBSS DOS_B_SHSSEG,  2           ; DIR's sort claim, 0 = none (SPEC.md
    DBSS DOS_B_SHSN,    2           ; 96.33.9.2) ...and how many records in it
    DBSS DOS_B_SHSI,    2           ; ...and which one the listing is emitting
    DBSS DOS_B_FPBUF,   DOS_PBUF    ; the name's folder part ([dos_fhpath])
    DBSS DOS_B_FHCWD,   2           ; where we were before walking it
    DBSS DOS_B_FHMOVED, 1           ; ...and whether we did
    DBSS DOS_B_FHKEEP,  1           ; AH=3Bh: do NOT walk back
    ; --- THE CONSOLE'S OWN (SPEC.md 96.33) ----------------------------------
    ; os88con.inc declares the SCREEN; these three are the prompt's, and they
    ; are here rather than there because a console is not a shell - the library
    ; has no idea a line is being typed into it.
    DBSS DOS_B_INBR,    1           ; the fsx bracket is up, so dos_tty's byte
                                    ; goes to the ROM and not to the console
    HBSS DOS_B_FROMCON, 1           ; ...and this launch was typed at the prompt
    DBSS DOS_B_FSXUP,   1           ; the console has the WHOLE screen (96.33.5),
                                    ; so no kernel drawing slot may be called
    HBSS DOS_B_ISPKG,   1           ; the typed name ends in .O88 (96.33.17)
    HBSS DOS_B_PKGQ,    1           ; ...and a launch of one is POSTED
    HBSS DOS_B_PKGN,   13           ; ...with its name banked out of dsh_a1
    HBSS DOS_B_PTRES,   1           ; dos_path_take: resolve, do not COMMIT
    HBSS DOS_B_PTBASE,  2           ; ...and where the text it was given began
    HBSS DOS_B_PGDIR,   2           ; the folder a resolve-only answered with
    HBSS DOS_B_PGVOL,   1           ; ...and its volume (SPEC.md 96.33.21)
    HBSS DOS_B_PGPDIR,  2           ; the PROGRAM's own folder, banked before
    HBSS DOS_B_PGPVOL,  1           ; the DOCUMENT's resolve overwrites those
    HBSS DOS_B_PGDOC,  16           ; OSAPI_PKG_START's document locator:
                                    ; 13 name, dir WORD, vol BYTE (21.5.3)
    HBSS DOS_B_PGHAS,   1           ; ...and whether the line named one
    HBSS DOS_B_FSXGO,   1           ; ...and a launch was typed INTO it, so the
                                    ; bracket comes down for the program and
                                    ; goes back up after it (SPEC.md 96.33.16)
    HBSS DOS_B_AEDN,    1           ; ...and what the LAST full-screen pass saw
                                    ; of Alt+Enter (SPEC.md 96.33.5.1):
                                    ; OSAPI_KEY_DOWN is a level read, so the
                                    ; edge is ours to find. Seeded DOWN at the
                                    ; top of the bracket, because the press
                                    ; that got us in is still held
    DBSS DOS_B_SHEXEC,  1           ; dsh_run may try an unknown verb as a
                                    ; PROGRAM: set from the prompt, 0 for `/c`
    HBSS DOS_B_CMDX,    2           ; the column the prompt ended on, which is
                                    ; how far back BS may rub (SPEC.md 96.33.3)
    HBSS DOS_B_CMDN,    2           ; ...and how much of dsh_line is typed

%ifdef DOS_MEGA                     ; both hosts of one build, so not rule 1's
    DBSS DOS_B_HSLOT,   2           ; the HMA host's slot maker (dos_hvec), or
%endif                              ; 0 - and LAST, so no cell before it moves
%ifdef DOS_MEGA
%ifndef KD_BACKEND
DOS_SNREC   equ 4                   ; **THE TRIP'S SNAP RECORD** (KERN-MEGA-PLAN
    HBSS DOS_B_SNAP, DOS_SNREC      ; 14.106): dw a cursor (column, row), dw a
                                    ; flag - and con_scr DIRECTLY AFTER IT,
                                    ; because the console's bss is based at
                                    ; DOS_BSS_SIZE and this is the last host
                                    ; row. kern_dos fills cursor, cells and
                                    ; flag through one linear address the
                                    ; kernel staged (kerndos/kdpark.inc), so
                                    ; the three must be one run
%if HB != DOS_B_SNAP + DOS_SNREC
 %error "the snap record must be the LAST HBSS row: con_scr follows it"
%endif
%endif
%endif
; **AND THE HOST'S BLOCK IS THE PACKAGE'S OWN bss** (SPEC.md 96.44.5). It was
; `CORE_BSS_SIZE + HB` - the core's cells first and the host's after them,
; both inside the package's bss - and since the core became a PART its cells
; live at `DOS_CBASE`, above the core's code and at the same address in every
; host. So what the package declares is its own, and `dos_hbss` is
; `os88_image_end` with nothing in front of it.
%if DB > CORE_BSS_SIZE
 %error "the DOS core's bss outgrew CORE_BSS_SIZE - raise it in \
apps/dos/doscall.inc, and note that EVERY host reserves the whole of it \
whether it uses the cells or not"
%endif
%if DVOL_MAX > DVOL_CAP
 %error "this host reaches more drives than dos_dvcwd has room for - raise \
DVOL_CAP in apps/dos/doscall.inc, which every host reads, and NEVER size the \
table from DVOL_MAX (SPEC.md 96.44.2.1)"
%endif
%ifdef DVW_SPLIT                    ; --- the live window, PARTED (96.54.9) ---
    ; Only what host code reads while the part may be ABSENT: the rest is
    ; the part's, laid out by apps/dos/doswcell.inc
    HBSS DOS_B_DVWF,  6         ; want, run, go, done, spawn, dirty
    HBSS DOS_B_DVWH,  6         ; three words: the arena cap, the posted
                                ; door's target and the part's segment
dvw_want    equ dos_hbss + DOS_B_DVWF + 0   ; Run in Window was asked for
dvw_run     equ dvw_want + 1    ; a program is on the worker
dvw_go      equ dvw_want + 2    ; ...UI -> worker: start it (run's PAIR)
dvw_done    equ dvw_want + 3    ; ...worker -> UI: it ended
dvw_spawn   equ dvw_want + 4    ; the worker exists, and outlives the part
dvw_dirty   equ dvw_want + 5    ; the console owes the band a paint
dvw_cap     equ dos_hbss + DOS_B_DVWH       ; word: this run's arena cap
dvw_rtgt    equ dvw_cap + 2     ; word: a posted door (HICALL's thunk)
dvw_pseg    equ dvw_cap + 4     ; word: the part's claim, 0 = not fetched
%if dvw_go != dvw_run + 1
 %error "dvw_start sets run and go with one word store"
%endif
%elifdef DVW                        ; --- the live window (SPEC.md 96.54) ---
    HBSS DOS_B_DVWF,  28        ; the flag bytes below, in order
    HBSS DOS_B_DVWC,  4         ; the capture's segment, and our CS for FFh
    HBSS DOS_B_DVWSM, 50        ; TopView's row sums, CON_ROWS words (96.54.5:
                                ; os88con.inc comes later, so dosw.inc asserts)
    HBSS DOS_B_DVWW,  16        ; eight words: LOW_SEG, two stacks, AX, a nap,
                                ; and this run's arena cap
    HBSS DOS_B_DVWK8, 4         ; the kernel's int 08h, far
    HBSS DOS_B_DVWR,  22        ; a posted door: its target, ten registers
    HBSS DOS_B_DVWQ,  32        ; the window's keys, sixteen
    HBSS DOS_B_DVWX,  DVW_XB_SIZE   ; the program's vectors and BDA...
    HBSS DOS_B_DVWD,  DVW_XB_SIZE   ; ...and the desktop's, while it runs
    HBSS DOS_B_DWM,   10        ; INT 33h in the window (doswm.inc)
dvw_want    equ dos_hbss + DOS_B_DVWF + 0   ; Run in Window was asked for
dvw_run     equ dvw_want + 1    ; a program is on the worker
dvw_go      equ dvw_want + 2    ; ...UI -> worker: start it
dvw_done    equ dvw_want + 3    ; ...worker -> UI: it ended
dvw_kill    equ dvw_want + 4    ; the window is closing: stop it
dvw_in      equ dvw_want + 5    ; the instance's world is in the IVT
dvw_spawn   equ dvw_want + 6    ; the worker exists (it is for ever)
dvw_rq      equ dvw_want + 7    ; a door: 1 posted, 2 served, 0 done
dvw_dirty   equ dvw_want + 8    ; the console owes the band a paint
dvw_chp     equ dvw_want + 9    ; the capture is a lone Hercules's PAGE 1
                                ; (96.54.6.1; + 9 was dvw_walk's, 96.54.3)
dvw_poll    equ dvw_want + 10   ; empty AH=01h polls, for the yield
dvw_kh      equ dvw_want + 11   ; the key ring's head...
dvw_kt      equ dvw_want + 12   ; ...and tail
dvw_closeq  equ dvw_want + 13   ; a refused close, owed when the run ends
dvw_fs      equ dvw_want + 14   ; the program is on the FULL SCREEN
dvw_frz     equ dvw_want + 15   ; FROZEN in the window (96.54.1) - the byte
                                ; after dvw_fs, so one word tests the pair
dvw_fsreq   equ dvw_want + 16   ; ...asked for: Alt+Enter, or a graphics mode
dvw_parked  equ dvw_want + 17   ; ...and the worker has stopped for it
dvw_gfx     equ dvw_want + 18   ; it set a graphics mode: no way back
dvw_cmode   equ dvw_want + 19   ; 0, DVWC_TV, _SEG or _HP (96.54.5-6.1)
dvw_ctick   equ dvw_want + 20   ; ...and the capture's tick count
dvw_dkind   equ dvw_want + 21   ; the desktop's VID_* kind (96.54.7)
dvw_hprow   equ dvw_want + 22   ; DVWC_HP's probe row, FFh once promoted
dvw_owe     equ dvw_want + 23   ; the band, owed whole (96.54.10)
dvw_kbusy   equ dvw_want + 24   ; ticks a poll stays awake after a key
dvw_ptk     equ dvw_want + 25   ; dvw_xpost was reached from a tick...
dvw_pgap    equ dvw_want + 26   ; ...and the ticks since its last post
%if dvw_kill != dvw_done + 1 || dvw_kt != dvw_kh + 1 || dvw_fs != dvw_closeq + 1 || dvw_fsreq != dvw_frz + 1 || dvw_gfx != dvw_parked + 1 || dvw_go != dvw_run + 1
 %error "dvw_start (dosw.inc) clears these flags in word PAIRS: keep each pair adjacent"
%endif
dvw_cseg    equ dos_hbss + DOS_B_DVWC       ; word: B800h or B000h
dvw_csview  equ dvw_cseg + 2                ; word: our CS, as TopView saw it
dvw_sums    equ dos_hbss + DOS_B_DVWSM
dvw_cap     equ dvw_lss + 14    ; word: this run's arena cap (96.54.2)
dvw_lss     equ dos_hbss + DOS_B_DVWW + 0
dvw_fss     equ dvw_lss + 2
dvw_fsp     equ dvw_lss + 4
dvw_nss     equ dvw_lss + 6
dvw_nsp     equ dvw_lss + 8
dvw_tax     equ dvw_lss + 10
dvw_narg    equ dvw_lss + 12
dvw_k08     equ dos_hbss + DOS_B_DVWK8
dvw_rtgt    equ dos_hbss + DOS_B_DVWR
dvw_r_ax    equ dvw_rtgt + 2
dvw_r_bx    equ dvw_rtgt + 4
dvw_r_cx    equ dvw_rtgt + 6
dvw_r_dx    equ dvw_rtgt + 8
dvw_r_si    equ dvw_rtgt + 10
dvw_r_di    equ dvw_rtgt + 12
dvw_r_bp    equ dvw_rtgt + 14
dvw_r_es    equ dvw_rtgt + 16
dvw_r_ds    equ dvw_rtgt + 18
dvw_r_fl    equ dvw_rtgt + 20
dvw_kq      equ dos_hbss + DOS_B_DVWQ
dvw_xblk    equ dos_hbss + DOS_B_DVWX
dvw_xdsk    equ dos_hbss + DOS_B_DVWD
%endif
%ifdef DOS_CORE_INLINE
DOS_BSS_SIZE equ CORE_BSS_SIZE + HB ; the core's cells are in here too
%else
DOS_BSS_SIZE equ HB
%endif

%ifndef DOS_EXTCORE                 ; THE CORE (SPEC.md 96.44) - the built-in
%include "dosh.inc"                 ; commands are INT 21h's own (96.30) and
%endif                              ; not a host's: a COMMAND.COM that is not
                                    ; a file, over the back end like every
                                    ; other file verb
%ifndef KD_BACKEND                  ; the console's, so 96.43 takes it too
WIN_CODE
%include "dosc.inc"                 ; ...AND THE PROMPT THAT TYPES INTO IT
WIN_CODE_END                        ; (DOS-HEADLESS-PLAN 4.4: the window's)
                                    ; (SPEC.md 96.33): an input, an output and
                                    ; a prompt, and not one verb of its own
%ifdef DOS_WIN16
%include "dosw16.inc"               ; ...and the door a Windows program leaves
%endif                              ; by, to WIN16 (SPEC.md 124.3) - RESIDENT,
                                    ; a headless host's door too (96.44.20)
%endif

; os88ui.inc first (os88line.inc needs its UI_* macros), and both LAST -
; the header and the icon block are at fixed offsets in the image (SPEC.md
; 20.2), so code emitted between them fails the icon macro's own assertion.
%ifdef DOSKPART
%define OS88UI_ALERT                ; SPEC.md 75.3: arm 3 on a machine with no
                                    ; fixed disk ENDS THE SESSION, and SPEC.md
                                    ; 96.42 will not let it do that quietly.
                                    ; The alert is the OS's own and costs the
                                    ; kernel nothing; this build is the only
                                    ; one that can ask the question
%endif
%define OS88UI_RAD                  ; SPEC.md 13.17.4: the memory page's one
                                    ; choice - where the program runs, which is
                                    ; two answers and so a radio. Opted into
                                    ; here because os88ui's rule is that a
                                    ; package that does not use a control pays
                                    ; NOTHING for it - and this is the
                                    ; control's FIRST caller in the tree
                                    ; (SPEC.md 96.36)
%define OS88UI_CHK                  ; ...and the three boxes inside the two
                                    ; subsections (96.36.7, 96.36.8), which are
                                    ; each one independent yes/no and so are
                                    ; not the radio
%define OS88UI_DROP                 ; ...and the disk cache's dial (96.36.6),
                                    ; which is one pick out of a SHORT LIST of
                                    ; widths - a radio of five would be 80 px
                                    ; of a column that has 155
%define OS88UI_NODRGEST             ; ...which is press-press (dos_click_mem ->
                                    ; os88ui_drpress both ways) and is never
                                    ; taken down from outside, so its drag,
                                    ; release and close are out (SPEC.md
                                    ; 13.14.7, apps size pass 2b)
%ifdef DOS_SETPART
%define OS88UI_CTLEXT               ; ...and where the Setup page is a PART,
%endif                              ; the box, radio, dial and their glyph are
                                    ; ITS code, not the window's: the page is
                                    ; their only caller (SPEC.md 96.32.2.3)
%define OS88UI_ABOUT                ; ...and the About card (SPEC.md 96.51).
                                    ; dos_about was a bare `ret` for the whole
                                    ; of this package's life, so the kernel put
                                    ; 'About DOS...' in the bar and the item
                                    ; did NOTHING - which is 20.5.1.1's own
                                    ; case, and what was missing behind it was
                                    ; the credit
%ifndef KD_BACKEND                  ; **THE WINDOW'S FURNITURE** (SPEC.md
                                    ; 96.43.2): buttons, fields, a radio and a
                                    ; line editor, and with the window half
                                    ; gated out there is nothing left in this
                                    ; root that names one.  os88ui's own rule -
                                    ; a package pays for the controls it uses -
                                    ; one level up: a build with no window uses
                                    ; none
WIN_CODE
%include "os88ui.inc"
WIN_CODE_END                        ; (DOS-HEADLESS-PLAN 4.4: the window's)
%if DOS_BTREC_SZ != OS88UI_BT_SIZE
 %error "DOS_BTREC_SZ mirrors OS88UI_BT_SIZE and they have drifted - the HBSS block is laid out before this include, so the size must be written twice; fix the literal"
%endif
%define OS88LINE_NOSET              ; the fields are filled in place and
                                    ; resynced (96.21.1), never replaced
                                    ; (apps size pass 2b)
WIN_CODE
%include "os88line.inc"
WIN_CODE_END                        ; (DOS-HEADLESS-PLAN 4.4: the window's)
%endif
; --- THE CONSOLE (SPEC.md 96.33), telnet's screen as a shared include -------
; **ITS BSS GOES LAST**, past this package's own chain, which is the one line
; that keeps the two apart: os88parts.inc already took the FIRST OP_BSS bytes
; off `os88_image_end` (the DBSS table's own note above), so a console based
; there by default would sit on the parts standard and on the head of this
; table - which is exactly the silent overlap SPEC.md 96.29.1 cost a session.
; DOS_BSS_SIZE is an `equ` a few lines above, so there is nothing to keep in
; step by hand.
%ifdef DOS_WTAIL
%define CON_BSS_AT (dos_wbss + WB)  ; ...in the TAIL's bss, after its rows
%else
%define CON_BSS_AT (os88_image_end + DOS_BSS_SIZE)
%endif
%define CON_FSX                     ; the FULL-SCREEN renderer (SPEC.md 70.8.13,
                                    ; 96.33.5): the same buffer on real text
                                    ; VRAM, driven by dosc.inc's own bracket
%ifndef KD_BACKEND                  ; **NO CONSOLE UNDER kern_dos** (SPEC.md
                                    ; 96.43): the program owns the screen and
                                    ; AH=02h/09h go to the ROM's teletype, which
                                    ; is what they already do inside the fsx
                                    ; bracket.  It is the plan's lever 5 and the
                                    ; largest single one, 10,138 bytes - 6,863
                                    ; of them the 80x25 shadow and the 8x8 font,
                                    ; which are .bss and so are a KB of the
                                    ; image rung apiece
%define CON_NOSCROLLDOWN            ; ...and no con_scrolldown: a teletype
                                    ; only scrolls UP (SPEC.md 96.44.16.1)
%define CON_TTY                     ; CR, LF, BS, TAB and BEL, because there is
                                    ; no ANSI parser over this one (SPEC.md
                                    ; 96.33.1) - and con_open, since the
                                    ; attribute a zeroed bss leaves is black
                                    ; on black
WIN_CODE
%include "os88con.inc"
WIN_CODE_END                        ; (DOS-HEADLESS-PLAN 4.4: the window's)
%ifdef DVW
%if CON_ROWS * 2 != 50
 %error "dos.asm reserves 50 bytes for dvw_sums - one word a CON_ROWS row"
%endif
%endif
%else
CON_BSS     equ 0                   ; ...and the carrier's own arithmetic still
                                    ; adds it, so it has a value rather than an
                                    ; %ifdef at every site that reads it
%endif                              ; KD_BACKEND
%ifdef DOS_NETINLINE                ; the network family, for 96.43's reason -
%include "dosnet.inc"               ; THE CABLE TRANSLATION (SPEC.md 96.26) -
%endif                              ; only reached when the route is the cable;
                                    ; in the PART otherwise (96.23.11)
%ifndef KD_BACKEND
%ifndef DOS_NONET
%include "os88sock.inc"             ; net_try - WHICH driver answers (SPEC.md
                                    ; 20.11.1). The packet driver wants the
                                    ; CARD and not the cable, so it asks
                                    ; DRVC_NET by name rather than calling
                                    ; net_find, whose job is to prefer one of
                                    ; two and whose second answer refuses
                                    ; every verb this feature is made of
%endif                              ; DOS_NONET
%endif                              ; KD_BACKEND

%ifndef KD_BACKEND                  ; the mirrors below compare against a
                                    ; library this build does not include
%if DOS_MRADSZ != OS88UI_RD_SIZE
 %error "DOS_MRADSZ must equal os88ui.inc's OS88UI_RD_SIZE - the bss table \
above reserves DOS_MRADSZ bytes for a record this file does not own"
%endif
%if DOS_MRADSEL != OS88UI_RD_SEL
 %error "DOS_MRADSEL must equal os88ui.inc's OS88UI_RD_SEL - [dos_keepc] IS \
that word of the record, and a wrong offset writes the pitch"
%endif
%if DOS_MDRSZ != OS88UI_DR_SIZE
 %error "DOS_MDRSZ must equal os88ui.inc's OS88UI_DR_SIZE - the bss table \
above reserves DOS_MDRSZ bytes for a record this file does not own"
%endif
%if DOS_MDRSEL != OS88UI_DR_SEL
 %error "DOS_MDRSEL must equal os88ui.inc's OS88UI_DR_SEL - [dos_cache] IS \
that word of the record, and a wrong offset writes the window pointer"
%endif
%if DOS_MCKSZ != OS88UI_CK_SIZE
 %error "DOS_MCKSZ must equal os88ui.inc's OS88UI_CK_SIZE - the bss table \
above reserves three records this file does not own"
%endif

%if DOS_LNSZ != OS88LINE_SZ
 %error "DOS_LNSZ must equal os88line.inc's OS88LINE_SZ - the bss table above reserves DOS_LNSZ bytes for a block this file does not own"
%endif
%endif                              ; KD_BACKEND

%ifndef KD_BACKEND
%ifdef DOS_WTAIL
    OS88_BSS DOS_BSS_SIZE           ; the RESIDENT host's: the console's and
                                    ; every WBSS row are the tail's
%else
    OS88_BSS DOS_BSS_SIZE + CON_BSS
%endif
    OS88_IMAGE_END
%endif

; =============================================================================
; BSS - zeroed by the loader (SPEC.md 21 step 5)
; =============================================================================
; THE IVT AND BDA SAVES ARE HERE AND NOT IN THE ARENA, ON PURPOSE (SPEC.md
; 96.5): the arena is the program's to scribble on, and a save area the
; program can corrupt is worse than none, because it fails at restore time
; when there is nothing left to do about it.
; **WHERE THE HOST'S BLOCK STARTS** (SPEC.md 96.44.2): above the core's, whose
; length is a budget rather than a sum, so a cell the core gains does not move
; every cell the host has.
%ifdef DOS_CORE_INLINE
dos_hbss    equ os88_image_end + CORE_BSS_SIZE  ; ...after the core's, which is
%else                                           ; in this same bss
dos_hbss    equ os88_image_end   ; the HOST's block IS the package's bss
%endif

dos_win equ dos_hbss + DOS_B_WIN     ; word: our window
%ifdef DOS_WTAIL
dos_head equ dos_hbss + DOS_B_HEAD   ; byte: no tail in this region (4.4)
%endif
dos_state equ dos_hbss + DOS_B_STATE   ; byte: DST_*
dos_err     equ DOS_CBASE + DOS_B_ERR     ; byte: DER_*, when DST_ERR
dos_exit    equ DOS_CBASE + DOS_B_EXIT    ; byte: the program's exit code
dos_badfn   equ DOS_CBASE + DOS_B_BADFN   ; byte: the AH we lack
dos_dir equ dos_hbss + DOS_B_DIR     ; word: its folder's cluster
dos_vol     equ DOS_CBASE + DOS_B_VOL     ; byte: ...and its volume
dos_name    equ DOS_CBASE + DOS_B_NAME    ; 16:   its NUL 8.3 name
dos_arena   equ DOS_CBASE + DOS_B_ARENA   ; word: the claim, 0 = none
dos_apara   equ DOS_CBASE + DOS_B_APARA   ; word: ...its paragraphs
dos_akb equ dos_hbss + DOS_B_AKB     ; word: ...and its KB, banked
dos_imgsz   equ DOS_CBASE + DOS_B_IMGSZ   ; word: the image's bytes
dos_prgsp   equ DOS_CBASE + DOS_B_PRGSP   ; word: the program's first SP
dos_sv_ss   equ DOS_CBASE + DOS_B_SVSS    ; word: OUR stack, banked
dos_sv_sp   equ DOS_CBASE + DOS_B_SVSP    ; word: ...across the far jump
%ifndef KD_BACKEND
dos_conx equ DOS_WB + DOS_B_CONX    ; the console band (SPEC.md
%endif
%ifndef KD_BACKEND
dos_cony equ DOS_WB + DOS_B_CONY    ; 96.32): top-left in screen
%endif
%ifndef KD_BACKEND
dos_concols equ DOS_WB + DOS_B_CONCOLS ; pixels, size in CELLS, all
%endif
%ifndef KD_BACKEND
dos_conrows equ DOS_WB + DOS_B_CONROWS ; four filled by dos_con_geom
%endif
%ifndef KD_BACKEND
dos_pln equ DOS_WB + DOS_B_PLN     ; the path box (SPEC.md 96.32.1)
%endif
%ifndef KD_BACKEND
dos_path equ DOS_WB + DOS_B_PATH    ; ...and its text
%endif
%ifndef KD_BACKEND
dos_erect equ DOS_WB + DOS_B_ERECT   ; 'Environment' on the bar
%endif
%ifndef KD_BACKEND
dos_rrect equ DOS_WB + DOS_B_RRECT   ; ...and 'Run'
%endif
%ifndef KD_BACKEND
dos_tvol equ DOS_WB + DOS_B_TVOL    ; dos_path_take's scratch pair
%endif
%ifndef KD_BACKEND
dos_tname equ DOS_WB + DOS_B_TNAME
%endif
%ifndef KD_BACKEND
%endif
%ifndef KD_BACKEND
%endif
%ifndef KD_BACKEND
dos_trect equ DOS_WB + DOS_B_TRECT   ; ...and 'Return'
%endif
%ifndef KD_BACKEND
dos_lnv equ DOS_WB + DOS_B_LNV     ; word: LN_VIEW before a key
%endif
%ifndef KD_BACKEND
dos_lnl equ DOS_WB + DOS_B_LNL     ; word: LN_LEN before a key
%endif
%ifndef KD_BACKEND
dos_lnc equ DOS_WB + DOS_B_LNC     ; word: LN_CAR before a key
%endif
%ifndef KD_BACKEND
dos_ncell equ DOS_WB + DOS_B_NCELL   ; word: cells the edits redrew
%endif
%ifndef KD_BACKEND
dos_nkey equ DOS_WB + DOS_B_NKEY    ; word: ...over this many keys
%endif
%ifndef KD_BACKEND
dos_page equ DOS_WB + DOS_B_PAGE    ; byte: DOS_PAGE_*
%endif
%ifndef KD_BACKEND
; --- the button groups' indices, PLUS ONE (os88ui_btn's convention) --------
; 0 is "no button" throughout os88ui.inc, so every index a caller passes or
; reads is one-based and these are what os88ui_btnpress and os88ui_btnup
; answer.
DOS_BT_ENV equ 1                       ; the bar: 'Setup', then 'Run'
DOS_BT_RUN equ 2
DOS_BT_RET equ 1                       ; the Setup page: 'Return', then 'Save
DOS_BT_SAV equ 2                       ; Shortcut' - a different page, so the
                                       ; numbers may repeat
dos_btrec equ DOS_WB + DOS_B_BTREC   ; ...and the button group's record
%endif
%ifndef KD_BACKEND
dos_srect equ DOS_WB + DOS_B_SRECT   ; ...and Save Shortcut's
%endif
%ifndef KD_BACKEND
dos_abon equ DOS_WB + DOS_B_ABON    ; byte: the About card is up
%endif
dos_erp     equ DOS_CBASE + DOS_B_ERP     ; word: the row being emitted
%ifndef KD_BACKEND
dos_lbuf equ dos_hbss + DOS_B_LBUF    ; a .LNK, read or written
%endif
%ifndef KD_BACKEND
dos_sbuf equ dos_hbss + DOS_B_SBUF    ; `.\NAME.EXT` while building
%endif
dos_cname   equ DOS_CBASE + DOS_B_CNAME   ; one path component
dos_fbuf    equ DOS_CBASE + DOS_B_FBUF    ; OSAPI_FIND_SZ, for the walk
%ifndef KD_BACKEND
dos_wname equ DOS_WB + DOS_B_WNAME   ; ...the name to write it under
%endif
%ifndef KD_BACKEND
dos_lend equ dos_hbss + DOS_B_LEND    ; word: bytes of dos_lbuf read
%endif
dos_ebuf    equ DOS_CBASE + DOS_B_EBUF    ; the four environment rows
%ifndef KD_BACKEND
dos_eln equ DOS_WB + DOS_B_ELN     ; ...and their os88line blocks
%endif
dos_args    equ DOS_CBASE + DOS_B_ARGS    ; 128: the command tail the user
                                               ; typed, without its count or
                                               ; its 0Dh - both are DOS's
                                               ; framing and go on at the PSP
dos_pbuf    equ DOS_CBASE + DOS_B_PBUF    ; the program's own path
dos_ln equ DOS_WB + DOS_B_LN      ; the field's os88line block
dos_hkv     equ DOS_CBASE + DOS_B_HKV     ; DHK_NENT words (96.44.3)
dos_bevec   equ DOS_CBASE + DOS_B_BEVEC   ; DBE_NENT words (96.44.1)
%ifdef DOSKPART
dos_pkgname equ dos_hbss + DOS_B_PKGNAME  ; 13: our own 8.3 file name
dos_pkgdir equ dos_hbss + DOS_B_PKGDIR   ; word: its folder's cluster
dos_pkgvol equ dos_hbss + DOS_B_PKGVOL   ; byte: ...and that volume
dos_kdh equ dos_hbss + DOS_B_KDH      ; the handoff record (96.40)
dos_wok equ dos_hbss + DOS_B_WOK      ; byte: arm 3 confirmed (96.42)
%endif
dos_memkb equ dos_hbss + DOS_B_MEMKB   ; word: the arena cap, 0 = all
%ifndef KD_BACKEND
dos_mrad equ dos_hbss + DOS_B_MRAD    ; the radio group's record
%endif
%ifndef KD_BACKEND
dos_keepc   equ dos_mrad + DOS_MRADSEL         ; byte: DOS_MEM_* (SPEC.md 96.36)
%endif
%ifndef KD_BACKEND
dos_mdr equ dos_hbss + DOS_B_MDR     ; the disk cache's drop-down (96.36.6)
%endif
%ifndef KD_BACKEND
dos_cache   equ dos_mdr + DOS_MDRSEL           ; byte: DOS_CA_* - the control's
                                               ; own SEL word, as [dos_keepc] is
%endif
%ifndef KD_BACKEND
dos_mhdd equ dos_hbss + DOS_B_MHDD   ; the two driver boxes (96.36.7)...
%endif
%ifndef KD_BACKEND
dos_mnet equ dos_hbss + DOS_B_MNET
%endif
%ifndef KD_BACKEND
dos_mmou equ dos_hbss + DOS_B_MMOU   ; ...and the mouse's (96.36.8)
%ifdef DVW
dos_mwin equ dos_hbss + DOS_B_MWIN   ; ...and Run in a window (96.32.2.4)
%ifdef DOS_WTAIL
dos_mhead equ DOS_WB + DOS_B_MHEAD  ; ...and Close when done (96.44.20)
%ifdef DOS_WIN16
dos_hne equ DOS_WB + DOS_B_HNE      ; ...and the target is an NE: it greys
dos_hnek equ DOS_WB + DOS_B_HNEK    ; ...and what that was asked of (16)
%endif
%endif
%endif
%ifdef DOS_SETPART
dos_spdoor equ dos_hbss + DOS_B_SPDOOR   ; dword: the Setup part's door
dos_sp_tgt equ dos_hbss + DOS_B_SPTGT    ; word: ...and its bouncer's target
dos_spwall equ dos_hbss + DOS_B_SPWALL  ; word: ...and the KB it walls off
%endif
%ifdef DOS_MEGA
dos_mhma equ dos_hbss + DOS_B_MHMA   ; ...and kern_mega's HMA box (96.40.9.2)
dos_msav equ dos_hbss + DOS_B_MSAV   ; ...and its save box (96.40.9.3)
%endif
%endif
%ifndef KD_BACKEND
dos_skbuf equ dos_hbss + DOS_B_MSKB  ; OSAPI_SYS_KB's buffer (96.36.3)
%endif
%ifndef KD_BACKEND
dos_mhkb equ dos_hbss + DOS_B_MHKB   ; word: what each class holds, banked
%endif
%ifndef KD_BACKEND
dos_mnkb equ dos_hbss + DOS_B_MNKB
%endif
%ifndef KD_BACKEND
dos_msnk equ dos_hbss + DOS_B_MSNK   ; ...and the sound driver's (96.36.7.2)
%endif
%ifndef KD_BACKEND
dos_mbuf equ DOS_WB + DOS_B_MBUF    ; the limit field's text
%endif
%ifndef KD_BACKEND
dos_mln equ DOS_WB + DOS_B_MLN     ; ...and its os88line block
%endif
%ifndef KD_BACKEND
dos_mx equ DOS_WB + DOS_B_MX      ; word: this paint's content x
%endif
%ifndef KD_BACKEND
dos_my equ DOS_WB + DOS_B_MY      ; word: ...and its top
%endif
%ifndef KD_BACKEND
dos_mpx equ DOS_WB + DOS_B_MSX    ; word: a press's x, banked for one click
%endif
%ifndef KD_BACKEND
dos_mpy equ DOS_WB + DOS_B_MSY    ; word: ...and its y (SPEC.md 96.36.4)
%endif
dos_pic1    equ DOS_CBASE + DOS_B_PIC1    ; byte: the 8259 masks as found
dos_pic2    equ DOS_CBASE + DOS_B_PIC2    ; byte:
dos_isexe   equ DOS_CBASE + DOS_B_ISEXE   ; byte: 1 = an .EXE was set up
dos_m33seg  equ DOS_CBASE + DOS_B_M33SEG  ; word: the trace part, or 0 (96.10.3)
dos_m33h    equ DOS_CBASE + DOS_B_M33H    ; --- the event handler (96.10.4) ---
dos_m33m    equ DOS_CBASE + DOS_B_M33M
dos_m33oh   equ DOS_CBASE + DOS_B_M33OH
dos_m33om   equ DOS_CBASE + DOS_B_M33OM
dos_m33old  equ DOS_CBASE + DOS_B_M33OLD
dos_m33lx   equ DOS_CBASE + DOS_B_M33LX
dos_m33ly   equ DOS_CBASE + DOS_B_M33LY
dos_m33lb   equ DOS_CBASE + DOS_B_M33LB
dos_m33hk   equ DOS_CBASE + DOS_B_M33HK
%ifdef DOS_MEGA
dos_hslot   equ DOS_CBASE + DOS_B_HSLOT
%endif
dos_m33bsy  equ DOS_CBASE + DOS_B_M33BSY
dos_m33shw  equ DOS_CBASE + DOS_B_M33SHW  ; --- the text cursor (96.10.5) ---
dos_m33x0   equ DOS_CBASE + DOS_B_M33X0  ; --- the window (96.10.7) -------
dos_m33x1   equ DOS_CBASE + DOS_B_M33X1
dos_m33y0   equ DOS_CBASE + DOS_B_M33Y0
dos_m33y1   equ DOS_CBASE + DOS_B_M33Y1
dos_m33sm   equ DOS_CBASE + DOS_B_M33SM
dos_m33cm   equ DOS_CBASE + DOS_B_M33CM
dos_m33dv   equ DOS_CBASE + DOS_B_M33DV
dos_m33do   equ DOS_CBASE + DOS_B_M33DO
dos_m33dc   equ DOS_CBASE + DOS_B_M33DC
%ifdef DOSTRACE
dos_tracen equ dos_hbss + DOS_B_TRACEN  ; word: DOSTRACE's call counter
dos_tracew equ dos_hbss + DOS_B_TRACEW  ; word: its ring write index
dos_traceb  equ DOS_TRB_OFF         ; **AN OFFSET IN THE PART, NOT IN US**
                                    ; (SPEC.md 96.29.1), so every reference to
                                    ; it carries an `es:` and ES is
                                    ; [dos_trseg]. An unprefixed one reads our
                                    ; own image at that offset, which
                                    ; assembles cleanly and traces nonsense
dos_tracei equ dos_hbss + DOS_B_TRACEI  ; ...the live entry's offset
dos_trnm equ dos_hbss + DOS_B_TRNM   ; the names passed in
dos_trnmi equ dos_hbss + DOS_B_TRNMI  ; ...how many so far
dos_trdump  equ DOS_TRD_OFF         ; ...rendered, for the file - and in the
                                    ; part beside the ring. OSAPI_FILE_WRITE
                                    ; takes ES:BX, so handing it over costs
                                    ; nothing at all
dos_trseg equ dos_hbss + DOS_B_TRSEG
%endif
dos_vw      equ DOS_CBASE + DOS_B_VW      ; word: the desktop's width...
dos_vh      equ DOS_CBASE + DOS_B_VH      ; word: ...and height, for 33h
dos_mou_lx  equ DOS_CBASE + DOS_B_MLX     ; word: the last position 0Bh
dos_mou_ly  equ DOS_CBASE + DOS_B_MLY     ; word: ...answered a delta from
dos_mou_lb  equ DOS_CBASE + DOS_B_MLB     ; byte: the mask the last state
dos_mou_pc  equ DOS_CBASE + DOS_B_MPC     ;       read saw, for the edges
dos_mou_rc  equ DOS_CBASE + DOS_B_MRC     ; 2 bytes each, INDEXED BY THE
dos_mou_px  equ DOS_CBASE + DOS_B_MPX     ; button number, so they are a
dos_mou_py  equ DOS_CBASE + DOS_B_MPY     ; pair and not two names
dos_mou_rx  equ DOS_CBASE + DOS_B_MRX
dos_mou_ry  equ DOS_CBASE + DOS_B_MRY
dos_wseg    equ DOS_CBASE + DOS_B_WSEG
dos_wbytes  equ DOS_CBASE + DOS_B_WBYTES
dos_wown    equ DOS_CBASE + DOS_B_WOWN
dos_wfill   equ DOS_CBASE + DOS_B_WFILL
dos_wdirty  equ DOS_CBASE + DOS_B_WDIRTY
dos_wbase   equ DOS_CBASE + DOS_B_WBASE
dos_wlen    equ DOS_CBASE + DOS_B_WLEN
dos_fname   equ DOS_CBASE + DOS_B_FNAME
dos_fent    equ DOS_CBASE + DOS_B_FENT
dos_bk_ss   equ DOS_CBASE + DOS_B_BKSS
dos_bk_sp   equ DOS_CBASE + DOS_B_BKSP
dos_dstk    equ DOS_CBASE + DOS_B_DSTK
dos_gax     equ DOS_CBASE + DOS_B_GAX
dos_gbp     equ DOS_CBASE + DOS_B_GBP
dos_gip     equ DOS_CBASE + DOS_B_GIP
dos_gcs     equ DOS_CBASE + DOS_B_GCS
dos_gfl     equ DOS_CBASE + DOS_B_GFL
dos_gss     equ DOS_CBASE + DOS_B_GSS
%ifdef DOS_PMGATE
dos_gds     equ DOS_CBASE + DOS_B_GSP   ; the program's DS (the gate's PM arm)
%endif
dos_betgt   equ DOS_CBASE + DOS_B_BETGT
dos_beflg   equ DOS_CBASE + DOS_B_BEFLG
dos_onprog  equ DOS_CBASE + DOS_B_ONPRG
dos_fhix    equ DOS_CBASE + DOS_B_FHIX
dos_fhtab   equ DOS_CBASE + DOS_B_FHTAB
dos_vcb     equ DOS_CBASE + DOS_B_VCB
dos_dta     equ DOS_CBASE + DOS_B_DTA
dos_dtaseg  equ DOS_CBASE + DOS_B_DTASEG
dos_dtasv   equ DOS_CBASE + DOS_B_DTASV
dos_dtasvs  equ DOS_CBASE + DOS_B_DTASVS
dos_ford    equ DOS_CBASE + DOS_B_FORD
dos_w83a    equ DOS_CBASE + DOS_B_W83A
dos_w83b    equ DOS_CBASE + DOS_B_W83B
dos_parent  equ DOS_CBASE + DOS_B_PARENT
dsh_line    equ DOS_CBASE + DOS_B_SHLINE
dsh_verb    equ DOS_CBASE + DOS_B_SHVERB
dsh_a1      equ DOS_CBASE + DOS_B_SHA1
dsh_a2      equ DOS_CBASE + DOS_B_SHA2
dsh_spec    equ DOS_CBASE + DOS_B_SHSPEC
dsh_leaf    equ DOS_CBASE + DOS_B_SHLEAF
dsh_dname   equ DOS_CBASE + DOS_B_SHDNAM
dsh_rtgt    equ DOS_CBASE + DOS_B_SHRTGT
dsh_pat     equ DOS_CBASE + DOS_B_SHPAT
dsh_nm11    equ DOS_CBASE + DOS_B_SHNM11
dsh_fname   equ DOS_CBASE + DOS_B_SHFNAM
dsh_fnd     equ DOS_CBASE + DOS_B_SHFND
dsh_num     equ DOS_CBASE + DOS_B_SHNUM
dsh_cpheap  equ DOS_CBASE + DOS_B_SHCPHEAP
dsh_quiet   equ DOS_CBASE + DOS_B_SHQUIET
dsh_asdir   equ DOS_CBASE + DOS_B_SHASDIR
dsh_del     equ DOS_CBASE + DOS_B_SHDEL
dsh_dirop   equ DOS_CBASE + DOS_B_SHDIROP
dsh_skip    equ DOS_CBASE + DOS_B_SHSKIP
dsh_n       equ DOS_CBASE + DOS_B_SHN
dsh_off     equ DOS_CBASE + DOS_B_SHOFF
dsh_bvol    equ DOS_CBASE + DOS_B_SHBVOL
dsh_bclus   equ DOS_CBASE + DOS_B_SHBCLUS
dsh_rdrv    equ DOS_CBASE + DOS_B_SHRDRV
dsh_rclus   equ DOS_CBASE + DOS_B_SHRCLUS
dsh_sdrv    equ DOS_CBASE + DOS_B_SHSDRV
dsh_sclus   equ DOS_CBASE + DOS_B_SHSCLUS
dsh_ddrv    equ DOS_CBASE + DOS_B_SHDDRV
dsh_dclus   equ DOS_CBASE + DOS_B_SHDCLUS
dsh_cname   equ DOS_CBASE + DOS_B_SHCNAME
dsh_cpseg   equ DOS_CBASE + DOS_B_SHCPSEG
dsh_cpkb    equ DOS_CBASE + DOS_B_SHCPKB
dsh_made    equ DOS_CBASE + DOS_B_SHMADE
dsh_got     equ DOS_CBASE + DOS_B_SHGOT
dsh_rcur    equ DOS_CBASE + DOS_B_SHRCUR
dsh_wtok    equ DOS_CBASE + DOS_B_SHWTOK
dsh_why     equ DOS_CBASE + DOS_B_SHWHY
dsh_dsw     equ DOS_CBASE + DOS_B_SHDSW   ; DIR's switches (SPEC.md 96.33.9)
dsh_more    equ DOS_CBASE + DOS_B_SHMORE  ; ...a listing is suspended
dsh_ord     equ DOS_CBASE + DOS_B_SHORD   ; ...at this ordinal
dsh_ln      equ DOS_CBASE + DOS_B_SHLN    ; ...this many lines on the page
dsh_wcol    equ DOS_CBASE + DOS_B_SHWCOL  ; ...and /W's column
dsh_sseg    equ DOS_CBASE + DOS_B_SHSSEG  ; DIR's sort claim (96.33.9.2)
dsh_sn      equ DOS_CBASE + DOS_B_SHSN    ; ...records in it
dsh_si      equ DOS_CBASE + DOS_B_SHSI    ; ...and the emit cursor
dos_inbr    equ DOS_CBASE + DOS_B_INBR    ; the console's five (96.33)
dos_fromcon equ dos_hbss + DOS_B_FROMCON
dos_fsxup   equ DOS_CBASE + DOS_B_FSXUP
dos_ispkg equ dos_hbss + DOS_B_ISPKG   ; byte: the name ends in .O88
dos_pkgq equ dos_hbss + DOS_B_PKGQ    ; byte: a package launch posted
dos_pkgn equ dos_hbss + DOS_B_PKGN    ; 13:   ...and which one
dos_ptres equ dos_hbss + DOS_B_PTRES   ; dos_path_take's resolve-only pair
dos_ptbase equ dos_hbss + DOS_B_PTBASE
dos_pgdir equ dos_hbss + DOS_B_PGDIR   ; ...and what it answers in
dos_pgvol equ dos_hbss + DOS_B_PGVOL
dos_pgpdir equ dos_hbss + DOS_B_PGPDIR ; the program's, banked (96.33.21.1)
dos_pgpvol equ dos_hbss + DOS_B_PGPVOL
dos_pgdoc equ dos_hbss + DOS_B_PGDOC   ; 16:   the document locator (21.5.3)
dos_pghas equ dos_hbss + DOS_B_PGHAS
dos_fsxgo equ dos_hbss + DOS_B_FSXGO
dos_aedn equ dos_hbss + DOS_B_AEDN     ; byte: 96.33.5.1's edge
dsh_exec    equ DOS_CBASE + DOS_B_SHEXEC
dos_cmdx equ dos_hbss + DOS_B_CMDX
%ifdef DOS_MEGA
%ifndef KD_BACKEND
dos_snrec equ dos_hbss + DOS_B_SNAP ; 4: the trip's snap record (14.106)
%endif
%endif
dos_cmdn equ dos_hbss + DOS_B_CMDN
dos_fhpath  equ DOS_CBASE + DOS_B_FHPATH
dos_fpbuf   equ DOS_CBASE + DOS_B_FPBUF
dos_fhcwd   equ DOS_CBASE + DOS_B_FHCWD
dos_fhmoved equ DOS_CBASE + DOS_B_FHMOVED
dos_fhkeep  equ DOS_CBASE + DOS_B_FHKEEP
dos_inchild equ DOS_CBASE + DOS_B_INCHLD
dos_psv_ss  equ DOS_CBASE + DOS_B_PSVSS
dos_psv_sp  equ DOS_CBASE + DOS_B_PSVSP
dos_ppsp    equ DOS_CBASE + DOS_B_PPSP
dos_ppara   equ DOS_CBASE + DOS_B_PPARA
dos_pgpar   equ DOS_CBASE + DOS_B_PGPAR
dos_pexe    equ DOS_CBASE + DOS_B_PEXE
dos_chexit  equ DOS_CBASE + DOS_B_CHEXIT
dos_chblk   equ DOS_CBASE + DOS_B_CHBLK
dos_dqbuf equ dos_hbss + DOS_B_DQBUF
dos_blaster equ DOS_CBASE + DOS_B_BLAST
dos_xmstab  equ DOS_CBASE + DOS_B_XMSTAB
dos_xmlen   equ DOS_CBASE + DOS_B_XMLEN
dos_xmsh    equ DOS_CBASE + DOS_B_XMSH
dos_xmso    equ DOS_CBASE + DOS_B_XMSO
dos_xmdh    equ DOS_CBASE + DOS_B_XMDH
dos_xmdo    equ DOS_CBASE + DOS_B_XMDO
dos_xmlin   equ DOS_CBASE + DOS_B_XMLIN
dos_xmcon   equ DOS_CBASE + DOS_B_XMCON
dos_xmdir   equ DOS_CBASE + DOS_B_XMDIR
dos_xparm   equ DOS_CBASE + DOS_B_XPARM
dos_xparms  equ DOS_CBASE + DOS_B_XPARMS
dos_ldname  equ DOS_CBASE + DOS_B_LDNAME
dos_ldpsp   equ DOS_CBASE + DOS_B_LDPSP
dos_ldpara  equ DOS_CBASE + DOS_B_LDPAR
dos_dy      equ DOS_CBASE + DOS_B_DY
dos_dm      equ DOS_CBASE + DOS_B_DM
dos_dd      equ DOS_CBASE + DOS_B_DD
dos_lasttl  equ DOS_CBASE + DOS_B_LTL
dos_lastth  equ DOS_CBASE + DOS_B_LTH
dos_tmp1    equ DOS_CBASE + DOS_B_TMP1
dos_tmp2    equ DOS_CBASE + DOS_B_TMP2
dos_tmp3    equ DOS_CBASE + DOS_B_TMP3
dos_acc     equ DOS_CBASE + DOS_B_ACC
dos_fabs    equ DOS_CBASE + DOS_B_FABS
%if DOS_B_FHPATH != DOS_B_FABS + 1
  %error "dos_cd_go reads [dos_fabs] and [dos_fhpath] as one word"
%endif
dos_pfbuf   equ DOS_CBASE + DOS_B_PFBUF   ; AH=29h's name scratch...
dos_pfcb    equ DOS_CBASE + DOS_B_PFCB    ; ...and its FCB prefix
dos_fname2  equ DOS_CBASE + DOS_B_FNAME2  ; AH=56h's second name
dos_rnvol   equ DOS_CBASE + DOS_B_RNVOL   ; ...and its three bytes of
dos_rndrv   equ DOS_CBASE + DOS_B_RNDRV   ; drive arithmetic
dos_rnabs   equ DOS_CBASE + DOS_B_RNABS
dos_vfy     equ DOS_CBASE + DOS_B_VFY     ; AH=2Eh/54h's byte
dos_fnseg   equ DOS_CBASE + DOS_B_FNSEG   ; word: where a name is read from
dos_fdrv    equ DOS_CBASE + DOS_B_FDRV    ; byte: the drive a name named
dos_pvol    equ DOS_CBASE + DOS_B_PVOL    ; byte: the volume the MACHINE is
dos_pdir    equ DOS_CBASE + DOS_B_PDIR    ; word: ...and the folder in it
dos_fvtgt   equ DOS_CBASE + DOS_B_FVTGT   ; word: the read's back end
dos_opmode  equ DOS_CBASE + DOS_B_OPMODE  ; byte: AH=3Dh's access mode
dos_wfil    equ DOS_CBASE + DOS_B_WFIL    ; byte: fill the window, not copy
dos_gapn    equ DOS_CBASE + DOS_B_GAPN    ; dword: the gap still to lay
dos_trnof   equ DOS_CBASE + DOS_B_TRNOF   ; dword: a shrink's copy offset
dos_cpw equ dos_hbss + DOS_B_CPW     ; byte: resuming on the pass
dos_drvout equ dos_hbss + DOS_B_DRVOUT  ; byte: the drivers are out
dos_curdir  equ DOS_CBASE + DOS_B_CURDIR
dos_dvcwd   equ DOS_CBASE + DOS_B_DVCWD   ; per drive: its cluster
dos_dvtgt   equ DOS_CBASE + DOS_B_DVTGT   ; ...and the one it goes to
dos_dvfrom  equ DOS_CBASE + DOS_B_DVFROM  ; the drive a switch is leaving
dos_ndrv    equ DOS_CBASE + DOS_B_NDRV    ; AH=0Eh's count, 0 = unprobed
dos_cwdst   equ DOS_CBASE + DOS_B_CWDST   ; AH=47h's destination
dos_cwdrv   equ DOS_CBASE + DOS_B_CWDRV   ; ...and the drive asked about
dos_imghi   equ DOS_CBASE + DOS_B_IMGHI   ; word: the file's size, high
dos_exe_fseg equ DOS_CBASE + DOS_B_XFSEG  ; word: where the FILE landed
dos_exe_lseg equ DOS_CBASE + DOS_B_XLSEG  ; word: ...and the load segment
dos_exe_hpara equ DOS_CBASE + DOS_B_XHPAR ; word: header paragraphs
dos_exe_nrel equ DOS_CBASE + DOS_B_XNREL  ; word: relocation entries
dos_exe_rloc equ DOS_CBASE + DOS_B_XRLOC  ; word: ...where the table is
dos_exe_minal equ DOS_CBASE + DOS_B_XMINA ; word: paragraphs it must have
dos_exe_ipara equ DOS_CBASE + DOS_B_XIPAR ; word: the image's paragraphs
dos_exe_cs  equ DOS_CBASE + DOS_B_XCS     ; word: the entry state, all
dos_exe_ip  equ DOS_CBASE + DOS_B_XIP     ; word: four out of the header
dos_exe_ss  equ DOS_CBASE + DOS_B_XSS     ; word: and CS/SS relocated
dos_exe_sp  equ DOS_CBASE + DOS_B_XSP     ; word:
dos_fsi equ dos_hbss + DOS_B_FSI     ; FSI_SIZE: the fsx info block
dos_ivt     equ DOS_CBASE + DOS_B_IVT     ; 1024: the whole vector table
dos_bda     equ DOS_CBASE + DOS_B_BDA     ; 256:  ...and the whole BDA

; --- the packet driver's (SPEC.md 96.23) -------------------------------------
%ifdef DOS_NETINLINE                ; 96.43; a PART's are its own (96.23.11)
dos_pkt_vec equ dos_hbss + DOS_B_PKTVEC
dos_pkt_raw   equ dos_hbss + DOS_B_PKTRAW
dos_pkt_busy equ dos_hbss + DOS_B_PKTBSY
dos_pkt_mode equ dos_hbss + DOS_B_PKTMODE
dos_pkt_htab equ dos_hbss + DOS_B_PKTHTAB
dos_pkt_mac equ dos_hbss + DOS_B_PKTMAC
dos_pkt_cvec equ dos_hbss + DOS_B_PKTCVEC
dos_pkt_chand equ dos_hbss + DOS_B_PKTCHAND
dos_pkt_old08 equ dos_hbss + DOS_B_PKTOLD08
dos_pkt_sss equ dos_hbss + DOS_B_PKTSSS
dos_pkt_ssp equ dos_hbss + DOS_B_PKTSSP
dos_pkt_can equ dos_hbss + DOS_B_PKTCAN
dos_pkt_cds equ dos_hbss + DOS_B_PKTCDS
dos_pkt_stats equ dos_hbss + DOS_B_PKTSTAT

PN_BUFSYMS 0                        ; ...and the network claim's offsets, at its
                                    ; base: dospktabi.inc (SPEC.md 96.26.6)
%endif
%ifdef DOS_NETPART
dos_pkt_door equ dos_hbss + DOS_B_PKTDOOR  ; the far door, whose SEGMENT word
%endif                                     ; IS [dos_pkt_bseg] (96.23.11)
%ifdef DOS_NETANY
dos_pkt_bseg equ dos_hbss + DOS_B_PKTBSEG
dos_pkt_xl equ dos_hbss + DOS_B_PKTXL
%endif

; --- AND THE bss SHIPS INSIDE THE PART (SPEC.md 96.44.4, 51.1.2) ------------
; `DOSKPART` is the arm that becomes PART 0 of `DOS.O88`, and a part is not
; zeroed by the loader: `ld_start` jumps to step 8 and not step 7, precisely so
; that the handoff `apps/dos/dosload.asm` wrote into the head of this bss
; survives the re-home (SPEC.md 20.12.10.2). So the bytes have to BE there, as
; zeros in the file - which is nothing on the disk, because a run of zeros is
; what LZ4 is best at and the row is `OP_COMP`.
;
; ONLY ON THIS ARM. The shipped `build/dos.o88` is an ordinary v3 package whose
; bss the loader zeroes, and padding it would put 11,839 bytes on every floppy
; for nothing. `tests/unit/t_pkg.py` reads the built files rather than this
; comment.
%ifdef DOSKPART
    times OS88_BSS_SIZE db 0
%endif
; --- ...AND THE TAIL'S OWN bss, AFTER ITS CODE (DOS-HEADLESS-PLAN 4.4) ------
; Zeros in the file for the reason above, at the very end of the tail, so the
; parts standard's OP_ZFILL keeps them off the disk and off the decoder.
%ifdef DOS_WTAIL
DOS_WBSS_SIZE equ WB + CON_BSS
section .wtext
dos_wbss:
    times DOS_WBSS_SIZE db 0
dos_wtail_end:                      ; ...where part 1 ends: the Setup page's
section .text                       ; headless figure adds DOS_WTAILKB
%define DOS_WTAILKB ((dos_wtail_end - dos_wtail_beg) >> 10)
%endif

; --- THE SETUP PART, APPENDED (SPEC.md 96.32.2.3) ----------------------------
; Only the Makefile's second assembly (-DDOS_SETEMIT) emits it, AFTER every
; byte of the host above - so the host is the same bytes either way, which
; tools/dossetchk.py checks before it cuts the part off. vstart=0: the part is
; read into a claim of its own and runs at that claim's offset 0.
%ifdef DOS_SETEMIT
%ifdef DOS_WTAIL
section .setp vstart=0 follows=.wtext align=16
%else
section .setp vstart=0 follows=.text align=16
%endif
%include "dosset.inc"
section .text
%endif
