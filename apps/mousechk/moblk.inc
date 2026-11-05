; =============================================================================
; os8088 - apps/mousechk/moblk.inc - the 'MO' block's layout (SPEC.md 126.4)
;
; What kernel/mouse.inc publishes under the debug registry's 'MO' tag
; (SPEC.md 9.4.2-9.4.4), as offsets. A fast-tier host check
; (tests/unit/t_mousechk.py) holds every number here to the kernel's own
; layout, so a kernel change to the block fails `make` until this file, and
; MOUSECHK with it, are changed in the same commit (SPEC.md 57.3, 126.4).
;
; Reading any of it is only meaningful while [mc_kok] says the kernel is the
; build this package was made with (SPEC.md 126.4): otherwise show raw hex.
; =============================================================================

; --- the descriptor: mou_dbg_blk, words at these offsets from the block ------
MOB_TAG     equ 0       ; 'MO'
MOB_BASES   equ 2       ; -> mou_bases: two words, a UART base each, 0 = none
MOB_SPAN    equ 4       ; -> the 34-byte span below (mou_run first)
MOB_CUR     equ 6       ; -> mouse_x (word), mouse_y (word), mouse_btn (byte)
MOB_P2      equ 8       ; -> the 7-byte PS/2 span

; --- the 34-byte span (SPEC.md 9.4.2), offsets from the pointer at MOB_SPAN --
MOS_RUN     equ 0       ; 4 bytes: clean packets in a row, per port (stride 2)
MOS_PORT    equ 4       ; the mouse's row: 0 COM1, 2 COM2, 4 = PS/2 won
MOS_NEED    equ 5       ; 4 bytes: the run a port must manage to be believed
MOS_IDN     equ 9       ; 4 bytes: identify-burst bytes seen, per port
MOS_IDB0    equ 13      ; 4 bytes: the first of them
MOS_IDLAST  equ 17      ; 2 words: [ticks] at the last one, per port
MOS_IDENT   equ 21      ; 4 bytes: this port answered like a mouse
MOS_IDANY   equ 25      ; at least one did
MOS_SEEN    equ 26      ; a complete packet has arrived this session
MOS_HPST    equ 27      ; hot-plug poller state
MOS_HPT     equ 28      ; word: the poller's last tick stamp
MOS_DRAIN   equ 30      ; byte: the identify drain's verdict
MOS_DSTAMP  equ 31      ; word: [ticks] at the drain's close
MOS_LINE    equ 33      ; the modem-status line the mouse pulls (0x10 / 0x08)
MOS_SIZE    equ 34

; --- the cursor, behind MOB_CUR ----------------------------------------------
MOC_X       equ 0       ; word
MOC_Y       equ 2       ; word
MOC_BTN     equ 4       ; byte

; --- the 7-byte PS/2 span (SPEC.md 9.4.4), behind MOB_P2 ---------------------
MOP_P2      equ 0       ; the PS/2 mouse is claimed
MOP_ST      equ 1       ; how far SPEC.md 9.9.1's handshake got
MOP_ID      equ 2       ; the device ID
MOP_PH      equ 3       ; the packet phase
MOP_B0      equ 4       ; the last packet's bytes
MOP_B1      equ 5
MOP_CMD0    equ 6       ; the 8042 command byte as the BIOS left it
MOP_SIZE    equ 7
