/* Byteopoly - the immutable board (SPEC.md 131.2.1). Original.
 * Lane: rules. Space ids 0..27 run clockwise from BOOT (bottom-left);
 * corners are 0, 7, 14 and 21. Short names are at most 6 characters, the
 * width a COMPACT cell holds. Display names are separate from ids. */

static const BPSpace bp_spaces[BP_SPACES] = {
    {BP_K_BOOT,     0, BP_NOBODY,   0,  0, "BOOT",            "BOOT"},
    {BP_K_COMPUTER, BP_G_COMMODORE, 0, 100,  6, "Commodore PET", "PET"},
    {BP_K_EVENT,    0, BP_NOBODY,   0,  0, "Floppy Find",     "FLOPPY"},
    {BP_K_COMPUTER, BP_G_COMMODORE, 1, 120,  8, "VIC-20",        "VIC-20"},
    {BP_K_COMPUTER, BP_G_COMMODORE, 2, 140, 10, "Commodore 64",  "C64"},
    {BP_K_UTILITY,  0, 3,         150,  0, "POWER",           "POWER"},
    {BP_K_CHARGE,   0, BP_NOBODY, 100,  0, "Import Duty",     "DUTY"},
    {BP_K_REPAIR,   0, BP_NOBODY,   0,  0, "REPAIR SHOP",     "REPAIR"},
    {BP_K_COMPUTER, BP_G_HOME,  4, 160, 12, "TRS-80",          "TRS-80"},
    {BP_K_COMPUTER, BP_G_HOME,  5, 180, 14, "Atari 800",       "A800"},
    {BP_K_EVENT,    1, BP_NOBODY,   0,  0, "System Error",    "ERROR"},
    {BP_K_COMPUTER, BP_G_HOME,  6, 200, 16, "ZX Spectrum",     "ZX"},
    {BP_K_COMPUTER, BP_G_APPLE, 7, 240, 20, "Apple II",        "AP II"},
    {BP_K_COMPUTER, BP_G_APPLE, 8, 260, 22, "Apple IIe",       "AP IIe"},
    {BP_K_SWAP,     0, BP_NOBODY,   0,  0, "SWAP MEET",       "SWAP"},
    {BP_K_COMPUTER, BP_G_APPLE, 9, 280, 24, "Macintosh 128K",  "MAC"},
    {BP_K_UTILITY,  0, 10,        150,  0, "BBS",             "BBS"},
    {BP_K_COMPUTER, BP_G_IBM,  11, 300, 26, "IBM PC 5150",     "PC"},
    {BP_K_EVENT,    0, BP_NOBODY,   0,  0, "Floppy Find",     "FLOPPY"},
    {BP_K_COMPUTER, BP_G_IBM,  12, 320, 28, "PC XT 5160",      "PC XT"},
    {BP_K_COMPUTER, BP_G_IBM,  13, 340, 30, "PC AT 5170",      "PC AT"},
    {BP_K_CRASH,    0, BP_NOBODY,   0,  0, "CRASH!",          "CRASH!"},
    {BP_K_COMPUTER, BP_G_RIVALS, 14, 360, 32, "Amiga 500",     "AMIGA"},
    {BP_K_COMPUTER, BP_G_RIVALS, 15, 380, 34, "Atari ST",      "ST"},
    {BP_K_EVENT,    1, BP_NOBODY,   0,  0, "System Error",    "ERROR"},
    {BP_K_COMPUTER, BP_G_POWER, 16, 400, 36, "386DX",          "386DX"},
    {BP_K_COMPUTER, BP_G_POWER, 17, 420, 40, "486DX2",         "486DX2"},
    {BP_K_CHARGE,   0, BP_NOBODY,  75,  0, "E-waste Fee",     "E-WASTE"}
};

/* prop index -> space id, the inverse of BPSpace.prop */
static const BPByte bp_prop_spaces[BP_PROPS] = {
    1, 3, 4, 5, 8, 9, 11, 12, 13, 15, 16, 17, 19, 20, 22, 23, 25, 26
};
static const BPWord bp_group_cost[BP_GROUPS] = {50, 75, 100, 125, 150, 200};
static const BPByte bp_group_n[BP_GROUPS] = {3, 3, 3, 3, 2, 2};
static const char bp_group_letters[BP_GROUPS + 1] = "CHAIRP";
static const char *bp_group_names[BP_GROUPS] = {
    "Commodore", "Home Micros", "Apple", "IBM", "16-bit Rivals", "Power PCs"
};

const BPSpace *bp_space(int space)
{
    if (space < 0 || space >= BP_SPACES) return 0;
    return &bp_spaces[space];
}
int bp_prop_space(int prop)
{
    if (prop < 0 || prop >= BP_PROPS) return -1;
    return bp_prop_spaces[prop];
}
BPWord bp_group_upgrade_cost(int group)
{
    if (group < 0 || group >= BP_GROUPS) return 0;
    return bp_group_cost[group];
}
int bp_group_size(int group)
{
    if (group < 0 || group >= BP_GROUPS) return 0;
    return bp_group_n[group];
}
char bp_group_letter(int group)
{
    if (group < 0 || group >= BP_GROUPS) return '?';
    return bp_group_letters[group];
}
const char *bp_group_name(int group)
{
    if (group < 0 || group >= BP_GROUPS) return "";
    return bp_group_names[group];
}
