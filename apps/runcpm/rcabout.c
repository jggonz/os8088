/* ============================================================================
 * os8088 - apps/runcpm/rcabout.c       the About panel (SPEC.md 71) - WAVE 5
 *
 * Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
 * "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT licence,
 * Copyright (c) 2017 Mockba the Borg). #included by runcpm.c.
 *
 * WAVE 5 puts here the About panel: product, 6.9, the MIT attribution, what
 * this port is, OK - at most 12 rows, control y = 6 + row*10, because that is
 * a 640x200 number (LESSONS.md 8) - and the shadow invalidation after it comes
 * down. It is the first ovl_* candidate (SPEC.md 70.14) if the measured size
 * line says so. WAVE 1: the kernel's 'About RunCPM' item toasts the product,
 * its version and its author - 'RunCPM 6.9 by M. Dantas', 23 of a toast's 24
 * characters (SPEC.md 59); the MIT attribution and what this port is arrive
 * with the panel - so the item is present and says something true until the
 * panel exists.
 * ==========================================================================*/

static void rc_about(void *win)
{
    (void)win;
    os88_toast("RunCPM 6.9 by M. Dantas", 0);   /* 23 chars; TOAST_MAX 24 */
}
