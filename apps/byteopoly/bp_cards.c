/* Byteopoly - the two eight-card decks (SPEC.md 131.2.9). Original text.
 * Lane: rules. Card id = deck * 8 + n; the id is stable and is what decks,
 * saves and frames carry. Text wraps at the panel; keep it under 64 chars. */

static const BPCard bp_cards[BP_DECKS * BP_DECK_CARDS] = {
    /* Floppy Find (deck 0) */
    {0, 0, BP_E_GAIN,    0, 100, TR_BOXED_NEW_OLD,  TR_RECEIVE_100_CR},
    {1, 0, BP_E_GAIN,    0,  50, TR_GARAGE_SALE_TREASURE, TR_RECEIVE_50_CR},
    {2, 0, BP_E_GAIN,    0,  25, TR_DUPLICATE_MANUAL,     TR_RECEIVE_25_CR},
    {3, 0, BP_E_TO,      0,   0, TR_REBOOT_TO_BOOT,       TR_ADVANCE_TO_BOOT},
    {4, 0, BP_E_TO,     16,   0, TR_DIAL_INTO_THE,    TR_ADVANCE_TO_THE},
    {5, 0, BP_E_FORWARD, 3,   0, TR_TURBO_BUTTON,         TR_MOVE_FORWARD_3},
    {6, 0, BP_E_KEEP,    0,   0, TR_RECOVERY_DISK,        TR_KEEP_THIS_DISK},
    {7, 0, BP_E_PAY,     0,  25, TR_REPLACEMENT_CABLE,    TR_PAY_THE_BANK},
    /* System Error (deck 1) */
    {8, 1, BP_E_PAY,     0, 100, TR_LEAKING_BATTERY,      TR_PAY_THE_BANK_100},
    {9, 1, BP_E_PAY,     0,  50, TR_FAILED_CAPACITOR,     TR_PAY_THE_BANK_50},
    {10, 1, BP_E_PAY,    0,  35, TR_SHIPPING_DAMAGE,      TR_PAY_THE_BANK_35},
    {11, 1, BP_E_REPAIRS, 0,  0, TR_BENCH_SERVICE,        TR_PAY_25_CR},
    {12, 1, BP_E_DETAIN, 0,   0, TR_GENERAL_PROTECTION_FAULT, TR_GO_TO_THE},
    {13, 1, BP_E_BACK,   3,   0, TR_ROLL_BACK,            TR_MOVE_BACK_3},
    {14, 1, BP_E_GAIN,   0,  50, TR_WARRANTY_REPLACEMENT, TR_RECEIVE_50_CR},
    {15, 1, BP_E_KEEP,   0,   0, TR_RECOVERY_DISK,        TR_KEEP_THIS_DISK}
};

const BPCard *bp_card(int id)
{
    if (id < 0 || id >= BP_DECKS * BP_DECK_CARDS) return 0;
    return &bp_cards[id];
}
