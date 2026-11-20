/* Compile-only engine TU. Production package includes these same sources. */
#include "../bt.h"
#include "../bt_lessons.c"
#include "../bt_core.c"
#include "../bt_codec.c"
/* These callbacks only satisfy the real package runtime for the compile
 * check. This fixture is never installed or substituted for the product. */
void *os88_main(void) { return 0; }
void os88_paint(void *win) { (void)win; }
