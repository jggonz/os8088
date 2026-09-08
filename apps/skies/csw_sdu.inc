; =============================================================================
; os8088 - apps/skies/csw_sdu.inc
;
; RIO DE JANEIRO (SPEC.md 88.6.4) - the world RIO-SDU stands in. Metres, x east,
; z north, y up; THE ORIGIN IS THE SUMMIT OF SUGARLOAF MOUNTAIN, because every
; other position in this file is easiest to read as a bearing and a distance
; from the one rock the location exists to show.
;
; Rio's landmarks are SHAPES rather than buildings, which is why it is worth a
; world here: a filled-polygon engine with a frustum primitive draws a granite
; dome for eight vertices and five faces, where the same money in Paris buys
; one wall of the Louvre. So this world is five hills, four sheets of water and
; a statue, and the anonymous city is seven blocks on the outskirts of it.
;
; THE LAYOUT, all of it hanging off the origin:
;
;   Sugarloaf         (0, 0)              396 m, the origin
;   Morro da Urca     (-560, 190)         220 m, 591 m WNW of it, the pair
;   the runway        (-2725, 990)        2,899 m WNW of Sugarloaf on 110
;   Corcovado         (-6860, 2495)       710 m, 4,400 m WNW of the runway
;   Christ            (-6860, 2495, 710)  38 m on Corcovado's summit plateau
;
; The runway is laid on the 110/290 axis THROUGH those two, so the take-off run
; has Sugarloaf ahead of it and Corcovado with the statue behind, which is what
; the airfield is for. It is the shape Paris-Issy already has: take off on 04
; there and fly straight ahead and the Eiffel Tower is 3.5 km down the nose,
; collidable, and 324 m tall. Here it is Sugarloaf at 2,249 m from the
; departure threshold and 396 m tall, so a Cessna that holds the runway
; heading and climbs does not clear it. That is Santos Dumont: a short strip on
; reclaimed land with high ground close off the end, and a departure that turns.
;
; It is NOT the real bearing. Santos Dumont's runway is 02/20 and both hills
; are south of it; the brief for this world asked for the pair on one axis
; through the field, and rotating the map is what buys "one landmark ahead and
; the other behind" for a runway that is otherwise a strip of grey. The
; distances are stretched the other way as well: Sugarloaf to Corcovado is
; 5.6 km in Rio and 7.3 km here, because the field was put between them.
;
; SUGARLOAF STANDS IN THE WATER IN RIO AND IT DOES NOT HERE, which is the one
; thing tests/unit/t_csworld.py fails a build over. It stands on the URCA
; PENINSULA instead, which is what it really stands on: the bay's two ribbons
; pass round the north of it and the Atlantic's round the south, and the
; arithmetic is written out at each of them. The margins are 78 m at the
; tightest against a gate that wants 5.
;
; The macros, the face tables, the shared filler models and the shared edge
; lists are csworld.inc's.
;
; THE ORDER IN THIS FILE IS FIXED: models, then the object table, then the
; names, then the location record - because CS_N_SDU is an equ over the
; table's own two labels and NASM cannot see them from above.
; =============================================================================

; --- the two face tables this world needs and csworld.inc has not got --------
; A three-level frustum is two tiers of sides and a top, which is the shape a
; monolith wants: a rock that flares, then narrows to a small summit. Both
; Sugarloaf and Corcovado use it, so the nine faces are declared once.
cs_f_sdu_dome:
    CS_SIDES 0, CSI_HILL, CSI_HILL
    CS_SIDES 4, CSI_HILL, CSI_HILL
    CS_TOP 8, CSI_HILL

; ...and the beach, which is a ribbon of three quads in CSI_MARK rather than in
; CSI_RIVER. CSI_MARK is the ground ink - roads and runway edges - and it is
; solid white on Hercules and near white in Mode X, so a 150 m band along the
; waterline reads as sand against 12.5% ground and striped water. It is not
; water, so nothing about it constrains where a collidable base may stand.
cs_f_sdu_snd:
    db 4, CSI_MARK, CSF_NOCULL, 0, 1, 5, 4
    db 4, CSI_MARK, CSF_NOCULL, 1, 2, 6, 5
    db 4, CSI_MARK, CSF_NOCULL, 2, 3, 7, 6

; --- Sugarloaf: the headline landmark, and the only model here with a far one
; Three levels rather than a plain CS_HILL because the rock BULGES: it leaves
; the water 560 by 520 m, flares to 600 by 560 at about 170 m, and rounds off
; to a 220 by 200 m summit plateau, which the cable car station really does sit
; on. The bulge is what makes it read as granite rather than as a hill.
;
; It is the ONE model in this world whose far version buys anything. A two
; level stack is already eight vertices, the smallest a STACK can be, so a far
; model for a plain hill would transform the same eight and save a single
; polygon; this one is twelve near, so beyond CSO_LOD it saves four vertices at
; some 3,300 cycles each and four faces. Everything else here is either eight
; vertices already or a ribbon, and the ribbons get centrelines below.
cs_m_sdu_pao: db CSM_STACK, 3, 9, 0, CSI_LINE, 0
    dw 738, .v, cs_f_sdu_dome, 0
.v: dw 280, 0, 260
    dw 300, 170, 280
    dw 110, 396, 100
CS_HILL cs_m_sdu_paof, 280, 260, 396, 110, 100

; --- Morro da Urca: 220 m, 591 m along 289 from Sugarloaf ---------------------
; The smaller sibling the cable car stops at, and the brief for this world is
; right that the PAIR is what makes either read: one dome is a hill and two
; stepped domes at the end of a peninsula are Rio. Its base is 300 by 280 m,
; which is the real one, and that is what keeps its radius at 400 and lets it
; stand at CSO_RANGE 4,000 with no far model at all.
CS_HILL cs_m_sdu_urca, 150, 140, 220, 55, 50

; --- Corcovado: 710 m, with the statue on top ---------------------------------
; The same three-level shape as Sugarloaf and for a different reason: the real
; hill is a broad wooded shoulder with a sheer granite spur standing out of it,
; so the middle level is the shoulder at 430 m and the top a 110 by 90 m
; platform. That platform is what the statue's ten-metre box stands on.
;
; It is the world's beacon, so it is drawn to CS_FAR less a little; a far model
; is worth having at that range even though it only saves four vertices and
; four faces, because the near model is in view from anywhere in the world.
cs_m_sdu_corc: db CSM_STACK, 3, 9, 0, CSI_LINE, 0
    dw 1155, .v, cs_f_sdu_dome, 0
.v: dw 420, 0, 380
    dw 240, 430, 210
    dw 55, 710, 45
CS_HILL cs_m_sdu_corcf, 420, 380, 710, 55, 45

; --- Christ the Redeemer: two boxes, and it is the thing you navigate by ------
; The statue is 30 m on an 8 m pedestal, which is one pixel at any range worth
; drawing it from, so it is drawn as its own silhouette instead: a 10 m square
; column the full 38 m, and a second box across it 28 m wide and 4 m thick at
; two thirds height. The arms are laid along z because the line from the runway
; to Corcovado runs WNW, so a bar across z is the one that stays broadside and
; keeps the cross a cross.
CS_BOX cs_m_sdu_crst, 5, 38, 5
CS_BOX cs_m_sdu_arms, 4, 4, 14

; --- the two ridges that close the horizon -----------------------------------
; The Tijuca massif stands behind Corcovado and is what stops the world ending
; in flat ground behind the beacon; Dois Irmaos closes the far end of the
; beach. Both are wide and low-detail on purpose: they are the only things in
; this world drawn purely to be a skyline, so they are two levels each and
; their ranges are cut to what a 470 m and a 380 m ridge are actually visible
; at rather than to CS_FAR.
CS_HILL cs_m_sdu_tij,  900, 480, 470, 320, 150
CS_HILL cs_m_sdu_dois, 380, 300, 380,  70,  55

; --- the water: Guanabara Bay in two pieces, Botafogo's cove, the Atlantic ----
; Each is a ribbon of n stations down one bank and n down the other, so the
; quads are csworld.inc's shared cs_f_rib2 and cs_f_rib3, and each has a FAR
; MODEL THAT IS ITS CENTRELINE (88.6.1). That is the biggest single lever in a
; world file: a sheet of water three rows tall at the horizon is two or three
; segments instead of eight transformed vertices and three polygons.
;
; The near bank of the first piece runs 250 m north-east of the runway, which
; is the quay Santos Dumont is built on. The far bank is 1,300 m out into the
; bay. Its south-east end joins the second piece, which carries on past the
; north side of the Urca peninsula and turns south round Sugarloaf's seaward
; face, so the rock has water on two sides of it and land behind.
cs_m_sdu_bay0: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 1926, .v, cs_f_rib2, 0
.v: dw -1925, -10                   ; the airfield shore, north-west end
    dw -225, -610                   ; abeam the runway, 250 m off the quay
    dw 1375, -1210                  ; ...and on toward the peninsula
    dw -1480, 1210                  ; the far bank, 1,300 m out
    dw 220, 610
    dw 1820, 10
cs_m_sdu_bay0f: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 1810, .v, 0, cs_e_road2
.v: dw -1703, 600
    dw -3, 0
    dw 1598, -600

; The bay mouth. Its near bank is the peninsula's north shore, and the last
; station turns south-east round the rock. The clearances that keep Sugarloaf
; and Morro da Urca out of it, measured from each collidable footprint corner
; to the nearest bank segment in world coordinates:
;   Morro da Urca (-410, 330)  to the S1-S2 segment ..... 90.6 m
;   Morro da Urca (-710, 330)  to the S1-S2 segment .... 189.2 m
;   Sugarloaf     (-280, 260)  to the S2-S3 segment .... 153.4 m
;   Sugarloaf      (280, 260)  to the S2-S3 segment .... 123.6 m
;   Sugarloaf      (280, 260)  to the S3-S4 segment .... 127.2 m
; The gate wants 5. This is the tightest water in the world and it is 78 m of
; margin over that at worst.
cs_m_sdu_bay1: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0
    dw 1413, .v, cs_f_rib3, 0
.v: dw -1285, -85                   ; joins bay0's near bank
    dw -600, -330
    dw 150, -370                    ; abeam Sugarloaf, 123 m off the rock
    dw 500, -850                    ; ...turning south round its seaward face
    dw -840, 1135                   ; the far bank, out in the bay mouth
    dw -300, 700
    dw 600, 550
    dw 1300, -250
cs_m_sdu_bay1f: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 1186, .v, 0, cs_e_road3
.v: dw -1063, 525
    dw -450, 185
    dw 375, 90
    dw 900, -550

; Botafogo's cove, on the other side of the peninsula: the water between the
; city and Urca, which is what makes the peninsula read as a peninsula rather
; than as a bulge in the coast. It is small and close in, so its range is the
; shortest of the four.
cs_m_sdu_bot: db CSM_FLAT, 6, 2, 0, CSI_RIVER, 0
    dw 846, .v, cs_f_rib2, 0
.v: dw -775, 300                    ; the city shore
    dw -275, -300
    dw 225, -800
    dw -275, 800                    ; the Urca shore
    dw 275, 200
    dw 775, -300
cs_m_sdu_botf: db CSM_FLAT, 3, 0, 2, CSI_RIVLINE, 0
    dw 761, .v, 0, cs_e_road2
.v: dw -525, 550
    dw 0, -50
    dw 500, -550

; The Atlantic off Copacabana. Its near bank IS the waterline, and the sand
; strip below shares those four stations exactly so the two meet edge to edge
; with no step. The nearest collidable thing to it is Sugarloaf's south-east
; base corner (280, -260), which is 441 m from the Leme end of the waterline.
cs_m_sdu_atl: db CSM_FLAT, 8, 3, 0, CSI_RIVER, 0
    dw 2232, .v, cs_f_rib3, 0
.v: dw 1425, 1050                   ; the waterline, the Leme end
    dw 675, 400
    dw -325, 0
    dw -1425, -150
    dw 2175, 500                    ; ...and 900 m out to sea
    dw 975, -450
    dw -225, -900
    dw -1425, -1050
cs_m_sdu_atlf: db CSM_FLAT, 4, 0, 3, CSI_RIVLINE, 0
    dw 1962, .v, 0, cs_e_road3
.v: dw 1800, 775
    dw 825, -25
    dw -275, -450
    dw -1425, -600

; --- the beach: the third Rio shape, and the cheapest of the three -----------
; A 150 m band of CSI_MARK laid just inland of the Atlantic's waterline, on the
; same four stations, so the pale crescent is three quads and eight vertices.
; It is the one thing in this world that is a colour rather than a solid.
cs_m_sdu_snd: db CSM_FLAT, 8, 3, 0, CSI_MARK, 0
    dw 1586, .v, cs_f_sdu_snd, 0
.v: dw 1365, 645                    ; the landward edge
    dw 685, 47
    dw -282, -346
    dw -1365, -495
    dw 1485, 555                    ; ...and the waterline, shared with atl
    dw 735, -95
    dw -265, -495
    dw -1365, -645
cs_m_sdu_sndf: db CSM_FLAT, 4, 0, 3, CSI_MARK, 0
    dw 1547, .v, 0, cs_e_road3
.v: dw 1425, 600
    dw 710, -24
    dw -274, -421
    dw -1365, -570

; --- the Rio-Niteroi bridge: three vertices and two segments -----------------
; A centreline with no near model at all, which is what a bridge at map scale
; is. It crosses the first bay piece from the city side to the far shore and it
; costs three transformed vertices, which is the cheapest recognisable thing in
; the file. It is not collidable, so the gate has nothing to say about a road
; over water.
cs_m_sdu_brdg: db CSM_FLAT, 3, 0, 2, CSI_MARK, 0
    dw 1082, .v, 0, cs_e_road2
.v: dw -600, -900
    dw 0, 0
    dw 600, 900

; =============================================================================
; The world. TWENTY-NINE objects, the last nine CSO_DENSE (88.13.1.3), so
; twenty of them are the Rio every rung below High draws:
; twenty objects, 159 vertices, five points of interest and seven
; filler.
;
; WHICH HILLS ARE COLLIDABLE IS A DECISION AND NOT AN OVERSIGHT. cs_collide
; (csflight.inc) tests the model's LEVEL 0 footprint and the tallest level's
; height as a BOX, so a collidable mountain is a rectangular no-go column the
; size of its base rather than a cone, and flying past a wide peak's shoulder
; reads as a crash.
;
;   Sugarloaf and Morro da Urca ARE collidable. Both are sheer-sided monoliths
;   whose base is very nearly their silhouette - a 560 by 520 m box up to 396 m
;   is an honest description of that rock - and both are small. They are also
;   the hazard the location is about: they stand off the departure end.
;
;   Corcovado, Tijuca and Dois Irmaos are NOT. Corcovado's base is 840 by 760 m
;   for a summit 110 m across, so its box would be a crash a quarter of a
;   kilometre clear of the hill; the two ridges are wider still. This is
;   Paris' own answer one world along: Montmartre is a 1,400 m hill and is not
;   collidable, and the Sacre-Coeur standing on it is.
;
;   CHRIST THE REDEEMER carries Corcovado's collision instead, which is the
;   Sacre-Coeur exactly. Its footprint is 10 m square, so the column is thin,
;   it stands on a summit nothing can reach without flying into the hill
;   anyway, and it is the name worth having in the crash line. The arms are
;   left alone: the figure already covers the centre of the cross, and a
;   second column would only add 28 m of no-go to either side of it.
;
; The ranges: the beacon at 16,000 and the far model in at 5,000; the pair on
; the peninsula at 12,000 and 4,000 because Morro da Urca at 220 m has nothing
; to say beyond that; water at 4,500 to 5,000 with its centreline in at 2,600,
; which is Paris' river band; the anonymous city at 2,500 to 3,000, which is
; Paris' filler band.
; =============================================================================
cs_ot_sdu:
    CS_OBJ cs_m_sdu_pao, cs_m_sdu_paof,      0,     0,   0, 12000, 3500, cs_n_sdu_pao,  CSO_COLLIDE | CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_sdu_urca, 0,               -560,   190,   0,  4000,    0, cs_n_sdu_urca, CSO_COLLIDE | CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_sdu_corc, cs_m_sdu_corcf, -6860,  2495,   0, 16000, 5000, cs_n_sdu_corc, CSO_POI | CSO_TERRAIN
    CS_OBJ cs_m_sdu_crst, 0,              -6860,  2495, 710,  9000,    0, cs_n_sdu_crst, CSO_COLLIDE | CSO_POI
    CS_OBJ cs_m_sdu_arms, 0,              -6860,  2495, 734,  6000,    0, cs_n_sdu_crst, CSO_POI
    CS_OBJ cs_m_sdu_tij, 0,              -8500,  3600,   0,  9000,    0, cs_n_sdu_tij, CSO_TERRAIN
    CS_OBJ cs_m_sdu_dois, 0,              -3600, -2400,   0,  8000,    0, cs_n_sdu_dois, CSO_TERRAIN
    CS_OBJ cs_m_sdu_bay0, cs_m_sdu_bay0f, -2460,  1875,   0,  5000, 2600, cs_n_sdu_bay,  CSO_TERRAIN
    CS_OBJ cs_m_sdu_bay1, cs_m_sdu_bay1f,   200,   750,   0,  5000, 2600, cs_n_sdu_bay,  CSO_TERRAIN
    CS_OBJ cs_m_sdu_atl,  cs_m_sdu_atlf,  -1175, -1750,   0,  5000, 2600, cs_n_sdu_cop,  CSO_TERRAIN
    CS_OBJ cs_m_sdu_bot,  cs_m_sdu_botf,  -1425,  -500,   0,  4500, 2600, cs_n_sdu_bay,  CSO_TERRAIN
    CS_OBJ cs_m_sdu_snd,  cs_m_sdu_sndf,  -1235, -1255,   0,  5000, 2600, cs_n_sdu_cop,  0
    CS_OBJ cs_m_sdu_brdg, 0,              -3300,  2600,   0,  8000,    0, cs_n_sdu_brg,  CSO_ROAD
    CS_OBJ cs_m_hangar,   0,              -2450,   560,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_shed,     0,              -2900,   480,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_small,    0,              -3150,   700,   0,  2500,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block,    0,              -3600,   100,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block2,   0,              -4300,   700,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_block,    0,              -4900,  1600,   0,  3000,    0, cs_n_bldg,     CSO_COLLIDE
    CS_OBJ cs_m_small,    0,              -1750,   500,   0,  2500,    0, cs_n_bldg,     CSO_COLLIDE
    ; --- THE HIGH TIER (SPEC.md 88.13.1.3): nine more, CSO_DENSE, so Detail
    ;     Level = High is the only rung that draws one and every rung below it
    ;     gives the Rio that always shipped.
    ;
    ;     THE CITY IS THE ONE THING THIS WORLD DOES NOT HAVE. It is five
    ;     hills, four sheets of water and a statue - which is the right shape
    ;     for the location, and it leaves Rio itself as seven sheds beside
    ;     the runway. So High adds the three built places the shapes are
    ;     wrapped round, in the order a RUNWAY 11 departure meets them:
    ;
    ;       CENTRO       three, off the runway's own end - Santos Dumont is
    ;                    reclaimed land at the foot of the business district,
    ;                    so this is what the take-off roll looks at
    ;       BOTAFOGO     two on the bay shore between Centro and Sugarloaf
    ;       COPACABANA   four mid-rises in a line just inland of the sand,
    ;                    which is what that beach is: a WALL of flats, evenly
    ;                    tall, following the crescent for two kilometres
    ;
    ;     The Copacabana four sit on the landward edge of cs_m_sdu_snd offset
    ;     130 m inland, which is the beach's own four stations, so the wall
    ;     follows the crescent rather than a straight line drawn near it. They
    ;     take 4,000 m and no more: the strip is BEHIND Sugarloaf from the
    ;     runway and the aeroplane meets it after crossing the rock, so
    ;     ranging it from the spawn buys nothing and costs CS_NVIS.
    ;
    ;     NINE AND NOT TEN: a fourth Centro block put 28 objects in one frame
    ;     against CS_NVIS' 32 with the thirty-third dropped SILENTLY, and the
    ;     eye that sees them is a shed on the airport road, which is inside
    ;     everything at once. Nine is the same 27 the other worlds' High tiers
    ;     settle at.
    ;
    ;     Nothing anonymous is over 180 m, against Sugarloaf's 396 and
    ;     Corcovado's 710; every footprint keeps 60 m from every other, 80 m
    ;     from the water and 60 m from the runway rectangle.
    CS_OBJ cs_m_tower2,   0,              -2600,   300,   0,  3500,    0, cs_n_sdu_ctr,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,              -2420,   760,   0,  3000,    0, cs_n_sdu_ctr,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower,    0,              -3000,   250,   0,  3000,    0, cs_n_sdu_ctr,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_tower2,   0,              -1300,   420,   0,  4500,    0, cs_n_sdu_bot,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,              -1360,   140,   0,  5000,    0, cs_n_sdu_bot,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,               -330,  -661,   0,  4000,    0, cs_n_sdu_cop,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,               -715, -1049,   0,  4000,    0, cs_n_sdu_cop,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,              -1502, -1197,   0,  4000,    0, cs_n_sdu_cop,  CSO_COLLIDE | CSO_DENSE
    CS_OBJ cs_m_midrise,  0,              -1968, -1345,   0,  4000,    0, cs_n_sdu_cop,  CSO_COLLIDE | CSO_DENSE
cs_ote_sdu:
CS_N_SDU equ (cs_ote_sdu - cs_ot_sdu) / CSO_SIZE

; --- the names the crash line uses -------------------------------------------
; ASCII, no accents: Pao de Acucar and Dois Irmaos are written the way the
; panel's font can set them.
cs_n_sdu_pao:  db 'SUGARLOAF MOUNTAIN', 0
cs_n_sdu_urca: db 'MORRO DA URCA', 0
cs_n_sdu_corc: db 'CORCOVADO', 0
cs_n_sdu_crst: db 'CHRIST THE REDEEMER', 0
cs_n_sdu_tij:  db 'THE TIJUCA RIDGE', 0
cs_n_sdu_dois: db 'DOIS IRMAOS', 0
cs_n_sdu_bay:  db 'GUANABARA BAY', 0
cs_n_sdu_cop:  db 'COPACABANA', 0
cs_n_sdu_brg:  db 'THE RIO-NITEROI BRIDGE', 0
cs_n_sdu_ctr:  db 'CENTRO', 0
cs_n_sdu_bot:  db 'BOTAFOGO', 0

; =============================================================================
; The location (SPEC.md 88.6, 88.6.4). Santos Dumont is 1,323 m of reclaimed
; land in the bay in the middle of the city, and the 1,300 m here is that
; runway rather than a round number.
;
; It is laid on 110/290 rather than the real 02/20 so that the two hills sit
; one off each end of it (see the header). CSA_SPAWN is negative, so the
; aeroplane stands 60 m in from the NORTH-WEST threshold facing 110 and rolls
; south-east: Sugarloaf is 2,249 m off the departure threshold and Corcovado
; with the statue is over the tail.
;
; The runway rectangle runs from (-2114, 768) to (-3336, 1212) and is 40 m
; wide. Every collidable base clears it by more than the 150 m the brief for
; this world asks: the terminal apron's hangar is the closest at 223 m from
; the runway rectangle, and the nearest hill is Morro da Urca at 1,469 m.
; =============================================================================
cs_a_sdu:
    dw cs_s_sdu                     ; CSA_NAME
    dw -2725, 990                   ; CSA_X, CSA_Z: on the bay's west shore,
    dw 0                            ; CSA_ELEV     2,899 m WNW of Sugarloaf
    dw 20025                        ; CSA_HDG      110
    dw 650                          ; CSA_HLEN     a 1,300 m runway...
    dw 20                           ; CSA_HWID     ...40 m wide
    dw cs_s_rwy_sdu                 ; CSA_RWY
    dw -590                         ; CSA_SPAWN    60 m in from the threshold
    dw cs_ot_sdu, CS_N_SDU          ; CSA_OBJS, CSA_NOBJ
    dw -3312, 2175                 ; CSA_WX, CSA_WZ: THE WATER STRIP
    dw 13653                        ; CSA_WHDG    (88.7.7) - GUANABARA BAY
    dw 1000                          ; CSA_WLEN    half its length...
    dw 100                           ; CSA_WWID    ...and half its width
    dw cs_n_sdu_bay              ; CSA_WNAME
    dw 0                        ; CSA_FLAGS
cs_s_sdu: db 'RIO-SDU', 0
cs_s_rwy_sdu: db 'RUNWAY 11', 0
