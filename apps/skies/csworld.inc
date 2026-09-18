; =============================================================================
; os8088 - apps/skies/csworld.inc
;
; THE AEROPLANES (SPEC.md 88.7), and what a world needs to be read at all.
;
; A LOCATION IS A RUNWAY AND THE COUNTRY ROUND IT (SPEC.md 88.6.4). The world
; used to be one global table read straight by cs_scene; it is a pair of words
; in the location record now (CSA_OBJS, CSA_NOBJ), so nine places can stand in
; nine different countries and the picked row is what the cull walks. Metres;
; x east, z north, y up; EVERY WORLD PUTS ITS OWN ORIGIN WHERE ITS OWN LANDMARK
; IS, because nothing outside a world's own file ever reads its coordinates.
;
; A MODEL is db type, nverts (a STACK's LEVELS), nfaces, nedges, edge ink, 0;
; dw radius, verts, faces, edges. A STACK's verts are levels of (wx, h, wz),
; each standing for the four corners (+wx,h,+wz) (-wx,h,+wz) (-wx,h,-wz)
; (+wx,h,-wz), so a box is two levels and a pyramid a level and an apex. A
; FLAT model's verts are (x, z) pairs on the ground. A FACE is db n, ink,
; flags, then n indices, listed COUNTER-CLOCKWISE seen from outside; an EDGE
; is two indices.
;
; THE LIMITS A WORLD FILE MUST RESPECT, all of them from skies.asm:
;   CS_MAXV   24  vertices in one model - so at most SIX levels in a STACK
;   CS_MAXPV  10  vertices in one face after the near clip
;   CS_NVIS   32  objects that can be in ONE FRAME; the 33rd is dropped
;                 silently, so a world whose skyline is denser than that
;                 loses buildings rather than slowing down
;   CS_FAR 16000  metres: nothing further is transformed at all
; =============================================================================

%include "cswmac.inc"           ; the macros, which emit nothing. The tables
                                ; they build against are csvocab.inc, and that
                                ; is NOT included here any more: it lives in
                                ; the overlay now (SPEC.md 88.10.5), and
                                ; build/cswidx.inc is where both this program
                                ; and a world part learn its addresses

; =============================================================================
; The aeroplanes (SPEC.md 88.7): one row, and every constant the flight model
; reads. Angles are 65536 to the turn and rates are per tick.
;
; SPEEDS ARE 16.7 m/s AND NOT 16.8 (SPEC.md 88.7.4). The dive caps at 1.25 x
; VMAX and two sites read the speed SIGNED - the cap's own `jle` and cs_move's
; `imul` - so 16.8 put a ceiling of 102 m/s on VMAX, which is 198 knots: fine
; for a trainer and a biplane, and not an aeroplane faster than either. Half
; the resolution buys twice the range, every signed site keeps working
; untouched, and what changed instead is arithmetic: VSTALL/VROT/VMAX are
; x 128, THRUST/FRICT/BRAKE halved, and DRAGK re-derived from the new THRUST
; rather than scaled - it reads the HIGH WORD of v x v, which at 16.7 is a
; quarter of what it was, so the balance point moves by 2 and not by 1.
; =============================================================================
cs_p_c172:
    dw cs_s_c172                    ; CSP_NAME
    dw 24 * 128                     ; CSP_VSTALL   48 knots
    dw 28 * 128                     ; CSP_VROT     55 knots
    dw 78 * 128                     ; CSP_VMAX    150 knots, level at full power
    dw 16                           ; CSP_THRUST   2.2 m/s^2 at full throttle
    dw 669                          ; CSP_DRAGK    balances THRUST at VMAX
    dw 3                            ; CSP_FRICT    rolling, on the ground
    dw 30                           ; CSP_BRAKE    20 x 3/2 (88.7.10.2)
    dw 546                          ; CSP_ROLLR    3 degrees a tick, held
    dw 146                          ; CSP_ROLLL    0.8 a tick back to level
    dw 146                          ; CSP_PITCHR   0.8 a tick, held
    dw 0                            ; CSP_PITCHT   the nose STAYS where the stick
                                    ;              left it: a trimmed aeroplane
    dw 60                           ; CSP_TURNK    3 degrees a second at 30 of bank
    dw 2                            ; CSP_EYE      metres above the wheels
    dw 10923                        ; CSP_MAXROLL  60 degrees
    dw 7282                         ; CSP_MAXPITCH 40 degrees
    dw cs_ck_c172                   ; CSP_COCKPIT  (cspanel.inc)
    dw cs_att_trim                  ; CSP_ATT      a trainer's model (88.7.2)
    dw 0                            ; CSP_SPOOL    a piston: the throttle IS
                                    ;              the power (88.7.5)
    dw 0                            ; CSP_LAUNCH   on the runway
    dw 0                            ; CSP_FLAGS
    dw cs_art_c172                  ; CSP_ART      SIDE ON (88.10.1)
    dw 703                         ; CSP_INDK     the wing's own drag (88.7.12)
    dw cs_snd_c172                  ; CSP_SND      a Lycoming (88.8.2)
cs_s_c172:   db 'CESSNA 172', 0

; --- the second row: a PITTS SPECIAL (SPEC.md 88.7.2), an aerobatic biplane.
;     Light, quick on the controls and limited by nothing - it rolls right
;     round, loops over the top and stays where the stick leaves it, which is
;     cs_att_free's whole job. The numbers are an S-2B's: 1,150 lb empty,
;     260 hp, stalls near 52 knots, cruises 152 mph and never-exceeds 203 ---
cs_p_pitts:
    dw cs_s_pitts                   ; CSP_NAME
    dw 27 * 128                     ; CSP_VSTALL   52 knots
    dw 30 * 128                     ; CSP_VROT     58 knots
    dw 88 * 128                     ; CSP_VMAX    171 knots, level at full power
    dw 26                           ; CSP_THRUST   3.7 m/s^2: 260 hp on half
                                    ;              the Cessna's weight
    dw 855                          ; CSP_DRAGK    balances THRUST at VMAX
    dw 3                            ; CSP_FRICT    a taildragger on grass
    dw 35                           ; CSP_BRAKE    23 x 3/2 (88.7.10.2)
    dw 1820                         ; CSP_ROLLR   10 degrees a tick - 182 a
                                    ;              second, and the real one
                                    ;              manages 240
    dw 0                            ; CSP_ROLLL    NOTHING returns it to level
    dw 728                          ; CSP_PITCHR   4 degrees a tick: a loop in
                                    ;              about five seconds
    dw 0                            ; CSP_PITCHT   ...and the nose stays put
    dw 90                           ; CSP_TURNK    a shorter wing, a tighter
                                    ;              turn at the same bank
    dw 2                            ; CSP_EYE      metres above the wheels
    dw 32767                        ; CSP_MAXROLL  UNUSED by cs_att_free, and
    dw 16384                        ; CSP_MAXPITCH filled in with the honest
                                    ;              figures - 180 and 90
                                    ;              degrees - so a reader is
                                    ;              not told the wrong thing
    dw cs_ck_pitts                  ; CSP_COCKPIT  its own scattered panel
    dw cs_att_free                  ; CSP_ATT      the aerobatic model
    dw 0                            ; CSP_SPOOL    a piston
    dw 0                            ; CSP_LAUNCH   on the runway
    dw 0                            ; CSP_FLAGS
    dw cs_art_pitts                 ; CSP_ART      three-quarters from ahead
                                    ;              and BELOW, banked - a
                                    ;              different VIEW and not only
                                    ;              a different aeroplane
                                    ;              (88.10.1)
    dw 1440                        ; CSP_INDK     the wing's own drag (88.7.12)
    dw cs_snd_pitts                 ; CSP_SND      six of them (88.8.2)
cs_s_pitts:  db 'PITTS SPECIAL', 0

; --- the third row: a FOUGA CM.170 MAGISTER (SPEC.md 88.7.5), the French jet
;     trainer the Patrouille de France flew nine of. It is here for one
;     MECHANIC and not for the number on the airspeed indicator: CSP_ATT is
;     cs_att_lag, so the stick sets a rate the aeroplane EASES onto, and
;     CSP_SPOOL 5 makes the thrust follow the throttle over about five
;     seconds. 715 km/h, stalls at 125, and its two Marbore turbojets are
;     0.28 of its weight in thrust ------------------------------------------
cs_p_fouga:
    dw cs_s_fouga                   ; CSP_NAME
    dw 35 * 128                     ; CSP_VSTALL   68 knots
    dw 42 * 128                     ; CSP_VROT     82 knots
    dw 190 * 128                    ; CSP_VMAX    369 knots. 1.25 x this is
                                    ;             30,400, which is the whole
                                    ;             reason 16.7 came first (88.7.4)
    dw 19                           ; CSP_THRUST   2.7 m/s^2: 0.28 t/w
    dw 137                          ; CSP_DRAGK    balances THRUST at VMAX
    dw 3                            ; CSP_FRICT
    dw 33                           ; CSP_BRAKE    22 x 3/2 (88.7.10.2) - and
                                    ;              this is the airbrake too
    dw 1200                         ; CSP_ROLLR    6.6 degrees a tick ASKED
                                    ;              FOR; the lag decides when
                                    ;              it arrives
    dw 0                            ; CSP_ROLLL    nothing returns it to level
    dw 600                          ; CSP_PITCHR   3.3 a tick asked for
    dw 0                            ; CSP_PITCHT
    dw 40                           ; CSP_TURNK    a jet at 190 m/s turns
                                    ;              BADLY: two thirds of the
                                    ;              trainer's at the same bank
    dw 3                            ; CSP_EYE
    dw 32767                        ; CSP_MAXROLL  unused by cs_att_lag, and
    dw 16384                        ; CSP_MAXPITCH honest: it aerobats
    dw cs_ck_fouga                  ; CSP_COCKPIT  a jet's panel
    dw cs_att_lag                   ; CSP_ATT      the lagging model (88.7.5)
    dw 5                            ; CSP_SPOOL    a 32nd of the gap a tick:
                                    ;              95% of the way in 5.3 s
    dw 0                            ; CSP_LAUNCH   on the runway
    dw 0                            ; CSP_FLAGS
    dw cs_art_fouga                 ; CSP_ART      from BEHIND and above,
                                    ;              which is where a butterfly
                                    ;              tail is a butterfly tail
                                    ;              (88.10.1)
    dw 651                         ; CSP_INDK     the wing's own drag (88.7.12)
    dw cs_snd_fouga                 ; CSP_SND      two turbojets (88.8.2)
cs_s_fouga:  db 'FOUGA MAGISTER', 0

; --- the fourth row: a WASSMER WA-30 BIJAVE (SPEC.md 88.7.6), the French
;     two-seat sailplane. NO ENGINE AT ALL - CSP_THRUST is zero, so the only
;     energy it has is the height it starts with and every turn spends some.
;     CSP_LAUNCH 600 is what puts it there: the tow is released over the
;     field and the session begins in the air, because a winch launch is a
;     scripted sequence and this is not one. 28:1 at 80 km/h, which in a
;     model whose only sink is the nose attitude IS two degrees down --------
cs_p_bijave:
    dw cs_s_bijave                  ; CSP_NAME
    dw 17 * 128                     ; CSP_VSTALL   33 knots
    dw 250 * 128                    ; CSP_VROT     UNREACHABLE, deliberately:
                                    ;              a sailplane that lands has
                                    ;              landed, and no throttle can
                                    ;              rotate it again
    dw 61 * 128                     ; CSP_VMAX    119 knots, the VNE
    dw 0                            ; CSP_THRUST   NONE
    dw 1300                         ; CSP_DRAGK    balances gravity's component
                                    ;              at two degrees down and
                                    ;              22 m/s, which is 28:1
    dw 8                            ; CSP_FRICT    a skid on grass
    dw 23                           ; CSP_BRAKE    the wheel brake, 15 x 3/2
    dw 546                          ; CSP_ROLLR    3 degrees a tick: a long
                                    ;              wing is slow in roll
    dw 60                           ; CSP_ROLLL    ...and comes back gently
    dw 110                          ; CSP_PITCHR   0.6 a tick
    dw 0                            ; CSP_PITCHT   trimmed: the nose stays
    dw 70                           ; CSP_TURNK
    dw 1                            ; CSP_EYE      low, reclined
    dw 10923                        ; CSP_MAXROLL  60 degrees
    dw 7282                         ; CSP_MAXPITCH 40
    dw cs_ck_bijave                 ; CSP_COCKPIT  no throttle, a variometer
    dw cs_att_trim                  ; CSP_ATT      docile, and it auto-levels
    dw 0                            ; CSP_SPOOL    there is no engine to spool
    dw 600                          ; CSP_LAUNCH   metres, tow just released
    dw 0                            ; CSP_FLAGS
    dw cs_art_bijave                ; CSP_ART      nearly PLAN, from above:
                                    ;              a sailplane's identity is
                                    ;              its span, and a side view
                                    ;              hides all of it (88.10.1)
    dw 344                         ; CSP_INDK     the wing's own drag (88.7.12)
    dw 0                            ; CSP_SND      NO ENGINE (88.8.2), and
                                    ;              0 is the silence 88.7.6.1
                                    ;              already gave it
cs_s_bijave: db 'WASSMER BIJAVE', 0

; --- the fifth row: an ICON A5 (SPEC.md 88.7.7), the light sport amphibian.
;     It fits the slow end of the envelope the other four leave empty - 95
;     knots and a 39-knot stall - and its whole point is CSPF_AMPHIB: it may
;     touch down on a location's WATER STRIP as well as its runway, and
;     where the place has water it STARTS there, hull in, which all nine of
;     them do. On the water the hull drags twice as hard as a wheel and
;     there is nothing to brake with -----------------------------------------
cs_p_a5:
    dw cs_s_a5                      ; CSP_NAME
    dw 20 * 128                     ; CSP_VSTALL   39 knots
    dw 24 * 128                     ; CSP_VROT     47 knots
    dw 49 * 128                     ; CSP_VMAX     95 knots
    dw 14                           ; CSP_THRUST   2.0 m/s^2: 100 hp Rotax, and
                                    ;              the field asked for a
                                    ;              quarter more than the 11
                                    ;              (1.5) it had - it is the
                                    ;              slowest aeroplane here and
                                    ;              was the slowest to get going
    dw 1400                         ; CSP_DRAGK    balances THRUST at VMAX -
                                    ;              RE-DERIVED with it, so the
                                    ;              A5 accelerates harder and
                                    ;              still tops out at 95 knots:
                                    ;              600 x 1500 >> 16 = 13
                                    ;              against a thrust of 14,
                                    ;              which is the same one-under
                                    ;              margin 1201/11 had
    dw 3                            ; CSP_FRICT    on land; DOUBLED on water
    dw 27                           ; CSP_BRAKE    18 x 3/2 (88.7.10.2), on
                                    ;              the wheels with B - and
                                    ;              on the WATER always, with
                                    ;              no key, because a hull is
                                    ;              a brake (88.7.7.2)
    dw 500                          ; CSP_ROLLR    2.7 degrees a tick
    dw 120                          ; CSP_ROLLL    it comes back to level on
                                    ;              its own: a spin-resistant
                                    ;              wing, and the easiest
                                    ;              aeroplane here to fly
    dw 130                          ; CSP_PITCHR   0.7 a tick
    dw 0                            ; CSP_PITCHT
    dw 65                           ; CSP_TURNK
    dw 2                            ; CSP_EYE
    dw 10923                        ; CSP_MAXROLL  60 degrees
    dw 7282                         ; CSP_MAXPITCH 40
    dw cs_ck_a5                     ; CSP_COCKPIT  one wide glass panel
    dw cs_att_trim                  ; CSP_ATT      docile, and it auto-levels
    dw 0                            ; CSP_SPOOL    a piston
    dw 0                            ; CSP_LAUNCH   ...on the WATER, which is
                                    ;              CSP_FLAGS' doing and not
                                    ;              a launch height
    dw CSPF_AMPHIB                  ; CSP_FLAGS
    dw cs_art_a5                    ; CSP_ART      from ahead and low, nose
                                    ;              to the RIGHT - the one that
                                    ;              shows a hull is a hull
                                    ;              (88.10.1)
    dw 710                         ; CSP_INDK     the wing's own drag (88.7.12)
    dw cs_snd_a5                    ; CSP_SND      a geared Rotax (88.8.2)
cs_s_a5:     db 'ICON A5', 0

; =============================================================================
; THE WORLDS (SPEC.md 88.6.4) - one file each, and each file is whole: its
; models, its object table, its names and its location record, in that order,
; because CS_N_* is an equ over the table's own two labels. Nothing here reads
; anything there but the two symbols the location table in skies.asm names.

; =============================================================================
; THE WORLDS ARE NOT HERE (SPEC.md 88.10.5). Eight files stood at the foot of
; this one, 11,904 bytes of models and object tables for nine countries, of
; which exactly ONE is under the aeroplane at any moment. They are parts now:
; packed by tools/csworlds.py, each assembled on its own at the overlay's
; fixed org, and read into cs_wldat when a location is picked.
; =============================================================================

; =============================================================================
; THE ENGINES (SPEC.md 88.8.2)
;
; One record per aeroplane that has one. Four aeroplanes, four engines, and the
; Bijave's word is 0 because a sailplane has none - its sound is the air's
; swoop (88.7.6.3) and nothing here touches it.
;
; THESE NUMBERS HAVE BEEN LISTENED TO, which is the only way any of them could
; have been settled (88.8.2.2): an emulator synthesises a square wave with none
; of a real speaker's response, so every figure below is the field's and not
; arithmetic's. What the first set got right was the trainer and the biplane,
; and the idles - "idle is audible", "sounds pretty good at idle". What it got
; wrong was the top of both the amphibian and the jet, and the BEAT, which is
; gone (88.8.2.1).
;
; THE ORDER IS STILL THE POINT and it is still the honest one. What a listener
; identifies in a piston aeroplane is the propeller's blade pass, which on a
; four-cylinder four-stroke is the firing rate too: 2 x rpm / 60. A Cessna at
; 2,700 rpm is 90 Hz, a six-cylinder Pitts firing three times a revolution is
; 135, and a Rotax 912 turning 5,800 through a 2.43:1 gearbox fires at 193 - so
; the amphibian is the highest-pitched piston here and the jet is not in the
; same octave as any of them. What the FIELD then did to that is cut the top of
; the two that were right on paper and wrong in a room, which is the whole
; reason 88.8.2.2 says an emulator cannot settle this.
; =============================================================================
cs_snd_c172:   dw 60, 45                ; 60..105 Hz. A direct-drive Lycoming,
               db 2, 4, 0               ; four cylinders, two blades - and the
                                        ; one the field has passed twice
cs_snd_pitts:  dw 75, 85                ; 75..160 Hz. Six cylinders firing 3 a
               db 2, 5, 0               ; revolution is 135 at 2,700 rpm: the
                                        ; loud one, and it should sound it. A
                                        ; tighter ceiling than the trainer's -
                                        ; "slightly steppy, but overall good" -
                                        ; because its range is twice as wide
cs_snd_fouga:  dw 180, 520              ; 180..700 Hz, off the thrust it HAS.
               db 3, 6, CSSF_SPOOL      ; A Marbore at 21,500 rpm is a whine
                                        ; and the arithmetic said 260..1200 -
                                        ; which the field heard as "much too
                                        ; high pitched for human ears", 1,200
                                        ; sitting in the band a PC speaker is
                                        ; harshest in. Down a fifth and a bit,
                                        ; still an octave and a half clear of
                                        ; every piston, and the TIGHTEST pitch
                                        ; ceiling here because its range is
                                        ; two octaves where no piston's is
                                        ; more than one
cs_snd_a5:     dw 95, 75                ; 95..170 Hz, and the top is the
               db 2, 4, 0               ; field's: 190 was "slightly high
                                        ; pitched at full. Probably accurate,
                                        ; but again, human ears and all". It
                                        ; stays the highest piston, by less
