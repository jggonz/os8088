/* APPDATA file transport. Production codec is in bt_codec.c.
 * Included after platform state declarations; host faults exercise this exact source. */
/* Two independent generations. Replacing the older one never frees the
 * newest one's chain. A corrupt candidate cannot mutate the live store. */
static void ovl_bt_load(void)
{
    unsigned n;
    BTWord first, candidate;
    int valid;
    if (bt_data_enter() != 0) return;
    n = os88_file_read(bt_slots[0], bt_io, sizeof(bt_io));
    valid = !os88_ferr() && n && bt_decode(bt_io, n);
    first = bt_store.generation;
    n = os88_file_read(bt_slots[1], bt_io, sizeof(bt_io));
    candidate = n >= 10 ? bt_io[8] | ((BTWord)bt_io[9] << 8) : 0;
    if (!os88_ferr() && n && (!valid || (short)(candidate - first) > 0) &&
        bt_decode(bt_io, n)) valid = 1;
    bt_saved_generation = bt_store.generation;
    if (!valid) bt_message("New profiles. Progress saves on this disk.");
    bt_data_back();
}

static void ovl_bt_save(void)
{
    unsigned n, check;
    BTWord checksum, expected;
    int slot, ok;
    if (bt_readonly) { bt_message("Read-only: another Borat session owns progress."); return; }
    if (bt_data_enter() != 0) { bt_message("Save failed. Use a writable installed disk."); return; }
    bt_store.generation = bt_saved_generation + 1;
    n = bt_encode(bt_io, sizeof(bt_io));
    slot = bt_store.generation & 1;
    expected = bt_store.generation;
    checksum = n >= 20 ? bt_io[18] | ((BTWord)bt_io[19] << 8) : 0;
    ok = n && os88_file_write(bt_slots[slot], bt_io, n) == 0;
    if (ok) {
        check = os88_file_read(bt_slots[slot], bt_io, sizeof(bt_io));
        ok = !os88_ferr() && check == n && check >= 20 &&
            (bt_io[8] | ((BTWord)bt_io[9] << 8)) == expected &&
            (bt_io[18] | ((BTWord)bt_io[19] << 8)) == checksum && bt_decode(bt_io, check);
    }
    bt_data_back();
    if (ok) { bt_saved_generation = bt_store.generation; bt_message("Progress saved."); }
    else { bt_store.generation = bt_saved_generation; bt_message("Save failed. Previous copy kept; free disk space."); }
}

