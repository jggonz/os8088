; MSEG's part 0 (SPEC.md 20.12). Nothing about the module is special: it is
; msegpart.inc like the others, and what differs is its INDEX - which is what
; makes "part 1's bytes at part 0's base" a failure a test can name.
%define MP_INDEX  0
%define MP_DATA_N 1000
%include "msegpart.inc"
