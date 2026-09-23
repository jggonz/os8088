; =============================================================================
; os8088 - apps/skies/skies.asm
;
; CLEAR SKIES, a filled-polygon flight simulator (SPEC.md 88): take off from
; an airport, fly over Paris, land - or crash and be put back on the runway.
; A .o88 package at org 0 owning a segment (SPEC.md 20.1), prefix `cs_`,
; embedded icon, no worker task, and no kernel change of any kind.
;
; It is TANK ATTACK's shape (SPEC.md 85) with the other half of the 1983
; vocabulary attached: Tank draws nothing but lines and this FILLS the space
; between them. Like Tank it runs inside an fsx bracket in a foreign mode
; (SPEC.md 53.4), where no kernel drawing slot is legal (53.7) and every
; pixel is this package's own. It is built against one number: TWELVE FRAMES
; A SECOND on a 4.77 MHz 8088 with a Hercules card, the machine in the tree
; that can least afford a filled picture.
;
; THE THREE THINGS THAT DECIDE THE WHOLE DESIGN (SPEC.md 88.1)
;
;  1. EVERY PIXEL OF THE VIEW CHANGES EVERY FRAME, which inverts Tank's
;     central finding. Tank keeps a per-row dirty span so that the next frame
;     clears exactly what the last one drew; a flight simulator's frame IS a
;     clear - the sky and the ground together cover the view and the horizon
;     between them moves with every degree of pitch and roll. So the view
;     keeps no dirty spans at all: the sky/ground pass writes every byte of it,
;     the polygons and lines go over that, and the blit copies it whole. The
;     span set is kept for the PANEL, where an instrument that has not changed
;     is not redrawn and not copied.
;  2. THE VIEW IS HALF THE BOX. A frame's cost on this design is proportional
;     to the view's AREA - the fill, the blit and every polygon row scale with
;     it - so the view is sized to the budget and not to the screen: 320x112
;     on CGA, 320x144 on Mode X, and 400x112 in the middle of the 640-wide box
;     on Hercules, 50 bytes of the 80 in every row.
;  3. A POLYGON IS FILLED BY ITS EDGES, NEVER BY ITS PIXELS. Each edge is
;     walked once per ROW with an integer step into a left and a right bound,
;     and each row is then one masked run - which is how every game of the
;     period did it (SPEC.md 88.2), and why an 8088 can fill a building face
;     in the time it takes to walk one of its edges.
;
; ONE PLANE AND ONE AIRPORT, EACH A RECORD (SPEC.md 88.6): the flight model
; reads every constant it needs out of [cs_plane]'s row and the runway is
; built out of [cs_airport]'s at bracket entry, so a second of either is a
; row in a table and not a rewrite.
;
; Keys: arrows pitch and roll (down = nose up, as in every flight simulator),
; W/S throttle, A/D rudder and nosewheel, B brakes, P pauses, R puts the
; aeroplane back on the runway, M mutes the engine, Esc or F leaves.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'SKIES', cs_entry, 1, OS88_STACK_DEFAULT
                                ; no worker: the whole flight is the bracket
                                ; on task 0, and the attract window is still

%include "csicon.inc"

; =============================================================================
; Constants
; =============================================================================

; --- which raster we are on ---------------------------------------------------
CSB_NONE  equ 0                 ; windowed: no bracket, nothing to draw into
CSB_MODEX equ 1                 ; 320x240x256 planar, 3 pages - PAGE FLIP
CSB_CGA   equ 2                 ; 320x200x4 banked, 1 page  - SHADOW + BLIT
CSB_HERC  equ 3                 ; 720x348 mono, 4 banks     - SHADOW + BLIT
CSB_C160  equ 4                 ; 160x100x16 - THE TEXT HACK (SPEC.md 88.15):
                                ; 80x25 retimed to a hundred two-scan-line
                                ; rows of half blocks, so an attribute byte
                                ; is two pixels in sixteen colours.
                                ; SHADOW + AN EXPANDING BLIT

; --- the logical inks (SPEC.md 88.4.4) ----------------------------------------
; Named by what they MEAN. Every backend's table gives an ink FOUR pattern
; bytes, one per row of a four-row cycle, which is what lets a Hercules dither
; and a Mode X colour index go through one fill.
CSI_SKY    equ 0
CSI_GROUND equ 1
CSI_RUNWAY equ 2
CSI_RIVER  equ 3
CSI_WALL   equ 4                ; a face toward the camera
CSI_WALL2  equ 5                ; a side
CSI_ROOF   equ 6
CSI_HILL   equ 7
CSI_LINE   equ 8                ; the tower, and every wireframe edge
CSI_MARK   equ 9                ; runway edges and roads
CSI_PBG    equ 10               ; the panel's ground...
CSI_PFG    equ 11               ; ...its ink...
CSI_PHI    equ 12               ; ...and its warning
CSI_PFACE  equ 13               ; the cockpit's face round the windows (88.9.2)
CSI_RIVLINE equ 15              ; A RIVER SEEN FROM FAR ENOUGH TO BE A LINE
                                ; (88.6.5): the river's own blue on the two
                                ; colour adapters, and white on Hercules,
                                ; where the river's fill is stripes and a
                                ; line drawn in stripes is half a line
CSI_BLACK  equ 14               ; NOTHING: the ground band with the Terrain
                                ; fill off (88.13.3). It cannot be CSI_SKY -
                                ; on a 1bpp adapter the sky IS black and the
                                ; two are the same row, but on a colour one
                                ; the whole world would then read as sky
CSI_NINK   equ 16

; --- the world (SPEC.md 88.5, 88.6) -------------------------------------------
; Metres. x east, z north, y up; the Eiffel Tower at the origin.
CS_NEAR   equ 40                ; the near plane
CS_NEARG  equ 4                 ; ...and a ground polygon's: the eye is 2 m up
                                ; and the view's bottom row is 10 m ahead, so
                                ; at 40 m the runway ended above it (88.5.5)
CS_FAR    equ 16000             ; nothing beyond this is transformed, and it
                                ; is what keeps every 16-bit sum in range
CS_MAXV   equ 24                ; vertices in the largest model (the tower's
                                ; five levels are 20)
CS_MAXPV  equ 10                ; ...and a face after the near clip
CS_ESEEN  equ CS_MAXV * 4       ; ...and the edges-once marks (88.13.3): a
                                ; bit per vertex pair, four bytes a row
CS_LODTALL equ 26               ; ...and how TALL it may be (88.5.4.4). The
                                ; complaint 88.5.4.1 answers was a WIDE, flat
                                ; rectangle that did not rotate in a bank -
                                ; 22x3, 20x2, 18x2 - and holding the height
                                ; to the same eight put a two-pixel-wide
                                ; tower back on the polygon path, where its
                                ; faces are sub-pixel and the winding is
                                ; decided by rounding: buildings vanished
CS_LODHYST equ 5                ; ...and what an object ALREADY drawn as the
                                ; impostor may grow to before it gives it up.
                                ; Without it a building sat on the boundary
                                ; and changed shape every few metres
CS_LODPX  equ 8                 ; the biggest RECTANGLE cs_boxlod may stand
                                ; in for a solid (SPEC.md 88.5.4.1): an
                                ; impostor is axis-aligned in SCREEN space,
                                ; which nothing in a banked world is, so it
                                ; has to be small enough that nobody can see
                                ; the shape
CS_NVIS   equ 48                ; objects that can be in one frame, and the
                                ; 49th is dropped SILENTLY. It was 32 and 32
                                ; IS REACHABLE: Paris at Draw Distance = Far
                                ; scales every range by 1.6 and puts 34
                                ; objects in a level frame, so two of them
                                ; came off the glass with nothing said
                                ; (88.13.2.2). Eleven bytes a slot
CS_OCCN   equ 4                 ; occluders cs_occlude keeps (88.13.7): the
                                ; four biggest BY ANGULAR SIZE. Three found
                                ; one spur of two over part of the run and
                                ; the fourth found both everywhere the
                                ; pixels say both are hidden
CS_OCCZ   equ 14                ; ...seven words each, the last of
                                ; them the angular size it is
                                ; ranked by
CS_VISZ   equ 6                 ; ...six bytes each: ptr, reach, along
CS_MAXROW equ 240               ; the tallest box any backend offers
CS_LASTB  equ 79                ; the last byte of a box row, on all FOUR -
                                ; 320x4bpp, 640x1bpp, a Mode X plane row and
                                ; 160 nibble pairs are each eighty bytes,
                                ; which is what one raster over four backends
                                ; rests on (88.15.1)
CS_SHSEG  equ 16000             ; the CGA/Hercules shadow, in bytes
CS_SHKB   equ 16                ; ...as a claim

; --- model types --------------------------------------------------------------
CSM_STACK equ 0                 ; levels of (wx, h, wz): boxes, pyramids, the
                                ; tower (SPEC.md 88.5)
CSM_FLAT  equ 1                 ; explicit (x, z) pairs on the ground

; --- a model: db type, nverts (STACK: nlevels), nfaces, nedges, edge ink, 0;
;     dw radius, verts, faces, edges ---------------------------------------------
CSM_TYPE  equ 0
CSM_NV    equ 1
CSM_NF    equ 2
CSM_NE    equ 3
CSM_EINK  equ 4                 ; the edges' ink (5 is 0)
CSM_RAD   equ 6
CSM_VERTS equ 8
CSM_FACES equ 10
CSM_EDGES equ 12
CSM_SIZE  equ 14

; a face: db n, ink, flags, then n vertex indices
; --- the attitude indicator's half-width TABLE (SPEC.md 88.9.2.5) -----------
; It was 41 ms of a BANKED frame - 14% of it - and every millisecond of that
; was the ERASE: the glass is a filled ellipse whose half-width was a square
; root a row, taken again on every redraw for a radius that never changes in
; flight. It is taken once a layout now, into cs_adtab.
CS_ADHMAX   equ 24              ; rows of half-glass the table can hold. The
                                ; tallest bezel any cockpit declares is 20
                                ; rows (CSK_ADRY), so the glass is 18; a taller
                                ; one ever added falls back to the roots, which
                                ; is what cs_adsize's `ja .out` is for

CSF_NOCULL equ 1                ; a ground polygon: visible from either side
; --- ...and which way a STACK face points, so it can be culled against the
;     EYE'S OWN WORLD POSITION before anything is gathered or projected
;     (SPEC.md 88.5.12). Free: the flags byte was 0 on every face -----------
CSF_PX    equ 2                 ; the face whose outward normal is +x
CSF_MX    equ 4                 ; ...-x
CSF_PZ    equ 8                 ; ...+z
CSF_MZ    equ 16                ; ...-z
CSF_TOP   equ 32                ; ...+y, a stack's cap
CSF_AXES  equ CSF_PX | CSF_MX | CSF_PZ | CSF_MZ | CSF_TOP

; --- an object (SPEC.md 88.6): sixteen bytes ----------------------------------
CSO_MODEL equ 0                 ; word: the model
CSO_FAR   equ 2                 ; word: the far model, or 0
CSO_X     equ 4
CSO_Z     equ 6
CSO_Y     equ 8                 ; the base height
CSO_RANGE equ 10                ; drawn within this (Manhattan) distance
CSO_LOD   equ 12                ; ...and the far model stands in beyond this
CSO_NAME  equ 14                ; word: for the crash line
CSO_FLAGS equ 16                ; word: CSO_*
CSO_SKIP  equ 18                ; word: the tick the cull looks at it again
                                ; (88.5.2): out of range by D metres is out
                                ; of range for D/16 ticks at any speed
CSO_SIZE  equ 20
; --- what a SETTING is (SPEC.md 88.13): four knobs the player turns, on the
;     Settings page and on hotkeys inside the bracket. Every one of them
;     trades picture for frame rate, and every default is what shipped ------
CSBL_NONE   equ 0                ; Detail Level: NOTHING built - refused in
                                 ;    cs_consider before any transform, which
                                 ;    is the cheapest form there is (88.13.1)
CSBL_ROADS  equ 1                ; ...the roads and bridges, and no more
CSBL_LOW    equ 2                ; ...and the critical points of interest
CSBL_MOD    equ 3                ; ...and the rest of what is built: THE
                                 ;    DEFAULT, and every location's whole
                                 ;    table until it grows a High tier
CSBL_HIGH   equ 4                ; ...and CSO_DENSE over that - the dense
                                 ;    city, which is a 286/386 rung and not
                                 ;    an 8088 one (88.13.1)
CSZ_SMALL  equ 0                ; Size: half the moderate view each way
CSZ_MOD    equ 1                ; ...the Hercules default, 75% elsewhere
CSZ_FULL   equ 2                ; ...the whole box, whatever it costs
CSL_NEAR   equ 0                ; Detail: 0.6 of every draw range...
CSL_MOD    equ 1                ; ...as it shipped...
CSL_FAR    equ 2                ; ...and 1.6, which also holds the near model
                                ;    of the tower out to 4 km
CSL_ULTRA  equ 3                ; ...and 2.0 with NO BOX IMPOSTOR AT ALL
                                ;    (88.13.2.1): every solid draws its
                                ;    polygons at every size, which is a rung
                                ;    for a machine pegged to the tick
CSFL_TERRAIN equ 1               ; Fill: TERRAIN - the ground under the
                                 ;    horizon, the water, and the hills and
                                 ;    mountains, which are the world's own
                                 ;    surface rather than anything built on
                                 ;    it (88.13.3)
CSFL_BLDG   equ 2                ; ...and everything built on it
CSFL_ALL    equ 3                ; both, which is a filled world; neither is
                                 ; a wireframe one, its lines still hidden

CSO_COLLIDE equ 1               ; the first level's footprint and the tallest
CSO_POI   equ 0x0100            ; a CRITICAL point of interest: drawn even at
                                ; CSBL_FEW, and its range is never cut back
CSO_DENSE equ 0x0200            ; ...and the other end: THE DENSE CITY, drawn
                                ; at CSBL_HIGH and at no other rung (88.13.1).
                                ; The bit was CSO_FILLER and the anonymous
                                ; blocks and sheds wore it; they draw at
                                ; Moderate now, and nothing wears this yet
CSO_ROAD  equ 0x0800            ; a ROAD, a causeway or a BRIDGE: drawn from
                                ; CSBL_ROADS up, where nothing else built is.
                                ; It is the shape of a city with no city on
                                ; it, and it costs almost nothing to draw -
                                ; every one of them is a line model
CSO_TERRAIN equ 0x0400          ; THE WORLD'S OWN SURFACE and not a building:
                                ; a hill, a mountain, the runway. The
                                ; Buildings density never refuses one - a
                                ; mountain range is not scenery you thin out
                                ; to buy frames. What its FILL follows is the
                                ; face's ink and not this bit, so the runway
                                ; carries it and still fills with the
                                ; buildings (88.13.1, 88.13.3). WATER carries
                                ; it too - a river is not a building either,
                                ; and without the bit None emptied the Seine.
                                ; It is on
                                ; the OBJECT and not the model so that
                                ; cs_consider tests it in the word it has
                                ; already loaded; tests/unit/t_csterrain.py
                                ; holds every hill, every water and every
                                ; road object to the right one
CSO_BOXED equ 0x2000            ; ...bit 13: cs_boxlod drew it last frame, so
                                ; it keeps the impostor until it grows past
                                ; CS_LODHYST more than it took it (88.5.4.4)
CSO_SEEN  equ 0x8000            ; ...and bit 15: drawn last frame (88.5.1)
                                ; level's height are a box the aeroplane may
                                ; not enter

; --- an airport (SPEC.md 88.6) ------------------------------------------------
CSA_NAME  equ 0                 ; word: the name
CSA_X     equ 2
CSA_Z     equ 4
CSA_ELEV  equ 6
CSA_HDG   equ 8                 ; the runway heading, 65536 to the turn
CSA_HLEN  equ 10                ; half the runway's length
CSA_HWID  equ 12                ; ...and half its width
CSA_RWY   equ 14                ; word: the runway's designation
CSA_SPAWN equ 16                ; where the aeroplane stands at reset, along
                                ; the runway from its centre (negative = the
                                ; near threshold)
CSA_OBJS  equ 18                ; word: THE WORLD THIS PLACE STANDS IN - its
CSA_NOBJ  equ 20                ; object table and how many rows it has. A
                                ; location is a runway AND the country round
                                ; it (88.6.4), so cs_scene walks the picked
                                ; row's table and not one global one
CSA_WX    equ 22                ; THE WATER STRIP (SPEC.md 88.7.7): a second
CSA_WZ    equ 24                ; runway made of water, in the same four
CSA_WHDG  equ 26                ; numbers as the first, so an amphibian's
CSA_WLEN  equ 28                ; landing is the same arithmetic and not a
CSA_WWID  equ 30                ; polygon test. CSA_WLEN 0 is a place with
CSA_WNAME equ 32                ; no water an aeroplane could get down on
CSA_FLAGS equ 34                ; word: CSA_* below - what this LOCATION asks
CSA_SIZE  equ 36                ; for that the others do not (88.13.7)
CSA_OCC   equ 1                 ; RUN THE OCCLUSION PASS here. It is off
                                ; everywhere else because it can only pay
                                ; where big objects stand behind big objects,
                                ; and a world it cannot help would carry the
                                ; test for nothing (88.13.7)

; --- a plane (SPEC.md 88.7) - speeds 16.8 m/s, angles 65536 to the turn -------
CSP_NAME   equ 0
CSP_VSTALL equ 2
CSP_VROT   equ 4
CSP_VMAX   equ 6
CSP_THRUST equ 8                ; 16.7 m/s a tick, at full throttle
CSP_DRAGK  equ 10               ; drag = v^2 x this >> 16, a tick
CSP_FRICT  equ 12               ; rolling friction a tick, on the ground
CSP_BRAKE  equ 14
CSP_ROLLR  equ 16               ; roll rate a tick, held
CSP_ROLLL  equ 18               ; ...and the return to level
CSP_PITCHR equ 20
CSP_PITCHT equ 22
CSP_TURNK  equ 24               ; heading a tick at sin(roll) = 1
CSP_EYE    equ 26               ; the pilot's eye above the wheels
CSP_MAXROLL equ 28
CSP_MAXPITCH equ 30
CSP_COCKPIT equ 32              ; word: its cockpit record (88.9.2)
CSP_ATT    equ 34               ; word: ITS FLIGHT MODEL (88.7.2) - the near
                                ; proc that integrates roll and pitch and
                                ; decides the stall, which is where one
                                ; aeroplane stops feeling like another. The
                                ; rest of cs_step - thrust, drag, the turn,
                                ; the motion, the ground - is shared, because
                                ; it is the same arithmetic for both and a
                                ; second copy of it would drift
CSP_SPOOL  equ 36               ; the engine's LAG, a shift count: thrust
                                ; closes this fraction of the gap to what the
                                ; throttle asks for, each tick. 0 is `sar by
                                ; 0`, which is instant - every propeller
                                ; (SPEC.md 88.7.5)
CSP_LAUNCH equ 38               ; metres above the field at reset, and the
                                ; state that goes with it: 0 is ON THE RUNWAY,
                                ; which is every aeroplane with an engine
CSP_FLAGS  equ 40               ; CSPF_*
CSP_ART    equ 42               ; word: ITS PICTURE (88.10.1) - the 1bpp band
                                ; the launcher blits when this row is the one
                                ; in use. It hangs off the record rather than
                                ; off a third table beside cs_planes and
                                ; cs_plnames, so an aeroplane carries its own
                                ; picture the way it carries its own cockpit
CSP_INDK   equ 44               ; INDUCED DRAG, the wing's own share
                                ; (SPEC.md 88.7.12): drag = CSP_INDK / (v^2/4)
                                ; a tick, which RISES as the speed falls -
                                ; where CSP_DRAGK's v^2 falls away to nothing
                                ; and lets an aeroplane hang at 60 knots on a
                                ; fifth of its power. APPENDED and not put
                                ; beside CSP_DRAGK where it belongs, because
                                ; tests/skiesbody.py and tests/skiesfleet.py
                                ; carry these offsets as literals and an
                                ; insertion moves every field after it
CSP_SND    equ 46               ; word: ITS ENGINE (88.8.2) - the sound record
                                ; below, or 0 for an aeroplane that has none.
                                ; APPENDED for CSP_INDK's reason: tests carry
                                ; the offsets before it as literals
CSP_SIZE   equ 48

; --- an ENGINE (SPEC.md 88.8.2): what ONE aeroplane sounds like ---------------
; The tone tier is a single square wave - AX = Hz and nothing else - so a
; per-aeroplane engine can only ever be a FREQUENCY LAW, and this is it:
;
;     Hz = CSS_IDLE + source x CSS_SPAN / full scale
;
; where the source is the throttle LEVER, or the thrust the engine actually HAS
; when CSSF_SPOOL is set. A CSS_IDLE of 0 IS silence at a shut throttle, which
; is what every aeroplane did before this, so the field is the switch as well as
; the number and no code tests for it.
;
; THERE IS NO BEAT FIELD, and there was one (88.8.2.1). A tone that dropped a
; few hertz on a fixed share of the ticks was meant to be a piston's roughness
; and the field heard "a periodic dip that does sound like a bug, rather than
; an engine" on the trainer and "a much more frequent bug" on the biplane -
; with its RATE moving as the frame rate moved, which is the sim-tick drop this
; project predicted and could not hear. The verdict was constant, so what is
; left is steady notes and the slew below.
CSS_IDLE  equ 0                 ; word: Hz with the throttle SHUT and the
                                ; engine turning. An aeroplane with an engine
                                ; running is NOT SILENT, and the idle is where
                                ; most of one aeroplane's character against
                                ; another is actually heard
CSS_SPAN  equ 2                 ; word: Hz added between shut and full power
CSS_LAG   equ 4                 ; THE ENGINE'S RESPONSE (88.8.2.1), a shift:
                                ; the note closes this fraction of the GAP to
                                ; what the engine wants, each tick. A throttle
                                ; that moves in one step GLIDES instead of
                                ; snapping, which is what an engine does and
                                ; what a note does not
CSS_CAP   equ 5                 ; ...AND THE NOTE'S PITCH CEILING, a second
                                ; shift (and see SPEC.md 34.1.1, which is the
                                ; OTHER half of what a glide sounds like: the
                                ; kernel used to restart the square wave on
                                ; every change, so a sweep wobbled however
                                ; smooth the numbers were),
                                ; on the NOTE rather than on the gap:
                                ; the step may not exceed this share of where
                                ; the note already is. A share of the gap is a
                                ; constant fraction in HERTZ and a wildly
                                ; varying one in INTERVAL - the Magister's
                                ; first step out of idle was 65 Hz at 180,
                                ; which is a musical FOURTH, and the field
                                ; heard it as exactly that: "this one still
                                ; plays notes as it goes up or down". A share
                                ; of the note is a constant interval, so a
                                ; wide range glides at the same rate at the
                                ; bottom as at the top. The lower of the two
                                ; wins, and neither may be less than one hertz
CSS_FLAGS equ 6                 ; CSSF_*
CSS_SIZE  equ 7

CSSF_SPOOL equ 0x01             ; follow the SPOOLED thrust and not the lever
                                ; (88.7.5) - a jet, and the one aeroplane here
                                ; whose note lags the hand. CSP_SPOOL has
                                ; modelled a 5.3-second spool since the Fouga
                                ; shipped and nothing has ever been able to
                                ; HEAR it. It is also the one source with
                                ; RESOLUTION to spare - cs_thracc is 8.8 where
                                ; the lever is fifty whole steps - so the jet
                                ; scales straight off it and never rounds
                                ; through a percentage

CSPF_AMPHIB equ 0x0001          ; it may touch down on water, and where the
                                ; location has some it STARTS there (88.7.7)

; --- a cockpit record (SPEC.md 88.9.2): what a plane's panel looks like ------
CSK_WIN    equ 0                ; word: the windows, (cell column, row, cells,
CSK_NWIN   equ 2                ; word: how many        rows) - SPEC.md 88.9.5
CSK_ITEMS  equ 4                ; word: the items' cells, (cell column, row)
CSK_ADCX   equ 6                ; the attitude indicator's centre, x at 320
CSK_ADCY   equ 8                ; ...and its row below the view
CSK_ADRY   equ 10               ; its bezel's vertical radius, rows - and the
                                ; GLASS is that less two, so the black disc
                                ; fills the ring rather than sitting inside
                                ; it (88.9.6). There is no separate
                                ; half-height any more
CSK_BARW   equ 12               ; the throttle bar's width, in CELLS (88.9.5)
CSK_DECO   equ 14               ; word: the DECORATIONS (88.9.3) - instruments
CSK_NDECO  equ 16               ; word: ...how many. 0 is a bare panel
CSK_SIZE   equ 18

; A decoration: five words, drawn once with the face and never read again.
; It is what a panel has that the simulation does not model - a tachometer,
; an oil gauge, the magnetos - and it costs ten bytes and no frame time.
CSD_KIND   equ 0                ; CSDK_*
CSD_X      equ 2                ; centre x at 320 wide
CSD_Y      equ 4                ; ...and its row below the view
CSD_R      equ 6                ; radius in ROWS (the x radius is this over
                                ; the pixel aspect, so it is round everywhere)
CSD_ARG    equ 8                ; a dial's needle angle; a switch's position;
                                ; a RAIL's count in the low byte and its
                                ; up/down mask in the high one, bit 0 first
CSD_SIZE   equ 10
CS_RAILSTEP equ 34              ; a rail's pitch, at 320 wide: eight from x 40
                                ; reach 278 (SPEC.md 88.9.3.1)

CSDK_DIAL  equ 0                ; a bezel and a needle, parked where it is
CSDK_SWITCH equ 1               ; a toggle on a stalk: ARG 0 down, 1 up
CSDK_RAIL  equ 2                ; a ROW of them, CS_RAILSTEP apart from CSD_X:
                                ; nine near-identical table rows are one, which
                                ; is what a rail is (SPEC.md 88.9.3.1)

; --- the session (SPEC.md 88.8) -----------------------------------------------
CS_ST_GROUND equ 0
CS_ST_AIR    equ 1
CS_ST_CRASH  equ 2
CS_MAXSTEP equ 3                ; the most ticks a frame may ever spend
CS_EASEOS equ 3                 ; the ramp OUT of the horizon is 1 << this
                                ; ticks, so under three frames back to the
                                ; full rate (SPEC.md 88.7.3.3)
CS_EASEF equ 5                  ; ...and the most FRAMES an approach to the
CS_EASEN equ CS_EASEF * CS_MAXSTEP  ; horizon is spread over, so the landing
                                ; is a frame's last tick and every frame of
                                ; the approach travels alike (SPEC.md
                                ; 88.7.3.2). Two frames left the last one at
                                ; HALF the roll rate; five is a rung a frame
CS_PRATE  equ 6                 ; ticks between instrument readings (88.9.1)
RW_NDASH  equ 4                 ; centreline stripes drawn ahead (88.6.2)
RW_DASHM  equ 25                ; ...each this long, with as much gap
RW_DASHH  equ 150               ; ...within this height of the runway
RW_DASHW  equ 300               ; ...and this far from its axis
CS_CRASHT  equ 36               ; ticks the crash stays on the glass: 2 s
CS_GRAV    equ 69               ; 9.81 m/s^2 a tick, 16.7 (SPEC.md 88.7.4)
CS_STALLSINK equ 24             ; 16.8 m/s of sink per 1 m/s under the stall
CS_STALLDROP equ 60             ; the nose drops this much a tick, stalled
CS_LIFTOFF equ 546              ; 3 degrees: the nose is up, and it flies
CS_INDMAX  equ 64               ; ...and a backstop on it, 9.1 m/s^2. The
                                ; real bound is the STALL's own q (88.7.12),
                                ; which is what stops the term running away;
                                ; this only catches a record whose numbers
                                ; disagree with each other
CS_RUDDER  equ 24               ; the rudder's yaw a tick, in the air
CS_STEERK  equ 2                ; the nosewheel: hdg += v x this >> 7
CS_LANDVS  equ -384             ; a landing sinks no faster than 3 m/s...
CS_LANDROLL equ 1820            ; ...banked under 10 degrees...
CS_LANDPMIN equ -910            ; ...with the nose between -5...
CS_LANDPMAX equ 2730            ; ...and +15
CS_CEIL    equ 3000             ; metres: what keeps the geometry in a word
CS_EDGE    equ 30000            ; ...and so does this, either way on x and z

; --- messages -----------------------------------------------------------------
CSG_NONE   equ 0
CSG_TAKEOFF equ 1
CSG_STALL  equ 2
CSG_LANDED equ 3
CSG_CRASH  equ 4
CSG_PAUSED equ 5
CSG_EDGE   equ 6
CSG_RELEASE equ 7               ; a sailplane's launch (88.7.6)
CSG_SPLASH equ 8                ; ...and an amphibian's water landing (88.7.7)
CSG_TOAST  equ 9                ; A SETTING JUST CHANGED (88.13.8): its name
                                ; and its new value, composed into cs_toastbuf
                                ; and shown for CS_TOASTT ticks in place of
                                ; whatever the strip was saying
CS_TOASTT  equ 27               ; ...which is a second and a half at 18.2 Hz

; --- the attract window (SPEC.md 88.10) ---------------------------------------
CS_WINW   equ 312               ; the launcher, frame included: it fits
CS_WINH   equ 156               ; between CGA's bar and dock (200 rows)

; --- scancodes this package reads directly (SPEC.md 9.7) ----------------------
CS_KW     equ 0x11
CS_KS     equ 0x1F
CS_KA     equ 0x1E
CS_KD     equ 0x20
CS_KB     equ 0x30

; --- a `loop` and a `jcxz` whose target is past a short jump's reach ---------
; NASM refuses an out-of-range short jump on cpu 8086 rather than widening it.
%macro LOOPF 1
    dec cx
    jz %%end
    jmp %1
%%end:
%endmacro
%macro JCXZF 1
    jcxz %%z
    jmp short %%go
%%z:
    jmp %1
%%go:
%endmacro

; =============================================================================
; cs_entry - the loader calls this once per instance (SPEC.md 20.1)
; =============================================================================
cs_entry:
    ; --- OUR OWN FILE'S NAME, FIRST (SPEC.md 20.2, 88.10.5) -----------------
    ; ES:SI is the name we were launched from - ES is KERNEL_SEG on entry, by
    ; the callback contract - and it points at a buffer the loader REUSES on
    ; the next launch, so a world read minutes from now cannot use it.
    ; Thirteen bytes, banked before anything else runs.
    push ax
    push cx
    push si
    push di
    mov di, cs_lname
    mov cx, 13
.name:
    mov al, [es:si]
    mov [di], al
    inc si
    inc di
    loop .name
    pop di
    pop si
    pop cx
    pop ax

    push si
    push di
    call OSAPI_VIDEO                ; AX = w, BX = h, CX = the dock's top row
    mov [cs_scrw], ax
    mov [cs_dock], cx

    mov byte [cs_setbld], CSBL_MOD   ; the settings' defaults (88.13): all of
    mov byte [cs_setlod], CSL_MOD   ; them are what the simulator shipped
    mov byte [cs_setfill], CSFL_ALL  ; with, so a player who never opens the
                                    ; page is flying exactly what they flew.
                                    ; Size is the fourth and cannot be set
                                    ; here: it is the ADAPTER's, and the
                                    ; adapter is not known until the window
                                    ; exists (below)

    call cs_artload                 ; where the LOADER put the title bands
                                    ; (88.10.4), before the window that draws
                                    ; them exists. A refusal here is a plainer
                                    ; page and not a failed launch

    mov al, KSC_SPACE               ; ARMING the scancode reader: the first
    call OSAPI_KEY_DOWN             ; answer is always "up" and this is where
                                    ; the SDK says to spend it (SPEC.md 9.7)
    mov word [cs_plane], cs_p_c172  ; the rows in use (SPEC.md 88.6)
    mov byte [cs_wldnow], 0FFh      ; nothing in the overlay yet - not even the
                                    ; vocabulary, which is what makes the pick
                                    ; below read both (SPEC.md 88.10.5)
    mov al, CS_DEFPORT              ; ...and THE LOCATION IS A READ now: a
    call cs_wldpick                 ; record lives in the overlay, so naming
                                    ; one here would name an empty one. A
                                    ; refusal leaves [cs_airport] pointing at
                                    ; the record anyway and cs_wldnow at 0xFF,
                                    ; which the Fly item's own predicate sees
    mov byte [cs_sound], 1

    ; Centre the launcher in the desktop band.
    mov ax, [cs_scrw]
    sub ax, CS_WINW
    jns .xok
    xor ax, ax
.xok:
    shr ax, 1
    mov [cs_tpl + WT_X], ax
    mov ax, [cs_dock]
    sub ax, MBAR_H
    sub ax, CS_WINH
    jns .yok
    xor ax, ax
.yok:
    shr ax, 1
    add ax, MBAR_H
    mov [cs_tpl + WT_Y], ax

    mov si, cs_tpl
    call OSAPI_WM_CREATE
    jc .full
    mov [cs_win], bx
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1), and the macro's bare `ret` IS the
    ; proc - because every word that names this region is the kernel's. It
    ; named a cs_reloc for a cycle, on the belief that [cs_artseg] pointed
    ; INSIDE our carve and had to follow it. It does not: csl_art expands the
    ; title bands into a claim of their OWN (OSAPI_MEM_CLAIM, CS_ART_KB) and
    ; hands us THAT segment, the packed stream in the carve being op_dropped
    ; before the handoff. After mem_reown_x that claim is slot-owned with no
    ; proc, so it never moves - and a proc that added the region's delta to
    ; [cs_artseg] would have pointed the title page at 11KB of whatever the
    ; compactor packed there the first time this region moved. Nothing
    ; exercised it; SPEC.md 66.6.1.1 records the correction.
    OS88_REGION_MOVABLE
    mov [cs_drplane + OS88UI_DR_WIN], bx    ; the drop-downs arm their clips
    mov [cs_drport + OS88UI_DR_WIN], bx     ; off it (os88ui.inc)
    mov si, cs_setdrops             ; ...AND THE SETTINGS PAGE'S FOUR, off the
    mov cx, 4                       ; table rather than by name, so a fifth
.win:                               ; control cannot be added to the page and
    mov di, [si]                    ; forgotten here. With WIN zero
    mov [di + OS88UI_DR_WIN], bx    ; OSAPI_WM_CLIP_SET refuses and drpress
    inc si                          ; answers SPENT with the list never drawn
    inc si                          ; - a drop-down that cannot be dropped
    LOOPF .win                      ; down (SPEC.md 88.13.6)
    mov al, 1                       ; an 8-aligned content origin: the two
    call OSAPI_WM_SNAP              ; bands land on the byte grid (SPEC.md
                                    ; 5.4.2) and font_run reaches 6.1's
                                    ; single-store cell
    mov al, 1                       ; every pixel of the page is ours, so the
    call OSAPI_WM_OWNBG             ; kernel's white fill before W_PAINT would
                                    ; only be written over (SPEC.md 11.96)
    call cs_adapter                 ; which raster we would take, whether the
                                    ; machine will give it to us, and the menus
    mov al, CSZ_FULL                ; ...and now Size can take its default,
    cmp byte [cs_want], CSB_HERC    ; which is the adapter's own (88.13.4):
    jne .sz                         ; CGA and Mode X open at the geometry they
    mov al, CSZ_MOD                 ; shipped with and Hercules at its 400-wide
.sz:                                ; view, so nobody's picture changed when
    mov [cs_setsize], al            ; the page arrived. ONCE, here and not in
                                    ; cs_adapter, which runs again on a Mode
                                    ; change and on a window move: neither may
                                    ; overwrite a Size the player picked
    call cs_set_load                ; ...AND THE KEPT ONES OVER ALL FOUR
                                    ; (88.13.9), which is why the read is here
                                    ; and not at the first paint: Size is the
                                    ; last default set and it is set from the
                                    ; ADAPTER, so a file read before this line
                                    ; would have its answer overwritten by the
                                    ; card. The entry proc is UI-task context
                                    ; - it is what opens the window - so the
                                    ; file slots are legal here
    mov ax, cs_onresize             ; the card can change under us
    call OSAPI_WM_ONRESIZE          ; (SPEC.md 11.98)
    mov ax, cs_onup                 ; the release half of a click (13.7)...
    call OSAPI_WM_ONMOUSEUP
    mov ax, cs_ondrag               ; ...and the tracking edge (13.8.2), for
    call OSAPI_WM_ONDRAG            ; the drop-downs' highlight
    mov si, cs_about                ; 'About Clear Skies' above the Close the
    call OSAPI_ABOUT_SET            ; kernel puts in our pull-down (SPEC.md
                                    ; 12.2). WINDOWED only: in the bracket
                                    ; there is no bar to pull down
.full:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; cs_artload - where the LOADER put the title bands (SPEC.md 88.10.4)
;
; out: [cs_artseg] = the art part's base segment, or 0.  preserves every
;      register
;
; The six bands are 10,480 bytes and no frame reads one: the title page draws
; them and the fsx bracket never does. They used to be an LZ4 stream in the
; IMAGE that this expanded into a claim of its own, which cost the package
; 4,487 of a 60KB segment (APP_MAX_SIZE) for bytes nothing on a frame's path
; ever touches. THEY ARE PART 0 NOW: an OP_COMP row that op_load reads and
; expands into the parts carve before the entry proc does anything else, so
; the image carries csart.inc's OFFSETS and not one byte of picture, and this
; routine is the assignment that used to be a claim and a decode.
;
; A BAND HOLDS NO POINTER, which is what made both moves free: what was a
; label in this segment is an offset into the blob (csart.inc's equs), the
; plane records' CSP_ART goes on assembling because an equ is a constant like
; any other, and the three blits read ES = [cs_artseg].
;
; A REFUSAL IS STILL A NORMAL PATH (SPEC.md 20.6, 47) and it is the SAME path:
; op_seg answers 0 for a part that is not there, and 0 is the page the blit
; slot's own refusal already drew - the title lettered in the 8x8 face and no
; aeroplane. Nothing to undo on the way out: the carve is the kernel's to free
; with the instance, exactly as the claim was.
; -----------------------------------------------------------------------------
cs_artload:
    push ax
    xor ax, ax
    cmp word [cs_hand + CSH_MAGIC], 'CS'
    jne .set                        ; NO LOADER, NO ART - and that is a real
                                    ; state rather than a paranoid one: this
                                    ; image is a PART and the only thing that
                                    ; starts it is csload.asm, so a zero magic
                                    ; means the bss was zeroed by an ordinary
                                    ; launch and there is no carve to point at
    mov ax, [cs_hand + CSH_ART]
.set:
    mov [cs_artseg], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; (cs_reloc used to stand here: SPEC.md 66.6.1.1 is why it was wrong and why
; the declaration above takes the macro's bare `ret` instead.)
; -----------------------------------------------------------------------------
; cs_wldpick - put location AL's world in the overlay and make it current
;              (SPEC.md 88.10.5)
; in:  AL = the row in cs_ports / cs_apnames
; out: CF = 0 and [cs_airport] is that location's record; CF = 1 nothing moved
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
;
; A world is 11,904 bytes across nine countries and exactly one is under the
; aeroplane, so the program carries none of them: the loader wrote down where
; each packed stream sits in the file (88.10.4.2's handoff, extended), and this
; reads the one it wants and expands it into cs_wldat.
;
; IT DOES NOTHING IF THE WORLD IS ALREADY THERE, which is most picks: nine
; locations stand in eight worlds, so Paris' two share one, and re-picking the
; same place is a comparison rather than a disk read.
;
; THE VOCABULARY GOES IN ONCE, ahead of the world at cs_vocat - the face tables
; and anonymous models every world's pointers reach into. cs_wldnow is 0xFF
; until it has, which is also what makes the first pick read both.
; -----------------------------------------------------------------------------
cs_wldpick:
    mov [cs_apnow], al              ; WHICH ROW, banked - because the record is
                                    ; in the overlay now and only the loaded
                                    ; world has one. cs_cmd_fly re-picks from
                                    ; this before it takes the machine, so
                                    ; setting the row is the whole of choosing
                                    ; a place: it is what a test pokes, and it
                                    ; is what survives a world being swapped
    push ax
    xor ah, ah
    shl ax, 1
    mov si, ax
    mov ax, [cs_ports + si]         ; the record, wherever the overlay puts it
    mov [cs_airport], ax
    pop ax
    push ax
    xor ah, ah
    mov si, ax
    mov al, [cs_apwld + si]         ; ...and the world it stands in
    cmp byte [cs_wldnow], 0FFh
    jne .have
    push ax                         ; --- the vocabulary, once
    mov al, CS_STREAM_VOCAB
    mov bx, CS_VOCAB_AT / 16
    mov cx, [cs_wstrraw + CS_STREAM_VOCAB * 2]
    call cs_wldget
    pop ax
    jc .no
.have:
    cmp al, [cs_wldnow]
    je .out                         ; already in: Paris' two locations share a
                                    ; world, and so does re-picking the same
    push ax
    add al, CS_STREAM_WLD0
    push bx
    xor bh, bh
    mov bl, al
    shl bx, 1
    mov cx, [cs_wstrraw + bx]       ; THE EXACT LENGTH and not CS_WLD_MAX: the
    pop bx                          ; overlay's ROOM is not what the stream
    mov bx, CS_WLD_AT / 16          ; expands to, and OSAPI_DECOMP CHECKS the
                                    ; number it is given (SPEC.md 20.13.3) -
                                    ; writing the bytes and THEN refusing, so
                                    ; the wrong one reads as a world that
                                    ; loaded and a load that failed (88.10.5.4)
    call cs_wldget
    pop ax
    jc .no
    mov [cs_wldnow], al
.out:
    call cs_ctopbuild               ; the collision walk's per-object ceiling
    pop ax                          ; (88.7.13), which only a pick can change
    clc
    ret
.no:
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; cs_wldget - read stream AL out of our own file and expand it into the overlay
; in:  AL = the stream's row in the loader's directory, BX = the PARAGRAPH the
;      expansion goes to, relative to our own segment, CX = the bytes it
;      expands to
; out: CF = 0 done; CF = 1 nothing was written
; clobbers: AX, BX, CX, DX, SI, DI, ES, flags
;
; TWO CONTRACTS SHAPE THIS AND NEITHER IS OURS. OSAPI_DECOMP wants its
; destination at OFFSET 0 of a segment (SPEC.md 20.13.3), which is why the
; caller passes a paragraph and why both overlay addresses are multiples of 16.
; And OSAPI_FILE_READ_AT wants a CLUSTER multiple for the offset and the
; capacity alike (20.14.3) while a part begins on a 512-byte boundary - so on a
; volume with bigger clusters the read starts BELOW the stream and the slack is
; stepped over afterwards. That is op_claim's arithmetic (20.12.2) done once,
; for one stream, instead of for a whole run.
; -----------------------------------------------------------------------------
cs_wldget:
    mov [cs_wgpar], bx
    mov [cs_wgout], cx
    xor ah, ah
    shl ax, 1
    shl ax, 1                       ; four bytes a row: the sector, the length
    mov si, ax
    mov ax, [cs_hand + CSH_WDIR + si]
    mov [cs_wgsec], ax
    mov ax, [cs_hand + CSH_WDIR + si + 2]
    mov [cs_wglen], ax

    mov ax, [cs_wgsec]              ; --- the stream's byte offset, 32 bits: a
    xor dx, dx                      ;     part may sit past 64KB in the file
    mov cl, 9
    shl ax, cl
    rcl dx, cl
    mov bx, [cs_hand + CSH_CLB]
    dec bx                          ; the cluster mask
    mov cx, ax
    and cx, bx
    mov [cs_wgslk], cx              ; how far past the boundary it begins...
    not bx
    and ax, bx                      ; ...and the boundary itself
    mov [cs_wgoff], ax
    mov [cs_wgoff+2], dx

    mov ax, [cs_wgslk]              ; --- the buffer: slack + stream, rounded
    add ax, [cs_wglen]              ;     UP to whole clusters, which is what
    mov bx, [cs_hand + CSH_CLB]     ;     the read wants for its capacity too
    add ax, bx
    dec ax
    xor dx, dx
    div bx
    mul bx
    mov [cs_wgcap], ax
    add ax, 1023                    ; ...and in KB, for the claim
    mov cl, 10
    shr ax, cl
    call OSAPI_MEM_CLAIM            ; DX = the buffer
    jc .no
    mov [cs_wgseg], dx

    mov es, dx                      ; --- the read
    xor bx, bx
    mov cx, [cs_wgcap]
    mov si, cs_lname
    mov ax, [cs_wgoff]
    mov dx, [cs_wgoff+2]
    call OSAPI_FILE_READ_AT         ; out DX:AX = delivered
    jc .free
    or dx, dx                       ; a short read is a stream that is not all
    jnz .got                        ; here, and expanding half of one writes
                                    ; rubbish into the overlay rather than
                                    ; refusing. **AGAINST WHAT THE STREAM
                                    ; NEEDS AND NOT WHAT THE READ ASKED FOR**
                                    ; (SPEC.md 88.10.5.4.1): [cs_wgcap] is
                                    ; rounded UP to whole clusters because
                                    ; that is what OSAPI_FILE_READ_AT wants
                                    ; for a capacity, so checking against it
                                    ; demands bytes past the end of the LAST
                                    ; stream in the file - which is San
                                    ; Francisco's world, ending exactly at
                                    ; EOF, on every geometry. Every other
                                    ; stream has more file behind it and
                                    ; filled the capacity by accident.
                                    ; op_load's [op_want] is the same
                                    ; predicate in os88partsbody.inc, which
                                    ; is where this read was copied from
    mov bx, [cs_wgslk]
    add bx, [cs_wglen]              ; ...the bytes the expansion consumes
    cmp ax, bx                      ; (BX is dead here - the destination one
    jb .free                        ; below is loaded fresh past .got)
.got:
    ; --- and the expansion, ES:0 by contract (SPEC.md 20.13.3) --------------
    ; EVERY WORD IS FETCHED BEFORE DS MOVES. The stream is in the buffer and
    ; the destination is ours, so this is the one place in the package where DS
    ; is not our own segment - and a `[cs_wg...]` read after the swap reads the
    ; BUFFER at that offset, which is the stream's own bytes and looks like a
    ; plausible number.
    mov ax, ds
    add ax, [cs_wgpar]
    mov es, ax
    mov si, [cs_wgslk]
    mov cx, [cs_wglen]
    mov dx, [cs_wgout]
    mov bx, [cs_wgseg]
    push ds
    mov ds, bx
    xor di, di
    xor bx, bx
    mov al, OSAPI_LZ_LZ4
    call OSAPI_DECOMP
    pop ds
    jc .free
    mov dx, [cs_wgseg]
    call OSAPI_MEM_FREE
    clc
    ret
.free:
    mov dx, [cs_wgseg]
    call OSAPI_MEM_FREE
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; cs_adapter - the raster this machine would give us, the mode to ask for,
;              and the menus that go with it
;
; out: [cs_want] = a CSB_*, [cs_fsxm] = the FSXM_* to set, [cs_caps], the
;      Fly item's caption, and the menu set installed - with a Mode menu
;      only where there is a choice. Preserves every register (cs_onresize
;      is a callback). EVERY BRANCH WRITES BOTH WAYS - SPEC.md 48's lesson.
; OSAPI_FSX_CAPS is asked with our WINDOW in BX, so on a two-card machine the
; answer is about the display this window is on (SPEC.md 39.18.2).
; -----------------------------------------------------------------------------
cs_adapter:
    push ax
    push bx
    push dx
    push si
    mov bx, [cs_win]
    call OSAPI_FSX_CAPS
    mov [cs_caps], ax
    mov [cs_vidk], dl
    mov byte [cs_want], CSB_NONE
    mov byte [cs_fsxm], 0FFh
    ; --- THE 16-COLOUR TEXT HACK IS A REAL CGA'S AND NOBODY ELSE'S (88.15.7).
    ;     It is programmed by writing the 6845 directly, and a VGA or an EGA
    ;     running mode 3 answers 3D4h with a CRTC that is not one - so the
    ;     offer is made on [vid_kind] and not on a caps bit, which is SPEC.md
    ;     47's rule exactly: a fact the code can test. And a VGA loses nothing
    ;     by it, Mode X already having sixteen times the colours.
    mov si, cs_i_mode               ; ...and the Mode row's two names with it
    cmp dl, VID_CGA
    jne .m1
    mov si, cs_i_mode160
.m1:
    mov [cs_drmode + OS88UI_DR_ITEMS], si
    cmp dl, VID_CGA
    jne .modex
    cmp byte [cs_modepref], 0       ; a real CGA's two are 320x200x4 and the
    je .cga                         ; hack, in that order (88.15.7)
    mov byte [cs_want], CSB_C160
    mov byte [cs_fsxm], FSXM_TEXT80
    jmp short .say
.modex:
    test ax, 1 << FSXM_MODEX
    jz .cga
    cmp byte [cs_modepref], 0       ; Mode X, unless CGA was asked for: a
    jne .cga                        ; VGA in an XT runs it at 4 fps (88.12)
    mov byte [cs_want], CSB_MODEX   ; and the Mode menu is the choice
    mov byte [cs_fsxm], FSXM_MODEX
    jmp short .say
.cga:
    test ax, 1 << FSXM_CGA320
    jz .herc
    mov byte [cs_want], CSB_CGA
    mov byte [cs_fsxm], FSXM_CGA320
    jmp short .say
.herc:
    test ax, 1 << FSXM_HERC
    jz .say
    mov byte [cs_want], CSB_HERC
    mov byte [cs_fsxm], FSXM_HERC
.say:
    mov dx, cs_s_fly                ; the menu says WHY not, off the same
    cmp byte [cs_want], CSB_NONE    ; predicate the command refuses on
    jne .ok                         ; (SPEC.md 47)
    mov dx, cs_s_flyn
.ok:
    mov [cs_mi_flight + 0], dx
    ; --- ONE menu now: the Mode choice moved onto the Settings page
    ;     (SPEC.md 88.13), where it sits beside the other four things that
    ;     trade picture for frame rate rather than alone in the bar ---------
    mov si, cs_menus
    mov bx, [cs_win]                ; the kernel keeps a COPY of the set
    call OSAPI_MENU_SET             ; (SPEC.md 12.2): installed afresh
    pop si
    pop dx
    pop bx
    pop ax
    ret

cs_onresize:
    call cs_adapter
    ret

; =============================================================================
; The windowed half: the title page (SPEC.md 88.10) - the lettering and the
; aeroplane are two 1bpp bands from tools/csart.py, the plane and the
; airport are os88ui.inc's drop-downs (the first two anywhere), Fly is the
; standard button, and the instructions are a second page of the same window
; =============================================================================
CS_TITLEX equ 16                    ; the bands, in content coordinates: both
CS_TITLEY equ 2                     ; x's are multiples of 8 (OSAPI_GFX_BLIT1)
CS_ARTX   equ 152
CS_ARTY   equ 40
CS_COLX   equ 8                     ; the left column: two labels, two
CS_DROPW  equ 128                   ; drop-downs and the button
CS_DROPH  equ 16
CS_PLANEY equ 56
CS_PORTY  equ 88
CS_FLYY   equ 112
CS_FLYW   equ 72
CS_FLYH   equ 18
CS_LINEH  equ 10                    ; the instructions page's line pitch

; --- the Settings page (SPEC.md 88.13): four drop-downs down the left with
;     their labels, three fill boxes across, and Done ----------------------
; TWO COLUMNS, and the content is 312x137: four drop-downs down one column
; would put the last one's list past the bottom edge, where it would be
; clipped away. Each label sits above its control, as the title page's do.
CS_SETX   equ 8                     ; the left column, and the right
CS_SETX2  equ 164
CS_SETDW  equ 140                   ; a control's width
CS_SETY   equ 16                    ; the first label's row...
CS_SETDY  equ 34                    ; ...and the pitch down to the second
CS_SETLH  equ 11                    ; a label's height above its control
CS_SETFY  equ 90                    ; the fill boxes' row...
CS_SETFX  equ 44                    ; ...their first column and pitch
CS_SETFW  equ 124                   ; ...WIDE ENOUGH FOR THE NOTES (88.13.11):
CS_SETFB  equ 120                   ; 'Buildings' plus its own is 17 + 72 + 8 +
                                    ; 32 = 129 from the box's left edge, and
                                    ; the row has 44..304 for two of them
CS_SETBY  equ 112                   ; Done

; -----------------------------------------------------------------------------
; cs_paint - W_PAINT.  in: SI = window ptr; gfx lock held.  preserves all
; -----------------------------------------------------------------------------
cs_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bx, [cs_win]
    call OSAPI_WM_CONTENT
    mov [cs_winox], ax
    mov [cs_winoy], dx
    mov bx, [cs_win]
    call OSAPI_WM_GEOM              ; CX/DX = the content box
    jc .out
    mov [cs_cw], cx
    mov [cs_ch], dx

    mov al, CWHITE                  ; the page: ours (OSAPI_WM_OWNBG), and it
    call OSAPI_SET_COLOR            ; is what takes a dropped list or the
    mov ax, [cs_winox]              ; instructions back down
    mov bx, [cs_winoy]
    mov cx, ax
    add cx, [cs_cw]
    dec cx
    mov dx, bx
    add dx, [cs_ch]
    dec dx
    call OSAPI_GFX_FILL
    cmp byte [cs_page], 0
    je .title
    cmp byte [cs_page], 2
    je .settings
    jmp .instr
.title:

    ; --- the title, one blit; lettered in the 8x8 face where the blit is
    ;     refused (kern_small carries the slot and not the body) OR where the
    ;     bands never unpacked, which is the same plainer page (88.10.2) ----
    mov es, [cs_artseg]             ; ES:offset, not DS:label - the bands live
    mov si, es                      ; in cs_artload's claim now. A zero segment
    or si, si                       ; is "there is no claim", and it takes the
    jz .notitle                     ; path a refused blit already took
    mov ax, [cs_winox]
    add ax, CS_TITLEX
    mov bx, [cs_winoy]
    add bx, CS_TITLEY
    mov cx, cs_art_title_w
    mov dx, cs_art_title_h
    mov bp, cs_art_title_w / 8
    mov si, cs_art_title
    call OSAPI_GFX_BLIT1
    jnc .plane
.notitle:
    mov si, cs_s_title
    mov bx, CS_TITLEY + 16
    mov al, CBLACK
    call cs_at_centre
.plane:                             ; --- the aeroplane in front of its cloud:
                                    ; the band of WHICHEVER row is in use
                                    ; (88.10.1), so the picture follows the
                                    ; Plane list. Every band shares the frame,
                                    ; so only the offset changes
    mov es, [cs_artseg]
    mov si, es
    or si, si
    jz .noplane
    mov ax, [cs_winox]
    add ax, CS_ARTX
    mov bx, [cs_winoy]
    add bx, CS_ARTY
    mov cx, cs_art_plane_w
    mov dx, cs_art_plane_h
    mov bp, cs_art_plane_w / 8
    mov si, [cs_plane]
    mov si, [si + CSP_ART]
    call OSAPI_GFX_BLIT1            ; refused: a plainer page, and that is all
.noplane:
    push ds                         ; ES back to ours: everything below this
    pop es                          ; point is written against DS = ES

    ; --- the configuration: the labels, the controls' rects (they follow
    ;     the window), then the controls, LOWEST FIRST - a dropped list lies
    ;     over whatever is under it -----------------------------------------
    mov al, CBLACK
    mov si, cs_s_plane
    mov cx, CS_COLX
    mov bx, CS_PLANEY - 10
    call cs_at_left
    mov si, cs_s_airport
    mov bx, CS_PORTY - 10
    call cs_at_left
    mov di, cs_drplane
    mov ax, CS_COLX
    mov bx, CS_PLANEY
    mov cx, CS_COLX + CS_DROPW - 1
    mov dx, CS_PLANEY + CS_DROPH - 1
    call cs_rect_at
    mov di, cs_drport
    mov bx, CS_PORTY
    mov dx, CS_PORTY + CS_DROPH - 1
    call cs_rect_at
    mov di, cs_flyrect
    mov bx, CS_FLYY
    mov cx, CS_COLX + CS_FLYW - 1
    mov dx, CS_FLYY + CS_FLYH - 1
    call cs_rect_at
    call cs_flybtn
    mov bx, cs_drport
    xor di, di
    call os88ui_drop
    mov bx, cs_drplane
    call os88ui_drop
    jmp short .card
.settings:
    call cs_set_page
    jmp short .card
.instr:
    call cs_instr_page
.card:
    cmp byte [cs_abon], 0           ; ...and the About card LAST, over the
    je .out                         ; page it is opaque about (20.5.1)
    mov bx, [cs_win]
    mov si, cs_ablines
    call os88ui_about_d
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_set_page - the SETTINGS page (SPEC.md 88.13): four drop-downs with their
;               labels, three fill boxes, and Done. Every control's rect is
;               written here in screen coordinates, so the page follows the
;               window without anything remembering where it was.
; in:  [cs_winox]/[cs_winoy] current; the gfx lock is held
; -----------------------------------------------------------------------------
cs_set_page:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, cs_s_setttl             ; the heading
    mov bx, 4
    mov al, CBLACK
    call cs_at_centre
    ; --- the rows: a label, and the control's rect beside it ---------------
    call cs_nsets                   ; ...however many this display HAS
    mov cx, di                      ; (DI = the last, so the count is
    inc cx                          ;  one more - 88.13.11)
    xor di, di                      ; DI = the control: 0 and 1 down the left
.row:                               ; column, 2 and 3 down the right
    push cx
    push di
    mov ax, di
    and ax, 1                       ; the row within the column
    mov bl, CS_SETDY
    mul bl
    add ax, CS_SETY
    mov bx, ax                      ; BX = the label's row
    mov ax, CS_SETX
    test di, 2
    jz .col
    mov ax, CS_SETX2
.col:
    push ax
    mov cx, ax
    mov si, di
    shl si, 1
    mov si, [cs_setlbls + si]
    mov al, CBLACK
    call cs_at_left
    push si                         ; ...and its hotkey beside it (88.13.11)
    mov si, di
    shl si, 1
    mov di, [cs_sethk + si]
    pop si
    or di, di
    jz .nohk
    call cs_hkat
.nohk:
    pop ax
    add bx, CS_SETLH                ; ...and the control under it
    mov cx, ax
    add cx, CS_SETDW - 1
    mov dx, bx
    add dx, CS_DROPH - 1
    pop si                          ; the control's index, banked at the top
    push si
    shl si, 1
    mov di, [cs_setdrops + si]      ; cs_rect_at wants the RECORD in DI
    call cs_rect_at
    pop di
    pop cx
    inc di
    LOOPF .row
    ; --- the fill boxes, across --------------------------------------------
    mov si, cs_s_lfill
    mov cx, CS_SETX
    mov bx, CS_SETFY + 2
    mov al, CBLACK
    call cs_at_left
    mov cx, CS_NFILL
    xor di, di
.box:
    push cx
    mov ax, di
    mov bl, CS_SETFW
    mul bl
    add ax, CS_SETFX
    mov cx, ax
    add cx, CS_SETFB - 1
    mov bx, CS_SETFY
    mov dx, bx
    add dx, 11
    push di
    mov si, di
    shl si, 1
    mov di, [cs_setboxes + si]
    call cs_rect_at
    pop di
    pop cx
    inc di
    LOOPF .box
    mov ax, CS_SETX2                ; ...and Done, under the right column
    mov bx, CS_SETBY
    mov cx, ax
    add cx, CS_FLYW - 1
    mov dx, bx
    add dx, CS_FLYH - 1
    mov di, cs_donerect
    call cs_rect_at
    ; --- now DRAW them, the drop-downs LOWEST FIRST so an open list lies
    ;     over what is under it ---------------------------------------------
    call cs_setsync                 ; the records say what the settings say
    call cs_donebtn
    mov cx, CS_NFILL
    xor di, di
.dbox:
    push cx
    mov si, di
    shl si, 1
    mov bx, [cs_setboxes + si]
    push di
    xor di, di
    call os88ui_chk
    pop di
    push di                         ; ...and its hotkey after the label the
    mov ax, di                      ; widget just drew (SPEC.md 88.13.11)
    mov bl, CS_SETFW
    mul bl
    add ax, CS_SETFX + OS88UI_CKBOX + OS88UI_CKGAP
    mov cx, ax                      ; CX = where that label starts...
    mov bx, CS_SETFY + 2            ; ...and the row os88ui_chk centres it on
    mov si, di
    shl si, 1
    mov di, [cs_fillhk + si]
    mov si, [cs_setboxes + si]
    mov si, [si + OS88UI_CK_LABEL]
    call cs_hkat
    pop di
    pop cx
    inc di
    LOOPF .dbox
    call cs_nsets                   ; ...however many this display HAS
    mov cx, di                      ; (DI = the last, so the count is
    inc cx                          ;  one more - 88.13.11)
.ddrop:
    push cx
    mov si, di
    shl si, 1
    mov bx, [cs_setdrops + si]
    push di
    xor di, di
    cmp bx, cs_drmode               ; the Mode row is greyed where the
    jne .live                       ; adapter offers no choice (SPEC.md 47)
    call cs_modechoice
    jnc .live
    mov di, OS88UI_DIS
.live:
    call os88ui_drop
    pop di
    pop cx
    dec di
    LOOPF .ddrop
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_modechoice - CF = 0 where the display offers TWO of them, which is the
;                 only case where the Mode row means anything (SPEC.md 47)
;
; A VGA's two are Mode X and CGA320; a real CGA's are CGA320 and SPEC.md
; 88.15's 16-colour text hack, which is why [cs_modepref] is "which of the
; two" and not a mode id - the names beside it are cs_adapter's to set, and
; the byte then means the same thing on both machines. Hercules has one
; mode and the row stays greyed there.
cs_modechoice:
    push ax
    cmp byte [cs_vidk], VID_CGA
    je .yes
    mov ax, [cs_caps]
    and ax, (1 << FSXM_MODEX) | (1 << FSXM_CGA320)
    cmp ax, (1 << FSXM_MODEX) | (1 << FSXM_CGA320)
    je .yes
    pop ax
    stc
    ret
.yes:
    pop ax
    clc
    ret

; cs_nsets - DI = the LAST live Settings row's index (SPEC.md 88.13.11); a
;            caller wanting the COUNT takes `mov cx, di` / `inc cx`.
;            EVERY OTHER REGISTER IS PRESERVED, CX INCLUDED, and that is the
;            whole reason it answers in DI: three of the seven walks are the
;            CLICK dispatch and hold the point in CX/DX, so a helper that
;            returned a count in CX ate the x on its way past. Every list
;            then failed to open, which reads as the drop-down being broken
;            rather than as a clobbered argument.
;
; HIDDEN AND NOT GREYED. SPEC.md 47 rule 2 greys a control the machine could
; use in another state and this is not one: a Hercules has ONE mode for the
; life of the session, so the row can never come alive and a greyed box is a
; promise the machine cannot keep. Greying it was also the shape that let the
; field open it - a control drawn disabled still took the press until
; 13.14.5, and a row that is not drawn at all takes none by construction,
; because every walk on this page is bounded by this count.
cs_nsets:
    push ax
    push cx
    mov cx, CS_NSETALL
    call cs_modechoice
    jnc .out
    dec cx                          ; one display mode: no Mode row
.out:
    mov di, cx
    dec di
    pop cx
    pop ax
    ret

; cs_setsync - the controls' selections FROM the settings, and the boxes'
;              ticks from the fill mask. Preserves everything
cs_setsync:
    push ax
    push bx
    push cx
    push si
    push di
    call cs_nsets                   ; ...however many this display HAS
    mov cx, di                      ; (DI = the last, so the count is
    inc cx                          ;  one more - 88.13.11)
    xor di, di
.d:
    mov si, di
    shl si, 1
    mov bx, [cs_setbytes + si]      ; the byte this row edits
    mov al, [bx]
    xor ah, ah
    mov bx, [cs_setdrops + si]
    mov [bx + OS88UI_DR_SEL], ax
    inc di
    loop .d
    mov cx, CS_NFILL
    xor di, di
    mov ah, CSFL_TERRAIN
.b:
    mov si, di
    shl si, 1
    mov bx, [cs_setboxes + si]
    mov al, [cs_setfill]
    and al, ah
    jz .off
    mov al, 1
.off:
    mov [bx + OS88UI_CK_ON], al
    shl ah, 1
    inc di
    loop .b
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; cs_settake - a drop-down on the Settings page answered: put its pick into
;              the byte it edits, and act on it. in: DI = the row, AL = the
;              pick. Clobbers everything
cs_settake:
    mov si, di
    shl si, 1
    mov bx, [cs_setbytes + si]
    mov [bx], al
    cmp di, 3                       ; Mode: the backend, the Fly caption and
    jne .out                        ; the button's greying all follow it
    call cs_adapter
.out:
    ret

; cs_setfillmask - the fill boxes back into [cs_setfill]. Preserves all
cs_setfillmask:
    push ax
    push bx
    push cx
    push si
    push di
    xor al, al
    mov ah, CSFL_TERRAIN
    mov cx, CS_NFILL
    xor di, di
.b:
    mov si, di
    shl si, 1
    mov bx, [cs_setboxes + si]
    cmp byte [bx + OS88UI_CK_ON], 0
    je .next
    or al, ah
.next:
    shl ah, 1
    inc di
    loop .b
    mov [cs_setfill], al
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cs_setclick - a press on the Settings page. in: CX/DX = the point
; -----------------------------------------------------------------------------
cs_setclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    ; --- AN OPEN LIST FIRST (SPEC.md 13.14.2), which is what .page0 already
    ;     does and this did not. The walk below is in the order the page
    ;     DRAWS in (cs_set_page counts DI down, so row 0 is drawn last and
    ;     lies on top), and os88ui_drpress lets a CLOSED control claim a
    ;     press that lands on its own box - so a press on the open list's
    ;     lower items went to whatever box the list was covering. Buildings
    ;     is row 0 and its list falls over Detail, so Moderate and High were
    ;     unreachable from the page; None arriving as a fourth item is what
    ;     walked into it (88.13.6).
    call cs_nsets                   ; ...the last row this display has
.o:
    mov si, di
    shl si, 1
    mov bx, [cs_setdrops + si]
    cmp byte [bx + OS88UI_DR_OPEN], 0
    jne .d                          ; ...start the walk at the open one
    dec di
    jns .o
    call cs_nsets                   ; none open: the drawn order will do
.d:
    push di
    mov si, di
    shl si, 1
    mov bx, [cs_setdrops + si]
    call os88ui_drpress             ; AH spent, AL the pick, CF a repaint
    pop di
    push di                         ; ...the ROW, across everything below
    pushf
    push ax
    cmp al, 0FFh
    je .nopick
    call cs_settake                 ; the pick, into the byte it edits - and
    pop ax                          ; a Mode pick regreys the Fly button, so
    popf                            ; the page is drawn again either way
    pop di
    call cs_repaint
    jmp short .out
.nopick:
    pop ax
    popf
    pop di
    jnc .dnext                      ; nothing came down over the page
    call cs_repaint
    jmp short .out
.dnext:
    or ah, ah
    jnz .out                        ; spent here, and nothing owes a repaint
    dec di
    jns .d
    xor di, di                      ; --- the fill boxes. THE COUNT IS NOT IN
.b:                                 ;     CX: CX is the press's x, and every
    push di                         ;     hit test below still needs it
    mov si, di
    shl si, 1
    mov bx, [cs_setboxes + si]
    call os88ui_chkhit              ; it toggles and redraws itself
    pop di
    jnc .filled
    inc di
    cmp di, 3
    jb .b
    jmp short .done
.filled:
    call cs_setfillmask
    jmp short .out
.done:
    mov bx, cs_donerect             ; --- Done: ARMED here and fired at the
    call os88ui_bhit                ; release, which is the whole gesture
    jc .out                         ; (SPEC.md 13.7) - it acted on the press
    mov ax, 2                       ; and showed nothing, where the Fly button
    call os88ui_arm                 ; beside it has always done both
    mov byte [cs_donedn], 1
    call cs_donedraw
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_flybtn - the Fly button as it stands: greyed with no mode to fly in
;             (SPEC.md 47), down while pressed. in: [cs_flyrect] current

; --- the one control's staging (SPEC.md 20.5.1.3) --------------------------
; One button at a time: this package's rects are not one contiguous group,
; so the record is pointed at whichever rect the caller staged.
cs_btlbl: dw 0
cs_btflg: dw 0
    OS88UI_BTNREC cs_btrec, 0, cs_btlbl, cs_btflg, 1

cs_flybtn:
    push bx
    push si
    push di
    mov bx, cs_flyrect
    mov si, cs_s_flybtn
    mov di, OS88UI_DEF | OS88UI_FILL
    cmp byte [cs_want], CSB_NONE
    jne .live
    or di, OS88UI_DIS
.live:
    cmp byte [cs_flydn], 0
    je .draw
    or di, OS88UI_DOWN
.draw:
    push ax                     ; THE ONE CONTROL (SPEC.md 20.5.1.3): BX
    push bx                     ; already holds this button's rect, SI its
    mov [cs_btlbl], si          ; label and DI its flags, so the record takes
    mov [cs_btflg], di          ; all three and the picture is identical
    mov [cs_btrec+OS88UI_BT_RECTS], bx
    mov bx, cs_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    pop ax
    pop di
    pop si
    pop bx
    ret

; cs_donebtn - the Settings page's Done as it stands: down while pressed.
;              Never greyed - leaving a page always means something
cs_donebtn:
    push bx
    push si
    push di
    mov bx, cs_donerect
    mov si, cs_s_done
    mov di, OS88UI_DEF | OS88UI_FILL
    cmp byte [cs_donedn], 0
    je .draw
    or di, OS88UI_DOWN
.draw:
    push ax                     ; THE ONE CONTROL (SPEC.md 20.5.1.3): BX
    push bx                     ; already holds this button's rect, SI its
    mov [cs_btlbl], si          ; label and DI its flags, so the record takes
    mov [cs_btflg], di          ; all three and the picture is identical
    mov [cs_btrec+OS88UI_BT_RECTS], bx
    mov bx, cs_btrec
    mov al, 1
    call os88ui_btn
    pop bx
    pop ax
    pop di
    pop si
    pop bx
    ret

; cs_donedraw - it alone, from a click handler: cs_flydraw's reason exactly
cs_donedraw:
    push bx
    mov bx, [cs_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call cs_donebtn
.out:
    pop bx
    ret

; cs_flydraw - the button alone, from a click handler: no region is armed
;              there (SPEC.md 11.3), so one is
cs_flydraw:
    push bx
    mov bx, [cs_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    call cs_flybtn
.out:
    pop bx
    ret

; cs_rect_at - DI -> a 4-word rect; AX/BX/CX/DX = it, in CONTENT coordinates:
;              write it in screen coordinates. Preserves every register
cs_rect_at:
    push ax
    add ax, [cs_winox]
    mov [di], ax
    mov ax, bx
    add ax, [cs_winoy]
    mov [di+2], ax
    mov ax, cx
    add ax, [cs_winox]
    mov [di+4], ax
    mov ax, dx
    add ax, [cs_winoy]
    mov [di+6], ax
    pop ax
    ret

; cs_instr_page - the instructions, a line table down the page
cs_instr_page:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, cs_i_lines
    mov bx, 6
    mov cx, CS_COLX
    mov al, CBLACK
.line:
    mov si, [di]
    or si, si
    jz .done
    call cs_at_left
    add di, 2
    add bx, CS_LINEH
    jmp short .line
.done:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; cs_repaint - the page again, from a handler: the kernel arms a region for
;              W_PAINT and for nothing else (SPEC.md 11.3), so one is armed
;              here and dies at its own gfx_unlock
cs_repaint:
    push bx
    mov bx, [cs_win]
    call OSAPI_WM_CLIP_SET
    jc .gone
    call cs_paint
.gone:
    pop bx
    ret

; -----------------------------------------------------------------------------
; cs_artdraw - put the aeroplane in use back up, and NOTHING else (88.10.1).
;              A pick from the Plane list changes exactly one picture on the
;              page, so it costs ONE BLIT rather than a repaint: the drop-down
;              has already put back the pixels its list covered and redrawn
;              its own box (13.14.1, os88ui_drup returning CF = 0), and
;              nothing else on the page depends on which aeroplane it is.
;              Where the list had no bank to restore, os88ui_drup asks for the
;              repaint instead and the caller takes that path, so the picture
;              is never drawn twice - a double draw being visible on the
;              target machine (PERFORMANCE.md).
; in:  the gfx lock is held.  preserves all
; -----------------------------------------------------------------------------
cs_artdraw:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    push es
    cmp byte [cs_page], 0           ; only the title page carries a picture
    jne .out
    mov bx, [cs_win]
    call OSAPI_WM_CLIP_SET
    jc .out
    mov bx, [cs_win]
    call OSAPI_WM_CONTENT           ; AX/DX, read again rather than remembered:
    mov [cs_winox], ax              ; a window that moved moved the picture
    mov [cs_winoy], dx
    mov es, [cs_artseg]             ; the claim, or 0 - and with no claim there
    mov si, es                      ; is no picture to put back (88.10.2)
    or si, si
    jz .out
    add ax, CS_ARTX
    mov bx, dx
    add bx, CS_ARTY
    mov cx, cs_art_plane_w
    mov dx, cs_art_plane_h
    mov bp, cs_art_plane_w / 8
    mov si, [cs_plane]
    mov si, [si + CSP_ART]
    call OSAPI_GFX_BLIT1            ; refused: the page keeps the last one
.out:
    pop es
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_at_left - SI = string, BX = content y, CX = content x, AL = ink, on the
;              white page
cs_at_left:
    push ax
    push bx
    push cx
    push dx
    push si
    mov dx, [cs_winoy]
    add dx, bx
    add cx, [cs_winox]
    mov ah, CWHITE
    call OSAPI_FONT_RUN
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_at_centre - SI = string, BX = content y, AL = ink; centred on the window
cs_at_centre:
    push cx
    push si
    call cs_strlen                  ; CX = its length
    shl cx, 1
    shl cx, 1
    shl cx, 1
    push ax
    mov ax, [cs_cw]
    sub ax, cx
    jns .ok
    xor ax, ax
.ok:
    shr ax, 1
    and al, 0F8h                    ; onto a cell boundary (SPEC.md 6.1)
    mov cx, ax
    pop ax
    call cs_at_left
    pop si
    pop cx
    ret

; cs_hkat - a hotkey note after a label (SPEC.md 88.13.11)
; in:  SI = the label, DI = the note, BX = content y, CX = the label's x
; Preserves every register. The note is placed off the label's OWN length
; rather than at a column, because 'Size' and 'Draw Distance' are 9 pixels
; and 104 wide and a fixed column puts one of them in the next control.
cs_hkat:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push cx
    call cs_strlen                  ; CX = the label's length...
    mov ax, cx
    shl ax, 1
    shl ax, 1
    shl ax, 1                       ; ...in pixels, the face being 8 wide
    pop cx
    add cx, ax
    add cx, 8                       ; one cell of daylight
    mov si, di
    mov al, CBLACK
    call cs_at_left
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_strlen - SI = string; out CX = its length. Preserves SI.
cs_strlen:
    push si
    xor cx, cx
.s:
    cmp byte [si], 0
    je .d
    inc si
    inc cx
    jmp short .s
.d:
    pop si
    ret

; -----------------------------------------------------------------------------
; The gestures (SPEC.md 13.7, 13.8.2): the press, the drag, the release
; -----------------------------------------------------------------------------
; cs_onclick - W_ONCLICK.  in: CX/DX = the point, SI = window; gfx lock held
;
; EVERY REGISTER GOES BACK, SI ABOVE ALL: ui_task arms the release with the
; SI it handed over AFTER this returns (`mov [ui_armw], si` in
; .content_front), so a handler that comes back with SI holding something
; else has named that something as the window the release is owed to, and
; the kernel far-calls through it. The first build returned with SI = the
; airport record the pick had just chosen, and the release went into the
; weeds at D3E8 - which read, on the glass, as the launcher working and the
; menu bar dying three seconds later
cs_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call cs_abdismiss               ; the card takes the click
    jc .out
    cmp byte [cs_page], 0
    je .page0
    cmp byte [cs_page], 2
    je .page2
    mov byte [cs_page], 0           ; the instructions: any click returns
    call cs_repaint
    jmp .out
.page2:
    call cs_setclick
    jmp .out
.page0:
    mov bx, cs_drport               ; THE OPEN LIST FIRST (SPEC.md 13.14.2), and
    cmp byte [bx + OS88UI_DR_OPEN], 0   ; that is not the same as the drawn
    jne .p0drop                     ; order any more: the nine-item Location
    mov bx, cs_drplane              ; list slides UP over the PLANE box, and
    cmp byte [bx + OS88UI_DR_OPEN], 0   ; os88ui_drpress lets a closed control
    jne .p0drop                     ; claim a press that lands on its own box -
    mov bx, cs_drplane              ; so the box underneath took the press and
    call os88ui_drpress             ; opened ITS list while the one on top was
    call cs_drtake                  ; still up. Neither open: the drawn order,
    jc .out                         ; where a press can only be on one box
    mov bx, cs_drport
.p0drop:
    call os88ui_drpress             ; an open list takes any press, wherever
    call cs_drtake                  ; it lands, so this is the whole of it
    jc .out
    cmp byte [cs_want], CSB_NONE    ; the button: greyed, it refuses
    je .out
    mov bx, cs_flyrect
    call os88ui_bhit
    jc .out
    mov ax, 1                       ; armed, and drawn down until the release
    call os88ui_arm
    mov byte [cs_flydn], 1
    call cs_flydraw
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_onup - W_ONMOUSEUP.  in: CX/DX = the point; gfx lock held
cs_onup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [cs_page], 2
    jne .title
    call cs_setup2                  ; the Settings page's own controls
    jmp .out
.title:
    mov bx, cs_drplane              ; a release over an item picks it
    call os88ui_drup
    call cs_drtake
    jc .out
    mov bx, cs_drport
    call os88ui_drup
    call cs_drtake
    jc .out
    call os88ui_fire                ; the button: pressed and released on it
    or ax, ax                       ; is the whole gesture (SPEC.md 13.7)
    jz .out
    mov byte [cs_flydn], 0
    call cs_flydraw                 ; up again, whatever the release was
    mov bx, cs_flyrect
    call os88ui_bhit
    jc .out                         ; released elsewhere: a cancel
    call cs_cmd_fly
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_ondrag - W_ONDRAG.  in: CX/DX = the point; gfx lock held
cs_ondrag:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [cs_page], 2
    jne .title
    call cs_nsets                   ; ...however many this display HAS
    mov cx, di                      ; (DI = the last, so the count is
    inc cx                          ;  one more - 88.13.11)
    xor di, di
.d:
    push cx
    mov si, di
    shl si, 1
    mov bx, [cs_setdrops + si]
    push di
    call os88ui_drdrag
    pop di
    pop cx
    inc di
    LOOPF .d
    jmp short .out
.title:
    mov bx, cs_drplane
    call os88ui_drdrag
    mov bx, cs_drport
    call os88ui_drdrag
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_setup2 - the release half on the Settings page: over an item it picks
;             (SPEC.md 13.14), and the Done button fires
cs_setup2:
    call cs_nsets                   ; ...the last row this display has
.d:
    push di
    mov si, di
    shl si, 1
    mov bx, [cs_setdrops + si]
    call os88ui_drup
    pop di
    push di
    pushf
    push ax
    cmp al, 0FFh
    je .nopick
    call cs_settake
    pop ax
    popf
    pop di
    call cs_repaint
    ret
.nopick:
    pop ax
    popf
    pop di
    jnc .next
    call cs_repaint
    ret
.next:
    or ah, ah
    jnz .fire
    dec di
    jns .d
.fire:
    call os88ui_fire                ; the Done button, whose press armed it
    or ax, ax
    jz .out
    mov byte [cs_donedn], 0
    mov bx, cs_donerect
    call os88ui_bhit
    jc .cancel                      ; released elsewhere: up again, and stay
    mov byte [cs_page], 0
    call cs_set_save                ; ...and the page's answer is kept (88.13.9)
    call cs_repaint
    ret
.cancel:
    call cs_donedraw
.out:
    ret

; cs_drtake - what a drop-down answered: a pick lands in the record it
;             names, a fresh flight is owed, a repaint if the control says so
; in:  BX = the record, AX/CF = os88ui_drpress's or os88ui_drup's answers
; out: CF = 1 the press was spent on this control. Preserves everything else
cs_drtake:
    pushf
    cmp al, 0FFh
    je .nopick
    push ax
    push si
    xor ah, ah
    shl ax, 1
    mov si, ax
    cmp bx, cs_drplane
    jne .port
    mov si, [cs_planes + si]
    mov [cs_plane], si
    jmp short .picked
.port:
    shr ax, 1                       ; back to the row: cs_wldpick reads the
    push bx                         ; world in and sets [cs_airport] itself,
    call cs_wldpick                 ; both of which a pick now means
    pop bx                          ; (SPEC.md 88.10.5). BX IS THE DROP-DOWN
                                    ; and it is compared again below, past
                                    ; .picked - cs_wldpick spends it on the
                                    ; expansion's destination paragraph
.picked:
    mov byte [cs_inited], 0         ; the next flight starts on the pick's
    pop si                          ; runway, in the pick's aeroplane
    pop ax
.nopick:
    popf
    jc .rep                         ; no bank to restore: the whole page
    cmp bx, cs_drplane              ; ...otherwise the list put its own pixels
    jne .norep                      ; back, and the only thing on the page
    cmp al, 0FFh                    ; that a pick changed is the PICTURE
    je .norep                       ; (88.10.1) - which is one blit
    call cs_artdraw
    jmp short .norep
.rep:
    call cs_repaint
.norep:
    or ah, ah
    jz .free
    stc
    ret
.free:
    clc
    ret

; cs_drcloseall - Esc, a menu, a key: any open list comes down
cs_drcloseall:
    push bx
    mov bx, cs_drplane
    call os88ui_drclose
    jc .rep
    mov bx, cs_drport
    call os88ui_drclose
    jnc .out
.rep:
    call cs_repaint
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; cs_onkey - W_ONKEY.  in: AL = ascii, SI = window; gfx lock held
; -----------------------------------------------------------------------------
cs_onkey:
    push ax
    push bx
    call cs_abdismiss               ; any key takes the credits down, and is
    jc .out                         ; spent doing it
    cmp byte [cs_page], 0
    je .page0
    cmp byte [cs_page], 2           ; a key leaves EITHER second page, and only
    jne .noset                      ; the Settings one has anything to keep
    call cs_set_save
.noset:
    mov byte [cs_page], 0           ; ...and the instructions
    call cs_repaint
    jmp short .out
.page0:
    cmp al, 27
    jne .notesc
    call cs_drcloseall              ; Esc: an open list down
    jmp short .out
.notesc:
    cmp ax, KEY_ALTENTER            ; Alt+Enter is the same door (SPEC.md
    je .go                          ; 11.2.1.1), and a plain Enter already
                                    ; opens it four lines down - so this is
                                    ; the chord agreeing with the key rather
                                    ; than a new action
    cmp al, 'f'
    je .go
    cmp al, 'F'
    je .go
    cmp al, 0x0D                    ; Enter: the default button
    jne .out
.go:
    call cs_drcloseall
    call cs_cmd_fly
.out:
    pop bx
    pop ax
    ret

; cs_oncmd - the menu handler.  in: AL = item index, AH = menu index
cs_oncmd:
    push ax
    push bx
    call cs_abdismiss
    call cs_drcloseall
    or al, al
    jz .fly
    mov byte [cs_page], 2           ; Flight -> Settings (1) or Instructions
    cmp al, 1                       ; (2), which are pages 2 and 1
    je .page
    mov byte [cs_page], 1
.page:
    call cs_repaint
    jmp short .out
.fly:
    call cs_cmd_fly
.out:
    pop bx
    pop ax
    ret

; =============================================================================
; Menus, strings, the tables the drop-downs read, the About card
; =============================================================================
    OS88_MENUSET cs_menus, cs_m_name, cs_oncmd
        OS88_MENU cs_m_flight, cs_mi_flight, 3
    OS88_MENUSET_END cs_menus
cs_m_name:   db 'Clear Skies', 0
cs_m_flight: db 'Flight', 0
cs_mi_flight: dw cs_s_fly, cs_s_setts, cs_s_instr  ; the first rewritten by
cs_s_fly:    db 'Fly', 0                          ; cs_adapter when no mode
cs_s_flyn:   db 'Fly (no mode)', 0                ; can be had
cs_s_instr:  db 'Instructions', 0

cs_ttl:      db 'Clear Skies', 0
cs_s_title:  db 'CLEAR SKIES', 0    ; the title where the blit is refused
cs_s_plane:  db 'Plane', 0
cs_s_airport: db 'Location', 0
cs_s_flybtn: db 'Fly', 0

; the drop-downs (os88ui.inc, OS88UI_DROP): the rect follows the window, the
; pick is an index into the tables below - the names the list shows and the
; records the flight reads, kept in step by position
cs_drplane:  dw 0, 0, 0, 0, cs_plnames, CS_NPLANES, 0, 0
             db 0, 0FFh
             dw 0, 0, 0, 0          ; the banked pixels (OS88UI_DR_SEG/_KB),
                                    ; where the open list goes (_TOP) and
                                    ; whether it is greyed (_DIS, 13.14.5)
cs_drport:   dw 0, 0, 0, 0, cs_apnames, CS_NPORTS, CS_DEFPORT, 0
             db 0, 0FFh
             dw 0, 0, 0, 0
cs_flyrect:  dw 0, 0, 0, 0
; --- the Settings page's controls (SPEC.md 88.13). Every one of them is the
;     shared drop-down or the shared check box, and the page is the first
;     user of the second ---------------------------------------------------
cs_drbld:    dw 0, 0, 0, 0, cs_i_bld,  5, CSBL_MOD, 0
             db 0, 0FFh
             dw 0, 0, 0, 0
cs_drlod:    dw 0, 0, 0, 0, cs_i_lod,  4, CSL_MOD, 0
             db 0, 0FFh
             dw 0, 0, 0, 0
cs_drsize:   dw 0, 0, 0, 0, cs_i_size, 3, CSZ_MOD, 0
             db 0, 0FFh
             dw 0, 0, 0, 0
cs_drmode:   dw 0, 0, 0, 0, cs_i_mode, 2, 0, 0
             db 0, 0FFh
             dw 0, 0, 0, 0
cs_ckterr:   dw 0, 0, 0, 0, cs_s_terr, 1
cs_ckbld:    dw 0, 0, 0, 0, cs_s_bld, 1
cs_donerect: dw 0, 0, 0, 0
cs_setlbls:  dw cs_s_lbld, cs_s_llod, cs_s_lsize, cs_s_lmode
cs_setdrops: dw cs_drbld, cs_drlod, cs_drsize, cs_drmode
CS_NSETALL   equ ($ - cs_setdrops) / 2  ; ...and MODE IS LAST, which is what
                                    ; lets cs_nsets hide it by returning one
                                    ; fewer (88.13.11)
cs_setboxes: dw cs_ckterr, cs_ckbld
CS_NFILL     equ ($ - cs_setboxes) / 2
cs_setbytes: dw cs_setbld, cs_setlod, cs_setsize, cs_modepref
cs_i_bld:    dw cs_s_bnone, cs_s_broad, cs_s_blow, cs_s_bmod, cs_s_bhigh
cs_i_lod:    dw cs_s_lnear, cs_s_lmod, cs_s_lfar, cs_s_lultra
cs_i_size:   dw cs_s_zsml, cs_s_zmod, cs_s_zful
cs_i_mode:   dw cs_s_modex, cs_s_cga
cs_i_mode160: dw cs_s_cga, cs_s_c160   ; a real CGA's two (SPEC.md 88.15.7)
cs_sethk:    dw cs_s_hk1, cs_s_hk2, cs_s_hk3, 0   ; Mode has no hotkey
cs_fillhk:   dw cs_s_hk4, cs_s_hk5
cs_s_hk1:    db '(F1)', 0
cs_s_hk2:    db '(F2)', 0
cs_s_hk3:    db '(F3)', 0
cs_s_hk4:    db '(F4)', 0
cs_s_hk5:    db '(F5)', 0
cs_s_setttl: db 'SETTINGS', 0
cs_s_lbld:   db 'Detail Level', 0
cs_s_llod:   db 'Draw Distance', 0
cs_s_lsize:  db 'Size', 0
cs_s_lmode:  db 'Mode', 0
cs_s_lfill:  db 'Fill', 0
cs_s_bnone:  db 'None', 0
cs_s_broad:  db 'Only Roads', 0
cs_s_blow:   db 'Low', 0
cs_s_bmod:   db 'Moderate', 0
cs_s_bhigh:  db 'High', 0
cs_s_lnear:  db 'Near', 0
cs_s_lmod:   db 'Moderate', 0
cs_s_lfar:   db 'Far', 0
cs_s_lultra: db 'Ultra', 0
cs_s_zsml:   db 'Small', 0
cs_s_zmod:   db 'Moderate', 0
cs_s_zful:   db 'Full', 0
cs_s_modex:  db 'Mode X, 256 col', 0
cs_s_cga:    db 'CGA, 4 col', 0
cs_s_c160:   db 'CGA, 16 col', 0
cs_s_terr:   db 'Terrain', 0
cs_s_bld:    db 'Buildings', 0
cs_s_done:   db 'Done', 0
cs_s_setts:  db 'Settings', 0
cs_planes:   dw cs_p_c172, cs_p_pitts, cs_p_fouga, cs_p_bijave, cs_p_a5
cs_plnames:  dw cs_s_c172, cs_s_pitts, cs_s_fouga, cs_s_bijave, cs_s_a5
CS_NPLANES   equ ($ - cs_plnames) / 2
; --- the LOCATIONS (SPEC.md 88.6.4), ALPHABETICALLY: the list a player reads
;     is sorted by its own names, which is what tools/csworlds.py's LOCATIONS
;     list carries. The two tables are kept in step BY POSITION - cs_apnames is
;     what the drop-down shows and cs_ports the record the flight reads - and
;     both are generated from the same list, so they cannot drift apart.
; GENERATED, by tools/csworlds.py (SPEC.md 88.10.5): cs_ports, cs_apnames,
; cs_apwld, CS_NPORTS, CS_DEFPORT, CS_NWORLDS, the overlay's two addresses and
; the shared vocabulary's own symbols. It is generated because every one of
; those depends on where a world's location record lands inside a blob this
; program does not contain - and because CS_DEFPORT was a hand-kept 5 under a
; comment warning that moving Paris down the list would silently change which
; runway a fresh instance opens on. It is derived now.
%include "cswidx.inc"

cs_i_lines:  dw cs_i1, cs_i2, cs_i3, cs_i4, cs_i5, cs_i6, cs_i7, cs_i8
             dw cs_i9, cs_i10, cs_i13, cs_i11, cs_i12, 0
cs_i1:       db 'INSTRUCTIONS', 0     ; every line under 38 cells: the
cs_i2:       db 0                     ; content is 310 wide (88.10)
cs_i3:       db 'Arrows    pitch and roll', 0
cs_i4:       db 'W and S   throttle up and down', 0
cs_i5:       db 'A and D   rudder', 0
cs_i6:       db 'B         brakes', 0
cs_i7:       db 'P         pause', 0
cs_i8:       db 'R         back to the runway', 0
cs_i9:       db 'M         engine sound on and off', 0
cs_i10:      db 'Esc or F  back to this window', 0
cs_i11:      db 'Full throttle; pull back at 55 knots.', 0
cs_i12:      db 'Click, or press a key, to return.', 0
cs_i13:      db 'F1 to F5  step a setting, in flight', 0   ; in the blank
                                                          ; separator's place:
                                                          ; the page is full
                                                          ; at thirteen lines

; -----------------------------------------------------------------------------
; cs_about - the OSAPI_ABOUT_SET handler (slot 0x01E0)
; in:  SI = our window ptr; the UI task, gfx lock HELD.  preserves all
; -----------------------------------------------------------------------------
cs_about:
    push bx
    push si
    mov byte [cs_abon], 1
    mov bx, si
    mov si, cs_ablines
    call os88ui_about               ; arms the clip itself (SPEC.md 11.3)
    pop si
    pop bx
    ret

; cs_abdismiss - take the card down if it is up. out: CF = 1 spent doing it
cs_abdismiss:
    cmp byte [cs_abon], 0
    je .none
    mov byte [cs_abon], 0
    call cs_repaint
    stc
    ret
.none:
    clc
    ret

cs_ablines:
    dw cs_ab1, cs_ab2, cs_ab3, 0
cs_ab1:      db 'Clear Skies for os8088', 0
cs_ab2:      db 0
cs_ab3:      db 'Contributed by Elendilon', 0

cs_tpl:
    dw 0, 0, CS_WINW, CS_WINH
    dw cs_ttl, cs_paint, cs_onkey, cs_onclick

%ifdef CSDIAG                   ; SPEC.md 88.14: where the frame had reached,
%macro CSSTAGE 1                ; for a machine that stopped inside it - and
    mov byte [cs_dstage], %1    ; the GUARDS checked at the same eleven points,
    call cs_diag_ck             ; so a scribble is caught in the phase that
%endmacro                       ; made it rather than at the death (88.14.1)
%else
%macro CSSTAGE 1
%endmacro
%endif

%include "csraster.inc"
%include "cs3d.inc"
%include "csworld.inc"
%include "csflight.inc"
%include "csgame.inc"
%include "cspanel.inc"
%include "csart.inc"
%include "csset.inc"        ; the settings, kept in SYSTEM\APPDATA (88.13.9)

; =============================================================================
; WHAT IS PAST THIS IMAGE (SPEC.md 88.10.4) - and it is not read from here
;
; THIS FILE IS PART 0. apps/skies/csload.asm is SKIES.O88's image: it carries
; apps/os88parts.inc, reads the two parts, writes the handoff into the head of
; the bss below, and hands its identity to this one through OSAPI_PKG_REHOME
; (SPEC.md 20.12.10). Its region is then FREED, so the reader costs this
; package nothing at all - not the 1,219 bytes of the standard's code, not the
; table, not its own header.
;
; THE HANDOFF IS THE WHOLE INTERFACE, and it is four bytes. The kernel does not
; zero a part, which is what lets a loader write into the head of a program's
; bss and the program read it back; nothing was published, stamped or
; registered to make that work.
; =============================================================================
CSH_MAGIC  equ 0                ; word: 'CS' - the loader ran
CSH_ART    equ 2                ; word: where it put the title bands
CSH_CLB    equ 4                ; word: this volume's bytes per cluster, which
                                ; is what OSAPI_FILE_READ_AT rounds to and the
                                ; one thing about the disk we cannot work out
CSH_WDIR   equ 6                ; 9 rows of (sector, packed length): the shared
                                ; vocabulary and then the eight worlds, in
                                ; tools/csworlds.py's own order
CSH_NDIR   equ 9
CSH_SIZE   equ CSH_WDIR + CSH_NDIR * 4

CS_STREAM_VOCAB equ 0           ; ...and the rows, by name
CS_STREAM_WLD0  equ 1
%include "csdiag.inc"       ; CSDIAG=1 only: the watchdog (SPEC.md 88.14)

; =============================================================================
; .bss (SPEC.md 20.5: the loader zeroes CS_BSS bytes after the image, and
; every name below is an offset from os88_image_end)
; =============================================================================
cs_hand equ os88_image_end      ; THE HANDOFF IS THE FIRST THING IN THE BSS,
                                ; which is what lets the loader write it
                                ; knowing only LD_H_IMG - the part's own header
                                ; field - and nothing about this file's layout
                                ; (SPEC.md 20.12.10.2)
%assign CS_BSS CSH_SIZE
%macro ZWORD 1
%1 equ os88_image_end + CS_BSS
%assign CS_BSS CS_BSS + 2
%endmacro
%macro ZBYTE 1
%1 equ os88_image_end + CS_BSS
%assign CS_BSS CS_BSS + 1
%endmacro
%macro ZBUF 2
%1 equ os88_image_end + CS_BSS
%assign CS_BSS CS_BSS + (%2)
%endmacro
%macro ZDWORD 1
%1 equ os88_image_end + CS_BSS
%assign CS_BSS CS_BSS + 4
%endmacro

; --- the desktop side ---------------------------------------------------------
    ZWORD cs_scrw
    ZWORD cs_dock
    ZWORD cs_win
    ZWORD cs_winox
    ZWORD cs_winoy
    ZWORD cs_cw
    ZWORD cs_ch
    ZWORD cs_caps
    ZBYTE cs_want
    ZBYTE cs_fsxm
    ZBYTE cs_vidk
    ZBYTE cs_abon
    ZBYTE cs_page                   ; the launcher's page: 0 the title, 1 the
                                    ; instructions (88.10)
    ZBYTE cs_setbld                 ; the four settings (88.13), and their
    ZBYTE cs_setsize                ; defaults are what shipped: every
    ZWORD cs_lodsc                  ; the Detail rung's range SCALE, looked up
                                    ; once a frame instead of once an object
    ZBYTE cs_setlod                 ; picture below the top of each list is
    ZBYTE cs_setfill                ; a trade the player asked for
    ZWORD cs_adtn                   ; the half-width table's rows, 0 = unbuilt
    ZBUF  cs_adtab, (CS_ADHMAX + 1) * 2
    ZWORD cs_odx                    ; the object's world offset from the EYE at
    ZWORD cs_ody                    ; its own scale, BEFORE the rotation - the
    ZWORD cs_odz                    ; axis cull's whole input (88.5.12)
    ZBYTE cs_modepref               ; the Mode menu's pick: 0 Mode X, 1 CGA
    ZBYTE cs_flydn                  ; the Fly button is pressed
    ZBYTE cs_donedn                 ; ...and the Settings page's Done
    ZWORD cs_plane                  ; the rows in use (SPEC.md 88.6)
    ZWORD cs_airport

; --- the raster ---------------------------------------------------------------
    ZBUF  cs_fsi, FSI_SIZE
    ZBYTE cs_back
    ZBYTE cs_par
    ZBYTE cs_kshift                 ; log2 of the pixels in a byte: 3 or 2
    ZBYTE cs_quit
    ZWORD cs_vx                     ; THE NINE WORDS cs_vptab's row lands on,
    ZWORD cs_vy                     ; in its order: the box...
    ZWORD cs_vw
    ZWORD cs_vh
    ZWORD cs_wx0                    ; ...the view's first x, width and height
    ZWORD cs_ww
    ZWORD cs_wh
    ZWORD cs_sclx                   ; ...and the two scales
    ZWORD cs_scly
    ZWORD cs_wx1                    ; the view's last x
    ZWORD cs_wb0                    ; ...its first byte and byte count
    ZWORD cs_wbn
    ZWORD cs_vcx                    ; the projection centre
    ZWORD cs_vcy
    ZWORD cs_tseg                   ; the target: the shadow, or VRAM
    ZWORD cs_tbase
    ZWORD cs_page0
    ZWORD cs_page1
    ZWORD cs_shseg
    ZWORD cs_artseg                 ; the title art, unpacked (88.10.2): the
                                    ; claim's segment, or 0 if it was refused -
                                    ; which is a page without the bands and not
                                    ; a launch that fails
    ZWORD cs_inktab
    ZWORD cs_hrunproc
    ZWORD cs_rowsproc               ; the polygon's row loop (88.4.6)
    ZWORD cs_rowsprocc              ; ...and the arm that CLAMPS (88.4.5.4)
    ZBYTE cs_cone                   ; the cull's cone factor this frame (88.5.1)
    ZBYTE cs_conebase               ; ...and the backend's: 0 = 0.5, 1 = 0.75
    ZBYTE cs_ownmk                  ; a segment marks its own rows: the
                                    ; panel's, whose drawing is no object's
                                    ; (88.3.2); zero for the scene
    ZWORD cs_near                   ; ...and its near plane: CS_NEAR or CS_NEARG
    ZBYTE cs_pshr                   ; the object's transform scale (88.5.6):
    ZBYTE cs_pinside                ; ...no vertex can be past a side (88.5.7)
    ZBYTE cs_noside                 ; ...set to turn the SIDE clip off wholly:
                                    ; no vertex is ever marked past a side, so
                                    ; a clamped point is drawn to and the line
                                    ; through it WANDERS, which is what the
                                    ; 1983 original did. The A/B for 88.5.7
    ZBYTE cs_pwhole                 ; ...nor behind the near plane: WHOLE, its
                                    ; box off its vertices (88.3.2)
    ZWORD cs_mkb2                   ; cs_markstep's bottom end, kept exact - a
                                    ; WORD because the store is `mov [x], bp`
                                    ; and a byte one would write the neighbour
    ZBUF  cs_mkslo, CS_MKT_N        ; THE STEPPED MARK'S SLOP, WIDENED AND
    ZBUF  cs_mkshi, CS_MKT_N        ; CLAMPED IN ONE READ (SPEC.md 88.3.2.3.6):
                                    ; cs_mkslo[c] = max(wb0, c - CS_MKD_SLOP)
                                    ; and cs_mkshi[c] = min(wb0+wbn-1,
                                    ; c + CS_MKD_SLOP), built once a bracket by
                                    ; cs_mktabs. A byte column is never above
                                    ; 79 on ANY backend - every row is 80 bytes
                                    ; there - so 96 entries is the bound plus
                                    ; sixteen, and the overrun is benign
                                    ; besides: both tables hold CLAMPED
                                    ; columns, so reading one for the other
                                    ; still names a byte of the view
    ZBYTE cs_slnoshort              ; set to put a SHORT sliced run back on the
                                    ; general row body (88.4.6.2's A/B)
    ZBYTE cs_mknostep               ; set to put a thin diagonal's mark back on
                                    ; its BOX, which is what shipped before
                                    ; SPEC.md 88.3.2.2 - the A/B, poked
    ZBYTE cs_pinview                ; ...and that box inside the view
    ZWORD cs_obx0                   ; a whole object's projected x range
    ZWORD cs_obx1
    ZWORD cs_projp                  ; 0, 2 or 4 (whole, quarter, sixteenth
    ZWORD cs_scx                    ; metres), its projection, and the
    ZWORD cs_scy                    ; origin from the eye's 16.8 position
    ZWORD cs_scz
    ZWORD cs_rwu                    ; cs_rwpt's u
    ZWORD cs_rwdu                   ; a runway stripe, on, in Q15 of the
                                    ; centreline (88.6.2); the pitch is twice
    ZBYTE cs_rwrev                  ; the aeroplane is pointed at the NEAR
                                    ; threshold, so the centreline is walked
                                    ; in facing space (88.6.2.4)
    ZWORD cs_rwfar                  ; ...and where the FAR threshold's run of
                                    ; them starts, 32766 - (2 RW_NDASH - 1) du
                                    ; (88.6.2.3), so the last stripe ends ON it
    ZBYTE cs_pgate                  ; the panel's rate gate (88.9.1)...
    ZWORD cs_plast                  ; ...and the tick the instruments last read
    ZWORD cs_pfan                   ; the fan's triangles left (88.5.9)
    ZBYTE cs_bshr                  ; cs_boxlod's saved cs_pshr (88.5.4.3)
    ZWORD cs_bw                     ; cs_boxlod's half-width, and its
    ZWORD cs_bx0                    ; projected centre x, top row and base
    ZWORD cs_by0                    ; row - and the TOP's x with them, which
    ZWORD cs_by1                    ; is where the bank shows (88.5.4.6)
    ZWORD cs_bx1
    ZWORD cs_bwp                    ; ...the half-width in PIXELS, and the
    ZWORD cs_brx                    ; screen-space half-width vector it turns
    ZWORD cs_bry                    ; into: w (cos r, -sin r)
    ZWORD cs_glyphproc
    ZWORD cs_lsh                    ; the walk trio: shallow, steep, vertical
    ZWORD cs_lst
    ZWORD cs_lvt
    ZBYTE cs_ink
    ZBYTE cs_ink8
    ZBYTE cs_pbg8                   ; the panel's ground, as a glyph stores it
    ZBUF  cs_pat, 4                 ; the ink's four pattern bytes
    ZWORD cs_spcur                  ; this frame's span set and the last
    ZWORD cs_spprv                  ; frame's (SPEC.md 85.3.1, 88.3)
    ZBUF  cs_spanp, 4
    ZBUF  cs_spguard0, 8
    ZBUF  cs_span0, CS_MAXROW * 2
    ZBUF  cs_spguard1, 8
    ZBUF  cs_span1, CS_MAXROW * 2
    ZBUF  cs_spguard2, 8
    ZBYTE cs_mklo                   ; the bytes cs_markrows marks
    ZBYTE cs_mkhi
    ZWORD cs_pbx0                   ; a polygon's clamped x range
    ZWORD cs_pbx1
    ZWORD cs_oby0                   ; the OBJECT's mark, accumulated by
    ZWORD cs_oby1                   ; cs_markacc over its polygons and
    ZBYTE cs_oblo                   ; segments and marked once by
    ZBYTE cs_obhi                   ; cs_drawobj (SPEC.md 88.3.2)
    ZWORD cs_hzkt                   ; cs_hzrows' pattern table
    ZBUF  cs_devoff, CS_MAXROW * 2
    ZBUF  cs_rowoff, CS_MAXROW * 2
    ZBUF  cs_xl, CS_MAXROW * 2      ; a polygon's left and right bound per row
    ZBUF  cs_xr, CS_MAXROW * 2
    ZBUF  cs_rowkind, CS_MAXROW     ; a view row's kind at the last sky and
                                    ; ground pass: 0 all sky, 1 all ground, 3
                                    ; split or unknown. Whether anything drew
                                    ; on it since is the previous span set's
                                    ; entry, not a bit here (88.3.1)
    ZWORD cs_fullspan               ; the span pair of a touched view row
    ZBUF  cs_hzpat4, 8              ; the fused band's ink PAIR per row phase,
                                    ; DL left DH right, built once a frame
    ZWORD cs_hzend                  ; ...and where its walk of cs_xl stops
    ZWORD cs_hzmm                   ; the crossing byte's pixel-mask index and
    ZWORD cs_hzmt                   ; its table, both the ADAPTER's (88.3.1.2)
    ZWORD cs_hzlo                   ; the band's span pass (88.3.1.1): the
    ZBYTE cs_hzhi                   ; view's first and last BYTE, and the kind
    ZWORD cs_hzsides                ; LAST frame's cs_hzl/cs_hzr as a pair: a
                                    ; band row's narrow span rests on the
                                    ; row's other bytes being what they were,
                                    ; and a side swap breaks exactly that
                                    ; (SPEC.md 88.3.1.1.3)
    ZBYTE cs_hzsplit                ; a row must have had to get a band rather
                                    ; than the whole view - 3, or 0xFF where
                                    ; nothing may be "as it was"
    ZBYTE cs_hzfull                 ; ...set to put every split row back on
                                    ; the whole-view span, which is the A/B
    ZWORD cs_slx                    ; the slice's (85.3.6): its first x, whole
    ZWORD cs_slq                    ; step, error, runs to go and last run
    ZWORD cs_slerr
    ZWORD cs_slcnt
    ZWORD cs_slfin
    ZWORD cs_cx1                    ; the clipper's four, CONTIGUOUS
    ZWORD cs_cy1
    ZWORD cs_cx2
    ZWORD cs_cy2
    ZBYTE cs_ctry
    ZBYTE cs_xdir
    ZWORD cs_e2
    ZWORD cs_ystep
    ZWORD cs_eq                     ; the edge tracer's step, remainder, dy,
    ZWORD cs_er                     ; and the lower end's row
    ZWORD cs_edy
    ZWORD cs_ey2
    ZWORD cs_py0                    ; a polygon's row range...
    ZWORD cs_py1
    ZWORD cs_pvp                    ; ...its vertex list and edges to go
    ZWORD cs_pei
    ZBYTE cs_pwind                  ; ...and its winding (88.4.2)
    ZWORD cs_pnoclip                ; ...and whether ANY row of it can reach a
                                    ; view edge (88.4.5.1): 0 when none can,
                                    ; which the row loop takes in BP
    ZBYTE cs_eside                  ; the chain an edge is on: 0 both, 1, 2
    ZWORD cs_lrunproc               ; the run a LINE's slice lays
    ZBUF  cs_pv, CS_MAXPV * 4       ; a projected face: (x, y) pairs
    ZBYTE cs_fcut                   ; ...the near plane CUT it, so cs_pv is
                                    ; not the model's vertices (88.13.3)
    ZWORD cs_wn                     ; ...cs_wire's vertex count...
    ZWORD cs_wj                     ; ...and the edge's far end
    ZBUF  cs_eseen, CS_ESEEN        ; ...the edges drawn already, this object
    ZWORD cs_pn
%ifdef CSHZPROBE                    ; ...its OWN define: CSPROBE's bss is
    ZBUF  cs_hzpb, CS_MAXROW        ; already at APP_MAX_SIZE, and this
    ZWORD cs_dbg_hzrow              ; question needs none of its arms
    ZWORD cs_dbg_hzby               ; ...bytes the whole-row refill lays
    ZWORD cs_dbg_hzinc              ; ...bytes an INCREMENTAL one would
    ZWORD cs_dbg_hzsame             ; ...rows whose crossing did not move
    ZWORD cs_dbg_hzmax              ; ...the widest single row's change
%endif
%ifdef CSPROBE
    ZWORD cs_dbg_etr                ; PROBE ONLY: edges cs_poly actually traced
    ZWORD cs_dbg_edup               ; ...of which a face of the SAME object
    ZWORD cs_dbg_ecut               ; ...had already traced; and the cut faces
    ZWORD cs_dbg_erow               ; ...and the rows those traces covered
    ZWORD cs_dbg_edrow              ; ...of which a duplicate's
    ZWORD cs_dbg_fwalk              ; ...faces cs_faces walked
    ZWORD cs_dbg_fcull              ; ...of which the winding threw away
    ZWORD cs_dbg_fpoly              ; ...and which reached cs_poly
    ZWORD cs_dbg_fout               ; ...of those, off the view
    ZWORD cs_dbg_fbox               ; ...one or two rows, so no trace at all
    ZWORD cs_dbg_ftr                ; ...objects whose faces reached cs_poly
    ZBYTE cs_dbg_fobj               ; ...(this one has)
    ZBYTE cs_dbl                    ; the A/B: trace every edge TWICE
    ZBYTE cs_nomark                 ; ...run the dedup TEST or not
    ZBYTE cs_cpy                    ; ...and price the COPY that would replace
CS_DBGSCR equ 112               ; ...and the copy A/B's scratch is 112 rows,
                                ; the HERCULES view - which is the machine
                                ; every one of these arms is read on. The copy
                                ; clamps to it: the probe build is at
                                ; APP_MAX_SIZE and a buffer sized for a view
                                ; nobody measures on costs the arms that are
    ZBUF  cs_dbg_scr, CS_DBGSCR * 2
    ZWORD cs_dbg_y0                 ; ...the trace's first row, clipped
    ZBYTE cs_dupface                ; ...and the A/B: repeat a face's GATHER
    ZBUF  cs_dbg_pv, CS_MAXPV * 4   ; and its winding cross, into scratch
    ZBYTE cs_dupgath                ; ...the GATHER alone
    ZBYTE cs_duparea                ; ...the winding cross alone
    ZBYTE cs_dblplot                ; ...and a 1bpp line's PLOT, done twice
    ZWORD cs_dbg_wsh                ; segments walked per pixel (shallow)
    ZWORD cs_dbg_wsl                ; ...taking the run slice instead
    ZWORD cs_dbg_wst                ; ...steep
    ZWORD cs_dbg_wvt                ; ...vertical
    ZWORD cs_dbg_wshpx              ; pixels the per-pixel shallow arm plots
    ZWORD cs_dbg_wshby              ; ...and the DISTINCT BYTES they land in:
    ZWORD cs_dbg_wstpx              ; the difference is what an accumulator
    ZWORD cs_dbg_wvtpx              ; could merge, and steep/vertical cannot
    ZWORD cs_dbg_wslrow             ; the SLICED segments' runs (= rows) and
    ZWORD cs_dbg_wslpx              ; pixels: a row is the unit that is paid
    ZWORD cs_dbg_prow               ; ...and the FILL's rows, pixels and the
    ZWORD cs_dbg_ppx                ; BYTES they span - the same question
    ZWORD cs_dbg_pby                ; asked of the bigger stage
    ZWORD cs_dbg_pby2               ; rows spanning <= 2 bytes...
    ZWORD cs_dbg_pby4               ; ...<= 4...
    ZWORD cs_dbg_pby8               ; ...and <= 8, because a MEAN hides a
                                    ; bimodal distribution and the two want
                                    ; different answers
    ZWORD cs_dbg_mfr                ; frames, and the ones whose MATRIX was
    ZWORD cs_dbg_mstab              ; unchanged (88.5.13.2's whole question)
    ZBUF  cs_dbg_mprev, 9 * 2       ; ...last frame's copy of it
    ZWORD cs_dbg_fvn                ; a FLAT model's vertices, and the ones
    ZWORD cs_dbg_fvx                ; whose x - or z - is the PREVIOUS
    ZWORD cs_dbg_fvz                ; vertex's, so three imuls are already to
    ZWORD cs_dbg_fvpx               ; hand. The running pair, and a flag for
    ZWORD cs_dbg_fvpz               ; the first vertex of a model, which has
    ZBYTE cs_dbg_fvfirst            ; no predecessor to match
    ZWORD cs_dbg_pvobj              ; objects reaching cs_projall, and the
    ZWORD cs_dbg_pvobje             ; ones whose model draws EDGES...
    ZWORD cs_dbg_pvedge             ; ...their vertices, which cannot be
    ZWORD cs_dbg_pvfree             ; skipped, against the ones that could
    ZWORD cs_dbg_pvnv               ; ...this object's vertex count, and
    ZBYTE cs_dbg_pvne               ; whether its model draws edges
    ZBUF  cs_dbg_used, CS_MAXV      ; one byte a vertex: did any face that
                                    ; SURVIVED cs_axcull want it?
    ZWORD cs_dbg_vcand              ; vertices in edge-free models, and the
    ZWORD cs_dbg_vunused            ; ones no surviving face wanted
    ZBYTE cs_axmask                 ; BISECT: which axis bits may cull
    ZBYTE cs_axoff                  ; AUDIT: cs_axcull computes and does NOT
    ZBYTE cs_dbg_ax                 ; act, so the winding decides every face
    ZWORD cs_dbg_axcull             ; ...and the two verdicts are compared:
    ZWORD cs_dbg_axagree            ; both cull
    ZWORD cs_dbg_axmiss             ; the winding culls, the axis test does not
    ZWORD cs_dbg_axbad              ; THE AXIS TEST CULLS A FACE THE WINDING
    ZWORD cs_dbg_axmdl              ; DRAWS - the model it last happened on
    ZWORD cs_dbg_axflg              ; ...and that face's flags
%endif
    ZWORD cs_rx1                    ; cs_prect's
    ZWORD cs_rx2
    ZWORD cs_ry2
    ZBUF  cs_hzsa, 8                ; the horizon's two ends, and whether it
    ZBYTE cs_hzhave                 ; has any: the line a bare ground leaves
    ZWORD cs_hzy0                   ; the horizon (88.4.1): the band's rows...
    ZWORD cs_hzy1
    ZWORD cs_hnx                    ; ...the quartered up vector...
    ZWORD cs_hny
    ZWORD cs_hnz
    ZDWORD cs_hza
    ZWORD cs_hzx0                   ; ...the intercept and the two ends...
    ZWORD cs_hzxa
    ZWORD cs_hzya
    ZWORD cs_hzyb
    ZBYTE cs_hzl                    ; ...the four inks...
    ZBYTE cs_hzr
    ZBYTE cs_hzab
    ZBYTE cs_hzbl
    ZWORD cs_hzlt                   ; ...and their pattern tables
    ZWORD cs_hzrt
    ZWORD cs_hzat
    ZWORD cs_hzbt
    ZWORD cs_hzpat
    ZBUF  cs_gbuf, 8
    ZWORD cs_gtab
    ZWORD cs_gseg
    ZBYTE cs_gfirst
    ZBYTE cs_glast
    ZWORD cs_gcol
    ZWORD cs_gcol2
    ZWORD cs_nibp

; --- the geometry -------------------------------------------------------------
    ZBUF  cs_m, 9 * 2               ; the camera matrix, rows right/up/forward
    ZWORD cs_sinh                   ; the six trig values it is built from
    ZWORD cs_cosh
    ZWORD cs_sinp
    ZWORD cs_cosp
    ZWORD cs_sinr
    ZWORD cs_cosr
    ZWORD cs_fsinh                  ; ...and the GROUND TRACK'S FACING, which
    ZWORD cs_fcosh                  ; is the heading turned round past the
                                    ; vertical (88.7.8.2)
    ZWORD cs_t1
    ZWORD cs_t2
    ZWORD cs_nx                     ; the world's up vector in camera space:
    ZWORD cs_ny                     ; the matrix's second column (88.4.1)
    ZWORD cs_nz
    ZWORD cs_ex                     ; the eye, in whole metres, and the high
    ZWORD cs_exh                    ; words of x and z for the 32-bit cull
    ZWORD cs_ey
    ZWORD cs_ez
    ZWORD cs_ezh
    ZWORD cs_ddx                    ; an object's offset from the eye
    ZWORD cs_ddy
    ZWORD cs_ddm                    ; ...and |dx| + |dz|, the range's measure
    ZWORD cs_ddz
    ZWORD cs_ocx                    ; the object's origin in camera space
    ZWORD cs_ocy
    ZWORD cs_ocz
    ZWORD cs_mdl
    ZWORD cs_obj
    ZBYTE cs_pass
    ; --- THE ROW LOOP'S TWO END TABLES (88.4.5.5), 640 entries each: a
    ;     view's x cannot leave [0, 639] because the shadow row is 80 bytes
    ;     and cs_vptab's Hercules box is 640 wide. 2,560 bytes of the CLAIM
    ;     and near enough nothing of the disk - the bss ships inside the
    ;     part as a run of zeros and LZ4 is best at exactly that ---------
    ZBUF  cs_lend, CS_ENDN * 2      ; (mask << 8) | (x >> 3), the left end
    ZBUF  cs_rend, CS_ENDN * 2      ; ...and the right
    ZBUF  cs_col0, 6                ; the three scaled columns (88.5)
    ZBUF  cs_col1, 6
    ZBUF  cs_col2, 6
    ZWORD cs_lwx                    ; a level's half-widths and centre
    ZWORD cs_lwz
    ZWORD cs_lcx
    ZWORD cs_lcy
    ZWORD cs_lcz
    ZWORD cs_nv
    ZBUF  cs_cxv, CS_MAXV * 2
    ZBUF  cs_cyv, CS_MAXV * 2
    ZBUF  cs_czv, CS_MAXV * 2
    ZBUF  cs_sxv, CS_MAXV * 2
    ZBUF  cs_syv, CS_MAXV * 2
    ZBUF  cs_fv,  CS_MAXV * 2
    ZWORD cs_fn                     ; the face in hand: ends, ink, flags, list
    ZBYTE cs_fink
    ZBYTE cs_fflags
    ZWORD cs_fidx
    ZBUF  cs_clipx, CS_MAXPV * 6    ; a face clipped to the near plane, x/y/z
    ZBUF  cs_clipy, CS_MAXPV * 6    ; ...and the other buffer the side passes
    ZWORD cs_csrc                   ; alternate between (88.5.7): source,
    ZWORD cs_cdst                   ; destination, the plane, the output
    ZWORD cs_cpln                   ; cursor and count, A's distance, t
    ZWORD cs_cout
    ZWORD cs_cn2
    ZWORD cs_cda
    ZWORD cs_ct
    ZWORD cs_cdistp                 ; the plane's distance routine
    ZBUF  cs_cdists, CS_MAXPV * 2   ; a pass's distances, one a point
    ZBUF  cs_xpt, 6                 ; a crossing point, x/y/z
    ZBUF  cs_ea, 6                  ; an edge's two ends in camera space
    ZBUF  cs_eb, 6
    ZWORD cs_ex1                    ; ...and the first projected
    ZWORD cs_ey1
    ZWORD cs_ncl
    ZWORD cs_na
    ZWORD cs_nb
    ZWORD cs_nnum
    ZWORD cs_nden
    ZBUF  cs_kx, 2048 * 2           ; the projection tables (85.5.3, 88.5)
    ZBUF  cs_ky, 2048 * 2
    ZBUF  cs_vis, CS_NVIS * CS_VISZ ; the frame's objects: ptr, reach, along
    ZBUF  cs_vkey, CS_NVIS * 4      ; ...sorted far to near: along, record
    ZWORD cs_nvisn
    ZBUF  cs_occ, CS_NVIS           ; ...and whether cs_occlude hid each one
    ZWORD cs_occn                   ; how many it hid, for the diagnostics
    ZBYTE cs_occk                   ; occluders kept this frame
    ZWORD cs_occi                   ; the slot cs_occlude is looking at
    ZWORD cs_occwa                  ; the occluder's half-width at this level
    ZWORD cs_occo                   ; ...and the object cs_occbox is reading
    ZWORD cs_asinh                  ; |sin h| and |cos h|, once a frame: the
    ZWORD cs_acosh                  ; extent of an axis-aligned box ACROSS the
                                    ; heading is wx |cos h| + wz |sin h|
    ZBUF  cs_p0, 4                  ; a 32-bit product, held for the compare
    ZWORD cs_ob_l                   ; THE CANDIDATE, six words in the order an
    ZWORD cs_ob_c                   ; occluder record keeps them (cs_occadd
    ZWORD cs_ob_y0                  ; copies them straight across): along,
    ZWORD cs_ob_y1                  ; across, base y, top y, base half-extent
    ZWORD cs_ob_w0                  ; and top half-extent
    ZWORD cs_ob_w1
    ZWORD cs_ob_ang                 ; ...and (half-extent + height) / along,
                                    ; which is what an occluder is RANKED by:
                                    ; keeping the three NEAREST fills the
                                    ; slots with a hangar and two valley
                                    ; walls and never reaches the peaks that
                                    ; actually hide anything
    ZBUF  cs_occa, CS_OCCN * CS_OCCZ ; ...and the occluders kept
    ZBUF  cs_rwverts, 6 * 4         ; the runway, built from the airport
    ZBUF  cs_rwmodel, CSM_SIZE
    ZBUF  cs_rwobj, CSO_SIZE
    ZWORD cs_rwax                   ; its along and across vectors
    ; --- WHAT A CRASH NEEDS THE AEROPLANE TO BE BELOW, one word an object
    ;     (88.7.13): cs_ctopbuild fills it when a location is picked, and
    ;     -32768 stands for an object that cannot be hit at all, so the
    ;     walk's first compare rejects a non-collider too. A world blob is
    ;     CS_WLD_MAX bytes and an object twenty of them, so this cannot be
    ;     short ---------------------------------------------------------
    ZBUF  cs_ctop, (CS_WLD_MAX / CSO_SIZE) * 2
    ; --- the eye's position PRE-SHIFTED for each of the three scales
    ;     (88.5.6.3): three groups of {x', y', z'}, nine words, rebuilt once
    ;     a frame by cs_eyeshift and read by cs_scale as a plain word -----
    ZBUF  cs_psh, 18
    ZWORD cs_pshp                   ; ...the group for THIS object's scale
    ZWORD cs_rwaz
    ZWORD cs_rwcx
    ZWORD cs_rwcz

; --- the aeroplane (SPEC.md 88.7) ---------------------------------------------
    ZDWORD cs_px                    ; 16.8 metres
    ZDWORD cs_py
    ZDWORD cs_pz
    ZWORD cs_hdg                    ; 65536 to the turn
    ZWORD cs_pitch
    ZWORD cs_roll
    ZWORD cs_thracc                 ; the engine's thrust in 8.8, which is
                                    ; what the spool integrates (88.7.5)
    ZBYTE cs_onwater                ; the wheels are in the water (88.7.7)
    ZWORD cs_lsn                    ; a strip's sine and cosine, while its
    ZWORD cs_lcs                    ; local coordinates are being taken
    ZWORD cs_rrate                  ; a lagging model's roll and pitch RATES
    ZWORD cs_prate                  ; (88.7.5); zero for a direct-drive one
    ZWORD cs_spd                    ; 16.7 m/s (SPEC.md 88.7.4)
    ZWORD cs_hs                     ; ...its horizontal component...
    ZWORD cs_vs                     ; ...and its vertical
    ZWORD cs_ht                     ; the ground moved this tick
    ZWORD cs_thr                    ; 0..100
    ZWORD cs_qv                     ; v^2/4 in whole m^2/s^2, the quantity the
                                    ; drag is made of - kept because the
                                    ; INDUCED term divides by it and the
                                    ; parasitic one has already multiplied it
                                    ; away by the time the air path is reached
    ZWORD cs_thrust                 ; CSP_THRUST x thr / 100, kept current
    ZBYTE cs_state
    ZBYTE cs_stall
    ZBYTE cs_inited                 ; the aeroplane has been put on the runway
    ZBYTE cs_kpitch                 ; the held keys, latched by cs_input and
    ZBYTE cs_hzhold                 ; bit 0 roll, bit 1 pitch: this axis
                                    ; ARRIVED on the horizon and stands still
                                    ; for the rest of the frame. cs_ease's
                                    ; since 88.7.3.1, so it is EVERY model's
                                    ; and not only the lagging one (88.7.5.2)
    ZBYTE cs_taproll                ; a stick press int 16h saw and the level
    ZBYTE cs_tappitch               ; read did not (88.7.5.2), worth one tick
    ZBYTE cs_wasroll                ; ...and the LEVEL read of the tick before,
    ZBYTE cs_waspitch               ; which is what tells a tap from the tail
                                    ; of a hold's typematic repeats
    ZBYTE cs_kroll                  ; spent one step at a time by cs_step
    ZBYTE cs_kyaw
    ZBYTE cs_kthr
    ZBYTE cs_kbrake
    ZBYTE cs_pause
    ZWORD cs_crasht
    ZWORD cs_crashwhy               ; a string: what was hit
    ZWORD cs_crashes                ; read by tests/skies.py
    ZWORD cs_landings
    ZBYTE cs_msg
    ZBYTE cs_sound
    ZWORD cs_tone                   ; the engine tone being played, or 0
    ZWORD cs_sndtk                  ; the tick cs_sound_step last serviced: the
                                    ; note moves once per WALL-CLOCK tick and
                                    ; not once per call (SPEC.md 88.8.2.1.2)
    ZWORD cs_eng                    ; ...and the ENGINE's own note, which the
                                    ; stall beep and the crash blast stand in
                                    ; front of without disturbing (88.8.2.1).
                                    ; It is what CSS_LAG slews
    ZWORD cs_last                   ; the tick the last frame was stepped at
    ZWORD cs_frames                 ; frames rendered; the only instrument
    ZWORD cs_rwsin                  ; the runway heading's sine and cosine
    ZWORD cs_rwcos
    ZWORD cs_along                  ; the aeroplane in runway coordinates
    ZWORD cs_across
    ZBYTE cs_stallt                 ; the stall beep's cadence

; --- the panel (SPEC.md 88.9) -------------------------------------------------
    ZBUF  cs_pshow, 16 * 2          ; what the instruments LAST READ (88.9.4):
                                    ; one set, shared by both pages, so the
                                    ; two of them cannot hold readings taken
                                    ; seconds apart
    ZBUF  cs_pkeys, 2 * 16 * 2      ; sixteen items' keys, one set per page
    ZWORD cs_pcur                   ; the item in hand, and its key
    ZWORD cs_pkeyv
    ZBYTE cs_ppage                  ; the page whose keys apply
    ZBUF  cs_numbuf, 16
    ZBUF  cs_msgbuf, 48
    ZWORD cs_hsx                    ; the panel's horizontal scale, in eighths
    ZWORD cs_pany                   ; the panel's first row
    ZWORD cs_dcx                    ; a decoration in hand (88.9.3): its
    ZWORD cs_dcy                    ; centre, its two radii, its argument
    ZWORD cs_drx                    ; and one endpoint of its needle
    ZWORD cs_dry
    ZWORD cs_darg
    ZWORD cs_dpx
    ZWORD cs_dsi                    ; ...and the row itself, across cs_setink
    ZWORD cs_pasp                   ; rows per logical x unit, Q8 (88.9.2)
    ZWORD cs_pbarx                  ; the throttle bar's left end, top row
    ZWORD cs_pbary                  ; and width, off the cockpit
    ZWORD cs_pbarw
CS_HULLTHR equ 25               ; a throttle under this is not driving, so the
                                ; HULL brakes (88.7.7.2)
CS_HZR equ 1                    ; the horizon's hold, a bit an axis (88.7.3.1)
CS_HZP equ 2
CS_MSGAGE equ 145               ; ticks an ANNOUNCEMENT stands: 8 seconds at
                                ; 18.2 Hz (SPEC.md 88.7.6.2)
CS_SWOOPT equ 8                 ; ticks the air's swoop lasts (88.7.6.3)...
CS_SWOOPD equ 75                ; ...hertz a tick of it, and the two ends it
CS_SWOOPLO equ 300              ; runs between: UP from the bottom in lift,
CS_SWOOPHI equ 900              ; DOWN from the top in sink

    ZBYTE cs_msgt                   ; ...and what is left of them
    ZBYTE cs_hzbit                  ; which axis cs_ease is on (88.7.3.1)
    ZBYTE cs_tleft                  ; the ticks left in this frame,
                                    ; this one counted, and how many
    ZBYTE cs_tframe                 ; it has altogether - cs_ease aims
                                    ; its landing at the last of them
                                    ; (SPEC.md 88.7.3.2)
    ZWORD cs_easm                   ; ...and the distance it is easing,
    ZWORD cs_eass                   ; against this axis's full rate
    ZWORD cs_hzox                   ; ...and the ramp OUT (88.7.3.3):
    ZBUF  cs_hzo, 8                 ; a cap and its increment an axis,
                                    ; roll first, indexed by cs_hzox
    ZBUF  cs_promptb, 44            ; THE TAKE-OFF PROMPT (88.7.9), composed
                                    ; from the aeroplane's own record: the
                                    ; longest is 29 + 3 digits + ' KNOTS'
    ZWORD cs_gsuf                   ; the units the sentence being composed
                                    ; ends in: ' KNOTS' or ' KT' (88.15.6.1)
    ZBUF  cs_promptc, 16            ; ...and the ONE-LINE strip's own, which
                                    ; is the same sentence in fourteen cells
                                    ; (88.15.6.1): 'ROTATE 55 KT'
    ZWORD cs_airv                   ; the air's rate here (88.7.6.3), the sign
    ZBYTE cs_airs                   ; of the last one, and the swoop it starts
    ZBYTE cs_swdir
    ZBYTE cs_swt
    ZWORD cs_wname                  ; cs_inwater (88.7.7.1): the touched
    ZWORD cs_wobj                   ; water's name, and the walk's own state -
    ZWORD cs_wnob                   ; the object, how many are left, the face
    ZWORD cs_wnf                    ; and how many of those, the vertex table,
    ZWORD cs_wfp                    ; the point in the object's frame, the
    ZWORD cs_wvt                    ; edge's two ends and the one just read,
    ZWORD cs_wpx                    ; the face's index list and its length,
    ZWORD cs_wpz                    ; where the walk is, and the edge's dz
    ZWORD cs_wax
    ZWORD cs_waz
    ZWORD cs_wvx
    ZWORD cs_wvz
    ZWORD cs_wface
    ZWORD cs_wnv                    ; (cs_wn is cs_wire's, 88.5)
    ZWORD cs_wi
    ZWORD cs_wdz
    ZBYTE cs_wodd                   ; ...and the crossing count's parity
    ZWORD cs_dn                     ; a switch RAIL's counter, its remaining
    ZWORD cs_dmask                  ; mask and the layout x of the switch it
    ZWORD cs_dxl                    ; is drawing (88.9.3.1)
    ZWORD cs_adrr                   ; the ADI chord (88.9.6.2): R^2, a, the
    ZWORD cs_ada                    ; root of a, half the chord scaled, and
    ZWORD cs_adsa                   ; the foot of the perpendicular
    ZWORD cs_adstep
    ZWORD cs_adus
    ZBUF  cs_svclip, 4              ; the view's x clip while the panel draws
    ZWORD cs_viewh                  ; ...and its HEIGHT, which the panel does
                                    ; not borrow: cs_pclip widens cs_wh to the
                                    ; whole box, so a reader that samples
                                    ; cs_wh mid-panel is told the box's height
                                    ; (SPEC.md 88.9.2.3)
%ifdef CSDIAG
    ZBUF  cs_dold, 4                ; the watchdog (SPEC.md 88.14): the int 08h
    ZBUF  cs_dring, CSD_SLOTS * 2   ; vector it chains to, the interrupted IPs
    ZWORD cs_dhead                  ; it rings, the tick counter that says
    ZWORD cs_dtick                  ; whether IRQ0 is alive at all, the tick
    ZWORD cs_dframe                 ; the last frame FINISHED on, and where in
    ZBYTE cs_dstage                 ; a frame the machine had got to
    ZBYTE cs_dbroke                 ; ...and the LATCH (88.14.1): a guard has
    ZBYTE cs_dbwhich                ; gone, which one, at which stage and on
    ZBYTE cs_dbstage                ; which tick - written once and never
    ZWORD cs_dcseg                  ; the interrupted CS, so an IP is placed
                                    ; in a segment rather than assumed to be
                                    ; ours
    ZBUF  cs_dcsr, CSD_SLOTS * 2    ; ...one PER SLOT (SPEC.md 88.14.4). The
                                    ; three IPs are three different ticks, so
                                    ; one CS places the NEWEST and leaves the
                                    ; other two unplaceable - and ordinarily
                                    ; they are not all in one segment, the
                                    ; flight's own int 16h poll (SPEC.md 53.1)
                                    ; putting a tick in the ROM
    ZWORD cs_dbtick                 ; again, so the photograph is of the
    ZBUF  cs_dcan, CSD_CANB         ; MOMENT and not of the wreckage
    ZBUF  cs_doff, CSD_BLKS * CSD_ROWS * 2  ; ...and the device offset of every
                                    ; row the strip is painted on, worked out
                                    ; ONCE at cs_diag_on (SPEC.md 88.14.3) so
                                    ; the ISR does no arithmetic - and worked
                                    ; out ABOVE THE VIEW where the backend
                                    ; leaves room, so a frame's blit cannot
                                    ; overwrite the reading
%endif
    ZWORD cs_adcx                   ; the attitude indicator: centre, the
    ZWORD cs_adcy                   ; bezel's radii, the window's half sizes
    ZWORD cs_adrx
    ZWORD cs_adry
    ZWORD cs_adhw
    ZWORD cs_adhh
    ZWORD cs_adroll                 ; ...the FOLDED roll it is drawn from
                                    ; (88.9.2.6), and
    ZWORD cs_adoff                  ; ...its horizon: the pitch offset, the
    ZWORD cs_addy                   ; slope's rise over the half-width, and
    ZWORD cs_adx1                   ; the two ends
    ZWORD cs_ady1
    ZWORD cs_adx2
    ZWORD cs_ady2
    ZWORD cs_elcx                   ; cs_pdisc/cs_pring: the centre, the radii,
    ZWORD cs_elcy                   ; the row being drawn and, for the outline,
    ZWORD cs_elrx                   ; the row above's half width and the run
    ZWORD cs_elry                   ; this one lights either side
    ZWORD cs_eldy
    ZWORD cs_elprev
    ZWORD cs_ello
    ZWORD cs_elhi
    ZWORD cs_elrow
    ZWORD cs_msgink                ; the message's ink and row while
    ZWORD cs_msgy                  ; its strip is erased (88.9.9)
    ZBUF  cs_toastbuf, 40          ; A SETTING'S NAME AND VALUE (88.13.8), and
    ZBYTE cs_toastt                ; the ticks it has left, and what the strip
    ZBYTE cs_toastwas              ; was saying before it
    ZBYTE cs_toastn                ; ...and a count, so the panel's key tells
                                   ; one toast from the next (88.13.8)
    ZBUF  cs_setbuf, CS_SETFSZ     ; the settings file, as it sits on the disk
    ZBYTE cs_setread               ; ...read once (88.13.9)
    ZWORD cs_sdclus                ; where we were standing before it
    ZBYTE cs_sddrv
    ZBUF  cs_sdfind, OSAPI_FIND_SZ
    ZWORD cs_pwl                   ; a window's edges while it is
    ZWORD cs_pwr                   ; being resolved (88.9.5)
    ZBYTE cs_pfirst                 ; bit n: page n has never had its ground
    ZBYTE cs_gcellb                 ; bytes an 8-pixel glyph cell covers
    ZWORD cs_panrows                ; rows the panel wants under the view: a
                                    ; cockpit's 88, or the strip's 12 (88.15.5)
    ZWORD cs_plblp                  ; the fixed labels this panel letters...
    ZWORD cs_plabw                  ; ...how many CELLS along its number goes
    ZWORD cs_msgtabp                ; ...and the message strings it can fit
    ZWORD cs_msgx0                  ; ...and where its strip starts (88.15.6)

; --- the world overlay's own state (SPEC.md 88.10.5) -------------------------
    ZBYTE cs_wldnow                 ; which world is IN the overlay, 0xFF = the
                                    ; vocabulary has not been read either
    ZBYTE cs_apnow                  ; ...and which LOCATION row is picked
    ZWORD cs_wgpar                  ; cs_wldget's: where the expansion goes...
    ZWORD cs_wgout                  ; ...and how many bytes it is
    ZWORD cs_wgsec                  ; the stream's sector and packed length,
    ZWORD cs_wglen                  ; out of the loader's directory
    ZWORD cs_wgslk                  ; how far past a cluster it begins
    ZWORD cs_wgoff                  ; ...and the cluster-aligned read offset,
    ZWORD cs_wgoff2                 ; 32 bits (ADJACENT: cs_wgoff+2 is read)
    ZWORD cs_wgcap                  ; the buffer's size in bytes
    ZWORD cs_wgseg                  ; ...and the claim itself
    ZBUF  cs_lname, 13              ; OUR OWN FILE, banked at entry: SI points
                                    ; into the KERNEL's segment at a buffer the
                                    ; loader reuses on the next launch
                                    ; (SPEC.md 20.2), so a world read minutes
                                    ; later cannot use it

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT            ; the standard About card, the standard
%define OS88UI_DROP             ; button, the drop-down (SPEC.md 13.14) and
%define OS88UI_CHK              ; the check box (13.15), which the Settings
                                ; page is the first user of,
%include "os88alt.inc"          ; SPEC.md 11.2.1.1's edge, for the bracket
%include "os88ui.inc"           ; of which this is the first user

; --- and the OVERLAY, at an address the worlds were assembled against ---------
; A world is laid at CS_WLD_AT and the shared vocabulary at CS_VOCAB_AT
; (build/cswidx.inc declares both, tools/csworlds.py assembles against them),
; so this is the one thing in the bss whose ADDRESS is part of the contract
; rather than an offset the assembler picks. What OS88_BSS is told is
; therefore the distance from os88_image_end to the TOP of the overlay, and
; not a sum of what the program asked for.
    OS88_BSS (CS_VOCAB_AT - (os88_image_end - $$)) + CS_VOCAB_MAX + CS_WLD_MAX
    OS88_IMAGE_END

; --- AND THE BSS SHIPS INSIDE THE PART (SPEC.md 20.12.10, 51.1.2) -----------
; This image is PART 0 of SKIES.O88 and the kernel does not zero a part: it
; jumps to ld_start's step 8 and not step 7, precisely so the loader's handoff
; at the head of these bytes survives. So they have to BE here.
;
; IT COSTS THE DISK ALMOST NOTHING. The row is OP_COMP and 13,777 of what
; follows is a run of zeros, which is what LZ4 is best at (51.1.2's own
; observation, one format along) - and it is what makes `image + bss` the
; length csload.asm hands to OSAPI_PKG_REHOME, said by adding the part's own
; two header fields rather than by a constant kept in step by hand.
; THE GAP IS THE ASSERTION, which is why this is three `times` and not one.
; The bss is the ZWORD chain, then a gap, then the overlay at its fixed
; address - and writing the gap as its own subtraction makes it go NEGATIVE,
; and nasm refuse the file, the moment the image plus the ordinary bss reaches
; CS_VOCAB_AT. One `times OS88_BSS_SIZE` would not: the total stays positive
; while the ZWORDs quietly overlap the vocabulary, which is a program whose
; every world pointer is right and whose state is being scribbled on.
;
; There is no %if to write here and there could not be: CS_BSS is a
; preprocessor %assign and `os88_image_end - $$` is not one, so the two can
; only meet at assembly time. The gap is the growth headroom for the image and
; the ZWORD chain TOGETHER - 9,872 bytes today, and NOT A NUMBER TO QUOTE FROM
; HERE: this comment said 1,444 for long enough to be stale by 1,236, and a
; costing believed it. Read it off the listing (`nasm -l`, the middle `times`),
; or off CS_VOCAB_AT less the image and CS_BSS.
;
; WHAT SETS IT IS NOT THIS FILE (SPEC.md 88.10.6). `CS_VOCAB_AT` is derived in
; tools/csworlds.py FROM THIS PROGRAM'S OWN SIZE - the image plus the ZWORD
; chain, rounded up to a paragraph - so the gap below the overlay is ZERO and
; the claim is what the program actually needs.
;
; IT WAS DERIVED FROM THE CEILING FOR ONE CYCLE and that is the mistake this
; replaces: APP_MAX_SIZE less the overlay is staleness-proof, like this is, but
; it maximises the gap BY CONSTRUCTION - and the gap is not free. The claim is
; CS_VOCAB_AT plus the overlay whatever the image does (88.4.5.5), so every
; byte the address is raised by is a byte of claimed RAM no instruction reads,
; on every instance, for the life of the program. At the ceiling that was 9,614
; bytes. Derived from the program it is under 16.
;
; GROWING STILL COSTS NOTHING TO GET WRONG, which is the property the ceiling
; was bought for and this keeps: add a routine or a ZWORD and the address
; MOVES, because build/cswidx.inc is regenerated from the size the program
; actually assembled to. There is no number for anyone to tune, get right, or
; leave behind - what changes is that the claim tracks the program DOWN as well
; as up.
;
; THE GAP IS STILL THE ASSERTION. It is a separate `times` for the reason the
; paragraph above gives - written as its own subtraction it goes NEGATIVE, and
; nasm refuses the file, if the derivation is ever stale - and that is now the
; backstop rather than the mechanism: a stale cswidx.inc means the two-pass
; build did not run, not that somebody forgot to raise an address.
%ifdef CS_SIZEPROBE
; --- PASS 1 (SPEC.md 88.10.6.1) ----------------------------------------------
; The overlay's address is derived from a size only nasm knows, and the bss
; that size is part of cannot be laid out until the address is known. That is
; a real circularity and this is the cut: with -DCS_SIZEPROBE the three fills
; below become two bytes, so the object is the IMAGE and then CS_BSS, and
; csworlds.py reads `image_end = len - 2` and `CS_BSS = the last word` off it.
; No listing is parsed and no symbol table is needed.
;
; The probe assembles against whatever cswidx.inc is already there, and does
; not care whether its address is right: every cs_* symbol in it is an
; immediate or an absolute disp16, so its VALUE cannot change an encoding's
; length, and the fills that WOULD care are the ones this replaces.
    dw CS_BSS
%else
    times CS_BSS db 0                       ; the declared bss...
    times (CS_VOCAB_AT - (os88_image_end - $$)) - CS_BSS db 0    ; ...the gap...
    times CS_VOCAB_MAX + CS_WLD_MAX db 0    ; ...and the overlay
%endif
