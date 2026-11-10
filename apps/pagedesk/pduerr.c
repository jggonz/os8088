/* ============================================================================
 * os8088 - apps/pagedesk/pduerr.c    ONE SENTENCE PER PDE_* (PAGEDESK-SPEC 3.6)
 *
 * A refusal names the feature or the limit, never "error". Every sentence is at most
 * 39 characters: the empty-view card shows it on one line in the narrowest canvas
 * (QA F14; tests/unit/t_pderrlen.py holds it). Every code in
 * pdabi.h has a row; an unknown code (a module newer than this table) says
 * so with its number rather than pretending to be one of these.
 * ==========================================================================*/

static const char *pdu_errtext(int code)
{
    switch (code) {
    case PDE_OK:       return "Done.";
    case PDE_SYNTAX:   return "Damaged: it does not parse as a PDF.";
    case PDE_XREF:     return "Its cross-reference table is damaged.";
    case PDE_TRUNC:    return "The file is truncated.";
    case PDE_CYCLE:    return "Its page or resource tree loops.";
    case PDE_DEPTH:    return "Objects nest too deep for PageDesk.";
    case PDE_FLATE:    return "No Flate cache (low RAM, or set Off).";   /* prefs: D17's two causes */
    case PDE_FILTER:   return "Its stream filter is not supported.";
    case PDE_FONT:     return "It uses a font PageDesk cannot draw.";
    case PDE_GRAPHICS: return "Its graphics are outside the profile.";
    case PDE_PASSWORD: return "Password needed.";
    case PDE_ENCRYPT:  return "Its encryption is not supported.";
    case PDE_XREFSTM:  return "Its cross-reference stream is damaged.";
    case PDE_SIGNED:   return "The file is signed: it opens read-only.";
    case PDE_RANGE:    return "A number is outside PageDesk's range.";
    case PDE_LIMIT:    return "It exceeds one of PageDesk's limits.";
    case PDE_NOSLOT:                    /* no RAM for a module (20.22.6) */
    case PDE_NOMEM:    return "Out of memory. Close a window, retry.";
    case PDE_STACK:    return "Out of module stack; document closed."; /* deepstk */
    case PDE_IO:       return "The disk could not be read.";
    case PDE_DISKFULL: return "The disk is full.";
    case PDE_WPROT:    return "The disk is write-protected.";
    case PDE_EXISTS:   return "A file of that name is already there.";
    case PDE_CHANGED:  return "The file changed since it was opened.";
    case PDE_NOTFOUND: return "Not found, or not in this build yet.";
    case PDE_CANCEL:   return "Cancelled.";
    case PDE_BUSY:     return "Still working.";
    }
    return "An unknown problem (a newer module?).";
}

/* a FERR_* (the file layer's own) as the PDE_* it means */
static int pdu_ferr_pde(int ferr)
{
    if (ferr == OS88_FERR_FULL || ferr == OS88_FERR_DIRFULL)
        return PDE_DISKFULL;
    if (ferr == OS88_FERR_WPROT || ferr == OS88_FERR_PROT)
        return PDE_WPROT;
    if (ferr == OS88_FERR_EXIST)
        return PDE_EXISTS;
    if (ferr == OS88_FERR_NOENT || ferr == OS88_FERR_NAME)
        return PDE_NOTFOUND;
    return PDE_IO;
}
