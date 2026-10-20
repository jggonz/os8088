; =============================================================================
; os8088 - apps/skies/cswdefs.inc
;
; THE CONSTANTS A WORLD FILE NEEDS (SPEC.md 88.10.5), and only those: the inks,
; the model kinds, the face flags, an object row's size and flags, and the one
; location flag a world sets. Twenty-four, counted.
;
; IT IS A SECOND COPY ON PURPOSE, and tests/unit/t_mirror.py holds it to
; skies.asm - both files are in its ASM list, so a constant that disagrees
; fails the build with no new gate to remember. A world part is assembled on its OWN - it has to be,
; or it could not be laid at a fixed org - so it cannot see skies.asm's own
; declarations, and moving them out of skies.asm would take twenty-four equs
; away from the prose that explains them. The tree's answer to a name defined
; in two places is a gate that compares them (tests/unit/t_mirror.py's shape),
; not a rule nobody remembers.
; =============================================================================

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
CSI_HILL2  equ 16               ; a hill's +-x faces (88.4.4.1)
CSI_HILLTOP equ 17              ; ...and its cap

CSM_STACK equ 0                 ; levels of (wx, h, wz): boxes, pyramids, the
                                ; tower (SPEC.md 88.5)
CSM_FLAT  equ 1                 ; explicit (x, z) pairs on the ground

CSF_NOCULL equ 1                ; a ground polygon: visible from either side
; --- ...and which way a STACK face points, so it can be culled against the
;     EYE'S OWN WORLD POSITION before anything is gathered or projected
;     (SPEC.md 88.5.12). Free: the flags byte was 0 on every face -----------
CSF_PX    equ 2                 ; the face whose outward normal is +x
CSF_MX    equ 4                 ; ...-x
CSF_PZ    equ 8                 ; ...+z
CSF_MZ    equ 16                ; ...-z
CSF_TOP   equ 32                ; ...+y, a stack's cap

CSO_SIZE  equ 20

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

CSA_OCC   equ 1                 ; RUN THE OCCLUSION PASS here. It is off
