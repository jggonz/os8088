; =============================================================================
; drivers/zip/ziptrans.inc - the TRANSPORT CONTRACT of ZIP.DRV (SPEC.md 138.2)
;
; ZIP.DRV speaks SCSI to an Iomega Zip drive through one of two parallel-port
; protocols, each in its own file:
;
;   zipppa.inc   VPI0, the Zip 100's PPA3 interface   (Linux drivers/scsi/ppa.c)
;   zipimm.inc   VPI2, the Zip Plus/250's MatchMaker  (Linux drivers/scsi/imm.c)
;
; Both export the SAME three near procs under their own prefix (ppa_ / imm_),
; and the core (zip.asm) calls whichever one probed through the three words
; of zt_vec. Neither transport knows SCSI beyond "send these CDB bytes, move
; these data bytes, read the status byte"; neither knows a volume exists.
;
; Every proc runs with DS = the driver's segment, interrupts as the caller
; left them (ON - never cli across a transfer), CLD set, 8086 only, and
; PRESERVES EVERY REGISTER BUT ITS DOCUMENTED OUTPUTS.
;
; ---------------------------------------------------------------------------
; <pfx>_probe   is there a drive of MY protocol on [zt_base]?
;   in   [zt_base]  = the port's base (378h, 278h or 3BCh)
;        [zt_mode]  = ZIPM_PS2 if the core proved the port bidirectional,
;                     else ZIPM_NIBBLE
;   out  CF = 0: found. [zt_id] = its SCSI target ID (0..7, normally 6),
;                [zt_mode] = the mode it WILL use (it may lower PS2 to
;                NIBBLE if a PS/2 test command fails, never raise it)
;        CF = 1: no drive of this protocol answered
;   The port is left DISCONNECTED (printer-idle): a printer daisy-chained
;   through the drive must still print after a failed probe.
;
; <pfx>_cmd     run ONE SCSI command, start to finish
;   in   SI     -> the CDB, in DS (6, 10 or 12 bytes)
;        BL     = the CDB's length
;        BH     = ZTD_IN (data comes from the drive) or ZTD_OUT (to it)
;        ES:DI  -> the data buffer (ignored when CX = 0)
;        CX     = data bytes expected, 0..65,024 (a multiple of 512 for
;                 READ(10)/WRITE(10); any count for INQUIRY, SENSE, ...)
;   out  CF = 0: AL = the SCSI STATUS byte (00h GOOD, 02h CHECK CONDITION,
;                08h BUSY), CX = the data bytes actually moved
;        CF = 1: AL = a ZTE_* transport error; the bus was released
;        AH is undefined on both paths; every other register is preserved
;   connect, select, command, data, status, message, disconnect - all of it.
;   A drive that ends the data phase early is not an error: it is a status
;   byte, and CX says how much was moved. A drive that wants MORE data than
;   CX allows is ZTE_PHASE (the buffer is never overrun).
;
; <pfx>_reset   the SCSI bus reset pulse (Linux ppa_reset / imm_reset)
;   in   nothing; out nothing (CF undefined). Leaves the port disconnected.
;
; TIMEOUTS ARE TIME, NOT ITERATIONS (CLAUDE.md Performance rule 3). A wait
; of more than about 1 ms reads the BIOS tick (0040:006Ch, 54.9 ms) and gives
; up after ZT_TMO_TICKS ticks; a spin bounded by a count alone is 100 times
; shorter on a 486 than on the 4.77 MHz XT it was sized on.
; =============================================================================

; --- the port's three registers, as offsets from [zt_base] -------------------
ZT_DATA     equ 0               ; data latch (input too, on a bidirectional
                                ; port with ZT_CTR_DIR set)
ZT_STAT     equ 1               ; status: bit 7 = /BUSY (inverted), 6 = ACK,
                                ; 5 = PAPER OUT, 4 = SELECT, 3 = /ERROR
ZT_CTRL     equ 2               ; control: bit 0 = /STROBE, 1 = /AUTOFEED,
                                ; 2 = INIT, 3 = /SELECT IN (each of 0,1,3
                                ; inverted on the wire), 5 = DIRECTION
ZT_CTR_DIR  equ 0x20            ; PS/2 bidirectional: data lines tri-stated

; --- direction of a command's data phase -------------------------------------
ZTD_IN      equ 0
ZTD_OUT     equ 1

; --- transport errors (CF = 1) -----------------------------------------------
ZTE_NOCONN  equ 1               ; the connect handshake was not answered
ZTE_NOSEL   equ 2               ; select timed out: no target at [zt_id]
ZTE_TIMEOUT equ 3               ; the drive stopped answering mid-command
ZTE_PHASE   equ 4               ; the drive asked for a phase that does not
                                ; fit the command (data in for a write, ...)

ZT_TMO_TICKS equ 19             ; a wait gives up after ~1 second

; --- the state both transports and the core share (in the driver's bss) -------
; The core DECLARES these (zip.asm); a transport only reads/writes them.
;   zt_base  dw   the port base
;   zt_mode  db   ZIPM_NIBBLE / ZIPM_PS2 (apps/os88api.inc)
;   zt_id    db   the drive's SCSI target ID
;   zt_vec   dw 3 the probed transport's _probe, _cmd, _reset
ZTV_PROBE   equ 0
ZTV_CMD     equ 2
ZTV_RESET   equ 4
