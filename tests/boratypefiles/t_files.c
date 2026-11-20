/* Fault injection over the production alternating APPDATA transport. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define BT_HOST
#include "../../apps/boratype/bt.h"
#include "../../apps/boratype/bt_lessons.c"
#include "../../apps/boratype/bt_core.c"
#include "../../apps/boratype/bt_codec.c"
int bt_practice_weak;
int bt_readonly;
static BTByte bt_io[BT_FILE_SIZE];
static BTWord bt_saved_generation;
static const char *bt_slots[2] = {"BORATA.BTT", "BORATB.BTT"};
static char notice[64];
static unsigned char disk[2][BT_SAVE_CAP], backup[2][BT_SAVE_CAP];
static unsigned length[2], backup_length[2];
static int ferr, enter_failure, fail_write, fail_read, writes, reads[2];
static void bt_message(const char *s) { snprintf(notice, sizeof(notice), "%s", s); }
static int bt_data_enter(void) { return enter_failure ? -1 : 0; }
static void bt_data_back(void) { }
static int slot_for(const char *name) { return !strcmp(name, bt_slots[1]); }
static int os88_ferr(void) { return ferr; }
static unsigned os88_file_read(const char *name, void *buf, unsigned cap)
{
    int s = slot_for(name);
    reads[s]++;
    if (fail_read) { ferr = 2; return 0; }
    if (!length[s]) { ferr = 4; return 0; }
    if (length[s] > cap) { ferr = 10; return 0; }
    memcpy(buf, disk[s], length[s]); ferr = 0; return length[s];
}
static int os88_file_write(const char *name, const void *buf, unsigned n)
{
    int s = slot_for(name);
    writes++;
    if (fail_write == 1) { length[s] = 0; ferr = 6; return -1; }
    if (fail_write == 2) { length[s] = n / 2; memcpy(disk[s], buf, length[s]); ferr = 2; return -1; }
    if (fail_write == 3) { ferr = 0; return 0; } /* stale valid read-back */
    memcpy(disk[s], buf, n); length[s] = n;
    if (fail_write == 4) disk[s][100] ^= 1;
    ferr = 0; return 0;
}
#include "../../apps/boratype/bt_files.c"
static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void fresh(void)
{
    bt_init(); memset(disk, 0, sizeof(disk)); memset(length, 0, sizeof(length));
    memset(reads, 0, sizeof(reads)); bt_saved_generation = 0; bt_readonly = 0;
    enter_failure = fail_write = fail_read = ferr = writes = 0; notice[0] = 0;
}
static void name(const char *n) { snprintf(bt_store.profile[0].name,13,"%s",n); }
static void reload(void) { bt_init(); ovl_bt_load(); }
static void two_valid(void)
{
    fresh(); name("First"); ovl_bt_save(); name("Newest"); ovl_bt_save();
    CHECK(bt_saved_generation == 2 && length[0] && length[1]);
    memcpy(backup, disk, sizeof(disk)); memcpy(backup_length, length, sizeof(length));
}
int main(void)
{
    int mode;
    fresh(); ovl_bt_load(); CHECK(bt_store.generation == 0);
    name("Alice"); ovl_bt_save(); CHECK(bt_store.generation == 1 && writes == 1);
    reload(); CHECK(!strcmp(bt_store.profile[0].name,"Alice"));
    two_valid(); reload(); CHECK(!strcmp(bt_store.profile[0].name,"Newest"));
    CHECK(reads[0] == 2 && reads[1] == 2); /* no reread/downgrade of valid first */
    disk[0][101] ^= 1; reload(); CHECK(!strcmp(bt_store.profile[0].name,"First"));
    memcpy(disk,backup,sizeof(disk)); memcpy(length,backup_length,sizeof(length));
    length[0] = 17; reload(); CHECK(!strcmp(bt_store.profile[0].name,"First"));
    for (mode = 1; mode <= 4; mode++) {
        two_valid(); name("Unsaved"); fail_write = mode; ovl_bt_save();
        CHECK(!strcmp(bt_store.profile[0].name,"Unsaved"));
        CHECK(bt_store.generation == 2 && strstr(notice,"failed"));
        fail_write = 0; reload(); CHECK(!strcmp(bt_store.profile[0].name,"Newest"));
    }
    two_valid(); name("ReadError"); fail_read = 1; ovl_bt_save();
    CHECK(bt_store.generation == 2 && !strcmp(bt_store.profile[0].name,"ReadError"));
    fail_read = 0; reload(); CHECK(!strcmp(bt_store.profile[0].name,"ReadError"));
    two_valid(); bt_readonly = 1; name("Second"); ovl_bt_save();
    CHECK(writes == 2 && strstr(notice,"Read-only"));
    bt_readonly = 0; reload(); CHECK(!strcmp(bt_store.profile[0].name,"Newest"));
    two_valid(); enter_failure = 1; name("NoDisk"); ovl_bt_save();
    CHECK(writes == 2 && !strcmp(bt_store.profile[0].name,"NoDisk"));
    fresh(); bt_saved_generation = 65534; name("WrapA"); ovl_bt_save();
    name("WrapB"); ovl_bt_save(); CHECK(bt_saved_generation == 0);
    reload(); CHECK(!strcmp(bt_store.profile[0].name,"WrapB"));
    CHECK(bt_store.generation == 0);
    printf("t_files: %u fault/roundtrip checks ok\n", checks);
    return 0;
}
