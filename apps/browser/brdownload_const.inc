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
