; =============================================================================
; os8088 - apps/skies/csw_paris.inc
;
; PARIS (SPEC.md 88.6, 88.6.4) - the world Clear Skies shipped with, and the
; worked example every other csw_*.inc is cut from. Two locations stand in it:
; PARIS-ISSY, the city's first aerodrome, on the river bend 3.5 km south-west
; of the Eiffel Tower, and PARIS-LBG (Le Bourget) 7 km north-east and outside
; the ring road's square. THE ORIGIN IS THE EIFFEL TOWER'S SQUARE.
;
; The tower itself STANDS 60 m BACK from it, at (25, -55), which is away from
; the river along the near bank's normal: its base is 124 m across and the
; Seine's near bank passes 66 m from the square, so a corner of it stood 15.7 m
; INSIDE the water and was drawn dipping into the river. The setback leaves the
; nearest corner 44.7 m clear. THE LOUVRE AND NOTRE-DAME WERE IN THE WATER TOO,
; which nobody had reported and tests/unit/t_csworld.py found the moment it
; existed: the palace by 30.8 m and the cathedral by 7.9. They moved with the
; tower and every collidable base now stands clear (88.6.3).
;
; The macros, the face tables, the anonymous filler models and the ribbon
; faces this file uses are csworld.inc's, shared with every other world.
; =============================================================================

CS_BOX cs_m_troc,   50,  24,  60
CS_BOX cs_m_ecole, 100,  22,  40
CS_BOX cs_m_inval, 110,  22, 110
CS_DOME cs_m_idome, 25,  60, 107
CS_BOX cs_m_montp,  25, 210,  16
CS_BOX cs_m_louvre, 250, 24,  60
CS_PYR cs_m_lpyr,   17,  21,  17
CS_BOX cs_m_ndnave, 60,  32,  24
CS_BOX cs_m_ndtwr,  10,  69,  10
CS_BOX cs_m_arc,    25,  50,  12
CS_BOX cs_m_arche,  55, 110,  55
CS_BOX cs_m_def1,   30, 180,  30
CS_BOX cs_m_def2,   25, 150,  25
CS_BOX cs_m_def3,   28, 160,  28
CS_BOX cs_m_def4,   24, 120,  24

cs_m_panth: db CSM_STACK, 4, 13, 0, CSI_LINE, 0
    dw 120, .v, cs_f_sacre, 0
.v: dw 40, 0, 40
    dw 40, 30, 40
    dw 15, 50, 15
    dw 0, 83, 0

cs_m_sacre: db CSM_STACK, 4, 13, 0, CSI_LINE, 0
    dw 120, .v, cs_f_sacre, 0
.v: dw 40, 0, 40
    dw 40, 25, 40
    dw 12, 55, 12
    dw 0, 83, 0

cs_m_hill: db CSM_STACK, 2, 5, 0, CSI_LINE, 0
    dw 1300, .v, cs_f_hill, 0
.v: dw 700, 0, 500
    dw 150, 100, 120

; --- the tower (SPEC.md 88.5): five levels, thirty-two edges ------------------
cs_m_eiffel: db CSM_STACK, 5, 0, 32, CSI_LINE, 0
    dw 330, .v, 0, .e
.v: dw 62, 0, 62
    dw 35, 57, 35
    dw 20, 115, 20
    dw 8, 276, 8
    dw 0, 324, 0
.e: db 0,1, 1,2, 2,3, 3,0           ; the squares at the four platforms
    db 4,5, 5,6, 6,7, 7,4
    db 8,9, 9,10, 10,11, 11,8
    db 12,13, 13,14, 14,15, 15,12
    db 0,4, 1,5, 2,6, 3,7           ; the legs between them
    db 4,8, 5,9, 6,10, 7,11
    db 8,12, 9,13, 10,14, 11,15
    db 12,16, 13,17, 14,18, 15,19   ; ...and the spire

; THE FAR ONE IS A SKYLINE (SPEC.md 88.5.4.5). It was four straight legs from
; the base square to a point - a tent, or a radio mast, and the two far legs
; project down the middle as a line that reads as a mistake. What makes this
; tower recognisable at twenty-five pixels is three things and none of them is
; a platform square: a FLARED base, ONE horizontal bar, and a thin spire.
;
; The SPIRE is one diagonal pair, the square being under two pixels across up
; there so the other two legs land in the same column; the SHAFT keeps all
; four, because eleven pixels of width tapering to five is the taper you can
; see, and the taper is what the old model had none of.
;
; The heights are the SKYLINE's and not the tower's. A first platform at the
; real 57 m of 324 is a fifth of the way up, which at this size is five
; pixels and lands inside the ground band: the flare was drawn, and invisible.
; At 105 it is a third of the way up and reads.
;
; THE PLATFORM BAR is what makes this tower not a pylon, and it is the reason
; 88.4.6.1 exists: it was refused for a whole commit because it left ink at
; the far LEFT of a CGA view, which turned out to be every exactly-horizontal
; segment in the program being drawn there.
;
; The apex is 324 and no longer 300, which is the near model's - the LOD
; switch used to change the tower's height by 24 m as you flew toward it.
cs_m_eiffelf: db CSM_STACK, 4, 0, 12, CSI_LINE, 0
    dw 330, .v, 0, .e
.v: dw 62, 0, 62                    ; the feet
    dw 34, 105, 34                  ; the first platform, where the flare ends
    dw 14, 190, 14                  ; the second
    dw 0, 324, 0                    ; the apex
.e: db 4,6, 5,7                     ; THE PLATFORM, as its two DIAGONALS and
                                    ; not its four sides - a diagonal is the
                                    ; square's full width from exactly the
                                    ; azimuths where a side is foreshortened,
                                    ; so two edges do what four would and no
                                    ; square is drawn (88.5.4.5)
    db 0,4, 1,5, 2,6, 3,7           ; the FLARE, all four legs: it is the
                                    ; widest thing here and the one part
                                    ; whose shape a viewer can resolve
    db 4,8, 5,9, 6,10, 7,11         ; the shaft, which needs its WIDTH
    db 8,12, 10,14                  ; ...and the spire, which does not

; --- the Seine, three strips cut in two (generated: SPEC.md 88.6): each half
;     a ribbon of two or three quads near, its centreline far, so a piece
;     that is a row of dither at the horizon is a segment or two and not a
;     dozen transformed vertices (88.6.1) ----------------------------------
cs_m_rive0: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1741, .v, cs_f_rib2, 0
.v: dw 1424, -1000
    dw -276, 99
    dw -1276, 700
    dw 1510, -866
    dw -190, 235
    dw -1190, 834
cs_m_rive0f: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1739, .v, 0, cs_e_road2
.v: dw 1467, -933
    dw -233, 167
    dw -1233, 767
cs_m_rive1: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1391, .v, cs_f_rib2, 0
.v: dw 1224, -600
    dw 43, 191
    dw -1333, 187
    dw 1310, -466
    dw 91, 343
    dw -1333, 347
cs_m_rive1f: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1375, .v, 0, cs_e_road2
.v: dw 1267, -533
    dw 67, 267
    dw -1333, 267
cs_m_rivc0: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1090, .v, cs_f_rib2, 0
.v: dw 1019, 239
    dw 26, -9
    dw -957, -450
    dw 981, 395
    dw -26, 143
    dw -1043, -316
cs_m_rivc0f: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1083, .v, 0, cs_e_road2
.v: dw 1000, 317
    dw 0, 67
    dw -1000, -383
cs_m_rivc1: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1624, .v, cs_f_rib2, 0
.v: dw 1310, 933
    dw -83, -163
    dw -1083, -962
    dw 1224, 1067
    dw -183, -37
    dw -1183, -838
cs_m_rivc1f: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1615, .v, 0, cs_e_road2
.v: dw 1267, 1000
    dw -133, -100
    dw -1133, -900
cs_m_rivw0: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1230, .v, cs_f_rib2, 0
.v: dw 1186, -78
    dw -43, -379
    dw -1190, 243
    dw 1148, 78
    dw -23, -221
    dw -1076, 357
cs_m_rivw0f: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1230, .v, 0, cs_e_road2
.v: dw 1167, 0
    dw -33, -300
    dw -1133, 300
cs_m_rivw1: db CSM_FLAT, 4, 1, 0, CSI_RIVER, 0
    dw 597, .v, cs_f_rib1, 0
.v: dw 193, -557
    dw -322, 464
    dw 307, -443
    dw -178, 536
cs_m_rivw1f: db CSM_FLAT, 2, 0, 1, CSI_RIVLINE, 0
    dw 597, .v, 0, cs_e_road1
.v: dw 250, -500
    dw -250, 500

; --- the roads: the axis from the Louvre to La Defense, and the ring road ----
; (the edge lists are csworld.inc's shared cs_e_road*)

cs_m_axis: db CSM_FLAT, 3, 0, 2, CSI_MARK, 0
    dw 4200, .v, 0, cs_e_road2
.v: dw -3700, 1900                  ; about (-600, 1900)
    dw 600, -200
    dw 3630, -1600

cs_m_per0: db CSM_FLAT, 4, 0, 3, CSI_MARK, 0
    dw 3739, cs_m_per0_v, 0, cs_e_road3
cs_m_per0_v:
    dw  -3076,   2124
    dw   -476,   1427
    dw   1427,   -476
    dw   2124,  -3076
cs_m_per1: db CSM_FLAT, 4, 0, 3, CSI_MARK, 0
    dw 3739, cs_m_per1_v, 0, cs_e_road3
cs_m_per1_v:
    dw   2124,   3076
    dw   1427,    476
    dw   -476,  -1427
    dw  -3076,  -2124
cs_m_per2: db CSM_FLAT, 4, 0, 3, CSI_MARK, 0
    dw 3739, cs_m_per2_v, 0, cs_e_road3
cs_m_per2_v:
    dw   3076,  -2124
    dw    476,  -1427
    dw  -1427,    476
    dw  -2124,   3076
cs_m_per3: db CSM_FLAT, 4, 0, 3, CSI_MARK, 0
    dw 3739, cs_m_per3_v, 0, cs_e_road3
cs_m_per3_v:
    dw  -2124,  -3076
    dw  -1427,   -476
    dw    476,   1427
    dw   3076,   2124

cs_ot_paris:
    CS_OBJ cs_m_eiffel, cs_m_eiffelf,    25,   -55,   0, 16000, 2500, cs_n_eiffel, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_troc,   0,             -480,   520,   0,  3000,    0, cs_n_troc,   CSO_COLLIDE
    CS_OBJ cs_m_troc,   0,             -180,   620,   0,  3000,    0, cs_n_troc,   CSO_COLLIDE
    CS_OBJ cs_m_ecole,  0,              150,  -900,   0,  3000,    0, cs_n_ecole,  CSO_COLLIDE
    CS_OBJ cs_m_inval,  0,             1320,  -380,   0,  3500,    0, cs_n_inval,  CSO_COLLIDE
    CS_OBJ cs_m_idome,  0,             1320,  -560,   0,  8000,    0, cs_n_inval,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_montp,  0,             2000, -1800,   0, 16000,    0, cs_n_montp,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_louvre, 0,             2700,    50,   0,  5000,    0, cs_n_louvre, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_lpyr,   0,             2830,    80,   0,  2500,    0, cs_n_louvre, CSO_COLLIDE
    CS_OBJ cs_m_ndnave, 0,             4040,  -620,   0,  4000,    0, cs_n_nd,     CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_ndtwr,  0,             3975,  -605,   0,  6000,    0, cs_n_nd,     CSO_COLLIDE
    CS_OBJ cs_m_ndtwr,  0,             3975,  -635,   0,  6000,    0, cs_n_nd,     CSO_COLLIDE
    CS_OBJ cs_m_panth,  0,             4210, -1280,   0,  7000,    0, cs_n_panth,  CSO_COLLIDE
    CS_OBJ cs_m_arc,    0,                0,  1700,   0,  5000,    0, cs_n_arc,    CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_arche,  0,            -4300,  3800,   0,  5000,    0, cs_n_def,    CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_def1,   0,            -4000,  3500,   0, 14000,    0, cs_n_def,    CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_def2,   0,            -3800,  3650,   0, 12000,    0, cs_n_def,    CSO_COLLIDE
    CS_OBJ cs_m_def3,   0,            -4100,  3350,   0, 12000,    0, cs_n_def,    CSO_COLLIDE
    CS_OBJ cs_m_def4,   0,            -3700,  3400,   0, 10000,    0, cs_n_def,    CSO_COLLIDE
    CS_OBJ cs_m_hill, 0,             3560,  3150,   0, 12000,    0, cs_n_mont,   CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_sacre,  0,             3560,  3150, 100,  9000,    0, cs_n_sacre,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_rive0, cs_m_rive0f,   5033,  -1067,   0,  4500, 2600, cs_n_seine,  CSO_TERRAIN
    CS_OBJ cs_m_rive1, cs_m_rive1f,   2533,    233,   0,  4500, 2600, cs_n_seine,  CSO_TERRAIN
    CS_OBJ cs_m_rivc0, cs_m_rivc0f,    200,    183,   0,  4500, 2600, cs_n_seine,  CSO_TERRAIN
    CS_OBJ cs_m_rivc1, cs_m_rivc1f,  -2067,  -1200,   0,  4500, 2600, cs_n_seine,  CSO_TERRAIN
    CS_OBJ cs_m_rivw0, cs_m_rivw0f,  -4367,  -2100,   0,  4500, 2600, cs_n_seine,  CSO_TERRAIN
    CS_OBJ cs_m_rivw1, cs_m_rivw1f,  -5750,  -1300,   0,  4500, 2600, cs_n_seine,  CSO_TERRAIN
    CS_OBJ cs_m_axis,   0,             -600,  1900,   0,  8000,    0, cs_n_seine,  CSO_ROAD
    CS_OBJ cs_m_per0,   0,             5576,  3576,   0,  8000,    0, cs_n_seine,  CSO_ROAD
    CS_OBJ cs_m_per1,   0,             5576, -2576,   0,  8000,    0, cs_n_seine,  CSO_ROAD
    CS_OBJ cs_m_per2,   0,             -576, -2576,   0,  8000,    0, cs_n_seine,  CSO_ROAD
    CS_OBJ cs_m_per3,   0,             -576,  3576,   0,  8000,    0, cs_n_seine,  CSO_ROAD
    CS_OBJ cs_m_hangar, 0,            -2750, -2350,   0,  3000,    0, cs_n_bldg,   CSO_COLLIDE
    CS_OBJ cs_m_shed,   0,            -2300, -2750,   0,  3000,    0, cs_n_bldg,   CSO_COLLIDE
    CS_OBJ cs_m_small,  0,            -2900, -2050,   0,  2500,    0, cs_n_bldg,   CSO_COLLIDE
    CS_OBJ cs_m_block,  0,             1000,  1200,   0,  3000,    0, cs_n_bldg,   CSO_COLLIDE
    CS_OBJ cs_m_block2, 0,             1900,   800,   0,  3000,    0, cs_n_bldg,   CSO_COLLIDE
    CS_OBJ cs_m_block,  0,             -900,  -700,   0,  3000,    0, cs_n_bldg,   CSO_COLLIDE
    CS_OBJ cs_m_block2, 0,            -1700,   200,   0,  3000,    0, cs_n_bldg,   CSO_COLLIDE
    ; --- THE HIGH TIER (SPEC.md 88.13.1.3): eight more, CSO_DENSE, so Detail
    ;     Level = High is the only rung that draws one and every rung below it
    ;     gives the Paris that always shipped - which matters more here than
    ;     anywhere, this being the world SPEC.md 88.12's whole frame budget
    ;     was measured on.
    ;
    ;     SIX OF THE EIGHT ARE AT LA DEFENSE, and that is the only honest
    ;     place to put towers in this city. Paris is a low city: what it has
    ;     instead of a skyline is a fabric of six-storey blocks that a
    ;     filled-polygon engine at twelve frames a second cannot draw the
    ;     point of. La Defense is the exception - a tight grid of slabs four
    ;     kilometres north-west of the Arc - and this world already gives it
    ;     five objects, which reads as five towers rather than as a business
    ;     district. Eleven reads as one.
    ;
    ;     THEY ARE ALL 110 m and not one is over it, so the 180, 160 and 150
    ;     already there stay the head of the cluster and the silhouette keeps
    ;     its shape. The other two are mid-rises on the axis between the Arc
    ;     de Triomphe and the Louvre, which is the line a flight over the
    ;     centre follows and the one stretch of it with nothing on either
    ;     side.
    ;
    ;     Ranges are Manhattan-from-the-spawn plus 400 rounded to 500, so La
    ;     Defense stands before the roll at Issy - it is 7.9 km from the
    ;     threshold and is what the departure turns towards.
    CS_OBJ cs_m_tower2, 0,            -4450,  3600,   0,  8500,    0, cs_n_def,    CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2, 0,            -3900,  3900,   0,  8500,    0, cs_n_def,    CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2, 0,            -3550,  3150,   0,  7000,    0, cs_n_def,    CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2, 0,            -4250,  3100,   0,  8000,    0, cs_n_def,    CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2, 0,            -3500,  3800,   0,  8000,    0, cs_n_def,    CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2, 0,            -4000,  4150,   0,  8500,    0, cs_n_def,    CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise, 0,             700,  1200,   0,  9500,    0, cs_n_p_axe,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise, 0,            1500,   700,   0,  9500,    0, cs_n_p_axe,  CSO_COLLIDE | CSO_DENSE
cs_ote_paris:
CS_N_PARIS equ (cs_ote_paris - cs_ot_paris) / CSO_SIZE
cs_n_eiffel: db 'THE EIFFEL TOWER', 0
cs_n_troc:   db 'THE TROCADERO', 0
cs_n_ecole:  db 'THE ECOLE MILITAIRE', 0
cs_n_inval:  db 'LES INVALIDES', 0
cs_n_montp:  db 'THE MONTPARNASSE TOWER', 0
cs_n_louvre: db 'THE LOUVRE', 0
cs_n_nd:     db 'NOTRE-DAME', 0
cs_n_panth:  db 'THE PANTHEON', 0
cs_n_arc:    db 'THE ARC DE TRIOMPHE', 0
cs_n_def:    db 'LA DEFENSE', 0
cs_n_mont:   db 'MONTMARTRE', 0
cs_n_sacre:  db 'THE SACRE-COEUR', 0
cs_n_seine:  db 'THE SEINE', 0
cs_n_p_axe:  db 'THE CHAMPS-ELYSEES', 0

; =============================================================================
; The locations (SPEC.md 88.6, 88.6.4): a name, a place, an elevation, a runway
; and THE WORLD THE RUNWAY IS IN. skies.asm's cs_ports lists both of these.
; =============================================================================
cs_a_issy:
    dw cs_s_issy                    ; CSA_NAME
    dw -2600, -2300                 ; CSA_X, CSA_Z: 3.5 km south-west of the
    dw 0                            ; CSA_ELEV     tower, on the river bend
    dw 7282                         ; CSA_HDG      040
    dw 500                          ; CSA_HLEN     a 1,000 m runway...
    dw 20                           ; CSA_HWID     ...40 m wide
    dw cs_s_rwy04                   ; CSA_RWY
    dw -440                         ; CSA_SPAWN    60 m in from the threshold
    dw cs_ot_paris, CS_N_PARIS      ; CSA_OBJS, CSA_NOBJ
    dw -1500, -750                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 9102                        ; CSA_WHDG    (88.7.7) - THE SEINE
    dw 800                          ; CSA_WLEN    half its length...
    dw 40                           ; CSA_WWID    ...and half its width
    dw cs_n_seine              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
cs_a_lbg:
    dw cs_s_lbg                     ; CSA_NAME
    dw 4500, 5000                   ; CSA_X, CSA_Z: 7 km north-east, outside
    dw 0                            ; CSA_ELEV     the ring road's square
    dw 12743                        ; CSA_HDG      070
    dw 700                          ; CSA_HLEN     a 1,400 m runway...
    dw 22                           ; CSA_HWID     ...45 m wide
    dw cs_s_rwy07                   ; CSA_RWY
    dw -640                         ; CSA_SPAWN    60 m in from the threshold
    dw cs_ot_paris, CS_N_PARIS      ; CSA_OBJS, CSA_NOBJ: the SAME world, seen
    dw 5650, -1450                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 22756                        ; CSA_WHDG    (88.7.7) - THE SEINE
    dw 1000                          ; CSA_WLEN    half its length...
    dw 40                           ; CSA_WWID    ...and half its width
    dw cs_n_seine              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
                                    ; from the other side of it
cs_s_issy:   db 'PARIS-ISSY', 0
cs_s_rwy04:  db 'RUNWAY 04', 0
cs_s_lbg:    db 'PARIS-LBG', 0
cs_s_rwy07:  db 'RUNWAY 07', 0
