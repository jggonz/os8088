; MSEG's part 6 - the LAZY module (SPEC.md 20.12.4). Not claimed and not read
; at load: op_size steps over its row, and op_fetch claims and reads it later
; when the package asks. It is deliberately the LARGEST of the five, because
; what a lazy part buys is measured in the sectors the launch did not move.
%define MP_INDEX  6             ; ITS PART INDEX, not its file number
%define MP_DATA_N 3000
%include "msegpart.inc"
