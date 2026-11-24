; =============================================================================
; drivers/zip/zipimm.inc - VPI2 - the Zip Plus/250's MatchMaker (Linux drivers/scsi/imm.c), SPEC.md 138.2.2
;
; A STUB until story Z4 replaces this file: the three procs of
; ziptrans.inc, each answering "no drive of this protocol", so the core
; assembles and attaches (and refuses, cleanly) before the transport exists.
; =============================================================================

imm_probe:                     ; out CF = 1: nothing answered
    stc
    ret

imm_cmd:                       ; out CF = 1, AL = ZTE_NOCONN
    mov al, ZTE_NOCONN
    stc
    ret

imm_reset:
    ret
