; =============================================================================
; os8088 - apps/video/vfx.inc
;
; THE SPEAKER SHAPER AS A PART of the Video Player's kern_mega arm (SPEC.md
; 98.3.15.2): apps/os88spkfx.inc assembled alone, apps/video/vfx.asm, part 2
; of the PKG_MEGA build, far-called through the vectors below. The shipped
; build includes os88spkfx.inc as it always did; the kern_mega arm had run out
; of room under APP_MAX_SIZE, and the shaper is the one subsystem of it with a
; three-call interface and no state the player reads.
;
;   +0 VFX_SIG   dw VFX_SIGVAL
;   +2 VFX_ABI   db VFX_ABIVER
;   +3 VFX_NVEC  db VFXV_N
;   +4 VFX_VEC   dw a near offset a vector, far-called with the part's CODE
;                alias for CS:
;      VFXV_INIT  os88spkfx_init's: ES:DI the family, AL/AH its two, and the
;                 speaker library's state it reads - BX = the part's own DATA
;                 segment, CX = os88spk_lptp, DX = os88spk_rseg, SI =
;                 os88spk_rl, BP = os88spk_sil (its low byte). out CL = what
;                 os88spk_sil must become (the shaper puts a dry ring's count
;                 at 1 with the carrier away), every other register kept
;      VFXV_LEVEL os88spkfx_level's: DS:SI a span, CX its length, ES the
;                 family. out AL its peak
;      VFXV_EMIT  os88spkfx_emit's: DS:SI -> ES:DI, CX samples
;      VFXV_BIND  the two segments the part keeps of the mode it was set up in
;                 (SPEC.md 98.3.24): BX = the part's own DATA segment (what
;                 its writing copies go through), DX = os88spk_rseg - after
;                 a mode change, so a real-mode bracket's play shapes through
;                 real segments and the desktop's through selectors again.
;                 The shaper's state is kept; every register kept
; =============================================================================
VFX_SIGVAL  equ 0x5846              ; 'FX'
VFX_ABIVER  equ 2
VFX_SIG     equ 0
VFX_ABI     equ 2
VFX_NVEC    equ 3
VFX_VEC     equ 4
VFXV_INIT   equ 0
VFXV_LEVEL  equ 1
VFXV_EMIT   equ 2
VFXV_BIND   equ 3
VFXV_N      equ 4

VFX_NLEV    equ 11                  ; os88spkfx_t.inc's SPKFX_NLEV (the part
                                    ; asserts it): the family's rows
VFX_PRE_DIFF equ 1                  ; SPKFX_PRE_DIFF
VFX_RINGX   equ SND_EXT_TAB + 256   ; SPKFX_RINGX: the family after the count
VFX_RINGEND equ VFX_RINGX + VFX_NLEV * 256  ; table, to SPKFX_RINGEND
