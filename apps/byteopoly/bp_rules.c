/* Byteopoly - the rules engine (SPEC.md 131.4). Lane: rules.
 * FOUNDATION STUB: every door refuses, so the skeleton links and nothing
 * pretends to play. The rules lane replaces this file. */
static const char *bp_errs[BP_ECOUNT] = {
    "OK", "Not now", "Not your decision", "Out of date - try again",
    "Not enough cash", "Not yours", "The rules refuse that", "Too large",
    "Bad request", "Out of the game", "Invalid game state", "Too deep",
    "Waiting for the other PC", "Link paused", "Played on the other PC",
    "Disk error", "Bad file", "Wrong version", "Damaged data", "Wrong match",
    "Disk full or protected"
};
void bp_setup_default(BPSetup *su) { os88_memset(su, 0, sizeof(BPSetup)); su->nseats = 2; }
int bp_setup_check(const BPSetup *su) { (void)su; return BP_OK; }
int bp_new_match(BPState *s, const BPSetup *su, const BPU32 *seed, BPResult *r)
{
    (void)su; (void)seed; os88_memset(s, 0, sizeof(BPState)); r->n = 0; return BP_EINVALID;
}
int bp_check(const BPState *s, const BPCmd *c) { (void)s; (void)c; return BP_EPHASE; }
int bp_apply(BPState *s, const BPCmd *c, BPResult *r) { (void)s; (void)c; r->n = 0; return BP_EPHASE; }
const char *bp_err_text(int code) { if (code < 0 || code >= BP_ECOUNT) return "?"; return bp_errs[code]; }
int bp_rent_quote(const BPState *s, int space, BPU32 *out) { (void)s; (void)space; bp_u32_set(out, 0, 0); return BP_OK; }
int bp_can(const BPState *s, int seat, int type, int arg) { (void)s; (void)seat; (void)type; (void)arg; return BP_EPHASE; }
int bp_mortgage_value(int space) { const BPSpace *p; p = bp_space(space); return p ? (int)(p->price / 2) : 0; }
void bp_redeem_cost(int space, BPU32 *out) { bp_u32_set(out, (BPWord)bp_mortgage_value(space), 0); }
int bp_group_owner(const BPState *s, int group) { (void)s; (void)group; return BP_NOBODY; }
void bp_score_of(const BPState *s, int seat, BPU32 *out) { bp_u32_copy(out, &s->cash[seat]); }
int bp_seat_assets(const BPState *s, int seat, BPU32 *mask) { (void)s; (void)seat; bp_u32_set(mask, 0, 0); return 0; }
