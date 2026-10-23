; =============================================================================
; os8088 - apps/xtmp3/x3stub.inc - the STUB backend's data layout (equates)
;
; Shared by the stub part (x3stub.asm) and the primary's test hook
; (x3hook.inc's x3_stubpoke). The stub is a TEST-ONLY backend, assembled
; only into an X3STUB=1 build (tests/xtmp3eng.py), never shipped: it speaks
; x3gran.inc's ABI, makes a tone per granule, and costs what X3STUB.CFG says.
; =============================================================================
%ifndef X3STUB_INC
%define X3STUB_INC
X3SB_KIND   equ 0x7F                ; X3K_KIND: neither STD nor XT - both
X3SB_RUNG   equ 0x20                ; db  the rung the engine opened it for
X3SB_CFG    equ 0x22                ; X3STUB.CFG's bytes:
X3SB_SPEED  equ 0                   ;   8 dw  speed x 100 by X3Q_* (0 = no
                                    ;         cost: as fast as the machine)
X3SB_SLR    equ 16                  ;   db    the rung that slows mid-track
X3SB_SLAFT  equ 18                  ;   dw    ...after this many granules...
X3SB_SLSPD  equ 20                  ;   dw    ...to this speed
X3SB_HAS    equ 22                  ;   dw    KB this machine HAS for XTMP3
                                    ;         (x3_haskb), 0 = the real figure
X3SB_FAILR  equ 24                  ;   db    a rung (X3Q_*) whose LIVE start is
                                    ;         refused as out of memory (the
                                    ;         card's ring), 0 = none
X3SB_UNDAT  equ 26                  ;   dw    a live granule (from the rung's
                                    ;         open) where the engine counts an
                                    ;         underrun as if the card ran dry...
X3SB_UNDN   equ 28                  ;   db    ...this many, 4 granules apart
                                    ;         (x3_window; 0 = none) - the
                                    ;         underrun RULE's test, not a stall
X3SB_QFAIL  equ 29                  ;   db    1 = the queue's probe is not had
                                    ;         while a stream is live, as on a
                                    ;         small heap (x3_e_qadd)
X3SB_CFGSZ  equ 30
%endif
