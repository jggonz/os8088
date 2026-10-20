/* ============================================================================
 * os8088 - apps/apple2/a2disk.c   ovl_*: the Disk II dialog - A STUB
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 14). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * ----------------------------------------------------------------------------
 * READ-ONLY DISK II IS A FOLLOW-UP PULL REQUEST (section 14, and
 * docs/APPLE2-PORT-PLAN.md Decision 13), AND ITS FIRST JOB IS A MEASURED SIZE
 * LINE
 * ----------------------------------------------------------------------------
 * Machine > Configure Slots... is GREYED in this PR and the fact that greys
 * it is a2menu.c's own comment beside the item:
 *
 *     No Disk II in this build. Load Program reads an Applesoft program,
 *     and Paste types a listing in.
 *
 * What lands here when the follow-up comes is pinned in section 14 so that
 * the wave is a transcription rather than a re-design: MII's own two-drive
 * dialog (`ui_gl/mii_mui_2dsk.c:87-145`) with `Drive 1:` / `Drive 2:`, Select
 * becoming Eject when loaded, the empty field reading
 * `Click "Select" to pick a file`, and a wrong size raising MII's "Invalid
 * Disk Image" alert with the size formatted HUMAN-READABLY; the exactly
 * 143,360-byte check; the extension alone picking the sector order (`.DSK`
 * and `.DO` DOS 3.3, `.PO` ProDOS) with `.NIB`, `.WOZ`, `.2MG` and `.HDV`
 * refused BY NAME; a 143,360-byte HEAP CLAIM PER DRIVE reached by segment
 * arithmetic, with drive 2 refusing on `os88_mem_largest_kb()`; and
 * CC_ASSOC gaining `DSK`, `DO` and `PO` in that wave and not before.
 *
 * The file exists from wave 1 because it is a WRITTEN PREREQUISITE and an
 * `#include`: make cannot see through either, and a file a later wave adds is
 * a build the tree does not know about (LESSONS.md 9).
 * ==========================================================================*/

/* AND THERE IS NO `ovl_a2_slots` HERE YET, DELIBERATELY. `nasm -f bin` has no
 * dead-code elimination, so a defined-and-uncalled ovl_* is emitted into
 * APPLE2.OVL and is NOT in the runtime's `cc_ovm` table - and that table is
 * the only place a reader can check what the overlay is actually entered
 * through, so an entry point nothing can reach makes it a lie. Machine >
 * Configure Slots... is greyed and a greyed item does not dispatch, so the
 * follow-up PR adds the function and the ovl_a2_cmd route in the same change
 * that un-greys the row. */
