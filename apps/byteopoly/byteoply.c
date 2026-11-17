/* Byteopoly for os8088 (SPEC.md 131). Original. One translation unit
 * (SPEC.md 73.1): the subsystems are included here in dependency order, and
 * this order is the module map. hosttest/ includes the OS-free subset with
 * BP_HOST defined (SPEC.md 131.12). Lead-owned. */
#ifndef BP_HOST
#include "os88.h"
#endif
#include "bp.h"
/* immutable tables and arithmetic (rules lane) */
#include "bp_board.c"
#include "bp_cards.c"
#include "bp_math.c"
/* the engine (rules lane) */
#include "bp_rules.c"
/* canonical encoding and digest (save lane) */
#include "bp_codec.c"
#ifndef BP_HOST
/* files (save lane), the session and cable (link lane), the face, the
 * panels and the application (face, panels lanes) */
#include "bp_save.c"
#include "bp_link.c"
#include "bp_face.c"
#include "bp_panels.c"
#include "bp_app.c"
#endif
