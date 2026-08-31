/* ============================================================================
 * os8088 - apps/weave/watom.c
 *
 * AN APP ATOM'S BYTES IN A BUNDLE CLAIM (WEAVE-SPEC 2.7). Two functions and
 * nothing else.
 *
 * They stood in apps/weave/wval.c until wave 7 and were EXTRACTED, not
 * rewritten: LOOM's Preview module (WEAVE-SPEC 1.2.4) walks the same UISTREAM
 * and paints the same labels, and it needs these two without needing the rest
 * of the hostile-bundle reader. wave 6 did exactly this to apps/weave/wnum.inc
 * for the same reason and WEAVE-SPEC 1.2 is the rule both extractions serve:
 * what the two packages share they share as SOURCE, and never as a second
 * copy.
 *
 * IT IS #included AT THE POINT THE TWO FUNCTIONS USED TO STAND, so the
 * translation unit WEAVE compiles is token-for-token what it was; wave 7
 * checked that by rebuilding build/weave.bin and comparing it whole.
 *
 * WHAT THEY ASSUME. w_soff[W_ATOMS], w_natoms and w_seg are set, and the
 * ATOMS section has been validated - in WEAVE by ovl_val_atoms, in the
 * preview module by the fact that LOOM's own packer wrote the image a
 * moment ago and the module re-reads its section table before it believes an
 * offset. Well-known ids have NO string table in the runtime (2.7), which is
 * why every PUSHA operand is 64..250 and why these answer 0 for one.
 * ==========================================================================*/

static unsigned w_atom_off(unsigned a)
{
    if (a < WA_APP_FIRST || a >= WA_APP_FIRST + w_natoms)
        return 0;
    return w_soff[W_ATOMS] + w_w(w_seg, w_soff[W_ATOMS] + 2 +
                                 2 * (a - WA_APP_FIRST)) + 1;
}

static unsigned w_atom_len(unsigned a)
{
    if (a < WA_APP_FIRST || a >= WA_APP_FIRST + w_natoms)
        return 0;
    return w_b(w_seg, w_atom_off(a) - 1);
}
