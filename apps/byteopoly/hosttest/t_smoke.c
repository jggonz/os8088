/* Byteopoly host harness smoke (lead): the OS-free subset compiles on the
 * host and the immutable tables match SPEC.md 131.2.1. Lanes add t_rules.c,
 * t_save.c and t_link.c beside it. */
#include "bphost.h"
#include "byteoply.c"
/* The UI hooks the session calls (bp.h BP_LINK). Weak, so the link lane's
 * own host no-ops (bp_link.c under BP_HOST) win without a clash. */
__attribute__((weak)) void bp_ui_committed(const BPResult *r) { (void)r; }
__attribute__((weak)) void bp_ui_link(void) {}
__attribute__((weak)) void bp_ui_chat(int machine, const char *text) { (void)machine; (void)text; }
int main(void)
{
    int s, n = 0, p;
    for (s = 0; s < BP_SPACES; s++) {
        p = bp_space(s)->prop;
        if (p != BP_NOBODY) { n++; BP_CHECK(bp_prop_space(p) == s, "prop<->space"); }
    }
    BP_CHECK(n == BP_PROPS, "18 purchasable");
    BP_CHECK(bp_space(0)->kind == BP_K_BOOT && bp_space(7)->kind == BP_K_REPAIR, "corners");
    BP_CHECK(bp_space(14)->kind == BP_K_SWAP && bp_space(21)->kind == BP_K_CRASH, "corners");
    BP_CHECK(bp_card(15)->effect == BP_E_KEEP && bp_card(6)->effect == BP_E_KEEP, "disks");
    printf("t_smoke: %s\n", bp_host_fail ? "FAILED" : "ok");
    return bp_host_fail ? 1 : 0;
}
