/* ============================================================================
 * os8088 - apps/cword/cwovl.c     everything that runs once per command
 *
 * #included by cword.c - one translation unit (SPEC.md 73.1) - but NOT in the
 * same segment as the rest of it. Every function here is named `ovl_*`, which
 * is the whole of the marking that puts its code in `.modc` and ships it as
 * CWORD.OVL beside CWORD.O88 (SPEC.md 73.14). The module is read into a heap
 * claim the first time one of these is called and stays for the session.
 *
 * WHY THIS IS THE LINE. A package's image and bss share 61,440 bytes and that
 * ceiling IS the segment (SPEC.md 33). What decides which side of the line a
 * subsystem falls is not its size but its FREQUENCY: everything on the redraw
 * path pays for the far call and the load check, and everything reached from a
 * menu pays it once and does not care. The dialogs, the file format, search
 * and the three utilities are all reached from a menu. Nothing a keystroke
 * touches is out here.
 *
 * THE DATA STAYS RESIDENT AND THAT IS NOT A COMPROMISE, it is what makes the
 * mechanism possible at all: the module keeps DS = the package's segment, so
 * the document, the control tables below, every string literal and every
 * static are ordinary DS-relative references from out here exactly as they are
 * from in there. Only code moves.
 *
 * A DIALOG IS A MODE, NOT A NESTED LOOP. This operating system delivers input
 * as callbacks, so there is nothing to loop on: ovl_dlg_open() draws the panel
 * and raises cw_dlg, and until the panel comes down every key and every click
 * is routed here by the resident half. The panel saves nothing under itself -
 * closing it repaints the window, which is what SPEC.md 11's damage model is
 * for and is cheaper than a save-under of 60KB of glass.
 * ==========================================================================*/

/* --- a control ------------------------------------------------------------
 * One table drives every dialog's drawing, its hit test and its keyboard
 * focus, which is the fm_hit discipline (SPEC.md 22) applied to dialogs: a
 * second layout in the hit test is a second implementation that drifts, and
 * the symptom is a check box that highlights one thing and toggles another. */
#define CWK_LABEL   0
#define CWK_CHECK   1
#define CWK_RADIO   2
#define CWK_EDIT    3
#define CWK_BUTTON  4

struct cw_ctl {
    unsigned char kind;
    unsigned char x;                    /* cells from the panel's left */
    unsigned char y;                    /* rows from its top */
    unsigned char w;                    /* cells wide (edits and buttons) */
    unsigned char id;                   /* what it means to its dialog */
    const char   *text;
};

/* --- Format Character (Opus dlg/chp.des) ---------------------------------
 * The seven check boxes the product has, in its order. Position and Color are
 * present and greyed: this machine has one 8x8 face and two colours on two
 * adapters of three, so they are facts rather than omissions (SPEC.md 47). */
static const struct cw_ctl cw_dc_char[] = {
    { CWK_LABEL,  1,  0, 0, 0,           "Character" },
    { CWK_CHECK,  2,  2, 0, CW_A_BOLD,   "Bold" },
    { CWK_CHECK,  2,  3, 0, CW_A_ITAL,   "Italic" },
    { CWK_CHECK,  2,  4, 0, CW_A_SCAP,   "Small Caps" },
    { CWK_CHECK,  2,  5, 0, CW_A_HID,    "Hidden" },
    { CWK_CHECK, 20,  2, 0, CW_A_ULINE,  "Underline" },
    { CWK_CHECK, 20,  3, 0, CW_A_WUL,    "Word Underline" },
    { CWK_CHECK, 20,  4, 0, CW_A_DUL,    "Double Underline" },
    { CWK_LABEL,  2,  7, 0, 0,           "Position: Normal   Color: Black" },
    { CWK_BUTTON,38,  7, 8, 200,         "OK" },
    { CWK_BUTTON,38,  9, 8, 201,         "Cancel" }
};
#define CW_NC_CHAR 11

/* --- Format Paragraph (Opus dlg/pap.des) --------------------------------- */
static const struct cw_ctl cw_dc_para[] = {
    { CWK_LABEL,  1,  0, 0, 0,   "Paragraph" },
    { CWK_LABEL,  2,  2, 0, 0,   "Alignment" },
    { CWK_RADIO,  2,  3, 0, 0,   "Left" },
    { CWK_RADIO,  2,  4, 0, 1,   "Centered" },
    { CWK_RADIO,  2,  5, 0, 2,   "Right" },
    { CWK_RADIO,  2,  6, 0, 3,   "Justified" },
    { CWK_LABEL, 16,  2, 0, 0,   "Line Spacing" },
    { CWK_RADIO, 16,  3, 0, 10,  "Single" },
    { CWK_RADIO, 16,  4, 0, 11,  "1 1/2 Lines" },
    { CWK_RADIO, 16,  5, 0, 12,  "Double" },
    { CWK_CHECK, 16,  6, 0, 20,  "Space Before" },
    { CWK_LABEL, 33,  2, 0, 0,   "Indents" },
    { CWK_EDIT,  40,  3, 5, 30,  "Left" },
    { CWK_EDIT,  40,  4, 5, 31,  "First" },
    { CWK_EDIT,  40,  5, 5, 32,  "Right" },
    { CWK_BUTTON,36,  8, 8, 200, "OK" },
    { CWK_BUTTON,46,  8, 8, 201, "Cancel" }
};
#define CW_NC_PARA 17

/* --- Search and Replace (Opus dlg/search.des, replace.des) ---------------
 * The real dialog's feature set less the regular expressions, which Word 1.1
 * did not have either: Whole Word, Match Upper/Lowercase, a direction, and -
 * on Replace - Confirm Changes. */
static const struct cw_ctl cw_dc_find[] = {
    { CWK_LABEL,  1,  0, 0, 0,   "Search" },
    { CWK_LABEL,  2,  2, 0, 0,   "Search For:" },
    { CWK_EDIT,  14,  2,28, 40,  "" },
    { CWK_CHECK,  2,  4, 0, 50,  "Whole Word" },
    { CWK_CHECK, 20,  4, 0, 51,  "Match Upper/Lowercase" },
    { CWK_RADIO,  2,  5, 0, 60,  "Down" },
    { CWK_RADIO, 20,  5, 0, 61,  "Up" },
    { CWK_BUTTON,32,  7, 9, 202, "Search" },
    { CWK_BUTTON,43,  7, 8, 201, "Cancel" }
};
#define CW_NC_FIND 9

static const struct cw_ctl cw_dc_repl[] = {
    { CWK_LABEL,  1,  0, 0, 0,   "Replace" },
    { CWK_LABEL,  2,  2, 0, 0,   "Search For:" },
    { CWK_EDIT,  15,  2,26, 40,  "" },
    { CWK_LABEL,  2,  3, 0, 0,   "Replace With:" },
    { CWK_EDIT,  15,  3,26, 41,  "" },
    { CWK_CHECK,  2,  5, 0, 50,  "Whole Word" },
    { CWK_CHECK, 20,  5, 0, 51,  "Match Upper/Lowercase" },
    { CWK_CHECK,  2,  6, 0, 52,  "Confirm Changes" },
    { CWK_BUTTON,30,  8,12, 203, "Replace All" },
    { CWK_BUTTON,44,  8, 8, 201, "Cancel" }
};
#define CW_NC_REPL 10

static const struct cw_ctl cw_dc_goto[] = {
    { CWK_LABEL,  1,  0, 0, 0,   "Go To" },
    { CWK_LABEL,  2,  2, 0, 0,   "Go to page:" },
    { CWK_EDIT,  15,  2, 6, 42,  "" },
    { CWK_BUTTON,10,  4, 8, 204, "OK" },
    { CWK_BUTTON,20,  4, 8, 201, "Cancel" }
};
#define CW_NC_GOTO 5

/* --- the save-changes prompt, which is Word's own wording ---------------- */
static const struct cw_ctl cw_dc_ask[] = {
    { CWK_LABEL,  1,  0, 0, 0,   "Microsoft Word" },
    { CWK_LABEL,  2,  2, 0, 0,   "Do you want to save changes to" },
    { CWK_LABEL,  2,  3, 0, 1,   "" },        /* filled with the name */
    { CWK_BUTTON, 4,  5, 8, 205, "Yes" },
    { CWK_BUTTON,14,  5, 8, 206, "No" },
    { CWK_BUTTON,24,  5, 8, 201, "Cancel" }
};
#define CW_NC_ASK 6

/* --- About: the product, the version, what this port is, and the credit that
 *     has to be on the screen (SPEC.md 73.12).
 *
 * AND NOTHING ELSE. It used to close with two lines about Draft view and the
 * one 8x8 face - a fact about how the build renders, which belongs in SPEC.md
 * and in the greyed items themselves (SPEC.md 47), not in the box a user opens
 * to find out whose software this is. It also went stale the day View > Page
 * and a face off FONTS/ landed (SPEC.md 73.12.1, 67.12.2), which is what a
 * release note in an About box does.
 *
 * TWELVE ROWS, AND THAT NUMBER IS THE 640x200 ADAPTERS' (SPEC.md 39). A
 * control's y is its ROW - ovl_ctl_y() is cw_d_y + 6 + row * 10 - while
 * ovl_dlg_open() clamps only the PANEL to the live content box, so a table
 * with more rows than the window is tall does not shrink: the box stops at the
 * window edge and the last controls carry on past it. This one used to run to
 * nineteen rows, and on CGA and Hercules the OK button was drawn on the
 * DESKTOP, below the window entirely. Twelve is what fits there:
 * 6 + 10 * 10 + 11 = 117 against the ~122 those adapters leave. Adding a row
 * here is a change that looks fine on VGA and is broken on two thirds of the
 * machines this runs on. */
static const struct cw_ctl cw_dc_about[] = {
    { CWK_LABEL,  1,  0, 0, 0, "About" },
    { CWK_LABEL,  4,  2, 0, 0, "Microsoft Word" },
    { CWK_LABEL,  4,  3, 0, 0, "Version 1.1a for os8088" },
    { CWK_LABEL,  4,  5, 0, 0, "A native reimplementation, in C, of the user" },
    { CWK_LABEL,  4,  6, 0, 0, "interface and file format of Word for Windows" },
    { CWK_LABEL,  4,  7, 0, 0, "1.1a. Copyright (C) Microsoft Corporation," },
    { CWK_LABEL,  4,  8, 0, 0, "from the Computer History Museum release, 2014." },
    { CWK_BUTTON,24, 10, 8, 201, "OK" }
};
#define CW_NC_ABOUT 8

/* --- the dialog's live state, all resident bss --------------------------- */
static int  cw_d_n;                     /* controls in the open dialog */
static int  cw_d_focus;
static int  cw_d_x;                     /* the panel, in pixels */
static int  cw_d_y;
static int  cw_d_w;
static int  cw_d_h;
static int  cw_d_attr;                  /* Format Character's working copy */
static int  cw_d_al;                    /* Format Paragraph's */
static int  cw_d_sp;
static int  cw_d_op;
static int  cw_d_li;
static int  cw_d_fi;
static int  cw_d_ri;
static int  cw_d_whole;                 /* Search's */
static int  cw_d_case;
static int  cw_d_back;
static int  cw_d_confirm;
static char cw_d_find[32];
static char cw_d_repl[32];
static char cw_d_page[8];

/* The control table of the open dialog. A POINTER TO const DATA, which is
 * exactly the sort of thing that must not be an overlay function's address -
 * it is data, it is resident, and the module reads it through DS like
 * everything else. */
static const struct cw_ctl *cw_d_c;

/* ==========================================================================
 * DRAWING A PANEL
 * ========================================================================*/

static int ovl_ctl_x(int i) { return cw_d_x + 8 + cw_d_c[i].x * 8; }
static int ovl_ctl_y(int i) { return cw_d_y + 6 + cw_d_c[i].y * 10; }

/* The text a control shows, which for two of them is not in the table: the
 * edits show their buffer and the save prompt shows the document's name. */
static const char *ovl_ctl_text(int i)
{
    int id;

    id = cw_d_c[i].id;
    if (cw_d_c[i].kind == CWK_EDIT) {
        if (id == 40)
            return cw_d_find;
        if (id == 41)
            return cw_d_repl;
        if (id == 42)
            return cw_d_page;
        return cw_num;
    }
    if (cw_d_c[i].kind == CWK_LABEL && id == 1)
        return cw_fname[0] ? cw_fname : "Document";
    return cw_d_c[i].text;
}

/* Is this check box or radio button on? The dialog's working copy answers,
 * never the document: a Cancel has to leave the document alone, so nothing is
 * applied until OK. */
static int ovl_ctl_on(int i)
{
    int id;

    id = cw_d_c[i].id;
    if (cw_d_c[i].kind == CWK_CHECK) {
        if (id == 20)
            return cw_d_op;
        if (id == 50)
            return cw_d_whole;
        if (id == 51)
            return cw_d_case;
        if (id == 52)
            return cw_d_confirm;
        return (cw_d_attr & id) != 0;
    }
    if (cw_d_c[i].kind == CWK_RADIO) {
        if (id < 10)
            return cw_d_al == id;
        if (id < 20)
            return cw_d_sp == id - 10;
        if (id == 60)
            return !cw_d_back;
        if (id == 61)
            return cw_d_back;
    }
    return 0;
}

static void ovl_ctl_draw(int i)
{
    int x;
    int y;
    int w;
    int k;

    x = ovl_ctl_x(i);
    y = ovl_ctl_y(i);
    k = cw_d_c[i].kind;

    if (k == CWK_LABEL) {
        os88_font_run(x, y, ovl_ctl_text(i), OS88_BLACK, OS88_WHITE);
        return;
    }
    if (k == CWK_CHECK || k == CWK_RADIO) {
        os88_set_color(OS88_WHITE);
        os88_gfx_fill(x, y, x + 7, y + 7);
        os88_set_color(OS88_BLACK);
        if (k == CWK_CHECK)
            os88_gfx_frame(x, y, x + 7, y + 7);
        else
            os88_gfx_frame(x + 1, y + 1, x + 6, y + 6);
        if (ovl_ctl_on(i))
            os88_gfx_fill(x + 2, y + 2, x + 5, y + 5);
        os88_font_run(x + 12, y, cw_d_c[i].text, OS88_BLACK, OS88_WHITE);
        return;
    }
    w = cw_d_c[i].w * 8;
    if (k == CWK_EDIT) {
        os88_set_color(OS88_WHITE);
        os88_gfx_fill(x, y - 1, x + w + 3, y + 8);
        os88_set_color(OS88_BLACK);
        os88_gfx_frame(x, y - 1, x + w + 3, y + 8);
        cw_pad(cw_rt, ovl_ctl_text(i), cw_d_c[i].w);
        os88_font_run(x + 2, y, cw_rt, OS88_BLACK, OS88_WHITE);
        return;
    }
    /* a button, and the focused one wears a second frame - which is how a
     * keyboard-driven dialog says which button Enter will press */
    os88_set_color(OS88_WHITE);
    os88_gfx_fill(x, y - 2, x + w, y + 9);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(x, y - 2, x + w, y + 9);
    if (i == cw_d_focus)
        os88_gfx_frame(x + 2, y, x + w - 2, y + 7);
    cw_pad(cw_rt, cw_d_c[i].text, cw_d_c[i].w - 1);
    os88_font_run(x + 4, y, cw_rt, OS88_BLACK, OS88_WHITE);
}

/* The focus ring on anything that is not a button: a rule under it, which is
 * one call and reads correctly on all three adapters. */
static void ovl_focus_mark(int i, int on)
{
    int x;
    int y;
    int w;

    if (i < 0 || cw_d_c[i].kind == CWK_BUTTON || cw_d_c[i].kind == CWK_LABEL)
        return;
    x = ovl_ctl_x(i);
    y = ovl_ctl_y(i);
    w = (cw_d_c[i].kind == CWK_EDIT) ? cw_d_c[i].w * 8 + 4
                                     : (int)os88_strlen(cw_d_c[i].text) * 8 + 12;
    os88_set_color(on ? OS88_BLACK : OS88_WHITE);
    os88_gfx_hline(x, x + w, y + 9);
}

static void ovl_panel(void)
{
    int i;

    os88_set_color(OS88_WHITE);
    os88_gfx_fill(cw_d_x, cw_d_y, cw_d_x + cw_d_w, cw_d_y + cw_d_h);
    os88_set_color(OS88_BLACK);
    os88_gfx_frame(cw_d_x, cw_d_y, cw_d_x + cw_d_w, cw_d_y + cw_d_h);
    os88_gfx_frame(cw_d_x + 2, cw_d_y + 2, cw_d_x + cw_d_w - 2,
                   cw_d_y + cw_d_h - 2);
    for (i = 0; i < cw_d_n; i++)
        ovl_ctl_draw(i);
    ovl_focus_mark(cw_d_focus, 1);

    /* THE PANEL HAS TAKEN THE GLASS, and the shadow must be told: it still
     * describes the text that was under here, and the damage model would
     * happily skip redrawing a cell it believes is already right. Every path
     * that closes a dialog repaints everything (ovl_dlg_end, ovl_ask_end), so
     * this is insurance rather than the mechanism - but it is the cheap kind:
     * two words, and it makes "a dialog is up" indistinguishable from any
     * other reason the glass stopped being ours. */
    cw_sh_ok = 0;
    cw_car_on = 0;
}

/* The first control the keyboard can be on: an edit if there is one, else the
 * first check or radio, else the first button. */
static int ovl_first_focus(void)
{
    int i;

    for (i = 0; i < cw_d_n; i++) {
        if (cw_d_c[i].kind == CWK_EDIT)
            return i;
    }
    for (i = 0; i < cw_d_n; i++) {
        if (cw_d_c[i].kind != CWK_LABEL)
            return i;
    }
    return 0;
}

int ovl_dlg_open(void *win, int which)
{
    int p;
    int rows;

    cw_dlg = which;
    switch (which) {
    case CW_DLG_CHAR:
        cw_d_c = cw_dc_char;  cw_d_n = CW_NC_CHAR;  rows = 11;
        cw_d_attr = cw_rib_attr() & CW_A_CHP;
        break;
    case CW_DLG_PARA:
        cw_d_c = cw_dc_para;  cw_d_n = CW_NC_PARA;  rows = 10;
        p = cw_pap_of(cw_cur);
        cw_d_al = cw_pap_al[p];
        cw_d_sp = cw_pap_sp[p];
        cw_d_op = cw_pap_op[p];
        cw_d_li = cw_pap_li[p];
        cw_d_fi = cw_pap_fi[p];
        cw_d_ri = cw_pap_ri[p];
        break;
    case CW_DLG_FIND:
        cw_d_c = cw_dc_find;  cw_d_n = CW_NC_FIND;  rows = 9;
        break;
    case CW_DLG_REPLACE:
        cw_d_c = cw_dc_repl;  cw_d_n = CW_NC_REPL;  rows = 10;
        break;
    case CW_DLG_GOTO:
        cw_d_c = cw_dc_goto;  cw_d_n = CW_NC_GOTO;  rows = 6;
        cw_d_page[0] = 0;
        break;
    case CW_DLG_ABOUT:
        cw_d_c = cw_dc_about; cw_d_n = CW_NC_ABOUT; rows = 12;
        break;
    default:
        cw_d_c = cw_dc_ask;   cw_d_n = CW_NC_ASK;   rows = 7;
        break;
    }

    /* Sized from the widest control rather than from a constant, and clamped
     * onto the live content box - two adapters of three are 640x200 and a
     * panel authored for 640x480 would hang off both (SPEC.md 39). */
    cw_d_w = 8;
    for (p = 0; p < cw_d_n; p++) {
        int r;

        r = cw_d_c[p].x * 8 + 24;
        if (cw_d_c[p].kind == CWK_EDIT || cw_d_c[p].kind == CWK_BUTTON)
            r = r + cw_d_c[p].w * 8;
        else
            r = r + (int)os88_strlen(cw_d_c[p].text) * 8;
        if (r > cw_d_w)
            cw_d_w = r;
    }
    cw_d_h = rows * 10 + 6;
    if (cw_d_w > cw_sz.w - 8)
        cw_d_w = cw_sz.w - 8;
    if (cw_d_h > cw_sz.h - 8)
        cw_d_h = cw_sz.h - 8;
    cw_d_x = cw_org.x + (cw_sz.w - cw_d_w) / 2;
    cw_d_y = cw_org.y + (cw_sz.h - cw_d_h) / 2;
    if (cw_d_x < cw_org.x + 2)
        cw_d_x = cw_org.x + 2;
    if (cw_d_y < cw_org.y + 2)
        cw_d_y = cw_org.y + 2;

    cw_d_focus = ovl_first_focus();
    cw_caret_off();
    ovl_panel();
    (void)win;
    return 1;
}

int ovl_dlg_paint(void *win)
{
    (void)win;
    if (!cw_dlg)
        return 0;
    ovl_panel();
    return 1;
}

/* ==========================================================================
 * WHAT A DIALOG DOES WHEN IT IS SAID YES TO
 * ========================================================================*/

static int ovl_atoi(const char *s)
{
    int v;
    int i;

    v = 0;
    for (i = 0; s[i] >= '0' && s[i] <= '9'; i++)
        v = v * 10 + (s[i] - '0');
    return v;
}

/* --- the search, which is the one piece of Opus's search.c that ports: the
 * MATCH, without the regular expressions Word 1.1 did not have. Whole Word
 * and Match Upper/Lowercase are the real dialog's two options. */
static int ovl_chr_eq(int a, int b)
{
    if (cw_d_case)
        return a == b;
    if (a >= 'a' && a <= 'z')
        a = a - 32;
    if (b >= 'a' && b <= 'z')
        b = b - 32;
    return a == b;
}

static int ovl_is_word(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

/* out: the offset of the match, or -1. Searches from `from`, in the direction
 * the dialog was set to. */
static int ovl_search(int from)
{
    int n;
    int i;
    int j;
    int step;

    n = (int)os88_strlen(cw_d_find);
    if (n == 0)
        return -1;
    step = cw_d_back ? -1 : 1;
    for (i = from; i >= 0 && i + n <= cw_len; i = i + step) {
        for (j = 0; j < n; j++) {
            if (!ovl_chr_eq(cw_buf[i + j], cw_d_find[j]))
                break;
        }
        if (j < n)
            continue;
        if (cw_d_whole) {
            if (i > 0 && ovl_is_word(cw_buf[i - 1]))
                continue;
            if (i + n < cw_len && ovl_is_word(cw_buf[i + n]))
                continue;
        }
        return i;
    }
    return -1;
}

int ovl_find_again(void *win, int back)
{
    int at;
    int save;

    if (cw_d_find[0] == 0) {
        ovl_dlg_open(win, CW_DLG_FIND);
        return 0;
    }
    save = cw_d_back;
    cw_d_back = back;
    at = ovl_search(back ? cw_cur - 1 : cw_cur + 1);
    cw_d_back = save;
    if (at < 0) {
        cw_toast("Search text not found.");
        return 0;
    }
    cw_cur = at;
    cw_anchor = at + (int)os88_strlen(cw_d_find);
    cw_sel_on = 1;
    cw_show(win, -1, 0, 0);
    return 1;
}

static int ovl_replace_all(void *win)
{
    int at;
    int n;
    int m;
    int i;
    int count;
    int a;

    n = (int)os88_strlen(cw_d_find);
    m = (int)os88_strlen(cw_d_repl);
    if (n == 0)
        return 0;
    count = 0;
    at = 0;
    for (;;) {
        cw_cur = at;
        at = ovl_search(at);
        if (at < 0)
            break;
        a = cw_att[at] & CW_A_CHP;      /* the replacement wears the
                                         * formatting of what it replaced */
        for (i = 0; i < n; i++)
            cw_remove(at);
        cw_cur = at;
        for (i = 0; i < m; i++) {
            if (!cw_insert(cw_d_repl[i], a))
                break;
            cw_cur++;
        }
        at = at + m;
        count++;
        if (count > 2000)               /* a replacement that contains its own
                                         * pattern would otherwise never end */
            break;
    }
    if (count == 0) {
        cw_toast("Search text not found.");
        return 0;
    }
    cw_touch();
    os88_strcpy(cw_tmp, "", sizeof(cw_tmp));
    os88_utoa((unsigned)count, cw_num);
    os88_strcpy(cw_tmp, cw_num, sizeof(cw_tmp));
    os88_strcpy(cw_tmp + os88_strlen(cw_tmp), " replaced",
                sizeof(cw_tmp) - os88_strlen(cw_tmp));
    cw_toast(cw_tmp);
    cw_show(win, -1, 0, 0);
    return count;
}

/* out: 1 = the panel stays up, 0 = it has come down and the caller clears
 * cw_dlg. Everything that closes goes through here, so the repaint that takes
 * the panel off the glass is written once. */
static int ovl_dlg_end(void *win, int ok)
{
    int which;
    int at;

    which = cw_dlg;
    cw_dlg = 0;

    if (ok) {
        switch (which) {
        case CW_DLG_CHAR:
            ovl_chp_apply(win, cw_d_attr, 1);
            ovl_chp_apply(win, (~cw_d_attr) & CW_A_CHP, 3);
            break;
        case CW_DLG_PARA:
            ovl_pap_apply(win, cw_d_al, cw_d_sp, cw_d_op, cw_d_li, cw_d_fi,
                         cw_d_ri);
            break;
        case CW_DLG_FIND:
            cw_redraw_all(win);
            at = ovl_search(cw_d_back ? cw_cur - 1 : cw_cur);
            if (at < 0) {
                cw_toast("Search text not found.");
            } else {
                cw_cur = at;
                cw_anchor = at + (int)os88_strlen(cw_d_find);
                cw_sel_on = 1;
                cw_show(win, -1, 0, 0);
            }
            return 0;
        case CW_DLG_REPLACE:
            cw_redraw_all(win);
            ovl_replace_all(win);
            return 0;
        case CW_DLG_GOTO:
            at = ovl_atoi(cw_d_page);
            if (at < 1)
                at = 1;
            at = at - 1;
            /* 54 lines to the page, reached by shifts: `imul r, r, imm` by a
             * non-power of two needs a scratch register to hold the
             * multiplicand, and tools/cc8086.py refuses the site when it
             * cannot prove one is dead - correctly, and it named this line.
             * 54 = 32 + 16 + 4 + 2, and four shifts and three adds are
             * cheaper on an 8088 than the multiply would have been anyway. */
            at = (at << 5) + (at << 4) + (at << 2) + (at << 1);
            cw_cur = 0;
            for (; at > 0 && cw_cur < cw_len; cw_cur++) {
                if (cw_buf[cw_cur] == '\n')
                    at--;
            }
            cw_sel_on = 0;
            break;
        default:
            break;
        }
    }
    cw_redraw_all(win);
    return 0;
}

/* The save-changes prompt's three answers, which are the whole of why it
 * exists: Yes saves and then does what the user actually asked for, No throws
 * the changes away and does it, Cancel does neither. */
static int ovl_ask_end(void *win, int answer)
{
    int which;
    int act;

    which = cw_dlg;
    cw_dlg = 0;
    act = (which == CW_DLG_SAVE_NEW) ? CWA_NEW :
          (which == CW_DLG_SAVE_OPEN) ? CWA_OPEN : CWA_EXIT;

    cw_redraw_all(win);
    if (answer == 0)                    /* Cancel */
        return 0;
    if (answer == 1) {                  /* Yes: save, then continue */
        cw_pending = act;
        if (cw_fname[0] == 0) {
            os88_file_dlg(OS88_FDLG_SAVE, win, "DOC.RTF");
            return 0;
        }
        if (ovl_file_store(win, cw_fname)) {
            cw_pending = 0;
            cw_mod = 0;
            cw_do(win, act);
        }
        return 0;
    }
    cw_mod = 0;                         /* No */
    cw_do(win, act);
    return 0;
}

/* ==========================================================================
 * THE DIALOG'S INPUT
 * ========================================================================*/

static char *ovl_edit_buf(int i)
{
    int id;

    id = cw_d_c[i].id;
    if (id == 40)
        return cw_d_find;
    if (id == 41)
        return cw_d_repl;
    if (id == 42)
        return cw_d_page;
    return 0;
}

static void ovl_toggle(int i)
{
    int id;

    id = cw_d_c[i].id;
    if (cw_d_c[i].kind == CWK_CHECK) {
        if (id == 20)
            cw_d_op = !cw_d_op;
        else if (id == 50)
            cw_d_whole = !cw_d_whole;
        else if (id == 51)
            cw_d_case = !cw_d_case;
        else if (id == 52)
            cw_d_confirm = !cw_d_confirm;
        else
            cw_d_attr = cw_d_attr ^ id;
        ovl_ctl_draw(i);
        return;
    }
    if (cw_d_c[i].kind == CWK_RADIO) {
        int j;

        if (id < 10)
            cw_d_al = id;
        else if (id < 20)
            cw_d_sp = id - 10;
        else
            cw_d_back = (id == 61);
        /* the whole group repaints, because turning one on turns another off
         * and the one that went off is the pixel nobody would have redrawn */
        for (j = 0; j < cw_d_n; j++) {
            if (cw_d_c[j].kind == CWK_RADIO &&
                ((cw_d_c[j].id < 10) == (id < 10)) &&
                ((cw_d_c[j].id < 20) == (id < 20)))
                ovl_ctl_draw(j);
        }
    }
}

static int ovl_button(void *win, int id)
{
    switch (id) {
    case 200:                           /* OK */
    case 202:                           /* Search */
    case 204:                           /* Go To's OK */
        return ovl_dlg_end(win, 1);
    case 203:                           /* Replace All */
        return ovl_dlg_end(win, 1);
    case 205:
        return ovl_ask_end(win, 1);
    case 206:
        return ovl_ask_end(win, 2);
    default:                            /* Cancel */
        if (cw_dlg >= CW_DLG_SAVE_NEW)
            return ovl_ask_end(win, 0);
        return ovl_dlg_end(win, 0);
    }
}

int ovl_dlg_click(void *win, int x, int y)
{
    int i;
    int cx;
    int cy;
    int w;

    for (i = 0; i < cw_d_n; i++) {
        if (cw_d_c[i].kind == CWK_LABEL)
            continue;
        cx = ovl_ctl_x(i);
        cy = ovl_ctl_y(i);
        w = (cw_d_c[i].kind == CWK_EDIT || cw_d_c[i].kind == CWK_BUTTON)
            ? cw_d_c[i].w * 8 + 4
            : (int)os88_strlen(cw_d_c[i].text) * 8 + 12;
        if (x < cx || x > cx + w || y < cy - 2 || y > cy + 9)
            continue;
        if (cw_d_c[i].kind == CWK_BUTTON)
            return ovl_button(win, cw_d_c[i].id);
        ovl_focus_mark(cw_d_focus, 0);
        cw_d_focus = i;
        ovl_focus_mark(cw_d_focus, 1);
        ovl_toggle(i);
        return 1;
    }
    return 1;                           /* a click on the panel's own frame:
                                         * modal means it goes nowhere else */
}

int ovl_dlg_key(void *win, int ascii, int scan)
{
    char *b;
    int n;
    int i;

    if (scan == 0x01) {                 /* Esc = Cancel, always */
        if (cw_dlg >= CW_DLG_SAVE_NEW)
            return ovl_ask_end(win, 0);
        return ovl_dlg_end(win, 0);
    }
    if (ascii == 13) {                  /* Enter presses the focused button,
                                         * or the dialog's default one */
        if (cw_d_c[cw_d_focus].kind == CWK_BUTTON)
            return ovl_button(win, cw_d_c[cw_d_focus].id);
        for (i = 0; i < cw_d_n; i++) {
            if (cw_d_c[i].kind == CWK_BUTTON && cw_d_c[i].id != 201)
                return ovl_button(win, cw_d_c[i].id);
        }
        return ovl_dlg_end(win, 1);
    }
    if (ascii == 9 || scan == CW_K_DOWN || scan == CW_K_UP) {
        int step;

        step = (scan == CW_K_UP) ? -1 : 1;
        ovl_focus_mark(cw_d_focus, 0);
        for (;;) {
            cw_d_focus = cw_d_focus + step;
            if (cw_d_focus < 0)
                cw_d_focus = cw_d_n - 1;
            if (cw_d_focus >= cw_d_n)
                cw_d_focus = 0;
            if (cw_d_c[cw_d_focus].kind != CWK_LABEL)
                break;
        }
        ovl_focus_mark(cw_d_focus, 1);
        for (i = 0; i < cw_d_n; i++) {
            if (cw_d_c[i].kind == CWK_BUTTON)
                ovl_ctl_draw(i);        /* the focus frame moved */
        }
        return 1;
    }
    if (ascii == ' ' && (cw_d_c[cw_d_focus].kind == CWK_CHECK ||
                         cw_d_c[cw_d_focus].kind == CWK_RADIO)) {
        ovl_toggle(cw_d_focus);
        return 1;
    }
    if (cw_d_c[cw_d_focus].kind == CWK_EDIT) {
        b = ovl_edit_buf(cw_d_focus);
        if (b == 0)
            return 1;
        n = (int)os88_strlen(b);
        if (ascii == 8) {
            if (n > 0)
                b[n - 1] = 0;
            ovl_ctl_draw(cw_d_focus);
            return 1;
        }
        if (ascii >= 32 && ascii <= 126 && n < cw_d_c[cw_d_focus].w &&
            n < 30) {
            b[n] = (char)ascii;
            b[n + 1] = 0;
            ovl_ctl_draw(cw_d_focus);
        }
        return 1;
    }
    return 1;
}

/* ==========================================================================
 * THE FILE FORMAT
 *
 * RTF, read and written by cwrtfio.c through tables transcribed out of Opus's
 * RTFOUT.C and RTFIN.C. Both directions are out here because both are large
 * and both run once per command.
 * ========================================================================*/

int ovl_file_load(void *win, const char *name, unsigned lo, unsigned hi)
{
    int r;

    /* REFUSE FROM THE SIZE THE DIALOG ALREADY KNOWS, before touching the
     * drive: 116KB off a floppy is ten seconds of motor, and a refusal after
     * the read is indistinguishable to the user from a load that worked. */
    if (hi != 0 || lo > (unsigned)CW_RTF_MAX) {
        cw_toast("That file is too large for this program.");
        return 0;
    }
    cw_rtfn = (int)os88_file_read(name, cw_rtf, (unsigned)CW_RTF_MAX);
    /* ZERO BYTES IS AN EMPTY FILE OR A FAILURE, and os88_ferr() is what tells
     * them apart (os88.h) - not `lo`, which the ASSOCIATION path has none of
     * and passes as 0.  Written against `lo` this arm was dead for a document
     * opened by double-click: a read that failed fell into ovl_rtf_parse(0),
     * which answers CW_RTF_NOTRTF for anything under five bytes, and the user
     * was told their RTF was not an RTF file. */
    if (cw_rtfn == 0 && os88_ferr() != OS88_FERR_OK) {
        cw_toast("Could not read that file.");
        return 0;
    }
    r = ovl_rtf_parse(cw_rtfn);
    if (r == CW_RTF_NOTRTF) {
        cw_toast("That is not an RTF file.");
        return 0;
    }
    if (r == CW_RTF_TRUNC)
        cw_toast("The document was truncated to fit.");
    else if (r == CW_RTF_DEEP)
        cw_toast("That file nests too deeply.");

    cw_cur = 0;
    cw_top = 0;
    cw_sel_on = 0;
    cw_ext = 0;
    cw_mod = 0;
    os88_strcpy(cw_fname, name, sizeof(cw_fname));
    ovl_retitle(win);
    return 1;
}

int ovl_file_store(void *win, const char *name)
{
    int r;

    if (ovl_rtf_build() < 0 || cw_ovf) {
        cw_toast("This document is too large to save as RTF.");
        return 0;
    }
    r = os88_file_write(name, cw_rtf, (unsigned)cw_rtfn);
    if (r != 0) {
        cw_toast("Could not write that file.");
        return 0;
    }
    os88_strcpy(cw_fname, name, sizeof(cw_fname));
    cw_mod = 0;
    ovl_retitle(win);
    cw_toast("Saved.");
    return 1;
}

/* ==========================================================================
 * THE UTILITIES (Opus sort.c, renum.c, toc.c - the shapes, not the code)
 *
 * 0 = Sort, 1 = Renumber, 2 = Table of Contents. Each works on paragraphs,
 * which is what makes all three affordable here: a paragraph is a run between
 * two marks and the document is one array.
 * ========================================================================*/

/* The heading of the list itself, inserted after it so that it ends up above
 * it - inserting at 0 twice puts the second one first. The first eight
 * characters are bold, which is the whole of the styling this port has. */
static const char cw_toc_head[] = "Contents\n";

static int ovl_para_cmp(int a, int b)
{
    int i;
    int j;

    i = a;
    j = b;
    for (;;) {
        int ca;
        int cb;

        ca = (i < cw_len && cw_buf[i] != '\n') ? cw_buf[i] : 0;
        cb = (j < cw_len && cw_buf[j] != '\n') ? cw_buf[j] : 0;
        if (ca >= 'a' && ca <= 'z')
            ca = ca - 32;
        if (cb >= 'a' && cb <= 'z')
            cb = cb - 32;
        if (ca != cb)
            return ca - cb;
        if (ca == 0)
            return 0;
        i++;
        j++;
    }
}

int ovl_util(void *win, int which)
{
    int a;
    int b;
    int i;
    int m;
    int n;
    int p;
    int q;
    int swapped;
    int guard;

    if (cw_sel_on) {
        cw_sel_bounds();
        a = cw_para_start(cw_w_end);
        b = cw_w_next;
    } else {
        a = 0;
        b = cw_len;
    }

    if (which == 0) {
        /* SORT: a bubble sort over paragraphs, swapping whole paragraphs
         * through cw_rtf[] as the scratch. It is O(n^2) in paragraphs and
         * that is the right trade here - a selection is tens of paragraphs,
         * the comparison is the expensive part, and a merge sort would need a
         * second array of the document's size out of a 60KB ceiling. */
        guard = 0;
        for (;;) {
            swapped = 0;
            p = a;
            for (;;) {
                q = p;
                while (q < b && cw_buf[q] != '\n')
                    q++;
                if (q >= b)
                    break;
                q++;                    /* q = the next paragraph's start */
                if (q >= b)
                    break;
                if (ovl_para_cmp(p, q) > 0) {
                    int qe;

                    qe = q;
                    while (qe < b && cw_buf[qe] != '\n')
                        qe++;
                    if (qe < b)
                        qe++;
                    n = qe - q;
                    if (n > CW_RTF_MAX)
                        break;
                    for (i = 0; i < n; i++) {
                        cw_rtf[i] = cw_buf[q + i];
                        cw_rtf[CW_RTF_MAX - 1 - i] = (char)cw_att[q + i];
                    }
                    cw_memmove(cw_buf + p + n, cw_buf + p, (unsigned)(q - p));
                    cw_memmove(cw_att + p + n, cw_att + p, (unsigned)(q - p));
                    for (i = 0; i < n; i++) {
                        cw_buf[p + i] = cw_rtf[i];
                        cw_att[p + i] = (unsigned char)cw_rtf[CW_RTF_MAX - 1 - i];
                    }
                    swapped = 1;
                }
                p = q;
            }
            if (!swapped)
                break;
            if (++guard > 400)
                break;
        }
        cw_toast("Sorted.");
    } else if (which == 1) {
        /* RENUMBER: "1." at the head of every paragraph in the range, counting
         * up. An existing number is replaced rather than added to, which is
         * what makes running it twice idempotent. */
        n = 1;
        p = a;
        while (p < b && p < cw_len) {
            i = p;
            while (i < cw_len && cw_buf[i] >= '0' && cw_buf[i] <= '9')
                i++;
            if (i > p && i < cw_len && cw_buf[i] == '.') {
                i++;
                if (i < cw_len && cw_buf[i] == ' ')
                    i++;
                while (i > p) {
                    cw_remove(p);
                    i--;
                    b--;
                }
            }
            os88_utoa((unsigned)n, cw_num);
            os88_strcpy(cw_tmp, cw_num, sizeof(cw_tmp));
            i = (int)os88_strlen(cw_tmp);
            cw_tmp[i] = '.';
            cw_tmp[i + 1] = ' ';
            cw_tmp[i + 2] = 0;
            q = cw_cur;
            cw_cur = p;
            for (i = 0; cw_tmp[i] != 0; i++) {
                if (!cw_insert(cw_tmp[i], 0))
                    break;
                cw_cur++;
                b++;
            }
            cw_cur = q;
            n++;
            while (p < cw_len && cw_buf[p] != '\n')
                p++;
            p++;
            if (n > 999)
                break;
        }
        cw_toast("Renumbered.");
    } else {
        /* TABLE OF CONTENTS. With no styles in the document there is nothing
         * to ask which paragraphs are headings, so the heuristic is the one a
         * plain document can offer: a SHORT paragraph that does not end in a
         * full stop is a heading. Opus's toc.c walks style handles; this walks
         * shape, and it says so in the toast so nobody reads more into the
         * list than is in it.
         *
         * The list goes in at the TOP, which is where a table of contents
         * belongs, and it is built in cw_rtf[] first: inserting as we scan
         * would move the very text being scanned. */
        n = 0;
        q = cw_cur;
        for (i = 0; i < cw_len && n < CW_RTF_MAX - 80; i++) {
            int e;

            if (i != 0 && cw_buf[i - 1] != '\n')
                continue;
            e = i;
            while (e < cw_len && cw_buf[e] != '\n')
                e++;
            if (e - i < 3 || e - i > 40 || cw_buf[e - 1] == '.')
                continue;
            for (p = i; p < e; p++)
                cw_rtf[n++] = cw_buf[p];
            cw_rtf[n++] = '\n';
        }
        if (n == 0) {
            cw_toast("No headings found - a heading is a short paragraph.");
            return 0;
        }

        /* THE HEADING FIRST AND THE LIST AFTER IT, in one forward pass from
         * offset 0. Inserting the list at 0 and then the heading wherever the
         * caret had got to put `Contents` UNDER the list, which is not a table
         * of contents at all - and it is the sort of thing that reads as a
         * mangled document rather than as a wrong offset. */
        cw_cur = 0;
        m = 0;
        for (i = 0; cw_toc_head[i] != 0; i++) {
            if (!cw_insert(cw_toc_head[i], (cw_toc_head[i] == '\n') ? 0
                                                                    : CW_A_BOLD))
                break;
            cw_cur++;
            m++;
        }
        for (i = 0; i < n; i++) {
            if (!cw_insert(cw_rtf[i], 0))
                break;
            cw_cur++;
            m++;
        }
        cw_cur = q + m;                 /* the caret was below all of this, and
                                         * m characters went in above it */
        if (cw_cur > cw_len)
            cw_cur = cw_len;
        cw_toast("Table of contents inserted at the top.");
    }

    cw_touch();
    cw_show(win, -1, 0, 0);
    return 1;
}
