; =============================================================================
; os8088 - drivers/net/nwire.inc
;
; THE SOCKET COMMANDS' ALPHABET, and it belongs to BOTH ENDS of the cable.
;
; lplink.inc's argument one layer up: the master (netsock.inc, in NET.DRV) and
; the slave (nwslv.inc, in OS88NET.COM) must agree byte for byte about what a
; command looks like, so they read the same file rather than two copies that
; can drift. A letter added here is added to both ends at once.
; =============================================================================

; --- the wire commands (lowercase, see the header) ---------------------------
NW_OPEN     equ 'o'             ; port word, NET_HOSTMAX host -> st, handle
NW_LISTEN   equ 'l'             ; port word                   -> st, handle
NW_ACCEPT   equ 'a'             ; handle byte                 -> st, handle
NW_STAT     equ 's'             ; handle byte      -> st, state byte, ready word
NW_SEND     equ 'w'             ; handle, len word, bytes     -> st, taken word
NW_RECV     equ 'r'             ; handle, cap word    -> st, len word, len bytes
NW_CLOSE    equ 'c'             ; handle byte                 -> st

; The status byte on the wire IS the NETE_* a package gets, which is one fewer
; mapping to keep in step - the far side is written against netpkg.inc like
; everything else here. And EVERY frame is fixed whatever the status says
; (SPEC.md 62.10.1): a refused open still carries its handle byte, so neither
; end is ever left talking into one that has stopped listening.

NET_VER_SOCK equ 2              ; THE VERSION BYTE IS THE PROBE, and it costs
                                ; no wire at all: mst_hello already reads one
                                ; (lplink.inc), so a far side that speaks
                                ; sockets says 2 and one built before them
                                ; says 1. Asking with a command instead would
                                ; mean sending a letter an old far side has
                                ; never heard of, into a command loop whose
                                ; behaviour for an unknown letter is not part
                                ; of any contract - which is how a probe
                                ; breaks the link it is probing
