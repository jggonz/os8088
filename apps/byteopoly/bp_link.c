/* Byteopoly - the session: local play, the banker host, the client, the
 * cable protocol and recovery (SPEC.md 131.5, 131.6). Lane: link.
 * FOUNDATION STUB: local pass-through only. The link lane replaces it. */
BPState bp_game;
BPByte bp_mode, bp_here;
BPDiag bp_diag;
static BPResult bp_sess_res;
static BPU32 bp_sess_seed;
void bp_sess_local(void) { bp_mode = BP_MODE_LOCAL; bp_here = 0; }
int bp_sess_new(const BPSetup *su)
{
    int e;
    bp_u32_set(&bp_sess_seed, os88_ticks(), 0x5A5A);
    e = bp_new_match(&bp_game, su, &bp_sess_seed, &bp_sess_res);
    if (e == BP_OK) bp_ui_committed(&bp_sess_res);
    return e;
}
int bp_sess_submit(const BPCmd *c)
{
    int e;
    e = bp_apply(&bp_game, c, &bp_sess_res);
    if (e == BP_OK) bp_ui_committed(&bp_sess_res);
    return e;
}
void bp_sess_tick(void) {}
int bp_sess_mine(int seat) { (void)seat; return 1; }
int bp_sess_busy(void) { return 0; }
int bp_link_state(void) { return BP_L_OFF; }
const char *bp_link_msg(void) { return "Same PC"; }
int bp_link_host(int port) { (void)port; return BP_EIO; }
int bp_link_join(int port) { (void)port; return BP_EIO; }
void bp_link_leave(void) {}
int bp_lobby_set(const BPSetup *su) { (void)su; return BP_EIO; }
int bp_lobby_ready(int ready) { (void)ready; return BP_EIO; }
int bp_lobby_start(void) { return BP_EIO; }
const BPSetup *bp_lobby_setup(void) { return 0; }
int bp_chat_send(const char *text) { (void)text; return BP_EIO; }
int bp_sess_save_pair(void) { return BP_EIO; }
int bp_sess_resume_pair(void) { return BP_EIO; }
int bp_sess_load_local(const BPState *loaded) { (void)loaded; return BP_EIO; }
/* the bridge's guest body is bp_cable.inc; until the link lane lands it,
 * a closed port */
#ifndef BP_HOST
int bp_ck_open(int port) { (void)port; return BP_EIO; }
void bp_ck_close(void) {}
void bp_ck_tick(void) {}
int bp_ck_heard(void) { return 0; }
int bp_ck_send(const BPByte *body, unsigned n) { (void)body; (void)n; return BP_EIO; }
int bp_ck_recv(BPByte *body) { (void)body; return 0; }
int bp_ck_port(void) { return 0; }
const char *bp_ck_msg(void) { return "Off"; }
#endif
