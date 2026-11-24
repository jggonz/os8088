; =============================================================================
; drivers/zip/zipppa.inc - VPI0 - the Zip 100's PPA3 (Linux drivers/scsi/ppa.c), SPEC.md 138.2.1
;
; A STUB until story Z3 replaces this file: the three procs of
; ziptrans.inc, each answering "no drive of this protocol", so the core
; assembles and attaches (and refuses, cleanly) before the transport exists.
; =============================================================================

ppa_probe:                     ; out CF = 1: nothing answered
    stc
    ret

ppa_cmd:                       ; out CF = 1, AL = ZTE_NOCONN
    mov al, ZTE_NOCONN
    stc
    ret

ppa_reset:
    ret
