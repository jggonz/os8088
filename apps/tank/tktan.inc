; tan(2i units) x 256 for i = 0..12 - the ridge's sampling angles either side
; of the sights, 2 units apart (tk_ridgesetup, SPEC.md 85.5.1). A tangent is
; odd, so the twelve to the left are these negated and are not stored; and
; the steepest is 171, so a byte holds every one.
    db     0,    13,    25,    38,    51,    64
    db    78,    92,   106,   121,   137,   153,   171
