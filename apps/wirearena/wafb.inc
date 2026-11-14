; WireArena's FRAME BLOCK and the renderer part's ABI (docs/plans/
; WIREARENA-PARTS-PLAN.md section 7). Included by BOTH halves, which is what
; makes the block one layout: the primary (wirearena.asm) expands WA_FBLOCK
; in its BSS, the renderer part (wrpart.asm) at WR_FB in its image, each
; through its own WA_FBV, so the variables come in this order in both. A
; variable's address in the part is therefore its primary address
; - wa_fb_cfg + WR_FB (WA_RAT below).
;
; Who writes what, and when the primary copies it across:
;   config   G writes (wa_setmode, wa_setfov); G -> R by wa_rsync
;   frame    G writes (the sim, the keys); G -> R every frame by wa_render
;   figs     G writes F_X..F_Z (bytes 0..19: G -> R every frame and
;            before TRACE) and F_HEAD/F_PATH; R writes the projection,
;            F_PZ..F_PWH, and keeps it. G's own F_PZ means only "drawn since
;            it spawned" (1 after each frame, 0 from wa_spawn); before TRACE
;            a 0 there zeroes the part's, so a figure respawned since the
;            frame is not hit where its body lay
;   arena    R writes (ARENA); R -> G by wa_loadarena - but for a pickup's
;            KIND byte, which a weapon spawn picks at random when it spawns:
;            G writes it into both copies (wa_pkroll)
;   overlay  G writes (wa_ovbuild: the weapon, the health, the ammo); G -> R
;            by wa_render when the game has rebuilt it, and not otherwise
;   rockets  G writes (the sim); G -> R by wa_rframe while any is live
; A variable the RENDERER reads that the primary writes belongs in config
; or frame, and nowhere else: R reads only its own copy.

; --- the renderer part's header ---------------------------------------------
WR_MAGIC0   equ 'WR'                ; +0: 'WRPART', then the ABI byte and the
WR_MAGIC1   equ 'PA'                ;     verb count - checked by the primary
WR_MAGIC2   equ 'RT'                ;     at launch, refused on a mismatch
WR_ABI      equ 9                   ; (2: the camera's sin/cos in FRAME;
                                    ; 3: F_Z, the treads, ARENA's heights;
                                    ; 4: wa_look, the detail level's bits,
                                    ; where wa_hyst was; wa_flod gone;
                                    ; 5: WEAPONS-PLAN 2.6 - F_SH at 10,
                                    ; F_SIZE 44, the pickups, the rockets,
                                    ; OVERLAY, TRACE's cone; 6: F_SIZE
                                    ; 56, the bots' bytes; no patrols in
                                    ; ARENA - AI-PLAN 3.1; 7: wa_look's
                                    ; bit 7, LK_COARSE2; 8: the lifts'
                                    ; tables, ARENAS-PLAN 4.1; 9: the
                                    ; floor lines, 4.5)
WA_MLIFT    equ 4                   ; lifts an arena may have (arenagen's)
WA_MFL      equ 24                  ; floor lines an arena may have (ARENAS-
                                    ; PLAN 4.5; arenagen's)
WR_H_ABI    equ 6
WR_H_NV     equ 7
WR_H_VERB   equ 8                   ; +8: a word per verb, its offset
WR_FB       equ 32                  ; the frame block's offset in the part

; the verbs: index x 2. In: DS = the part's data (the primary's glue sets
; it), SS the caller's, ES anything. Out: retf; every register but DS, SS
; and SP may be clobbered, ES included.
WRV_INIT    equ 0                   ; after a mode set: row tables, stamps
WRV_GEOM    equ 2                   ; after ARENA: wa_geominit
WRV_ARENA   equ 4                   ; AL = an arena: its working tables
WRV_RENDER  equ 6                   ; out AL = the frame number (wa_frame)
WRV_PRESENT equ 8                   ; out AX = text cells written (wa_nwr)
WRV_TRACE   equ 10                  ; in CX = the cone's half width, sub-
                                    ; columns, DX its half height, half-
                                    ; rows (0, 0: the pistol's ray); out
                                    ; AX = the nearest figure hit, its
                                    ; offset in wa_figs, or -1, and DX a
                                    ; bit each figure hit (bit 8 + i: and
                                    ; DIRECTLY, through the crosshair)
WR_NVERB    equ 6

WA_FIGHEAD  equ 20                  ; figs: the bytes G -> R every frame
                                    ; (F_X..F_Z)
WR_FKA      equ 36                  ; ...and two words of a figure's record
WR_FKB      equ 38                  ; that are the PART's alone: which
                                    ; keyframe rows it holds (wa_kfrows; the
                                    ; game's F_VH/F_VX there never cross)

%define WA_RAT(x) ((x) - wa_fb_cfg + WR_FB)

WA_NRK      equ 4                   ; rockets and grenades live at once
WA_RKSZ     equ 8                   ; FRAME's rocket: x, y, z, state
WA_RKX      equ 0
WA_RKY      equ 2
WA_RKZ      equ 4
WA_RKST     equ 6                   ; (its low byte; the high byte its type)
WA_RKTYP    equ 7                   ; 0 a rocket, 1 a grenade
WA_RKFLY    equ 1                   ; state: flying; 2..: exploding
WA_RKBOOM   equ 8                   ; an explosion's first state (7 ticks)
WA_PKSZ     equ 8                   ; ARENA's pickup: x, y, z, kind, 0
WA_MOV      equ 160                 ; OVERLAY's most cells (the scoreboard's
                                    ; 100 among them: AI-PLAN 5)

; --- the block: WA_FBLOCK, expanded where the includer keeps it -------------
%macro WA_FBLOCK 0
WA_FBV wa_fb_cfg, 0                 ; CONFIG
WA_FBV wa_vseg, 2
WA_FBV wa_w, 2
WA_FBV wa_w2, 2
WA_FBV wa_k16, 2
WA_FBV wa_c16, 2
WA_FBV wa_kyq, 2
WA_FBV wa_kyh, 2
WA_FBV wa_wf, 2
WA_FBV wa_mono, 1
WA_FBV wa_fbpad0, 1
WA_FBV wa_posg, 2                   ; the POSE part's segment (the keyframes,
                                    ; PARTS-PLAN 8.6): the primary's op_seg
WA_FBV wa_fb_frm, 0                 ; FRAME
WA_FBV wa_px, 2
WA_FBV wa_py, 2
WA_FBV wa_ang, 2
WA_FBV wa_pitch, 2
WA_FBV wa_eye, 2
WA_FBV wa_fsn, 2                    ; the camera's sin and cos, Q14, at wa_ang
WA_FBV wa_fcs, 2                    ; >> 6 (G: wa_rframe, from its wa_sincos)
WA_FBV wa_fov, 1
WA_FBV wa_look, 1                   ; the detail level's options, LK_*
WA_FBV wa_grid, 1
WA_FBV wa_sparse, 1
WA_FBV wa_mflash, 1
WA_FBV wa_treads, 1                 ; a ramp drawn as stepped treads (T: the
                                    ; graphics option, HEIGHTS-PLAN 7.4.2)
WA_FBV wa_pkon, 1                   ; the pickups present, a bit each
WA_FBV wa_fbpad3, 1                 ; (WEAPONS-PLAN 2.4)
WA_FBV wa_figs, F_SIZE*WA_NFIG      ; FIGS (follows FRAME: one copy)
WA_FBV wa_fb_arn, 0                 ; ARENA
WA_FBV wa_sx, 2
WA_FBV wa_sy, 2
WA_FBV wa_sa, 2
WA_FBV wa_nbox, 2
WA_FBV wa_boxes, WA_MBOX*8
WA_FBV wa_aflat, 1                  ; 1: no box below the tallest, no lift
WA_FBV wa_fbpad2, 1                 ; (HEIGHTS-PLAN 7.4: the sim's heights)
WA_FBV wa_ahmax, 2                  ; the tallest height (wa_hmax's copy)
WA_FBV wa_bxh, WA_MBOX*4            ; per box: height, kind, faces - a
                                    ; lift's height the sim's to move (G
                                    ; writes it into R's every frame)
WA_FBV wa_nlift, 2                  ; THE LIFTS (ARENAS-PLAN 4.1): how many,
WA_FBV wa_lift, WA_MLIFT*2          ; and per lift its record's offset in
WA_FBV wa_lrest, WA_MLIFT*2         ; wa_bxh, its rest and raised heights
WA_FBV wa_ltop, WA_MLIFT*2          ; (HEIGHTS-PLAN 3.2) - a word each
WA_FBV wa_nfl, 2                    ; ITS FLOOR LINES (ARENAS-PLAN 4.5): how
WA_FBV wa_fl, WA_MFL*8              ; many, and each's x0, y0, x1, y1 - the
                                    ; game's, for the feet (wa_flcross)
WA_FBV wa_npk, 2                    ; its pickups (WEAPONS-PLAN 2.4): how
WA_FBV wa_pks, WA_MPK*WA_PKSZ       ; many, and each's x, y, z, kind
WA_FBV wa_fb_end, 0
WA_FBV wa_fb_ovl, 0                 ; OVERLAY: G writes (wa_ovbuild), G -> R
WA_FBV wa_ovfresh, 2                ; when it is rebuilt: 1 (R clears it
WA_FBV wa_ovn, 2                    ; once it has shown it), cells, each's
WA_FBV wa_ov, WA_MOV*2              ; offset (x 2), and its glyph and
WA_FBV wa_ovg, WA_MOV*2             ; attribute
WA_FBV wa_fb_oend, 0
WA_FBV wa_rks, WA_NRK*WA_RKSZ       ; ROCKETS: G -> R by wa_rframe while any
                                    ; flies or explodes, and once more after:
                                    ; each's x, y, z, state (0 none, 1
                                    ; flying, 2.. an explosion's ticks left)
                                    ; and type (0 rocket, 1 grenade:
                                    ; WEAPONS-PLAN 2.2, 2.5, 6)
%endmacro
