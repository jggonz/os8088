/* ============================================================================
 * os8088 - apps/pagedesk/pduerr.c    ONE SENTENCE PER PDE_* (PAGEDESK-SPEC 3.6)
 *
 * A refusal names the feature or the limit, never "error". Every code in
 * pdabi.h has a row; an unknown code (a module newer than this table) says
 * so with its number rather than pretending to be one of these.
 * ==========================================================================*/

static const char *pdu_errtext(int code)
{
    switch (code) {
    case PDE_OK:       return "Done.";
    case PDE_SYNTAX:   return "The file is damaged: it does not parse as PDF.";
    case PDE_XREF:     return "The file's cross-reference table is damaged.";
    case PDE_TRUNC:    return "The file is truncated.";
    case PDE_CYCLE:    return "The file's page or resource tree loops.";
    case PDE_DEPTH:    return "The file nests objects deeper than PageDesk reads.";
    case PDE_FLATE:    return "Compressed (Flate) content is not in the XT profile.";
    case PDE_FILTER:   return "It uses a stream filter PageDesk cannot decode.";
    case PDE_FONT:     return "It uses a font PageDesk cannot draw.";
    case PDE_GRAPHICS: return "It uses graphics outside the XT profile.";
    case PDE_ENCRYPT:  return "The file is encrypted; PageDesk cannot open it.";
    case PDE_XREFSTM:  return "It uses cross-reference streams (PDF 1.5+ packing).";
    case PDE_SIGNED:   return "The file is signed: it opens read-only.";
    case PDE_RANGE:    return "A page or number is outside PageDesk's range.";
    case PDE_LIMIT:    return "The file exceeds one of PageDesk's limits.";
    case PDE_NOMEM:    return "Not enough memory. Close a window and try again.";
    case PDE_STACK:    return "A page needed more stack than PageDesk has; the document was closed."; /* deepstk */
    case PDE_IO:       return "The disk could not be read.";
    case PDE_DISKFULL: return "The disk is full.";
    case PDE_WPROT:    return "The disk is write-protected.";
    case PDE_EXISTS:   return "A file of that name is already there.";
    case PDE_CHANGED:  return "The file changed on disk since it was opened.";
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
