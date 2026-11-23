/* One translation unit; production engine is shared with the host harness. */
#include "os88.h"
#include "gk.h"
/* Single-run setup and final accounting live in the existing cold overlay. */
#define bt_init ovl_bt_init
#define bt_commit ovl_bt_commit
#define bt_start ovl_bt_start
#define bt_weak ovl_bt_weak
#define bt_ratio_compare ovl_bt_ratio_compare
#include "bt.h"
#include "bt_face.h"
#include "art.h"
#include "bt_audio.h"
#include "bt_lessons.c"
#include "bt_core.c"
#include "bt_codec.c"
#include "bt_pcm.c"
#include "bt_face.c"
#include "bt_audio.c"
#include "bt_platform.c"
#include "bt_cga.c"
