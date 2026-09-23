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
    OS88_HEADER 'DOS', dos_entry, 3 | OS88_F_GLYPH | OS88_F_PARTS
%else
    OS88_HEADER 'DOS', dos_entry, 3 | OS88_F_GLYPH
%endif
                                        ; flags bit 0 = icon, bit 1 = the
                                        ; association block after it, bit 5
                                        ; the document glyph after THAT
                                        ; (SPEC.md 54.3.2)

%include "dosicon.inc"          ; the icon, the association block and the
                                ; document glyph, shared with dosload.asm
%endif                              ; KD_BACKEND

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
%ifndef KD_BACKEND                  ; THE BOX'S reservation only: kern_dos makes
%ifdef DOS_EXTCORE                  ; its own in kerndos/kdos.asm, above its
                                    ; API refusal wall, and would otherwise
                                    ; make a second one here
  %if ($ - $$) > CORE_ORG
    %error "the box's own header and icon reached CORE_ORG - raise it in \
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
DSHW_BADSW  equ 10              ; a switch this box has not got (96.33.9)

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
; what the page promised against the arena the box carries home (96.41.1).
DOS_LOWKB   equ 1                   ; KB below KERNEL_SEG: the IVT, the BDA and
                                    ; the boot area. 0x0060 paragraphs is 1.5
                                    ; and this rounds DOWN, which is the
                                    ; direction an estimate should err
DOS_KDKB    equ 42                  ; ...and what `kern_dos` keeps below the
                                    ; program: its own image, the FAT window,
                                    ; the mount buffers and the stack -
                                    ; `LOW_SEG + KD_LOW_KB * 64` over there,
                                    ; in KB
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
                                            ; the callbacks the box actually
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
; window SPANS the screen on every adapter, because on two of the three there
; is no other way to hold the thing it is for.
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
; the DOS programs this box exists for want one or two: BLASTER= is written
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
LNK_EXT2SZ  equ 12                  ; size(4) + signature(4) + memkb(2) + a
                                    ; byte for the cache and one of padding
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
DOS_MHDDY   equ 46                  ; [x] Hard drives (Up to NNK) - arm 0's
DOS_MNETY   equ 59                  ; [x] Network (Up to NNK)
DOS_MFLDY   equ 72                  ; Limit: [____] K
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
DOS_CA_AUTO   equ 0                 ; what the box picks - 9 KB on arm 1 today
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
                                    ; this box can walk, so a truncation here
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
DST_CPWAIT  equ 4                   ; ...and it is waiting for the heap to be
                                    ; packed (SPEC.md 96.35). A posted
                                    ; OSAPI_MEM_COMPACT's post runs at ui_task's
                                    ; step 0, and the EVT_WAKE it sends lands
                                    ; here - so this is one more state and not
                                    ; a lifecycle, which is why a stale wake
                                    ; still finds the state advanced

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
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

; -----------------------------------------------------------------------------
; dos_entry - package entry (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear
;
; ARG_FILE is READ-AND-CLEAR and its name lives in the KERNEL segment, so it
; is copied out through ES before anything else is called (SPEC.md 54.5).
; -----------------------------------------------------------------------------
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
    ; through the ES a package proc was entered with (SPEC.md 74240). Every
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
    pop es
%endif

    mov si, dos_tpl                 ; THE TEMPLATE IS THE VGA/CGA SIZE and
    call OSAPI_WM_CREATE            ; dos_pref is the rest (SPEC.md 96.32).
    jc .out                         ; dos_size is GONE: it computed 90% of the
    mov [dos_win], bx               ; desktop band with a mul and a div, and
                                    ; the requirement is a COUNT of columns
    mov si, dos_menus               ; ...and the menu bar gains a Program menu
    call OSAPI_MENU_SET             ; (SPEC.md 96.32.3)

    push dx
    mov ax, bx                      ; **AND THE BUTTONS' GESTURE** (SPEC.md
    mov bx, dos_btrec               ; 20.5.1.3): btninit installs all THREE
    mov si, dos_onup                ; slots - the press, the release and the
    mov di, dos_ondrag              ; tracking edge - and none of them is a
    mov dx, dos_click               ; template word except the press, which
    call os88ui_btninit             ; 20.5.1.3.3's cell now lets it write
    pop dx                          ; late. dos_click is OUR own click work
    mov bx, [dos_win]               ; and the library chains to it

%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    mov al, KSC_ALT                 ; **ASK ONCE, TO ARM THE KEY-STATE MAP**
    call OSAPI_KEY_DOWN             ; (SPEC.md 9.7): kbm_isr does not track a
                                    ; scancode until something has asked, so
                                    ; without this call neither half of
                                    ; 96.33.5.1 can see Alt+Enter - not the
                                    ; kernel's latch and not our own poll. The
                                    ; ANSWER is discarded; the asking is the
                                    ; whole point, and it is what keeps the
                                    ; feature costing a machine that never
                                    ; opens this box exactly nothing
%endif

    OS88_REGION_MOVABLE             ; **AND OUR REGION MAY MOVE** (SPEC.md
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
    mov si, dos_about
    call OSAPI_ABOUT_SET

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

    call dos_lnk_open               ; A SHORTCUT names another program and
                                    ; carries its arguments (SPEC.md 96.21);
                                    ; anything else is the program itself
    call dos_path_make              ; ...and EITHER WAY the box shows the
                                    ; fully qualified path of what is about to
                                    ; run (SPEC.md 96.32.3). AFTER the link is
                                    ; opened, because a link rewrites
                                    ; [dos_name] and the path must be the
                                    ; PROGRAM's rather than the shortcut's.
                                    ; A refusal leaves the bare name there,
                                    ; which is still true and still runs
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

.idle:
    mov byte [dos_state], DST_IDLE
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
    ; left `A:\>` on the glass over a box standing on B:.
    call dos_prompt                 ; ...and BEFORE the `clc`: it spends the
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
%ifndef KD_BACKEND                  ; 96.43: the console is the window's
    cmp byte [dos_pkgq], 0          ; **A `.O88` TYPED AT THE PROMPT** (SPEC.md
    je .nopkg                       ; 96.33.17): OSAPI_PKG_START wants the gfx
    call dos_pkg_go                 ; lock FREE and W_ONKEY holds it, so the
    jmp short .out                  ; console posts and this is where it lands
                                    ; - the same place a DOS program's own
                                    ; launch is serviced, and for the same
                                    ; reason. It is tested FIRST and clears its
                                    ; own flag, so it neither reads nor moves
                                    ; [dos_state]: a package is not the thing
                                    ; `run it again` re-runs
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
    ; a box sitting at DST_READY would otherwise launch the program a second
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
    mov byte [dos_state], DST_RAN   ; the words the box already had
.said:
    ; **AND THE ARENA IT WAS GIVEN** (SPEC.md 96.41.1), which came home in the
    ; next cell. The box cannot work this one out: `[dos_akb]` is `dos_run`'s
    ; banked figure and on this arm `dos_run` posted and returned without ever
    ; claiming, so what stands there is the last WINDOWED launch's number or
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
    ; ...and the console says so HERE, which is where the run really ended.
    ; `dos_run`'s own `.out` cannot: the post is spent long before the program
    ; starts, so a line written there is about a launch that has not happened
    ; (SPEC.md 96.35.1). Before `dos_swap`, which is the repaint, for
    ; `dos_repaint`'s reason one arm over.
    call dos_con_ended
    mov bx, [dos_win]
    call dos_swap
    jmp short .out
.nocode:
%endif
    cmp byte [dos_state], DST_CPWAIT
    je .go                          ; the compaction has run and the heap is
                                    ; packed BOTH ways: dos_run picks up at the
                                    ; claim, and plain OSAPI_MEM_AVAIL is exact
%ifdef DOSKPART
    ; --- A RESUME THIS BOX NEVER SAW (SPEC.md 96.35.5) ---------------------
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
    ; exit code to deliver - so this box is restored onto the desktop with its
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
%ifdef DOSKPART
    call dos_wholeask           ; SPEC.md 96.42: arm 3 with nothing to come
    jc .out                     ; back to asks FIRST, and the answer comes back
%endif                          ; through this same door
.go:
    mov byte [dos_state], DST_RAN
    call dos_run
.out:
    ret

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
%ifndef KD_BACKEND
    call dos_con_starting           ; **SAID BEFORE IT HAPPENS** (SPEC.md
                                    ; 96.33.20.1), and HERE rather than at
                                    ; `.there`: everything above this point
                                    ; runs again on the compaction wake, and a
                                    ; launch that had to wait for a pass would
                                    ; announce itself twice
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
    ; The field read it as the box loading something in order to exit. Every
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
    cmp byte [dos_cpw], 0
    jne .ask                        ; on the wake the heap IS packed, so plain
                                    ; avail is exact and posting again is how a
                                    ; program spins (SPEC.md 66.4.3.2)
    cmp word [dos_memkb], 0
    je .unmount                     ; NO CAP: we want the maximum, so the
                                    ; question is only whether a pass adds any
    push bx                         ; ...A CAP, and the cheaper road: a program
    call OSAPI_MEM_AVAIL            ; that asked for 200K on a machine with
    pop bx                          ; 300K free needs no compaction and no
                                    ; silence. Net of the floor, like every
                                    ; AVAIL on this task from .sized on
    cmp ax, [dos_memkb]
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
    cmp word [dos_memkb], 0
    jne .capmax
    cmp ax, [dos_akb]               ; no cap: does a pass add anything at all?
    jbe .ask                        ; no - claim what is there
    jmp short .post
.capmax:
    cmp ax, [dos_memkb]             ; a cap: could a pass even fill it?
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
    mov dx, [dos_memkb]             ; ...and the user's own cap, if there is
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
    mov dx, [dos_imghi]             ; a .COM is ONE segment: 64KB - the PSP -
    or dx, dx                       ; the pushed word is the ceiling, so a high
    jnz .toobig                     ; word at all is a file that cannot be one
    cmp word [dos_imgsz], 0xFF00
    jbe .ready
.toobig:
    mov al, DER_BIG
    jmp short .freeerr
.ready:

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
    mov [dos_err], al
    mov byte [dos_state], DST_ERR
.free:
%ifdef DOSTRACE
    call dos_trace_dump             ; ...and the field gets to read it too
%endif
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
    mov byte [dos_cpw], 0           ; THE REAL EXIT, so the next launch in this
                                    ; instance sizes from the start again
%ifndef KD_BACKEND
    call dn_shut                    ; every translated flow closed, before the
                                    ; driver that owns its sockets is resumed
    call dos_pkt_shut               ; THE RAW CLAIM GOES BACK FIRST, on every
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
.outq:                              ; ...AND THE POSTED PATH, which reaches
                                    ; none of the above on purpose: nothing has
                                    ; ended, the drivers must stay out for the
                                    ; pass to have their space, and the window
                                    ; still says what it said (SPEC.md 96.35.1)
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    call OSAPI_GFX_UNLOCK
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
; in:  [dos_ldpsp], [dos_ldpara], [dos_name]
; out: CF=0 and [dos_imgsz] = the bytes; CF=1 with AL = a DER_*
; -----------------------------------------------------------------------------
dos_load:
    push bx
    push cx
    push dx
    push si
    push es

    mov ax, [dos_ldpsp]             ; THE PROGRAM BEING LOADED, not the arena
    add ax, 16                      ; (SPEC.md 96.14): a child from AH=4Bh is
    mov es, ax                      ; loaded exactly this way into a block of
    xor bx, bx                      ; its own, and everything below here would
                                    ; otherwise be the first program's for ever
    mov ax, [dos_ldpara]            ; the capacity is everything from the image
    sub ax, 16                      ; to the top of ITS block, in paragraphs...
    mov dx, 16
    mul dx                          ; ...as a 32-bit byte count in DX:AX, which
    mov cx, ax                      ; is what OSAPI_FILE_READ takes in DX:CX
    mov si, [dos_ldname]            ; THE NAME IS AN ARGUMENT TOO: AH=4Bh loads
                                    ; a file the running program named, and
                                    ; [dos_name] is the one the DESKTOP did -
                                    ; which made the first child a second copy
                                    ; of its own parent
    call dos_be_read                ; DX:AX = bytes read
    jc .rerr

    mov [dos_imgsz], ax             ; the WHOLE 32-bit size: an .EXE may be
    mov [dos_imghi], dx             ; bigger than a segment and the .COM
    pop es                          ; ceiling is the .COM path's business
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret
.rerr:
    mov al, DER_READ
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    stc
    ret
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
    mov ax, [es:MZ_CS]
    mov [dos_exe_cs], ax
    mov ax, [es:MZ_IP]
    mov [dos_exe_ip], ax
    mov ax, [es:MZ_SS]
    mov [dos_exe_ss], ax
    mov ax, [es:MZ_SP]
    mov [dos_exe_sp], ax
    mov ax, [es:MZ_CPARHDR]
    mov [dos_exe_hpara], ax
    mov ax, [es:MZ_CRLC]
    mov [dos_exe_nrel], ax
    mov ax, [es:MZ_LFARLC]
    mov [dos_exe_rloc], ax
    mov ax, [es:MZ_MINALLOC]
    mov [dos_exe_minal], ax

    ; --- the image's size, in bytes then paragraphs ------------------------
    mov ax, [es:MZ_CP]              ; pages INCLUDING the header. A last-page
    or ax, ax                       ; count of 0 means the last page is FULL,
    jz .bad                         ; which is the encoding everybody forgets
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
    jc .bad

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
.noseed:

    call OSAPI_VIDEO                ; AX = width, BX = height: INT 33h's scale
    mov [dos_vw], ax                ; (SPEC.md 96.10). Asked ONCE, here, and
    mov [dos_vh], bx                ; not per call - it cannot change inside a
                                    ; bracket and a divide is 80+ clocks

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

    call dos_prog_enter             ; ...and away (SPEC.md 96.14): the same
                                    ; door AH=4Bh's child goes through
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_prog_done:                      ; the INT 21h terminate path jumps here,
                                    ; having already put SS:SP back
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
    mov word [es:0x20*4], dos_int20     ; terminate, the CP/M door
    mov [es:0x20*4+2], cs
    mov word [es:0x21*4], dos_int21     ; ...and the one that matters
    mov [es:0x21*4+2], cs
    mov word [es:0x22*4], dos_int22     ; terminate address - a program may read
    mov [es:0x22*4+2], cs               ; it out of its own PSP
    mov word [es:0x23*4], dos_iret      ; Ctrl-Break
    mov [es:0x23*4+2], cs
    mov word [es:0x24*4], dos_int24     ; critical error: FAIL, never retry
    mov [es:0x24*4+2], cs
    mov word [es:0x2F*4], dos_int2f     ; the MULTIPLEX interrupt, which is
    mov [es:0x2F*4+2], cs               ; how a program finds XMS (SPEC.md
                                        ; 96.15) - and, unhooked, is how it
                                        ; finds whatever the ROM left there
    mov word [es:0x33*4], dos_int33     ; ...and the MOUSE (SPEC.md 96.10),
    mov [es:0x33*4+2], cs               ; which costs us a translation and not
                                        ; a driver: the kernel's own ISR keeps
                                        ; mouse_x/y/btn fresh for the whole
                                        ; bracket (SPEC.md 53.1)
    call dos_m33_hidden                 ; **AND THE CURSOR STARTS AWAY**
                                        ; (SPEC.md 96.10.5): the show counter
                                        ; is -1, which is not the zero a bss
                                        ; arrives as - 0 means VISIBLE, so a
                                        ; bracket that skipped this would put
                                        ; a cursor on the screen of a program
                                        ; that never asked for one

    ; --- ...AND THE REST OF THE BLOCK A REAL DOS OWNS (SPEC.md 96.5.2) ------
    ; Seven vectors were hooked above and the other fourteen of DOS's own were
    ; left at 0000:0000 - which is not `unimplemented`, it is A JUMP TO
    ; ADDRESS ZERO. Measured on IBM DOS 3.30: 28h, 2Ah..2Eh, 32h and 34h..3Eh
    ; all point at ONE `iret` inside IBMDOS, and 25h, 26h, 27h and 29h at real
    ; code. The eleven from 34h up are the 8087 EMULATOR's, which every
    ; Borland- and Microsoft-compiled program reads before installing its own.
    ; 2Eh is COMMAND.COM's undocumented back door rather than an iret, and an
    ; iret is the safe approximation: it does nothing where DOS would run a
    ; command, which beats running the vector table as code.
    ;
    ; CS AND NOT DS FOR THE TABLE. In the parted build (SPEC.md 96.44) this
    ; body is in the CORE and DS addresses the HOST's bss - `[dos_arena]`
    ; below is exactly that - so a table in our own image is reachable only
    ; through CS, which is the same segment either way.
    mov si, dos_ivirets
    mov dx, dos_iret
.hkir:
    mov al, [cs:si]
    inc si
    or al, al
    jz .hkird
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov [es:bx], dx
    mov [es:bx+2], cs
    jmp short .hkir
.hkird:
    mov word [es:0x25*4], dos_int25     ; ABSOLUTE DISK READ and WRITE: a
    mov [es:0x25*4+2], cs               ; refusal, and refusing is exactly why
    mov word [es:0x26*4], dos_int25     ; they cannot be `dos_iret` - both
    mov [es:0x26*4+2], cs               ; return with the FLAGS still pushed
    mov word [es:0x27*4], dos_int20     ; TSR: nothing can stay resident past
    mov [es:0x27*4+2], cs               ; the bracket, so it is a terminate -
                                        ; which is what DOS's own 27h is, a
                                        ; jump to AH=31h
    mov word [es:0x29*4], dos_int29     ; FAST CONSOLE OUTPUT, which DOS's own
    mov [es:0x29*4+2], cs               ; CON driver writes through
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
; A refusal rather than a body. The public disk surface of this box is file-
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

; The vectors a real IBM DOS 3.30 fills with one `iret`, measured rather than
; listed out of a book.  Read through CS: see dos_hook_vectors.
dos_ivirets:
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
    ; letter and never the set's own terminator. This box's set is BLASTER=
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_fcb_blank - the unparsed FCB DOS leaves when there is no argument for it
; in:  ES = the PSP, DI = 5Ch or 6Ch; out: DI past the name
; -----------------------------------------------------------------------------
dos_fcb_blank:
    push ax
    push cx
    mov byte [es:di], 0             ; drive 0 = "whichever is current"
    inc di
    mov cx, 11
    mov al, ' '                     ; ...and a name of spaces, which is what a
    cld                             ; tail with nothing in it parses to
    rep stosb
    pop cx
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
    pop cx
    pop bx
    pop ax
    ret
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

    mov word [es:0x00], 0x20CD      ; INT 20h, so a .COM that plain `ret`s
                                    ; lands here and terminates
    mov ax, [dos_ldpsp]
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
    mov byte [es:0x05], 0x9A
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
    mov cl, 4
    shr bx, cl
    mov [es:0x06], ax
    mov ax, [dos_ldpsp]
    add ax, 5
    sub ax, bx
    mov [es:0x08], ax
.cpmnone:
    mov word [es:0x0A], dos_int22   ; the terminate address, which DOS copies
    mov [es:0x0C], cs               ; out of the vectors it is about to hook
    mov word [es:0x0E], dos_iret
    mov [es:0x10], cs
    mov word [es:0x12], dos_int24
    mov [es:0x14], cs
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
    cld
    rep stosb
    mov word [es:0x18], 0x0101      ; 0, 1: stdin and stdout, the console
    mov byte [es:0x1A], 0x01        ; 2: stderr, the same device
    mov byte [es:0x1B], 0x00        ; 3: AUX
    mov byte [es:0x1C], 0x02        ; 4: PRN
    mov word [es:0x32], 20          ; ...and its size and address, which is how
    mov word [es:0x34], 0x0018      ; a program with more than twenty files
    mov ax, [dos_ldpsp]             ; open finds the table that replaced it
    mov [es:0x36], ax
    mov word [es:0x38], 0xFFFF      ; the previous PSP: DOS 3 leaves FFFF:FFFF
    mov word [es:0x3A], 0xFFFF      ; and a program may test for it
    mov ax, [dos_arena]
    add ax, DOS_ENVSEG
    mov [es:0x2C], ax               ; ...and one environment, shared: a child
                                    ; inherits the parent's, which is the
                                    ; default AH=4Bh's block asks for with a 0
    mov word [es:0x50], 0x21CD      ; INT 21h / RETF, the DOS 2+ call gate
    mov byte [es:0x52], 0xCB
    call dos_psp_tail               ; THE ARGUMENTS (SPEC.md 96.19), or the
                                    ; empty tail this used to write flat
    mov di, 0x5C                    ; ...and the two FCBs, in the shape an
    call dos_fcb_blank              ; EMPTY tail parses to. Zero - which this
    mov di, 0x6C                    ; wrote before - is a name of eleven NULs
    call dos_fcb_blank              ; on drive A and not a blank one, so a
                                    ; program that opens FCB 1 without reading
                                    ; the tail got a file that cannot exist
                                    ; rather than one obviously unnamed
                                    ; (SPEC.md 96.21.6)

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
    sub ax, 2
    mov bx, ax
.sp:
    sub bx, 2                       ; ...and the 0 word DOS pushes, which is
    mov [dos_prgsp], bx             ; the offset half of that PSP:0000 return
    mov word [es:bx], 0
.nostk:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; =============================================================================
; INT 20h / INT 21h (SPEC.md 96.7)
; =============================================================================
; Entered on the PROGRAM's stack with the program's segment registers, so the
; first thing either does is reach its own data through CS.
;
; The carry flag a DOS call returns is the one in the FLAGS image the `int`
; pushed, not the live one, so the refusal path edits [bp+8] rather than
; executing `stc` - which the `iret` would discard.
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

dos_int21:
    sti                             ; DOS runs its calls with interrupts on
    push bp
    push ds
    mov bp, sp                      ; [bp]=DS [bp+2]=BP [bp+4]=IP [bp+6]=CS
    push si                         ; [bp+8]=FLAGS, all on the PROGRAM's stack
    push di                         ; ...and [bp-2]=SI [bp-4]=DI [bp-6]=ES
    push es                         ; [bp-8]=DX, banked here rather than per
    push dx                         ; handler (SPEC.md 96.7.1)
    push cs
    pop ds
%ifdef DOSTRACE
    call dos_trace
%endif
    cmp word [dos_hkv + DHK_POLL], 0 ; **THE THIRD POLL** (SPEC.md 96.23.4): a
    je .nopkt                        ; client doing file I/O between receives
    call word [dos_hkv + DHK_POLL]   ; drains here. One compare on a path that
.nopkt:                              ; is already a dispatch, and it costs a
                                     ; host with no packet driver nothing

    cmp ah, 0x4C
    je .term
    cmp ah, 0x00
    je .term0
    cmp ah, 0x02
    je .putc
    cmp ah, 0x09
    je .puts
    cmp ah, 0x30
    je .ver
    cmp ah, 0x01
    je .getce
    cmp ah, 0x07
    je .getc
    cmp ah, 0x08
    je .getc
    cmp ah, 0x0B
    je .kbhit
    cmp ah, 0x40
    je .write
    cmp ah, 0x4A
    je .resize
    cmp ah, 0x48
    je .alloc
    cmp ah, 0x49
    je .free
    cmp ah, 0x3C
    je .create
    cmp ah, 0x3D
    je .open
    cmp ah, 0x3E
    je .close
    cmp ah, 0x3F
    je .read
    cmp ah, 0x41
    je .unlink
    cmp ah, 0x42
    je .seek
    cmp ah, 0x25
    je .setvec
    cmp ah, 0x35
    je .getvec
    cmp ah, 0x19
    je .curdrv
    cmp ah, 0x0E
    je .seldrv
    cmp ah, 0x36
    je .dfree
    cmp ah, 0x1A
    je .setdta
    cmp ah, 0x2F
    je .getdta
    cmp ah, 0x4E
    je .ff
    cmp ah, 0x4F
    je .fn
    cmp ah, 0x0D
    je .dskreset
    cmp ah, 0x39
    je .mkdir
    cmp ah, 0x3A
    je .rmdir
    cmp ah, 0x3B
    je .chdir
    cmp ah, 0x47
    je .getcwd
    cmp ah, 0x2A
    je .getdate
    cmp ah, 0x2B
    je .setdate
    cmp ah, 0x2C
    je .gettime
    cmp ah, 0x2D
    je .settime
    cmp ah, 0x4B
    je .exec
    cmp ah, 0x4D
    je .retcode
    cmp ah, 0x44
    je .ioctl
    cmp ah, 0x43
    je .getattr
    cmp ah, 0x29
    je .parsefcb
    cmp ah, 0x56
    je .rename
    cmp ah, 0x06
    je .dconio
    cmp ah, 0x0C
    je .flushin
    cmp ah, 0x31
    je .term                        ; TSR: AL is the exit code, exactly as
                                    ; AH=4Ch's is, and DX - the paragraphs to
                                    ; keep - cannot be honoured, the bracket
                                    ; taking the arena back whole. INT 27h is
                                    ; the same call through the same door
    jmp .bad

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
    jc .fhpath
    call dos_fh_isdev               ; A DEVICE IS NOT ON THE DISK (96.11.7)
    jnc .opdev
    call dos_fh_stat                ; fills [dos_fent]
    jc .fnoent
    call dos_fh_new                 ; BX = the handle, SI = the record, zeroed
    jc .fmany
    call dos_fh_setname
    mov al, [dos_pvol]              ; THE VOLUME THE NAME LANDED ON, which is
    mov [si+FH_VOL], al             ; where every later read of this handle
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
    jc .fmany
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

.create:
    ; AH=3Ch: DS:DX = an ASCIZ name, CX = attributes; out AX = a handle. The
    ; file is not touched until the first window flushes, which is also what
    ; makes the truncate free - the first flush REPLACES.
    push bx
    call dos_fh_name
    jc .fherr
    call .fhabs
    jc .fhpath
    call dos_fh_new
    jc .fmany
    call dos_fh_setname
    mov al, [dos_pvol]              ; ...and the same for a file just CREATED
    mov [si+FH_VOL], al             ; (SPEC.md 96.48.3)
    mov byte [si+FH_FLAGS], FHF_USED | FHF_WRITE
    call dos_jft_sync
    mov ax, bx
    jmp .fhok

.close:
    ; AH=3Eh: BX = the handle.
    push bx
    call dos_fh_slot                ; SI = the record, BX = its index
    jc .fhbad
    cmp bl, [dos_wown]
    jne .clnw
    call dos_fh_flush
    jc .fherr
.clnw:
    test byte [si+FH_FLAGS], FHF_WRITE
    jz .cldone
    test byte [si+FH_FLAGS], FHF_MADE | FHF_INPLC
    jnz .cldone                     ; ...OR OPENED, WHICH IS THE SAME ANSWER
                                    ; HERE and is why this is one immediate
                                    ; rather than a second test: an AH=3Dh
                                    ; handle never created anything, so
                                    ; "created and never written" is not a
                                    ; state it can be in - and touching its
                                    ; file would truncate the one it just
                                    ; overwrote in place (SPEC.md 96.11.6)
    call dos_fh_touch               ; created, never written: DOS leaves a
    jc .fherr                       ; zero-length file and so does this
.cldone:
    mov byte [si+FH_FLAGS], 0
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
    jc .fherr
    jmp .fhok
.fhrdev:
    cmp bx, DOS_FH0                 ; a DEVICE handle: stdin has no line editor
    jae .fhbad                      ; here, so it is at end of file, which is
.fhrdeof:                           ; what a program reading it will act on
    xor ax, ax
    jmp .fhok

.fwrite:
    ; AH=40h with a file handle: BX, CX and DS:DX as .write's.
    push bx
    call dos_fh_slot
    jc .fhbad
    test byte [si+FH_FLAGS], FHF_DEV ; ...OR A DEVICE, which is neither a file
    jnz .fwdev                       ; nor one of the five (SPEC.md 96.11.7)
    test byte [si+FH_FLAGS], FHF_WRITE
    jz .fhacc
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
    ; those are opposite cases. Behind is §96.11.2's real refusal - a write
    ; into the middle of a file this handle created, which the append path
    ; cannot make. PAST is a GAP, which 96.11.6.1 already lays, and Microsoft
    ; Works reported it as `Cannot write file`: create, seek to 0x180 on the
    ; empty file, write there, which is that section's own first bullet -
    ; pre-allocating a header to patch later.
    mov ax, [si+FH_POS+2]           ; the HIGH words first, so this is an
    cmp ax, [si+FH_SIZE+2]          ; UNSIGNED 32-BIT compare and not two
    ja .fwgap                       ; equality tests wearing one
    jb .fhacc
    mov ax, [si+FH_POS]
    cmp ax, [si+FH_SIZE]
    ja .fwgap
    jb .fhacc
    call dos_fh_wrloop
    jc .fherr
    jmp .fhok
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
    jc .fherr                       ; stops at the end of file and answers
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

.unlink:
    ; AH=41h: DS:DX = an ASCIZ name.
    push bx
    call dos_fh_name
    jc .fherr
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
    jne .fhacc                      ; that (SPEC.md 96.11.9) - `.rnerr` above
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
    jc .fhbad
    cmp ah, 2
    ja .fhacc
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
    push bx
    push si
    push di
    push es
    mov al, dl
    or al, al
    jnz .df1
    mov al, [dos_vol]               ; 0 = the drive we are on
    jmp short .dfv
.df1:
    dec al                          ; 1-based -> a volume index
.dfv:
    mov bh, [dos_vol]               ; where to come back to...
    mov di, bx                      ; ...banked in DI's high byte, because BX
                                    ; is about to be an ANSWER (the free
                                    ; count), and .dfhome used to read the
                                    ; home drive out of BH AFTER that load
    cmp al, bh
    je .dfask                       ; the common case by far: a program selects
                                    ; the drive and then asks about it
    mov dl, al
    call dos_drv_sel                ; A REAL MOUNT, and dos_drv_sel is the one
    cmp al, [dos_vol]               ; place that knows how to put itself back
    jne .dfbad                      ; if the mount refuses
.dfask:
    call dos_be_vstat               ; AX = sectors per cluster, BX = free
                                    ; clusters, CX = bytes per sector, DX =
                                    ; total clusters - AH=36h's own four, which
                                    ; is the slot's shape BECAUSE this is its
                                    ; caller (SPEC.md 18.4.6). THROUGH THE BACK
                                    ; END (96.4.1): the slot mounts the volume
                                    ; and reads its FAT, which is disk work on
                                    ; the program's stack
    jc .dfback
    call .dfhome
    pop es
    pop di
    pop si
    add sp, 2                       ; BX is an ANSWER: drop the banked one
    mov [bp-8], dx                  ; ...and so is DX (SPEC.md 96.7.1)
    jmp .ok
.dfback:
    call .dfhome
.dfbad:
    mov ax, 0xFFFF                  ; DOS's own "invalid drive", and a value a
    pop es                          ; program can test even when it ignores the
    pop di                          ; flag - which for this call every program
    pop si                          ; does, DOS never setting CF here
    add sp, 2
    jmp .ok
; --- .dfhome - back to the drive we were standing on, if we left it ---------
; in: DI's high byte = that drive; preserves everything (dos_drv_sel does)
.dfhome:
    push ax
    push dx
    mov dx, di
    mov dl, dh
    cmp dl, [dos_vol]
    je .dfh
    call dos_drv_sel
.dfh:
    pop dx
    pop ax
    ret

.setdta:
    mov [dos_dta], dx               ; AH=1Ah: DS:DX, and DS is the program's -
    mov ax, [bp]                    ; which for every real program is the PSP
    mov [dos_dtaseg], ax            ; it already runs on
    jmp .ok
.getdta:
    mov bx, [dos_dta]               ; AH=2Fh: out ES:BX - and ES is written into
    mov ax, [dos_dtaseg]            ; the gate's banked slot, which .leave
    mov [bp-6], ax                  ; restores from (SPEC.md 96.7.1)
    jmp .ok

; --- find first / find next (SPEC.md 96.12.1) --------------------------------
.ff:
    ; AH=4Eh: DS:DX = the pattern, CX = the attribute mask.
    push bx
    call dos_fh_name                ; the pattern travels the same road a name
    jc .fherr                       ; does, wildcards and all
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
    add di, DTA_PAT                 ; ordinal out of the pattern
    mov si, dos_fname
    mov cx, 13                      ; the pattern lives in the DTA, so AH=4Fh
    cld                             ; needs no state of ours at all. DOS keeps
    rep movsb                       ; it there and so does this
    pop di
    pop si
    ; A SEARCH THAT MATCHED NOTHING ANSWERS 18, THE SAME AS ONE THAT RAN OUT,
    ; and the distinction this once drew is one DOS does not (SPEC.md
    ; 96.12.1.2). MEASURED, by running one binary under IBM DOS 3.30 and under
    ; this box: a name that is not there in a directory that IS answers 0012h,
    ; and 0002h is not what DOS says for it at all. What DOS answers 3 for is
    ; the DIRECTORY not being there, which `dos_fh_name` above has already
    ; refused by the time this runs.
    call dos_find_step
    jc .ffnone
    xor ax, ax
    jmp .fhok
.ffnone:
    mov al, 18
    jmp .fherr
.fn:
    ; AH=4Fh: everything it needs is in the DTA AH=4Eh filled.
    push bx
    call dos_dta_seg
    mov al, [es:di+DTA_VOL]         ; ...the volume included: a walk started on
    mov [dos_fdrv], al              ; B: carries on there whatever the program
    call dos_fh_enter               ; has done to its own drive since, and
    jc .fherr                       ; .fhok/.fherr bring us home
    call dos_find_step              ; CF=1 with AL = 18 (no more files)
    jc .fherr
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
    ; drive included. SPEC.md 96.22's shape for the fourth time in this box.
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
    add si, 2
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
    jc .fherr
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
    jc .fherr
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
    jc .fherr
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
    jne .fhacc                      ; everything else this can fail with - the
    mov al, 2                       ; target existing included - it says 5 for
    jmp .fherr

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
    jc .fherr
    push si
    mov si, dos_fname
    call dos_be_mkdir
    pop si
    jc .fhacc
    xor ax, ax
    jmp .fhok
.rmdir:
    push bx
    call dos_fh_name
    jc .fherr
    push si
    mov si, dos_fname
    xor al, al                      ; STRICT: remove it only if it is empty,
    call dos_be_rmdir               ; which is the one AH=3Ah means. The
    pop si                          ; recursive form is a different call and
    jc .fhacc                       ; DOS does not have it
    xor ax, ax
    jmp .fhok
.chdir:
    push bx
    call dos_fh_name
    jc .fherr
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
    ; IT IS OSAPI_FILE_PATH's ANSWER NOW, not a string this box maintained on
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
    jc .fherr
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
    mov ax, ss
    mov [dos_psv_ss], ax
    mov [dos_psv_sp], sp
    mov byte [dos_inchild], 1
    call dos_prog_enter             ; ...and comes back HERE when it exits
    call dos_exec_unload            ; the child's block, back to the chain
    xor ax, ax
    jmp .fhok
.exerr:
    xor ah, ah
    jmp .fherr
.exbadfn:
    mov al, 1                       ; "invalid function"
    jmp .fherr
.exnest:
    mov al, 8                       ; "not enough memory", which is the honest
    jmp .fherr                      ; DOS answer for a child that cannot run

.resize:
    ; AH=4Ah: ES = the block, BX = the paragraphs wanted (SPEC.md 96.9).
    call dos_mcb_resize
    jc .badax
    jmp .ok

.alloc:
    ; AH=48h: BX = paragraphs wanted; out AX = the segment. A refusal answers
    ; the LARGEST available in BX, which is how a program asks "how much is
    ; there" - BX=FFFFh is that question and must get a truthful number.
    call dos_mcb_alloc
    jc .badax
    jmp .ok

.free:
    ; AH=49h: ES = a segment we handed out.
    call dos_mcb_free
    jc .badax
    jmp .ok

.ver:
    mov ax, 0x1E03                  ; AL = 3, AH = 30: DOS 3.30 (SPEC.md
    mov bx, 0                       ; 96.21.7). The version is a SETTING and
    mov cx, 0                       ; not a constant the day a program wants
    jmp .ok                         ; 5.00 - reporting a version whose
                                    ; functions we lack is worse than reporting
                                    ; a lower one, because a program branches
                                    ; on it (DOS-EXEC-PLAN 12 q1) - and 3.31
                                    ; was half a release above the machine this
                                    ; box is measured against, for no feature
                                    ; it has. BX and CX are the OEM and serial,
                                    ; and 0/0 is what IBM DOS 3.30 answers too

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
    call dos_fh_flush               ; ONE window, so this IS "flush everything"
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
    mov dl, [si+FH_VOL]             ; **THE HANDLE'S OWN DRIVE, NOT THE BOX'S**
    and dl, 0x3F                    ; (SPEC.md 96.7.1.2). Bits 0-5 are the drive
    xor dh, dh                      ; the FILE is on and [dos_vol] is where the
                                    ; PROGRAM is standing; they are the same
                                    ; number until a program opens `B:NAME`
                                    ; from A:, and IBM DOS 3.30 answers 0041h
                                    ; for exactly that - the file's drive, with
                                    ; the box on A:. BIT 7 CLEAR = a file,
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
    jc .fherr
    call .fhabs
    jc .fhpath
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
    jc .fnoent                      ; thing AH=43h is asked about. Only a name
.att_dir:                           ; that is NEITHER is `file not found`
    mov cx, 0x10                    ; DIRECTORY, which is the bit every caller
    mov ax, cx                      ; tests
    jmp .fhok
.att_set:
    push bx
    call dos_fh_name                ; it still has to NAME something real...
    jc .fherr
    call .fhabs
    jc .fhpath
    call dos_fh_stat
    jc .fnoent
    xor ax, ax                      ; ...and the new attributes are then
    jmp .fhok                       ; DROPPED rather than refused: there is
                                    ; nowhere to keep them, and a program that
                                    ; sets ARCHIVE on a file it has just
                                    ; written must not fail for it

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
    je .getce
    cmp ah, 0x06
    je .dconio
    cmp ah, 0x07
    je .getce
    cmp ah, 0x08
    je .getce
    jmp .ok                         ; flushed, and the rest is not ours

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
    pop ds                          ; so the gate's promise does not rest on
    pop bp                          ; every one of them being balanced
    iret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_terminate - back to the bracket, on our own stack
; in:  AL = the exit code; running on the PROGRAM's stack
; out: never returns
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
    sub ax, 2
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
; the box writes it: one file, plain text, in the directory the program was
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
    ; own - the box's console, which is where AH=02h/09h/40h go while the
    ; window is up (96.33). `kern_dos` sets no hook: the bracket is up from
    ; `kd_entry` to `int 19h`, the program owns the screen, and the ROM's
    ; teletype below is the only arm there has ever been on that host.
    ;
    ; A HOOK AND NOT A `%ifndef`, because the core is assembled ONCE (96.44):
    ; a build-time arm would be the box's or kern_dos's, never both.
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
DBE_NENT    equ 22

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
DHK_NENT    equ 7
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
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_be_copy:
    mov word [dos_betgt], DBE_COPY
    jmp dos_be_go
%endif                              ; DOS_EXTCORE
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
; -----------------------------------------------------------------------------
dos_be_go:
    ; --- THE ORDINAL BECOMES AN ADDRESS HERE (SPEC.md 96.44.1) -------------
    ; **THE CORE MAY NOT NAME A `dos_k_*`.** Each host has its own back end -
    ; the box's goes to `OSAPI_*` cells and `kern_dos`'s straight to the disk
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
dos_k_goto:
    call OSAPI_FILE_GOTO_QM         ; QM AND NOT Q, and the difference is the
    ret                             ; whole of whether this works when the
                                    ; handler is on a different volume from the
                                    ; document. GOTO_Q moves the MACHINE and not
                                    ; the INSTANCE, and the SDK says what that
                                    ; costs in as many words: "GOTO_Q alone is
                                    ; undone by that next cell, which first
                                    ; re-stands the machine in your instance's
                                    ; folder". Our instance stands where
                                    ; assoc_locate found DOS.O88 - A:\APPS on a
                                    ; cross-volume launch - so the read looked
                                    ; for the program THERE. It worked at all
                                    ; only while the handler happened to sit
                                    ; beside the document, which is the one
                                    ; arrangement a gate disk naturally has

dos_k_read:
    call OSAPI_FILE_READ
    ret

dos_k_find:
    call OSAPI_FILE_FIND
    ret

dos_k_rdat:
    call OSAPI_FILE_READ_AT
    ret

dos_k_write:
    call OSAPI_FILE_WRITE
    ret

dos_k_append:
    call OSAPI_FILE_APPEND
    ret

dos_k_delete:
    call OSAPI_FILE_DELETE
    ret

dos_k_dfree:
    call OSAPI_FILE_DFREE
    ret

dos_k_mkdir:
    call OSAPI_FILE_MKDIR
    ret

dos_k_rmdir:
    call OSAPI_FILE_RMDIR
    ret

dos_k_xcaps:
    call OSAPI_XMEM_CAPS
    ret

dos_k_xalloc:
    call OSAPI_XMEM_ALLOC
    ret

dos_k_xfree:
    call OSAPI_XMEM_FREE
    ret

dos_k_rename:
    call OSAPI_FILE_RENAME
    ret

dos_k_copy:
    mov al, OSAPI_FCP_COPY          ; the file manager's own engine, published
    jmp short dos_k_fcp             ; as ONE cell with a verb (SPEC.md 22.24)
dos_k_move:
    mov al, OSAPI_FCP_MOVE          ; ...whose move is the engine's Cut: the
dos_k_fcp:                          ; re-link first, copy-then-delete where it
    call OSAPI_FILE_COPY            ; declines. The shell's MOVE streams with
    ret                             ; its own buffer only on FERR_FULL, which
                                    ; is the engine unable to claim ITS buffer
                                    ; inside a bracket (96.30.6)

dos_k_xcopy:
    call OSAPI_XMEM_COPY
    ret

dos_k_path:
    call OSAPI_FILE_PATH
    ret

dos_k_here:
    call OSAPI_FILE_HERE
    ret

dos_k_vkind:
    call OSAPI_VOL_KIND
    ret

dos_k_vstat:
    call OSAPI_VOL_STAT
    ret

dos_k_wrat:
    call OSAPI_FILE_WRITE_AT
    ret

; -----------------------------------------------------------------------------
; dos_be_bind - the twenty-two doors, as addresses (SPEC.md 96.44.1)
; in:  nothing; out: nothing, every register preserved
;
; CALLED ONCE, BEFORE THE FIRST DOOR. The core stores a `DBE_*` ORDINAL and
; `dos_be_go` turns it into an address through this table, so the core can be
; assembled ONCE and joined to either back end. The table is DS-relative data
; and the copy is a `rep movsw` of twenty-two words.
; -----------------------------------------------------------------------------
dos_be_bind:
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, dos_betab
    mov di, dos_bevec
    mov cx, DBE_NENT
    cld
    rep movsw
    pop es
    pop di
    pop si
    pop cx
    ret


; -----------------------------------------------------------------------------
; dos_hk_bind - the six hooks the BOX wants (SPEC.md 96.44.3)
; in:  nothing; out: nothing, every register preserved
;
; Beside `dos_be_bind` and called with it. `kern_dos` has no equivalent and
; wants none: its six cells stay the zero a bss arrives as, which is what
; "this host does not want it" is spelled as.
; -----------------------------------------------------------------------------
dos_hk_bind:
    push ax
    mov word [dos_hkv + DHK_SNAP], dos_snap
    mov word [dos_hkv + DHK_POLL], dos_hk_poll
    mov word [dos_hkv + DHK_TTY],  dos_hk_tty
    mov word [dos_hkv + DHK_MOUSE], dos_hk_mouse
    mov word [dos_hkv + DHK_TXT],  dos_hk_txt
    mov word [dos_hkv + DHK_CLAIM], dos_hk_claim
    mov word [dos_hkv + DHK_FREE],  dos_hk_free
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
    cmp byte [dos_pkt_raw], 0
    je .no
    call dos_pkt_poll
.no:
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
dos_hk_tty:
    cmp byte [cs:dos_inbr], 0
    jne .rom                        ; inside the bracket the ROM's teletype IS
    push ds                         ; the machine's own screen
    push cs                         ; ...and the console arm needs OUR DS for
    pop ds                          ; real, con_scr and every byte of the
    call con_write                  ; library being DS-relative
                                    ; IT DRAWS NOTHING HERE: a verb that
                                    ; printed a directory would take the gfx
                                    ; lock per character. dos_con_run spends
                                    ; the marks once, after the verb returned
    cmp byte [cs:dos_fsxup], 0      ; **FULL SCREEN STREAMS A LINE AT A TIME**
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

dos_betab:
    dw dos_k_goto, dos_k_read, dos_k_find, dos_k_rdat, dos_k_write
    dw dos_k_append, dos_k_delete, dos_k_dfree, dos_k_mkdir, dos_k_rmdir
    dw dos_k_xcaps, dos_k_xalloc, dos_k_xfree, dos_k_xcopy, dos_k_rename
    dw dos_k_copy, dos_k_move, dos_k_path, dos_k_vstat, dos_k_wrat
    dw dos_k_here, dos_k_vkind

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
dos_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov bx, si                      ; THE BAND FIRST, on every page: it is a
    call dos_con_geom               ; property of the WINDOW and not of the
                                    ; page, and a painter that asked for it
                                    ; per-page would answer differently on two
                                    ; of them after a resize (SPEC.md 96.32)
    cmp byte [dos_page], DOS_PAGE_MAIN
    je .mainpage
    mov bx, si                      ; --- a SETUP page: its body, then the
    push bx                         ;     furniture every one of them shares
    call dos_paint_set              ; ...and there is only the one now
    pop bx
    call dos_paint_furn
    jmp .card                       ; ...and the About card over it, which is
.mainpage:                          ; the window's and not the page's

    cmp byte [dos_state], DST_READY  ; THE RE-KICK (SPEC.md 74.1): the kernel
    jne .nokick                      ; keeps at most one queued wake per window,
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
    call con_markall
    call con_rows_owed
    call con_takescroll
%endif
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
; dos_line - one opaque run at BX, DX
; in:  SI = NUL string, BX = x, DX = y
; out: nothing; preserves all registers
;
; font_run and not a fill-then-letter pair: one pass draws the ground and the
; glyphs, so the line is never momentarily blank (SPEC.md 6.1).
; -----------------------------------------------------------------------------
dos_line:
    push ax
    push cx
    push dx
    mov cx, bx
    mov al, CBLACK
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_fmt_exit - stamp the exit code into dos_l2_ran
; in:  nothing; out: SI = the line
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
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_err_line - SI = the sentence for [dos_err]
; -----------------------------------------------------------------------------
dos_err_line:
    push ax
    push bx
    mov al, [dos_err]
    xor ah, ah
    shl ax, 1
    mov bx, ax
    mov si, [dos_errs + bx]
    cmp byte [dos_err], DER_FSX     ; the unsupported-function case borrows the
    jne .out                        ; refusal line and stamps the number, so
    call dos_fmt_fn                 ; "it asked for AH=3Dh" is on the glass
.out:
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

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
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_hexd:
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 7
.out:
    ret
%endif                              ; DOS_EXTCORE
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)

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
; DOWN to a multiple of 8 rather than halved exactly. On VGA and CGA it is 0
; and on Hercules's 720 it is 40, which is a multiple of 8 already - so the
; rounding never costs a pixel on any adapter in this tree and is there for
; the one that is not.
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
    sub cx, ax                      ; CX = the slack: 0 on VGA and CGA, 80 on
    shr cx, 1                       ; Hercules
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
; box's answer about its own window and the seven below are the library's
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
    mov word [con_topy], 0          ; the band's origin IS the text's: this box
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
;                     current directory is a thing this box does not keep
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
    add si, 2
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
                                    ; makes the directory part EMPTY, which
                                    ; dos_walk_pbuf reads as that volume's root
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
.cnend:                             ; nothing this box can open
    mov byte [di], 0
    pop si
    pop di                          ; the separator again
    cmp byte [dos_tname], 0
    je .no                          ; a path ending in a separator names a
                                    ; FOLDER and this box runs programs
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
    push dx                         ; **STAND THE MACHINE, do not move the BOX**
    mov dl, [dos_tvol]              ; (SPEC.md 96.48.2). This used to set
    call dos_fh_stand               ; `[dos_vol]` for the walk and put it back
    pop dx                          ; if the walk refused, because the walk read
    jc .no                          ; `[dos_vol]` - it reads `[dos_pvol]` now,
    call dos_walk_pbuf              ; so the drive to walk on is said by
    jc .no                          ; standing on it. The rollback goes with it:
    mov [dos_pgdir], dx             ; nothing was changed, so a typo leaves the
    jmp short .commit               ; box exactly where it was - and the arm
                                    ; falls into `.commit` rather than past it,
                                    ; because `[dos_vol]` is no longer already
                                    ; set by the time it gets there
.here:
    ; **THE FOLDER WE STAND IN IS [dos_curdir], NOT [dos_dir]** (SPEC.md
    ; 96.33.13).  This arm left [dos_dir] alone, which is right for the door it
    ; was written for - the Run button on a box that has never moved - and
    ; wrong the moment the CONSOLE exists: `CD` goes through dos_cd_go, which
    ; writes [dos_curdir] and nothing else, so from the second directory onward
    ; every bare name resolved against the LAUNCH folder.  `CD SBEEPS` then
    ; `sb` looked in the volume root and answered "Bad command or file name"
    ; about a file DIR had just listed.
    ;
    ; The two are genuinely different (96.6.1): [dos_dir] is where the package
    ; was launched from and survives a program's own chdir, and [dos_curdir] is
    ; where the box is now.
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
; dos_btn_rect - the page button's rect, into dos_brect
; in:  BX = the window; out: dos_brect filled; every register preserved
;
; BOTTOM RIGHT of the content on both pages, so the button does not move when
; the page does - a control that jumps under the pointer is one the user
; clicks by accident.
; -----------------------------------------------------------------------------
dos_btn_rect:
    push ax
    push cx
    push dx
    push si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov si, dos_brect
    mov cx, ax
    add cx, 8 + DOS_FLDW - DOS_BTNW
    mov [si+0], cx
    add cx, DOS_BTNW
    mov [si+4], cx
    mov cx, dx
    add cx, DOS_BTNY
    mov [si+2], cx
    add cx, DOS_BTNH
    mov [si+6], cx
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_sav_rect - the Save Shortcut button's rect, into dos_srect
; in:  BX = the window; every register preserved
; -----------------------------------------------------------------------------
dos_sav_rect:
    push ax
    push cx
    push dx
    push si
    call OSAPI_WM_CONTENT
    mov si, dos_srect
    mov cx, ax
    add cx, 8
    mov [si+0], cx
    add cx, DOS_SAVW
    mov [si+4], cx
    mov cx, dx
    add cx, DOS_BTNY
    mov [si+2], cx
    add cx, DOS_BTNH
    mov [si+6], cx
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_sav_go - the Save Shortcut button was pressed
; in:  BX = the window
;
; The kernel's Standard File dialog in SAVE mode (SPEC.md 38), with the
; completion proc below. The DEFAULT NAME is the program's own with .LNK on
; it, because that is what the user would type.
; -----------------------------------------------------------------------------
dos_sav_go:
    push ax
    push cx
    push si
    push di
    call dos_lnk_build              ; **BUILT BEFORE THE DIALOG OPENS**, and
    jc .out                         ; that is not an ordering preference: the
    mov [dos_lend], cx              ; dialog NAVIGATES, so by the time its
                                    ; completion runs the instance stands
                                    ; wherever the user went - and the link's
                                    ; WORKING_DIR would be that folder rather
                                    ; than the program's. It read `\` before
                                    ; this moved
    call dos_sav_dfl                ; dos_wname = `NAME.LNK`
    mov bx, [dos_win]               ; **BX IS THE WINDOW WE WANT TO HEAR BACK
                                    ; ABOUT**, and leaving it out is a dialog
                                    ; given a garbage pointer - which opens,
                                    ; closes on Enter, and writes nothing
    mov si, dos_wname
    mov di, dos_sav_done
    mov al, FDLG_SAVE
    call OSAPI_FILE_DLG             ; CF=1 = one is already up, or no room -
.out:                               ; and a refusal needs no report: the user
    pop di                          ; pressed a button and nothing happened,
    pop si                          ; which is what a busy dialog looks like
    pop cx
    pop ax
    ret

; --- dos_sav_dfl - `NAME.LNK` from dos_name, into dos_sbuf ------------------
dos_sav_dfl:
    push ax
    push cx
    push si
    push di
    mov si, dos_name
    mov di, dos_wname
    mov cx, 8
.c:
    mov al, [si]
    or al, al
    jz .ext
    cmp al, '.'
    je .ext
    mov [di], al
    inc si
    inc di
    dec cx
    jnz .c
.ext:
    mov byte [di+0], '.'
    mov byte [di+1], 'L'
    mov byte [di+2], 'N'
    mov byte [di+3], 'K'
    mov byte [di+4], 0
    pop di
    pop si
    pop cx
    pop ax
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_paint_furn - the setup area's title and its bottom row (SPEC.md 96.32.2)
; in:  BX = the window, gfx lock held (every painter has it)
; out: nothing; every register preserved
;
; ONE ROUTINE FOR EVERY SETUP PAGE, which is the point: `<`, `>`, Save Shortcut
; and Return are in the same place whatever is above them, so the pointer does
; not have to hunt and a third page costs a string rather than a layout. The
; TITLE is what says which page is up - a page that looked the same as its
; neighbour but behaved differently would be the worst of the three shapes.
; -----------------------------------------------------------------------------
dos_paint_furn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov bx, ax
    add bx, DOS_SETPAD
    add dx, DOS_TITY
    mov si, dos_l_tset              ; THE page's name, there being one
    mov ax, (CWHITE << 8) | CBLACK
    mov cx, bx
    call OSAPI_FONT_RUN
    pop bx

    push bx                         ; ...and THE COMMAND, beside it (SPEC.md
    call dos_cmd_place              ; 96.32.2.2): the same box the main page's
    mov si, dos_pln                 ; bar carries, on the row that names the
    call os88line_draw              ; page, because what every option below
    pop bx                          ; applies to is the command

    call dos_furn_rects             ; the two rects, both from one arithmetic
    push bx
    mov bx, dos_btrec
    mov al, DOS_BT_SAV
    call os88ui_btn
    mov al, DOS_BT_RET
    call os88ui_btn
    pop bx
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_furn_rects - the bottom row's two buttons (SPEC.md 96.32.2)
; in:  BX = the window
; out: dos_srect and dos_trect filled; every register preserved
;
; dos_bar_rects' shape one page down, and for its reason: the two words are
; RIGHT-anchored, and both share a row
; whose y comes from the content BOX rather than a constant - so the row sits
; on the bottom of a CGA window and of a VGA one without a per-adapter number.
; -----------------------------------------------------------------------------
dos_furn_rects:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, dos_btrec               ; the Setup page's pair, aimed here for
    mov word [si+OS88UI_BT_RECTS], dos_trect
    mov word [si+OS88UI_BT_LABELS], dos_bt_setl
    mov word [si+OS88UI_BT_N], 2    ; dos_bar_rects' reason
    call OSAPI_WM_GEOM              ; CX = content width, DX = its height
    jc .out
    mov di, cx
    sub dx, DOS_BTNH + DOS_FURNB    ; DX = the row's top, from the BOTTOM
    push dx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    add dx, cx                      ; ...and now it is a screen row
    mov bx, dx
    add bx, DOS_BTNH - 1            ; BX = its last line, rects being inclusive

    mov si, dos_trect               ; --- 'Return', right-anchored ---
    mov cx, ax
    add cx, di
    sub cx, DOS_SETPAD + DOS_RETW
    mov [si+0], cx
    mov [si+2], dx
    mov [si+6], bx
    push cx
    add cx, DOS_RETW - 1
    mov [si+4], cx
    pop cx
    mov si, dos_srect               ; --- ...and 'Save Shortcut' beside it ---
    sub cx, DOS_BARGAP + DOS_SAVW
    mov [si+0], cx
    mov [si+2], dx
    mov [si+6], bx
    add cx, DOS_SAVW - 1
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
; dos_paint_set - the Setup page's body (SPEC.md 96.32.2)
; in:  BX = the window, gfx lock held
; out: CF=0; every register preserved
;
; TWO HALVES. The left is what a RUN needs - the arguments and one environment
; row - and the right is §96.25's memory block, which used to be a page and is
; three lines and a control. The env box here IS Environment's first row: one
; buffer, two rects, so there is no mirroring to keep in step and no way for
; the user to type into the one nothing reads.
; -----------------------------------------------------------------------------
dos_paint_set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    mov cx, ax
    add cx, DOS_SETPAD
    add dx, DOS_BODYY
    mov ax, (CWHITE << 8) | CBLACK
    push dx
    mov si, dos_l_args
    call OSAPI_FONT_RUN             ; 'Arguments:'
    pop dx
    push dx
    add dx, DOS_SLBL2
    mov ax, (CWHITE << 8) | CBLACK
    mov si, dos_l_envb
    call OSAPI_FONT_RUN             ; 'Environment'
    pop dx
    pop bx

    push bx                         ; ...and the two boxes under them
    call dos_fld_place
    mov si, dos_ln
    call os88line_draw
    pop bx
    push bx                         ; ...and EVERY environment row under it
    xor cx, cx                      ; (SPEC.md 96.32.2.1), where there used to
.erow:                              ; be one and a page button to reach the
    push cx                         ; other three
    call dos_erow
    call os88line_draw
    pop cx
    inc cx
    cmp cx, DOS_ENVN
    jb .erow
    pop bx

    call dos_paint_mem              ; ...and the right half, which is §96.25's
                                    ; block drawn at an origin of its own
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dos_mck_place - the three check boxes and the drop-down's figures, put where
;                 the window is now (SPEC.md 96.36.7)
; in:  BX = the window; every register preserved
;
; ONE PLACER FOR ALL THREE, because they share an origin and a width and are
; recomputed on every paint and every click for dos_mfld_place's reason. It
; also reads the two CLASS FIGURES - what the hard disks and the network are
; holding - and banks them, because the label prints one and dos_mem_arena
; adds the same one, and two reads could answer differently across a driver
; being unloaded between them (SPEC.md 47 rule 5).
; -----------------------------------------------------------------------------
dos_mck_place:
    push ax
    push bx
    push cx
    push dx
    push si
    push di                         ; dos_mem_num2 patches THROUGH DI, and the
                                    ; click path banks nothing in it any more -
                                    ; but the contract says every register, and
                                    ; a placer that quietly kept one would be
                                    ; the next silent defect of exactly this
                                    ; shape (SPEC.md 96.36.4)
    call dos_mem_org
    mov al, DRVC_DISK               ; WHAT EACH CLASS IS HOLDING, once
    call dos_classk                 ; (SPEC.md 51.12)
    mov [dos_mhkb], ax
    mov al, DRVC_NET
    call dos_classk
    mov [dos_mnkb], ax
    mov al, DRVC_SOUND              ; ...AND THE ONE WITH NO BOX (SPEC.md
    call dos_classk                 ; 96.36.7.2): dos_drv_take unmounts the
    mov [dos_msnk], ax              ; sound driver on EVERY arm and the user is
                                    ; never asked, because a DOS program cannot
                                    ; reach it - so it is a term in the figure
                                    ; and not a control

    ; --- ...AND THE CAPTIONS, WHICH ARE THE OTHER QUESTION (SPEC.md 51.12.1) -
    ; `(Up to NNK)` is about the CLASS and not about this machine's state, so
    ; it is a BUILD-TIME constant and the answer is the same on every machine.
    ; Fed the figures above it read `(Up to  0K)` on a machine with nothing of
    ; that class mounted - a true number that reads exactly like a page whose
    ; arithmetic has broken, and the state the box is MOST often opened in,
    ; since a user shutting the OS down for a DOS program tends not to have a
    ; hard disk or a card in the first place.
    mov ax, DRVM_CEIL_DISK          ; ...and the ceiling is a CONSTANT and not
    mov di, dos_mhddk               ; a call (SPEC.md 51.12.1): the slot used
    call dos_mem_num2               ; to walk a table of build-time figures to
    mov ax, DRVM_CEIL_NET           ; add them up, and the SDK carries the sum
    mov di, dos_mnetk               ; that walk was producing
    call dos_mem_num2
    mov si, dos_mhdd                ; ...then the three rects, one pitch apart
    mov ax, dos_l_mhdd
    mov dx, DOS_MHDDY
    call dos_mck1
    mov si, dos_mnet
    mov ax, dos_l_mnet
    mov dx, DOS_MNETY
    call dos_mck1
    mov si, dos_mmou
    mov ax, dos_l_mmou
    mov dx, DOS_MMOUY
    call dos_mck1
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dos_classk - one class's KB, with the refusal folded into the figure
; in:  AL = a DRVC_*
; out: AX = the KB, 0 where the slot refused; clobbers flags
;
; Three call sites wanted the same two instructions after the slot. There
; used to be a third instruction and two more sites: the slot's answer once
; carried `DRVM_PLUS` in bit 15 and once had a CEILING form, and both went
; when it stopped quoting drv_memk (SPEC.md 51.12.2, 51.12.1) - what it
; weighs now is the heap, in plain kilobytes.
dos_classk:
    call OSAPI_DRV_CLASSK
    jnc .ok
    xor ax, ax                      ; CF = nothing of that class - the figure
.ok:                                ; is 0 and not whatever AX was left as
    ret

; dos_mck1 - one box's rect and label (internal)
; in:  SI = the record, AX = its label, DX = the row offset from [dos_my]
; out: nothing; AX, DX and SI preserved
dos_mck1:
    push ax
    push cx
    push dx
    mov [si+OS88UI_CK_LABEL], ax
    mov cx, [dos_mx]
    add cx, DOS_MSUBX
    mov [si+OS88UI_CK_RECT+0], cx
    add cx, OS88UI_CKBOX + OS88UI_CKGAP + 8 * DOS_MCKW
    dec cx                          ; ...x2 INCLUSIVE, os88ui_rad's rule
    mov [si+OS88UI_CK_RECT+4], cx
    add dx, [dos_my]
    mov [si+OS88UI_CK_RECT+2], dx
    add dx, OS88UI_CKBOX
    dec dx
    mov [si+OS88UI_CK_RECT+6], dx
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mck_di - DI = OS88UI_DIS when the box at BX names a class nothing has
;              mounted (SPEC.md 47 rule 5, 96.36.7)
; in:  BX = dos_mhdd or dos_mnet
; out: DI; every other register preserved
;
; **GREY A FACT.** A box that would unmount a driver the machine has not got
; is offering to give back memory nobody is holding, and OSAPI_DRV_CLASSK's CF
; is that fact - banked in the figure by dos_mck_place, so this is a compare
; and not a second call. The box is forced ON with it: an unticked box means
; "take it out" and there is nothing to take out, so an off box would be
; describing an action rather than a state.
; -----------------------------------------------------------------------------
dos_mck_di:
    push ax
    mov di, OS88UI_DIS              ; **AND A SUBSECTION BELONGS TO ITS ARM**
    cmp bx, dos_mmou                ; (SPEC.md 96.36.9): a box whose arm is not
    jne .arm0                       ; the pick cannot be used, so it is greyed
    cmp byte [dos_keepc], DOS_MEM_WHOLE ; - and a press on it falls through to
    jne .out                        ; the radio, which picks that arm
    xor di, di                      ; (96.36.4). Two clicks, and the first one
    jmp short .out                  ; says what the second will mean
.arm0:
    cmp byte [dos_keepc], DOS_MEM_IN
    jne .out
    xor di, di                      ; **AND LIVE WHATEVER IS MOUNTED** (SPEC.md
.out:                               ; 96.36.7.1). It used to grey a box whose
    pop ax                          ; class had nothing loaded, and force it
    ret                             ; ON, on the ground that an unticked box
                                    ; would describe an action rather than a
                                    ; state. That reads the box as a REPORT
                                    ; about this machine, and it is a REQUEST
                                    ; about the program: `include these if
                                    ; available`. A .LNK outlives the setup it
                                    ; was saved under, so a machine with no
                                    ; card is exactly when the user needs to
                                    ; be able to say what they want on the
                                    ; machine that has one.
                                    ;
                                    ; The arithmetic needs nothing: the figure
                                    ; a clear box adds is the class's own, and
                                    ; a class with nothing loaded reports ZERO
                                    ; - so an empty class moves the estimate by
                                    ; 0 whichever way its box is set, which is
                                    ; the only honest answer about the machine
                                    ; in front of you. `Up to` in the label is
                                    ; what says the tick is about another one

; -----------------------------------------------------------------------------
; dos_mck_hit - test the box at BX against the banked press, if it is live
; in:  BX = one of the three records; [dos_mpx]/[dos_mpy] = the point
; out: CF = 0 it was HIT AND TOGGLED (the box redrew itself), CF = 1 it was
;      not ours - greyed, or the point is elsewhere
;
; It reloads the point rather than taking it in CX/DX because that is the one
; thing every caller here gets wrong: `dos_mck_di` answers in DI and
; `os88ui_drpress` zeroes it, so a point banked in a register does not survive
; the control before this one (SPEC.md 96.36.4).
; -----------------------------------------------------------------------------
dos_mck_hit:
    push cx
    push dx
    push di
    call dos_mck_di
    or di, di
    pop di
    jnz .no                     ; greyed: nothing to give, and the press
    mov cx, [dos_mpx]           ; belongs to the arm under it (SPEC.md 96.36.9)
    mov dx, [dos_mpy]
    call os88ui_chkhit
    jmp short .out
.no:
    stc
.out:
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; dos_drop_place - the disk cache's box, and WHICH LIST IS IN IT (96.36.6)
; in:  BX = the window; every register preserved
;
; **THE LIST IS THE ARM'S**, and the pick is remembered per arm: arm 1 has the
; whole rung ladder because `kd_giveback` sheds one at a time, and arm 0 has
; Auto and Off because the kernel's claim is taken at a mount and is either
; standing or shed. Swapping the list without swapping the pick would leave
; SEL indexing past the short one, which draws a caption out of whatever
; follows the table.
; -----------------------------------------------------------------------------
dos_drop_place:
    push ax
    push cx
    push dx
    push si
    call dos_mem_org
    mov si, dos_mdr
    mov word [si+OS88UI_DR_WIN], bx
    mov word [si+OS88UI_DR_ITEMS], dos_ca_items     ; ONE LIST ON BOTH ARMS
    mov word [si+OS88UI_DR_N], DOS_CA_N             ; (96.36.6): since SPEC.md
                                    ; 18.95.8 arm 0 can hold a WIDTH too, so
                                    ; there is nothing to swap, nothing to park
                                    ; and no second length to clamp against
    mov ax, [dos_mx]
    add ax, DOS_MCACX
    mov [si+OS88UI_DR_RECT+0], ax
    add ax, DOS_MCACW
    dec ax
    mov [si+OS88UI_DR_RECT+4], ax
    mov dx, [dos_my]
    add dx, DOS_MCACY
    mov [si+OS88UI_DR_RECT+2], dx
    add dx, DOS_MCACH
    dec dx
    mov [si+OS88UI_DR_RECT+6], dx
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mfld_place - put the memory limit field where the window is now
; in:  BX = the window; every register preserved
;
; dos_fld_place's shape and for its reason: os88line's rect is in SCREEN
; coordinates, so a banked one puts the caret one drag behind.
; -----------------------------------------------------------------------------
dos_mfld_place:
    push ax
    push cx
    push dx
    push si
    call dos_mem_org                ; the BLOCK's origin (SPEC.md 96.32.2),
    mov ax, [dos_mx]                ; never the content edge
    mov dx, [dos_my]
    mov si, dos_mln
    mov cx, ax
    add cx, DOS_MSUBX + DOS_MFLDX   ; ...indented into arm 0's subsection
    mov [si+LN_X1], cx
    add cx, DOS_MFLDW
    mov [si+LN_X2], cx
    mov cx, dx
    add cx, DOS_MFLDY
    mov [si+LN_Y1], cx
    add cx, DOS_FLDH
    mov [si+LN_Y2], cx
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mrad_place - put the radio group's record where the window is now
; in:  BX = the window; every register preserved
;
; The RECT IS THE WHOLE CLICKABLE AREA - every ring, gap and label of every
; arm - which is os88ui_rad's own contract, so a press on the words counts and
; the row is worked out by dividing the press's y by the pitch. Recomputed
; from the content origin on every paint and every click for dos_fld_place's
; reason: the rect is in SCREEN coordinates and a window moves.
;
; THE OTHER FOUR FIELDS ARE SET HERE TOO, not once at entry, for the same
; reason the rect is: this runs before every paint and before every click, so
; a record filled in one place cannot disagree with itself. The exception is
; OS88UI_RD_SEL, which IS [dos_keepc] (SPEC.md 96.36) and is the user's.
;
; DOS_MEM_WHOLE's greying is DI's bit 2 and comes from dos_mem_whole - one
; predicate, three consumers (SPEC.md 47 rule 4), of which this is the one
; that makes the ring and the label dither.
; -----------------------------------------------------------------------------
dos_mrad_place:
    push ax
    push cx
    push dx
    push si
    push di
    call dos_mem_org                ; the BLOCK's origin, as above
    mov ax, [dos_mx]
    mov dx, [dos_my]
    xor di, di
    call dos_mem_whole              ; CF = 1: the third arm cannot be picked.
    jnc .live                       ; **IT ANSWERS IN SI**, so it is asked
    mov di, 1 << DOS_MEM_WHOLE      ; BEFORE the record is pointed at and not
.live:                              ; after - the reason it returns and the
    mov si, dos_mrad                ; record live in the same register, and
                                    ; the wrong order writes the rect into the
                                    ; LABEL and draws the group at 0,0
    mov word [si+OS88UI_RD_ITEMS], dos_mem_items
    mov word [si+OS88UI_RD_N], DOS_MEM_N
    mov word [si+OS88UI_RD_PITCH], DOS_MRADP
    mov [si+OS88UI_RD_DIS], di
    mov cx, ax
    mov [si+0], cx
    add cx, OS88UI_RDBOX + OS88UI_RDGAP + 8 * DOS_MEMI_N
    mov [si+4], cx                  ; ...x2 INCLUSIVE, so the longest label's
    dec word [si+4]                 ; last cell is inside and the next is not
    mov cx, dx
    add cx, DOS_MRADY
    mov [si+2], cx
    mov cx, dx                      ; **AND y2 IS NOT y1 + N*PITCH** (SPEC.md
    add cx, DOS_MRADB               ; 96.36.4): two rows of the pitch would
    dec cx                          ; reach past the block, and os88ui_radhit
    mov [si+6], cx                  ; tests the rect FIRST and divides
                                    ; afterwards. It used to stop one line
                                    ; above the disk cache's box, which was
                                    ; below it; the dial is at the TOP now
                                    ; (96.36.6.3) so there is nothing left to
                                    ; stop short of and this is simply the
                                    ; block's own last line. It still has to
                                    ; clear arm 1's band MIDDLE - y1 + pitch +
                                    ; pitch/2 - which is what every kd* row
                                    ; clicks that arm at
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

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
    mov si, dos_l_memw3
    stc
    ret

%endif                              ; KD_BACKEND
%ifdef DOSKPART
; =============================================================================
; ARM 3: HANDING THE MACHINE TO kern_dos (SPEC.md 96.40)
; =============================================================================
; docs/plans/KERN-DOS-PLAN.md §7. The box fills a record in its OWN image, the
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
    mov byte [di+KDH_PAD], 0
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

    mov ax, [dos_kdrow + OP_R_OFF]  ; the part's first file SECTOR, which is
    mov [di+KDH_POFF], ax           ; what os88pkg.py wrote there
    mov ax, [dos_kdrow + OP_R_ZKB]  ; ...and on an OP_COMP row this word is the
    mov [di+KDH_PLEN], ax           ; PACKED length (SPEC.md 20.12.7)
    mov word [di+KDH_PLEN+2], 0
    mov ax, [dos_kdrow + OP_R_LEN]
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

; -----------------------------------------------------------------------------
; dos_wholeask - THE ONE DELIBERATELY DESTRUCTIVE THING THIS BOX DOES (96.42)
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
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    jne .no
    cmp byte [dos_wok], DOS_W_YES
    je .no                      ; asked and confirmed for THIS launch
    cmp byte [dos_wok], DOS_W_NO
    je .hold                    ; **ASKED AND REFUSED, AND THE REFUSAL HAS TO
                                ; STICK.** The launch is a posted WAKE and a
                                ; wake is a kick (SPEC.md 74.1): tearing the
                                ; alert down repaints, and the box is still
                                ; DST_READY when the next one lands - so a
                                ; two-state flag put the question straight back
                                ; up and Cancel could not be answered at all
    call dos_hasfixed
    jnc .no                     ; there is one: the session comes back (96.41)
                                ; and there is nothing to warn about
    push ax
    push bx
    push si
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
    jc .no                      ; REFUSED - one was already up and has been
                                ; raised. Falling through to the launch there
                                ; would run the program behind its own question
.hold:
    stc
    ret
.no:
    clc
    ret

; dos_wholedone - the alert's answer. AL = 0 Cancel, 1 Proceed, or
;                 OS88UI_ACANCEL for a dismissal; the lock is NOT held
dos_wholedone:
    mov byte [dos_wok], DOS_W_NO
    cmp al, 1
    jne .out                    ; Cancel, Esc, the close box: [dos_state] was
                                ; never advanced, so the box is still READY and
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
                                            ; KDL_DPT: the box zeroes them so a
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
  %error "the DOS box's launch block outgrew KDL_SIZE"
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
; dos_mem_row - redraw the arena line alone, after a control has moved it
; in:  BX = the window; the gfx lock is held. Every register preserved
;
; ONE ROW AND NOT THE BLOCK (PERFORMANCE.md's first rule): a check box redraws
; its own mark and a drop-down its own caption, so the only other pixels that
; changed are these eleven cells - and the line is an opaque font_run, so it
; needs no ground laid under it (SPEC.md 13.14.6).
; -----------------------------------------------------------------------------
dos_mem_row:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dos_mem_arena
    mov di, dos_marn
    call dos_mem_num
    mov bx, [dos_mx]
    mov dx, [dos_my]
    add dx, DOS_MARNY
    mov si, dos_l_marn
    call dos_line
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_mem_arena - THE ONE FIGURE, as the controls under it stand (96.36.3)
; out: AX = the KB this page's settings would hand the program; flags
;
; It replaces the two numbers the page used to be a choice BETWEEN, because
; the page is no longer a choice between two numbers: it is an arm, three
; boxes, a dial and a cap, and what the user wants to know is what all of them
; together come to. So it is read on every paint and after every control that
; can move it - one row, live.
;
; **ARM 0's TERMS ARE EXACT AND ARM 1's ARE ESTIMATES**, which is what the '~'
; in front of the row is about. Inside the OS the kernel answers the same
; question `dos_run` asks, so what the page shows is what the program gets;
; the driver boxes add figures that are build-time constants (SPEC.md 51.12)
; and a driver's real claim can be a rung lower. Shutting the OS down cannot
; be asked of anything - the machine it describes does not exist yet - so it
; is conventional memory less what `kern_dos` is known to keep.
;
; **AND THE SAME QUESTION IS THE WHAT-IF, NOT PLAIN AVAIL** (SPEC.md 96.25.1.1).
; dos_run posts OSAPI_MEM_COMPACT and claims on the wake, so what it
; hands the program is a heap that has been packed with OUR OWN REGION IN THE
; PASS - which is exactly the question the what-if answers and exactly
; the one plain avail does not, a package being pinned by the act of asking
; (SPEC.md 66.4.3). It made no difference for a release because this region
; could not move at all: it is PART 0 of a re-homed DOS.O88 and the carve was
; refused its declaration, so the excuse bought nothing and the two slots
; agreed by accident. SPEC.md 66.6.1.2 unpinned it and the accident ended -
; measured on a 360KB desktop, the page said 442K/474K and the launch handed
; out 445K/477K, under-promising by the 3KB the region's own move recovers.
;
; The what-if is documented as a measurement rather than a promise, and for
; THIS package it is the promise: the post is what makes it true, and dos_run
; sends one whenever a pass would add anything (SPEC.md 66.4.3.2).
; -----------------------------------------------------------------------------
dos_mem_arena:
    push bx
    push cx
    push dx
    push di
    push es
    cmp byte [dos_keepc], DOS_MEM_WHOLE
    je .whole

    ; --- ARM 0: the kernel's own answer, plus what the boxes would hand back --
    mov ax, DOS_PG_FLOOR            ; AH = MEMC_WHATIF, AL = the level, which is
    call OSAPI_MEM_COMPACT          ; the floor dos_run will set - ON EVERY RUNG
                                    ; of the dial now (SPEC.md 18.95.8), so this
                                    ; no longer picks between two levels. The
                                    ; what-if reads its level from AL rather
                                    ; than from the task's floor the way
                                    ; OSAPI_MEM_AVAIL does
    call dos_cache_give             ; ...PLUS what the dial hands back out of
    add ax, cx                      ; the cache, which is the one term the
                                    ; what-if cannot see: it was asked at a
                                    ; floor that counts the window as KEPT, and
                                    ; the dial is about to narrow it. Auto
                                    ; answers 0 here, which is right - it
                                    ; commands nothing
    ; ...AND AN UNTICKED BOX IS A DRIVER THAT WILL NOT BE THERE (96.36.7) -
    ; **ASKED OF THE ROUTINE THE LAUNCH ITSELF ASKS** (SPEC.md 96.36.7.3).
    ; This used to read the two check boxes directly, which is the same
    ; question one step short of the answer: `dos_drv_take` passed a mask of
    ; ZERO for a cycle, so every term added here was a promise nothing kept.
    ; One reader means the figure and the sweep cannot disagree again, and it
    ; carries the guard with it - a class the launch will keep adds nothing,
    ; whatever its box says. A class nothing has mounted reports 0 KB either
    ; way (96.36.7.1).
    call dos_spmask                 ; BL = what the launch will really let go
    test bl, 1 << DRVC_DISK
    jz .nohdd
    add ax, [dos_mhkb]
.nohdd:
    test bl, 1 << DRVC_NET
    jz .snd
    add ax, [dos_mnkb]
.snd:
    add ax, [dos_msnk]              ; ...AND THE SOUND DRIVER, UNCONDITIONALLY
                                    ; (SPEC.md 96.36.7.2). It has no box
                                    ; because there is nothing to ask: a DOS
                                    ; program cannot reach it either way, so
                                    ; dos_drv_take unmounts it on every arm and
                                    ; every path. The what-if above is asked
                                    ; while it is still MOUNTED, so without
                                    ; this line the page under-reported by the
                                    ; whole of it on any machine with a card,
                                    ; silently.
                                    ;
                                    ; IT IS A FLOOR. Measured card against
                                    ; bare, the launch delivers the SAME 443KB
                                    ; either way - so what comes back is 48KB
                                    ; where drv_memk says 34, the other 14
                                    ; being the MERGE the unmount unlocks
                                    ; (docs/plans/HEAP-UNPIN-PLAN.md 2.0). No
                                    ; constant can carry that, so the residual
                                    ; stays under-reported - which is the safe
                                    ; direction, and 48 under became 14
    ; ...AND THE PACKET DRIVER'S BUFFERS COME OUT OF IT (SPEC.md 96.23.7.1).
    ; dos_pkt_bufs claims them BEFORE the what-if above was ever asked, out of
    ; the same heap, so on a machine with a wire the row promised three
    ; kilobytes the program could never be handed. It is the same reader the
    ; claim uses, which is what stops the two disagreeing again - and it
    ; answers 0 on a machine with no wire and on one whose card the Network
    ; box has just told the launch to let go.
    push ax
    call dos_pkt_want               ; CX = the KB, or CF = 1 for none
    pop ax
    jc .cap
    cmp ax, cx                      ; ...and never BELOW zero: `.cap` clamps
    jbe .none                       ; against the user's ceiling and has no
    sub ax, cx                      ; floor, so an underflow here would print a
    jmp short .cap                  ; 65,000K row on a machine too tight to run
.none:                              ; the box at all
    xor ax, ax
    jmp short .cap
                                    ; ...and the LIMIT is arm 0's too, which
                                    ; is why the other arm jumps past it:
                                    ; `dos_lbfill` fills KDL_CAP from the BDA
                                    ; and never from [dos_memkb], so a cap
                                    ; shown on arm 1 would be a promise the
                                    ; launch does not keep (SPEC.md 96.36.7)

    ; --- ARM 1: conventional memory, less what kern_dos is known to keep ------
    ; Every term is a constant this build knows, which is the only instrument
    ; there is: the machine being described has no kernel in it, so nothing
    ; can be asked. SK_KERN + SK_HEAP is everything from KERNEL_SEG up, and
    ; DOS_LOWKB is what sits below it - the IVT, the BDA and the boot area.
.whole:
    push ds
    pop es
    mov di, dos_skbuf
    call OSAPI_SYS_KB
    mov ax, [dos_skbuf + SK_KERN]
    add ax, [dos_skbuf + SK_HEAP]
    add ax, DOS_LOWKB
    sub ax, DOS_KDKB                ; ...less kern_dos's own resident span
    sub ax, DSH_CPKB                ; ...and the buffer COPY and TYPE share
    call dos_cache_kb               ; ...and the rung the dial is asking for
    sub ax, cx
    jmp short .out
.cap:
    mov cx, [dos_memkb]             ; ...and the user's own ceiling, which is
    jcxz .out                       ; the one term that is neither estimate nor
    cmp ax, cx                      ; measurement: it is a decision
    jbe .out
    mov ax, cx
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_cache_kb - what the dial is asking the cache to cost, in KB
; out: CX = the KB; AX and every other register preserved
;
; `(runs * 9 + 1) >> 1` is `dsk_rah_want`'s own arithmetic and `kd_rahkb`'s,
; mirrored here for the same reason those two mirror each other: a figure on
; the glass that disagrees with the claim is worse than no figure.
; -----------------------------------------------------------------------------
dos_cache_kb:
    push ax
    push bx
    mov bl, [dos_cache]
    xor bh, bh
    mov al, [bx + dos_ca_runs]      ; the ladder, one byte a row
    xor ah, ah
    call dos_rah_kb
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_rah_kb - ...and the same sum for a width that is not the dial's
; in:  AX = chunks
; out: CX = the KB they cost; every other register preserved
; -----------------------------------------------------------------------------
dos_rah_kb:
    push bx
    mov bx, ax
    mov cx, ax
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; CX = runs * 8 (cpu 8086: no shl by an
    add cx, bx                      ; immediate other than 1)...
    inc cx
    shr cx, 1                       ; ...and KB = ceil(runs * 4.5)
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_cache_ask - what the KERNEL is holding, in chunks (SPEC.md 18.95.8)
; out: CF = 0 with AX = the width held now, 0 = none; CF = 1 = no such slot
;      (a kernel older than 18.95.8), AX = 0. Every other register preserved
;
; `DSK_RAH_AUTO` is the sentinel that means *no ceiling of mine*, which is the
; truth outside a launch - the box has commanded nothing - so ASKING IS NOT A
; SIDE EFFECT and the slot needs no read-only spelling. Where a previous launch
; left the cache shed it takes it BACK, which is the resting state every figure
; on this page is supposed to describe.
; -----------------------------------------------------------------------------
dos_cache_ask:
    mov al, DSK_RAH_AUTO
    call OSAPI_DSK_CACHE
    jnc .out
    xor ax, ax
.out:
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

; -----------------------------------------------------------------------------
; dos_cache_give - the KB the dial is going to hand back out of the cache
; out: CX = the KB, 0 = none; every other register preserved
;
; The one term arm 0's estimate cannot get from the what-if. `OSAPI_MEM_COMPACT`
; is asked at `DOS_PG_FLOOR`, which counts the read-ahead window as KEPT - and
; that is right, because the dial usually keeps some of it. What the dial gives
; up is the difference between the two widths, and it is computed as two KB
; figures rather than one width: `ceil(a * 4.5) - ceil(b * 4.5)` is not
; `ceil((a - b) * 4.5)` when one of them is odd (7 and 2 agree at 23, 2 and 1
; disagree at 4 against 5), and a figure that is 1 KB over is a promise the
; claim does not keep.
; -----------------------------------------------------------------------------
dos_cache_give:
    push ax
    push bx
    push dx
    xor cx, cx
    cmp byte [dos_cache], DOS_CA_AUTO
    je .out                         ; Auto commands nothing, so nothing moves
    call dos_cache_ask              ; AX = what is held now...
    call dos_rah_kb
    mov dx, cx                      ; DX = what it costs
    mov bl, [dos_cache]
    xor bh, bh
    mov al, [bx + dos_ca_runs]      ; ...and what the dial leaves standing
    xor ah, ah
    call dos_rah_kb
    sub dx, cx
    mov cx, dx
    jnc .out
    xor cx, cx                      ; a dial ASKING FOR MORE than is held frees
.out:                               ; nothing - the kernel's own two bounds are
    pop dx                          ; what held it down and this cannot lift
    pop bx                          ; them (SPEC.md 18.95.8)
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_paint_mem - the memory page (SPEC.md 96.25)
; in:  BX = the window; the gfx lock is held, as every W_PAINT's is
;
; NO GROUND FILL (SPEC.md 13.14.6), dos_paint_env's reason exactly: every line
; is an opaque font_run, an os88line or a control that draws its own ground.
; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; dos_mem_org - the memory block's top-left, into [dos_mx]/[dos_my]
; in:  BX = the window; every register preserved
;
; **IT IS THE SETUP PAGE'S RIGHT HALF NOW** (SPEC.md 96.32.2), and this is the
; whole of what moving it cost: the block already painted from a banked origin
; because the heading, the figures, the field and the check box all had to
; agree on one, so giving that origin a different answer moves the lot. The
; two placers ask for it rather than computing from the content edge, which is
; what they did when the block had a page to itself.
; -----------------------------------------------------------------------------
dos_mem_org:
    push ax
    push cx
    push dx
    call OSAPI_WM_GEOM              ; CX = content width
    jc .out
    shr cx, 1                       ; ...and the block lives in the right half
    push cx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    add ax, cx
    add ax, DOS_SETPAD
    mov [dos_mx], ax
    add dx, DOS_BODYY
    mov [dos_my], dx
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
dos_paint_mem:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dos_mem_org                ; [dos_mx]/[dos_my] = the block's top-left

    ; **THE FIGURES BEFORE THE FIGURE THEY ARE TERMS OF** (SPEC.md 96.36.3.1).
    ; dos_mck_place reads the three class words the arena is a sum of, and it
    ; DRAWS NOTHING - it is rects and literals - so it sat two blocks below,
    ; where the boxes it places are. That made the FIRST paint of a fresh
    ; window compute the arena from the bss zeros the loader left, so the
    ; sound term was missing from it and from nothing else; the next recompute
    ; - a click, the dial, a keystroke - had them, and the figure moved with
    ; the user having changed nothing. Reported from the field as `433K, then
    ; 467K after going back in`.
    push bx
    call dos_mck_place              ; all three boxes, and their figures
    pop bx

    push bx                         ; **THE ARENA, AND IT IS ONE LINE NOW**
    call dos_mem_arena              ; (SPEC.md 96.36.3): AX = the estimate this
    mov di, dos_marn                ; page's own controls add up to, patched
    call dos_mem_num                ; into the run it is drawn as
    mov bx, [dos_mx]
    mov dx, [dos_my]
    add dx, DOS_MARNY
    mov si, dos_l_marn
    call dos_line
    pop bx

    push bx                         ; ...and the choice itself, FIRST of the
    call dos_mrad_place             ; controls because its subsections are the
    mov bx, dos_mrad                ; ground the rest of them stand on
    xor di, di
    call os88ui_rad
    pop bx

    ; --- ARM 0's subsection: the two drivers, and the limit ------------------
    push bx                         ; ...placed at the top, with the figures
    mov bx, dos_mhdd
    call dos_mck_di                 ; DI = OS88UI_DIS when this one is dead
    call os88ui_chk
    mov bx, dos_mnet
    call dos_mck_di
    call os88ui_chk
    pop bx

    ; --- the limit: its box, its label and its unit, all three together -----
    ; **AND ALL THREE GREY WITH THE ARM** (SPEC.md 96.36.10.2). It is arm 0's
    ; control exactly as the two driver boxes above it are, and the click path
    ; has always known that - `.notck` refuses the press on any other arm and
    ; lets it fall through to the radio - but the DRAWING did not, so the field
    ; sat there black and typeable on an arm that ignores it. 47 rule 2 is why
    ; the three are one bracket: half a control greyed is worse than none.
    push bx
    mov byte [dos_mln+LN_DIS], 0
    cmp byte [dos_keepc], DOS_MEM_IN
    je .lim
    mov byte [dos_mln+LN_DIS], 1    ; the frame takes the pen below instead of
    stc                             ; forcing black, and no caret is drawn
    call OSAPI_GFX_PEN
.lim:
    call dos_mfld_place             ; THE BOX FIRST, because it is the only one
    mov si, dos_mln                 ; of the three that reads [gfx_color] - the
    call os88line_draw              ; other two are opaque font_run pairs with
    mov bx, [dos_mx]                ; their colours in AL/AH, and take their
    add bx, DOS_MSUBX               ; grey from [gfx_dis] alone (font_ink)
    mov dx, [dos_my]
    add dx, DOS_MFLDY + 3
    mov si, dos_l_meml
    call dos_line
    mov bx, [dos_mx]                ; ...and the UNIT, hard against the box
    add bx, DOS_MSUBX + DOS_MFLDX + DOS_MFLDW + DOS_MFLDKX
    mov dx, [dos_my]
    add dx, DOS_MFLDY + 3
    mov si, dos_l_memk
    call dos_line
    clc                             ; ...and the pen back at once, whichever
    call OSAPI_GFX_PEN              ; arm it was: the lock is held for the
    pop bx                          ; whole page and whatever paints next
                                    ; would inherit it (the `.why` caption
                                    ; below states the same rule)

    ; --- ARM 1's: the mouse box, OR why the arm cannot be picked -------------
    ; ONE ROW, TWO THINGS, and they never want it at once (SPEC.md 96.36.8):
    ; an arm that is greyed is not offering its option either.
    push bx
    call dos_mem_whole              ; (SPEC.md 47 rule 3): consumer two of the
    jc .why                         ; one predicate
    mov bx, dos_mmou
    call dos_mck_di                 ; greyed while arm 0 is the pick (96.36.9)
    call os88ui_chk
    jmp short .nowhy
.why:
    mov bx, [dos_mx]                ; a caption explaining a control that is
    add bx, DOS_MSUBX               ; not disabled is noise, so this is drawn
    mov dx, [dos_my]                ; only when the arm really is
    add dx, DOS_MMOUY + 3
    stc                             ; **IN THE DISABLED PEN** (SPEC.md 47 rule
    call OSAPI_GFX_PEN              ; 2, 96.36.1): it belongs to the row above
    call dos_line                   ; it. Drawn solid it is the HEAVIEST text
    clc                             ; on the block and sits at the labels' own
    call OSAPI_GFX_PEN              ; indent, so it reads as a THIRD arm that
.nowhy:                             ; is live - which is rule 2's confusion one
    pop bx                          ; level out. And the pen goes back at once:
                                    ; gfx_unlock clears the flag but the lock
                                    ; is held for the whole page, so whatever
                                    ; paints after this would inherit it

    ; --- ...and the disk cache, which belongs to neither ---------------------
    push bx
    mov bx, [dos_mx]
    mov dx, [dos_my]
    add dx, DOS_MCACY + 3
    mov si, dos_l_memc
    call dos_line
    pop bx
    push bx
    call dos_drop_place             ; the list is the ARM's (SPEC.md 96.36.6)
    mov bx, dos_mdr
    xor di, di
    call os88ui_drop
    pop bx

    pop di                          ; **NO PAGE BUTTON HERE.** The setup area's
                                    ; furniture - the title, the arrows, Save
                                    ; Shortcut and Return - is drawn once by
                                    ; dos_paint_furn for whichever page is up,
                                    ; so a page draws only its own body
                                    ; (SPEC.md 96.32.2)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dos_btn_lbl - SI = what the page button should SAY, for the page that is up
; out: SI; every other register preserved
;
; It names WHERE IT GOES and not where you are, which is what a cycle needs: a
; button labelled with the current page is one the user has to press to find
; out what it does.
; -----------------------------------------------------------------------------
dos_btn_lbl:
    push bx
    xor bh, bh
    mov bl, [dos_page]
    shl bl, 1
    mov si, [dos_btn_tab+bx]
    pop bx
    ret

; in:  AX = 0..65535, DI -> five bytes inside a literal; clobbers nothing
;
; Into the LITERAL rather than into a buffer, which is dos_fmt_exit's shape
; one line along: the line is drawn by one opaque font_run and a number
; assembled anywhere else would need a second store to get there.
; -----------------------------------------------------------------------------
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
    call dos_mem_fix                ; ...and the arm, which is the other half
    ret                             ; of this block (SPEC.md 96.36.1)

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
; dos_click_mem - a press on the memory page
; in:  BX = the window, CX = x, DX = y
; -----------------------------------------------------------------------------
dos_click_mem:
    push ax
    push si
    push di
    mov [dos_mpx], cx               ; **THE POINT GOES IN THE IMAGE AND NOT IN
    mov [dos_mpy], dx               ; A REGISTER** (SPEC.md 96.36.4): five
                                    ; controls are tested with it now, and two
                                    ; of the library calls between them do not
                                    ; preserve DI

    ; --- THE DROP-DOWN FIRST, and then the boxes (SPEC.md 96.36.4) ----------
    ; A pitch here is a SUBSECTION, so the radio's own rect covers everything
    ; between the two headings - which is where the boxes are. The order is
    ; the fix and not a hack: os88ui's controls all answer "was this mine",
    ; so the INNERMOST one asks first and whatever no control claims falls
    ; through to the arm it is standing in.
    push bx
    call dos_drop_place
    mov bx, dos_mdr
    call os88ui_drpress             ; AH = 1 SPENT here, AL = the new pick or
    pop bx                          ; 0FFh, CF = 1 the save-under was REFUSED
    jc .repaint                     ; **A PICK IS AL, AND ONLY AL** (SPEC.md
                                    ; 13.14.1). CF=1 is one case and it is not
                                    ; this one - the bank was refused, so the
                                    ; write-back never happened and the whole
                                    ; half is owed; `wd_drrep` states the same
                                    ; rule in as many words. And AH=1 means
                                    ; SPENT, which an OPENING press is too. So
                                    ; the pick is AL != 0FFh and nothing else:
    cmp al, 0FFh                    ; testing CF or AH reads a pick as an open
    je .nopick                      ; and leaves the figure above STANDING at
    call dos_mem_row                ; the last value - which is what shipped,
                                    ; and which the field reported as `changing
                                    ; the disk cache does not change the
                                    ; estimate` (96.36.6.2). The BOX repaints
                                    ; either way - os88ui_drbox runs on the
                                    ; CF=0 path - so the caption and the
                                    ; caption's consequence disagreed, which is
                                    ; the shape that reads as a dead control
.nopick:
    or ah, ah
    jz .boxes
    call dos_defocus
    jmp .out
.boxes:
    push bx                         ; ...then the three check boxes, of which
    call dos_mck_place              ; only the arm's own are live
    mov bx, dos_mhdd
    call dos_mck_hit                ; a greyed box is not offered, which is
    jnc .ckhit                      ; SPEC.md 47 rule 6 - and here it means the
    mov bx, dos_mnet                ; press belongs to the ARM under it
    call dos_mck_hit
    jnc .ckhit
    call dos_mem_whole              ; arm 1's box is not offered while the arm
    jc .notck                       ; itself is greyed - the row is carrying
    mov bx, dos_mmou                ; the REASON there (96.36.8)
    call dos_mck_hit
    jc .notck
.ckhit:
    pop bx                          ; the box redrew itself, but the ARENA row
    call dos_mem_row                ; above it has just moved (96.36.3)
    jmp short .out
.notck:
    pop bx
    cmp byte [dos_keepc], DOS_MEM_IN
    jne .notfld                     ; ...THEN THE LIMIT, which is arm 0's own
                                    ; (SPEC.md 96.36.9) and is INSIDE the
                                    ; radio's rect like the boxes above it -
                                    ; so it asks before the arm does, or a
                                    ; press on it picks the arm it is already
                                    ; in and never reaches the caret
    call dos_mfld_place
    mov si, dos_mln
    mov cx, [dos_mpx]
    mov dx, [dos_mpy]
    call os88line_hit
    jc .notfld
    call dos_defocus_but            ; ...and a field keeping the caret while
    cmp byte [si+LN_FOCUS], 0       ; another control is worked is a caret the
    jne .move                       ; user cannot account for
    mov byte [si+LN_FOCUS], 1
    call os88line_draw
    jmp short .out
.move:
    mov cx, [dos_mpx]
    mov dx, [dos_mpy]
    call dos_caret_to               ; ...and the BAR follows it (96.32.4)
    jmp short .out

.notfld:
    mov cx, [dos_mpx]
    mov dx, [dos_mpy]
    call dos_mrad_place             ; ...AND THE ARM LAST, which is what
    push bx                         ; anything inside a subsection that no
    mov bx, dos_mrad                ; control of its own claimed belongs to.
    call os88ui_radhit              ; os88ui_radhit takes the point in CX/DX,
    pop bx                          ; moves the record's own SEL word and
    jc .away                        ; redraws THE TWO DOTS that changed - not
    jz .repaint                     ; the group and not the page (13.17.4).
    call dos_defocus                ; **TEST ZF BEFORE ANYTHING ELSE IS
    jmp short .out                  ; CALLED**: dos_defocus clobbers the flags,
                                    ; and reading its answer instead of
                                    ; radhit's is a pick that moves and a block
                                    ; that does not repaint. A greyed arm is
                                    ; swallowed here (CF=0, ZF=0), which is
                                    ; SPEC.md 47 rule 6: the control already
                                    ; said why. **A REAL CHANGE REPAINTS THE
                                    ; BLOCK**, because the two subsections
                                    ; under it and the cache's own LIST are
                                    ; the arm's (96.36.6)
.repaint:
    call dos_defocus
    call dos_paint_mem              ; the whole right half, under the lock the
    jmp short .out                  ; handler already holds
.away:
    ; The POINT is still in CX/DX for dos_fld_hit, which tests the two boxes
    ; beside this block with it - and os88ui_radhit DOES carry os88ui_bhit's
    ; "every register preserved", which the check box it replaced did not, so
    ; the point survives a miss without being banked first.
    pop di                          ; **IT REFUSES WHAT IT DID NOT USE** now
    pop si                          ; (SPEC.md 96.32.2): this block shares the
    pop ax                          ; Setup page with two more boxes, so a
    stc                             ; click outside its own two controls is
    ret                             ; theirs to test - where it used to be the
                                    ; whole page and could defocus on sight
.out:
    pop di
    pop si
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; dos_fld_place - put the arguments field where the window is now
; in:  BX = the window; out: nothing, every register preserved
;
; The rect is recomputed from the CONTENT origin on every paint rather than
; banked, because a window moves and os88line's rect is in SCREEN coordinates
; (its LN_X1 comment says so). Banking it would put the caret one drag behind.
; -----------------------------------------------------------------------------
dos_fld_place:
    mov si, dos_ln
    push dx
    mov dx, DOS_SFLD1
    call dos_setbox
    pop dx
    ret

; -----------------------------------------------------------------------------
; dos_cmd_place - the COMMAND box, on the Setup page's TITLE row (SPEC.md
;                 96.32.2.2)
; in:  BX = the window
; out: dos_pln's rect; SI is clobbered, every other register preserved
;
; **IT IS THE SAME BLOCK the main page's top bar places** (96.32.1) and not a
; second one: one `os88line` record, one buffer, so the two rects are two VIEWS
; of the command and there is nothing to mirror. `dos_senv_place` makes the
; same trade one control along, and for the same reason - whichever page is up
; placed it last, which is exactly what a hit test wants.
;
; It sits beside the page's own name rather than in either column, because the
; command is what all the setup below APPLIES to: it is the header, not a
; field. So it spans everything the name does not.
; -----------------------------------------------------------------------------
dos_cmd_place:
    push ax
    push cx
    push dx
    call OSAPI_WM_GEOM              ; CX = content width
    jc .out
    push cx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    mov si, dos_pln
    add cx, ax                      ; CX = the content's RIGHT edge, before AX
    add ax, DOS_CMDX                ; is spent on the left one
    mov [si+LN_X1], ax
    sub cx, DOS_SETPAD + 1
    mov [si+LN_X2], cx
    add dx, DOS_CMDY
    mov [si+LN_Y1], dx
    add dx, DOS_FLDH
    mov [si+LN_Y2], dx
.out:
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_senv_place - ...and the environment box beside it, on the SAME page
; in:  BX = the window; every register preserved
;
; IT IS Environment's FIRST ROW (SPEC.md 96.32.2) - dos_eln, not a block of
; its own - so the two rects address one buffer and there is nothing to
; mirror. dos_erow places the same block for the other page, and whichever
; page is up placed it last, which is exactly what a hit test wants.
; -----------------------------------------------------------------------------
dos_senv_place:
    push cx
    push si
    xor cx, cx
.row:
    call dos_erow
    inc cx
    cmp cx, DOS_ENVN
    jb .row
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; dos_setbox - put the block at SI in the Setup page's LEFT column, DX rows
;              down from the body top
; in:  BX = the window, SI = an os88line block, DX = the row offset
; out: the block's rect; SI and every other register preserved
;
; THE COLUMN IS HALF THE CONTENT and the content is 640 or 720, so a box here
; is 304 or 344 px - 38 or 43 cells - against the 256 it had when the whole
; window was 288 wide. Computed rather than a constant for that reason: the
; number that was right for one window is wrong for all three.
; -----------------------------------------------------------------------------
dos_setbox:
    push ax
    push cx
    push dx
    push di
    mov di, dx                      ; DI = the row offset, across the calls
    call OSAPI_WM_GEOM              ; CX = content width
    jc .out
    shr cx, 1                       ; ...the left half of it
    sub cx, DOS_SETPAD * 2
    push cx
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    pop cx
    mov ax, ax
    add ax, DOS_SETPAD
    mov [si+LN_X1], ax
    add ax, cx
    mov [si+LN_X2], ax
    add dx, DOS_BODYY
    add dx, di
    mov [si+LN_Y1], dx
    add dx, DOS_FLDH
    mov [si+LN_Y2], dx
.out:
    pop di
    pop dx
    pop cx
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
    call dos_mem_row                 ; else; this one is a TERM of the arena
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
    cmp byte [dos_state], DST_RAN
    je .again
    cmp byte [dos_state], DST_ERR
    je .again
    jmp short .console               ; ...and with nothing to re-run it is the
                                     ; prompt's again, which is a fresh line
                                     ; and what DOS does with a bare Enter
.rerun:
    cmp byte [dos_state], DST_RAN    ; only from a program that has FINISHED -
    je .again                        ; DST_READY is one already queued and a
    cmp byte [dos_state], DST_ERR    ; second wake would run it twice
    jne .no
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
; dos_erow_focus - the environment row with the caret, rect placed for drawing
; in:  BX = the window; out: SI = the block, or 0
;
; The rect has to be re-placed before the field is drawn OR hit, because
; os88line's rect is in SCREEN coordinates and the window moves - which is
; dos_fld_place's note, one page along.
; -----------------------------------------------------------------------------
dos_erow_focus:
    push ax
    push cx
    push dx
    push di
    xor cx, cx
.r:
    push cx
    call dos_erow
    cmp byte [si+LN_FOCUS], 0
    jne .got
    pop cx
    inc cx
    cmp cx, DOS_ENVN
    jb .r
    xor si, si
    jmp short .out
.got:
    pop cx
.out:
    pop di
    pop dx
    pop cx
    pop ax
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
    ; **NO DST_IDLE GATE** (SPEC.md 96.32.1): an idle box is the internal
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
    call dos_click_mem              ; the memory block's field and its radio
    jnc .out                        ; CF=1 = none of those, so it is the
    call dos_fld_hit                ; arguments box or an environment row
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
    mov bx, si
    push cx
    push dx
    call dos_place
    pop dx
    pop cx
    mov bx, dos_btrec
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
    mov bx, si
    push cx
    push dx
    call dos_place                  ; the window can have moved (13.7)
    pop dx
    pop cx
    mov bx, dos_btrec
    call os88ui_btnup               ; AX = what FIRED, 0 = cancelled
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
    call dos_defocus                ; the caret does not follow us off the page
    mov byte [dos_page], DOS_PAGE_SET
    call dos_swap
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
    jmp short .out
.sav:
    call dos_defocus                ; ...as Run does, and for its reason
    call dos_mem_take               ; ...and the shortcut carries what is in
    call dos_sav_go                 ; the boxes NOW, for the same reason
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
; one cell. THIS BOX DID NEITHER at all three of its call sites, so a click in
; the middle of a line moved the caret and left the bar standing where it was -
; reported from the field as exactly that.
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
    call dos_defocus                ; 'Environment', which is the bar's other
    mov byte [dos_page], DOS_PAGE_SET
    mov bx, [dos_win]
    call dos_swap
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
; dos_fld_hit - the Setup page's two left-column boxes (SPEC.md 96.32.2)
; in:  BX = the window, CX/DX = the point, the rects already placed
; out: nothing; every register preserved
;
; The memory block's own two controls have had their turn before this is
; reached, so a click that is not in either of these is a click on the page
; background - and the caret goes out, because a caret on a page the user has
; clicked away from is one they cannot account for.
; -----------------------------------------------------------------------------
dos_fld_hit:
    push cx
    push si
    push di
    push bp
    mov si, dos_pln                 ; THE COMMAND BOX FIRST (SPEC.md 96.32.2.2):
    call os88line_hit               ; it is on this page too now, and it is the
    jnc .take                       ; one every option below is about
    mov si, dos_ln
    call os88line_hit               ; preserves CX and DX, so every test below
    jnc .take                       ; gets the same point
    ; **AND EVERY ENVIRONMENT ROW** (SPEC.md 96.32.2.1) - `dos_click_env`'s
    ; loop, moved here now that there is no second page for it to live on.
    ; `dos_erow` spends CX on the row index, so the point is banked across it
    ; exactly as that routine's own caller used to bank it.
    mov di, cx
    mov bp, dx
    xor cx, cx
.erow:
    push cx
    call dos_erow
    mov cx, di
    mov dx, bp
    call os88line_hit
    pop cx
    jnc .take
    inc cx
    cmp cx, DOS_ENVN
    jb .erow
    call dos_defocus
    jmp short .out
.take:
    cmp byte [si+LN_FOCUS], 0
    jne .move                       ; already ours: just move the caret
    call dos_defocus
    mov byte [si+LN_FOCUS], 1
    call os88line_draw
    jmp short .out
.move:
    call dos_caret_to               ; ...and the BAR follows it (96.32.4)
.out:
    pop bp
    pop di
    pop si
    pop cx
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
    call dos_furn_rects             ; Save Shortcut and Return
    call dos_cmd_place              ; ...the command box on the title row...
    call dos_fld_place              ; ...the arguments box...
    call dos_senv_place             ; ...every environment row under it...
    call dos_mfld_place             ; ...and the memory block's two
    call dos_mrad_place
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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
    mov word [dos_keepc], DOS_MEM_IN    ; which is what a double click gets.
                                    ; A WORD: OS88UI_RD_SEL is one (96.36)
    mov word [dos_cache], DOS_CA_AUTO   ; ...and the cache's dial, the same way
                                    ; - one list on both arms (96.36.6), so
                                    ; there is no parked second pick
    mov byte [dos_mhdd + OS88UI_CK_ON], 1   ; ...and the three boxes, which are
    mov byte [dos_mnet + OS88UI_CK_ON], 1   ; TICKED by default: a tick is
    mov byte [dos_mmou + OS88UI_CK_ON], 0   ; "leave it as it is" on the two
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
; dos_erow - the Nth environment row's block and its rect
; in:  CX = the row (0..DOS_ENVN-1), BX = the window
; out: SI = its os88line block, rect filled in; CX preserved
; -----------------------------------------------------------------------------
dos_erow:
    push ax
    push cx
    push dx
    mov ax, DOS_LNSZ
    mul cx                          ; DX:AX, and DX is banked - the high half
    mov si, dos_eln                 ; a row index lands there is nobody's
    add si, ax
    mov ax, DOS_EROWH               ; **THE LEFT COLUMN, NOT THE WHOLE WIDTH**
    mul cx                          ; (SPEC.md 96.32.2.1). The rows had the
    add ax, DOS_SFLD2               ; window to themselves on a page of their
    mov dx, ax                      ; own; they sit under the first one in
    call dos_setbox                 ; Setup's left column now - which is the
    pop dx                          ; SAME width row 0 has always had there,
    pop cx                          ; `os88line` scrolling a 48-char buffer
    pop ax                          ; through a ~37-cell box either way, so
    ret                             ; nothing narrowed

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
    call dos_fld_reload             ; the fields show what the link carried
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
; dos_lnk_build - this instance's state as a Shell Link, into dos_lbuf
; out: CX = its length; CF=1 = it does not fit LNK_MAX
;
; THE HEADER IS 76 BYTES AND ALMOST ALL OF IT IS LEGALLY ZERO - three
; FILETIMEs, the file size, the icon index, the hotkey and three reserved
; fields. What is not zero is the size dword, the fixed CLSID, LinkFlags and
; ShowCommand, so the template below IS the header and nothing is patched.
;
; Each StringData entry is a 2-byte CHARACTER COUNT then the characters, NOT
; NUL-terminated. Count is characters rather than bytes, which are the same
; thing here only because IsUnicode is clear.
; -----------------------------------------------------------------------------
dos_lnk_build:
    push ax
    push bx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov di, dos_lbuf
    mov si, dos_lnk_hdr
    mov cx, LNK_HDR
    cld
    rep movsb

    call dos_lnk_wdir               ; WORKING_DIR: the folder we came from
    jc .no
    call dos_lnk_rel                ; RELATIVE_PATH: `.\` and the 8.3 name
    jc .no
    mov si, dos_args                ; COMMAND_LINE_ARGUMENTS
    call dos_lnk_str
    jc .no
    call dos_lnk_env                ; ...and ours, in an ExtraData block
    jc .no
    call dos_lnk_mem                ; ...and the memory settings, in a SECOND
    jc .no                          ; one (SPEC.md 96.25.2)
    xor ax, ax                      ; the terminal block: any value below 4
    stosw
    stosw
    mov cx, di
    sub cx, dos_lbuf
    clc
    jmp short .out
.no:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; --- dos_lnk_str - SI = a NUL string -> a counted StringData at ES:DI --------
; out: DI advanced; CF=1 = it would pass LNK_MAX and nothing was written
dos_lnk_str:
    push ax
    push cx
    push si
    mov ax, si
    xor cx, cx
.len:
    cmp byte [si], 0
    je .got
    inc si
    inc cx
    jmp short .len
.got:
    mov si, ax                      ; ...back to the start
    mov ax, di
    sub ax, dos_lbuf
    add ax, cx
    add ax, 2
    cmp ax, LNK_MAX
    ja .no
    mov ax, cx
    stosw                           ; the character count...
    jcxz .done
    rep movsb                       ; ...and the characters, no NUL
.done:
    pop si
    pop cx
    pop ax
    clc
    ret
.no:
    pop si
    pop cx
    pop ax
    stc
    ret

; --- dos_lnk_wdir - the launch folder, as a StringData ----------------------
; **DI IS AN OUTPUT AND MUST NOT BE RESTORED.** dos_lnk_str advances it past
; the entry it wrote, and an earlier version of this banked DI across the
; whole routine - so the working directory was written and then OVERWRITTEN by
; the next string, and every field in the file came out one place early.
dos_lnk_wdir:
    push ax
    push bx
    push cx
    push dx
    push si
    ; **STAND WHERE THE PROGRAM IS, FIRST** - dos_path_make's own opening, and
    ; for the same reason: `OSAPI_FILE_PATH` answers for where the MACHINE is
    ; standing (SPEC.md 96.48), which a program that has walked away with
    ; AH=3Bh or AH=0Eh has moved. `dos_sav_go` already orders the build before
    ; the dialog, so the DIALOG's navigation cannot reach this; the program is
    ; the other mover, and until 96.21.2.1 it could only ever write a wrong
    ; FOLDER - under a drive letter it is a wrong folder stated confidently.
    mov dx, [dos_dir]
    mov bl, [dos_vol]
    call dos_be_goto
    jc .bare
    push di                         ; ...only across the CALL that needs it as
    mov di, dos_pbuf + 2            ; a destination of its own - and TWO ALONG,
    mov cx, DOS_PBUF - 2            ; because the drive goes in front of it
                                    ; (SPEC.md 96.21.2.1)
    call OSAPI_FILE_PATH            ; ES is the caller's DS: an X cell sets it
    pop di
    jnc .drv
.bare:
    mov byte [dos_pbuf+2], '\'      ; a refusal is not fatal - `\` is a folder
    mov byte [dos_pbuf+3], 0        ; and the link still resolves from it
.drv:
    ; **AND THE DRIVE, WHICH OSAPI_FILE_PATH DOES NOT ANSWER** (SPEC.md
    ; 19.2.4: OSAPI_FILE_HERE answers that question, so the path slot does
    ; not). Without it a shortcut says `\PRINCE` and resolves against
    ; whichever volume it was READ from, which is right exactly as often as
    ; the link and its program are on one disk.
    mov al, [dos_vol]               ; the box's own volume, which is the one
    add al, 'A'                     ; dos_run resolves the program on
    mov [dos_pbuf+0], al
    mov byte [dos_pbuf+1], ':'
    mov si, dos_pbuf
    call dos_lnk_str                ; ...and DI comes out ADVANCED
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- dos_lnk_rel - `.\NAME.EXT`, which is BOTH spellings --------------------
; Windows wants a link-relative path and so do we, and `.\` satisfies each.
dos_lnk_rel:
    push ax
    push cx
    push si
    push bx
    mov bx, dos_sbuf
    mov byte [bx], '.'
    mov byte [bx+1], '\'
    inc bx
    inc bx
    mov si, dos_name
.c:
    mov al, [si]
    mov [bx], al
    inc si
    inc bx
    or al, al
    jnz .c
    mov si, dos_sbuf
    call dos_lnk_str
    pop bx
    pop si
    pop cx
    pop ax
    ret

; --- dos_lnk_env - the four rows, in an ExtraData block ---------------------
; {size, signature, data}: size counts ITSELF and the signature, so an
; unknown-signature reader steps over the whole thing with one add - which is
; what makes a private block legal rather than a squat.
dos_lnk_env:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, di                      ; bank where the size goes
    add di, 8                       ; ...and step over it and the signature
    mov bx, dos_ebuf
    mov cx, DOS_ENVN
.row:
    push cx
    mov si, bx
    cmp byte [si], 0
    je .next
    mov ax, di
    sub ax, dos_lbuf
    add ax, DOS_ENVBUF + 8
    cmp ax, LNK_MAX
    ja .no
.cp:
    mov al, [si]
    stosb
    inc si
    or al, al
    jnz .cp                         ; ...NUL and all: the block is a SET, and
                                    ; a set's members are NUL-terminated
.next:
    pop cx
    add bx, DOS_ENVBUF
    loop .row
    xor al, al
    stosb                           ; ...and the bare NUL that ends the set
    mov ax, di                      ; now the size, over the hole banked above
    sub ax, dx
    push di
    mov di, dx
    stosw
    xor ax, ax
    stosw                           ; ...a dword, and 512 never needs the top
    mov ax, LNK_EXTSIG & 0xFFFF
    stosw
    mov ax, LNK_EXTSIG >> 16
    stosw
    pop di                          ; ...back to the END of the block
    clc
    jmp short .out
.no:
    pop cx
    stc
.out:
    pop si                          ; **DI IS NOT RESTORED**, for dos_lnk_wdir's
    pop dx                          ; reason: it is an OUTPUT. Banking it here
    pop cx                          ; left the block written and then
    pop bx                          ; OVERWRITTEN by the terminal marker, which
    pop ax                          ; presents as a link with no environment
    ret

; --- dos_lnk_mem - the memory settings, in an ExtraData block of their own --
; A SECOND BLOCK and not two more fields on the first (SPEC.md 96.25.2): that
; one ends in a bare NUL after a variable number of rows, so anything appended
; to it sits at an offset that depends on what the user typed. Here the two
; fields are at a fixed offset inside a fixed-size block, and a reader that
; does not know the signature steps over it with one add - which is what
; ExtraData is specified for.
dos_lnk_mem:
    push ax
    mov ax, di
    sub ax, dos_lbuf
    add ax, LNK_EXT2SZ
    cmp ax, LNK_MAX
    ja .no
    mov ax, LNK_EXT2SZ              ; BlockSize, counting itself
    stosw
    xor ax, ax
    stosw
    mov ax, LNK_EXTSIG2 & 0xFFFF
    stosw
    mov ax, LNK_EXTSIG2 >> 16
    stosw
    mov ax, [dos_memkb]             ; the cap, 0 = as much as the machine gives
    stosw
    mov al, [dos_keepc]             ; ...the arm...
    stosb
    mov al, [dos_cache]             ; ...and the cache dial, WHICH TOOK THE
    stosb                           ; PAD BYTE (SPEC.md 96.36.6): the block
    clc                             ; stays 12 bytes, the signature does not
                                    ; move, and a link written before the dial
                                    ; existed reads a zero there - which is
                                    ; Auto, the default a link without one
                                    ; would have been saved with
    jmp short .out
.no:
    stc
.out:
    pop ax
    ret                             ; DI IS NOT RESTORED - it is an OUTPUT, for
                                    ; dos_lnk_env's reason

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
    call dos_fh_stand               ; **THE MACHINE, NOT THE BOX** (SPEC.md
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

; --- dos_lnk_skip - step SI over one StringData ------------------------------
dos_lnk_skip:
    push ax
    mov ax, [dos_lend]
    sub ax, si
    cmp ax, 2
    jb .no
    mov ax, [dos_lbuf+si]           ; the character count
    add si, 2
    push bx
    mov bx, [dos_lend]
    sub bx, si
    cmp ax, bx                      ; ...against what is LEFT, never against
    pop bx                          ; the buffer (SPEC.md 20.8 rule 2)
    ja .no
    add si, ax
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

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
    add si, 2
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
    cmp ax, LNK_EXT2SZ
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
    call dos_mem_fix                ; carried one was saved with
    call dos_mem_put                ; ...and the field shows what the link said
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
dos_tpl:
    dw 0, MBAR_H, DOS_CONW, DOS_FRAMEH
                                    ; x, y, w, h (SPEC.md 96.32). **x IS 0 AND
                                    ; THAT IS THE WHOLE TRICK**: the span test
                                    ; 11.95.3 applies is "starts at its
                                    ; display's first column and reaches its
                                    ; last", so this is what buys the two
                                    ; border pixels back and puts the content
                                    ; origin on an 8-aligned column. The WIDTH
                                    ; is per-adapter and dos_pref says so;
                                    ; this row is the VGA/CGA one
    dw dos_ttl, dos_paint, dos_key, 0   ; W_ONCLICK is installed by
                                    ; os88ui_btninit (SPEC.md
                                    ; 20.5.1.3.3), so the buttons see
                                    ; the press before dos_click does

    OS88_PREFER dos_pref, DOS_CONW, DOS_FRAMEH,  720, DOS_FRAMEH,  DOS_CONW, 300
                                    ; **A REAL WIDTH AND A GENEROUS HEIGHT**,
                                    ; which is OSAPI_WM_PREFER's own advice
                                    ; (SPEC.md 11.100.1): the width is what
                                    ; matters and is almost never clamped, so
                                    ; Hercules gets its full 720 and the
                                    ; console centres in it. The CGA row asks
                                    ; for 300 and is clamped to what the band
                                    ; plus the dock's strip allows - 17 rows
                                    ; instead of 25 - so this package never
                                    ; has to know the dock exists

    ; --- THE MENU (SPEC.md 12.2), which this box has never had -------------
    OS88_MENUSET dos_menus, dos_ttl, dos_oncmd
        OS88_MENU dos_m_prog, dos_items_prog, 3
    OS88_MENUSET_END dos_menus

dos_m_prog: db 'Program', 0
dos_items_prog: dw dos_l_run, dos_l_tset, dos_l_full

dos_ttl:    db 'DOS', 0
dos_l_args: db 'Arguments:', 0
dos_l_envb: db 'Environment:', 0    ; ...and the rows under it are NAME=VALUE,
                                    ; one to a line (SPEC.md 96.32.2.1)
dos_l_done: db 'Done', 0
dos_l_retb: db 'Return', 0          ; --- the setup area's furniture (96.32.2)
dos_l_tset: db 'Setup', 0           ; ...and its name, which the title row
                                    ; shows and the bar's own button carries -
                                    ; one page, one word (SPEC.md 96.32.2.1)
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
dos_l_memb: db 'Memory', 0
; ...and WHERE THE BUTTON GOES from each page, indexed by DOS_PAGE_*
dos_btn_tab:
    dw dos_l_envb                   ; main -> environment
    dw dos_l_memb                   ; environment -> memory
    dw dos_l_done                   ; memory -> back to the main page
dos_l_meml: db 'Limit:', 0
dos_l_memk: db 'K', 0                ; ...and the UNIT, drawn hard against the
                                     ; field's right edge (SPEC.md 96.36.10.1).
                                     ; The field takes a bare number and `300`
                                     ; is three plausible quantities on this
                                     ; page alone - KB, paragraphs, or a
                                     ; percentage of the arena above it
dos_l_memc: db 'Disk cache:', 0
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
; box is about to run, under whatever is ticked below. One value, one name for
; it, and no word about arms: the user is picking options, not picking arms.
dos_l_marn: db 'Est DOS Ram (Current Machine): '
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; THE WINDOW HALF (SPEC.md 96.43.2)
dos_marn:   db '     KB', 0
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
DOS_MCKW    equ 23                  ; the longest of the two, in cells
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
; asks INT 33h anything is paying for a cursor nobody reads. Off, the box
; hands `kern_dos` a zero KDL_MOUBASE, which kdmouse.inc already reads as
; "no mouse: nothing is hooked and INT 33h answers the still pointer".
dos_l_mmou: db 'Disable the mouse', 0

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
dos_ca_runs:
    db DOS_CA_AUTORUN               ; Auto - kern_dos's own KD_RAH_KEEP
    db 7                            ; 32K
    db 4                            ; 18K
    db 2                            ; 9K
    db 0                            ; Off
dos_lnk_root: db '\', 0

; --- the Shell Link header, 76 bytes, fixed (SPEC.md 96.21) ------------------
dos_lnk_hdr:
    dd 0x0000004C                   ; HeaderSize, and the format's own magic
    db 0x01,0x14,0x02,0x00          ; LinkCLSID {00021401-0000-0000-
    db 0x00,0x00, 0x00,0x00         ;            C000-000000000046}, in the
    db 0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46   ; mixed-endian GUID order
    dd LNK_F_WDIR | LNK_F_RELP | LNK_F_ARGS      ; LinkFlags
    dd 0                            ; FileAttributes
    dd 0, 0                         ; CreationTime  - legally zero...
    dd 0, 0                         ; AccessTime
    dd 0, 0                         ; WriteTime
    dd 0                            ; FileSize      - ...and so is this
    dd 0                            ; IconIndex
    dd 1                            ; ShowCommand = SW_SHOWNORMAL
    dw 0                            ; HotKey
    dw 0                            ; Reserved1
    dd 0                            ; Reserved2
    dd 0                            ; Reserved3
DOS_LNK_HDRLEN equ $ - dos_lnk_hdr
%if DOS_LNK_HDRLEN != LNK_HDR
 %error "the Shell Link header is 76 bytes and this template is not"
%endif
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
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

dos_errs:
    dw dos_e_goto, dos_e_mem, dos_e_read, dos_e_big, dos_e_fsx, dos_e_exe
    dw dos_e_badexe, dos_e_fit, dos_e_hand
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_goto:  db 'Its folder could not be opened.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_mem:   db 'Not enough memory.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_hand:  db 'A handover is already under way.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_read:  db 'It could not be read.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_big:   db 'Too large for one segment.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_fsx:   db 'The screen is already in use.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_exe:   db '.EXE is not supported yet.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_badexe: db 'Its .EXE header is malformed.', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_fit:   db 'Program too big to fit in memory.', 0  ; DOS 3.30's own words,
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
                                    ; measured at COMMAND.COM offset 2436
dos_dotdot:  db '..', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_s_epath: db 'PATH=', 0        ; **NEVER AN EMPTY SET** (SPEC.md
                                  ; 96.44.13.1): a real DOS always has a
                                  ; row, so a program that walks the set
                                  ; to reach its own path always can
dos_s_blast: db 'BLASTER=A', 0
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_mlen:    db 31,28,31,30,31,30,31,31,30,31,30,31
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_dowt:    db 0,3,2,5,0,3,5,1,4,6,2,4    ; Sakamoto's month table
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_e_fn:    db 'It asked for INT 21h AH='
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)
dos_fnd:     db '00h.', 0
%endif                              ; DOS_EXTCORE
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
    je .hide                       ; (SPEC.md 96.10.5) - in the windowed box
    cmp ax, 3                      ; DHK_TXT is absent and both are the no-op
    je .pos                        ; they were
    cmp ax, 4
    je .none                       ; set position: warping the host pointer is
    cmp ax, 5                      ; the kernel's, and a program that borrowed
    je .press                      ; the screen has not borrowed the arrow
    cmp ax, 6
    je .release
    cmp ax, 7
    je .none                       ; set the X / Y RANGE. We clamp nothing -
    cmp ax, 8                      ; the host's pointer is already inside the
    je .none                       ; screen - so these are no-ops, but they
                                   ; must be no-ops that LEAVE AX ALONE, which
                                   ; is the whole of SPEC.md 96.10.6 and is
                                   ; what Microsoft Works reads as "is there a
                                   ; mouse"
    cmp ax, 0x0A
    je .tcur
    cmp ax, 0x0B
    je .motion
    cmp ax, 0x0C
    je .setevt
    cmp ax, 0x14
    je .swpevt
    jmp .none

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
                                   ; the WINDOWED box there is no cursor at
                                   ; all, so hooking IRQ0 there would buy
                                   ; nothing and cost the DOS task's slice a
                                   ; frame every tick. One-shot, so this and
                                   ; `0Ch` compose
.shpaint:
    call dos_m33_paint
    jmp .none
.hide:
    ; AX=0002h, and it has NO FLOOR on purpose - a program that hides five
    ; times has asked for five shows, and clamping here would put the cursor
    ; back up in the middle of its drawing.
    dec byte [dos_m33shw]
    call dos_m33_wipe
    jmp .none
.tcur:
    ; AX=000Ah: BX = 0 SOFTWARE (CX = the screen mask, DX = the cursor mask),
    ; BX = 1 HARDWARE - a CRTC scan-line pair, which is the machine's own text
    ; caret and not something a POINTER may take over. The hardware arm is
    ; ignored rather than refused, for `.none`'s reason: a program that asks
    ; for a shape and is refused still expects a cursor.
    or bx, bx
    jnz .none
    pushf                          ; one critical section over all four, for
    cli                            ; `dos_m33_paint`'s reason one level up
    call dos_m33_wipe              ; off FIRST, with the masks it went on with
    mov [dos_m33sm], cx            ; - restoring under the new pair would put
    mov [dos_m33cm], dx            ; back a cell that was never there
    call dos_m33_paint
    popf
    jmp .none

.reset:
    call dos_m33_hidden            ; a reset puts the cursor away and takes the
                                   ; masks back to the pair a driver powers up
                                   ; with (SPEC.md 96.10.5)
    call dos_mou_zero              ; a reset clears the edge state with it
    call dos_m33_drop              ; ...and the EVENT HANDLER, which is what a
    mov ax, 0xFFFF                 ; real driver's reset does (SPEC.md 96.10.4)
    mov bx, 2                      ; a mouse IS installed - and it is, whatever
    jmp .out                       ; the machine has, because the kernel found
                                   ; one at boot or the pointer would not move
.pos:
    call dos_mou_read              ; BX = the buttons, CX = x, DX = y
    jmp .out
.press:
    mov si, bx                     ; the button asked about, banked before the
    call dos_mou_read              ; read overwrites BX with the live mask
    mov ax, bx
    and si, 1                      ; two buttons, so anything else is button 1
    mov bl, [si+dos_mou_pc]
    mov byte [si+dos_mou_pc], 0    ; reading a count CONSUMES it, which is why
    xor bh, bh                     ; it is a count and not a flag
    mov cx, [dos_mou_px]
    mov dx, [dos_mou_py]
    jmp .out
.release:
    mov si, bx
    call dos_mou_read
    mov ax, bx
    and si, 1
    mov bl, [si+dos_mou_rc]
    mov byte [si+dos_mou_rc], 0
    xor bh, bh
    mov cx, [dos_mou_rx]
    mov dx, [dos_mou_ry]
    jmp short .out
.motion:
    call dos_mou_delta             ; CX = dx, DX = dy since the last call -
    jmp short .out                 ; derived, see the header
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
    ; MICROSOFT WORKS IS THAT CALLER and it cost this box the whole feature
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
; port's - and this box has neither: the kernel owns both ISRs and keeps
; `mouse_x`/`mouse_y`/`mouse_btn` fresh for the whole bracket (SPEC.md 53.1),
; which is what makes function 3 exact and costs a translation instead of a
; driver. What it does not give is a moment of OUR code running, and a
; callback needs one.
;
; Everything else the box gets control at is the program calling us, and a
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
    mov word [es:0x08*4], dos_m33_tick
    mov [es:0x08*4+2], cs
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
    cmp word [dos_m33h+2], 0        ; no handler: two instructions a tick
    je .out
    cmp byte [dos_m33bsy], 0        ; a callback that ran long enough to be
    jne .out                        ; interrupted by the next tick
    mov byte [dos_m33bsy], 1

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
    mov ah, [dos_m33lb]
    mov si, bx                      ; SI = the live mask, AH = the last one
    test si, 1
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
    test si, 2
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
    ; --- AND THE BOX COUNTS ITS OWN CALLBACKS (SPEC.md 96.10.3.1), because
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
    call far [dos_m33h]             ; AX = the events, BX = the buttons,
                                    ; CX/DX = where, SI/DI = the mickeys, and
                                    ; DS = OURS, which is the driver's own -
                                    ; a handler sets up its own from CS
.nothing:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    mov byte [dos_m33bsy], 0
.out:
    pop ds
    pop ax
    iret
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
; **IT IS `kern_dos`'s CAPABILITY AND NOT THE BOX'S**, and `DHK_TXT` is how
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
    ; box this routine is on a hot path and can never do anything: it is
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
                                    ; windowed box and most of them elsewhere
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
%include "os88parts.inc"
OS88_PARTS_BEGIN 1
  OS88_PART OP_ASSET, OP_ZERO | OP_OPT, DOS_TRACE_KB
OS88_PARTS_END
%endif
%endif                              ; DOS_EXTCORE

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
; IMAGE became `apps/dos/dosload.asm` - which reads the box out of part 0 and
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
%endif

%include "dosnetabi.inc"             ; the cable translation's numbers, EARLY
                                    ; (SPEC.md 96.26) - its code is dosnet.inc
                                    ; at the end, and the bss table below
                                    ; cannot see an equ from there

PKT_NHAND   equ 4                   ; handles. mTCP opens ONE (IP) and ARP
                                    ; rides the same one; four is room for a
                                    ; client that separates them and one more
PKT_HUSED   equ 0                   ; --- a handle row ---
PKT_HTYPE   equ 1                   ; word: the ethertype, big-endian as it
                                    ; sits on the wire. 0 = every frame
PKT_HRCVO   equ 3                   ; word: the client's receiver...
PKT_HRCVS   equ 5                   ; word: ...far
PKT_HSIZE   equ 7

PKT_VEC_LO  equ 0x60                ; the range the spec reserves, and which a
PKT_VEC_HI  equ 0x80                ; client walks looking for the signature

; --- what CF=1 means, in DH (Crynwr) -----------------------------------------
PKE_BADHAND equ 1
PKE_NOCLASS equ 2
PKE_NOTYPE  equ 3
PKE_NONUM   equ 4
PKE_BADTYPE equ 5
PKE_NOSPACE equ 9
PKE_TYPEUSED equ 10
PKE_BADCMD  equ 11
PKE_CANTSEND equ 12

; --- get_statistics' record: six DWORDS, in the Crynwr order -----------------
; These are the numbers mTCP's PKTTOOL prints, so they are kept for real
; rather than left at zero - and they used to be two DIAGNOSTIC word counters
; written over bytes_out and errors_in, which made a debugging aid into a
; wrong answer to a published call.
PKS_PIN     equ 0                   ; packets in  - frames handed to a client
PKS_POUT    equ 4                   ; packets out - frames a client sent
PKS_BIN     equ 8                   ; ...and their lengths, as the client
PKS_BOUT    equ 12                  ; asked for them rather than as padded
PKS_ERRIN   equ 16                  ; errors in: nothing here can report one -
                                    ; a frame the driver could not read never
                                    ; reaches us at all
PKS_DROP    equ 20                  ; dropped: no handle matched it, or the
                                    ; client refused the buffer

PKT_ETYPE   equ 12                  ; where the ethertype sits in a frame. The
                                    ; ABI publishes the header's SIZE and this
                                    ; is the one offset inside it a demux needs
PKT_STK     equ 1024                ; the tick poll's own stack
                                    ; (SPEC.md 96.23.4.1). The chain under it
                                    ; is OSAPI_DRV_CALL into the kernel, the
                                    ; driver's verb, ne_rx's DMA loop and then
                                    ; the CLIENT's receiver - and the last of
                                    ; those is the one we cannot measure, so
                                    ; this is cut generously rather than to a
                                    ; walked depth

; =============================================================================
; THE NETWORK CLAIM (SPEC.md 96.23.7) - every byte the wire needs, and NONE
; of it in this package's bss
; =============================================================================
; **A BSS BYTE IS A BYTE THE DOS PROGRAM CANNOT HAVE.** §96.3 hands the
; program OSAPI_MEM_AVAIL's whole answer, and this package's image + bss is
; claimed off the same heap first - so every byte declared below the DBSS
; macro comes straight out of the arena, on every machine, whether or not
; there is a wire to use it. The frames, the staging copy and the poll's
; private stack were 4,052 bytes of exactly that: on the 128KB floor machine
; that is 8% of the arena spent on a driver kern_small does not even ship
; (SPEC.md 24.5).
;
; So they live in a claim `dos_pkt_bufs` takes only when net_find answers -
; and the shape that makes it nearly free is that **DS IS THIS CLAIM inside
; every dn_* routine** (SPEC.md 96.26.6). dosnet.inc's premise was already
; "one segment, no segment override", so pointing that one segment at the
; claim instead of at ourselves leaves all 78 of its frame references
; untouched: what changes is the VALUE of the symbols, not the code.
;
; It also collapses two branches that were only ever about which buffer:
; dos_pkt_deliver and dos_pkt_poll each had a `[dos_pkt_xl]` arm choosing
; between the claim and our bss, and both arms are now the same claim.
; **THE ORDER IS THE CARD'S NEEDS FIRST**, so that the two routes' claims
; nest: a card wants the received frame and the stack and nothing else, and
; putting the cable-only parts above them means the card claims 3KB rather
; than paying for a hole.
PKB_RX      equ 0                   ; NET_FRAME: the frame FOR the client -
                                    ; the card's received one, or the one the
                                    ; translation built
PKB_STK     equ 1536                ; PKT_STK bytes of private stack
PKB_STKTOP  equ PKB_STK + PKT_STK
PKB_TX      equ PKB_STKTOP          ; NET_FRAME: the CLIENT's own frame,
                                    ; staged - the CABLE PATH ALONE, because
                                    ; the card hands the client's buffer to
                                    ; the driver where it lies (96.23.8)
PKB_STATE   equ PKB_TX + 1536       ; ...and the translation's own state, laid
                                    ; out by dosnetabi.inc's DNB_*
PKT_RXOFF   equ PKB_RX              ; the packet driver's own name for it,
                                    ; kept because SPEC.md 96.23.7 publishes
                                    ; that one

; **THE TWO ROUTES CLAIM DIFFERENT AMOUNTS**, which is the other half of not
; spending a byte that cannot be used: a card needs the received frame and the
; stack and nothing else, because send_pkt hands the client's own buffer to the
; driver where it lies (SPEC.md 96.23.8). The cable needs the staging frame and
; the whole translation state on top.
PKB_CARDKB  equ (PKB_STK + PKT_STK + 1023) / 1024
PKB_XLKB    equ (PKB_STATE + DNB_SIZE + 1023) / 1024

PKT_CLASS   equ 1                   ; DIX Ethernet (Blue Book), which is what
                                    ; an NE2000 is and what mTCP expects
PKT_TYPE    equ 1
PKT_FUNC    equ 2                   ; basic plus extended: set/get_rcv_mode
                                    ; and get_statistics are answered
PKT_VERSION equ 9

; **THE CHAIN STARTS PAST THE PARTS STANDARD'S OWN BSS** in the trace build
; (SPEC.md 96.29.1). os88parts.inc puts its 86 bytes at OP_BSS_AT, which
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
; IT IS AN `OP_ROW` AND NOT A STRUCT OF OUR OWN: the box read
; `[dos_kdrow + OP_R_OFF]` when the part table was in its own image, so a
; verbatim copy leaves all four of those read sites spelled as they were and
; moves only the base.
    HBSS DOS_B_KDROW, 8          ; kind, flags, dw off, dw len, dw zkb
    HBSS DOS_B_COREROW, 8        ; ...and the CORE part's, beside it - the stub
                                 ; reads both (SPEC.md 96.44.5.4)
    ; --- THE CONSOLE BAND (SPEC.md 96.32), four words dos_con_geom fills and
    ;     the console wave reads. Not banked "for one paint" like the line
    ;     above: they are the answer to a question about the WINDOW, so
    ;     anything that wants them asks for them again rather than trusting a
    ;     paint to have run.
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    HBSS DOS_B_CONX,  2          ; the band's left in SCREEN px, 8-aligned
    HBSS DOS_B_CONY,  2          ; ...its top, below the bar
    HBSS DOS_B_CONCOLS, 2        ; ...and its size in CELLS: 80, and the rows
    HBSS DOS_B_CONROWS, 2        ; are 25 or whatever the adapter allows
    ; --- THE TOP BAR (SPEC.md 96.32.1) --------------------------------------
    HBSS DOS_B_PLN,   DOS_LNSZ   ; the path box's os88line block...
    HBSS DOS_B_PATH,  DOS_PBUF   ; ...and its text, which is the only place
                                 ; this box has ever held a PATH rather than
                                 ; a volume, a cluster and an 8.3 name
    HBSS DOS_B_ERECT, 8          ; 'Environment' and 'Run', four words each
    HBSS DOS_B_RRECT, 8
    HBSS DOS_B_TVOL,  1          ; dos_path_take's scratch: the parsed volume
    HBSS DOS_B_TNAME, 13         ; and name, held apart until the walk has
                                 ; agreed, so a typo cannot half-commit
DOS_BTREC_SZ equ 16              ; **A MIRROR OF os88ui.inc's OS88UI_BT_SIZE**,
                                 ; and it has to be one: this block is laid
                                 ; out thousands of lines before os88ui.inc is
                                 ; included, so the real constant is not
                                 ; defined yet. The %if beside that include
                                 ; fails the BUILD if the two ever disagree,
                                 ; which is the only thing that makes a second
                                 ; copy of a number safe
    ; --- THE SETUP AREA'S FURNITURE (SPEC.md 96.32.2), four words each -------
    HBSS DOS_B_TRECT, 8          ; 'Return' and 'Save Shortcut', and they are
    HBSS DOS_B_SRECT, 8          ; ADJACENT ON PURPOSE (SPEC.md 20.5.1.3): a
                                 ; button group's rects must be contiguous
                                 ; because os88ui_btnpress walks them, so this
                                 ; pair is the Setup page's group and the pair
                                 ; above is the bar's. dos_srect used to sit
                                 ; nine declarations further down
    HBSS DOS_B_LNV,   2          ; the field's view and length as they were
    HBSS DOS_B_LNL,   2          ; before a keystroke (os88line_edit's inputs)
    HBSS DOS_B_LNC,   2          ; ...and its caret, which is the third of them
    HBSS DOS_B_NCELL, 2          ; glyph cells the edits have redrawn...
    HBSS DOS_B_NKEY,  2          ; ...over this many keystrokes
    HBSS DOS_B_PAGE,  1          ; which page is up (DOS_PAGE_*)
    HBSS DOS_B_BRECT, 8          ; the page button's rect, x1 y1 x2 y2
    HBSS DOS_B_BTREC, DOS_BTREC_SZ  ; the standard button record (20.5.1.4),
                                 ; REPOINTED per page by dos_place - one
                                 ; record, because only one page is ever up.
                                 ; DOS_B_SRECT is NOT here any more: it moved
                                 ; up beside DOS_B_TRECT, a button group's
                                 ; rects having to be contiguous
    HBSS DOS_B_ABON,  1          ; the About card is up (SPEC.md 96.51)
%endif
    DBSS DOS_B_ERP,   2          ; the environment row being emitted
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    HBSS DOS_B_LBUF,  LNK_MAX    ; a shortcut, read or written
    HBSS DOS_B_SBUF,  20         ; ...and `.\NAME.EXT` while one is built
%endif
    DBSS DOS_B_CNAME, 14         ; one component of a link's working directory
    DBSS DOS_B_FBUF,  24         ; ...and OSAPI_FIND_SZ while it is looked up
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    HBSS DOS_B_WNAME, 14         ; ...and the name a Save dialog chose, which
                                 ; may NOT share dos_sbuf: the builder uses it
    HBSS DOS_B_LEND,  2          ; how many bytes of dos_lbuf are real
%endif
    DBSS DOS_B_EBUF,  DOS_ENVN * DOS_ENVBUF   ; the environment rows...
%ifndef KD_BACKEND                  ; the window's own state (SPEC.md 96.43.2)
    HBSS DOS_B_ELN,   DOS_ENVN * DOS_LNSZ     ; ...and their field blocks
%endif
    DBSS DOS_B_ARGS,  DOS_ARGSZ  ; the user's arguments, NUL-terminated
    DBSS DOS_B_PBUF,  DOS_PBUF   ; ...and the program's own path, for the env
    HBSS DOS_B_LN,    DOS_LNSZ  ; the arguments field's block (os88line.inc)
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
    HBSS DOS_B_MBUF,  DOS_MEMBUF ; the limit field's text...
    HBSS DOS_B_MLN,   DOS_LNSZ   ; ...and its os88line block
    HBSS DOS_B_MX,    2          ; the memory page's content origin, banked
    HBSS DOS_B_MY,    2          ; for one paint
    HBSS DOS_B_MSX,   2          ; ...and A PRESS's own x and y, banked for one
    HBSS DOS_B_MSY,   2          ; click (SPEC.md 96.36.4). It used to ride in
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
    DBSS DOS_B_MRC,   2        ; ...and release counts
    DBSS DOS_B_MPX,   2        ; where the last press landed, shared
    DBSS DOS_B_MPY,   2        ; between the buttons (SPEC.md 96.10.1)
    DBSS DOS_B_MRX,   2
    DBSS DOS_B_MRY,   2
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
; therefore exact, and a write anywhere else REFUSES (SPEC.md 96.11.2) rather
; than reporting a success it did not have.
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
FH_SIZEOF   equ 23
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
                                    ; It cost the INLINE box two drives of
                                    ; reach on a kern_big machine, and it cost
                                    ; the PARTED box the whole console
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
                                    ; grows a ninth volume costs this box reach
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
    mov cx, FH_SIZEOF * DOS_NFH     ; saying so costs six bytes against a
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
    shl bx, cl                      ; sectors -> bytes
    mov [dos_cbytes], bx

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
    add di, DTA_PAT
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
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

    cmp byte [dos_fabs], 0          ; "\" or "\NAME": from the VOLUME's root.
    je .rel                         ; There is no jail any more - a program
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; =============================================================================
; DRIVES (SPEC.md 96.6.1)
; =============================================================================
; AH=0Eh used to answer the COUNT and never move, on the reasoning that a
; program reads the count far more often than it changes drives and that a
; real change wanted a directory walk this box did not have. The first half is
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
    mov bx, 100                     ; field below is checked for a value that
    mul bx                          ; is merely POSSIBLE before any of it is
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
; It writes 0040:006C, which is banked at bracket entry and put back at the
; end (SPEC.md 96.5) - so a DOS program may set the clock, read it back and
; agree with itself, and the machine's own time is not moved by it.
; -----------------------------------------------------------------------------
dos_time_set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov [dos_tmp1], ch
    mov [dos_tmp2], cl
    mov [dos_tmp3], dh

    mov al, [dos_tmp1]
    xor ah, ah
    mov bx, 32772
    mul bx
    shl ax, 1
    rcl dx, 1                       ; hours, in ticks
    mov si, ax
    mov di, dx
    mov al, [dos_tmp2]
    xor ah, ah
    mov bx, 1092
    mul bx
    add si, ax
    adc di, dx
    mov al, [dos_tmp3]
    xor ah, ah
    mov bx, 182
    mul bx
    mov bx, 10
    div bx
    xor dx, dx
    add si, ax
    adc di, dx

    mov bx, 0x40
    mov es, bx
    mov [es:0x6C], si
    mov [es:0x6E], di
    mov [dos_lasttl], si            ; ...and midnight is measured from here now
    mov [dos_lastth], di
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
    cli                             ; SS and SP are loaded as a pair, always:
    mov ss, cx                      ; an interrupt between them lands on a
    mov sp, bx                      ; stack that is half of each
    sti
    mov ds, dx
    mov es, dx
    xor ax, ax                      ; AL/AH = the two FCB drive checks, and 0
                                    ; is "both valid"
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
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
    mov bx, dos_xms_ent
    pop ds
    pop bp
    push cs
    pop es
    iret
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
    mov bx, 0
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
    add word [dos_xmcon+2], 0x1000  ; one segment's worth
    jmp short .chunk
.done:
    clc
    jmp short .out
.bothc:
    mov bl, 0x8E                    ; conventional to conventional: a program
    jmp short .err                  ; with two far pointers can `movsb`
.bothx:
    mov bl, 0x8E                    ; ...and extended to extended has no slot
    jmp short .err
.badsh:
    mov bl, 0xA3
    jmp short .err
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
%endif                              ; DOS_EXTCORE


; =============================================================================
; THE DRIVERS, OUT OF THE WAY (SPEC.md 96.17)
; =============================================================================
; A DOS program that wants the Sound Blaster wants to program it ITSELF -
; reset the DSP, set its own IRQ and DMA, own the card completely - and
; os8088's SOUND.DRV is in the way of that in three separate ways: it owns an
; IRQ vector, it owns DMA channel 1, and its refill worker is TF_SERVICE, so
; it KEEPS RUNNING inside the bracket by design (SPEC.md 53.2) and can feed
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

dos_pkt_name: db 'os8088 ETHER', 0

; -----------------------------------------------------------------------------
; dos_pkt_entry - THE VECTOR (SPEC.md 96.23.2)
;
; The first three bytes are a jump and the signature starts at offset 3, which
; is the whole of how a client finds this. A short jump would be two and put
; the signature one byte early, so the assertion below is not decoration - it
; is the one thing in this file a reader cannot check by eye.
; -----------------------------------------------------------------------------
dos_pkt_entry:
    jmp near dos_pkt_go
dos_pkt_sig:
    db 'PKT DRVR', 0
%if dos_pkt_sig - dos_pkt_entry != 3
 %error "the PKT DRVR signature must begin at offset 3 of the handler (Crynwr)"
%endif

; -----------------------------------------------------------------------------
; The gate, in dos_int21's shape and for its reasons (SPEC.md 96.7.1): entered
; on the CLIENT's stack with the client's segment registers, so the first thing
; it does is reach its own data through CS, and the carry flag it returns is
; the one in the FLAGS image the `int` pushed.
;
;   [bp]=DS [bp+2]=BP [bp+4]=IP [bp+6]=CS [bp+8]=FLAGS
;   [bp-2]=SI [bp-4]=DI [bp-6]=ES
;
; SI, DI and ES are banked BELOW bp and restored from there, so a handler that
; leaves the stack at any depth still returns the client's registers - and
; `driver_info` writes its DS:SI answer into [bp] and [bp-2] rather than into
; the live registers, which is the same trick AH=35h uses one section up.
; -----------------------------------------------------------------------------
dos_pkt_go:
    sti
    push bp
    push ds
    mov bp, sp
    push si
    push di
    push es
    push ds                         ; **THE CLIENT'S DS, BANKED FROM THE
    push cs                         ; REGISTER** (SPEC.md 96.23.9) - the
    pop ds                          ; version that read it back out of [bp]
    pop word [dos_pkt_cds]          ; answered our OWN segment, and `[bp]` is
                                    ; SS-relative so it was never going to be
                                    ; checkable by eye. This pops the value
                                    ; pushed one instruction earlier, with DS
                                    ; already ours so the store lands here

    call dos_pkt_poll               ; **DRAIN FIRST** (SPEC.md 96.23.4): a
                                    ; client in a send loop receives as it
                                    ; sends, without waiting for a tick

    cmp ah, 1
    je .info
    cmp ah, 2
    je .access
    cmp ah, 3
    je .release
    cmp ah, 4
    je .send
    cmp ah, 5
    je .term
    cmp ah, 6
    je .getaddr
    cmp ah, 20
    je .setmode
    cmp ah, 21
    je .getmode
    cmp ah, 24
    je .stats
    mov dh, PKE_BADCMD              ; a client asking for a feature it can
    jmp .err                        ; live without expects exactly this

; --- AH=1 driver_info --------------------------------------------------------
; out BX=version CH=class DX=type CL=number DS:SI=name AL=functionality
.info:
    mov bx, PKT_VERSION
    mov ch, PKT_CLASS
    mov dx, PKT_TYPE
    mov cl, 0                       ; interface number
    mov al, PKT_FUNC
    mov word [bp-2], dos_pkt_name   ; the BANKED SI and DS, not the live ones:
    mov [bp], cs                    ; the exit path restores both from here
    jmp .ok

; --- AH=2 access_type --------------------------------------------------------
; in AL=if_class BX=if_type DL=if_number DS:SI=type CX=typelen ES:DI=receiver
; out AX = a handle
;
; DS:SI is the CLIENT's - our own DS is CS by now - so the ethertype is read
; through the banked [bp]. ES and DI are still the client's in the live
; registers, which is what makes the receiver pointer a plain bank.
.access:
    cmp al, PKT_CLASS
    jne .noclass
    cmp bx, PKT_TYPE                ; 0FFFFh is the spec's "any type of card"
    je .clsok
    cmp bx, 0xFFFF
    jne .notype
.clsok:
    or dl, dl                       ; one card, number 0
    jz .numok
    cmp dl, 0xFF
    jne .nonum
.numok:
    push cx
    push si
    xor ax, ax                      ; AX = the ethertype, 0 = every frame
    or cx, cx
    jz .anytype
    cmp cx, 2                       ; **A LENGTH OTHER THAN 2 IS REFUSED**
    jne .badtype                    ; rather than read short: an ethertype is
    push es                         ; two bytes and a client that passed a
    mov es, [bp]                    ; longer one means a protocol this card
    mov ah, [es:si]                 ; layer does not have (802.2 LSAPs)
    mov al, [es:si+1]
    pop es
.anytype:
    call dos_pkt_hnew               ; BX = the row, or CF=1 = full
    jc .nospace
    mov [bx+PKT_HTYPE], ax
    mov [bx+PKT_HRCVO], di
    mov ax, es
    mov [bx+PKT_HRCVS], ax
    mov byte [bx+PKT_HUSED], 1
    call dos_pkt_claim              ; **THE WIRE, ON THE FIRST HANDLE**
    jc .noclaim                     ; (SPEC.md 96.23.5) - not at bracket entry
    mov ax, bx                      ; the handle IS the row address: it is
    pop si                          ; ours to choose and this makes every
    pop cx                          ; later lookup a bounds check instead of
    jmp .ok                         ; a multiply
.badtype:
    pop si
    pop cx
    mov dh, PKE_BADTYPE
    jmp .err
.nospace:
    pop si
    pop cx
    mov dh, PKE_NOSPACE
    jmp .err
.noclaim:
    mov byte [bx+PKT_HUSED], 0      ; the row goes back: a handle that cannot
    pop si                          ; receive is worse than a refusal
    pop cx
    mov dh, PKE_NOSPACE
    jmp .err
.noclass:
    mov dh, PKE_NOCLASS
    jmp .err
.notype:
    mov dh, PKE_NOTYPE
    jmp .err
.nonum:
    mov dh, PKE_NONUM
    jmp .err

; --- AH=3 release_type -------------------------------------------------------
; in BX = the handle
.release:
    call dos_pkt_hchk
    jc .badhand
    mov byte [bx+PKT_HUSED], 0
    call dos_pkt_idle               ; the last handle takes the claim with it
    jmp .ok
.badhand:
    mov dh, PKE_BADHAND
    jmp .err

; --- AH=4 send_pkt -----------------------------------------------------------
; in DS:SI = the frame, CX = its length. The client's DS again.
.send:
    cmp cx, NET_EHSIZE
    jb .cantsend
    cmp cx, NET_FRAME
    ja .cantsend
    ; --- **THERE IS NO STAGING COPY** (SPEC.md 96.23.8) --------------------
    ; NETV_RAWTX takes the segment (SPEC.md 72.22.4), so the client's own
    ; buffer is handed over where it lies: DX = the DS the `int` pushed, SI =
    ; the offset it was called with. The driver copies into eth_txb either
    ; way, so a staging copy of ours was a SECOND copy of 1,514 bytes on a
    ; 4.77MHz machine and 1,514 bytes of claim to hold it - and it was where
    ; a whole class of segment bug lived, because it was the one place this
    ; package addressed the client's memory itself.
    mov dx, [dos_pkt_cds]           ; the CLIENT's DS, banked at the gate
    push cx                         ; ...and the LENGTH, because get_statistics
                                    ; wants it after the route has run and
                                    ; OSAPI_DRV_CALL publishes CX as the
                                    ; driver's to define
    cmp byte [dos_pkt_xl], 0
    jne .xlate                      ; the cable carries no frames (SPEC.md
                                    ; 72.22.3), so they are TRANSLATED
    mov bh, DRVC_NET
    mov bl, NETV_RAWTX
    call OSAPI_DRV_CALL
    jnc .sent
    pop cx
    jmp short .cantsend
.xlate:
    ; --- the translation reads the frame in OUR segment --------------------
    ; dn_tx and everything under it use no segment override, because every
    ; other byte they touch is ours. So this is the one copy the card path
    ; does not make - 42 to 1514 bytes, against a wire that moves 3,741 a
    ; second, which is not the cost that decides anything here.
    push cx
    push si
    push di
    push es
    push ds
    mov es, [dos_pkt_bseg]          ; ES:DI is the CLAIM's staging frame and
    mov di, dos_pkt_txs             ; DS:SI the client's own buffer
    mov ds, dx
    call dos_pkt_copy               ; DS:SI -> ES:DI, CX bytes
    pop ds
    pop es
    pop di
    pop si
    pop cx
    mov si, dos_pkt_txs
    call dn_tx
.sent:
    ; --- get_statistics' OWN numbers, and they are the real ones -----------
    ; Both routes end here so the count is written once. It is the client's
    ; own frame length that is added, not the driver's padded one: a Crynwr
    ; client asked for these bytes and mTCP's PKTTOOL prints them back.
    pop cx
    add word [dos_pkt_stats+PKS_POUT], 1
    adc word [dos_pkt_stats+PKS_POUT+2], 0
    add word [dos_pkt_stats+PKS_BOUT], cx
    adc word [dos_pkt_stats+PKS_BOUT+2], 0
    jmp .ok
.cantsend:
    mov dh, PKE_CANTSEND
    jmp .err

; --- AH=5 terminate ----------------------------------------------------------
.term:
    call dos_pkt_hchk
    jc .badhand
    call dos_pkt_rawdrop            ; NOT dos_pkt_shut: the buffers are the
    jmp .ok                         ; bracket's and a program that terminates
                                    ; the driver may still open it again

; --- AH=6 get_address --------------------------------------------------------
; in BX = handle, ES:DI = a buffer, CX = its size; out CX = bytes written
.getaddr:
    call dos_pkt_hchk
    jc .badhand
    cmp cx, 6
    jb .badhand
    push si
    push di
    mov si, dos_pkt_mac             ; banked by the claim (SPEC.md 72.22), so
    mov cx, 6                       ; this costs no driver call at all
    call dos_pkt_copy             ; DS:SI -> ES:DI
    pop di
    pop si
    mov cx, 6
    jmp .ok

; --- AH=20 / 21 set and get receive mode -------------------------------------
; 3 is "every frame addressed to me, plus broadcast", which is the mode
; ne_init leaves the card in and the only one this driver can honestly offer.
.setmode:
    cmp cx, 3
    jne .badmode
    mov byte [dos_pkt_mode], 3
    jmp .ok
.badmode:
    mov dh, PKE_BADTYPE
    jmp .err
.getmode:
    xor ax, ax
    mov al, [dos_pkt_mode]
    jmp .ok

; --- AH=24 get_statistics ----------------------------------------------------
; out DS:SI = six dwords: packets in, packets out, bytes in, bytes out,
;     errors in, packets dropped.
.stats:
    mov word [bp-2], dos_pkt_stats
    mov [bp], cs
    jmp .ok

.ok:
    and word [bp+8], 0xFFFE         ; CF=0 in the RETURNED flags, not the live
    jmp short .leave                ; ones - the iret would discard those
.err:
    or word [bp+8], 1
.leave:
    ; STKBALANCE-OK: dos_int21's arrangement and its reasons (SPEC.md 96.7.1)
    ; - the frame is restored from `bp`, so this gate's promise does not rest
    ; on every handler above being balanced.
    mov si, [bp-2]
    mov di, [bp-4]
    mov es, [bp-6]
    mov sp, bp
    pop ds
    pop bp
    iret

; -----------------------------------------------------------------------------
; dos_pkt_hnew / dos_pkt_hchk - the handle table
;
; **A HANDLE IS THE ROW'S OWN ADDRESS**, which is ours to choose and makes
; every later lookup a bounds check rather than a multiply. hchk is what makes
; that safe: a client handing back a number it made up is refused before it
; can index anything.
; out: hnew  CF=0 with BX = a free row, CF=1 = the table is full
;      hchk  CF=0 = BX is one of ours and in use
; -----------------------------------------------------------------------------
dos_pkt_hnew:
    push cx
    mov bx, dos_pkt_htab
    mov cx, PKT_NHAND
.l:
    cmp byte [bx+PKT_HUSED], 0
    je .got
    add bx, PKT_HSIZE
    loop .l
    pop cx
    stc
    ret
.got:
    pop cx
    clc
    ret

dos_pkt_hchk:
    push ax
    push dx
    mov ax, bx
    sub ax, dos_pkt_htab            ; below the table wraps to a huge unsigned,
    cmp ax, PKT_NHAND * PKT_HSIZE   ; so one compare catches both ends
    jae .no
    xor dx, dx
    mov cx, PKT_HSIZE               ; ...and it must be ON a row boundary, not
    div cx                          ; merely inside the table
    or dx, dx
    jnz .no
    cmp byte [bx+PKT_HUSED], 0
    je .no
    pop dx
    pop ax
    clc
    ret
.no:
    pop dx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dos_pkt_claim / dos_pkt_idle / dos_pkt_shut - the raw claim's lifetime
;
; Taken on the FIRST handle and dropped when the last one goes or the bracket
; ends (SPEC.md 96.23.5). A DOS program that never asks for a packet driver
; leaves our own stack running, which is most of them.
; -----------------------------------------------------------------------------
dos_pkt_claim:
    cmp byte [dos_pkt_raw], 0
    jne .have
    ; --- **THE CABLE HAS NO RAW CLAIM TO TAKE** (SPEC.md 96.26.4) ----------
    ; NETV_RAW is one of the three verbs NET.DRV refuses (72.22.3), so asking
    ; for it here would fail every access_type on the machine this translation
    ; exists for. There is nothing to claim: no ring, no card, and our own
    ; stack is not using a wire the DOS program can collide with.
    ;
    ; It also has to invent the STATION ADDRESS the claim would have handed
    ; back, because get_address must answer something and there is no PROM to
    ; read. Locally-administered unicast again, and one digit off the
    ; gateway's - a client that saw its own address on both ends of a frame
    ; would drop it as a loop.
    cmp byte [dos_pkt_xl], 0
    je .real
    push ax
    push cx
    push si
    push di
    mov si, dn_ourmac_c             ; **THE IMAGE COPY AND NOT THE CLAIM'S**:
    mov di, dos_pkt_mac             ; dn_ourmac is an offset into the network
    mov cx, 6                       ; claim now (SPEC.md 96.26.6) and DS here
    call dn_copy                    ; is ours, so reading it would fetch six
                                    ; bytes of our own bss. The constant is in
                                    ; the image, which is the one place both
                                    ; segments can see
    pop di
    pop si
    pop cx
    mov byte [dos_pkt_raw], 1
    xor ax, ax                      ; ...AND THE BUFFERS ARE PINNED FROM HERE
    call dos_pkt_rloc               ; (96.23.7.2): dos_pkt_tick's stack is in
                                    ; them from this instruction on. BEFORE the
                                    ; `pop ax` and not after: this arm restores
                                    ; the caller's AX on its way out, and a
                                    ; clobber below the pop is a routine that
                                    ; has quietly stopped preserving a register
    pop ax
    clc
    ret
.real:
    push ax
    push bx
    push di
    mov di, dos_pkt_mac             ; the claim hands back the station address
    mov al, 1                       ; (SPEC.md 72.22) - a consumer that is the
    mov bh, DRVC_NET                ; stack now cannot build a frame without it
    mov bl, NETV_RAW
    call OSAPI_DRV_CALL
    jc .no
    mov byte [dos_pkt_raw], 1
    xor ax, ax                      ; ...and the buffers are pinned from here
    call dos_pkt_rloc               ; too - the card route's stack is the same
    pop di                          ; stack (96.23.7.2)
    pop bx
    pop ax
.have:
    clc
    ret
.no:
    pop di
    pop bx
    pop ax
    stc
    ret

dos_pkt_idle:                       ; the LAST handle takes the claim with it
    push bx
    push cx
    mov bx, dos_pkt_htab
    mov cx, PKT_NHAND
.l:
    cmp byte [bx+PKT_HUSED], 0
    jne .busy
    add bx, PKT_HSIZE
    loop .l
    pop cx
    pop bx
    jmp dos_pkt_rawdrop             ; a TAIL JUMP and not a fall-through: the
                                    ; label between them would take the local
                                    ; names below it into its own namespace,
                                    ; which is how `.busy` stopped resolving
.busy:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_pkt_rawdrop - the handles and the raw claim, but NOT the buffers
; dos_pkt_shut    - ...and the buffers too, which only the bracket may do
;
; **THE SPLIT IS THE POINT.** The first version had release_type free the
; buffer claim along with everything else, and that is unrecoverable: §96.3
; has already given the whole heap to the program, so a client that released
; a handle and asked for another got its access_type refused for ever. The
; buffers belong to the BRACKET's lifetime and the raw claim to the handles'.
; -----------------------------------------------------------------------------
dos_pkt_rawdrop:
    push ax
    push bx
    push cx
    push di
    mov cx, PKT_NHAND               ; every handle goes, whichever path came
    mov bx, dos_pkt_htab            ; here: terminate is defined to end them
.z:
    mov byte [bx+PKT_HUSED], 0
    add bx, PKT_HSIZE
    loop .z
    cmp byte [dos_pkt_raw], 0
    je .out
    cmp byte [dos_pkt_xl], 0        ; nothing was claimed on the translation
    jne .letgo                      ; path, so there is nothing to give back
    xor di, di                      ; no MAC wanted on the way out
    xor al, al                      ; release
    mov bh, DRVC_NET
    mov bl, NETV_RAW
    call OSAPI_DRV_CALL
.letgo:
    mov byte [dos_pkt_raw], 0
    mov ax, dos_pkt_reloc           ; ...and nothing is standing on the private
    call dos_pkt_rloc               ; stack any more, so the block may move
                                    ; again (96.23.7.2)
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

dos_pkt_shut:
    call dos_pkt_rawdrop
    cmp word [dos_pkt_bseg], 0      ; ...and NOW the buffers. The kernel frees
    je .nobuf                       ; a dead instance's claims anyway
    push dx                         ; (os88api.inc), so this is only about
    mov dx, [dos_pkt_bseg]          ; handing memory back MID-SESSION - which
    call OSAPI_MEM_FREE             ; is exactly what a DOS window that stays
    mov word [dos_pkt_bseg], 0      ; open after a run is
    pop dx
.nobuf:
    ret

; -----------------------------------------------------------------------------
; dos_pkt_poll - drain the ring, up-calling the client once per frame
;
; **BP IS NOT TOUCHED**: this is called from the gate, where BP is the frame
; pointer every exit path restores the client's registers through.
;
; The budget is what stops a busy segment of network from holding a tick for
; as long as it likes, and it is the ring's own depth rather than a guess: the
; NE2000 holds about ten frames, so a drain of ten empties whatever was there
; and an eleventh would be a frame that arrived while we were working.
; -----------------------------------------------------------------------------
PKT_BUDGET  equ 10

dos_pkt_poll:
    cmp byte [dos_pkt_raw], 0
    je .out                         ; no claim, no frames - and this is the
                                    ; common case: most DOS programs are not
                                    ; network programs
    cmp byte [dos_pkt_busy], 0
    jne .out                        ; **THE TICK CAN LAND INSIDE A CALL** and
                                    ; the client's receiver is not re-entrant
                                    ; merely because ours is
    cmp word [dos_pkt_bseg], 0      ; **AND THE CLAIM GUARD IS BOTH ROUTES'**
    je .out                         ; now (SPEC.md 96.23.7): it was the card's
                                    ; alone while the translation kept its
                                    ; frame in bss, and the symptom of getting
                                    ; that wrong was an ARP that vanished -
                                    ; dn_tx had swallowed the request and
                                    ; built the reply correctly, and this test
                                    ; stopped the poll before anything came to
                                    ; collect it
    mov byte [dos_pkt_busy], 1
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov dx, PKT_BUDGET
.f:
    push dx                         ; the budget: DX is the segment argument now
    cmp byte [dos_pkt_xl], 0
    jne .xl
    mov di, PKT_RXOFF
    mov cx, NET_FRAME
    mov dx, [dos_pkt_bseg]
    mov bh, DRVC_NET
    mov bl, NETV_RAWRX
    call OSAPI_DRV_CALL
    pop dx
    jc .done
    jmp short .got
.xl:
    call dn_ready                   ; ...and it is ALREADY at PKB_RX, because
    pop dx                          ; the translation BUILT it there - which
    jc .done                        ; is the one place the two routes differ
                                    ; and the only reason this branch is left
.got:                        ; the ring is empty
    cmp cx, NET_FRAME                ; the TRUE length may exceed what we asked
    ja .next                        ; for (SPEC.md 72.22.2), and a cut frame
                                    ; handed to a stack is worse than none
    cmp cx, NET_EHSIZE
    jbe .next
    push dx                         ; **THE BUDGET GOES ON THE STACK ACROSS
    call dos_pkt_deliver            ; THE UP-CALL**, because DX is not ours
    pop dx                          ; over it: dos_pkt_deliver far-calls the
                                    ; CLIENT's receiver twice, and a client
                                    ; owes us no register at all. With the
                                    ; count in DX a receiver that used it
                                    ; turned a ten-frame drain into up to
                                    ; 65,535 of them, inside a tick handler -
                                    ; the same livelock dn_pump's CL counter
                                    ; had, one layer out
.next:
    dec dx
    jnz .f
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov byte [dos_pkt_busy], 0
.out:
    ret

; -----------------------------------------------------------------------------
; dos_pkt_deliver - one frame in the claim, CX bytes, to whoever registered
;
; **THE UP-CALL IS TWO CALLS** and that is the Crynwr contract rather than a
; choice (SPEC.md 96.23.4): AX=0 asks the client for somewhere to put CX
; bytes, and AX=1 hands the same buffer back full. A client that answers 0:0
; has refused it, and the frame is gone - it is off the ring already and there
; is nowhere to put it back.
; -----------------------------------------------------------------------------
dos_pkt_deliver:
    push es                         ; **ONE SOURCE ON BOTH ROUTES NOW** - the
    mov es, [dos_pkt_bseg]          ; card writes the claim through NETV_RAWRX
    mov ax, [es:PKT_RXOFF+PKT_ETYPE] ; and the translation BUILDS in it
    pop es                          ; (SPEC.md 96.23.7), so the two arms this
                                    ; routine had, and the [dos_pkt_xl] test
                                    ; that chose between them, are gone
    xchg al, ah                     ; the wire is big-endian and we are not
    mov bx, dos_pkt_htab
    mov si, PKT_NHAND
.l:
    cmp byte [bx+PKT_HUSED], 0
    je .next
    cmp word [bx+PKT_HTYPE], 0
    je .hit                         ; 0 = every frame, whatever its type
    cmp [bx+PKT_HTYPE], ax
    je .hit
.next:
    add bx, PKT_HSIZE
    dec si
    jnz .l
                                    ; nobody registered for it, so it is
                                    ; dropped - which is what a packet driver
                                    ; does and not an error of ours, and
                                    ; get_statistics has a FIELD for saying so
.dropped:
    add word [dos_pkt_stats+PKS_DROP], 1
    adc word [dos_pkt_stats+PKS_DROP+2], 0
    ret
.hit:
    mov ax, [bx+PKT_HRCVO]
    mov [dos_pkt_cvec], ax
    mov ax, [bx+PKT_HRCVS]
    mov [dos_pkt_cvec+2], ax
    mov [dos_pkt_chand], bx         ; the handle, which both calls carry

    push cx                         ; --- call one: where shall I put it? ---
    xor ax, ax
    push ds
    push bp                         ; the CLIENT is about to run: it owes us
    call far [dos_pkt_cvec]         ; nothing, and BP is the gate's frame
    pop bp
    pop ds
    pop cx
    mov ax, es
    or ax, di
    jz .dropped                     ; 0:0 - refused, and the frame is gone -
                                    ; it is off the ring already and there is
                                    ; nowhere to put it back, so it is a DROP
                                    ; and get_statistics counts it as one

    push cx                         ; --- the copy, the claim to theirs ---
    push di
    push es
    push ds
    mov si, PKT_RXOFF               ; DS:SI is the claim and ES:DI the buffer
    mov ds, [dos_pkt_bseg]          ; the client just gave us
    call dos_pkt_copy
    pop ds
    pop es
    pop di
    pop cx

    mov bx, [dos_pkt_chand]         ; --- call two: here it is ---
    mov si, di                      ; DS:SI is the buffer THEY chose, which is
    mov ax, 1                       ; what the contract hands back
    push ds
    push bp
    push es
    pop ds                          ; **DS IS THE CLIENT'S FROM HERE TO THE
    call far [cs:dos_pkt_cvec]      ; POP**, so the vector is only reachable
    pop bp                          ; with an override - and reading it from
    pop ds                          ; the client's segment would be a wild
                                    ; far call into the program's own data
    add word [dos_pkt_stats+PKS_PIN], 1
    adc word [dos_pkt_stats+PKS_PIN+2], 0
    add word [dos_pkt_stats+PKS_BIN], cx
    adc word [dos_pkt_stats+PKS_BIN+2], 0
.out:
    ret

; -----------------------------------------------------------------------------
; dos_pkt_copy - DS:SI -> ES:DI, CX bytes
;
; Written out rather than `rep movsb` for the reason every package here has
; one of these: a package's ES is the kernel's on entry to a callback and its
; own only where it has just set it, so the string instructions are the one
; family whose implicit segment is worth not relying on.
; -----------------------------------------------------------------------------
dos_pkt_copy:
    push ax
    push cx
    push si
    push di
    jcxz .out
.l:
    mov al, [si]
    mov [es:di], al
    inc si
    inc di
    loop .l
.out:
    pop di
    pop si
    pop cx
    pop ax
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
    mov word [dos_pkt_bseg], 0
    mov byte [dos_pkt_xl], 0
    call dos_pkt_want               ; CX = the KB, AL = the translation flag -
    jc .out                         ; ONE reader for the size and the route, so
    mov [dos_pkt_xl], al            ; the figure on the Memory page and the
                                    ; claim taken here cannot disagree
                                    ; (SPEC.md 96.23.7.1, 47 rule 5)
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
    ; the whole of why the proc is two instructions.
    mov ax, dos_pkt_reloc
    call dos_pkt_rloc
.out:
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
    mov cx, PKB_CARDKB              ; ...and the two want DIFFERENT amounts
    xor al, al
    pop bx
    clc
    ret
%endif
.xlate:
    mov cx, PKB_XLKB                ; the translation wants the staging frame
    mov al, 1                       ; and its own state as well
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

; -----------------------------------------------------------------------------
; dos_pkt_start - publish the interface, if there is a card to publish it over
;
; **NOT PUBLISHED ON A MACHINE WITH NO NIC**, and that is SPEC.md 96.15.1's
; argument for the third time in this package: a signature a client can find,
; over a card that is not there, sends it to open a handle that cannot work
; and it has no way to ask why. Its absence sends it to its own "no packet
; driver" path, which every mTCP application has and which says so.
;
; The CARD by name and not net_find: the cable answers the socket verbs too
; and refuses every raw one (SPEC.md 72.22.3), so preferring one of two is
; the wrong question here.
; -----------------------------------------------------------------------------
dos_pkt_start:
    push bx
    mov byte [dos_pkt_vec], 0
    mov byte [dos_pkt_raw], 0
    mov byte [dos_pkt_busy], 0
    mov word [dos_pkt_can], 0x5A5A  ; the canary, armed
    mov byte [dos_pkt_mode], 3      ; what the card is in, and what get_rcv_mode
                                    ; answers until somebody sets it
    cmp byte [dos_pkt_xl], 0        ; the translation needs no buffers...
    jne .go
    cmp word [dos_pkt_bseg], 0      ; ...and the card path's dos_pkt_bufs
    je .none                        ; already asked whether there is a card
                                    ; AND got the buffers for it, so that is
                                    ; both of its questions in one compare
.go:
    call dos_pkt_install
    cmp byte [dos_pkt_vec], 0
    je .none
    call dos_pkt_hook08
.none:
    pop bx
    ret

; -----------------------------------------------------------------------------
; dos_pkt_install - find a free vector and put ourselves on it (SPEC.md 96.23.2)
; out: [dos_pkt_vec] = the vector taken, or 0 if every one was occupied
;
; **SEARCHED RATHER THAN CHOSEN**, which costs four instructions and buys the
; case that actually happens: a program the user ran earlier left something at
; 60h, or the .COM being run is itself a packet driver for a card we have not
; got. A vector whose handler already answers to `PKT DRVR` is somebody's.
; -----------------------------------------------------------------------------
dos_pkt_install:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov byte [dos_pkt_vec], 0
    mov bl, PKT_VEC_LO
.v:
    mov bh, 0
    mov ax, bx
    shl ax, 1
    shl ax, 1                       ; the vector's slot: v * 4
    mov si, ax
    xor ax, ax
    mov es, ax
    mov ax, [es:si+2]               ; its segment...
    or ax, [es:si]                  ; ...and offset: a NULL vector is free
    jz .take
    call dos_pkt_issig              ; ...and so is one nobody signed
    jc .take
.next:
    inc bl
    cmp bl, PKT_VEC_HI
    jbe .v
    jmp short .out                  ; every one taken: [dos_pkt_vec] stays 0
                                    ; and dos_run reports it
.take:
    cli
    mov word [es:si], dos_pkt_entry
    mov [es:si+2], cs
    sti
    mov [dos_pkt_vec], bl
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; --- dos_pkt_issig - does the handler at ES:SI carry `PKT DRVR` at offset 3? --
; out: CF=1 = it does NOT (the vector is free to take)
dos_pkt_issig:
    push ax
    push bx
    push cx
    push si
    push di
    push ds
    push es
    mov ax, [es:si+2]
    mov bx, [es:si]
    mov ds, ax
    mov si, bx
    add si, 3
    push cs
    pop es
    mov di, dos_pkt_sig
    mov cx, 8
.c:
    mov al, [si]
    cmp al, [es:di]
    jne .free
    inc si
    inc di
    loop .c
    pop es                          ; every byte matched: somebody else's
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.free:
    pop es
    pop ds
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; dos_pkt_hook08 - chain INT 08h for the life of the bracket (SPEC.md 96.23.4)
;
; This is the poll that makes asynchronous delivery real: a client that calls
; us once and then waits still receives. The unhook is dos_restore_machine's,
; which puts the WHOLE IVT back.
; -----------------------------------------------------------------------------
dos_pkt_hook08:
    push ax
    push es
    xor ax, ax
    mov es, ax
    cli
    mov ax, [es:0x08*4]
    mov [dos_pkt_old08], ax
    mov ax, [es:0x08*4+2]
    mov [dos_pkt_old08+2], ax
    mov word [es:0x08*4], dos_pkt_tick
    mov [es:0x08*4+2], cs
    sti
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; dos_pkt_tick - IRQ0, chained, with the poll on a stack of our own
;
; **THE CHAIN GOES FIRST**, so the tick reaches the kernel at the depth it
; always did and on the stack it always did - the scheduler saves SP per task
; and a private one under it is a thing nothing here has tested.
;
; **THEN THE STACK SWAPS** (SPEC.md 96.23.4.1). The poll's deepest chain is
; OSAPI_DRV_CALL into the kernel, into the driver, into ne_rx's byte-at-a-time
; DMA loop and out into the client's receiver, and without this it would land
; on whatever stack the DOS program was running on at whatever depth it had
; reached - a .COM's default being 256 bytes under its own image. `mov
; [cs:x], sp` and `mov sp, imm16` need no register, which is what makes the
; swap possible at a gate where every register is the program's.
; -----------------------------------------------------------------------------
dos_pkt_tick:
    pushf                           ; the chain, exactly as an `int` would
    call far [cs:dos_pkt_old08]     ; have entered it - and through CS, since
                                    ; DS is the interrupted program's
    push ax
    push ds
    push cs
    pop ds
    cmp byte [dos_pkt_raw], 0       ; nothing claimed: not even the swap
    je .out
    cmp byte [dos_pkt_busy], 0      ; already inside a poll somewhere below
    jne .out
    mov [dos_pkt_sss], ss
    mov [dos_pkt_ssp], sp
    mov ax, [dos_pkt_bseg]          ; **THE STACK IS IN THE NETWORK CLAIM**
                                    ; (SPEC.md 96.23.7), not in our bss - and
                                    ; [dos_pkt_raw] above is what proves there
                                    ; is one: a claim that was refused
                                    ; publishes no interface, so nothing can
                                    ; have taken a handle
    cli
    mov ss, ax                      ; **SS AND SP IN CONSECUTIVE INSTRUCTIONS**
    mov sp, dos_pkt_stk_top         ; - an 8086 masks interrupts for one
    sti                             ; instruction after a `mov ss`, which is
                                    ; exactly this pair and is why the `cli`
                                    ; is belt and braces rather than the rule
    call dos_pkt_poll
    cli
    mov ss, [dos_pkt_sss]
    mov sp, [dos_pkt_ssp]
    sti
.out:
    pop ds
    pop ax
    iret


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
    ; go when the box is standing on a driver-backed volume unmounts the disk
    ; the program is about to be read off. The box would then refuse its own
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                              ; KD_BACKEND
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

; -----------------------------------------------------------------------------
; dos_blaster_set - "BLASTER=A220 I5 D1 T4" from the record at SI
; in:  SI = a DQ record whose class is DRVC_SOUND
; out: nothing; [dos_blaster] written
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
    cmp al, 0xFF                    ; driver defers discovery to first use
    je .noirq                       ; (SPEC.md 34.5), so a machine that has
    push ax                         ; not played a sound yet genuinely does
    mov al, 'I'                     ; not know - and a BLASTER= naming the
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
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

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
%endif                              ; DOS_EXTCORE
%ifndef DOS_EXTCORE                ; THE CORE (SPEC.md 96.44)

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
%endif                              ; DOS_EXTCORE
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
; dos_fh_touch - make the zero-length file the record at SI names
; in:  SI = the record; out: CF=1 with AL = a DOS error code
; -----------------------------------------------------------------------------
dos_fh_touch:
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
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
; because a DOS box growing the resident kernel a couple of hundred bytes at a
; time is how a machine that boots on 128KB stops doing so. So the prefix is
; copied out under a temporary name and the two are swapped, which is what a
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
    mov byte [dos_wown], 0xFF
    mov byte [dos_wfill], 0
    mov word [dos_wlen], 0

    mov al, [si+FH_VOL]             ; THE BYTES GO WHERE THE FILE IS (96.6.2),
    call dos_vol_to                 ; and one switch covers the whole rewrite
    jc .terr                        ; where dos_fh_fill brackets each call
    push ax
    call .body
    pop ax
    pushf                           ; ...and the walk home happens whatever the
    call dos_vol_to                 ; body did, or the program is left standing
    popf                            ; somewhere it never asked to be
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
    jnz .b1
    jmp dos_fh_touch                ; nothing kept: the zero-length replace
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
    ja .ihole
    jb .ishort
    mov ax, [si+FH_POS]
    cmp ax, [si+FH_SIZE]
    ja .ihole
    je .ichunk
.ishort:                            ; BEFORE the end, where a count of zero is
    or bx, bx                       ; "the file ends HERE" (96.11.6.2) and
    jnz .ichunk                     ; anything else is an ordinary overwrite
    call dos_fh_shrink
    jc .ierr
    jmp .idone                      ; DX is still 0, and 0 is what DOS answers
.ichunk:
    or bx, bx
    jz .idone
    call dos_fh_fill                ; AX = the offset into the window, CX =
    jc .ierr                        ; the bytes of the file that live there
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
    mov ax, [dos_cbytes]            ; ...otherwise the rest of this cluster,
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
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
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
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
; the box's other literals: those are in the WINDOW half (`%ifndef
; KD_BACKEND`), which the core build gates out - so a table put beside them
; assembles for the box and leaves kern_dos with an undefined symbol.
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
; drive is naming ours" - which was true of a box that had one volume and
; stopped being true the day AH=0Eh really switched (SPEC.md 96.6.1).  What it
; cost is in tests/dostrap/drvname.asm: standing on B:, this box answered
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
    add si, 2                       ; range and dos_fh_enter refuses it with the
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
    add si, 2
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
    mov cx, 13                      ; ...AND CX IS RE-ARMED, because the split
                                    ; spends it scanning: it is the 8.3 bound
                                    ; the loop below counts on, and leaving the
                                    ; scan's leftover there truncated or ran
                                    ; past every name in the box
                                    ; (SPEC.md 96.12.3). It used to be refused
                                    ; with code 3, which is what stopped a
                                    ; program opening `B:\PRINCE\PRINCE.DAT` -
                                    ; a path it built itself out of AH=47h's
                                    ; own answer, and the shape every Microsoft
                                    ; C program uses
.copy2:
    lodsb
    cmp al, '\'                     ; ...so anything left here is a separator
    je .path                        ; dos_fh_split could not remove, which
    cmp al, '/'                     ; means the folder part did not fit
    je .path
    cmp al, 'a'
    jb .store
    cmp al, 'z'
    ja .store
    sub al, 32                      ; 8.3 names are upper case on the disk
.store:
    stosb
    or al, al
    jz .done
    loop .copy2
    mov al, 3                       ; longer than 8.3 can be: "path not found"
    jmp short .bad
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
    call dos_be_goto
    jc .no
.ok:
    pop dx
    pop bx
    pop ax
    clc
    ret
.no:
    pop dx
    pop bx
    pop ax
    stc
    ret
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
    mov al, [si+FH_VOL]             ; THE BYTES GO WHERE THE FILE IS, not where
    call dos_vol_to                 ; the program is standing: a copy off B:
    jc .err                         ; onto C: writes with B: current every
    mov [dos_wvsv], al              ; other window (SPEC.md 96.6.2)
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
    sub si, FH_NAME
    or byte [si+FH_FLAGS], FHF_MADE
    jmp short .home
.append:
    call dos_be_append
    jc .errv
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
.home:
    mov al, [dos_wvsv]              ; ...and back, before anything else can
    call dos_vol_park               ; run - the PROGRAM's drive only, the
.clean:                             ; machine staying where the write went
                                    ; (SPEC.md 96.48)
    mov byte [dos_wdirty], 0
    mov word [dos_wlen], 0
.ok:
    clc
    jmp short .out
.errv:
    mov al, [dos_wvsv]              ; a failed write still comes home, or the
    call dos_vol_park               ; program is left standing somewhere it
.err:                               ; never asked to be
    mov byte [dos_wdirty], 0        ; do not retry it for ever - one write that
    mov word [dos_wlen], 0          ; will not go is reported once
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    mov al, 5                       ; "access denied", which is what DOS
    stc                             ; answers for a write that will not go
    ret
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
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
    push es

    call dos_fh_take
    jc .errp

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

.refill:
    cmp byte [dos_wdirty], 0        ; **A DIRTY WINDOW GOES OUT FIRST** (SPEC.md
    je .rfclean                     ; 96.11.6): for a READ the window is a view
    call dos_fh_flush               ; and never dirty, but an in-place write
    jc .errp                        ; makes it an accumulator for its own file -
.rfclean:                           ; and a write that spans two windows would
                                    ; otherwise have the second refill discard
                                    ; the first one's bytes
    mov al, [si+FH_VOL]             ; THE VOLUME FIRST, because AX becomes the
    mov [dos_fvvol], al             ; file OFFSET four lines down and AL is its
                                    ; low byte. Reading it later cost a whole
                                    ; round: FH_VOL is 0 for A:, so the arm
                                    ; under test read correctly and every
                                    ; ordinary read on B: came back EMPTY
    test byte [si+FH_FLAGS], FHF_WHOLE
    jnz .whole                      ; the window IS the file on that arm, and
                                    ; it is re-read rather than kept because
                                    ; another handle may have taken it
    mov ax, [si+FH_POS]             ; the cluster-aligned base under the
    mov dx, [si+FH_POS+2]           ; position. The cluster is a power of two,
    mov bx, [dos_cbytes]            ; so the mask is its own negation
    neg bx
    and ax, bx
    mov [dos_wbase], ax
    mov [dos_wbase+2], dx

    push si
    mov bx, [dos_wseg]              ; THE BYTES COME FROM WHERE THE FILE IS,
    mov es, bx                      ; not from where the program is standing
    xor bx, bx                      ; (SPEC.md 96.6.2)
    mov cx, [dos_wbytes]
    add si, FH_NAME
    mov word [dos_fvtgt], DBE_RDAT  ; AN ORDINAL, not an address (96.44.1):
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
    pop dx
    pop bx
    ret

; --- .onvol - dos_be_go, standing where the WINDOW OWNER's file is ----------
; [dos_fvtgt] is the back end's target and [dos_fvvol] is the volume.  It is
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
    push ax
    push dx
    mov al, [dos_fvvol]
    call dos_vol_to
    jc .onbad
    mov [dos_fvsv], al
    pop dx
    pop ax
    push ax                         ; ...and only now, with every mount the
    mov ax, [dos_fvtgt]             ; switch needed already made. BP IS NOT A
    mov [dos_betgt], ax             ; SCRATCH REGISTER HERE - it is the INT 21h
    pop ax                          ; frame, and [bp] is the program's own DS
    call dos_be_go
    pushf                           ; the back end's answer is DX:AX and CF,
    push ax                         ; and the walk home must not spend any of
    push dx                         ; them
    mov al, [dos_fvsv]
    call dos_vol_park               ; **PARK AND NOT GO** (SPEC.md 96.48): the
                                    ; program's drive comes back and the
                                    ; machine stays on the file's, so a read
                                    ; loop over one file mounts once rather
                                    ; than twice a call
    pop dx
    pop ax
    popf
    ret
.onbad:
    pop dx                          ; THE VOLUME IS GONE - the floppy came out
    pop ax                          ; between the open and the read.  The
    stc                             ; caller reads this as end of file, which
    ret                             ; is the honest half of it: no bytes, and
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
    DBSS DOS_B_CBYTES,2        ; is a multiple of the volume's cluster
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
    DBSS DOS_B_BETGT, 2        ; the back end's own three words
    DBSS DOS_B_BEFLG, 2
    DBSS DOS_B_ONPRG, 1
    DBSS DOS_B_ONPAD, 1
    DBSS DOS_B_FHIX,  1        ; the window owner's index, set by every
    DBSS DOS_B_FHPAD, 1        ; slot resolution rather than threaded
    DBSS DOS_B_FHTAB, FH_SIZEOF * DOS_NFH
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
    DBSS DOS_B_PFBUF, DOS_PFIN + 1  ; AH=29h's copy of the program's name...
    DBSS DOS_B_PFCB,  12            ; ...and the twelve bytes it hands back
    DBSS DOS_B_FNAME2, 16      ; AH=56h's SECOND name (SPEC.md 96.31)
    DBSS DOS_B_RNVOL,  1       ; ...the volume it was ASKED on, which is what
    DBSS DOS_B_RNDRV,  1       ; an unqualified name means; the old name's
    DBSS DOS_B_RNABS,  1       ; drive; and whether it carried a separator
    DBSS DOS_B_RNPAD,  1
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
    DBSS DOS_B_FVVOL, 1        ; the window owner's volume, and where the read
    DBSS DOS_B_FVSV,  1        ; came from; the FLUSH has a byte of its own
    DBSS DOS_B_OPMODE, 1       ; AH=3Dh's access mode, banked (96.11.6)
    DBSS DOS_B_WVSV,  1        ; because it runs INSIDE a fill, through
    DBSS DOS_B_WFIL,  1        ; dos_fh_take, and must not spend the fill's
                               ; own. WFIL was that byte's PAD: the gap's
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
%ifndef KD_BACKEND                  ; 96.43: no driver, so no packet driver
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
    HBSS DOS_B_PKTBSEG, 2           ; **THE FRAME BUFFER IS A HEAP CLAIM**
                                    ; (SPEC.md 96.23.7) and this is its segment,
                                    ; 0 = none. They were 3,028 bytes of bss,
                                    ; which every DOS window paid for on every
                                    ; machine - including every machine with no
                                    ; card in it
    HBSS DOS_B_PKTSTAT, 24          ; six dwords, get_statistics' own order
; --- AND NOTHING FOR THE WIRE ITSELF (SPEC.md 96.23.7) -----------------------
; Every frame, the staging copy, the poll's private stack and the whole of the
; cable translation's state are in the NETWORK CLAIM - see PKB_* above. They
; were 4,052 bytes of this table, which every DOS window paid for on every
; machine, including the 128KB one that ships no network driver at all.
    HBSS DOS_B_PKTXL,   1           ; **WHICH PATH, and it is NOT the same
                                    ; question as which CLASS** (SPEC.md
                                    ; 96.26.1): the translation calls sockets
                                    ; on whatever [net_cls] says, so forcing
                                    ; the path on a card machine - which is
                                    ; how it is tested at all - must not
                                    ; forge the class underneath it
%endif                              ; KD_BACKEND
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
    DBSS DOS_B_FHPATH,  1           ; the name carried a folder part...
    DBSS DOS_B_FPBUF,   DOS_PBUF    ; ...which is this
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
%include "dosc.inc"                 ; ...AND THE PROMPT THAT TYPES INTO IT
                                    ; (SPEC.md 96.33): an input, an output and
                                    ; a prompt, and not one verb of its own
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
%include "os88ui.inc"
%if DOS_BTREC_SZ != OS88UI_BT_SIZE
 %error "DOS_BTREC_SZ mirrors OS88UI_BT_SIZE and they have drifted - the HBSS block is laid out before this include, so the size must be written twice; fix the literal"
%endif
%include "os88line.inc"
%endif
; --- THE CONSOLE (SPEC.md 96.33), telnet's screen as a shared include -------
; **ITS BSS GOES LAST**, past this package's own chain, which is the one line
; that keeps the two apart: os88parts.inc already took the FIRST OP_BSS bytes
; off `os88_image_end` (the DBSS table's own note above), so a console based
; there by default would sit on the parts standard and on the head of this
; table - which is exactly the silent overlap SPEC.md 96.29.1 cost a session.
; DOS_BSS_SIZE is an `equ` a few lines above, so there is nothing to keep in
; step by hand.
%define CON_BSS_AT (os88_image_end + DOS_BSS_SIZE)
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
%define CON_TTY                     ; CR, LF, BS, TAB and BEL, because there is
                                    ; no ANSI parser over this one (SPEC.md
                                    ; 96.33.1) - and con_open, since the
                                    ; attribute a zeroed bss leaves is black
                                    ; on black
%include "os88con.inc"
%else
CON_BSS     equ 0                   ; ...and the carrier's own arithmetic still
                                    ; adds it, so it has a value rather than an
                                    ; %ifdef at every site that reads it
%endif                              ; KD_BACKEND
%ifndef KD_BACKEND                  ; the network family, for 96.43's reason
%include "dosnet.inc"               ; THE CABLE TRANSLATION (SPEC.md 96.26) -
                                    ; only reached when the route is the cable
%include "os88sock.inc"             ; net_try - WHICH driver answers (SPEC.md
                                    ; 20.11.1). The packet driver wants the
                                    ; CARD and not the cable, so it asks
                                    ; DRVC_NET by name rather than calling
                                    ; net_find, whose job is to prefer one of
                                    ; two and whose second answer refuses
                                    ; every verb this feature is made of
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
    OS88_BSS DOS_BSS_SIZE + CON_BSS
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
dos_conx equ dos_hbss + DOS_B_CONX    ; the console band (SPEC.md
%endif
%ifndef KD_BACKEND
dos_cony equ dos_hbss + DOS_B_CONY    ; 96.32): top-left in screen
%endif
%ifndef KD_BACKEND
dos_concols equ dos_hbss + DOS_B_CONCOLS ; pixels, size in CELLS, all
%endif
%ifndef KD_BACKEND
dos_conrows equ dos_hbss + DOS_B_CONROWS ; four filled by dos_con_geom
%endif
%ifndef KD_BACKEND
dos_pln equ dos_hbss + DOS_B_PLN     ; the path box (SPEC.md 96.32.1)
%endif
%ifndef KD_BACKEND
dos_path equ dos_hbss + DOS_B_PATH    ; ...and its text
%endif
%ifndef KD_BACKEND
dos_erect equ dos_hbss + DOS_B_ERECT   ; 'Environment' on the bar
%endif
%ifndef KD_BACKEND
dos_rrect equ dos_hbss + DOS_B_RRECT   ; ...and 'Run'
%endif
%ifndef KD_BACKEND
dos_tvol equ dos_hbss + DOS_B_TVOL    ; dos_path_take's scratch pair
%endif
%ifndef KD_BACKEND
dos_tname equ dos_hbss + DOS_B_TNAME
%endif
%ifndef KD_BACKEND
%endif
%ifndef KD_BACKEND
%endif
%ifndef KD_BACKEND
dos_trect equ dos_hbss + DOS_B_TRECT   ; ...and 'Return'
%endif
%ifndef KD_BACKEND
dos_lnv equ dos_hbss + DOS_B_LNV     ; word: LN_VIEW before a key
%endif
%ifndef KD_BACKEND
dos_lnl equ dos_hbss + DOS_B_LNL     ; word: LN_LEN before a key
%endif
%ifndef KD_BACKEND
dos_lnc equ dos_hbss + DOS_B_LNC     ; word: LN_CAR before a key
%endif
%ifndef KD_BACKEND
dos_ncell equ dos_hbss + DOS_B_NCELL   ; word: cells the edits redrew
%endif
%ifndef KD_BACKEND
dos_nkey equ dos_hbss + DOS_B_NKEY    ; word: ...over this many keys
%endif
%ifndef KD_BACKEND
dos_page equ dos_hbss + DOS_B_PAGE    ; byte: DOS_PAGE_*
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
dos_brect equ dos_hbss + DOS_B_BRECT   ; the page button's rect
dos_btrec equ dos_hbss + DOS_B_BTREC   ; ...and the button group's record
%endif
%ifndef KD_BACKEND
dos_srect equ dos_hbss + DOS_B_SRECT   ; ...and Save Shortcut's
%endif
%ifndef KD_BACKEND
dos_abon equ dos_hbss + DOS_B_ABON    ; byte: the About card is up
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
dos_wname equ dos_hbss + DOS_B_WNAME   ; ...the name to write it under
%endif
%ifndef KD_BACKEND
dos_lend equ dos_hbss + DOS_B_LEND    ; word: bytes of dos_lbuf read
%endif
dos_ebuf    equ DOS_CBASE + DOS_B_EBUF    ; the four environment rows
%ifndef KD_BACKEND
dos_eln equ dos_hbss + DOS_B_ELN     ; ...and their os88line blocks
%endif
dos_args    equ DOS_CBASE + DOS_B_ARGS    ; 128: the command tail the user
                                               ; typed, without its count or
                                               ; its 0Dh - both are DOS's
                                               ; framing and go on at the PSP
dos_pbuf    equ DOS_CBASE + DOS_B_PBUF    ; the program's own path
dos_ln equ dos_hbss + DOS_B_LN      ; the field's os88line block
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
dos_mbuf equ dos_hbss + DOS_B_MBUF    ; the limit field's text
%endif
%ifndef KD_BACKEND
dos_mln equ dos_hbss + DOS_B_MLN     ; ...and its os88line block
%endif
%ifndef KD_BACKEND
dos_mx equ dos_hbss + DOS_B_MX      ; word: this paint's content x
%endif
%ifndef KD_BACKEND
dos_my equ dos_hbss + DOS_B_MY      ; word: ...and its top
%endif
%ifndef KD_BACKEND
dos_mpx equ dos_hbss + DOS_B_MSX    ; word: a press's x, banked for one click
%endif
%ifndef KD_BACKEND
dos_mpy equ dos_hbss + DOS_B_MSY    ; word: ...and its y (SPEC.md 96.36.4)
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
dos_m33bsy  equ DOS_CBASE + DOS_B_M33BSY
dos_m33shw  equ DOS_CBASE + DOS_B_M33SHW  ; --- the text cursor (96.10.5) ---
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
dos_cbytes  equ DOS_CBASE + DOS_B_CBYTES
dos_wown    equ DOS_CBASE + DOS_B_WOWN
dos_wfill   equ DOS_CBASE + DOS_B_WFILL
dos_wdirty  equ DOS_CBASE + DOS_B_WDIRTY
dos_wbase   equ DOS_CBASE + DOS_B_WBASE
dos_wlen    equ DOS_CBASE + DOS_B_WLEN
dos_fname   equ DOS_CBASE + DOS_B_FNAME
dos_fent    equ DOS_CBASE + DOS_B_FENT
dos_bk_ss   equ DOS_CBASE + DOS_B_BKSS
dos_bk_sp   equ DOS_CBASE + DOS_B_BKSP
dos_betgt   equ DOS_CBASE + DOS_B_BETGT
dos_beflg   equ DOS_CBASE + DOS_B_BEFLG
dos_onprog  equ DOS_CBASE + DOS_B_ONPRG
dos_fhix    equ DOS_CBASE + DOS_B_FHIX
dos_fhtab   equ DOS_CBASE + DOS_B_FHTAB
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
dos_pfbuf   equ DOS_CBASE + DOS_B_PFBUF   ; AH=29h's name scratch...
dos_pfcb    equ DOS_CBASE + DOS_B_PFCB    ; ...and its FCB prefix
dos_fname2  equ DOS_CBASE + DOS_B_FNAME2  ; AH=56h's second name
dos_rnvol   equ DOS_CBASE + DOS_B_RNVOL   ; ...and its three bytes of
dos_rndrv   equ DOS_CBASE + DOS_B_RNDRV   ; drive arithmetic
dos_rnabs   equ DOS_CBASE + DOS_B_RNABS
dos_fnseg   equ DOS_CBASE + DOS_B_FNSEG   ; word: where a name is read from
dos_fdrv    equ DOS_CBASE + DOS_B_FDRV    ; byte: the drive a name named
dos_pvol    equ DOS_CBASE + DOS_B_PVOL    ; byte: the volume the MACHINE is
dos_pdir    equ DOS_CBASE + DOS_B_PDIR    ; word: ...and the folder in it
dos_fvtgt   equ DOS_CBASE + DOS_B_FVTGT   ; word: the read's back end
dos_fvvol   equ DOS_CBASE + DOS_B_FVVOL   ; byte: the fill's volume...
dos_fvsv    equ DOS_CBASE + DOS_B_FVSV    ; byte: ...and where it came from
dos_opmode  equ DOS_CBASE + DOS_B_OPMODE  ; byte: AH=3Dh's access mode
dos_wvsv    equ DOS_CBASE + DOS_B_WVSV    ; byte: the flush's own
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
%ifndef KD_BACKEND                  ; 96.43
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
dos_pkt_bseg equ dos_hbss + DOS_B_PKTBSEG
dos_pkt_stats equ dos_hbss + DOS_B_PKTSTAT

dos_pkt_xl equ dos_hbss + DOS_B_PKTXL

; --- the cable translation's, IN THE NETWORK CLAIM (SPEC.md 96.26.6) --------
; These are offsets into [dos_pkt_bseg] and NOT into our own segment, which is
; the whole of what made the move cheap: dosnet.inc already addressed all of
; them DS-relative with no override, so pointing DS at the claim leaves its
; seventy-eight frame references untouched and only these fourteen lines move.
dn_flows    equ PKB_STATE + DNB_FLOWS
dn_names    equ PKB_STATE + DNB_NAMES
dn_qname    equ PKB_STATE + DNB_QNAME
dn_pend     equ PKB_STATE + DNB_PEND
dn_lastip   equ PKB_STATE + DNB_LASTIP
dn_pseudo   equ PKB_STATE + DNB_PSEUDO
dn_rr       equ PKB_STATE + DNB_RR
dn_gwmac    equ PKB_STATE + DNB_GWMAC       ; copied out of the image by
dn_ourmac   equ PKB_STATE + DNB_OURMAC      ; dn_init (dn_mac_c below)
dn_lsn      equ PKB_STATE + DNB_LSN         ; the inbound listeners (96.26.8)
dn_lrr      equ PKB_STATE + DNB_LRR
dn_cip      equ PKB_STATE + DNB_CIP         ; ...and the client's own address
dn_frame    equ PKB_RX                      ; the frame we build FOR the
                                            ; client, which is also...
dos_pkt_rxs equ PKB_RX                      ; ...the one dos_pkt_deliver hands
                                            ; over, on EITHER route: the card
                                            ; writes the same offset through
                                            ; NETV_RAWRX, which is why both
                                            ; of that routine's arms collapsed
dos_pkt_txs equ PKB_TX                      ; and the client's own, staged
dos_pkt_stk_top equ PKB_STKTOP
%endif                              ; KD_BACKEND

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
