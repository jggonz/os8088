; =============================================================================
; os8088 - apps/skies/csw_lcy.inc
;
; LONDON (SPEC.md 88.6, 88.6.4) - the world LONDON-LCY stands in. Metres,
; x east, z north, y up; THE PALACE OF WESTMINSTER IS THE ORIGIN, and the
; Elizabeth Tower that carries Big Ben stands on it at (0, 0).
;
; THE WORLD IS THE APPROACH. London City really sits in the Royal Docks some
; 12,000 m east of Westminster, which is further than CS_FAR and further than
; anything a Cessna at 300 m would bother to fly. The runway is 7,100 m east
; here instead, and 3,870 m east of the Tower of London, so that a take-off on
; RUNWAY 27 leaves the docks heading 270 and runs straight up the Thames: the
; Tower of London first, then St Paul's, then the Palace of Westminster, with
; Buckingham Palace 1,290 m beyond it. That line is what this world is for,
; and every other choice below serves it.
;
; The three landmarks the location is named for are Big Ben at the origin, the
; Tower of London and Buckingham Palace, and the two that are away from the
; origin stand on their real bearings from Westminster at about a fifth of
; their real distance, which is what puts all three inside one flight rather
; than one of them at a time. St Paul's is the cheap fourth and sits between
; the Tower and Westminster, where it really is:
;
;   the Tower of London   (3230,  940)   3,364 m, bearing 074
;   St Paul's Cathedral   (1800, 1480)   2,330 m, bearing 051
;   Buckingham Palace    (-1250,  320)   1,290 m, bearing 284
;
; Those four and the Palace of Westminster that Big Ben stands on are the five
; CSO_POI, so Detail Level = Low draws the whole line of the departure rather than
; four scattered blocks.
;
; NOTHING COLLIDABLE STANDS IN THE THAMES (SPEC.md 88.6.3), and in London that
; is not a formality: the Palace of Westminster, the Tower of London and the
; Shard are all built ON the water in reality, and the Elizabeth Tower's foot
; is 20 m from the parapet. Every one of them is set back from the bank this
; world draws. Measured against the river polygons below, the tightest is the
; Palace of Westminster at 96.9 m, then the Tower of London at 107.8, the
; Victoria Tower at 115.3, the Shard at 137.7, the Elizabeth Tower at 152.5
; and the London Eye at 162.0; the gate wants 5.
;
; The macros, the face tables, the anonymous filler models, the ribbon faces
; and the shared edge lists are csworld.inc's.
;
; THE ORDER IN THIS FILE IS FIXED: models, then the object table, then the
; names, then the location record - because CS_N_LCY is an equ over the
; table's own two labels and NASM cannot see them from above.
; =============================================================================

; --- Westminster -------------------------------------------------------------
; The Elizabeth Tower is 96 m to the tip of its spire and 12 m square, and its
; silhouette is three things stacked: a plain shaft, the clock stage stepped in
; above it, and the spire. That is four levels and cs_f_sacre's thirteen faces,
; the same table the Pantheon and the Sacre-Coeur use in Paris. Level 1's roof
; at 62 m shows as the ledge under the clocks, which is what a reader picks the
; tower out by from the air.
cs_m_lcy_ben: db CSM_STACK, 4, 13, 0, CSI_LINE, 0
    dw 96, .v, cs_f_sacre, 0        ; the Euclidean radius, 88.5.11
.v: dw 9, 0, 9                      ; the shaft, 18 m square
    dw 9, 62, 9                     ; the ledge under the clock faces
    dw 7, 78, 7                     ; the belfry above them
    dw 0, 96, 0                     ; the spire

; Beyond 2,500 m the thirteen faces are worth nothing: the tower is a spike a
; few pixels wide, so the far model is that spike (SPEC.md 88.5.4).
CS_PYR cs_m_lcy_benf, 9,  96,  9

; The Palace itself is 260 m of hall lying north to south along the river with
; a tower at each end, which is why it needs both this box and the Victoria
; Tower below: one box alone reads as a warehouse.
CS_BOX cs_m_lcy_pal,  45,  24, 130
CS_BOX cs_m_lcy_vic,  12,  90,  12  ; the Victoria Tower, 90 m, at the south end

; Westminster Abbey stands 250 m west of the Palace across Parliament Square,
; and its nave runs east to west, across the Palace's, so the two together
; read as Westminster from the air rather than as one long roof. It is 150 m
; long and 30 m to the ridge; the west towers are 69 m and are not modelled,
; because a stack's levels are concentric and a tower at one end of a box is
; not a level.
CS_BOX cs_m_lcy_abb,  75,  30,  22

; The London Eye is 120 m across and turns in a plane parallel to the river,
; which here runs almost due north, so the wheel lies in z and y and is only
; 8 m thick in x. Four levels give a six-station rim: the hub is at 70 m and
; the chords at 42 and 106 are 120 m apart, which is the wheel's own diameter.
; It is drawn as EDGES and no faces, like the Eiffel Tower, because a rim with
; a face in it would read as a disc. Twelve edges are the two rims, and the
; last two cross the hub so that the wheel does not look empty. The 6 m chords
; that close the rim top and bottom are left out: they are under a pixel from
; anywhere the aeroplane can be.
cs_m_lcy_eye: db CSM_STACK, 4, 0, 14, CSI_LINE, 0
    dw 141, .v, 0, .e               ; the Euclidean radius, 88.5.11
.v: dw 4,   0,  3
    dw 4,  42, 60
    dw 4, 106, 60
    dw 4, 140,  3
.e: db 0,4, 4,8, 8,12               ; the +x rim, up its +z side
    db 3,7, 7,11, 11,15             ; the +x rim, up its -z side
    db 1,5, 5,9, 9,13               ; the -x rim, up its +z side
    db 2,6, 6,10, 10,14             ; the -x rim, up its -z side
    db 4,11, 7,8                    ; two spokes crossing at the hub

; --- the City and Southwark --------------------------------------------------
; St Paul's is 111 m to the cross over a drum 34 m across. CS_DOME is a box
; with a pyramid on it, which is the dome at this scale; the box is 70 m square
; rather than 34 so that the nave and transepts are in it too, and the world
; does not pay an object for them.
CS_DOME cs_m_lcy_stp, 35,  45, 111

; The Shard is 306 m and tapers to a point, so it is a pyramid and nothing
; else. It stands on the south bank at London Bridge and is the tallest thing
; in the world, which makes it the marker that says where the river bends.
CS_PYR  cs_m_lcy_shd, 28, 306,  28

; One Canada Square is 235 m with a pyramid roof of about 30, on a 50 m square
; plan, so CS_DOME is literally the building. It stands between the runway and
; the Tower of London and is the first landmark a RUNWAY 27 departure passes.
CS_DOME cs_m_lcy_ccw, 25, 200, 235

; --- the Tower of London -----------------------------------------------------
; A curtain wall 210 m by 170 m with the White Tower in the middle of it. Four
; levels and cs_f_sacre again: the walls to 13 m, the ward's ground inside
; them, then the keep rising off it to a 60 m by 52 m parapet at 30 m and a
; pyramid roof at 44. From 300 m that is a square fortress on the riverside
; with a square keep in it, which is the only shape it needs to be.
cs_m_lcy_tow: db CSM_STACK, 4, 13, 0, CSI_LINE, 0
    dw 212, .v, cs_f_sacre, 0       ; 105 + 85 + 44/2
.v: dw 105,  0, 85                  ; the curtain wall's footprint
    dw 105, 13, 85                  ; ...and its top
    dw  30, 30, 26                  ; the White Tower's parapet
    dw   0, 44,  0                  ; its roof

; Past 2,400 m the ward and the keep are one grey block, so the far model is
; that block and the near one's thirteen faces are not transformed at all.
CS_BOX  cs_m_lcy_towf, 105, 30,  85

; --- Buckingham Palace -------------------------------------------------------
; The east front is 108 m long and the quadrangle behind it about 120 m deep,
; and the whole thing is 24 m high, which is a box and honestly a box.
CS_BOX  cs_m_lcy_buc,  55,  24,  60

; --- the Thames (SPEC.md 88.6.1) ---------------------------------------------
; Ten stations from the Battersea reach at (-700, -2300) to the Royal Docks at
; (8200, 200), cut into three ribbons of four stations that share their ends,
; so the water joins without a step. The banks are 120 m either side of the
; centreline, a 240 m river, which is what the Thames is through the middle of
; London. The shape is the reason the ribbons are worth their vertices: the
; river runs north past Westminster, swings east and bulges north past
; Blackfriars, comes back south-east under the Tower and then straightens for
; the docks. That S is what the player navigates by, so it is drawn and not
; approximated.
;
; Each ribbon is eight vertices, four down the south-east bank and four down
; the north-west, so cs_f_rib3's three quads index them. Beyond CSO_LOD each
; gives way to ITS CENTRELINE - four vertices and three segments - because a
; piece that is one row of dither at the horizon must not cost eight
; transforms and three polygons (88.6.1).

cs_m_lcy_thw: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0    ; Battersea to Waterloo
    dw 1668, .v, cs_f_rib3, 0
.v: dw  -557, -1562                 ; the south-east bank
    dw    49,  -550
    dw   376,   449
    dw   564,  1426
    dw  -763, -1438                 ; ...and the north-west
    dw  -169,  -450
    dw   144,   511
    dw   376,  1574
cs_m_lcy_thwf: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 1668, .v, 0, cs_e_road3
.v: dw  -660, -1500
    dw   -60,  -500
    dw   260,   480
    dw   470,  1500

cs_m_lcy_thc: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0    ; Waterloo to Tower Bridge
    dw 1614, .v, cs_f_rib3, 0
.v: dw -1416,  -174
    dw  -527,   231
    dw   521,   -13
    dw  1474,  -465
    dw -1604,   -26
    dw  -553,   469
    dw   599,   213
    dw  1546,  -235
cs_m_lcy_thcf: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 1614, .v, 0, cs_e_road3
.v: dw -1510,  -100
    dw  -540,   350
    dw   560,   100
    dw  1510,  -350

cs_m_lcy_the: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0    ; Tower Bridge to the docks
    dw 2559, .v, cs_f_rib3, 0
.v: dw -2246,    25
    dw  -966,  -180
    dw   639,   -80
    dw  2531,  -230
    dw -2174,   255
    dw  -954,    60
    dw   641,   160
    dw  2549,    10
cs_m_lcy_thef: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 2559, .v, 0, cs_e_road3
.v: dw -2210,   140
    dw  -960,   -60
    dw   640,    40
    dw  2540,  -110

; --- the Mall and Whitehall --------------------------------------------------
; One road, three stations: out of Buckingham Palace's gates north-east to
; Trafalgar Square, then south to Parliament Square. It costs three vertices
; and two segments and it is what joins the two western points of interest, so
; a pilot who has found one has found the other.
cs_m_lcy_mall: db CSM_FLAT, 3, 0, 2, CSI_MARK, 0
    dw 600, .v, 0, cs_e_road2
.v: dw  -590,   -40                 ; about (-1150, 260)
    dw   110,   260                 ; about ( -450, 560)
    dw   490,  -220                 ; about (  -70,  80)

; =============================================================================
; The world. THIRTY objects, and the last ten are CSO_DENSE. Twenty of them
; are the London every rung below High draws, and of those five are CSO_POI:
; the three the location is named for, the Palace of Westminster that carries
; Big Ben, and St Paul's between them, so Detail Level = Low still draws the
; whole line of the RUNWAY 27 departure. Six are anonymous blocks and sheds,
; two of them in the Royal Docks beside the runway and four in the city; they
; draw from Moderate, so those twenty are the default picture.
;
; The TEN stand only at High (88.13.1.3) and are the City of London and
; Canary Wharf as the clusters they are rather than one tower each.
;
; Ranges follow Paris' bands: 16,000 for the headline tower, 12,000 to 14,000
; for the two that are over 200 m tall, 5,000 to 9,000 for a big building,
; 4,500 for a piece of river and 2,500 to 3,000 for a shed.
; =============================================================================
cs_ot_lcy:
    CS_OBJ cs_m_lcy_ben,  cs_m_lcy_benf,     0,     0,   0, 16000, 2500, cs_n_lcy_ben,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_lcy_pal,  0,               -40,  -150,   0,  6000,    0, cs_n_lcy_pal,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_lcy_vic,  0,               -40,  -310,   0,  6000,    0, cs_n_lcy_pal,  CSO_COLLIDE
    CS_OBJ cs_m_lcy_abb,  0,              -290,  -200,   0,  5000,    0, cs_n_lcy_abb,  CSO_COLLIDE
    CS_OBJ cs_m_lcy_eye,  0,               580,    40,   0,  5000,    0, cs_n_lcy_eye,  CSO_COLLIDE
    CS_OBJ cs_m_lcy_stp,  0,              1800,  1480,   0,  9000,    0, cs_n_lcy_stp,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_lcy_shd,  0,              2700,   480,   0, 14000,    0, cs_n_lcy_shd,  CSO_COLLIDE
    CS_OBJ cs_m_lcy_tow,  cs_m_lcy_towf,  3230,   940,   0,  9000, 2400, cs_n_lcy_tow,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_lcy_ccw,  0,              5300,   780,   0, 12000,    0, cs_n_lcy_ccw,  CSO_COLLIDE
    CS_OBJ cs_m_lcy_buc,  0,             -1250,   320,   0,  5000,    0, cs_n_lcy_buc,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_lcy_thw,  cs_m_lcy_thwf,   -40,  -800,   0,  4500, 2600, cs_n_lcy_th,   CSO_TERRAIN
    CS_OBJ cs_m_lcy_thc,  cs_m_lcy_thcf,  1940,   800,   0,  4500, 2600, cs_n_lcy_th,   CSO_TERRAIN
    CS_OBJ cs_m_lcy_the,  cs_m_lcy_thef,  5660,   310,   0,  5000, 3000, cs_n_lcy_th,   CSO_TERRAIN
    CS_OBJ cs_m_lcy_mall, 0,              -560,   300,   0,  5000,    0, cs_n_lcy_mall, CSO_ROAD
    CS_OBJ cs_m_hangar,   0,              6620,  1300,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_shed,     0,              7550,  1320,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block,    0,              1500,  1780,   0,  2500,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block2,   0,              2320,  1560,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_small,    0,              -900,  -640,   0,  2500,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block,    0,              2950,  -120,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    ; --- THE HIGH TIER (SPEC.md 88.13.1.3): ten more, CSO_DENSE, so Detail
    ;     Level = High is the only rung that draws one and every rung below it
    ;     gives the London that always shipped. They are the two things this
    ;     world names and then leaves as ONE OBJECT EACH - the City of London
    ;     and Canary Wharf are both CLUSTERS on the ground, and a departure up
    ;     the river that passes a single tower at each is a departure past two
    ;     lamp-posts.
    ;
    ;     The City goes between St Paul's and the Tower, which is where
    ;     Bishopsgate and Leadenhall really are, and NORTH of the water: the
    ;     first draft put one at (2560, 960) and that is in the Thames, the
    ;     middle ribbon's north bank running through z = 1,013 at that x.
    ;     Canary Wharf goes round One Canada Square at (5300, 780) - the Isle
    ;     of Dogs, inside the river's loop - and it is what a RUNWAY 27
    ;     departure meets first, 3.2 to 3.9 km from the spawn.
    ;
    ;     Each range is its own Manhattan distance FROM THE SPAWN plus 400 m
    ;     rounded up to 500, so the skyline is standing before the take-off
    ;     roll rather than arriving under the aeroplane, and every footprint
    ;     keeps 60 m of daylight from every other and 80 m from the water.
    CS_OBJ cs_m_tower,    0,              2380,  1250,   0,  7500,    0, cs_n_lcy_city, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,              2600,  1150,   0,  7000,    0, cs_n_lcy_city, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,    0,              2760,  1380,   0,  7000,    0, cs_n_lcy_city, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,              2950,  1180,   0,  6500,    0, cs_n_lcy_city, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,              2510,  1500,   0,  7500,    0, cs_n_lcy_city, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,    0,              3080,  1420,   0,  6500,    0, cs_n_lcy_city, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,    0,              5140,   630,   0,  4500,    0, cs_n_lcy_ccw,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,    0,              5470,   930,   0,  4000,    0, cs_n_lcy_ccw,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,              5180,  1010,   0,  4000,    0, cs_n_lcy_ccw,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,              5530,   590,   0,  4000,    0, cs_n_lcy_ccw,  CSO_COLLIDE | CSO_DENSE
cs_ote_lcy:
CS_N_LCY equ (cs_ote_lcy - cs_ot_lcy) / CSO_SIZE

; --- the names the crash line uses -------------------------------------------
; The Victoria Tower takes the Palace's name because it is part of it, and the
; three river pieces share one, which is what a pilot who ditches has hit.
cs_n_lcy_ben:  db 'THE ELIZABETH TOWER', 0
cs_n_lcy_pal:  db 'THE PALACE OF WESTMINSTER', 0
cs_n_lcy_abb:  db 'WESTMINSTER ABBEY', 0
cs_n_lcy_eye:  db 'THE LONDON EYE', 0
cs_n_lcy_stp:  db "ST PAUL'S CATHEDRAL", 0
cs_n_lcy_shd:  db 'THE SHARD', 0
cs_n_lcy_tow:  db 'THE TOWER OF LONDON', 0
cs_n_lcy_ccw:  db 'CANARY WHARF', 0
cs_n_lcy_buc:  db 'BUCKINGHAM PALACE', 0
cs_n_lcy_th:   db 'THE THAMES', 0
cs_n_lcy_mall: db 'THE MALL', 0
cs_n_lcy_city: db 'THE CITY OF LONDON', 0

; =============================================================================
; The location (SPEC.md 88.6, 88.6.4). London City is one runway, 09/27, on
; the north side of the Royal Albert Dock, and it is placed here at (7100, 950)
; which is 7,163 m from Westminster on bearing 082 and 3,870 m east of the
; Tower of London. The aeroplane stands 60 m in from the east threshold facing
; 270, so the take-off run goes west and the whole world is ahead of it. The
; north bank of the Thames passes about 540 m south of the runway and the two
; dock sheds stand 268 m and 308 m north of its rectangle, both clear of the
; 150 m the runway must keep.
; =============================================================================
cs_a_lcy:
    dw cs_s_lcy                     ; CSA_NAME
    dw 7100, 950                    ; CSA_X, CSA_Z: the Royal Docks, east of
    dw 0                            ; CSA_ELEV     the city on the river's
                                    ;              north side
    dw 49152                        ; CSA_HDG      270
    dw 750                          ; CSA_HLEN     a 1,500 m runway...
    dw 22                           ; CSA_HWID     ...44 m wide
    dw cs_s_rwy_lcy                 ; CSA_RWY
    dw -690                         ; CSA_SPAWN    60 m in from the threshold
    dw cs_ot_lcy, CS_N_LCY          ; CSA_OBJS, CSA_NOBJ
    dw 7150, 275                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 50062                        ; CSA_WHDG    (88.7.7) - THE THAMES,
                                    ; 275 and not 95: upstream, toward the
                                    ; Tower and Westminster (88.7.7.3)
    dw 800                          ; CSA_WLEN    half its length...
    dw 100                           ; CSA_WWID    ...and half its width
    dw cs_n_lcy_th              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
cs_s_lcy: db 'LONDON-LCY', 0
cs_s_rwy_lcy: db 'RUNWAY 27', 0
