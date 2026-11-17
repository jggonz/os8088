/* ============================================================================
 * pdelink.c - LINKS (PAGEDESK-SPEC 9.4, 20.10.2)
 *
 * A /Link annotation says where a click goes; PDENG only READS it. It never
 * runs anything: a /URI is handed up as text for the status line, and a
 * /Launch or /JavaScript action is named so the primary can say it was not
 * run (never executed, 1.4). The link record is the ANNOTS record (pdabi.h,
 * no field added) with these meanings for Subtype /Link:
 *
 *   state         0 = no destination in this document, else the page index + 1
 *   nm            the action's /S name ("GoTo", "URI", "Launch", ...), or
 *                 "Named" for a /Dest the engine could not resolve
 *   contents_len  a /URI's true length, contents its first 159 characters
 *
 * PDP_HASLINKS (PAGEINFO) is set when one of the page's first 64
 * annotations is a /Link. A destination is an array [page ...] (direct, as
 * /Dest or as /A /D) or a NAME looked up in the catalog's /Dests dictionary
 * (PDF 1.1); the name TREE of later files is not read - such a link answers
 * state 0 and nm "Named", and the primary says so.
 * ==========================================================================*/
#define PDE_LINK_SCAN 64

static pdw lk_page_has(pdw num, pdw gen)
{
    static struct pd_u32 ap;
    pdw rc, k, n, g;

    if (obj_dict(num, gen) != PDE_OK) return 0;
    if (od_key("Annots") != PDE_OK) return 0;
    if (ov.kind == PDK_REF && ob_follow() != PDE_OK) return 0;
    if (ov.kind != PDK_ARR) return 0;
    for (k = 0; k < PDE_LINK_SCAN; k++) {
        rc = ob_value();
        if (rc || ov.kind == PDK_CLOSE) return 0;
        if (ov.kind != PDK_REF) {
            if ((ov.kind == PDK_ARR || ov.kind == PDK_DICT) && ob_skipc()) return 0;
            continue;
        }
        n = ov.rnum; g = ov.rgen;
        lx_tell(&ap);
        if (obj_dict(n, g) == PDE_OK && od_key("Subtype") == PDE_OK
            && ov.kind == PDK_NAME && str_eq(ov.text, "Link"))
            return 1;
        lx_seek(&ap);
    }
    return 0;
}

/* the page index + 1 of the destination the lexer is at (a name is looked up
 * in the catalog's /Dests), 0 when it does not resolve */
static char lk_name[32];
static pdw lk_dest(struct pdxb_annot *a)
{
    pdw rc, i;
    if (ov.kind == PDK_NAME || ov.kind == PDK_STR) {
        for (i = 0; i < ov.len && i < 31; i++) lk_name[i] = (char)ov.text[i];
        lk_name[i] = 0;
        os_cpy(a->nm, "Named");
        if (obj_dict(tr_root_n, tr_root_g) != PDE_OK) return 0;
        if (od_key("Dests") != PDE_OK) return 0;
        if (ov.kind == PDK_REF && ob_follow() != PDE_OK) return 0;
        if (ov.kind != PDK_DICT) return 0;
        if (ob_dict_find(lk_name) != PDE_OK) return 0;
        rc = ob_value();
        if (rc) return 0;
        if (ov.kind == PDK_DICT) {              /* << /D [page ...] >> */
            if (ob_dict_find("D") != PDE_OK || ob_value()) return 0;
        }
    }
    i = dest_page();
    return i == 0xFFFF ? 0 : (pdw)(i + 1);
}

static void lk_fill(struct pdxb_annot *a)
{
    static struct pd_u32 ap;
    pdw rc, k;

    a->state = 0;
    rc = od_key("Dest");
    if (rc == PDE_OK) {
        a->state = lk_dest(a);
        return;
    }
    if (rc != PDE_NOTFOUND || od_key("A") != PDE_OK) return;
    if (ov.kind == PDK_REF && ob_follow() != PDE_OK) return;
    if (ov.kind != PDK_DICT) return;
    lx_tell(&ap);
    if (ob_dict_find("S") != PDE_OK || ob_value() || ov.kind != PDK_NAME) return;
    for (k = 0; k < 23 && k < ov.len; k++) a->nm[k] = (char)ov.text[k];
    a->nm[k] = 0;
    if (str_eq((pdb *)a->nm, "GoTo")) {
        lx_seek(&ap);
        if (ob_dict_find("D") == PDE_OK && ob_value() == PDE_OK)
            a->state = lk_dest(a);
    } else if (str_eq((pdb *)a->nm, "URI")) {
        lx_seek(&ap);
        if (ob_dict_find("URI") == PDE_OK && ob_value() == PDE_OK
            && ov.kind == PDK_STR) {
            a->contents_len = ov.len;
            to_latin(a->contents, 160, ov.text, strn());
        }
    }
}
