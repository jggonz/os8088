; =============================================================================
; os8088 - apps/skies/csw_mia.inc
;
; MIAMI (SPEC.md 88.6.4) - the world MIAMI-MIA stands in. Metres, x east,
; z north, y up; THE ORIGIN IS A POINT ON MIAMI BEACH'S ATLANTIC SHORE, at
; the latitude of the runway's centreline, and the sea is to the east of it.
;
; THE COASTLINE IS THE LANDMARK HERE, which is a different kind of thing to
; put in front of a player than a tower. A tower is an object; a coast is a
; SHAPE, and the shape that says Miami rather than any other warm shore is a
; BARRIER ISLAND: a long straight bar of sand with the open Atlantic on one
; side of it and a second, enclosed sheet of water on the other. So this
; world is built out of two bands of water and the strip of ground between
; them, and the strip is not drawn at all - it is what is LEFT when the
; Atlantic stops at x = SO(z) and Biscayne Bay starts again at x = SI(z).
; Water is the only thing modelled and the land is the gap, which is why the
; island costs this file nothing and reads from any height.
;
; Three causeways cross the bay. They are two segments of CSI_MARK each and
; they are the cheapest thing in the file, but they are what turns two
; parallel stripes into an island with a mainland behind it: a stripe of
; water nothing crosses could be a river.
;
; The take-off is the tour. RUNWAY 09 is 4.6 km west of the beach, so the
; climb-out crosses downtown at 2.2 km, the bay at 3.0, the island at 3.7 and
; then the open sea, in that order, and the player has seen the whole
; location before the first turn.
;
; THE SHORE CHAINS. Every water model in this file is cut from three chains
; of stations 1,500 m apart, from z = -6,000 to z = +6,000. SO is Miami
; Beach's Atlantic front, SI the same island's bay shore, SM the mainland's:
;
;      z    SO      SI      SM        island   bay
;  -6000   -330    -520   -3100         190   2580
;  -4500   -200    -700   -2900         500   2200
;  -3000   -110    -820   -2700         710   1880
;  -1500    -40    -880   -2480         840   1600
;      0      0    -860   -2350         860   1490
;  +1500    +20    -820   -2420         840   1600
;  +3000    +10    -770   -2620         780   1850
;  +4500    -70    -640   -2860         570   2220
;  +6000   -190    -480   -3050         290   2570
;
; The island is widest opposite downtown and tapers at both ends, and the bay
; is narrowest there and widens away from it. That is the real bay's shape
; and it is also what makes the picture legible: the two shores converge
; where the player crosses them and the eye reads a taper rather than two
; rails. The Atlantic front is nearly straight because it IS nearly straight;
; the shape of Miami Beach is in its WIDTH, not in its ocean edge.
;
; The macros, the face tables, the shared filler models and the shared edge
; lists are csworld.inc's.
;
; THE ORDER IN THIS FILE IS FIXED: models, then the object table, then the
; names, then the location record - because CS_N_MIA is an equ over the
; table's own two labels and NASM cannot see them from above.
; =============================================================================

; --- the models --------------------------------------------------------------

; A ribbon of FIVE stations a side (SPEC.md 88.6): four quads over ten
; vertices, (i, i+1, i+n+1, i+n) with n = 5. csworld.inc shares the one, two
; and three quad tables and stops there; the four big pieces of water in this
; world all want the same four, so they are declared once here and every one
; of them points at these twenty-eight bytes.
cs_f_mia_rib4:
    db 4, CSI_RIVER, CSF_NOCULL, 0, 1, 6, 5
    db 4, CSI_RIVER, CSF_NOCULL, 1, 2, 7, 6
    db 4, CSI_RIVER, CSF_NOCULL, 2, 3, 8, 7
    db 4, CSI_RIVER, CSF_NOCULL, 3, 4, 9, 8

; --- THE ATLANTIC, in three pieces (88.6.1) ----------------------------------
; The two INSHORE pieces carry the coastline itself: their west bank is the SO
; chain and their east bank is a straight line 2,800 m out. They are cut at
; z = 0 and they share that station's coordinates exactly, so the join is
; invisible and the two of them are 2.8 km of water and 12 km of beach.
;
; Cutting them was worth doing for the reason the Seine was cut, but not by
; the arithmetic anyone expects. A piece 2,800 m wide and 6,000 m long has a
; radius of 4,150, and the cull's cone drops an object when its "along" plus
; its radius is negative: standing on the runway facing WEST, sea1's along is
; -4,600, so 4,150 of radius leaves it 450 m behind the eye and the piece
; costs four multiplies instead of ten transforms. One undivided ocean would
; have a radius bigger than the world and could never be dropped at all.
;
; The far model of a coast piece is its SHORELINE and not its centreline. A
; river's centreline is the river; a coast's centreline is 1,400 m out to sea
; and means nothing. Five vertices and four segments either way.
cs_m_mia_sea1: db CSM_FLAT, 10, 4, 0, CSI_RIVER, 0
    dw 4150, .v, cs_f_mia_rib4, 0       ; origin (0, 3000), ON the beach, so
.v: dw     0, -3000                     ; the range and LOD are measured from
    dw    20, -1500                     ; the shore rather than from a point
    dw    10,     0                     ; out at sea
    dw   -70,  1500
    dw  -190,  3000
    dw  2800, -3000
    dw  2800, -1500
    dw  2800,     0
    dw  2800,  1500
    dw  2800,  3000
cs_m_mia_sea1f: db CSM_FLAT, 5, 0, 4, CSI_RIVLINE, 0
    dw 4150, .v, 0, cs_e_road4
.v: dw     0, -3000
    dw    20, -1500
    dw    10,     0
    dw   -70,  1500
    dw  -190,  3000
cs_m_mia_sea0: db CSM_FLAT, 10, 4, 0, CSI_RIVER, 0
    dw 4150, .v, cs_f_mia_rib4, 0       ; origin (0, -3000)
.v: dw  -330, -3000
    dw  -200, -1500
    dw  -110,     0
    dw   -40,  1500
    dw     0,  3000
    dw  2800, -3000
    dw  2800, -1500
    dw  2800,     0
    dw  2800,  1500
    dw  2800,  3000
cs_m_mia_sea0f: db CSM_FLAT, 5, 0, 4, CSI_RIVLINE, 0
    dw 4150, .v, 0, cs_e_road4
.v: dw  -330, -3000
    dw  -200, -1500
    dw  -110,     0
    dw   -40,  1500
    dw     0,  3000

; The OPEN Atlantic: everything from 2,800 m offshore to the far edge of what
; a 16-bit world can hold. Three stations and two quads, because six vertices
; is what the whole eastern half of the world is worth: there is nothing out
; there to draw, only water that has to REACH far enough not to end where the
; player can see it end.
;
; It does still end, and this is the one thing about this location that does
; not read perfectly. Its far bank is 10,900 to 11,400 m out, which is as far
; as coordinates go, and the Hercules row's scly is 285, so the far edge sits
; 285 x altitude / distance rows below the horizon: 7.8 rows at 300 m and
; 15.7 at 600, out of the 56 the ground gets. Near the horizon on a dithered
; 1bpp display that is a change of texture rather than a shoreline, and the
; bank is deliberately RAGGED - 11,400, 10,900, 11,300 - so that what is
; there is not a straight line either. A true sea horizon is not something
; this engine can be asked for; the ground plane has no ink of its own past
; the last polygon.
;
; No far model: the model IS six vertices, and the two the far one would save
; would be paid for by the ocean turning into a line.
cs_m_mia_open: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 7550, .v, cs_f_rib2, 0           ; origin (6900, 0)
.v: dw -4100, -6000                     ; the west bank meets the two inshore
    dw -4100,     0                     ; pieces at x = 2800 exactly
    dw -4100,  6000
    dw  4500, -6000
    dw  4000,     0
    dw  4400,  6000

; --- BISCAYNE BAY, in two pieces ---------------------------------------------
; The inner water, and the reason the strip east of it is an island rather
; than a beach. Its west bank is the SM chain and its east bank the SI chain,
; so both of its shores are real shores; the far model is therefore the
; CENTRELINE, which is what a piece of water with two banks reduces to.
; Cut at z = 0 on the shared station, like the sea.
cs_m_mia_bay1: db CSM_FLAT, 10, 4, 0, CSI_RIVER, 0
    dw 3320, .v, cs_f_mia_rib4, 0       ; origin (-1700, 3000)
.v: dw  -650, -3000
    dw  -720, -1500
    dw  -920,     0
    dw -1160,  1500
    dw -1350,  3000
    dw   840, -3000
    dw   880, -1500
    dw   930,     0
    dw  1060,  1500
    dw  1220,  3000
cs_m_mia_bay1f: db CSM_FLAT, 5, 0, 4, CSI_RIVLINE, 0
    dw 3320, .v, 0, cs_e_road4
.v: dw    95, -3000
    dw    80, -1500
    dw     5,     0
    dw   -50,  1500
    dw   -65,  3000
cs_m_mia_bay0: db CSM_FLAT, 10, 4, 0, CSI_RIVER, 0
    dw 3340, .v, cs_f_mia_rib4, 0       ; origin (-1700, -3000)
.v: dw -1400, -3000
    dw -1200, -1500
    dw -1000,     0
    dw  -780,  1500
    dw  -650,  3000
    dw  1180, -3000
    dw  1000, -1500
    dw   880,     0
    dw   820,  1500
    dw   840,  3000
cs_m_mia_bay0f: db CSM_FLAT, 5, 0, 4, CSI_RIVLINE, 0
    dw 3340, .v, 0, cs_e_road4
.v: dw  -110, -3000
    dw  -100, -1500
    dw   -60,     0
    dw    20,  1500
    dw    95,  3000

; --- GOVERNMENT CUT: the ship channel through the island ----------------------
; 220 m of water crossing the bar at z = -3,750, where the island is 760 m
; wide. It is the only place in this world where the two sheets of water are
; joined, and it is what makes the bar read as an ISLAND at a glance rather
; than as a spit running off the bottom of the view. Both ends deliberately
; overrun the shores they meet - 200 m into the bay, 315 m into the sea - so
; that no rounding can leave a thread of green across the channel; the overlap
; is drawn twice in one ink and costs two short rows.
cs_m_mia_cut: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 580, .v, cs_f_rib2, 0            ; origin (-400, -3750)
.v: dw  -560,  -110
    dw     0,  -105
    dw   560,  -100
    dw  -560,   110
    dw     0,   105
    dw   560,   100
cs_m_mia_cutf: db CSM_FLAT, 2, 0, 1, CSI_RIVLINE, 0
    dw 580, .v, 0, cs_e_road1
.v: dw  -560,     0
    dw   560,     0

; --- the three causeways ------------------------------------------------------
; Three vertices and two segments each, which is already what a far model
; would be, so none of them has one. Each runs from a point ON the mainland,
; through a bend at a spoil island in mid-bay, to a point ON the beach: the
; ends overrun both shores by 60 to 100 m for the reason the cut's do, and a
; causeway that stopped short of the land would read as a jetty.
cs_m_mia_cw0: db CSM_FLAT, 3, 0, 2, CSI_MARK, 0
    dw 1040, .v, 0, cs_e_road2          ; origin (-1800, -2580)
.v: dw  -920,   -20
    dw     0,    30
    dw  1010,   -20
cs_m_mia_cw1: db CSM_FLAT, 3, 0, 2, CSI_MARK, 0
    dw 860, .v, 0, cs_e_road2           ; origin (-1600, -260), the one the
.v: dw  -840,   -20                     ; climb-out crosses
    dw     0,    30
    dw   790,   -20
cs_m_mia_cw2: db CSM_FLAT, 3, 0, 2, CSI_MARK, 0
    dw 980, .v, 0, cs_e_road2           ; origin (-1700, 2420)
.v: dw  -920,   -20
    dw     0,    30
    dw   960,   -20

; --- downtown, on the mainland shore -----------------------------------------
; Three towers of their own and three of csworld.inc's anonymous blocks. The
; tallest is four levels on cs_f_sacre's thirteen faces: a 26 m podium, a
; setback to a 160 m shaft and a 196 m crown, which is Miami's tallest
; compressed from 233. Its radius is 210, written here as a
; local fix when CS_BOX's old wx + wz + h/2 was found to understate a spike
; badly; 88.5.11 made a sound bound the rule for every model, and this one
; was already over its 200, so it is unchanged.
cs_m_mia_sefc: db CSM_STACK, 4, 13, 0, CSI_LINE, 0
    dw 210, .v, cs_f_sacre, 0
.v: dw 28, 0, 28
    dw 28, 26, 28
    dw 20, 160, 20
    dw 0, 196, 0
CS_DOME cs_m_mia_twr2, 22, 108, 146     ; a pyramidal crown, which half of
                                        ; this skyline has and no European
                                        ; one does
CS_BOX  cs_m_mia_twr3, 30,  96,  20

; --- the beach ---------------------------------------------------------------
; A hotel block: 60 m of frontage, 40 m deep and 42 m tall, standing on the
; ocean side of the bar where the real ones stand. Two of them, and three of
; the shared blocks between and around them.
CS_BOX  cs_m_mia_hotel, 30, 42, 20

; --- the world ---------------------------------------------------------------
; TWENTY-NINE objects and the last nine are CSO_DENSE, so twenty of them are
; the Miami every rung below High draws (88.13.1.3).
; EVERY PIECE OF WATER IS A POINT OF INTEREST AND NOTHING
; ELSE IS (88.13.1), so Detail Level = Low in Miami draws the coastline and leaves
; the rest of Florida flat, which is exactly what this location is for. Six
; of them, which is the most the convention allows and the right six.
;
; The two inshore sea pieces keep their near model out to 12,000 m, which is
; past the far corner of this world: their far model exists because a ribbon
; must have one and because a piece the aeroplane has flown away from should
; cost five vertices, but inside the world the Atlantic is always water. The
; bay swaps at 9,000 m, where it is a band two rows deep and a centreline
; says everything a fill would.
;
; Nothing collidable stands in either sheet of water. The tightest is the
; southern beach hotel, 92.8 m from the Atlantic's edge; downtown's closest
; corner is the tall tower's at 111 m from the bay, and the widest margin
; between the runway rectangle and anything solid is the 238 m the two
; airport buildings keep from its sides.
cs_ot_mia:
    CS_OBJ cs_m_mia_sea1,  cs_m_mia_sea1f,     0,  3000, 0, 16000, 12000, cs_n_mia_atl,   CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_mia_sea0,  cs_m_mia_sea0f,     0, -3000, 0, 16000, 12000, cs_n_mia_atl,   CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_mia_open,  0,               6900,     0, 0, 16000,     0, cs_n_mia_atl,   CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_mia_bay1,  cs_m_mia_bay1f, -1700,  3000, 0, 12000,  9000, cs_n_mia_bay,   CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_mia_bay0,  cs_m_mia_bay0f, -1700, -3000, 0, 12000,  9000, cs_n_mia_bay,   CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_mia_cut,   cs_m_mia_cutf,   -400, -3750, 0, 11000,  5000, cs_n_mia_cut,   CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_mia_cw0,   0,              -1800, -2580, 0,  9000,     0, cs_n_mia_cway,  CSO_ROAD
    CS_OBJ cs_m_mia_cw1,   0,              -1600,  -260, 0,  9000,     0, cs_n_mia_cway,  CSO_ROAD
    CS_OBJ cs_m_mia_cw2,   0,              -1700,  2420, 0,  9000,     0, cs_n_mia_cway,  CSO_ROAD
    CS_OBJ cs_m_mia_sefc,  0,              -2500,   200, 0, 14000,     0, cs_n_mia_dtn,   CSO_COLLIDE
    CS_OBJ cs_m_mia_twr2,  0,              -2560,  -250, 0, 11000,     0, cs_n_mia_dtn,   CSO_COLLIDE
    CS_OBJ cs_m_mia_twr3,  0,              -2520,   620, 0,  9000,     0, cs_n_mia_dtn,   CSO_COLLIDE
    CS_OBJ cs_m_block,     0,              -2700,  -700, 0,  4000,     0, cs_n_bldg,      CSO_COLLIDE
    CS_OBJ cs_m_block2,    0,              -2850,   900, 0,  4000,     0, cs_n_bldg,      CSO_COLLIDE
    CS_OBJ cs_m_mia_hotel, 0,               -140,   900, 0,  6000,     0, cs_n_mia_hotel, CSO_COLLIDE
    CS_OBJ cs_m_mia_hotel, 0,               -150, -1000, 0,  6000,     0, cs_n_mia_hotel, CSO_COLLIDE
    CS_OBJ cs_m_small,     0,               -200,  2100, 0,  4000,     0, cs_n_bldg,      CSO_COLLIDE
    CS_OBJ cs_m_block2,    0,               -420,   300, 0,  4500,     0, cs_n_bldg,      CSO_COLLIDE
    CS_OBJ cs_m_hangar,    0,              -4900,  -320, 0,  3500,     0, cs_n_bldg,      CSO_COLLIDE
    CS_OBJ cs_m_shed,      0,              -4300,   300, 0,  3000,     0, cs_n_bldg,      CSO_COLLIDE
    ; --- THE HIGH TIER (SPEC.md 88.13.1.3): eleven more, CSO_DENSE, so
    ;     Detail Level = High is the only rung that draws one and every rung
    ;     below it gives the Miami that always shipped.
    ;
    ;     A CLIMB-OUT ON RUNWAY 09 CROSSES THIS WORLD'S TWO BUILT PLACES AND
    ;     BOTH ARE THIN: downtown is three towers and two blocks at 2.2 km,
    ;     and the barrier island - the shape the whole location is built to
    ;     show - carries two hotels along nine kilometres of sand. So High
    ;     puts a WALL on the bay and a STRIP on the beach, which is what
    ;     those two places are.
    ;
    ;     Brickell is the wall: four towers running south from downtown along
    ;     the bay's west shore. The strip is two mid-rises spaced down Collins
    ;     between the two hotels already there, so the island reads as built
    ;     along its length rather than at two points on it.
    ;
    ;     NINE AND NOT ELEVEN, and CS_NVIS is what settled that. A fifth
    ;     Brickell tower and two more mid-rises put 29 objects in one frame
    ;     against 32, with the thirty-third dropped SILENTLY - and the eye
    ;     that sees them is not the runway or the beach but the MIDDLE
    ;     CAUSEWAY, which is halfway between downtown and the island and can
    ;     see the whole of both. A world with two built places 2.4 km apart
    ;     has its peak in the water between them.
    ;
    ;     Nothing anonymous passes DOWNTOWN MIAMI's 196 m, every footprint
    ;     keeps 60 m of daylight from every other and 80 m from the water -
    ;     which on a barrier island is the binding rule and not a formality -
    ;     Brickell's ranges are its own Manhattan distance from the spawn
    ;     plus 400 m rounded up to 500, so the skyline the climb-out flies at
    ;     is standing before the roll. THE STRIP'S ARE NOT, and deliberately:
    ;     it takes 4,500 like the island's own block and hotel, because a
    ;     55 m mid-rise nine kilometres away is a box LOD dot and the strip is
    ;     something the aeroplane flies OVER rather than towards. Ranging it
    ;     from the spawn as well put 29 objects in one frame against
    ;     CS_NVIS' 32, and the thirty-third is dropped SILENTLY.
    CS_OBJ cs_m_tower,     0,              -2500,  -520, 0,  5000,     0, cs_n_mia_bkl,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,    0,              -2600,  -850, 0,  5000,     0, cs_n_mia_bkl,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,     0,              -2540,  -980, 0,  5500,     0, cs_n_mia_bkl,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,    0,              -2620, -1330, 0,  5500,     0, cs_n_mia_bkl,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,    0,              -2480,   320, 0,  4500,     0, cs_n_mia_dtn,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,   0,              -2680,    60, 0,  4500,     0, cs_n_mia_dtn,   CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,   0,               -155,   180, 0,  4500,     0, cs_n_mia_hotel, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,   0,               -145,  -500, 0,  4500,     0, cs_n_mia_hotel, CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,   0,               -175, -1600, 0,  4500,     0, cs_n_mia_hotel, CSO_COLLIDE | CSO_DENSE
cs_ote_mia:
CS_N_MIA equ (cs_ote_mia - cs_ot_mia) / CSO_SIZE

; --- the names the crash line uses -------------------------------------------
cs_n_mia_atl:   db 'THE ATLANTIC', 0
cs_n_mia_bay:   db 'BISCAYNE BAY', 0
cs_n_mia_cut:   db 'GOVERNMENT CUT', 0
cs_n_mia_cway:  db 'A CAUSEWAY', 0
cs_n_mia_dtn:   db 'DOWNTOWN MIAMI', 0
cs_n_mia_hotel: db 'A BEACH HOTEL', 0
cs_n_mia_bkl:   db 'BRICKELL', 0

; --- the location ------------------------------------------------------------
; The real Miami International is nine kilometres inland and its runways are
; 09/27, which is the one fact about it worth keeping: an eastbound departure
; leaves the ground pointing at the whole location. Compressed to 4.6 km, so
; the far threshold is 1.45 km from the mainland shore and the aeroplane is
; over downtown before it has finished the first climb.
cs_a_mia:
    dw cs_s_mia                     ; CSA_NAME
    dw -4600, 0                     ; CSA_X, CSA_Z: 4.6 km west of the beach,
    dw 0                            ; CSA_ELEV     2.25 km west of the bay
    dw 16384                        ; CSA_HDG      090, straight out to sea
    dw 800                          ; CSA_HLEN     a 1,600 m runway...
    dw 22                           ; CSA_HWID     ...44 m wide
    dw cs_s_rwy_mia                 ; CSA_RWY
    dw -740                         ; CSA_SPAWN    60 m in from the threshold
    dw cs_ot_mia, CS_N_MIA          ; CSA_OBJS, CSA_NOBJ
    dw -1642, -750                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 8192                        ; CSA_WHDG    (88.7.7) - BISCAYNE BAY
    dw 1000                          ; CSA_WLEN    half its length...
    dw 60                           ; CSA_WWID    ...and half its width
    dw cs_n_mia_bay              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
cs_s_mia: db 'MIAMI-MIA', 0
cs_s_rwy_mia: db 'RUNWAY 09', 0
