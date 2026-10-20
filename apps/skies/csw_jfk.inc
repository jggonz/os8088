; =============================================================================
; os8088 - apps/skies/csw_jfk.inc
;
; NEW YORK, 1931 (SPEC.md 88.6.4) - the world NYC-JFK stands in, at the one
; moment the Empire State Building was the tallest thing ever built and had
; the sky to itself. Metres, x east, z north, y up; THE ORIGIN IS THE EMPIRE
; STATE BUILDING, at Fifth Avenue and Thirty-Fourth Street.
;
; WHAT MAKES IT READ AS NEW YORK IS THE ISLAND, not the buildings. Manhattan
; is a long thin wedge with water down both sides, and that silhouette carries
; further than any facade: the Hudson to the west, the East River to the east,
; and the two of them closing on each other at the Battery. So the two rivers
; are four ribbons with a centreline far model each (88.6.1), and the island
; is what is left between them - it costs nothing to draw the gap.
;
; EVERY COORDINATE IN THIS FILE IS DERIVED FROM ONE FRAME, so that a reader
; can check the map. Manhattan's long axis runs about 020 true (twenty degrees
; east of north), and the file was laid out in ISLAND coordinates - s along
; that axis, positive uptown, and c across it, positive east - and then turned
; into the world's:
;
;       x = 0.342 s + 0.940 c          s =  0.342 x + 0.940 z
;       z = 0.940 s - 0.342 c          c =  0.940 x - 0.342 z
;
; The near banks are quoted below in c, which is what makes the wedge checkable
; by hand: at midtown the island is 2,543 m across (the Hudson's near bank at
; c = -1325, the East River's at c = +1218) and the two banks CROSS at
; s = -5,610, which is the Battery, 5.6 km down the axis from the Empire State
; and (-1925, -5272) in the world. Compressed from the real 3.5 km width, and
; the Upper Bay south of the tip is left as ground: a third ribbon for the
; harbour would cost an object, and the wedge is what does the work.
;
; The airfield is JFK, out on Jamaica Bay south-east of the city. Runway 31
; really does point north-west at Manhattan and that is the whole reason to
; put an airport there in a flight simulator, so the runway's centre is at
; (4600, -3860) - 6,005 m from the Empire State on a bearing of EXACTLY 310,
; which is the runway's own heading. A take-off on 31 flies at the skyline
; head on, and the aeroplane starts the roll 6,845 m from the tower.
;
; The macros, the face tables, the shared filler models, the ribbon faces and
; the shared edge lists are csworld.inc's.
;
; THE ORDER IN THIS FILE IS FIXED: models, then the object table, then the
; names, then the location record - because CS_N_JFK is an equ over the
; table's own two labels and NASM cannot see them from above.
; =============================================================================

; --- the models --------------------------------------------------------------

; ONE FACE TABLE FOR ALL THREE TOWERS. The Empire State, the Chrysler and the
; Woolworth are the same thing three times over - a broad base, a setback, a
; shaft, and a crown drawn out to a point - so they are five-level stacks with
; identical topology and share these seventeen faces: four bands of wall and a
; cap. Sharing it is worth 238 bytes over a table each, and it is honest
; rather than a trick, because the three silhouettes differ only in their
; levels' numbers. Five levels and not six: the sixth would buy one more step
; in the crown and costs four vertices and four faces in every frame the tower
; is near, which is the wrong side of a budget that has to hold twelve frames
; a second on a 4.77 MHz 8088.
cs_f_jfk_twr:
    CS_SIDES 0,  CSI_WALL, CSI_WALL2
    CS_SIDES 4,  CSI_WALL, CSI_WALL2
    CS_SIDES 8,  CSI_WALL, CSI_WALL2
    CS_SIDES 12, CSI_WALL, CSI_WALL2
    CS_TOP  16,  CSI_ROOF

; THE EMPIRE STATE BUILDING, 1931: 381 m to the tip of the mooring mast, which
; is the height it held until an aerial was put on it in 1950. Its plot is
; 124 by 60 m, the five-storey base fills the whole of it, and the tower steps
; back at the sixth floor to the 68 by 40 m shaft that runs to the eighty-sixth
; at 320 m. The top level is 4 m across rather than a point, so the crown and
; the mast come to a spike and not to a needle that vanishes at range.
cs_m_jfk_esb: db CSM_STACK, 5, 17, 0, CSI_LINE, 0
    dw 382, .v, cs_f_jfk_twr, 0     ; the Euclidean radius, 88.5.11
.v: dw 62,   0, 30                  ; the plot
    dw 62,  45, 30                  ; the five-storey base
    dw 34,  62, 20                  ; the setback
    dw 34, 320, 20                  ; the shaft, to the observatory
    dw  4, 381,  4                  ; the crown and the mooring mast

; ...and the stand-in beyond CSO_LOD (88.5.4). It is the FIRST thing the
; player sees, from 6.8 km away at the start of the take-off roll, so its
; range is 16,000 and its near model would be twenty vertices of nothing:
; two levels tapering to the same tip draw the same sliver for eight.
cs_m_jfk_esbf: db CSM_STACK, 2, 5, 0, CSI_LINE, 0
    dw 382, .v, cs_f_box, 0
.v: dw 44,   0, 24
    dw  8, 381,  8

; THE CHRYSLER BUILDING, 1930: 319 m, and the only other spire in this world
; worth its own model. It stands 900 m from the Empire State on a bearing of
; 050 - island (s 780, c 450), which is Forty-Second and Lexington - so from
; the start of the roll the two of them are seven and a half degrees apart and
; read as one skyline with two peaks. A 60 m square plot, and the stainless
; crown is the taper from the sixty-first floor to the spire.
cs_m_jfk_chry: db CSM_STACK, 5, 17, 0, CSI_LINE, 0
    dw 320, .v, cs_f_jfk_twr, 0     ; the Euclidean radius, 88.5.11
.v: dw 30,   0, 30
    dw 30,  62, 30                  ; the base
    dw 22,  78, 22                  ; the setback
    dw 22, 240, 22                  ; the shaft
    dw  3, 319,  3                  ; the arches and the spire
cs_m_jfk_chryf: db CSM_STACK, 2, 5, 0, CSI_LINE, 0
    dw 320, .v, cs_f_box, 0
.v: dw 24,   0, 24
    dw  6, 319,  6

; THE WOOLWORTH BUILDING, 1913: 241 m, and the tallest building in the world
; for the seventeen years before the Chrysler took it. It belongs in a 1931
; New York precisely because it is the one that was beaten, and it is four
; kilometres down the island at City Hall (s -4000, c -300), which puts it
; over the financial district where the player's approach passes to the left.
cs_m_jfk_wool: db CSM_STACK, 5, 17, 0, CSI_LINE, 0
    dw 242, .v, cs_f_jfk_twr, 0     ; the Euclidean radius, 88.5.11
.v: dw 32,   0, 32
    dw 32,  58, 32                  ; the base block
    dw 16,  70, 16                  ; the setback
    dw 16, 205, 16                  ; the tower
    dw  3, 241,  3                  ; the pinnacle
cs_m_jfk_woolf: db CSM_STACK, 2, 5, 0, CSI_LINE, 0
    dw 242, .v, cs_f_box, 0
.v: dw 26,   0, 26
    dw  5, 241,  5

; --- and the rest of the city is squares, which is what the budget wants ----
; Four anonymous shapes: a 90 m midtown block and a 140 m one for the
; financial district, which Moderate draws, and the 190 m slim tower and 55 m
; broad block the DENSE city is built from (88.13.1.1). Every one is under the
; Woolworth's 241, which is under the Chrysler's 319, which is under the
; Empire State's 381, and that ladder is the only thing the skyline has to
; say - a nameless block that out-tops a landmark would undo it.
CS_BOX cs_m_jfk_mid, 45,  90, 30    ; 90 by 60 m, twenty-odd storeys
CS_BOX cs_m_jfk_dtn, 35, 140, 35    ; 70 by 70 m, a Wall Street tower
CS_BOX cs_m_jfk_hi,  26, 190, 26    ; 52 by 52 m, a slim one for the spires
CS_BOX cs_m_jfk_lo,  55,  55, 40    ; 110 by 80 m, the floor between them

; --- the two rivers, four ribbons and four centrelines -----------------------
; Seven stations 1,600 m apart down the island axis, at s = -6000, -4400,
; -2800, -1200, +400, +2000 and +3600, and a bank's c at each of them. Each
; river is cut in two at the s = -1200 station so that the pieces meet exactly
; and neither is longer than the cull can usefully reach round; each piece is
; four stations down the near bank and four down the far one, so the quads are
; (i, i+1, i+5, i+4), which is what csworld.inc's shared cs_f_rib3 indexes.
; The vertices are offsets from the object's own origin, which is the piece's
; centroid rounded to 10 m - a river 6 km from the runway must not be eight
; absolute coordinates the cull has to reach for.
;
; The Hudson's near bank runs c = +200, -650, -1150, -1280, -1340, -1380,
; -1400 and its far bank is 900 m west of that; the East River's near bank
; runs c = -200, +600, +1080, +1180, +1230, +1260, +1280 and its far bank is
; 620 m east. The two near banks therefore CROSS between the first two
; stations, and the crossing is the Battery: south of it the two ribbons
; overlap, which costs nothing (they are the same ink and both CSF_NOCULL)
; and gives the harbour continuous water for free.
;
; Every piece has a far model past CSO_LOD and that is the single biggest
; lever in the file (88.6.1): the centreline is four vertices and three
; segments where the ribbon is eight vertices and three filled quads, and
; from the runway all four pieces are centrelines.

cs_m_jfk_hud0: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0   ; the Hudson, the Battery
    dw 2790, .v, cs_f_rib3, 0                       ; to the Chelsea piers
.v: dw   466, -2727                 ; near bank, s = -6000 .. -1200
    dw   214,  -932
    dw   292,   742
    dw   717,  2290
    dw  -380, -2419                 ; far bank, the New Jersey shore
    dw  -631,  -625
    dw  -554,  1050
    dw  -129,  2598
cs_m_jfk_hud0f: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 2600, .v, 0, cs_e_road3
.v: dw    43, -2573
    dw  -209,  -778
    dw  -131,   896
    dw   294,  2444

cs_m_jfk_hud1: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0   ; ...and on past Central
    dw 2480, .v, cs_f_rib3, 0                       ; Park to Harlem
.v: dw  -333, -2430
    dw   158,  -906
    dw   667,   611
    dw  1196,  2122
    dw -1179, -2122
    dw  -688,  -598
    dw  -178,   919
    dw   350,  2430
cs_m_jfk_hud1f: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 2430, .v, 0, cs_e_road3
.v: dw  -756, -2276
    dw  -265,  -752
    dw   244,   765
    dw   773,  2276

cs_m_jfk_est0: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0   ; the East River, the
    dw 2690, .v, cs_f_rib3, 0                       ; Battery to the bridges
.v: dw -1920, -1850
    dw  -621,  -620
    dw   377,   719
    dw  1018,  2189
    dw -1337, -2062                 ; far bank, the Brooklyn shore
    dw   -38,  -832
    dw   960,   507
    dw  1601,  1977
cs_m_jfk_est0f: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 2570, .v, 0, cs_e_road3
.v: dw -1629, -1956
    dw  -330,  -726
    dw   669,   613
    dw  1310,  2083

cs_m_jfk_est1: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0   ; ...and on past midtown
    dw 2450, .v, cs_f_rib3, 0                       ; with Queens beyond it
.v: dw -1162, -2131
    dw  -567,  -645
    dw     8,   848
    dw   574,  2345
    dw  -579, -2343
    dw    15,  -857
    dw   591,   636
    dw  1157,  2133
cs_m_jfk_est1f: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 2420, .v, 0, cs_e_road3
.v: dw  -870, -2237
    dw  -276,  -751
    dw   299,   742
    dw   865,  2239

; --- the two bridges over the East River -------------------------------------
; A line each, in the road ink, and NOT filled: a bridge deck at map scale is
; a stroke across the water, and a filled one would be a ribbon that fights
; the river underneath it for the same rows. Two vertices and one segment
; is also why neither carries a far model even though its radius is over
; 400 m: there is nothing cheaper to fall back TO, and this is already the
; floor a far model would be trying to reach.
cs_m_jfk_bbr: db CSM_FLAT, 2, 0, 1, CSI_MARK, 0     ; the Brooklyn Bridge, 1883
    dw 510, .v, 0, cs_e_road1
.v: dw  -393,   299                 ; the Manhattan anchorage, at City Hall
    dw   388,  -305                 ; ...and the Brooklyn one
cs_m_jfk_mbr: db CSM_FLAT, 2, 0, 1, CSI_MARK, 0     ; the Manhattan Bridge,
    dw 505, .v, 0, cs_e_road1                       ; 1909, 700 m upstream
.v: dw  -388,   298
    dw   384,  -302

; --- the world ---------------------------------------------------------------
; THIRTY-ONE objects, and the last twelve are CSO_DENSE. Nineteen of them are
; the New York every rung below High draws: four are CSO_POI, so Detail Level
; = Low leaves the three towers and the Brooklyn Bridge, a city a player can
; still navigate by, and six are anonymous blocks and sheds that come in at
; Moderate with the rest. The TWELVE stand only at High (88.13.1.1), which is
; the rung for a machine faster than the 4.77 MHz 8088 this is costed on.
;
; THE RANGES OF THE NINETEEN ARE CUT SO THAT NO MORE THAN FIFTEEN OF THEM ARE
; EVER IN ONE FRAME. With the dense city the peak is TWENTY-SEVEN, which is
; what tests/unit/t_csworlds.py reads, and CS_NVIS is 32 with the
; thirty-third dropped SILENTLY - so this world has five objects of headroom
; and no more, on any CPU. That ceiling is not a speed budget and a faster
; machine does not raise it: the next thing added to Manhattan has to take
; range off something or be counted against those five.
;
; From the start of the take-off roll TWELVE of the nineteen are in range, and
; only the two field buildings and the two financial-district blocks draw a
; near model: 72 vertices. Over midtown FOURTEEN are in range for 116, against
; Paris' twelve and 160 ms. On Hercules, Moderate against High: 165.0 -> 200.5
; ms on the roll, 121.0 -> 180.1 at the take-off point, 140.1 -> 232.8 at
; 4 km, 170.2 -> 278.9 at 2 km and 116.6 -> 142.6 over midtown.
;
; The anonymous city is 2,500 to 3,000 like Paris', the towers hand over to
; their far models at 3,000 and the rivers at the same, and the three towers'
; ranges are set from the SPAWN POINT so that the skyline is there before the
; aeroplane moves: the Empire State is 9,643 m of Manhattan distance from it,
; the Chrysler 9,533 and the Woolworth 7,633.
cs_ot_jfk:
    CS_OBJ cs_m_jfk_esb,  cs_m_jfk_esbf,      0,     0,   0, 16000, 3000, cs_n_jfk_esb,  CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_jfk_chry, cs_m_jfk_chryf,   690,   580,   0, 14000, 3000, cs_n_jfk_chry, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_jfk_wool, cs_m_jfk_woolf, -1650, -3660,   0, 12000, 3000, cs_n_jfk_wool, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_jfk_mid,  0,               -390,   460,   0,  7000,    0, cs_n_jfk_mid,  CSO_COLLIDE
    CS_OBJ cs_m_jfk_mid,  0,                350,  -680,   0,  7000,    0, cs_n_jfk_mid,  CSO_COLLIDE
    CS_OBJ cs_m_jfk_dtn,  0,               -990, -3310,   0,  8000,    0, cs_n_jfk_dtn,  CSO_COLLIDE
    CS_OBJ cs_m_jfk_dtn,  0,              -1890, -3940,   0,  8000,    0, cs_n_jfk_dtn,  CSO_COLLIDE
    CS_OBJ cs_m_jfk_hud0, cs_m_jfk_hud0f, -2330, -2980,   0, 11000, 3000, cs_n_jfk_hud,  CSO_TERRAIN
    CS_OBJ cs_m_jfk_hud1, cs_m_jfk_hud1f, -1280,  1740,   0, 11000, 3000, cs_n_jfk_hud,  CSO_TERRAIN
    CS_OBJ cs_m_jfk_est0, cs_m_jfk_est0f,  -320, -3720,   0, 11000, 3000, cs_n_jfk_east, CSO_TERRAIN
    CS_OBJ cs_m_jfk_est1, cs_m_jfk_est1f,  1860,   600,   0, 11000, 3000, cs_n_jfk_east, CSO_TERRAIN
    CS_OBJ cs_m_jfk_bbr,  0,               -500, -4390,   0,  8000,    0, cs_n_jfk_bbr,  CSO_POI | CSO_ROAD
    CS_OBJ cs_m_jfk_mbr,  0,                -50, -3810,   0,  8000,    0, cs_n_jfk_mbr,  CSO_ROAD
    CS_OBJ cs_m_hangar,   0,               4900, -3680,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_shed,     0,               4490, -3340,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block,    0,                680, -5140,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block2,   0,               2170, -1640,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_small,    0,               1180,  2660,   0,  2500,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block,    0,               -110,  1740,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    ; --- THE DENSE CITY (SPEC.md 88.13.1.1): twelve more, CSO_DENSE, so
    ;     Detail Level = High is the only rung that draws one and the picture
    ;     every other rung gives is the New York that always shipped. They sit
    ;     on the ISLAND'S OWN AXIS - the line from the Empire State to the
    ;     Woolworth, which is (-0.41, -0.91) - offset across it by no more
    ;     than 430 m; every footprint keeps 60 m of daylight from every other
    ;     and from all eleven that were here already, and 150 m from the
    ;     WATER - the twelfth was first placed at (-2058,-4130), which is
    ;     the Battery and is IN the Hudson, and tests/unit/t_csworld.py is
    ;     what said so rather than anybody's eye. Each range is its own
    ;     Manhattan distance FROM THE SPAWN plus 400 m, rounded up to 500,
    ;     so the whole skyline is standing
    ;     before the take-off roll starts rather than arriving under the
    ;     aeroplane - which is the entire point of the rung.
    CS_OBJ cs_m_jfk_hi,   0,                625,   901,   0, 10500,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_mid,  0,                  8,   675,   0, 11000,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_lo,   0,                527,   124,   0, 10000,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_mid,  0,               -306,   269,   0, 11000,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_hi,   0,                148,  -351,   0, 10000,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_mid,  0,               -660,  -688,   0, 10500,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_lo,   0,               -226, -1376,   0,  9000,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_mid,  0,               -893, -1568,   0,  9500,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_dtn,  0,              -1064, -3090,   0,  8500,    0, cs_n_jfk_dtn,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_hi,   0,              -1817, -3232,   0,  9000,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_dtn,  0,              -1363, -3852,   0,  8000,    0, cs_n_jfk_dtn,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_jfk_lo,   0,              -1678, -4039,   0,  8000,    0, cs_n_jfk_mid,  CSO_COLLIDE | CSO_DENSE
cs_ote_jfk:
CS_N_JFK equ (cs_ote_jfk - cs_ot_jfk) / CSO_SIZE

; --- the names the crash line uses -------------------------------------------
cs_n_jfk_esb:  db 'THE EMPIRE STATE BUILDING', 0
cs_n_jfk_chry: db 'THE CHRYSLER BUILDING', 0
cs_n_jfk_wool: db 'THE WOOLWORTH BUILDING', 0
cs_n_jfk_mid:  db 'MIDTOWN', 0
cs_n_jfk_dtn:  db 'THE FINANCIAL DISTRICT', 0
cs_n_jfk_hud:  db 'THE HUDSON RIVER', 0
cs_n_jfk_east: db 'THE EAST RIVER', 0
cs_n_jfk_bbr:  db 'THE BROOKLYN BRIDGE', 0
cs_n_jfk_mbr:  db 'THE MANHATTAN BRIDGE', 0

; --- the location ------------------------------------------------------------
; The runway is 6,005 m from the Empire State on a bearing of 310, which is
; its own heading, so runway 31 points at the tower and nothing else has to be
; arranged. In island coordinates the field is at (s -2054, c +5638), which is
; 3.9 km east of the East River's far bank: dry land, out on Jamaica Bay's
; side of Brooklyn, and the nearer of the two field buildings keeps 3,527 m
; from the nearest water this world draws.
;
; 1,800 m of runway rather than JFK's real 4,400: CSA_HLEN is a divisor in
; cs3d.inc and csflight.inc and wants to stay in 250..900, and 900 is the top
; of that band. The Cessna rotates well inside it. The two field buildings
; stand 330 m off the centreline, which leaves the nearer of them 243 m of
; daylight from the runway rectangle against the 150 m the world rules ask.
cs_a_jfk:
    dw cs_s_jfk                     ; CSA_NAME
    dw 4600, -3860                  ; CSA_X, CSA_Z: 6 km south-east of the
    dw 0                            ; CSA_ELEV      Empire State Building
    dw 56434                        ; CSA_HDG       310, straight at it
    dw 900                          ; CSA_HLEN      an 1,800 m runway...
    dw 22                           ; CSA_HWID      ...44 m wide
    dw cs_s_rwy_jfk                 ; CSA_RWY
    dw -840                         ; CSA_SPAWN     60 m in from the threshold,
                                    ;               6,845 m out from the tower
    dw cs_ot_jfk, CS_N_JFK          ; CSA_OBJS, CSA_NOBJ
    dw -1299, -5061                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 10923                        ; CSA_WHDG    (88.7.7) - THE HUDSON RIVER
    dw 1000                          ; CSA_WLEN    half its length...
    dw 40                           ; CSA_WWID    ...and half its width
    dw cs_n_jfk_hud              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
cs_s_jfk: db 'NYC-JFK', 0
cs_s_rwy_jfk: db 'RUNWAY 31', 0
