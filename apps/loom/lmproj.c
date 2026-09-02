/* ============================================================================
 * os8088 - apps/loom/lmproj.c
 *
 * THE PROJECT (WEAVE-SPEC 11.2), THE THREE CLAIMS (11.4), THE FILE SWITCHER
 * (13.1) AND SPEC.md 19.9'S PREFERENCES. #included by apps/loom/loom.c.
 *
 * ---------------------------------------------------------------------------
 * WHAT A PROJECT IS
 * ---------------------------------------------------------------------------
 * A folder holding `MAIN.WML` (required) plus `MAIN.WJS` (iff a <script>
 * names it), `SHEET.WFX` (iff a grid has initial cells) and `SPRITES.WSP`
 * (iff sprites). LOOM opens the `.WML`; the companions are found BESIDE IT,
 * by the `.WML`'s own stem first (`FORM.WJS` beside `FORM.WML`) and then by
 * 11.2's project spellings.
 *
 * THAT ORDER IS NOT A CONVENIENCE. It is exactly what tools/weavesim.py's
 * pack_project() does, and the two packers must agree about WHICH FILE THEY
 * READ or WEAVE-SPEC 11.1's byte-identity gate compares two different
 * projects and reports a difference that is in neither compiler.
 *
 * ---------------------------------------------------------------------------
 * WHERE THE BYTES LIVE
 * ---------------------------------------------------------------------------
 * THREE CLAIMS, and none of them is in this package's segment because none of
 * them could be (SPEC.md 20.1 gives the whole program 61,440 bytes for image
 * AND bss):
 *
 *   the SOURCE claim   LM_CLAIMKB (29KB), held while a project is open. Four
 *                      slots of LM_TEXTMAX at slot * LM_TEXTMAX, which is
 *                      also slot * 384 PARAGRAPHS - so a slot has a segment
 *                      address of its own and os88_file_read_seg() can read a
 *                      file straight into one with no staging buffer at all.
 *                      Past the four slots, at LM_SHBASE, the EDITOR'S GLASS
 *                      SHADOW rides in the same claim: 4,576 bytes that would
 *                      otherwise be 7.5% of SPEC.md 20.1's whole allowance
 *                      for image and bss together (loom.c says why there).
 *   the SCRATCH claim  LM_WORKKB, TRANSIENT: taken when Pack starts and freed
 *                      when it ends (11.4). Every compiler table is a byte
 *                      offset into it (loom.h says why at length).
 *   the OUTPUT claim   LM_OUTKB, TRANSIENT for the same reason and in the
 *                      same bracket: it holds the finished `.WAB`.
 *
 * The two transient ones are 112KB together and they are taken BEFORE ANY
 * I/O, with WEAVE-SPEC 10.1's arithmetic in the refusal - which is
 * apps/weave/wload.c's rule applied to the other direction: WEAVE refuses
 * before it READS a bundle, LOOM refuses before it WRITES one.
 * ==========================================================================*/

/* --- the claims ---------------------------------------------------------- */
static unsigned lm_srcseg;              /* 0 = no project is open */
static unsigned lm_workseg;             /* 0 = Pack is not running */
static unsigned lm_outseg;

#define LM_SLOTPARA  (LM_TEXTMAX / 16)  /* 384 - a slot's base, in paragraphs */

/* --- the four slots ------------------------------------------------------ */
static unsigned      lm_slen[LM_NSLOT];   /* bytes of text in each */
static unsigned char lm_shave[LM_NSLOT];  /* the project HAS this file */
static unsigned char lm_smod[LM_NSLOT];   /* ...and it is modified */
static int           lm_slot;             /* the one being edited */

static char              lm_stem[10];     /* the .WML's stem, for the .WAB */
static struct os88_place lm_projplace;    /* where the project folder is */
static int               lm_projhave;

/* ============================================================================
 * THE CLAIM ACCESSORS (loom.h's contract)
 *
 * These are the machine's half of what apps/loom/hosttest/lmhost.c stands up
 * as three plain arrays, and that pairing is the whole point: the compilers
 * are written against this surface and can therefore be diffed against
 * tools/weavesim.py in a second rather than in a boot (WEAVE-SPEC 12.4).
 *
 * EVERY ONE GOES THROUGH apps/weave/wblob.inc, unchanged and un-copied
 * (WEAVE-SPEC 1.2's sharing rule, SPEC.md 20.5.1). Two readers of one claim
 * that can disagree about endianness is the failure 11's byte-identity rule
 * exists to prevent, said about code instead of about bundles.
 *
 * THE BOUND TEST IS DEFENCE IN DEPTH AND NOT THE CONTRACT. The compilers own
 * their bounds and each has a refusal sentence for its own region (loom.h's
 * LM_MAX* list); the host harness exits(3) loudly on an overrun so the defect
 * is found there. What these tests buy on the machine is that a compiler bug
 * writes nothing rather than writing into the claim the heap gave somebody
 * else, which on this machine does not fault (LESSONS.md 4).
 * ==========================================================================*/

unsigned lm_wb(unsigned off)
{
    if (lm_workseg == 0 || off >= LMW_END)
        return 0;
    return w_b(lm_workseg, off);
}

void lm_wpb(unsigned off, unsigned v)
{
    if (lm_workseg == 0 || off >= LMW_END)
        return;
    w_pb(lm_workseg, off, v);
}

unsigned lm_ww(unsigned off)
{
    if (lm_workseg == 0 || off + 1 >= LMW_END)
        return 0;
    return w_w(lm_workseg, off);
}

void lm_wpw(unsigned off, unsigned v)
{
    if (lm_workseg == 0 || off + 1 >= LMW_END)
        return;
    w_pw(lm_workseg, off, v);
}

void lm_wfill(unsigned off, unsigned v, unsigned n)
{
    if (lm_workseg == 0 || off + n > LMW_END)
        return;
    lm_sfill(lm_workseg, off, v, n);
}

unsigned lm_ob(unsigned off)
{
    if (lm_outseg == 0 || off >= W_CAP)
        return 0;
    return w_b(lm_outseg, off);
}

void lm_opb(unsigned off, unsigned v)
{
    if (lm_outseg == 0 || off >= W_CAP)
        return;
    w_pb(lm_outseg, off, v);
}

void lm_opw(unsigned off, unsigned v)
{
    if (lm_outseg == 0 || off + 1 >= W_CAP)
        return;
    w_pw(lm_outseg, off, v);
}

void lm_ofill(unsigned off, unsigned v, unsigned n)
{
    if (lm_outseg == 0 || off + n > W_CAP)
        return;
    lm_sfill(lm_outseg, off, v, n);
}


unsigned lm_srclen(int slot)
{
    if (slot < 0 || slot >= LM_NSLOT || !lm_shave[slot])
        return 0;
    return lm_slen[slot];
}

/* lm_sb - ONE BYTE OF A SOURCE, and 0 PAST THE END. The parsers use that zero
 * as EOF (loom.h says so), so it is a contract and not a convenience: a
 * scanner that ran off the end of a slot would otherwise read the next slot's
 * first byte, which is a perfectly valid character and would make a `.WJS`
 * finish inside a `.WFX`. */
unsigned lm_sb(int slot, unsigned off)
{
    if (lm_srcseg == 0 || slot < 0 || slot >= LM_NSLOT)
        return 0;
    if (!lm_shave[slot] || off >= lm_slen[slot])
        return 0;
    return w_b(lm_srcseg, (unsigned) slot * LM_TEXTMAX + off);
}

/* lm_sbase - a slot's base, in paragraphs. os88_file_read_seg() and
 * os88_file_write_seg() take a SEGMENT and read or write from its offset 0,
 * so a slot with a segment address of its own needs no staging buffer - and a
 * 6KB staging buffer is 10% of this package's whole allowance. LM_TEXTMAX is
 * a multiple of 16 precisely so that this arithmetic is exact. */
static unsigned lm_sbase(int slot)
{
    return lm_srcseg + (unsigned) slot * LM_SLOTPARA;
}

/* ============================================================================
 * THE SLOT NAMES
 *
 * lmerr.c owns the names (lm_fname / lm_setfname) because WEAVE-SPEC 10.5's
 * sentences carry them, and the name a slot was ACTUALLY read from is what
 * has to appear - `FORM.WJS:12:` and not `MAIN.WJS:12:` for a project whose
 * script is named after its stem. So every reader below calls lm_setfname().
 * ==========================================================================*/

static const char *lm_slotext(int slot)
{
    if (slot == LM_SLOT_WML)
        return "WML";
    if (slot == LM_SLOT_WJS)
        return "WJS";
    if (slot == LM_SLOT_WFX)
        return "WFX";
    return "WSP";
}

/* lm_samename - a case-insensitive 8.3 compare, because the two sources
 * disagree: tools/os88disk.py uppercases every name it writes and a host OS
 * that rewrote the disk may not have (apps/weave/wload.c's w_samename, and
 * the same sentence is true here). */
static int lm_samename(const char *a, const char *b)
{
    int i, ca, cb;

    for (i = 0; i < 13; i++) {
        ca = a[i];
        cb = b[i];
        if (ca >= 'a' && ca <= 'z')
            ca -= 32;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
    }
    return 1;
}

/* lm_mkname - `<stem>.<ext>` into lm_line, 8.3 and uppercase. */
static void lm_mkname(const char *stem, const char *ext)
{
    lm_l0();
    lm_ls(stem);
    lm_ls(".");
    lm_ls(ext);
}

/* ============================================================================
 * READING AND WRITING A SLOT
 *
 * Both are RESIDENT and neither is `ovl_*`, which is a decision rather than an
 * oversight. Save is reachable from three places - the menu, `^S`, and the
 * close guard's alert - and the last of those is the one that matters: a Save
 * that could not happen because LOOM.OVL had gone missing would lose the
 * user's work at the exact moment the program had just promised not to.
 * SPEC.md 73.14's split is by FREQUENCY, and the frequency that decides this
 * one is "once, at the worst possible moment".
 * ==========================================================================*/

/* lm_readslot - `name` into slot, whole. 1 = it is there and read, 0 = it is
 * not (which for a companion is the ordinary case and not an error).
 *
 * A FILE OVER LM_TEXTMAX IS REFUSED AND NOT TRUNCATED. os88_file_read_seg()
 * decides FERR_BIG from the DIRECTORY ENTRY before any data I/O - so the
 * refusal costs no motor time - and loom.h's own comment says why truncating
 * would be worse than refusing: a truncated source packs a bundle nobody
 * wrote. */
static int lm_readslot(int slot, const char *name)
{
    unsigned n;

    lm_slen[slot] = 0;
    lm_shave[slot] = 0;
    lm_smod[slot] = 0;
    n = os88_file_read_seg(name, lm_sbase(slot), LM_TEXTMAX);
    if (n == 0 && os88_ferr() != OS88_FERR_OK) {
        if (os88_ferr() == OS88_FERR_BIG) {
            lm_l0();
            lm_ls(name);
            lm_ls(" is over ");
            lm_ln(LM_TEXTMAX);
            lm_ls(" bytes; LOOM will not truncate a source.");
            lm_say(lm_line);
        }
        return 0;
    }
    lm_slen[slot] = n;
    lm_shave[slot] = 1;
    lm_setfname(slot, name);
    return 1;
}

/* lm_save - one slot back to its file. 0 = written (or nothing to write),
 * -1 = refused, and the status row says which fact refused it. */
static int lm_save(int slot)
{
    int e;

    if (!lm_shave[slot] || !lm_smod[slot])
        return 0;
    if (lm_projhave && os88_file_goto(&lm_projplace) != 0) {
        lm_say("The project's folder is not listable - was the disk swapped?");
        return -1;
    }
    if (os88_file_write_seg(lm_fname(slot), lm_sbase(slot),
                            lm_slen[slot]) != 0) {
        e = os88_ferr();
        lm_l0();
        lm_ls(lm_fname(slot));
        if (e == OS88_FERR_WPROT)
            lm_ls(": the disk is write-protected.");
        else if (e == OS88_FERR_FULL)
            lm_ls(": the disk is full.");
        else if (e == OS88_FERR_PROT)
            lm_ls(": the file is read-only.");
        else
            lm_ls(": the disk refused the write.");
        lm_say(lm_line);
        return -1;
    }
    lm_smod[slot] = 0;
    lm_l0();
    lm_ls("Saved ");
    lm_ls(lm_fname(slot));
    lm_ls(", ");
    lm_ln((int) lm_slen[slot]);
    lm_ls(" bytes.");
    lm_say(lm_line);
    return 0;
}

static void lm_saveall(void)
{
    int i;

    for (i = 0; i < LM_NSLOT; i++)
        if (lm_smod[i] && lm_save(i) != 0)
            return;                     /* the first refusal keeps its own
                                         * sentence: a second attempt would
                                         * overwrite it with the same one */
}

static int lm_anymod(void)
{
    int i;

    for (i = 0; i < LM_NSLOT; i++)
        if (lm_shave[i] && lm_smod[i])
            return 1;
    return 0;
}

/* lm_nmodified - how many slots are unsaved, and WHICH the first one is. The
 * close guard asks about the file that is actually unsaved rather than the
 * one on screen, and with more than one it says the count instead.
 * `first` is a file-scope static in the caller, never `&local` - SS != DS
 * makes a stack address meaningless and tools/cc8086.py refuses it outright
 * (SPEC.md 73.5). */
static int lm_nmodified(int *first)
{
    int i, n;

    n = 0;
    *first = LM_SLOT_WML;
    for (i = 0; i < LM_NSLOT; i++)
        if (lm_shave[i] && lm_smod[i]) {
            if (n == 0)
                *first = i;
            n++;
        }
    return n;
}

static int lm_nextslot(void)
{
    int i, k;

    for (i = 1; i <= LM_NSLOT; i++) {
        k = (lm_slot + i) % LM_NSLOT;
        if (lm_shave[k])
            return k;
    }
    return lm_slot;
}

/* ============================================================================
 * SPEC.md 19.9's PREFERENCES - `SYSTEM/APPDATA/LOOM.CFG`
 *
 * The bank / GOTO / act / GOTO-back idiom, TOLERATING ABSENCE at every step.
 * A disk without the folder is not an error: it is a user's own disk, or one
 * written by something else, and the volume goes back where it was.
 * apps/weave/wstate.c is the shape and its header carries the doctrine -
 * including the one that is easy to get wrong, that SYSTEM/ is FOUND BY
 * WALKING rather than assumed, and that it is os88_file_goto() and never the
 * quiet twin (which moves the global cwd and deliberately not the instance's,
 * so the quiet move is undone by the very next write).
 *
 * NEVER FAIL A LAUNCH BECAUSE IT IS MISSING. The file holds the last
 * project's folder and the last slot, and both are conveniences: without them
 * LOOM opens empty, which is exactly what a first run does.
 *
 * THE FORMAT is four little-endian words: a magic, the volume, the folder's
 * first cluster, the slot. Sixteen bytes with room to grow, and the magic is
 * what stops a LOOM.CFG written by a later wave being read as this one's.
 * ==========================================================================*/

#define LM_CFG_MAGIC  0x4C31            /* 'L1' - bump it when the shape moves */
#define LM_CFG_LEN    8

static struct os88_place lm_here;
static struct os88_place lm_there;
static struct os88_find  lm_find;
static unsigned char     lm_cfg[LM_CFG_LEN];

/* lm_dive - find the visible directory `name` where we stand and go into it.
 * 1 = we are in it, 0 = it is not here and nothing moved. */
static int lm_dive(const char *name)
{
    int ord;

    ord = 0;
    while (ord >= 0) {
        ord = os88_file_find(ord, &lm_find);
        if (ord < 0)
            break;
        if (lm_find.type != OS88_FT_DIR)
            continue;
        if (!lm_samename(lm_find.name, name))
            continue;
        lm_there.clus = lm_find.clus;
        lm_there.vol = lm_here.vol;
        return os88_file_goto(&lm_there) == 0;
    }
    return 0;
}

static int lm_data_enter(void)
{
    os88_file_here(&lm_here);
    lm_there.clus = 0;                  /* the ROOT of that same volume */
    lm_there.vol = lm_here.vol;
    if (os88_file_goto(&lm_there) != 0)
        return 0;
    if (!lm_dive("SYSTEM") || !lm_dive("APPDATA")) {
        os88_file_goto(&lm_here);
        return 0;
    }
    return 1;
}

static void lm_data_leave(void)
{
    os88_file_goto(&lm_here);           /* leaving the instance elsewhere would
                                         * move where every unqualified name it
                                         * passes the file API resolves, and
                                         * where its next dialog opens (19.9) */
}

static void lm_prefs_read(void)
{
    unsigned n;

    n = 0;
    if (lm_data_enter()) {
        n = os88_file_read("LOOM.CFG", lm_cfg, LM_CFG_LEN);
        lm_data_leave();
    }
    if (n < LM_CFG_LEN)
        return;                         /* absent, short or unreadable: the
                                         * ordinary first run */
    if ((lm_cfg[0] | (lm_cfg[1] << 8)) != LM_CFG_MAGIC)
        return;
    lm_projplace.vol = lm_cfg[2] | (lm_cfg[3] << 8);
    lm_projplace.clus = lm_cfg[4] | (lm_cfg[5] << 8);
    lm_projhave = 1;
    lm_slot = lm_cfg[6] & 3;
}

static void lm_prefs_write(void)
{
    if (!lm_projhave)
        return;
    lm_cfg[0] = LM_CFG_MAGIC & 0xFF;
    lm_cfg[1] = (LM_CFG_MAGIC >> 8) & 0xFF;
    lm_cfg[2] = (unsigned char) (lm_projplace.vol & 0xFF);
    lm_cfg[3] = (unsigned char) ((lm_projplace.vol >> 8) & 0xFF);
    lm_cfg[4] = (unsigned char) (lm_projplace.clus & 0xFF);
    lm_cfg[5] = (unsigned char) ((lm_projplace.clus >> 8) & 0xFF);
    lm_cfg[6] = (unsigned char) lm_slot;
    lm_cfg[7] = 0;
    if (lm_data_enter()) {
        os88_file_write("LOOM.CFG", lm_cfg, LM_CFG_LEN);
        lm_data_leave();                /* a refused write is fine: the next
                                         * launch opens empty, which is what a
                                         * first run does anyway */
    }
}

/* ============================================================================
 * OPENING A PROJECT (WEAVE-SPEC 11.2, 1.5)
 *
 * `ovl_*`, so the whole of it ships in LOOM.OVL: it runs once per open, on
 * the UI task, and it is on no path a keystroke takes - SPEC.md 73.14's own
 * test for a tenant.
 *
 * THE WRAPPERS ANSWER 1 AT EVERY EXIT INCLUDING THEIR OWN REFUSALS, which
 * have already put a sentence on the glass, so a 0 can only mean the module
 * is not there (apps/weave/wload.c's rule, and its header explains why the
 * natural `void` return would have produced a double-click that opened a
 * window, drew the empty screen and said nothing at all).
 * ==========================================================================*/

static void lm_freeproject(void)
{
    int i;

    if (lm_srcseg) {
        os88_mem_free(lm_srcseg);
        lm_srcseg = 0;
    }
    for (i = 0; i < LM_NSLOT; i++) {
        lm_slen[i] = 0;
        lm_shave[i] = 0;
        lm_smod[i] = 0;
    }
    lm_state = LM_ST_EMPTY;
    lm_slot = LM_SLOT_WML;
    lm_ed_reset();
}

/* ovl_stemof - the 8.3 stem of `name`, uppercased, into lm_stem. */
static void ovl_stemof(const char *name)
{
    int i;
    int c;

    for (i = 0; i < 8 && name[i] && name[i] != '.'; i++) {
        c = name[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        lm_stem[i] = (char) c;
    }
    lm_stem[i] = 0;
}

/* ovl_exists - is `name` a FILE in the directory we stand in?
 *
 * FIND is by ORDINAL with no cursor anywhere (SPEC.md 19.7.1): ask for 0,
 * then for what it returns. A folder wearing the companion's name is not it,
 * and neither is the `..` row. */
static int ovl_exists(const char *name)
{
    int ord;

    ord = 0;
    while (ord >= 0) {
        ord = os88_file_find(ord, &lm_find);
        if (ord < 0)
            break;
        if (lm_find.type >= OS88_FT_DIR)
            continue;
        if (lm_samename(lm_find.name, name))
            return 1;
    }
    return 0;
}

/* ovl_companion - WEAVE-SPEC 11.2's search, in ITS order: the .WML's own stem
 * first, then 11.2's project spelling. tools/weavesim.py's pack_project()
 * does exactly this and the two must not diverge. */
static void ovl_companion(int slot, const char *projname)
{
    lm_mkname(lm_stem, lm_slotext(slot));
    if (ovl_exists(lm_line)) {
        lm_readslot(slot, lm_line);
        return;
    }
    lm_l0();
    lm_ls(projname);
    if (ovl_exists(lm_line))
        lm_readslot(slot, lm_line);
}

/* ovl_openproj - the whole of an open, in the order the contract gives. */
static int ovl_openproj(void *win, const char *name)
{
    unsigned n;

    (void)win;
    lm_freeproject();
    lm_status[0] = 0;

    /* 10.1's shape, applied to the claim this program cannot start without.
     * The source claim is small, but the refusal has to name BOTH figures:
     * a heap with room overall and no run big enough cannot host it either. */
    lm_srcseg = os88_mem_claim(LM_CLAIMKB);
    if (lm_srcseg == 0) {
        lm_l0();
        lm_ls("LOOM needs ");
        lm_ln(LM_CLAIMKB);
        lm_ls("KB for the sources; the largest free run is ");
        lm_ln((int) os88_mem_largest_kb());
        lm_ls("KB.");
        lm_say(lm_line);
        return 1;
    }

    ovl_stemof(name);
    if (!lm_readslot(LM_SLOT_WML, name)) {
        if (lm_status[0] == 0) {
            lm_l0();
            lm_ls(name);
            lm_ls(" is missing or unreadable.");
            lm_say(lm_line);
        }
        os88_mem_free(lm_srcseg);
        lm_srcseg = 0;
        return 1;
    }

    /* Where we now stand IS the project folder - the file dialog left us
     * there, and so did SPEC.md 54.9's assoc_back on the association route.
     * Bank it: Save and Pack come back here, and SPEC.md 19.9's preference
     * walk is about to move us. */
    os88_file_here(&lm_projplace);
    lm_projhave = 1;

    ovl_companion(LM_SLOT_WJS, "MAIN.WJS");
    ovl_companion(LM_SLOT_WFX, "SHEET.WFX");
    ovl_companion(LM_SLOT_WSP, "SPRITES.WSP");

    lm_state = LM_ST_EDIT;
    lm_slot = LM_SLOT_WML;
    lm_ed_reset();
    lm_menusync();

    n = 0;
    if (lm_shave[LM_SLOT_WJS])
        n++;
    if (lm_shave[LM_SLOT_WFX])
        n++;
    if (lm_shave[LM_SLOT_WSP])
        n++;
    lm_l0();
    lm_ls(lm_fname(LM_SLOT_WML));
    lm_ls(", ");
    lm_ln(lm_nline);
    lm_ls(" lines");
    if (n) {
        lm_ls(" + ");
        lm_ln((int) n);
        lm_ls(" companion");
        if (n > 1)
            lm_ls("s");
    }
    lm_ls(".");
    lm_quiet(lm_line);
    return 1;
}

/* lm_open_pend - spend the banked launch document (SPEC.md 54.5, 54.8).
 * Called from the FIRST W_PAINT and exactly once.
 *
 * THE GOTO GOES FIRST. It is a REMOUNT - real floppy I/O - and a failure is
 * the folder no longer being listable, a disk swapped between the
 * double-click and this first paint. Searching the wrong directory would
 * report the wrong reason, so a refused GOTO takes the not-found exit rather
 * than falling through to a search where we happen to stand.
 *
 * Only when a locator actually came with the name: 0,0 is a REAL locator -
 * the root of volume A: - so lm_arghave is what says whether there was one.
 *
 * A `.WJS` DOUBLE-CLICK OPENS ITS PROJECT AND NOT JUST THE FILE. LOOM's
 * association block claims both extensions (WEAVE-SPEC 1.5 step 2) and a
 * project's root is always the `.WML` (11.2), so the name is turned back into
 * its stem's `.WML` before the open. Opening a lone script would give the
 * user an editor that cannot pack.
 */
static void lm_open_pend(void *win)
{
    lm_prefs_read();                    /* before anything moves us, and it
                                         * tolerates absence at every step */
    if (!lm_arghave) {
        lm_state = LM_ST_EMPTY;
        lm_menusync();
        return;
    }
    if (os88_file_goto(&lm_argplace) != 0) {
        lm_l0();
        lm_ls(lm_argname);
        lm_ls(" missing - the folder is not listable.");
        lm_say(lm_line);
        return;
    }
    ovl_stemof(lm_argname);
    lm_mkname(lm_stem, "WML");
    os88_strcpy(lm_argname, lm_line, sizeof(lm_argname));
    if (!ovl_openproj(win, lm_argname))
        lm_say("LOOM.OVL is missing; a project cannot be opened.");
}

/* ============================================================================
 * PACK (WEAVE-SPEC 11.4)
 *
 * "Pack stages the image in a transient claim, writes the `.WAB` whole on the
 * UI task, and refuses politely (toast + sidebar) when the overlay cannot
 * load or the claim cannot be had - Pack is a menu command and menu commands
 * may refuse."
 *
 * THE MEMORY CHECK IS BEFORE ANY I/O, which is 10.1's rule pointed the other
 * way: WEAVE refuses before it reads a bundle and LOOM refuses before it
 * writes one. The two claims are 114KB together, which on the 256KB XT is the
 * difference between a Pack that works and one that half-happens, and the
 * refusal names BOTH figures because a heap with room overall and no run big
 * enough for the larger claim cannot host it either.
 *
 * BOTH CLAIMS ARE GIVEN BACK BEFORE THIS FUNCTION RETURNS, on every path
 * including every refusal. That is what "transient" means in 11.4 and it is
 * why lm_pack_free() is called from six places rather than once at the end:
 * a claim held across a refusal is a Pack that works the first time and
 * refuses for ever after.
 * ==========================================================================*/

static unsigned lm_packlen;             /* the last bundle's size, 0 = none */
static int      lm_packok;              /* 1 = the last Pack succeeded */
static int      lm_packran;             /* 1 = a Pack has been attempted */

static void lm_pack_free(void)
{
    if (lm_outseg) {
        os88_mem_free(lm_outseg);
        lm_outseg = 0;
    }
    if (lm_workseg) {
        os88_mem_free(lm_workseg);
        lm_workseg = 0;
    }
}

/* lm_pack_claim - the two transient claims, with 10.1's arithmetic in the
 * refusal. 1 = both are held, 0 = neither is and the sentence is up. */
/* lm_short - WEAVE-SPEC 10.1's sentence, naming BOTH figures: refuse when the
 * total free is short OR when the largest free RUN cannot hold the largest
 * single claim, because a heap with 120KB free in three pieces cannot host a
 * 62KB output claim and saying only the total would be a refusal the user
 * cannot act on. apps/weave/wload.c's w_short, pointed the other way.
 *
 * ONE FUNCTION AND NOT THREE COPIES. The three exits below said the same
 * sentence three times, and SmallerC pools no string literals: three copies
 * of these four fragments were ~90 bytes of a package with 86 to spare. */
static void lm_short(unsigned ask)
{
    lm_l0();
    lm_ls("Pack needs ");
    lm_ln((int) ask);
    lm_ls("KB; the largest free run is ");
    lm_ln((int) os88_mem_largest_kb());
    lm_ls("KB.");
    lm_say(lm_line);
}

static int lm_pack_claim(void)
{
    unsigned ask, biggest;

    ask = LM_WORKKB + LM_OUTKB;
    biggest = LM_OUTKB;
    if (ask > os88_mem_total_kb() || biggest > os88_mem_largest_kb()) {
        lm_short(ask);
        return 0;
    }
    lm_outseg = os88_mem_claim(LM_OUTKB);       /* the bigger one first: it is
                                                 * the one that can fail */
    if (lm_outseg == 0) {
        lm_short(ask);
        return 0;
    }
    lm_workseg = os88_mem_claim(LM_WORKKB);
    if (lm_workseg == 0) {
        lm_pack_free();
        lm_short(ask);
        return 0;
    }
    /* Every compiler table is a byte offset into the scratch claim and every
     * one of them assumes it starts zeroed - which a fresh heap claim does
     * NOT: os88_mem_claim() hands back whatever was in the run. One
     * lm_sfill() of the whole region is ~52KB of `rep stosb`, about 40 ms on
     * the target, against a compiler that reads a stale count word and builds
     * a bundle out of the last Pack's atoms. */
    lm_sfill(lm_workseg, 0, 0, LMW_END);
    return 1;
}

/* lm_packrun - the compilers, in one door, with the workspace standing.
 * Answers what ovl_pack() answered, which is 1 = a bundle is in the output
 * claim, 0 = it did not happen. lm_failed() is what distinguishes a PACK
 * ERROR (the sentence is in lm_errtext()) from an overlay that would not
 * load, and the two need different sentences: one is the author's problem and
 * one is the disk's. */
static void lm_pack(void)
{
    int ok;
    int written;

    if (lm_state == LM_ST_EMPTY)
        return;
    lm_packran = 1;
    lm_packok = 0;
    lm_packlen = 0;

    /* SAY SO BEFORE IT STARTS, because a Pack is SECONDS on the target and a
     * program that goes quiet for seconds is one the user thinks has died
     * (SPEC.md 47: refusal - and delay - is a normal, VISIBLE path).
     *
     * The arithmetic, from CLAUDE.md's cost table. Saving the sources is one
     * `int 13h` a modified file at ~400 ms; the compilers run in LOOM.OVL and
     * SPEC.md 73.14 makes EVERY call out of the module a far call through a
     * shim at 46.7 us, and the scanners call lm_sb() once a source character,
     * so FORM's 1,578 bytes of source cost of the order of a second before
     * anything is written; and the .WAB write is one more ~400 ms. Two
     * seconds for the smallest demo, more for PONG's sprite art. One
     * status-row line - 78 cells, ~71 ms - is a cheap thing to spend on
     * saying so. */
    lm_say("Packing...");
    lm_status_paint(1);

    /* Save first. WEAVE-SPEC 11.3 calls pack-on-save Loom's default, and the
     * reason is sharper than tidiness: `weavesim --pack` reads the FILES, so
     * a Pack of an unsaved buffer would compare a bundle built from the
     * editor against one built from the disk - and 11.1's gate would report a
     * difference that is in neither compiler. */
    lm_saveall();
    if (lm_anymod()) {
        lm_side_paint();
        lm_status_paint(0);
        return;                         /* the save refused and said why */
    }

    if (!lm_pack_claim()) {
        lm_side_paint();
        lm_status_paint(0);
        return;
    }

    lm_clearerr();
    ok = ovl_pack();
    if (!ok && !lm_failed()) {
        lm_pack_free();
        lm_say("LOOM.OVL is missing or stale; no bundle can be packed.");
        lm_side_paint();
        lm_status_paint(0);
        return;
    }
    if (!ok) {
        /* WEAVE-SPEC 11.3: "Loom shows them in the sidebar and jumps the
         * caret to the first" - which is the loop's whole speed, so it
         * happens here and not after another click. */
        lm_pack_free();
        lm_say(lm_errtext());
        if (lm_errslot() >= 0 && lm_errslot() < LM_NSLOT
            && lm_shave[lm_errslot()]) {
            lm_switch(lm_errslot());
            lm_ed_goline(lm_errline() - 1);
        }
        lm_side_paint();
        lm_status_paint(0);
        return;
    }

    lm_packlen = lm_outlen();
    written = -1;
    if (lm_projhave && os88_file_goto(&lm_projplace) == 0) {
        lm_mkname(lm_stem, "WAB");
        os88_strcpy(lm_amsg, lm_line, sizeof(lm_amsg));
        written = os88_file_write_seg(lm_amsg, lm_outseg, lm_packlen);
    }
    lm_pack_free();                     /* 11.4: transient, and given back
                                         * BEFORE the report is drawn */

    if (lm_packlen == 0) {
        /* The writer answered success with nothing in the claim, which no
         * project can now produce: 2.2's header alone is 32 bytes and 2.4
         * makes five sections mandatory, so the smallest legal bundle is
         * PLAIN's 240. It is kept as a REPORT rather than removed because a
         * writer that ever answers this way must say so on the glass instead
         * of writing an empty file over the author's last good bundle. */
        lm_say("Packed 0 bytes - nothing was written.");
    } else if (written != 0) {
        lm_l0();
        lm_ls(lm_amsg);
        lm_ls(": the disk refused the write.");
        lm_say(lm_line);
    } else {
        lm_packok = 1;
        lm_l0();
        lm_ls("Packed ");
        lm_ls(lm_amsg);
        lm_ls(", ");
        lm_ln((int) lm_packlen);
        lm_ls(" bytes. Reload it in Weave with ^R.");
        lm_say(lm_line);
    }
    lm_side_paint();
    lm_status_paint(0);
}

/* ============================================================================
 * THE SIDEBAR (WEAVE-SPEC 13.1's "project folder + file switcher", 11.3)
 *
 * LM_SIDE_CELLS columns down the left of the content area: the four slots the
 * project has, one row each, the current one INVERTED, a `*` on a modified
 * one - and below them the last Pack's result, which is CLICKABLE: clicking
 * it switches to the offending file and puts the caret on the line (11.3,
 * "caret-to-error is the loop's whole speed").
 *
 * IT HAS A SHADOW OF ITS OWN, and the arithmetic is why: a 12-cell row is
 * ~11 ms on the target machine (CLAUDE.md's ~900 us a glyph cell) and there
 * are up to ten rows, so a sidebar repainted whenever anything happened would
 * be 110 ms of visible redraw - on a path the user did not ask for. The
 * shadow makes an unchanged row cost a compare and nothing else, which is
 * PERFORMANCE.md's first rule stated about the smallest pane in the program.
 * ==========================================================================*/

#define LM_SIDE_ROWS  10

static char          lm_shside[LM_SIDE_ROWS][LM_SIDE_CELLS + 1];
static unsigned char lm_shsinv[LM_SIDE_ROWS];
static char          lm_sbuf2[LM_SIDE_CELLS + 2];

static void lm_side_invalidate(void)
{
    int r, c;

    for (r = 0; r < LM_SIDE_ROWS; r++) {
        lm_shsinv[r] = 0xFF;            /* neither 0 nor 1: always differs */
        for (c = 0; c <= LM_SIDE_CELLS; c++)
            lm_shside[r][c] = (char) 0xFF;
    }
}

/* lm_side_row - one row of the sidebar, padded to LM_SIDE_CELLS and drawn
 * only if it CHANGED. The padding is the erase (WEAVE-SPEC 6.2's rule), so
 * there is no fill under a shorter label and no instant at which the row is
 * blank - the erase-then-letter pair is the canonical double-draw in this
 * tree and it is invisible in an emulator. */
static void lm_side_row(int r, const char *s, int inv)
{
    int i, n, same;

    if (r < 0 || r >= LM_SIDE_ROWS || r >= lm_ch - 1)
        return;
    n = (int) os88_strlen(s);
    if (n > LM_SIDE_CELLS)
        n = LM_SIDE_CELLS;
    for (i = 0; i < n; i++)
        lm_sbuf2[i] = s[i];
    for (i = n; i < LM_SIDE_CELLS; i++)
        lm_sbuf2[i] = ' ';
    lm_sbuf2[LM_SIDE_CELLS] = 0;

    same = (lm_shsinv[r] == (unsigned char) inv);
    if (same)
        for (i = 0; i < LM_SIDE_CELLS; i++)
            if (lm_shside[r][i] != lm_sbuf2[i]) {
                same = 0;
                break;
            }
    if (same)
        return;
    for (i = 0; i < LM_SIDE_CELLS; i++)
        lm_shside[r][i] = lm_sbuf2[i];
    lm_shsinv[r] = (unsigned char) inv;
    os88_font_run(lm_ox, lm_oy + r * 8, lm_sbuf2,
                  inv ? OS88_WHITE : OS88_BLACK,
                  inv ? OS88_BLACK : OS88_WHITE);
}

/* The row map, in one place because the painter and the hit test both read it
 * and a second copy is how a drawn row and a clickable row drift apart
 * (apps/calc/calc.asm's cal_layout rule, SPEC.md 22). */
#define LM_SR_TITLE  0
#define LM_SR_FILE0  1                  /* ...through LM_SR_FILE0 + 3 */
#define LM_SR_GAP    5
#define LM_SR_PACK   6
#define LM_SR_PK1    7
#define LM_SR_PK2    8

static void lm_side_paint(void)
{
    int i, r;

    if (!lm_sidew)
        return;
    lm_side_row(LM_SR_TITLE, lm_stem[0] ? lm_stem : "Project", 1);
    r = LM_SR_FILE0;
    for (i = 0; i < LM_NSLOT; i++) {
        lm_l0();
        if (!lm_shave[i]) {
            lm_ls(" -");
            lm_side_row(r, lm_line, 0);
        } else {
            lm_ls(lm_smod[i] ? "*" : " ");
            lm_ls(lm_fname(i));
            lm_side_row(r, lm_line, i == lm_slot);
        }
        r++;
    }
    lm_side_row(LM_SR_GAP, "", 0);
    if (!lm_packran) {
        lm_side_row(LM_SR_PACK, "Not packed", 0);
        lm_side_row(LM_SR_PK1, "", 0);
        lm_side_row(LM_SR_PK2, "", 0);
        return;
    }
    if (lm_packok) {
        lm_side_row(LM_SR_PACK, "Packed", 0);
        lm_l0();
        lm_ln((int) lm_packlen);
        lm_ls(" bytes");
        lm_side_row(LM_SR_PK1, lm_line, 0);
        lm_side_row(LM_SR_PK2, "", 0);
        return;
    }
    lm_side_row(LM_SR_PACK, "Pack failed", 0);
    if (lm_failed() && lm_errslot() >= 0) {
        lm_side_row(LM_SR_PK1, lm_fname(lm_errslot()), 0);
        lm_l0();
        lm_ls("line ");
        lm_ln(lm_errline());
        lm_side_row(LM_SR_PK2, lm_line, 0);
        return;
    }
    lm_side_row(LM_SR_PK1, "", 0);
    lm_side_row(LM_SR_PK2, "", 0);
}

/* lm_switch - show a different source. The caret, the view and the line table
 * all belong to the slot, so lm_ed_reset() rebuilds them; the sidebar's
 * selection moved, so it is repainted (two rows change and its shadow makes
 * the other eight free). */
static void lm_switch(int slot)
{
    if (slot < 0 || slot >= LM_NSLOT || !lm_shave[slot] || slot == lm_slot)
        return;
    lm_slot = slot;
    lm_state = LM_ST_EDIT;
    lm_ed_reset();
    lm_side_paint();
    lm_ed_paint(1);
    lm_status_paint(0);
}

/* lm_side_click - a row of the sidebar. A file row switches to it; either of
 * the two pack-error rows jumps the caret to the failure (WEAVE-SPEC 11.3).
 *
 * ON THE PRESS, and that is the platform's answer for a LIST row rather than
 * a shortcut: SPEC.md 13.7's press/release gesture is what a BUTTON fires on,
 * and these are rows in a list - the Finder's rows and the Standard File
 * dialog's select on the press. Nothing here arms, so nothing here needs a
 * W_ONMOUSEUP (apps/loom/loom.asm says the same thing about the %define). */
static void lm_side_click(int x, int y)
{
    int r;

    (void)x;
    r = (y - lm_oy) / 8;
    if (r >= LM_SR_FILE0 && r < LM_SR_FILE0 + LM_NSLOT) {
        lm_switch(r - LM_SR_FILE0);
        return;
    }
    if (r == LM_SR_PK1 || r == LM_SR_PK2) {
        if (!lm_packran || lm_packok || !lm_failed())
            return;
        if (lm_errslot() < 0 || lm_errslot() >= LM_NSLOT
            || !lm_shave[lm_errslot()])
            return;
        if (lm_errslot() != lm_slot)
            lm_switch(lm_errslot());
        lm_ed_goline(lm_errline() - 1);
        lm_say(lm_errtext());
        lm_status_paint(0);
    }
}
