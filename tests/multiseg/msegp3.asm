; MSEG's part 5 - the XMS-accepting ASSET (SPEC.md 20.12.4). Small enough to
; be brought back down into MSEG's 8KB scratch part in one OSAPI_XMEM_COPY,
; because that round trip is the only way to check an asset the CPU cannot
; address: the primary sums what comes back against the figure the ASSEMBLER
; computed here.
%define MP_INDEX  5             ; ITS PART INDEX, not its file number
%define MP_DATA_N 1000
%include "msegpart.inc"
