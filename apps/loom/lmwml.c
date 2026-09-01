/* ============================================================================
 * os8088 - apps/loom/lmwml.c
 *
 * THE WML COMPILER (WEAVE-SPEC 3), LOOM.OVL's first tenant (WEAVE-SPEC 1.2).
 *
 * It reads the project's MAIN.WML and fills the model apps/loom/lmwrite.c
 * writes out: the component rows, the property records, the cards, the menus,
 * the event bindings, and - in WEAVE-SPEC 2.14 rule 3a's pinned order - the
 * atom pool.
 *
 * ---------------------------------------------------------------------------
 * IT SCANS AND ANALYSES IN ONE PASS, AND THERE IS NO TREE
 * ---------------------------------------------------------------------------
 * tools/weavesim.py builds a Node tree and walks it, because on a host that
 * is free. Here a tree would be a second allocator over the scratch claim for
 * a structure read exactly once, so this is a recursive descent that analyses
 * each element as it goes. The ORDER the two produce must be identical or the
 * atom pool differs and WEAVE-SPEC 11.1's byte-identity gate fails, so the
 * rule is written at every site:
 *
 *   the attributes that INTERN do so at the element's OPEN, in WEAVE-SPEC
 *   3.3's table order; children and text content follow in document order;
 *   the content atom is interned at the element's CLOSE.
 *
 * That is exactly what Analyzer.component() produces - common(), then
 * el_<tag>(), whose body interns its own attributes before it reaches
 * text_prop(). Element by element: label/text/button/check intern only their
 * content; radio interns `group` and then its content; input interns `text`
 * and has no content; list interns nothing of its own and then each item's
 * content in child order; menu interns its title and then each item's; every
 * other element interns nothing at all. Checked against that reading, because
 * "it is the same order" is the claim the whole gate rests on.
 *
 * ---------------------------------------------------------------------------
 * ONE DIVERGENCE, STATED RATHER THAN HIDDEN
 * ---------------------------------------------------------------------------
 * weavesim scans a whole element - children included - before analysing any
 * of it, so in a document with TWO faults, one structural and one semantic,
 * its scanner speaks first. This one analyses as it goes, so a semantic fault
 * inside an element that is ALSO never closed is reported before the missing
 * close tag. Both refuse; they name different lines. A single-fault document -
 * which is what tests/weave/packerr/ holds and what an author actually types -
 * refuses identically, sentence for sentence, and that is the property
 * WEAVE-SPEC 10.5 asks for.
 * ==========================================================================*/

/* --- the model, defined here and declared in apps/loom/loom.h ------------ */
char     lm_appname[16];
int      lm_vmkb = 0;
int      lm_entrycard = 0;
int      lm_ncard = 0;
int      lm_ncomp = 0;
int      lm_nprop = 0;
int      lm_nblob = 0;
int      lm_nrec = 0;
int      lm_nfunc = 0;
int      lm_nglob = 0;
int      lm_ncell = 0;
int      lm_nform = 0;
int      lm_nspr = 0;
int      lm_nspr_art = 0;
int      lm_nmenu = 0;
int      lm_startfn = 0;
int      lm_hasgrid = 0;
int      lm_hascanvas = 0;
int      lm_hasscript = 0;
int      lm_gridcols = 0;
int      lm_gridrows = 0;
int      lm_canvw = 0;
int      lm_canvh = 0;
int      lm_canvspr = 0;
int      lm_flags = 0;
int      lm_used = 0;
char     lm_scriptsrc[13];
int      lm_scriptline = 0;

unsigned char lm_mtitle[LM_MAXMENU];
unsigned char lm_mcount[LM_MAXMENU];
unsigned char lm_mlabel[LM_MAXMENU * LM_MAXITEM];
unsigned      lm_mfnoff[LM_MAXMENU * LM_MAXITEM];
unsigned char lm_mfnlen[LM_MAXMENU * LM_MAXITEM];
int           lm_mline[LM_MAXMENU * LM_MAXITEM];

unsigned char lm_evcomp[LM_MAXEV];
unsigned char lm_evatom[LM_MAXEV];
unsigned      lm_evoff[LM_MAXEV];
unsigned char lm_evlen[LM_MAXEV];
int           lm_evline[LM_MAXEV];
int           lm_nev = 0;

static int ovl_elem_index(unsigned off, unsigned len);

/* --- the scanner's cursor ------------------------------------------------ */
static unsigned lmw_i;
static int      lmw_line;

/* One element's attributes. A single table is enough because every element
 * CONSUMES its attributes before it parses a child - the only nesting that
 * exists (app > card > canvas > sprite, app > card > list > item, app > menu >
 * item) never needs two live at once. */
#define LMW_MAXATTR  16
#define LMW_VALBUF  640             /* the folded values, packed. 3.3's
                                     * longest legal value is an <input>'s
                                     * text= at 255 and no element carries
                                     * two of those */
static unsigned      lmw_anoff[LMW_MAXATTR];    /* the NAME, in the source */
static unsigned char lmw_anlen[LMW_MAXATTR];
static unsigned      lmw_avoff[LMW_MAXATTR];    /* the VALUE, in lmw_valbuf */
static unsigned char lmw_avlen[LMW_MAXATTR];
static int           lmw_aline[LMW_MAXATTR];
static int           lmw_nattr;
static char          lmw_valbuf[LMW_VALBUF];
static int           lmw_valn;
static int           lmw_selfclose;
static unsigned      lmw_tagoff;
static unsigned      lmw_taglen;
static int           lmw_tagline;

/* The card ids and the component ids are SOURCE SPANS and never atoms:
 * interning one would put a string in the pool that weavesim's pool does not
 * have, and the ATOMS section is compared byte for byte. The WML text is
 * resident in the project's source slot for the whole of the pack, so a span
 * costs four bytes and stays valid. */
static unsigned      lmw_cardoff[LM_MAXCARD];
static unsigned char lmw_cardlen[LM_MAXCARD];

/* Event-handler NAMES, menu oncommand names, sprite image names and 2.6.1's
 * list-item blobs all outlive lmw_valbuf, which the next element overwrites,
 * so they are copied into LMW_NAMES the moment they are taken. */
static int lmw_namen;

/* The message scratch. ONE buffer: only one message is ever built, lm_perr()
 * keeps the first, and SPEC.md 73.5 forbids a 160-byte automatic. */
static char lmw_msg[LM_ERRMAX];

/* ==========================================================================
 * 3.2/3.3's CLOSED TABLES
 *
 * One string a row, in WEAVE-SPEC 3.3's TABLE ORDER, because that order is
 * the interning order (2.14 rule 3a) and a reader has to be able to check the
 * two against each other.
 * ========================================================================*/

static const char *lmw_common[] = {
    "id", "w", "h", "style", "align", "br", "hidden", "disabled", 0
};
/* SPELLED ONCE, and the reason is SPEC.md 73.14 rather than tidiness: a
 * string literal an ovl_ function names stays RESIDENT even though its code
 * does not, and SmallerC emits one per SITE.  This sentence has five sites -
 * every place a tag that is not in 3.2's inventory can appear - and five
 * copies of it were 256 resident bytes against a package that closed wave 7
 * with 262 spare.  The same cut apps/weave/wval.c took (WEAVE-PLAN 5.3). */
static const char lm_s_noelem[] =
    ">: not a Weave element; the inventory is closed (WEAVE-SPEC 3.2)";

static const char *lmw_a_app[]    = { "name", "vm", 0 };
static const char *lmw_a_card[]   = { "id", 0 };
static const char *lmw_a_none[]   = { 0 };
static const char *lmw_a_meter[]  = { "value", "max", 0 };
static const char *lmw_a_button[] = { "onclick", 0 };
static const char *lmw_a_check[]  = { "checked", "onchange", 0 };
static const char *lmw_a_radio[]  = { "group", "checked", "onchange", 0 };
static const char *lmw_a_input[]  = { "cols", "text", "onchange", "onkey", 0 };
static const char *lmw_a_list[]   = { "rows", "onselect", 0 };
static const char *lmw_a_grid[]   = { "cols", "rows", "onselect", "onedit",
                                      "oncalc", 0 };
static const char *lmw_a_canvas[] = { "w", "h", "ink", "paper", "walls",
                                      "tick", "onkey",
                                      "oncollide", "onwall", "onscore",
                                      "ontick", 0 };
static const char *lmw_a_sprite[] = { "id", "img", "x", "y", "shown",
                                      "color", 0 };

static const char *lmw_a_menu[]   = { "title", 0 };
static const char *lmw_a_item[]   = { "oncommand", 0 };
static const char *lmw_a_script[] = { "src", 0 };

/* The inventory, in ctype order so the index IS the ctype for the fourteen
 * that have one (2.5.1); app, card, menu, item and script follow. */
#define LMW_NELEM 19
#define LMW_E_LABEL   0
#define LMW_E_TEXT    1
#define LMW_E_RULE    2
#define LMW_E_BOX     3
#define LMW_E_SPACER  4
#define LMW_E_METER   5
#define LMW_E_BUTTON  6
#define LMW_E_CHECK   7
#define LMW_E_RADIO   8
#define LMW_E_INPUT   9
#define LMW_E_LIST   10
#define LMW_E_GRID   11
#define LMW_E_CANVAS 12
#define LMW_E_SPRITE 13
#define LMW_E_APP    14
#define LMW_E_CARD   15
#define LMW_E_MENU   16
#define LMW_E_ITEM   17
#define LMW_E_SCRIPT 18

static const char *lmw_elem[LMW_NELEM] = {
    "label", "text", "rule", "box", "spacer", "meter", "button", "check",
    "radio", "input", "list", "grid", "canvas", "sprite",
    "app", "card", "menu", "item", "script"
};
static const char **lmw_attrs[LMW_NELEM] = {
    lmw_a_none, lmw_a_none, lmw_a_none, lmw_a_none, lmw_a_none,
    lmw_a_meter, lmw_a_button, lmw_a_check, lmw_a_radio, lmw_a_input,
    lmw_a_list, lmw_a_grid, lmw_a_canvas, lmw_a_sprite,
    lmw_a_app, lmw_a_card, lmw_a_menu, lmw_a_item, lmw_a_script
};

/* ==========================================================================
 * MESSAGE BUILDING - one buffer, five appenders, and every sentence below is
 * the text tools/weavesim.py prints (WEAVE-SPEC 10.5, tests/weave/packerr/).
 * ========================================================================*/

static void ovl_m0(const char *s)
{
    lmw_msg[0] = 0;
    lm_cat(lmw_msg, s);
}

static void ovl_mc(const char *s)
{
    lm_cat(lmw_msg, s);
}

static void ovl_mn(int v)
{
    lm_catn(lmw_msg, v);
}

/* ...and its lowercasing twin, for the names 3.1 folds. */
static void ovl_msrcl(unsigned off, unsigned len)
{
    unsigned n = os88_strlen(lmw_msg);
    unsigned k;

    for (k = 0; k < len && n + 1 < LM_ERRMAX; k++) {
        lmw_msg[n] = (char) lm_lower((int) lm_sb(LM_SLOT_WML, off + k));
        n++;
    }
    lmw_msg[n] = 0;
}

static void ovl_msrc(unsigned off, unsigned len)
{
    unsigned n = os88_strlen(lmw_msg);
    unsigned k;

    for (k = 0; k < len && n + 1 < LM_ERRMAX; k++) {
        lmw_msg[n] = (char) lm_sb(LM_SLOT_WML, off + k);
        n++;
    }
    lmw_msg[n] = 0;
}

static void ovl_mv(int ai)
{
    unsigned n = os88_strlen(lmw_msg);
    unsigned k;

    for (k = 0; k < lmw_avlen[ai] && n + 1 < LM_ERRMAX; k++) {
        lmw_msg[n] = lmw_valbuf[lmw_avoff[ai] + k];
        n++;
    }
    lmw_msg[n] = 0;
}

static void ovl_mtag(void)
{
    ovl_msrcl(lmw_tagoff, lmw_taglen);
}

static void ovl_raise(int line)
{
    lm_perr(LM_SLOT_WML, line, lmw_msg);
}

/* ==========================================================================
 * THE SCANNER
 * ========================================================================*/

static unsigned lmw_at(unsigned k)
{
    return lm_sb(LM_SLOT_WML, k);
}

static int lmw_eof(void)
{
    return lmw_i >= lm_srclen(LM_SLOT_WML);
}

static void lmw_adv(unsigned n)
{
    unsigned k;

    for (k = 0; k < n; k++) {
        if (lmw_at(lmw_i) == '\n')
            lmw_line++;
        lmw_i++;
    }
}

static int lmw_lit(const char *s)
{
    unsigned k = 0;

    while (s[k] != 0) {
        if (lmw_at(lmw_i + k) != (unsigned) (unsigned char) s[k])
            return 0;
        k++;
    }
    return 1;
}

static void lmw_ws(void)
{
    while (!lmw_eof() && lm_isspace((int) lmw_at(lmw_i)))
        lmw_adv(1);
}

static int ovl_wscomment(void)
{
    unsigned k;
    unsigned n = lm_srclen(LM_SLOT_WML);

    for (;;) {
        lmw_ws();
        if (!lmw_lit("<!--"))
            return 1;
        k = lmw_i + 4;
        for (;;) {
            if (k + 2 >= n + 1) {
                ovl_m0("unterminated comment");
                ovl_raise(lmw_line);
                return 0;
            }
            if (lm_sb(LM_SLOT_WML, k) == '-'
                && lm_sb(LM_SLOT_WML, k + 1) == '-'
                && lm_sb(LM_SLOT_WML, k + 2) == '>')
                break;
            k++;
            if (k >= n) {
                ovl_m0("unterminated comment");
                ovl_raise(lmw_line);
                return 0;
            }
        }
        lmw_adv(k + 3 - lmw_i);
    }
}

/* 3.1's four entities, and nothing else. The cursor goes IN and comes back
 * OUT through lmw_ek, and the character through lmw_ec, for ovl_intattr's
 * reason: SS != DS, so the address of an automatic is not a thing this
 * package may take (SPEC.md 73.5). */
static unsigned lmw_ek;
static int      lmw_ec;

static int ovl_entity(void)
{
    unsigned p = lmw_ek + 1;
    unsigned e = p;
    unsigned n = lm_srclen(LM_SLOT_WML);

    while (e < n && e < p + 5 && lm_sb(LM_SLOT_WML, e) != ';')
        e++;
    if (e < n && lm_sb(LM_SLOT_WML, e) == ';') {
        unsigned len = e - p;
        int c = 0;

        if (lm_srceq(LM_SLOT_WML, p, len, "lt"))
            c = '<';
        else if (lm_srceq(LM_SLOT_WML, p, len, "gt"))
            c = '>';
        else if (lm_srceq(LM_SLOT_WML, p, len, "amp"))
            c = '&';
        else if (lm_srceq(LM_SLOT_WML, p, len, "quot"))
            c = '"';
        if (c) {
            lmw_ec = c;
            lmw_ek = e + 1;
            return 1;
        }
    }
    return 0;
}

static void ovl_badentity(int line, unsigned k)
{
    unsigned p = k + 1;
    unsigned e = p;
    unsigned n = lm_srclen(LM_SLOT_WML);

    while (e < n && e < p + 5 && lm_sb(LM_SLOT_WML, e) != ';')
        e++;
    if (!(e < n && lm_sb(LM_SLOT_WML, e) == ';'))
        e = p;                      /* no ';': weavesim's body is "" */
    ovl_m0("&");
    ovl_msrc(p, e - p);
    ovl_mc(": not one of &lt; &gt; &amp; &quot; - the entity set is closed "
           "(WEAVE-SPEC 3.1)");
    ovl_raise(line);
}

/* ovl_opentag - `<name attrs>` or `<name attrs/>`. On return the attribute
 * table holds this element's attributes with entities expanded and values
 * folded (3.1), lmw_selfclose says how the tag ended, and the cursor stands
 * after it. */
static int ovl_opentag(void)
{
    unsigned m;

    lmw_tagline = lmw_line;
    if (!lmw_lit("<")) {
        ovl_m0("expected '<'");
        ovl_raise(lmw_line);
        return 0;
    }
    lmw_adv(1);
    if (!lm_isalpha((int) lmw_at(lmw_i))) {
        ovl_m0("expected an element name after '<'");
        ovl_raise(lmw_line);
        return 0;
    }
    lmw_tagoff = lmw_i;
    m = lmw_i;
    while (lm_isalnum((int) lmw_at(m)))
        m++;
    lmw_taglen = m - lmw_i;
    lmw_adv(lmw_taglen);

    lmw_nattr = 0;
    lmw_valn = 0;
    lmw_selfclose = 0;
    for (;;) {
        unsigned noff, nlen;
        int aline;
        int i;

        lmw_ws();
        if (lmw_lit("/>")) {
            lmw_adv(2);
            lmw_selfclose = 1;
            return 1;
        }
        if (lmw_lit(">")) {
            lmw_adv(1);
            return 1;
        }
        if (!lm_isalpha((int) lmw_at(lmw_i))) {
            ovl_m0("expected an attribute name in <");
            ovl_mtag();
            ovl_mc(">");
            ovl_raise(lmw_line);
            return 0;
        }
        noff = lmw_i;
        m = lmw_i;
        while (lm_isalnum((int) lmw_at(m)))
            m++;
        nlen = m - lmw_i;
        aline = lmw_line;
        lmw_adv(nlen);
        if (lmw_nattr >= LMW_MAXATTR) {
            ovl_m0("more than 16 attributes on one element "
                   "(WEAVE-SPEC 11.4)");
            ovl_raise(aline);
            return 0;
        }
        /* 3.1: an attribute written twice is a pack error. Checked BEFORE the
         * value is read, which is where weavesim checks it (the dict store is
         * after the value, but the value cannot change the answer). */
        for (i = 0; i < lmw_nattr; i++) {
            if (lmw_anlen[i] == (unsigned char) nlen) {
                unsigned j = 0;
                while (j < nlen
                       && lm_lower((int) lmw_at(lmw_anoff[i] + j))
                          == lm_lower((int) lmw_at(noff + j)))
                    j++;
                if (j == nlen) {
                    ovl_m0("");
                    ovl_mtag();
                    ovl_mc(": attribute \"");
                    ovl_msrcl(noff, nlen);
                    ovl_mc("\" written twice");
                    ovl_raise(aline);
                    return 0;
                }
            }
        }
        lmw_anoff[lmw_nattr] = noff;
        lmw_anlen[lmw_nattr] = (unsigned char) nlen;
        lmw_aline[lmw_nattr] = aline;
        lmw_avoff[lmw_nattr] = (unsigned) lmw_valn;
        if (lmw_lit("=")) {
            unsigned k;
            lmw_adv(1);
            if (!lmw_lit("\"")) {
                ovl_m0("attribute values are double-quoted (WEAVE-SPEC 3.1)");
                ovl_raise(lmw_line);
                return 0;
            }
            lmw_adv(1);
            k = lmw_i;
            for (;;) {
                int ch;
                if (k >= lm_srclen(LM_SLOT_WML)) {
                    ovl_m0("unterminated attribute value");
                    ovl_raise(lmw_line);
                    return 0;
                }
                if (lm_sb(LM_SLOT_WML, k) == '"')
                    break;
                if (lm_sb(LM_SLOT_WML, k) == '&') {
                    lmw_ek = k;
                    if (!ovl_entity()) {
                        ovl_badentity(aline, k);
                        return 0;
                    }
                    k = lmw_ek;
                    ch = lmw_ec;
                } else {
                    ch = (int) lm_sb(LM_SLOT_WML, k);
                    k++;
                }
                ch = lm_fold(ch);
                if (ch >= 0) {
                    if (lmw_valn >= LMW_VALBUF) {
                        ovl_m0("the attribute values on one element are over "
                               "640 bytes (WEAVE-SPEC 11.4)");
                        ovl_raise(aline);
                        return 0;
                    }
                    lmw_valbuf[lmw_valn] = (char) ch;
                    lmw_valn++;
                }
            }
            lmw_adv(k + 1 - lmw_i);
        } else {
            if (lmw_valn < LMW_VALBUF) {
                lmw_valbuf[lmw_valn] = '1';     /* the bare boolean (3.1) */
                lmw_valn++;
            }
        }
        lmw_avlen[lmw_nattr] =
            (unsigned char) ((unsigned) lmw_valn - lmw_avoff[lmw_nattr]);
        lmw_nattr++;
    }
}

/* ovl_closetag - `</name>`, and its two sentences. */
static int ovl_closetag(const char *name)
{
    int cl = lmw_line;
    unsigned m;

    lmw_adv(2);
    m = lmw_i;
    while (lm_isalnum((int) lmw_at(m)))
        m++;
    if (!lm_srceqi(LM_SLOT_WML, lmw_i, m - lmw_i, name)) {
        ovl_m0("</");
        /* RAW, not folded: the model quotes the close tag's own spelling here
         * (`m.group(0)`) while the element it failed to close is the folded
         * name. It is the one place in this file a name is not lowercased,
         * and a fuzz run against tools/weavesim.py is what found it. */
        ovl_msrc(lmw_i, m - lmw_i);
        ovl_mc("> closes <");
        ovl_mc(name);
        ovl_mc("> - tags must nest (WEAVE-SPEC 3.1)");
        ovl_raise(cl);
        return 0;
    }
    lmw_adv(m - lmw_i);
    lmw_ws();
    if (!lmw_lit(">")) {
        ovl_m0("expected '>'");
        ovl_raise(lmw_line);
        return 0;
    }
    lmw_adv(1);
    return 1;
}

static void ovl_neverclosed(const char *name, int openline)
{
    ovl_m0("<");
    ovl_mc(name);
    ovl_mc("> is never closed");
    ovl_raise(openline);
}

/* ==========================================================================
 * ATTRIBUTES
 * ========================================================================*/

static int ovl_afind(const char *name)
{
    int i;

    for (i = 0; i < lmw_nattr; i++) {
        if (lm_srceqi(LM_SLOT_WML, lmw_anoff[i], lmw_anlen[i], name))
            return i;
    }
    return -1;
}

static int ovl_aveq(int ai, const char *s)
{
    unsigned k = 0;

    while (s[k] != 0) {
        if (k >= lmw_avlen[ai] || lmw_valbuf[lmw_avoff[ai] + k] != s[k])
            return 0;
        k++;
    }
    return k == lmw_avlen[ai];
}

/* _int_attr (weavesim's), and its two sentences verbatim. Python's int()
 * takes an optional sign and digits; anything else is "not a number".
 *
 * THE ANSWER COMES BACK IN A FILE-SCOPE WORD AND NOT THROUGH A POINTER.
 * SS != DS here, so `&ok` on an automatic is a stack offset dereferenced
 * through the package segment: it assembles cleanly and writes into somebody
 * else's data, and tools/cc8086.py fails the build over it (SPEC.md 73.5).
 * apps/weave/weave.c's rule is the fix and it is used everywhere below - an
 * out-parameter is a static.
 *
 * THERE IS NO `long` EITHER (SPEC.md 73.7), and SmallerC does not merely
 * refuse the type: it refuses the TOKEN. So the accumulator is an `unsigned`
 * that STOPS at a value no bound can accept rather than growing past 16 bits -
 * the range test only has to answer "outside", and the message quotes the
 * TEXT that was written rather than the number it parsed to. */
static int lmw_ok;
static int lmw_intover;

static int ovl_intattr(const char *tag, const char *attr, int ai, int lo,
                       int hi, int sgn)
{
    unsigned k = 0;
    int neg = 0;
    unsigned v = 0;
    int val;
    unsigned n = lmw_avlen[ai];
    const char *p = lmw_valbuf + lmw_avoff[ai];

    lmw_ok = 0;
    lmw_intover = 0;
    if (n > 0 && (p[0] == '-' || p[0] == '+')) {
        neg = (p[0] == '-');
        k = 1;
    }
    if (k >= n)
        goto notnum;
    for (; k < n; k++) {
        if (!lm_isdigit((int) (unsigned char) p[k]))
            goto notnum;
        if (v > 3276)
            lmw_intover = 1;        /* past anything 3.3 bounds; see above */
        else
            v = v * 10 + (unsigned) (p[k] - '0');
    }
    val = (int) v;
    if (neg)
        val = -val;
    if (lmw_intover || (val < 0 && !sgn) || val < lo || val > hi) {
        ovl_m0(tag);
        ovl_mc(": ");
        ovl_mc(attr);
        ovl_mc("=\"");
        ovl_mv(ai);
        ovl_mc("\" is outside ");
        ovl_mn(lo);
        ovl_mc("..");
        ovl_mn(hi);
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    lmw_ok = 1;
    return val;

notnum:
    ovl_m0(tag);
    ovl_mc(": ");
    ovl_mc(attr);
    ovl_mc("=\"");
    ovl_mv(ai);
    ovl_mc("\" is not a number");
    ovl_raise(lmw_aline[ai]);
    return 0;
}

static int ovl_boolattr(const char *tag, const char *attr, int ai)
{
    lmw_ok = 1;
    if (ovl_aveq(ai, "1"))
        return 1;
    if (ovl_aveq(ai, "0"))
        return 0;
    lmw_ok = 0;
    ovl_m0(tag);
    ovl_mc(": ");
    ovl_mc(attr);
    ovl_mc("=\"");
    ovl_mv(ai);
    ovl_mc("\" is not a boolean - bare or \"1\"/\"0\" (WEAVE-SPEC 3.1)");
    ovl_raise(lmw_aline[ai]);
    return 0;
}

/* 3.1: "Element and attribute names are case-insensitive, folded to lowercase
 * by the parser." Every comparison on a NAME in this file goes through
 * lm_srceqi, and every message that quotes one lowercases it (ovl_msrcl) -
 * which is what "folded by the parser" looks like where a reader can see it.
 * Attribute VALUES keep their case (3.1 again), so `style="Bold"` is still a
 * pack error and `id="Who"` still differs from `id="who"`.
 *
 * -1 = not one of the eighteen. */
static int ovl_elem_index(unsigned off, unsigned len)
{
    int i;

    for (i = 0; i < LMW_NELEM; i++) {
        if (lm_srceqi(LM_SLOT_WML, off, len, lmw_elem[i]))
            return i;
    }
    return -1;
}

/* 3.4/10.5's unknown-attribute voice, in weavesim's own branch order: an
 * `on*` name first (a known event on the wrong element, else the hover
 * sentence), then a colour name, then the general one.
 *
 * WEAVE-SPEC 10.5's own illustration of the third sentence is `color`, which
 * this branch order can never reach - the colour test comes first. The
 * example is wrong in the spec rather than here; it is amended in this wave
 * and named in the pull request. */
static void ovl_badattr(int ei, int ai)
{
    static const char *known_ev[] = { "onclick", "onchange", "onkey",
                                      "onselect", "onedit", "oncalc",
                                      "oncollide", "onwall", "onscore",
                                      "ontick", "oncommand", "ontimer",
                                      "onalert", 0 };
    static const char *colourish[] = { "fg", "bg", "font", "size", 0 };
    unsigned off = lmw_anoff[ai];
    unsigned len = lmw_anlen[ai];
    int k;

    if (len >= 2 && lm_srceqi(LM_SLOT_WML, off, 2, "on")) {
        int isev = 0;
        int hov = lm_srceqi(LM_SLOT_WML, off, len, "onhover")
            || lm_srceqi(LM_SLOT_WML, off, len, "onmouseover")
            || lm_srceqi(LM_SLOT_WML, off, len, "onmouseout")
            || lm_srceqi(LM_SLOT_WML, off, len, "onmousemove");
        for (k = 0; known_ev[k]; k++)
            if (lm_srceqi(LM_SLOT_WML, off, len, known_ev[k]))
                isev = 1;
        if (isev && !hov) {
            ovl_m0(lmw_elem[ei]);
            ovl_mc(": no event \"");
            ovl_msrcl(off, len);
            ovl_mc("\" exists on it (WEAVE-SPEC 3.3)");
            ovl_raise(lmw_aline[ai]);
            return;
        }
        ovl_m0("");
        ovl_msrcl(off, len);
        ovl_mc(": no hover exists; pointer movement reaches a package only "
               "between press and release (SPEC.md 13.7)");
        ovl_raise(lmw_aline[ai]);
        return;
    }
    for (k = 0; k + 5 <= (int) len; k++) {
        if (lm_srceqi(LM_SLOT_WML, off + (unsigned) k, 5, "color"))
            goto colour;
    }
    for (k = 0; colourish[k]; k++)
        if (lm_srceqi(LM_SLOT_WML, off, len, colourish[k]))
            goto colour;
    ovl_m0(lmw_elem[ei]);
    ovl_mc(": no such attribute \"");
    ovl_msrcl(off, len);
    ovl_mc("\"; style is bold/invert/align only - two of three adapters "
           "are 1bpp");
    ovl_raise(lmw_aline[ai]);
    return;

colour:
    ovl_m0("");
    ovl_msrcl(off, len);
    ovl_mc(": no color here; a palette is a canvas's (WEAVE-SPEC 9.2.1) - "
           "grey rounds to black on 1bpp (SPEC.md 39.4)");
    ovl_raise(lmw_aline[ai]);
}

/* 3.3: "Any attribute not in these tables - on any element - refuses at
 * pack", checked in SOURCE order so the first one written wins. */
static int ovl_checkattrs(int ei)
{
    int i, k;
    int flow = (ei <= LMW_E_CANVAS);        /* FLOW = every ctype but sprite */
    const char **tab = lmw_attrs[ei];

    for (i = 0; i < lmw_nattr; i++) {
        int legal = 0;

        if (flow) {
            for (k = 0; lmw_common[k]; k++)
                if (lm_srceqi(LM_SLOT_WML, lmw_anoff[i], lmw_anlen[i],
                              lmw_common[k]))
                    legal = 1;
        }
        for (k = 0; tab[k]; k++)
            if (lm_srceqi(LM_SLOT_WML, lmw_anoff[i], lmw_anlen[i], tab[k]))
                legal = 1;
        if (!legal) {
            ovl_badattr(ei, i);
            return 0;
        }
    }
    return 1;
}

/* ==========================================================================
 * THE MODEL'S ROWS
 * ========================================================================*/

static unsigned ovl_crow(int i)
{
    return LMW_COMPS + (unsigned) i * LM_COMPSZ;
}

static void ovl_cset(int i, int field, int v)
{
    lm_wpb(ovl_crow(i) + (unsigned) field, (unsigned) v);
}

static int ovl_cget(int i, int field)
{
    return (int) lm_wb(ovl_crow(i) + (unsigned) field);
}

/* 2.14 rule 5: records inside a block sort by ascending name atom id. They
 * are written in emission order and sorted once, at the close of the
 * component, by an insertion sort over the dozen rows a component ever has. */
static int ovl_prop(int comp, int atom, int kind, int val)
{
    unsigned r;

    if (lm_nprop >= LM_MAXPROP) {
        ovl_m0("too many property records for one pack; this machine "
               "stages 1024 (WEAVE-SPEC 11.4)");
        ovl_raise((int) lm_ww(ovl_crow(comp) + LMC_LINE));
        return 0;
    }
    r = LMW_PROPS + (unsigned) lm_nprop * LM_PROPSZ;
    lm_wpb(r + LMP_ATOM, (unsigned) atom);
    lm_wpb(r + LMP_KIND, (unsigned) kind);
    lm_wpw(r + LMP_VAL, (unsigned) val & 0xFFFF);
    lm_nprop++;
    ovl_cset(comp, LMC_NPROP, ovl_cget(comp, LMC_NPROP) + 1);
    return 1;
}

void ovl_psort(int comp)
{
    int first = (int) lm_ww(ovl_crow(comp) + LMC_PROP);
    int n = ovl_cget(comp, LMC_NPROP);
    int i, j;

    for (i = 1; i < n; i++) {
        for (j = i; j > 0; j--) {
            unsigned a = LMW_PROPS + (unsigned) (first + j - 1) * LM_PROPSZ;
            unsigned b = LMW_PROPS + (unsigned) (first + j) * LM_PROPSZ;
            unsigned ka, kb, va;

            if (lm_wb(a + LMP_ATOM) <= lm_wb(b + LMP_ATOM))
                break;
            ka = lm_wb(a + LMP_ATOM);
            kb = lm_wb(a + LMP_KIND);
            va = lm_ww(a + LMP_VAL);
            lm_wpb(a + LMP_ATOM, lm_wb(b + LMP_ATOM));
            lm_wpb(a + LMP_KIND, lm_wb(b + LMP_KIND));
            lm_wpw(a + LMP_VAL, lm_ww(b + LMP_VAL));
            lm_wpb(b + LMP_ATOM, ka);
            lm_wpb(b + LMP_KIND, kb);
            lm_wpw(b + LMP_VAL, va);
        }
    }
}

/* ==========================================================================
 * TEXT CONTENT
 *
 * Node.content(): every non-blank run folded and entity-expanded, joined with
 * a single space, then interior whitespace collapsed and the ends trimmed
 * (3.1). It accumulates into lm_sbuf as the content is scanned, so an
 * element's text is ready the moment its close tag arrives.
 * ========================================================================*/

static int lmw_hastext;
static int lmw_textline;
static int runline;      /* where the text run being scanned STARTED */

static int ovl_text_run(unsigned from, unsigned to, int line)
{
    unsigned k;
    int any = 0;

    for (k = from; k < to; k++)
        if (!lm_isspace((int) lm_sb(LM_SLOT_WML, k)))
            any = 1;
    if (!any)
        return 1;                   /* weavesim records only non-blank runs */
    if (!lmw_hastext) {
        lmw_hastext = 1;
        lmw_textline = line;
    } else {
        lm_sbputc(' ');             /* the " ".join of two runs */
    }
    for (k = from; k < to; k++) {
        int ch = (int) lm_sb(LM_SLOT_WML, k);
        if (ch == '&') {
            lmw_ek = k;
            if (!ovl_entity()) {
                ovl_badentity(line, k);
                return 0;
            }
            k = lmw_ek - 1;
            ch = lmw_ec;
        }
        ch = lm_fold(ch);
        if (ch >= 0)
            lm_sbputc(ch);
    }
    return 1;
}

static void ovl_collapse(void)
{
    int r = 0, w = 0, sp = 0;

    while (r < lm_sbn) {
        int c = (int) (unsigned char) lm_sbuf[r];
        if (c == ' ') {
            if (!sp) {
                lm_sbuf[w] = ' ';
                w++;
            }
            sp = 1;
        } else {
            lm_sbuf[w] = (char) c;
            w++;
            sp = 0;
        }
        r++;
    }
    lm_sbn = w;
    lm_sbuf[w] = 0;
    lm_sbtrim();
}

/* ==========================================================================
 * EVENTS AND NAMES
 * ========================================================================*/

static unsigned ovl_nameput(unsigned off, unsigned len)
{
    unsigned at = (unsigned) lmw_namen;
    unsigned k;

    if (at + len + 1 > LM_NAMEMAX)
        return 0xFFFF;
    for (k = 0; k < len; k++)
        lm_wpb(LMW_NAMES + at + k,
               (unsigned) (unsigned char) lmw_valbuf[off + k]);
    lm_wpb(LMW_NAMES + at + len, 0);
    lmw_namen = (int) (at + len + 1);
    return at;
}

/* An event binding is kept as a NAME until the script exists to resolve it
 * against - pack_project()'s own order, and the reason is that a function
 * index does not exist while the WML is being read.
 *
 * THE RECORD IS EMITTED NOW ALL THE SAME, with the event's index where its
 * function index will go, and ovl_resolve() patches the value in place. That
 * matters: 2.14 rule 5 packs a component's block back to back with the next
 * one's, so a record appended to component 3 after component 4 exists has
 * nowhere to go. tools/weavesim.py appends and re-sorts because a Python list
 * can grow in the middle; a claim cannot, and this is the shape that answers
 * it. The same trick carries a <sprite>'s PK_SPRITE record (3.3). */
static int ovl_event(int comp, const char *name, int atom)
{
    int ai = ovl_afind(name);
    unsigned at;

    if (ai < 0)
        return 1;
    if (lm_nev >= LM_MAXEV) {
        ovl_m0("more than 128 event bindings in one app (WEAVE-SPEC 11.4)");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    at = ovl_nameput(lmw_avoff[ai], lmw_avlen[ai]);
    if (at == 0xFFFF) {
        ovl_m0("the handler names do not fit this machine's pack claim "
               "(WEAVE-SPEC 11.4)");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    lm_evcomp[lm_nev] = (unsigned char) comp;
    lm_evatom[lm_nev] = (unsigned char) atom;
    lm_evoff[lm_nev] = at;
    lm_evlen[lm_nev] = (unsigned char) lmw_avlen[ai];
    lm_evline[lm_nev] = lmw_aline[ai];
    if (!ovl_prop(comp, atom, PK_FUNC, lm_nev))
        return 0;
    lm_nev++;
    return 1;
}

/* ==========================================================================
 * THE ELEMENTS
 * ========================================================================*/

static int ovl_id_taken(unsigned off, unsigned len)
{
    int i;
    unsigned j;

    for (i = 0; i < lm_ncard; i++) {
        if (lmw_cardlen[i] == (unsigned char) len) {
            for (j = 0; j < len; j++)
                if (lm_sb(LM_SLOT_WML, lmw_cardoff[i] + j)
                    != (unsigned) (unsigned char) lmw_valbuf[off + j])
                    break;
            if (j == len)
                return 1;
        }
    }
    for (i = 0; i < lm_ncomp; i++) {
        unsigned coff = lm_ww(ovl_crow(i) + LMC_IDOFF);
        unsigned clen = (unsigned) ovl_cget(i, LMC_IDLEN);
        if (clen != len || coff == 0)
            continue;
        for (j = 0; j < len; j++)
            if (lm_sb(LM_SLOT_WML, coff + j)
                != (unsigned) (unsigned char) lmw_valbuf[off + j])
                break;
        if (j == len)
            return 1;
    }
    return 0;
}

/* The id is stored as a span of the WML text. lmw_valbuf holds the FOLDED
 * value, and an id is a name - `[A-Za-z0-9_]` after folding is itself - so the
 * span in the source and the folded value are the same bytes. The span is
 * what is kept, because lmw_valbuf does not survive the next element. */
static int ovl_setid(int comp, int ei)
{
    int ai = ovl_afind("id");

    if (ai < 0)
        return 1;
    if (ovl_id_taken(lmw_avoff[ai], lmw_avlen[ai])) {
        ovl_m0(lmw_elem[ei]);
        ovl_mc(": id \"");
        ovl_mv(ai);
        ovl_mc("\" is already taken");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    /* find the value's span in the source: it is the quoted run, which for a
     * name is byte for byte what the folded value holds */
    {
        unsigned k = lmw_anoff[ai] + lmw_anlen[ai];
        while (lm_sb(LM_SLOT_WML, k) != '"')
            k++;
        lm_wpw(ovl_crow(comp) + LMC_IDOFF, k + 1);
        ovl_cset(comp, LMC_IDLEN, (int) lmw_avlen[ai]);
    }
    return 1;
}

/* common() - the flow attributes every component but <sprite> carries. */
static int ovl_common(int comp, int ei)
{
    int ai;
    int style = 0;
    int cflags = 0;

    if (!ovl_setid(comp, ei))
        return 0;
    if (ei != LMW_E_CANVAS) {       /* a canvas's w/h are PIXELS (3.3) */
        ai = ovl_afind("w");
        if (ai >= 0) {
            int v = ovl_intattr(lmw_elem[ei], "w", ai, 0, 160, 0);
            if (!lmw_ok)
                return 0;
            ovl_cset(comp, LMC_W, v);
        }
        ai = ovl_afind("h");
        if (ai >= 0) {
            int v = ovl_intattr(lmw_elem[ei], "h", ai, 0, 40, 0);
            if (!lmw_ok)
                return 0;
            ovl_cset(comp, LMC_H, v);
        }
    }
    ai = ovl_afind("style");
    if (ai >= 0) {
        unsigned k = 0;
        unsigned n = lmw_avlen[ai];
        const char *p = lmw_valbuf + lmw_avoff[ai];
        while (k < n) {
            unsigned s;
            while (k < n && p[k] == ' ')
                k++;
            s = k;
            while (k < n && p[k] != ' ')
                k++;
            if (k == s)
                break;
            if (k - s == 4 && p[s] == 'b' && p[s + 1] == 'o'
                && p[s + 2] == 'l' && p[s + 3] == 'd') {
                style |= WS_BOLD;
            } else if (k - s == 6 && p[s] == 'i' && p[s + 1] == 'n'
                       && p[s + 2] == 'v' && p[s + 3] == 'e'
                       && p[s + 4] == 'r' && p[s + 5] == 't') {
                style |= WS_INVERT;
            } else {
                unsigned j;
                ovl_m0("style: no such style \"");
                {
                    unsigned m = os88_strlen(lmw_msg);
                    for (j = s; j < k && m + 1 < LM_ERRMAX; j++) {
                        lmw_msg[m] = p[j];
                        m++;
                    }
                    lmw_msg[m] = 0;
                }
                ovl_mc("\"; style is bold/invert/align only - two of three "
                       "adapters are 1bpp");
                ovl_raise(lmw_aline[ai]);
                return 0;
            }
        }
    }
    ai = ovl_afind("align");
    if (ai >= 0) {
        int a = -1;
        if (ovl_aveq(ai, "left"))
            a = 0;
        else if (ovl_aveq(ai, "center"))
            a = 1;
        else if (ovl_aveq(ai, "right"))
            a = 2;
        if (a < 0) {
            ovl_m0("align: no such alignment \"");
            ovl_mv(ai);
            ovl_mc("\"; left/center/right (WEAVE-SPEC 3.3)");
            ovl_raise(lmw_aline[ai]);
            return 0;
        }
        style |= a << 2;
    }
    ai = ovl_afind("br");
    if (ai >= 0) {
        if (ovl_boolattr(lmw_elem[ei], "br", ai))
            cflags |= CF_BREAK;
        if (!lmw_ok)
            return 0;
    }
    ai = ovl_afind("hidden");
    if (ai >= 0) {
        if (ovl_boolattr(lmw_elem[ei], "hidden", ai))
            cflags |= CF_HIDDEN;
        if (!lmw_ok)
            return 0;
    }
    ai = ovl_afind("disabled");
    if (ai >= 0) {
        if (ovl_boolattr(lmw_elem[ei], "disabled", ai))
            cflags |= CF_DISABLED;
        if (!lmw_ok)
            return 0;
    }
    ovl_cset(comp, LMC_STYLE, style);
    ovl_cset(comp, LMC_CFLAGS, cflags);
    return 1;
}

/* -1 = refused (the sentence is already set); else the component's index.
 * An out-parameter would be an `&local` at every call site. */
static int ovl_newcomp(int ei, int card)
{
    int i;
    unsigned r;

    if (lm_ncomp >= LM_MAXCOMP) {
        ovl_m0("251 components; comp_id is one byte, 1..250 "
               "(WEAVE-SPEC 2.5)");
        ovl_raise(lmw_tagline);
        return -1;
    }
    i = lm_ncomp;
    r = ovl_crow(i);
    lm_wfill(r, 0, LM_COMPSZ);
    lm_wpb(r + LMC_CTYPE, (unsigned) (ei + 1));     /* 2.5.1: index+1 */
    lm_wpb(r + LMC_ID, (unsigned) (i + 1));
    lm_wpb(r + LMC_CARD, (unsigned) card);
    lm_wpw(r + LMC_PROP, (unsigned) lm_nprop);
    lm_wpw(r + LMC_LINE, (unsigned) lmw_tagline);
    lm_ncomp++;
    return i;
}

static int ovl_component(int ei, int card);

/* --- <item> inside a <list> ---------------------------------------------- */
/* 2.6.1's items. The count and the array are file-scope for SPEC.md 73.5's
 * reason and because a <list> cannot nest inside another one, so one of each
 * is all there can ever be in flight. */
static int           lmw_nitem;
static unsigned char lmw_items[66];

static int ovl_list_item(void)
{
    int openline = lmw_tagline;
    int ai;

    if (!ovl_checkattrs(LMW_E_ITEM))
        return 0;
    ai = ovl_afind("oncommand");
    if (ai >= 0) {
        ovl_m0("item: oncommand is a menu item's; a list fires onselect "
               "(WEAVE-SPEC 3.3)");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    lm_sbclear();
    lmw_hastext = 0;
    if (!lmw_selfclose) {
        for (;;) {
            unsigned run;
            if (lmw_eof()) {
                ovl_neverclosed("item", openline);
                return 0;
            }
            if (lmw_lit("<!--")) {
                if (!ovl_wscomment())
                    return 0;
                continue;
            }
            if (lmw_lit("</"))
                break;
            if (lmw_lit("<")) {
                unsigned toff = lmw_i + 1;
                unsigned m = toff;
                while (lm_isalnum((int) lmw_at(m)))
                    m++;
                ovl_m0("<");
                ovl_msrcl(toff, m - toff);
                ovl_mc(">: <item> takes no children");
                ovl_raise(lmw_line);
                return 0;
            }
            run = lmw_i;
            runline = lmw_line;
            while (!lmw_eof() && lmw_at(lmw_i) != '<')
                lmw_adv(1);
            if (!ovl_text_run(run, lmw_i, runline))
                return 0;
        }
        if (!ovl_closetag("item"))
            return 0;
    }
    ovl_collapse();
    if (lm_sbn == 0) {
        ovl_m0("item: an empty list item");
        ovl_raise(openline);
        return 0;
    }
    if (lmw_nitem >= 65) {
        ovl_m0("");
        ovl_mn(lmw_nitem + 1);
        ovl_mc(" list items; the blob count byte caps at 64 "
               "(WEAVE-SPEC 2.6.1)");
        ovl_raise(lmw_tagline);
        return 0;
    }
    {
        int a = ovl_intern(LM_SLOT_WML, openline);
        if (a == 0)
            return 0;
        lmw_items[lmw_nitem] = (unsigned char) a;
        lmw_nitem++;
    }
    return 1;
}

/* WEAVE-SPEC 6.10.7's palette, in SPEC.md 3's order so the index IS the
 * colour. A name outside it is a pack error naming the list; numbers are not
 * accepted, because the reader of the WML is the person who has to know that
 * 4 is red. */
static const char *lmw_pal[16] = {
    "black", "blue", "green", "cyan", "red", "magenta", "brown", "lightgray",
    "darkgray", "lightblue", "lightgreen", "lightcyan", "lightred",
    "lightmagenta", "yellow", "white"
};
static int lm_canvink;                  /* what a <sprite> child inherits */
static int lm_canvpaper;

/* One palette attribute, by name. Answers the colour, or the default when the
 * attribute is absent; sets lmw_ok = 0 on a name that is not one of the
 * sixteen. weavesim's color_attr(), sentence for sentence. */
static int ovl_colorattr(const char *tag, const char *attr, int ai, int dflt)
{
    int k;

    lmw_ok = 1;
    if (ai < 0)
        return dflt;
    for (k = 0; k < 16; k++)
        if (ovl_aveq(ai, lmw_pal[k]))
            return k;
    ovl_m0(tag);
    ovl_mc(": ");
    ovl_mc(attr);
    ovl_mc("=\"");
    ovl_mv(ai);
    ovl_mc("\" is not one of the sixteen colours (WEAVE-SPEC 6.10.7)");
    ovl_raise(lmw_aline[ai]);
    lmw_ok = 0;
    return dflt;
}

/* ...and SPEC.md 5.4.2.2's FOURTH refusal, which is why this is decided at
 * pack: GFX_BLIT1 takes a pair only when one colour's planes are a subset of
 * the other's, and a run-time refusal would be a band that silently does not
 * arrive (WEAVE-SPEC 6.10.2 has no second path for one). `white` paper is
 * legal against all sixteen and so is `black`, so no palette anybody writes
 * meets this. */
static int ovl_penok(const char *attr, int ink, int paper, int line)
{
    if ((paper & ~ink & 0x0F) == 0 || (~paper & ink & 0x0F) == 0)
        return 1;
    ovl_m0("canvas: paper=\"");
    ovl_mc(lmw_pal[paper]);
    ovl_mc("\" against ");
    ovl_mc(attr);
    ovl_mc("=\"");
    ovl_mc(lmw_pal[ink]);
    ovl_mc("\": the two share no plane either way and GFX_BLIT1 refuses the "
           "pair (SPEC.md 5.4.2.2)");
    ovl_raise(line);
    return 0;
}

/* --- <sprite> ------------------------------------------------------------ */
static int ovl_sprite_elem(int card)
{
    int comp;
    int ai;
    int x = 0, y = 0, shown = 1, color;
    unsigned imgoff;
    unsigned imglen;

    if (!ovl_checkattrs(LMW_E_SPRITE))
        return 0;
    comp = ovl_newcomp(LMW_E_SPRITE, card);
    if (comp < 0)
        return 0;
    if (!ovl_setid(comp, LMW_E_SPRITE))
        return 0;
    ai = ovl_afind("img");
    if (ai < 0) {
        ovl_m0("sprite: attribute \"img\" is required");
        ovl_raise(lmw_tagline);
        return 0;
    }
    imgoff = ovl_nameput(lmw_avoff[ai], lmw_avlen[ai]);
    imglen = lmw_avlen[ai];
    if (imgoff == 0xFFFF) {
        ovl_m0("the sprite names do not fit this machine's pack claim "
               "(WEAVE-SPEC 11.4)");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    ai = ovl_afind("x");
    if (ai >= 0) {
        x = ovl_intattr("sprite", "x", ai, -320, 320, 1);
        if (!lmw_ok)
            return 0;
    }
    ai = ovl_afind("y");
    if (ai >= 0) {
        y = ovl_intattr("sprite", "y", ai, -160, 160, 1);
        if (!lmw_ok)
            return 0;
    }
    ai = ovl_afind("shown");
    if (ai >= 0) {
        shown = ovl_boolattr("sprite", "shown", ai);
        if (!lmw_ok)
            return 0;
    }
    ai = ovl_afind("color");
    color = ovl_colorattr("sprite", "color", ai, -1);
    if (!lmw_ok)
        return 0;
    if (ai >= 0 && !ovl_penok("color", color, lm_canvpaper, lmw_aline[ai]))
        return 0;
    if (x && !ovl_prop(comp, WA_X, PK_INT, x))
        return 0;
    if (y && !ovl_prop(comp, WA_Y, PK_INT, y))
        return 0;
    if (!shown && !ovl_prop(comp, WA_SHOWN, PK_INT, 0))
        return 0;
    /* 6.10.7, PRESENT -> PROP: written means a record, even at the canvas's
     * own ink, which is `walls`'s rule and not `tick`'s. It is what lets two
     * packers agree with no arithmetic, and what makes color="black" mean
     * something under an ink="yellow" canvas. */
    if (ai >= 0 && !ovl_prop(comp, WA_COLOR, PK_INT, color))
        return 0;
    /* The .WSP name and its length ride in the row: LMC_IDOFF is the id's and
     * a sprite's img is kept beside it in LMC_AUXOFF/LMC_AUX. */
    lm_wpw(ovl_crow(comp) + LMC_AUXOFF, imgoff);
    ovl_cset(comp, LMC_AUX, (int) (imglen > 255 ? 255 : imglen));
    /* 3.3: the sprite-image record is named by atom 11 (`frame`) and doubles
     * as that property's initial value. It is emitted HERE, with 0 where the
     * SPRITES index will go, because ovl_prop() cannot append to a closed
     * block - ovl_sprites() patches it once the .WSP has been read. */
    if (!ovl_prop(comp, WA_FRAME, PK_SPRITE, 0))
        return 0;
    if (!lmw_selfclose) {
        int openline = lmw_tagline;
        for (;;) {
            unsigned run;
            if (lmw_eof()) {
                ovl_neverclosed("sprite", openline);
                return 0;
            }
            if (lmw_lit("<!--")) {
                if (!ovl_wscomment())
                    return 0;
                continue;
            }
            if (lmw_lit("</"))
                break;
            if (lmw_lit("<")) {
                unsigned toff = lmw_i + 1;
                unsigned m = toff;
                while (lm_isalnum((int) lmw_at(m)))
                    m++;
                ovl_m0("<");
                ovl_msrcl(toff, m - toff);
                ovl_mc(">: <sprite> takes no children");
                ovl_raise(lmw_line);
                return 0;
            }
            run = lmw_i;
            runline = lmw_line;
            while (!lmw_eof() && lmw_at(lmw_i) != '<')
                lmw_adv(1);
            {
                unsigned k;
                for (k = run; k < lmw_i; k++)
                    if (!lm_isspace((int) lm_sb(LM_SLOT_WML, k))) {
                        ovl_m0("sprite: takes no text content");
                        ovl_raise(runline);
                        return 0;
                    }
            }
        }
        if (!ovl_closetag("sprite"))
            return 0;
    }
    ovl_psort(comp);
    lm_nspr++;
    return 1;
}

/* --- one component, whatever its tag -------------------------------------
 * Streaming: the open tag has already been read into the attribute table and
 * lmw_tagline names its line. */
static int ovl_component(int ei, int card)
{
    int comp;
    int ai;
    int openline = lmw_tagline;
    int takestext = (ei == LMW_E_LABEL || ei == LMW_E_TEXT
                     || ei == LMW_E_BUTTON || ei == LMW_E_CHECK
                     || ei == LMW_E_RADIO);
    int textatom = (ei == LMW_E_LABEL || ei == LMW_E_TEXT) ? WA_TEXT
                                                           : WA_LABEL;
    int tick = 0;
    int meter_max = 100;

    if (!ovl_checkattrs(ei))
        return 0;
    comp = ovl_newcomp(ei, card);
    if (comp < 0)
        return 0;
    if (!ovl_common(comp, ei))
        return 0;

    /* -- the attributes that INTERN or emit, in 3.3's table order -- */
    switch (ei) {
    case LMW_E_BOX:
        if (ovl_cget(comp, LMC_W) < 2 || ovl_cget(comp, LMC_H) < 1) {
            ovl_m0("box: w and h are required, 2x1 or more");
            ovl_raise(openline);
            return 0;
        }
        break;
    case LMW_E_SPACER:
        if (ovl_cget(comp, LMC_W) < 1) {
            ovl_m0("spacer: w is required");
            ovl_raise(openline);
            return 0;
        }
        break;
    case LMW_E_METER:
        ai = ovl_afind("max");
        if (ai >= 0) {
            meter_max = ovl_intattr("meter", "max", ai, 1, 32000, 0);
            if (!lmw_ok)
                return 0;
            if (!ovl_prop(comp, WA_MAX, PK_INT, meter_max))
                return 0;
        }
        ai = ovl_afind("value");
        if (ai >= 0) {
            int v = ovl_intattr("meter", "value", ai, 0, meter_max, 0);
            if (!lmw_ok)
                return 0;
            if (!ovl_prop(comp, WA_VALUE, PK_INT, v))
                return 0;
        }
        break;
    case LMW_E_BUTTON:
        if (!ovl_event(comp, "onclick", WA_ONCLICK))
            return 0;
        break;
    case LMW_E_CHECK:
        ai = ovl_afind("checked");
        if (ai >= 0) {
            int v = ovl_boolattr("check", "checked", ai);
            if (!lmw_ok)
                return 0;
            if (!ovl_prop(comp, WA_CHECKED, PK_INT, v))
                return 0;
        }
        if (!ovl_event(comp, "onchange", WA_ONCHANGE))
            return 0;
        break;
    case LMW_E_RADIO:
        ai = ovl_afind("group");
        if (ai < 0) {
            ovl_m0("radio: attribute \"group\" is required");
            ovl_raise(openline);
            return 0;
        }
        {
            int a;
            lm_sbclear();
            {
                unsigned k;
                for (k = 0; k < lmw_avlen[ai]; k++)
                    lm_sbputc((int) (unsigned char)
                              lmw_valbuf[lmw_avoff[ai] + k]);
            }
            a = ovl_intern(LM_SLOT_WML, lmw_aline[ai]);
            if (a == 0)
                return 0;
            if (!ovl_prop(comp, WA_GROUP, PK_ATOM, a))
                return 0;
        }
        ai = ovl_afind("checked");
        if (ai >= 0) {
            int v = ovl_boolattr("radio", "checked", ai);
            if (!lmw_ok)
                return 0;
            if (!ovl_prop(comp, WA_CHECKED, PK_INT, v))
                return 0;
        }
        if (!ovl_event(comp, "onchange", WA_ONCHANGE))
            return 0;
        break;
    case LMW_E_INPUT:
        ai = ovl_afind("cols");
        if (ai >= 0) {
            int v = ovl_intattr("input", "cols", ai, 2, 60, 0);
            if (!lmw_ok)
                return 0;
            if (!ovl_prop(comp, WA_COLS, PK_INT, v))
                return 0;
        }
        ai = ovl_afind("text");
        if (ai >= 0 && lmw_avlen[ai] > 0) {
            int a;
            lm_sbclear();
            {
                unsigned k;
                for (k = 0; k < lmw_avlen[ai]; k++)
                    lm_sbputc((int) (unsigned char)
                              lmw_valbuf[lmw_avoff[ai] + k]);
            }
            a = ovl_intern(LM_SLOT_WML, lmw_aline[ai]);
            if (a == 0)
                return 0;
            if (!ovl_prop(comp, WA_TEXT, PK_ATOM, a))
                return 0;
        }
        if (!ovl_event(comp, "onchange", WA_ONCHANGE))
            return 0;
        if (!ovl_event(comp, "onkey", WA_ONKEY))
            return 0;
        break;
    case LMW_E_LIST:
        ai = ovl_afind("rows");
        if (ai >= 0) {
            int v = ovl_intattr("list", "rows", ai, 1, 40, 0);
            if (!lmw_ok)
                return 0;
            if (!ovl_prop(comp, WA_ROWS, PK_INT, v))
                return 0;
        }
        if (!ovl_event(comp, "onselect", WA_ONSELECT))
            return 0;
        break;
    case LMW_E_GRID:
        {
            int cols, rows;
            ai = ovl_afind("cols");
            if (ai < 0) {
                ovl_m0("grid: attribute \"cols\" is required");
                ovl_raise(openline);
                return 0;
            }
            if (ovl_afind("rows") < 0) {
                ovl_m0("grid: attribute \"rows\" is required");
                ovl_raise(openline);
                return 0;
            }
            cols = ovl_intattr("grid", "cols", ai, 1, 26, 0);
            if (!lmw_ok)
                return 0;
            rows = ovl_intattr("grid", "rows", ovl_afind("rows"), 1, 256,
                               0);
            if (!lmw_ok)
                return 0;
            if (rows * cols > 6140) {
                ovl_m0("grid is ");
                ovl_mn(cols);
                ovl_mc("x");
                ovl_mn(rows);
                ovl_mc(" = ");
                ovl_mn(rows * cols);
                ovl_mc(" cells; the cap is 6140 - the cell store plus its "
                       "pool must fit a 26KB claim");
                ovl_raise(openline);
                return 0;
            }
            lm_hasgrid = 1;
            lm_gridcols = cols;
            lm_gridrows = rows;
            if (!ovl_prop(comp, WA_COLS, PK_INT, cols))
                return 0;
            if (!ovl_prop(comp, WA_ROWS, PK_INT, rows))
                return 0;
        }
        if (!ovl_event(comp, "onselect", WA_ONSELECT))
            return 0;
        if (!ovl_event(comp, "onedit", WA_ONEDIT))
            return 0;
        if (!ovl_event(comp, "oncalc", WA_ONCALC))
            return 0;
        break;
    case LMW_E_CANVAS:
        {
            int w, h;
            if (ovl_afind("w") < 0) {
                ovl_m0("canvas: attribute \"w\" is required");
                ovl_raise(openline);
                return 0;
            }
            if (ovl_afind("h") < 0) {
                ovl_m0("canvas: attribute \"h\" is required");
                ovl_raise(openline);
                return 0;
            }
            w = ovl_intattr("canvas", "w", ovl_afind("w"), 64, 320, 0);
            if (!lmw_ok)
                return 0;
            h = ovl_intattr("canvas", "h", ovl_afind("h"), 32, 160, 0);
            if (!lmw_ok)
                return 0;
            if (w % 8) {
                ovl_m0("canvas: w=\"");
                ovl_mn(w);
                ovl_mc("\" is not a multiple of 8 - bands are byte-aligned "
                       "(WEAVE-SPEC 3.3)");
                ovl_raise(lmw_aline[ovl_afind("w")]);
                return 0;
            }
            /* 6.10.7's palette, before `walls` because 3.3's table order is
             * the interning order (2.14 rule 3a) - these intern nothing, but
             * the two packers walk the same table in the same direction and a
             * divergence here would be found by a bundle that does. */
            ai = ovl_afind("ink");
            lm_canvink = ovl_colorattr("canvas", "ink", ai, 0);
            if (!lmw_ok)
                return 0;
            if (ai >= 0 && !ovl_prop(comp, WA_INK, PK_INT, lm_canvink))
                return 0;
            ai = ovl_afind("paper");
            lm_canvpaper = ovl_colorattr("canvas", "paper", ai, 15);
            if (!lmw_ok)
                return 0;
            if (ai >= 0 && !ovl_prop(comp, WA_PAPER, PK_INT, lm_canvpaper))
                return 0;
            if (!ovl_penok("ink", lm_canvink, lm_canvpaper, openline))
                return 0;
            ai = ovl_afind("walls");
            if (ai >= 0) {
                int mask = 0;
                unsigned k;
                for (k = 0; k < lmw_avlen[ai]; k++) {
                    char c = lmw_valbuf[lmw_avoff[ai] + k];
                    int b = (c == 'T') ? 1 : (c == 'B') ? 2
                          : (c == 'L') ? 4 : (c == 'R') ? 8 : 0;
                    if (!b) {
                        ovl_m0("canvas: walls=\"");
                        ovl_mv(ai);
                        ovl_mc("\" is not a subset of TBLR");
                        ovl_raise(lmw_aline[ai]);
                        return 0;
                    }
                    mask |= b;
                }
                if (!ovl_prop(comp, WA_WALLS, PK_INT, mask))
                    return 0;
            }
            ai = ovl_afind("tick");
            if (ai >= 0) {
                tick = ovl_intattr("canvas", "tick", ai, 0, 255, 0);
                if (!lmw_ok)
                    return 0;
                if (tick && !ovl_prop(comp, WA_TICK, PK_INT, tick))
                    return 0;
            }
            lm_hascanvas = 1;
            lm_canvw = w;
            lm_canvh = h;
            ovl_cset(comp, LMC_W, w / 8);
            ovl_cset(comp, LMC_H, (h + 7) / 8);
        }
        if (!ovl_event(comp, "onkey", WA_ONKEY))
            return 0;
        if (!ovl_event(comp, "oncollide", WA_ONCOLLIDE))
            return 0;
        if (!ovl_event(comp, "onwall", WA_ONWALL))
            return 0;
        if (!ovl_event(comp, "onscore", WA_ONSCORE))
            return 0;
        if (!ovl_event(comp, "ontick", WA_ONTICK))
            return 0;
        if (ovl_afind("ontick") >= 0 && tick < 1) {
            ovl_m0("canvas: ontick requires tick=\"1\" or more "
                   "(WEAVE-SPEC 3.3)");
            ovl_raise(openline);
            return 0;
        }
        break;
    default:
        break;
    }

    /* -- the content, in document order -- */
    lm_sbclear();
    lmw_hastext = 0;
    lmw_nitem = 0;
    lm_canvspr = 0;
    if (!lmw_selfclose) {
        for (;;) {
            unsigned run;
            if (lmw_eof()) {
                ovl_neverclosed(lmw_elem[ei], openline);
                return 0;
            }
            if (lmw_lit("<!--")) {
                if (!ovl_wscomment())
                    return 0;
                continue;
            }
            if (lmw_lit("</"))
                break;
            if (lmw_lit("<")) {
                int ce;

                if (!ovl_opentag())
                    return 0;
                ce = ovl_elem_index(lmw_tagoff, lmw_taglen);
                if (ei == LMW_E_LIST) {
                    if (ce != LMW_E_ITEM) {
                        ovl_m0("<");
                        ovl_mtag();
                        ovl_mc(ce >= 0 ? ">: not a child of <list> "
                                         "(WEAVE-SPEC 3.2)"
                                       : lm_s_noelem);
                        ovl_raise(lmw_tagline);
                        return 0;
                    }
                    if (!ovl_list_item())
                        return 0;
                    continue;
                }
                if (ei == LMW_E_CANVAS) {
                    if (ce != LMW_E_SPRITE) {
                        ovl_m0("<");
                        ovl_mtag();
                        ovl_mc(ce >= 0 ? ">: not a child of <canvas> "
                                         "(WEAVE-SPEC 3.2)"
                                       : lm_s_noelem);
                        ovl_raise(lmw_tagline);
                        return 0;
                    }
                    if (!ovl_sprite_elem(card))
                        return 0;
                    lm_canvspr++;
                    if (lm_canvspr > 16) {
                        ovl_m0("");
                        ovl_mn(lm_canvspr);
                        ovl_mc(" sprites; a canvas composes 16 at most "
                               "(WEAVE-SPEC 6.10)");
                        ovl_raise(openline);
                        return 0;
                    }
                    continue;
                }
                ovl_m0("<");
                ovl_mtag();
                ovl_mc(">: <");
                ovl_mc(lmw_elem[ei]);
                ovl_mc("> takes no children");
                ovl_raise(lmw_tagline);
                return 0;
            }
            run = lmw_i;
            runline = lmw_line;
            while (!lmw_eof() && lmw_at(lmw_i) != '<')
                lmw_adv(1);
            if (takestext) {
                if (!ovl_text_run(run, lmw_i, runline))
                    return 0;
            } else if (ei != LMW_E_CANVAS) {
                unsigned k;
                for (k = run; k < lmw_i; k++)
                    if (!lm_isspace((int) lm_sb(LM_SLOT_WML, k))) {
                        ovl_m0(lmw_elem[ei]);
                        ovl_mc(": takes no text content");
                        ovl_raise(runline);
                        return 0;
                    }
            }
        }
        if (!ovl_closetag(lmw_elem[ei]))
            return 0;
    }

    /* -- the content atom, at the CLOSE (2.14 rule 3a) -- */
    if (takestext) {
        ovl_collapse();
        if (lm_sbn > 0) {
            int a = ovl_intern(LM_SLOT_WML, lmw_textline);
            if (a == 0)
                return 0;
            if (!ovl_prop(comp, textatom, PK_ATOM, a))
                return 0;
        }
    }
    if (ei == LMW_E_LIST && lmw_nitem > 0) {
        /* 2.6.1's blob: the count byte then the item atoms. It is written
         * into the blob store by lmwrite.c; here the records are handed over
         * as a run in the name store, which is the same claim region. */
        unsigned at = (unsigned) lmw_namen;
        int k;
        if (at + (unsigned) lmw_nitem + 1 > LM_NAMEMAX) {
            ovl_m0("the list items do not fit this machine's pack claim "
                   "(WEAVE-SPEC 11.4)");
            ovl_raise(openline);
            return 0;
        }
        lm_wpb(LMW_NAMES + at, (unsigned) lmw_nitem);
        for (k = 0; k < lmw_nitem; k++)
            lm_wpb(LMW_NAMES + at + 1 + (unsigned) k, lmw_items[k]);
        lmw_namen = (int) (at + (unsigned) lmw_nitem + 1);
        if (!ovl_prop(comp, WA_ITEMS, PK_BLOB, (int) at))
            return 0;
    }
    ovl_psort(comp);
    return 1;
}

/* --- <card> -------------------------------------------------------------- */
static int ovl_element_card(void)
{
    int openline;
    int ai;
    int idx;

    openline = lmw_tagline;
    if (!ovl_checkattrs(LMW_E_CARD))
        return 0;
    ai = ovl_afind("id");
    if (ai < 0) {
        ovl_m0("card: attribute \"id\" is required");
        ovl_raise(openline);
        return 0;
    }
    if (ovl_id_taken(lmw_avoff[ai], lmw_avlen[ai])) {
        ovl_m0("card: id \"");
        ovl_mv(ai);
        ovl_mc("\" is already taken");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    if (lm_ncard >= LM_MAXCARD) {
        ovl_m0("");
        ovl_mn(lm_ncard + 1);
        ovl_mc(" cards; an app has 1..8 (WEAVE-SPEC 3.2)");
        ovl_raise(openline);
        return 0;
    }
    idx = lm_ncard;
    {
        unsigned k = lmw_anoff[ai] + lmw_anlen[ai];
        while (lm_sb(LM_SLOT_WML, k) != '"')
            k++;
        lmw_cardoff[idx] = k + 1;
        lmw_cardlen[idx] = lmw_avlen[ai];
    }
    lm_ncard++;
    if (lmw_selfclose)
        return 1;
    for (;;) {
        unsigned run;
        if (lmw_eof()) {
            ovl_neverclosed("card", openline);
            return 0;
        }
        if (lmw_lit("<!--")) {
            if (!ovl_wscomment())
                return 0;
            continue;
        }
        if (lmw_lit("</"))
            break;
        if (lmw_lit("<")) {
            int ce;

            if (!ovl_opentag())
                return 0;
            ce = ovl_elem_index(lmw_tagoff, lmw_taglen);
            if (ce < 0) {
                ovl_m0("<");
                ovl_mtag();
                ovl_mc(lm_s_noelem);
                ovl_raise(lmw_tagline);
                return 0;
            }
            if (ce > LMW_E_CANVAS) {        /* sprite, app, card, menu... */
                ovl_m0("<");
                ovl_mtag();
                ovl_mc(">: not a child of <card> (WEAVE-SPEC 3.2)");
                ovl_raise(lmw_tagline);
                return 0;
            }
            if (!ovl_component(ce, idx + 1))
                return 0;
            continue;
        }
        run = lmw_i;
        runline = lmw_line;         /* where the run STARTED - the model
                                     * records a text node at its first line */
        while (!lmw_eof() && lmw_at(lmw_i) != '<')
            lmw_adv(1);
        {
            unsigned k;
            for (k = run; k < lmw_i; k++)
                if (!lm_isspace((int) lm_sb(LM_SLOT_WML, k))) {
                    ovl_m0("card: bare text is not a component");
                    ovl_raise(runline);
                    return 0;
                }
        }
    }
    return ovl_closetag("card");
}

/* --- <menu> -------------------------------------------------------------- */
static int ovl_element_menu(void)
{
    int openline;
    int ai;
    int mi;

    openline = lmw_tagline;
    if (!ovl_checkattrs(LMW_E_MENU))
        return 0;
    ai = ovl_afind("title");
    if (ai < 0) {
        ovl_m0("menu: attribute \"title\" is required");
        ovl_raise(openline);
        return 0;
    }
    if (lmw_avlen[ai] < 1 || lmw_avlen[ai] > 8) {
        ovl_m0("menu: title=\"");
        ovl_mv(ai);
        ovl_mc("\" is over 8 chars");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    if (lm_nmenu >= LM_MAXMENU) {
        ovl_m0("");
        ovl_mn(lm_nmenu + 1);
        ovl_mc(" menus; MENU_APPMAX is 5 (SPEC.md 12.2)");
        ovl_raise(openline);
        return 0;
    }
    mi = lm_nmenu;
    lm_sbclear();
    {
        unsigned k;
        for (k = 0; k < lmw_avlen[ai]; k++)
            lm_sbputc((int) (unsigned char) lmw_valbuf[lmw_avoff[ai] + k]);
    }
    {
        int a = ovl_intern(LM_SLOT_WML, lmw_aline[ai]);
        if (a == 0)
            return 0;
        lm_mtitle[mi] = (unsigned char) a;
    }
    lm_mcount[mi] = 0;
    lm_nmenu++;
    if (lmw_selfclose) {
        ovl_m0("0 menu items; a menu holds 1..8");
        ovl_raise(openline);
        return 0;
    }
    for (;;) {
        unsigned run;
        if (lmw_eof()) {
            ovl_neverclosed("menu", openline);
            return 0;
        }
        if (lmw_lit("<!--")) {
            if (!ovl_wscomment())
                return 0;
            continue;
        }
        if (lmw_lit("</"))
            break;
        if (lmw_lit("<")) {
            int ce;
            int itemline;

            if (!ovl_opentag())
                return 0;
            ce = ovl_elem_index(lmw_tagoff, lmw_taglen);
            if (ce != LMW_E_ITEM) {
                ovl_m0("<");
                ovl_mtag();
                ovl_mc(ce >= 0 ? ">: not a child of <menu> (WEAVE-SPEC 3.2)"
                               : lm_s_noelem);
                ovl_raise(lmw_tagline);
                return 0;
            }
            itemline = lmw_tagline;
            if (!ovl_checkattrs(LMW_E_ITEM))
                return 0;
            {
                int k = mi * LM_MAXITEM + (int) lm_mcount[mi];
                int cai = ovl_afind("oncommand");
                if (lm_mcount[mi] >= LM_MAXITEM) {
                    ovl_m0("");
                    ovl_mn((int) lm_mcount[mi] + 1);
                    ovl_mc(" menu items; a menu holds 1..8");
                    ovl_raise(openline);
                    return 0;
                }
                if (cai >= 0) {
                    unsigned at = ovl_nameput(lmw_avoff[cai],
                                              lmw_avlen[cai]);
                    if (at == 0xFFFF) {
                        ovl_m0("the handler names do not fit this machine's "
                               "pack claim (WEAVE-SPEC 11.4)");
                        ovl_raise(lmw_aline[cai]);
                        return 0;
                    }
                    lm_mfnoff[k] = at;
                    lm_mfnlen[k] = lmw_avlen[cai];
                } else {
                    lm_mfnoff[k] = 0xFFFF;
                    lm_mfnlen[k] = 0;
                }
                lm_mline[k] = itemline;
                lm_sbclear();
                lmw_hastext = 0;
                if (!lmw_selfclose) {
                    for (;;) {
                        unsigned r2;
                        if (lmw_eof()) {
                            ovl_neverclosed("item", itemline);
                            return 0;
                        }
                        if (lmw_lit("<!--")) {
                            if (!ovl_wscomment())
                                return 0;
                            continue;
                        }
                        if (lmw_lit("</"))
                            break;
                        if (lmw_lit("<")) {
                            ovl_m0("<item> takes no children");
                            ovl_raise(lmw_line);
                            return 0;
                        }
                        r2 = lmw_i;
                        runline = lmw_line;
                        while (!lmw_eof() && lmw_at(lmw_i) != '<')
                            lmw_adv(1);
                        if (!ovl_text_run(r2, lmw_i, runline))
                            return 0;
                    }
                    if (!ovl_closetag("item"))
                        return 0;
                }
                ovl_collapse();
                if (lm_sbn < 1 || lm_sbn > 24) {
                    ovl_m0("item: a menu item label is 1..24 glyphs");
                    ovl_raise(itemline);
                    return 0;
                }
                {
                    int a = ovl_intern(LM_SLOT_WML, itemline);
                    if (a == 0)
                        return 0;
                    lm_mlabel[k] = (unsigned char) a;
                }
                lm_mcount[mi]++;
            }
            continue;
        }
        run = lmw_i;
        runline = lmw_line;
        while (!lmw_eof() && lmw_at(lmw_i) != '<')
            lmw_adv(1);
        {
            unsigned k;
            for (k = run; k < lmw_i; k++)
                if (!lm_isspace((int) lm_sb(LM_SLOT_WML, k))) {
                    ovl_m0("menu: bare text is not an item");
                    ovl_raise(runline);
                    return 0;
                }
        }
    }
    if (!ovl_closetag("menu"))
        return 0;
    if (lm_mcount[mi] < 1) {
        ovl_m0("0 menu items; a menu holds 1..8");
        ovl_raise(openline);
        return 0;
    }
    return 1;
}

/* --- <script> ------------------------------------------------------------ */
static int ovl_element_script(void)
{
    int openline;
    int ai;

    openline = lmw_tagline;
    if (!ovl_checkattrs(LMW_E_SCRIPT))
        return 0;
    if (lm_hasscript) {
        ovl_m0("script: one <script> per app");
        ovl_raise(openline);
        return 0;
    }
    if (!lmw_selfclose) {
        /* 3.3/10.5: inline script is not packed. weavesim raises this when
         * the element carries ANY text or child, so the content is scanned
         * first and the close tag consumed. */
        int any = 0;
        for (;;) {
            unsigned run;
            if (lmw_eof()) {
                ovl_neverclosed("script", openline);
                return 0;
            }
            if (lmw_lit("<!--")) {
                if (!ovl_wscomment())
                    return 0;
                continue;
            }
            if (lmw_lit("</"))
                break;
            if (lmw_lit("<")) {
                any = 1;
                lmw_adv(1);
                while (!lmw_eof() && lmw_at(lmw_i) != '>')
                    lmw_adv(1);
                if (!lmw_eof())
                    lmw_adv(1);
                continue;
            }
            run = lmw_i;
            while (!lmw_eof() && lmw_at(lmw_i) != '<')
                lmw_adv(1);
            {
                unsigned k;
                for (k = run; k < lmw_i; k++)
                    if (!lm_isspace((int) lm_sb(LM_SLOT_WML, k)))
                        any = 1;
            }
        }
        if (any) {
            ovl_m0("script: inline script is not packed; name a .WJS file - "
                   "the runtime never parses text");
            ovl_raise(openline);
            return 0;
        }
        if (!ovl_closetag("script"))
            return 0;
    }
    ai = ovl_afind("src");
    if (ai < 0) {
        ovl_m0("script: attribute \"src\" is required");
        ovl_raise(openline);
        return 0;
    }
    if (lmw_avlen[ai] > 12) {
        ovl_m0("script: src=\"");
        ovl_mv(ai);
        ovl_mc("\" is not an 8.3 name");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    {
        unsigned k;
        for (k = 0; k < lmw_avlen[ai]; k++)
            lm_scriptsrc[k] = lmw_valbuf[lmw_avoff[ai] + k];
        lm_scriptsrc[lmw_avlen[ai]] = 0;
    }
    lm_hasscript = 1;
    lm_scriptline = openline;
    return 1;
}

/* ==========================================================================
 * THE DOCUMENT
 * ========================================================================*/

/* The radio-group check (WEAVE-SPEC 11.3: "radio groups have >= 2 members").
 * Groups are compared by ATOM ID, which is exact: two equal strings intern to
 * one atom by construction (2.7). */
static int ovl_radio_groups(void)
{
    int i, j;

    for (i = 0; i < lm_ncomp; i++) {
        int g = 0, chk = 0, n = 0, nchk = 0;
        int first = -1;
        unsigned pr;
        int k, np;

        if (ovl_cget(i, LMC_CTYPE) != WC_RADIO)
            continue;
        np = ovl_cget(i, LMC_NPROP);
        pr = (unsigned) lm_ww(ovl_crow(i) + LMC_PROP);
        for (k = 0; k < np; k++) {
            unsigned r = LMW_PROPS + (pr + (unsigned) k) * LM_PROPSZ;
            if (lm_wb(r + LMP_ATOM) == WA_GROUP)
                g = (int) lm_ww(r + LMP_VAL);
        }
        /* has this group already been reported on? */
        for (j = 0; j < i; j++) {
            int g2 = 0;
            int np2, k2;
            unsigned pr2;
            if (ovl_cget(j, LMC_CTYPE) != WC_RADIO)
                continue;
            np2 = ovl_cget(j, LMC_NPROP);
            pr2 = (unsigned) lm_ww(ovl_crow(j) + LMC_PROP);
            for (k2 = 0; k2 < np2; k2++) {
                unsigned r = LMW_PROPS + (pr2 + (unsigned) k2) * LM_PROPSZ;
                if (lm_wb(r + LMP_ATOM) == WA_GROUP)
                    g2 = (int) lm_ww(r + LMP_VAL);
            }
            if (g2 == g)
                break;
        }
        if (j < i)
            continue;               /* a later member; already counted */
        for (j = 0; j < lm_ncomp; j++) {
            int g2 = 0;
            int np2, k2;
            unsigned pr2;
            if (ovl_cget(j, LMC_CTYPE) != WC_RADIO)
                continue;
            np2 = ovl_cget(j, LMC_NPROP);
            pr2 = (unsigned) lm_ww(ovl_crow(j) + LMC_PROP);
            chk = 0;
            for (k2 = 0; k2 < np2; k2++) {
                unsigned r = LMW_PROPS + (pr2 + (unsigned) k2) * LM_PROPSZ;
                if (lm_wb(r + LMP_ATOM) == WA_GROUP)
                    g2 = (int) lm_ww(r + LMP_VAL);
                if (lm_wb(r + LMP_ATOM) == WA_CHECKED)
                    chk = (int) lm_ww(r + LMP_VAL);
            }
            if (g2 != g)
                continue;
            if (first < 0)
                first = j;
            n++;
            if (chk)
                nchk++;
        }
        if (n < 2) {
            ovl_m0("radio: group \"");
            {
                unsigned m = os88_strlen(lmw_msg);
                unsigned off = lm_atom_off(g);
                unsigned len = lm_atom_len(g);
                unsigned q;
                for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                    lmw_msg[m] = (char) lm_wb(LMW_ATTXT + off + q);
                    m++;
                }
                lmw_msg[m] = 0;
            }
            ovl_mc("\" has one member; a group is 2 or more");
            ovl_raise((int) lm_ww(ovl_crow(first) + LMC_LINE));
            return 0;
        }
        if (nchk > 1) {
            ovl_m0("radio: group \"");
            {
                unsigned m = os88_strlen(lmw_msg);
                unsigned off = lm_atom_off(g);
                unsigned len = lm_atom_len(g);
                unsigned q;
                for (q = 0; q < len && m + 1 < LM_ERRMAX; q++) {
                    lmw_msg[m] = (char) lm_wb(LMW_ATTXT + off + q);
                    m++;
                }
                lmw_msg[m] = 0;
            }
            ovl_mc("\" has two checked members; one per group");
            ovl_raise((int) lm_ww(ovl_crow(first) + LMC_LINE));
            return 0;
        }
    }
    return 1;
}

int ovl_wml(void)
{
    int ai;
    int ngrid = 0, ncanv = 0;
    int i;

    lm_atoms_reset();
    lmw_i = 0;
    lmw_line = 1;
    lm_ncard = 0;
    lm_ncomp = 0;
    lm_nprop = 0;
    lm_nmenu = 0;
    lm_nev = 0;
    lm_nspr = 0;
    lm_nblob = 0;
    lmw_namen = 0;
    lm_vmkb = 16;
    lm_entrycard = 1;
    lm_hasgrid = 0;
    lm_hascanvas = 0;
    lm_hasscript = 0;
    lm_gridcols = 0;
    lm_gridrows = 0;
    lm_canvw = 0;
    lm_canvh = 0;
    lm_canvspr = 0;
    lm_startfn = -1;
    lm_nfunc = 0;
    lm_nglob = 0;
    lm_ncell = 0;
    lm_nform = 0;
    lm_appname[0] = 0;
    lm_scriptsrc[0] = 0;

    if (lm_srclen(LM_SLOT_WML) == 0) {
        ovl_m0("no MAIN.WML in the project folder");
        ovl_raise(0);
        return 0;
    }
    if (!ovl_wscomment())
        return 0;
    if (!ovl_opentag())
        return 0;
    if (!lm_srceqi(LM_SLOT_WML, lmw_tagoff, lmw_taglen, "app")) {
        ovl_m0("<");
        ovl_mtag();
        ovl_mc(">: the document element is <app> (WEAVE-SPEC 3.2)");
        ovl_raise(lmw_tagline);
        return 0;
    }
    if (!ovl_checkattrs(LMW_E_APP))
        return 0;
    ai = ovl_afind("name");
    if (ai < 0) {
        ovl_m0("app: attribute \"name\" is required");
        ovl_raise(lmw_tagline);
        return 0;
    }
    if (lmw_avlen[ai] < 1 || lmw_avlen[ai] > 15) {
        ovl_m0("app: name=\"");
        ovl_mv(ai);
        ovl_mc("\" is ");
        ovl_mn((int) lmw_avlen[ai]);
        ovl_mc(" chars; the header field is 15 (WEAVE-SPEC 2.2)");
        ovl_raise(lmw_aline[ai]);
        return 0;
    }
    {
        unsigned k;
        for (k = 0; k < lmw_avlen[ai]; k++)
            lm_appname[k] = lmw_valbuf[lmw_avoff[ai] + k];
        lm_appname[lmw_avlen[ai]] = 0;
    }
    ai = ovl_afind("vm");
    if (ai >= 0) {
            lm_vmkb = ovl_intattr("app", "vm", ai, 16, 32, 0);
        if (!lmw_ok)
            return 0;
    }
    {
        int approot = lmw_tagline;
        if (!lmw_selfclose) {
            for (;;) {
                unsigned run;
                if (lmw_eof()) {
                    ovl_neverclosed("app", approot);
                    return 0;
                }
                if (lmw_lit("<!--")) {
                    if (!ovl_wscomment())
                        return 0;
                    continue;
                }
                if (lmw_lit("</"))
                    break;
                if (lmw_lit("<")) {
                    unsigned toff;
                    unsigned tlen;
                    int ce;
                    /* THE TAG IS SCANNED BEFORE IT IS CLASSIFIED, which is
                     * the model's order: WmlScanner reads a whole element -
                     * name, attributes and all - and the Analyzer rejects the
                     * name afterwards. Classifying first would report "not a
                     * Weave element" for `<but style=` where weavesim reports
                     * the malformed attribute list. */
                    if (!ovl_opentag())
                        return 0;
                    toff = lmw_tagoff;
                    tlen = lmw_taglen;
                    ce = ovl_elem_index(toff, tlen);
                    if (ce == LMW_E_CARD) {
                        if (!ovl_element_card())
                            return 0;
                    } else if (ce == LMW_E_MENU) {
                        if (!ovl_element_menu())
                            return 0;
                    } else if (ce == LMW_E_SCRIPT) {
                        if (!ovl_element_script())
                            return 0;
                    } else {
                        ovl_m0("<");
                        ovl_msrcl(toff, tlen);
                        ovl_mc(ce >= 0 ? ">: not a child of <app> "
                                         "(WEAVE-SPEC 3.2)"
                                       : lm_s_noelem);
                        ovl_raise(lmw_tagline);
                        return 0;
                    }
                    continue;
                }
                run = lmw_i;
                runline = lmw_line;
                while (!lmw_eof() && lmw_at(lmw_i) != '<')
                    lmw_adv(1);
                {
                    unsigned k;
                    for (k = run; k < lmw_i; k++)
                        if (!lm_isspace((int) lm_sb(LM_SLOT_WML, k))) {
                            ovl_m0("app: text content is not a card");
                            ovl_raise(runline);
                            return 0;
                        }
                }
            }
            if (!ovl_closetag("app"))
                return 0;
        }
        if (!ovl_wscomment())
            return 0;
        if (!lmw_eof()) {
            ovl_m0("text after the document element");
            ovl_raise(lmw_line);
            return 0;
        }
        if (lm_ncard < 1) {
            ovl_m0("0 cards; an app has 1..8 (WEAVE-SPEC 3.2)");
            ovl_raise(approot);
            return 0;
        }
        for (i = 0; i < lm_ncomp; i++) {
            if (ovl_cget(i, LMC_CTYPE) == WC_GRID)
                ngrid++;
            if (ovl_cget(i, LMC_CTYPE) == WC_CANVAS)
                ncanv++;
        }
        if (ngrid > 1 || ncanv > 1) {
            ovl_m0("");
            ovl_mn(ngrid);
            ovl_mc(" grids, ");
            ovl_mn(ncanv);
            ovl_mc(" canvases; at most one of each - each owns a dedicated "
                   "claim and the cap is 8 per owner (SPEC.md 50.2)");
            ovl_raise(approot);
            return 0;
        }
    }
    return ovl_radio_groups();
}
