/* Byteopoly - the in-game panels (SPEC.md 131.9.7-131.9.11). Lane: panels.
 * FOUNDATION STUB: no panel; the panels lane replaces this file. */
static const char *bp_gp_hints0[] = {"Arrows Select", "F1 Rules", 0};
void bp_gp_draw(void) {}
int bp_gp_covers(void) { return 0; }
int bp_gp_key(int ascii, int scan) { (void)ascii; (void)scan; return 0; }
int bp_gp_click(int x, int y) { (void)x; (void)y; return 0; }
const char *bp_gp_hint(int i) { if (i < 0 || i > 1) return 0; return bp_gp_hints0[i]; }
void bp_gp_sync(void) {}
void bp_gp_open(int which) { (void)which; }
