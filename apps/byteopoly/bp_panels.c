/* Byteopoly - the centre's panels and the hint bar (SPEC.md 131.9).
 * Lane: panels. FOUNDATION STUB: no panel; the panels lane replaces it. */
static const char *bp_pn_hints0[] = {"Arrows Select", "F1 Rules", 0};
void bp_pn_draw(void) {}
int bp_pn_covers(void) { return 0; }
int bp_pn_key(int ascii, int scan) { (void)ascii; (void)scan; return 0; }
int bp_pn_click(int x, int y) { (void)x; (void)y; return 0; }
const char *bp_pn_hint(int i) { if (i < 0 || i > 1) return 0; return bp_pn_hints0[i]; }
void bp_pn_sync(void) {}
