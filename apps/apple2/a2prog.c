/* ============================================================================
 * os8088 - apps/apple2/a2prog.c   ovl_*: Load Program and Save Program
 *
 * Part of APPLE2 (docs/APPLE2-SPEC.md section 12). #included into
 * apps/apple2/apple2.c - ONE translation unit (SPEC.md 73.1).
 * apps/apple2/ is GPL-2-or-later; see apps/apple2/COPYING.
 *
 * ----------------------------------------------------------------------------
 * THESE TWO ROWS HAVE NO DEFINER IN ANY REFERENCE, AND THAT IS RECORDED
 * ----------------------------------------------------------------------------
 * MII's `Load & Run Binary...` is `.disabled = 1` with its handler commented
 * out; apple2emu and AppleWin reach a program through a disk. So these are
 * THIS PORT'S OWN rows, and section 12 states the reason: there is no Disk II
 * in this PR and a listing has to get in somehow.
 *
 * They are written from AppleWin's `bin/A2_BASIC.SYM`, which names its own
 * source (Bob Sander-Cederlof's S-C DocuMentor: Applesoft):
 *
 *     TXTTAB $67/$68   VARTAB $69/$6A   ARYTAB $6B/$6C   STREND $6D/$6E
 *     FRETOP $6F/$70   MEMSIZ $73/$74   PRGEND $AF/$B0
 *
 * and program text begins at $0801 on a II+.
 *
 * WHAT WAVE 4 WRITES HERE (it is pinned in section 12 so that the wave is a
 * transcription and not a re-design):
 *
 *   Load  os88_fdlg for the picker, then the file into $0801 with the
 *         documented 2-BYTE-LENGTH-PREFIX SNIFF - if word 0 equals
 *         filesize - 2, skip it; otherwise the file is headerless - then
 *         TXTTAB = $0801 and VARTAB = ARYTAB = STREND = PRGEND = the end.
 *   Save  $0801 to VARTAB - 1, headerless.
 *
 * ...and CC_ASSOC declares `BAS` in the same wave (a2assoc.inc), so a .BAS
 * beside the package opens on the FIRST double-click of a cold boot. Not
 * before: declaring an extension the build cannot open launches the emulator
 * and refuses, which is worse than no association.
 * ==========================================================================*/

/* ovl_a2_prog - the body os88_onfile calls, once the picker has a name.
 *
 * The RESIDENT half (os88_onfile in apple2.c) does the size refusal and the
 * overlay fence; this is the part that runs once per command and therefore
 * goes out (SPEC.md 73.14). It answers 1 when it loaded or saved. */
static int ovl_a2_prog(int mode, const char *name, unsigned size_lo)
{
    (void)mode;
    (void)name;
    (void)size_lo;
    a2_say("No loader in this build.");
    return 0;
}
