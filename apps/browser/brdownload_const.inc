; Shared Download Manager constants used by the primary and its lazy painter.
BR_DL_URLMAX equ BR_UBUF
BR_DL_HOSTMAX equ BR_HOSTMAX
BR_DL_PATHMAX equ BR_PATHMAX
; Worst case: GET + 95-byte path + HTTP/1.0 + Host + 63-byte host + current
; User-Agent + Connection headers = 233 bytes. Keep six bytes of growth room.
BR_DL_REQMAX equ 240
BR_DL_LINE equ 128
; br_dl_drawcut emits at most 58 visible bytes plus NUL. Speed and ETA fit too.
BR_DL_DISPTXT equ 59
BR_DL_DEST equ 128
; The URL store's size (SPEC.md 71.23): the Back stack's 8 x 160 URLs and the
; 1,477-byte bookmark file, to a paragraph. A transfer's cluster buffer, and a
; Copy's text after it, are carved from BR_URLDL on: the next 512-byte
; boundary, because a cluster is written to the disk from there and every
; disk-visible base is 512-aligned (the claim's own base is). browser.asm
; checks BR_URLSTORE.
BR_URLSTORE equ 2768
BR_URLDL    equ 3072

DL_IDLE       equ 0
DL_PICK       equ 1
DL_OPEN       equ 2
DL_WAIT       equ 3
DL_SEND       equ 4
DL_HEAD       equ 5
DL_BODY       equ 6
DL_PENDING    equ 7
DL_ENDWAIT    equ 8
DL_CANCELREQ  equ 9
DL_CANCELREADY equ 10
DL_DONE       equ 11
DL_FAIL       equ 12
DL_CANCEL     equ 13

DL_ACTIVE equ DL_OPEN

; Part 0's far entry table (brdownload_part.asm leads with it, a word each) as
; byte offsets into br_dl_vec, whose entries are four bytes (SPEC.md 71.20.5)
BRDL_PAINT    equ 0                 ; W_PAINT: the painter and the button
BRDL_COPY     equ 4                 ; Edit > Copy of a page selection
BRDL_OPEN     equ 8                 ; File > Downloads
BRDL_LINK     equ 12                ; a link to save: the Save dialog
BRDL_WAKE     equ 16                ; W_ONWAKE: a cluster to write, an end
BRDL_CLOSE    equ 20                ; the manager's close negotiator
BRDL_TIMER    equ 24                ; its timer
BRDL_UP       equ 28                ; its button's mouse-up
BRDL_DRAG     equ 32                ; ...and drag
BRDL_SAVED    equ 36                ; the Save dialog's answer
BRDL_MAIN     equ 40                ; the Browser's own close
BRDL_STEP     equ 44                ; the worker's transfer step
BRDL_CTXSHOW  equ 48                ; the rarely used UI (SPEC.md 71.24): the
BRDL_CTXCLICK equ 52                ; context menu, shown and clicked,
BRDL_SRC      equ 56                ; View and Close Source,
BRDL_SUBMIT   equ 60                ; a form's submit,
BRDL_BMCOMMIT equ 64                ; and a bookmark's add or remove
BRDL_N        equ 17
