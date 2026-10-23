; =============================================================================
; os8088 - apps/xtmp3/x3stub.inc - the STUB backend's data layout (equates)
;
; Shared by the stub part (x3stub.asm) and the primary's test hook
; (x3stubp.inc's x3_stubpoke). The stub is a TEST-ONLY backend, assembled
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
X3SB_CFGSZ  equ 22
%endif
