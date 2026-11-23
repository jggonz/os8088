/* Compile-only engine TU. Production package includes these same sources. */
#include "../../apps/boratype/bt.h"
#include "../../apps/boratype/bt_lessons.c"
#include "../../apps/boratype/bt_core.c"
#include "../../apps/boratype/bt_codec.c"
/* These callbacks only satisfy the real package runtime for the compile
 * check. This fixture is never installed or substituted for the product. */
void *os88_main(void) { return 0; }
void os88_paint(void *win) { (void)win; }
