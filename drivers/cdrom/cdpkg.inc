; =============================================================================
; os8088 - drivers/cdrom/cdpkg.inc - CDROM.DRV's package door (SPEC.md 119.4)
;
; The ABI between CDROM.DRV and a package that plays a disc's audio, included
; by BOTH ends so the two cannot drift (SPEC.md 20.11): the driver and
; apps/cdrack. Reached by OSAPI_DRV_CALL with BH = DRVC_CD and BL = a CDV_*.
;
; UI TASK ONLY, EVERY VERB. Each one talks to the drive and waits on it - a
; seek is up to a second and a spin-up several - so none is worker-safe
; (SPEC.md 20.6 rule 7). Answers are in AX, CX, DX and SI; DI, BP, DS and ES
; come back the caller's (SPEC.md 20.11). A refusal is CF = 1 with AX = a
; CDE_*, and AX = 0 is the KERNEL's refusal - no CD-ROM driver loaded - which
; is why no CDE_* is 0.
; =============================================================================

CD_SIG       equ 'CD'           ; CDV_IDENT's AX: NASM puts the first
                                ; character in the LOW byte
CD_ABI       equ 1              ; ...and CX: this file's version

CDV_IDENT    equ 0              ; out AX = CD_SIG, CX = CD_ABI. Always CF = 0
CDV_DRIVE    equ 1              ; ES:DI = CD_MODELSZ bytes for the drive's
                                ; model (NUL-ended), or DI = 0. out AX = the
                                ; task file's base port, CL = the unit (0
                                ; master, 1 slave), DL = the volume index
                                ; (0FFh = none registered)
CDV_DISC     equ 2              ; check the medium and read its TOC. out AL =
                                ; the first track, AH = the last, CX = the
                                ; disc's length in seconds, DL = CDK_* bits,
                                ; DH = the media generation
CDV_TRACK    equ 3              ; AL = a track. out DX:AX = its start LBA,
                                ; CX = its length in seconds, SI = its control
                                ; nibble (CDT_DATA set = a data track)
CDV_PLAY     equ 4              ; AL = the first track, AH = the last (0 = the
                                ; disc's last audio track), CX = seconds into
                                ; the first. Plays to the end of the last, or
                                ; to the first data track after the first
CDV_PAUSE    equ 5
CDV_RESUME   equ 6
CDV_STOP     equ 7
CDV_STATUS   equ 8              ; out AL = CDS_*, AH = the track, CX = seconds
                                ; into it, DX = seconds into the disc, SI =
                                ; the media generation
CDV_EJECT    equ 9              ; AL = 0 open the tray, 1 close it
CDV_VOLUME   equ 10             ; AL = 0 get / 1 set; CL/CH = left/right,
                                ; 0..255 (set). out CL/CH = what the drive holds
CDV_MAX      equ 10

CDE_NODISC   equ 1              ; no disc in the drive
CDE_NOTREADY equ 2              ; a disc, and the drive never became ready
CDE_IO       equ 3              ; the drive refused, or never answered
CDE_ARG      equ 4              ; a track or a verb out of range
CDE_NOAUDIO  equ 5              ; the drive refused an audio command (QEMU's
                                ; ATAPI drive has none - SPEC.md 119.7)

CDK_AUDIO    equ 1              ; CDV_DISC's DL: the disc has audio tracks...
CDK_DATA     equ 2              ; ...and/or a data track

CDT_DATA     equ 4              ; CDV_TRACK's SI: the control nibble's data bit

CDS_IDLE     equ 0              ; CDV_STATUS's AL: no play in progress
CDS_PLAYING  equ 1
CDS_PAUSED   equ 2
CDS_DONE     equ 3              ; a play ran to its end
CDS_ERROR    equ 4              ; a play stopped on an error

CD_MODELSZ   equ 41             ; IDENTIFY PACKET DEVICE's 40 model characters
                                ; and a NUL
