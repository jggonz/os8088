; =============================================================================
; os8088 - apps/skies/csw_sfo.inc
;
; SAN FRANCISCO (SPEC.md 88.6.4) - the world SANFAN-SFO stands in. Metres, x east,
; z north, y up; THE ORIGIN IS THE MIDDLE OF THE GOLDEN GATE BRIDGE'S DECK,
; mid-strait. The strait runs east and west and the bridge crosses it north
; and south, so the whole world is laid out about that one cross.
;
; THE BRIDGE IS THE POINT OF THIS WORLD, and it is built as GEOMETRY rather
; than as a picture, because the one thing this simulator can offer that a
; photograph cannot is flying under it. It is four objects and 52 of the
; table's 172 vertices: a tower each side of the strait, the deck between
; them, and the two main cables. Everything else is there to frame it - the
; strait with a headland either side, so that the gate reads as a gate; the
; city off to the south-east so that there is somewhere to fly to; and
; Alcatraz on its rock, which is a famous silhouette for two objects.
;
; THE TOWERS COLLIDE AND THE DECK DOES NOT, and that is the whole design
; rather than an oversight. csflight.inc's cs_collide takes an object's first
; level as a footprint and its tallest level's height as a ceiling, so what it
; tests is a BOX FROM THE GROUND UP and not the shape of the model: a
; collidable deck 80 m in the air would be a solid wall from the water to the
; roadway, and flying under the bridge would be a crash. The deck and the
; cables are therefore scenery the aeroplane passes through, the two towers
; are what kill, and the arch between them is open. It is also why the deck
; may lie across the water at all: tests/unit/t_csworld.py holds only
; COLLIDABLE footprints against the river polygons, and its own comment names
; a bridge over the water as the case a corner-only test would have missed.
;
; THE GEOGRAPHY IS COMPRESSED, on 88.6's rule that a location is a runway and
; the country round it rather than a map. The real main span is 1,280 m and
; SFO is 19 km down the peninsula; here the span is 800 m and the runway is
; 4.2 km from the bridge on a bearing of 130, so that RUNWAY 31's take-off run
; points straight up the strait and the bridge stands 5.0 km ahead of the
; aeroplane at reset, which is the first thing in the windscreen and stays
; there for the whole climb.
;
; THE TOWERS STAND ON THE SHORE AND NOT IN THE STRAIT, which is the one place
; this world departs from the real bridge on purpose. Both towers really stand
; in the water; a collidable base in water fails t_csworld.py, so the strait's
; banks are drawn 345 m either side of the centreline and the towers at 400 m,
; leaving 35 m of daylight at the north tower and 43 at the south. Narrowing
; the water was the cheaper of the two fixes: widening the span instead would
; have made the deck a needle at the range the bridge is first seen from.
;
; The macros, the face tables, the shared filler models, the ribbon faces and
; the shared edge lists are csworld.inc's.
;
; THE ORDER IN THIS FILE IS FIXED: models, then the object table, then the
; names, then the location record - because CS_N_SFO is an equ over the
; table's own two labels and NASM cannot see them from above.
; =============================================================================

; --- the models --------------------------------------------------------------

; A three-level tower wants eight side faces and a cap, and no shared table is
; that shape: cs_f_dome is the eight sides with no top and cs_f_sacre's top is
; on the wrong level. Nine faces of its own is 63 bytes and it is the only
; model in the world that is looked DOWN on from close to, because the
; aeroplane spends its time between the towers rather than beyond them.
cs_f_sfo_twr:
    CS_SIDES 0, CSI_WALL, CSI_WALL2
    CS_SIDES 4, CSI_WALL, CSI_WALL2
    CS_TOP 8, CSI_ROOF

; --- the tower: 227 m, which is the real one's height above the water -------
; Slim and barely tapered, because the real tower is a PORTAL straddling a
; 27 m roadway and so is nearly as wide at the top as at the bottom - and
; because the cables are hung at x = 17, which only meets the tower if the top
; is still 15 wide. At the 5 km the aeroplane starts from it is three pixels
; across and twenty tall, which is what a tower is meant to look like there.
; The radius is 235 rather than the macros' |wx| + |wz| + h/2 (141): a slim
; 227 m column's true reach is its height, and the headline landmark is the
; one object worth paying the cull's margin for, as Paris' tower does at 330.
cs_m_sfo_twr: db CSM_STACK, 3, 9, 0, CSI_LINE, 0
    dw 235, .v, cs_f_sfo_twr, 0
.v: dw 18,   0, 12
    dw 16, 110,  9
    dw 15, 227,  7

; --- the deck: 1,400 m of roadway, 30 m wide, 6 m thick ---------------------
; A BOX and not a ribbon, although a ribbon is what a road wants, because
; cs_scene draws every CSM_FLAT model first and every CSM_STACK after
; (the ground shapes, then the solids): a ribbon lifted to 80 m would be
; painted before the towers whatever the depth sort said, and a tower behind
; the deck would then be drawn over it. A box sorts with them.
; It reaches 700 m either side of the middle so that both ends land on dry
; ground - the strait's banks are at 345 - and the object carries the 80 m in
; CSO_Y rather than in the model, so that CSO_Y means the base height here
; exactly as it does everywhere else.
CS_BOX cs_m_sfo_deck, 15, 6, 700

; Beyond CSO_LOD the deck is ONE SEGMENT down its centreline (88.6.1): a
; single level whose four corners collapse onto the axis, so 0 to 3 is the
; whole roadway for four transformed vertices instead of eight and one
; segment instead of five faces. h = 3 puts the line through the middle of
; the 6 m slab, and CSO_Y lifts it with the near model.
cs_m_sfo_deckf: db CSM_STACK, 1, 0, 1, CSI_MARK, 0
    dw 718, .v, 0, .e
.v: dw 0, 3, 700
.e: db 0,3

; --- the two main cables ----------------------------------------------------
; A CSM_FLAT model cannot hold a cable, because its vertices are (x, z) pairs
; ON THE GROUND and a cable's whole character is its height. A CSM_STACK can:
; a level is (wx, h, wz) with h an explicit height, and its four corners are
; (+-wx, h, +-wz) - which places FOUR cable stations at one level, at both
; x = +-17 and both z = +-wz. That is exactly the symmetry a catenary has
; about mid-span, so five levels are five heights and ten stations down each
; of the two cables, in 20 of the 24 vertices a model may have.
;
; The sag is the parabola y = 88 + 139 (z/400)^2, which is 227 at the tower
; and 88 at mid-span: 88 rather than the deck's own 80 so that the cable
; passes just OVER the 6 m slab instead of through it, there being no depth
; buffer to sort one object's line against another object's face. The stations
; are at |z| = 400, 300, 200, 100 and 25 - closest together where the curve
; bends most, which is what makes four segments read as a curve at all.
cs_m_sfo_cbl: db CSM_STACK, 5, 0, 18, CSI_LINE, 0
    dw 530, .v, 0, .e
.v: dw 17, 227, 400
    dw 17, 166, 300
    dw 17, 123, 200
    dw 17,  97, 100
    dw 17,  88,  25
.e: db 0,4, 4,8, 8,12, 12,16         ; the east cable, north tower to mid-span
    db 16,19                         ; ...across the middle...
    db 19,15, 15,11, 11,7, 7,3       ; ...and out to the south tower
    db 1,5, 5,9, 9,13, 13,17         ; the west cable, the same stations at
    db 17,18                         ; x = -17
    db 18,14, 14,10, 10,6, 6,2

; The far cable is each cable as a straight V, tower top to mid-span: two
; levels, the lower one with wz = 0 so its four corners collapse into two, and
; four segments for the pair. A wireframe model never takes cs_boxlod's
; one-rectangle path - that is for solids with faces - so without this the
; bridge would be transforming 20 vertices and drawing 18 segments from the
; far end of the world.
cs_m_sfo_cblf: db CSM_STACK, 2, 0, 4, CSI_LINE, 0
    dw 530, .v, 0, .e
.v: dw 17, 227, 400
    dw 17,  88,   0
.e: db 0,4, 3,4, 1,5, 2,5

; --- the water: four ribbons, west to east, each with its centreline --------
; The Pacific outside the gate, the strait under the bridge, and the bay east
; of it in TWO pieces with Alcatraz in the lens between them. Splitting the
; bay is what lets the rock be collidable: an island really is water either
; side of a piece of ground, and drawing it that way is cheaper than the
; alternative, which is a rock the aeroplane flies through. Each piece is
; six vertices and two quads near and its three-station centreline far
; (88.6.1), and consecutive pieces share their end stations so the banks join
; without a step.

; The Pacific, widening south-west from the gate. Its east end is the gate
; ribbon's west end.
cs_m_sfo_ocn: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 2203, .v, cs_f_rib2, 0
.v: dw -1400, -1700
    dw     0,  -950
    dw  1400,  -152
    dw -1400,  1100
    dw     0,   840
    dw  1400,   562
cs_m_sfo_ocnf: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 2100, .v, 0, cs_e_road2
.v: dw -1400, -300
    dw     0,  -55
    dw  1400,  205

; The strait itself, pinched to 690 m across under the bridge so that both
; towers stand on land (see the header). This is the piece the aeroplane flies
; along, so it is the one with the longer range.
cs_m_sfo_gate: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1105, .v, cs_f_rib2, 0
.v: dw -800, -352
    dw    0, -338
    dw 1000, -430
    dw -800,  362
    dw    0,  344
    dw 1000,  470
cs_m_sfo_gatef: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1100, .v, 0, cs_e_road2
.v: dw -800, 5
    dw    0, 3
    dw 1000, 20

; The bay south of Alcatraz, along the city's waterfront.
cs_m_sfo_bays: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1900, .v, cs_f_rib2, 0
.v: dw -1600, -130
    dw     0, -420
    dw  1600, -520
    dw -1600,  770
    dw     0,  450
    dw  1600, 1000
cs_m_sfo_baysf: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1900, .v, 0, cs_e_road2
.v: dw -1600, 320
    dw     0,  15
    dw  1600, 240

; ...and the bay north of it, under the Marin shore.
cs_m_sfo_bayn: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1900, .v, cs_f_rib2, 0
.v: dw -1600, -650
    dw     0, -420
    dw  1600, -250
    dw -1600,    0
    dw     0,  500
    dw  1600,  850
cs_m_sfo_baynf: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1900, .v, 0, cs_e_road2
.v: dw -1600, -325
    dw     0,   40
    dw  1600,  300

; --- the land either side of the gate: three frustums in the hill ink -------
; A hill needs no far model even at 12 km, which looks like it breaks the
; rule about a radius over 400 m and is the same exception Paris' Montmartre
; takes: a FACED solid under about six pixels across is drawn by cs_boxlod as
; ONE rectangle off three projected points, so a distant frustum already
; costs three multiplies rather than eight transforms. A wireframe gets no
; such path, which is why the cables have a far model and these do not.
CS_HILL cs_m_sfo_marw, 620, 430, 265, 180, 130    ; the Marin headlands, the
                                                  ; high ground the bridge
                                                  ; points at from the runway
CS_HILL cs_m_sfo_mare, 420, 300, 175, 140, 100    ; the ridge above Lime Point,
                                                  ; east of the north tower
CS_HILL cs_m_sfo_pres, 420, 240, 105, 190, 110    ; the Presidio bluff, west of
                                                  ; the south tower and clear
                                                  ; of the deck's own line so
                                                  ; the road does not vanish
                                                  ; into it
CS_HILL cs_m_sfo_rock, 130, 115,  42,  75,  65    ; Alcatraz, in the gap
                                                  ; between the two bay pieces

; --- what stands on the rock and in the city --------------------------------
CS_BOX cs_m_sfo_pen,   55,  16,  22     ; the cellhouse: long, low and wider
                                        ; than it is deep, which is the shape
                                        ; that makes the rock read as Alcatraz
CS_PYR cs_m_sfo_tram,  22, 220,  22     ; the Transamerica Pyramid, 220 m on a
                                        ; 44 m base: the one downtown shape
                                        ; that is recognisable in outline, so
                                        ; it is the city's point of interest
CS_BOX cs_m_sfo_dtwr,  22, 270,  22     ; and the tall flat-topped tower beside
                                        ; it, for something to judge it against

; --- the world ---------------------------------------------------------------
; TWENTY-EIGHT objects and the last eight are CSO_DENSE, so twenty of them are
; the San Francisco every rung below High draws. Twenty objects, 172 vertices. About eleven of them are in the frame on the
; take-off run: the four of the bridge, the Pacific, the strait, the three
; hills and one or two of the bay pieces, with the city 49 degrees off the
; nose and outside the view. Most of those are past their CSO_LOD at 5 km, so
; what the first frame actually transforms is two towers, two far models and
; three centrelines.
cs_ot_sfo:
    CS_OBJ cs_m_sfo_twr,  0,                  0,   400,   0, 16000,    0, cs_n_sfo_gg,   CSO_COLLIDE | CSO_POI | CSO_ROAD
    CS_OBJ cs_m_sfo_twr,  0,                  0,  -400,   0, 16000,    0, cs_n_sfo_gg,   CSO_COLLIDE | CSO_POI | CSO_ROAD
    CS_OBJ cs_m_sfo_deck, cs_m_sfo_deckf,     0,     0,  80, 16000, 2500, cs_n_sfo_gg,   CSO_POI | CSO_ROAD
    CS_OBJ cs_m_sfo_cbl,  cs_m_sfo_cblf,      0,     0,   0, 16000, 2200, cs_n_sfo_gg,   CSO_POI | CSO_ROAD
    CS_OBJ cs_m_sfo_ocn,  cs_m_sfo_ocnf,  -1800,  -200,   0,  5000, 2600, cs_n_sfo_gate, CSO_TERRAIN
    CS_OBJ cs_m_sfo_gate, cs_m_sfo_gatef,   400,     0,   0,  5000, 2600, cs_n_sfo_gate, CSO_TERRAIN
    CS_OBJ cs_m_sfo_bays, cs_m_sfo_baysf,  3000,  -300,   0,  4500, 2600, cs_n_sfo_bay,  CSO_TERRAIN
    CS_OBJ cs_m_sfo_bayn, cs_m_sfo_baynf,  3000,  1150,   0,  4500, 2600, cs_n_sfo_bay,  CSO_TERRAIN
    CS_OBJ cs_m_sfo_marw, 0,              -1150,  1200,   0, 12000,    0, cs_n_sfo_mar,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_sfo_mare, 0,                900,  1150,   0, 10000,    0, cs_n_sfo_mar,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_sfo_pres, 0,               -500,  -950,   0,  9000,    0, cs_n_sfo_pres, CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_sfo_rock, 0,               2950,   430,   0,  7000,    0, cs_n_sfo_alc,  CSO_COLLIDE | CSO_TERRAIN
    CS_OBJ cs_m_sfo_pen,  0,               2950,   430,  42,  6000,    0, cs_n_sfo_alc,  CSO_COLLIDE
    CS_OBJ cs_m_sfo_tram, 0,               3770, -1010,   0, 12000,    0, cs_n_sfo_tram, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_sfo_dtwr, 0,               3980, -1180,   0,  9000,    0, cs_n_sfo_dt,   CSO_COLLIDE
    CS_OBJ cs_m_block,    0,               3560, -1320,   0,  4000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block2,   0,               4080,  -900,   0,  4000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_hangar,   0,               2866, -2942,   0,  2600,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_shed,     0,               2560, -2685,   0,  2600,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_small,    0,               3211, -3231,   0,  2600,    0, cs_n_bldg,     CSO_COLLIDE
    ; --- THE HIGH TIER (SPEC.md 88.13.1.3): eight more, CSO_DENSE, so Detail
    ;     Level = High is the only rung that draws one and every rung below it
    ;     gives the San Francisco that always shipped.
    ;
    ;     THE CITY IS WHERE THE TRADE WAS TAKEN. This world spends 52 of its
    ;     172 vertices on the bridge, which is right - it is what the location
    ;     is for - and downtown is left as two towers and two blocks, there to
    ;     be somewhere to fly to. But the Financial District is a TIGHT
    ;     cluster on a peninsula, and two towers 400 m apart read as two
    ;     towers rather than as a city; from the Marin side, which is where a
    ;     RUNWAY 31 departure ends up, the whole of it is one silhouette.
    ;
    ;     So six fill the district round the Transamerica and the flat-topped
    ;     tower, and two more stand down towards the Embarcadero, which is the
    ;     side the bay is on and the side the aeroplane sees. Nothing
    ;     anonymous passes 180 m, under the Transamerica's 220 and the tower
    ;     beside it at 270; every footprint keeps 60 m of daylight from every
    ;     other, 80 m from the bay and 60 m from the runway rectangle.
    ;
    ;     Ranges are each one's own Manhattan distance from the spawn plus
    ;     400 m rounded up to 500 - 3,500 to 4,500 here, which is short
    ;     because the city is close: the runway is on the bay shore 1.7 km
    ;     south of it, so the cluster is standing on the roll.
    CS_OBJ cs_m_tower,    0,               3710, -1300,   0,  4000,    0, cs_n_sfo_fidi, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,               3900, -1520,   0,  3500,    0, cs_n_sfo_fidi, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,    0,               4150, -1150,   0,  3500,    0, cs_n_sfo_fidi, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,               3560, -1000,   0,  4500,    0, cs_n_sfo_fidi, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,               4200, -1420,   0,  3500,    0, cs_n_sfo_fidi, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,    0,               3830,  -880,   0,  4000,    0, cs_n_sfo_fidi, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,               3350,  -860,   0,  4500,    0, cs_n_sfo_emb,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,               3300, -1500,   0,  4000,    0, cs_n_sfo_emb,  CSO_COLLIDE | CSO_DENSE
cs_ote_sfo:
CS_N_SFO equ (cs_ote_sfo - cs_ot_sfo) / CSO_SIZE

; --- the names the crash line uses -------------------------------------------
; The two water names are never spoken, water being uncollidable, and they are
; here because a table row with no name is a row a reader has to check.
cs_n_sfo_gg:   db 'THE GOLDEN GATE BRIDGE', 0
cs_n_sfo_gate: db 'THE GOLDEN GATE', 0
cs_n_sfo_bay:  db 'SAN FRANCISCO BAY', 0
cs_n_sfo_mar:  db 'THE MARIN HEADLANDS', 0
cs_n_sfo_pres: db 'THE PRESIDIO', 0
cs_n_sfo_alc:  db 'ALCATRAZ', 0
cs_n_sfo_tram: db 'THE TRANSAMERICA PYRAMID', 0
cs_n_sfo_dt:   db 'DOWNTOWN SAN FRANCISCO', 0
cs_n_sfo_fidi: db 'THE FINANCIAL DISTRICT', 0
cs_n_sfo_emb:  db 'THE EMBARCADERO', 0

; --- the location ------------------------------------------------------------
; The runway is 4,187 m from the bridge on a bearing of 130, and its heading
; is the reciprocal: 310.16 degrees, which is 56462 of the 65536 to the turn.
; The aeroplane is stood 60 m in from the near threshold, at (3804, -3209),
; and from there the bridge bears 310.2 - the runway's own heading to within a
; twentieth of a degree - at 4,977 m. So the gate is dead ahead of the
; take-off run and stays there.
cs_a_sfo:
    dw cs_s_sfo                     ; CSA_NAME
    dw 3200, -2700                  ; CSA_X, CSA_Z: 4.2 km south-east of the
    dw 0                            ; CSA_ELEV     bridge, down the peninsula
    dw 56462                        ; CSA_HDG      310
    dw 850                          ; CSA_HLEN     a 1,700 m runway...
    dw 22                           ; CSA_HWID     ...44 m wide
    dw cs_s_rwy_sfo                 ; CSA_RWY
    dw -790                         ; CSA_SPAWN    60 m in from the threshold
    dw cs_ot_sfo, CS_N_SFO          ; CSA_OBJS, CSA_NOBJ
    dw 3800, -572                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 16384                        ; CSA_WHDG    (88.7.7) - SAN FRANCISCO BAY
    dw 800                          ; CSA_WLEN    half its length...
    dw 100                           ; CSA_WWID    ...and half its width
    dw cs_n_sfo_bay              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
cs_s_sfo: db 'SANFAN-SFO', 0
cs_s_rwy_sfo: db 'RUNWAY 31', 0
