; =============================================================================
; SOUND.DRV's package verbs - the FM chip, whole (SPEC.md 34.12), the
; MPU-401's MIDI out (SPEC.md 34.13) and the Covox's port (SPEC.md 34.14)
;
; Included by BOTH ends, netpkg.inc's shape: the driver that answers these and
; every package that asks. Two doors, because the two halves need different
; things from the kernel:
;
;   OSAPI_SND_FM verbs 4 and 5 - INFO and CLAIM. They go through the FM slot
;   because THAT slot stamps the requesting instance in DH (SPEC.md 34.3), and
;   a claim is only worth anything if the teardown can find its owner: a
;   package that dies holding the chip has it released by DSV_RELINST, which
;   reads exactly those stamps.
;
;   OSAPI_DRV_CALL, BH = DRVC_SOUND - the WRITES. That door is the one that
;   hands the driver the caller's segment in ES (SPEC.md 20.11), so a batch of
;   register writes is read straight out of the package with nothing staged.
;   It knows nothing about instances, which is why it is gated on a claim
;   existing at all rather than on who holds it: packages are trusted (the
;   owner's ruling, docs/plans/LAST-DROP-BYTES.md 7.11), and what the gate
;   really protects is the TONE voice and the chip's global state, which no
;   claim ever covers.
;
;       mov  al, SND_FM_CLAIM          ; UI callback: the stamp is yours
;       mov  cl, 1                     ; ...OPL3 mode if the chip has it
;       call OSAPI_SND_FM              ; CF=1: somebody else holds a channel
;       ...
;       mov  bh, DRVC_SOUND            ; any task, worker included
;       mov  bl, SNDV_OPLW
;       mov  si, my_pairs              ; ES:SI - ES is YOUR segment here
;       mov  cx, n
;       call OSAPI_DRV_CALL
;       ...
;       mov  al, 3                     ; all-off: every channel AND the chip
;       call OSAPI_SND_FM              ; back, OPL2 mode restored
;
; WORKER SAFETY (SPEC.md 20.6 rule 7): SNDV_IDENT and SNDV_OPLW take no lock,
; raise no sch_lock and touch no disk - every port write is opl_wr's own
; pushf/cli window - so a package's worker may call them. The two FM verbs are
; UI-callback verbs like every other FM verb: the instance stamp is only true
; there (SPEC.md 34.3).
; =============================================================================

; --- OSAPI_DRV_CALL verbs, BH = DRVC_SOUND -----------------------------------
SNDV_IDENT   equ PKGV_IDENT     ; = 0. out AX = SND_SIG, CF=0. ASK IT FIRST
SND_SIG      equ 'SD'           ; ...the signature it answers
SNDV_OPLW    equ 1              ; ES:SI -> CX two-byte entries {register,
                                ; value}, written in order through opl_wr.
                                ; An entry whose REGISTER is 0 is not a write:
                                ; its value selects the BANK the entries after
                                ; it go to (0 = 388h, 1 = 38Ah, OPL3 only). The
                                ; batch starts in bank 0. out CF=0, AX = the
                                ; entries WRITTEN; CF=1 AX = SNDE_NOCLAIM when
                                ; no package holds the chip (SND_FM_CLAIM), or
                                ; SNDE_NOFM with no chip at all. A register
                                ; that is not a claimer's - channel 8's (the
                                ; tone voice), the test, timer and mode
                                ; registers 01h-04h, 104h/105h - is SKIPPED and
                                ; not counted, never refused: a batch is not
                                ; half-applied over one bad byte

SNDV_MIDINFO equ 2              ; THE MPU-401 (SPEC.md 34.13). out CF=0, AX =
                                ; the interface's data port (330h / 300h);
                                ; CF=1 AX = SNDE_NOMIDI, none answered
SNDV_MIDOPEN equ 3              ; the interface is the CALLER'S SEGMENT's (ES):
                                ; CF=1 AX = SNDE_BUSY when another package's.
                                ; Reopening your own is not an error
SNDV_MIDW    equ 4              ; ES:SI -> CX bytes of MIDI, sent in order.
                                ; out CF=0 AX = the bytes sent; CF=1 AX =
                                ; SNDE_NOCLAIM (not yours) or SNDE_MIDTIME (the
                                ; interface stopped taking bytes - AX is then
                                ; NOT a count). WORKER-SAFE: port I/O only
SNDV_MIDCLOSE equ 5             ; every channel's notes off and controllers
                                ; reset, and the interface given back. Closing
                                ; one that is not yours does nothing

SNDV_DACINFO equ 6              ; THE COVOX (SPEC.md 34.14). out CF=0, AX = the
                                ; DATA PORT of the parallel port the user said
                                ; a Covox Speech Thing is on - the first that
                                ; answered attach's latch test, LPT1 first;
                                ; CF=1 AX = SNDE_NODAC when the sound tier is
                                ; not SND_RT_LPT or no port answered. Claims
                                ; nothing: the DAC is played from inside the
                                ; caller's own FSXF_RATE bracket
                                ; (apps/os88spk.inc's os88spk_lpt), and only
                                ; one bracket is ever open. WORKER-SAFE.
                                ; apps/os88spk.inc types this number out a
                                ; second time (its os88spk_lptq asks it with
                                ; no -I drivers/sound/); tests/unit/t_mirror
                                ; holds the two equal

SNDE_NOFM    equ 1
SNDE_NOCLAIM equ 2
SNDE_NOMIDI  equ 3
SNDE_BUSY    equ 4
SNDE_MIDTIME equ 5
SNDE_NODAC   equ 6

; --- OSAPI_SND_FM verbs, after 0..3 (apps/os88api.inc) -----------------------
SND_FM_INFO  equ 4              ; out CF=0, AL = SND_OPL2 / SND_OPL3 (the chip),
                                ; AH = the channels a claim would give (8 or
                                ; 17); CF=1 no FM. Claims nothing
SND_FM_CLAIM equ 5              ; CL = 0 OPL2 mode / 1 OPL3 mode where the chip
                                ; has it. Claims EVERY melodic channel - 0..7,
                                ; and 9..17 on an OPL3 - for the caller, or
                                ; none: CF=1 when any is somebody else's. out
                                ; CF=0, AL = the mode granted (SND_OPL2/3), AH =
                                ; the channels. Verb 3 (all-off) gives it back
                                ; and puts an OPL3 back in OPL2 mode
SND_OPL2     equ 2
SND_OPL3     equ 3
SND_FM_TONECH equ 8             ; the one channel no claim covers (SPEC.md 34.8)
