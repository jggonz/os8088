/* Byteopoly - the two eight-card decks (SPEC.md 131.2.9). Original text.
 * Lane: rules. Card id = deck * 8 + n; the id is stable and is what decks,
 * saves and frames carry. Text wraps at the panel; keep it under 64 chars. */

static const BPCard bp_cards[BP_DECKS * BP_DECK_CARDS] = {
    /* Floppy Find (deck 0) */
    {0, 0, BP_E_GAIN,    0, 100, "Boxed New Old Stock",  "Receive 100 cr from the bank."},
    {1, 0, BP_E_GAIN,    0,  50, "Garage Sale Treasure", "Receive 50 cr from the bank."},
    {2, 0, BP_E_GAIN,    0,  25, "Duplicate Manual",     "Receive 25 cr from the bank."},
    {3, 0, BP_E_TO,      0,   0, "Reboot to BOOT",       "Advance to BOOT. Collect 200 cr."},
    {4, 0, BP_E_TO,     16,   0, "Dial Into the BBS",    "Advance to the BBS. Collect salary if you pass BOOT."},
    {5, 0, BP_E_FORWARD, 3,   0, "Turbo Button",         "Move forward 3 spaces."},
    {6, 0, BP_E_KEEP,    0,   0, "Recovery Disk",        "Keep this disk. Use it to leave the Repair Shop."},
    {7, 0, BP_E_PAY,     0,  25, "Replacement Cable",    "Pay the bank 25 cr."},
    /* System Error (deck 1) */
    {8, 1, BP_E_PAY,     0, 100, "Leaking Battery",      "Pay the bank 100 cr."},
    {9, 1, BP_E_PAY,     0,  50, "Failed Capacitor",     "Pay the bank 50 cr."},
    {10, 1, BP_E_PAY,    0,  35, "Shipping Damage",      "Pay the bank 35 cr."},
    {11, 1, BP_E_REPAIRS, 0,  0, "Bench Service",        "Pay 25 cr per RAM chip and 100 cr per Workshop."},
    {12, 1, BP_E_DETAIN, 0,   0, "General Protection Fault", "Go to the Repair Shop. No salary."},
    {13, 1, BP_E_BACK,   3,   0, "Roll Back",            "Move back 3 spaces. No salary."},
    {14, 1, BP_E_GAIN,   0,  50, "Warranty Replacement", "Receive 50 cr from the bank."},
    {15, 1, BP_E_KEEP,   0,   0, "Recovery Disk",        "Keep this disk. Use it to leave the Repair Shop."}
};

const BPCard *bp_card(int id)
{
    if (id < 0 || id >= BP_DECKS * BP_DECK_CARDS) return 0;
    return &bp_cards[id];
}
