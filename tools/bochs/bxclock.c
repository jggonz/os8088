/* bxclock.c - an LD_PRELOAD for bochs-bin: a STOPPED Bochs costs the guest
 * no time (docs/BOCHS.md, *The clock*).
 *
 * `clock: sync=slowdown` makes Bochs's guest time its instruction count and
 * caps it at real time by comparing against time(NULL) - Bochs 2.7's
 * iodev/slowdown_timer.cc, its only reader of time() once `time0=` is given.
 * That reference is the WALL clock, so host seconds spent stopped at the
 * debugger prompt (a memory read, a screenshot, a breakpoint) put the guest
 * behind it, and on the resume the timer stops sleeping until the guest has
 * caught up: up to a guest second run flat out, ~25x real time here. Every
 * harness interval measured in host time after a stop - an input gap, a
 * status poll, the lag of a reading - then holds a burst of guest time, and
 * how much depends on how long the stops took, i.e. on host load.
 *
 * This replaces time() with a clock that does not move while Bochs does not
 * run. The slowdown timer is a guest timer firing every guest millisecond,
 * so a RUNNING Bochs calls time() every host millisecond or less; a gap of
 * more than OS88_BXCLOCK_GAP_MS (default 50) between two calls is a stop -
 * the debugger's prompt, a SIGSTOP, a host stall - and is taken out of the
 * clock whole, less the one millisecond a call is expected to take. Nothing
 * else changes: the guest's own clock is still the instruction count, and
 * the cap at real time still holds against the time Bochs actually ran.
 *
 * MartyPC has this property by construction (a stopped core is a stopped
 * machine), and every row shimmed onto Bochs by tools/os88megabochs.py
 * assumes it. Measured on a loaded box (docs/BOCHS.md): half a host second
 * of running after a 2 s stop held 2.4-2.6 guest seconds without this and
 * 0.59-0.61 with it, against 0.42-0.45 with no stop at all.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

static int64_t bx_base_real, bx_base_mono, bx_last, bx_off, bx_gap = -1;

static int64_t bx_ns(clockid_t c)
{
    struct timespec t;
    clock_gettime(c, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

time_t time(time_t *tp)
{
    int64_t now = bx_ns(CLOCK_MONOTONIC), v;
    time_t r;
    if (bx_gap < 0) {
        const char *g = getenv("OS88_BXCLOCK_GAP_MS");
        bx_gap = (g && *g ? atoll(g) : 50) * 1000000;
        bx_base_real = bx_ns(CLOCK_REALTIME);
        bx_base_mono = bx_last = now;
    } else if (now - bx_last > bx_gap) {
        bx_off += now - bx_last - 1000000;
    }
    bx_last = now;
    v = bx_base_real + (now - bx_base_mono) - bx_off;
    r = (time_t)(v / 1000000000);
    if (tp)
        *tp = r;
    return r;
}

/* AND A HALTED CPU COSTS THE HOST NO SLEEP. Debian's Bochs is built with
 * BX_USE_IDLE_HACK: every HLT (cpu/proc_ctrl.cc's enter_sleep_state) calls
 * the X GUI's sim_is_idle, which waits up to a millisecond in select() for
 * an X event. That millisecond is the HOST's and buys the guest only the
 * ticks to its next timer event, so a guest that halts N times a guest
 * second runs at most 1000/N of real time: kern_mega's idle task halts
 * after every IRQ0, and a PIT reprogrammed to 8 kHz (Tracker's speaker
 * player on a card-less machine) made the guest an eighth of real time -
 * 0.35M instructions a host second at 4% CPU, mega-trkcompact's 4.5 guest
 * seconds not run in 45 host seconds. The slowdown timer already caps the
 * guest at real time and sleeps when it is ahead, so the idle hack's sleep
 * is only ever a loss here: a select() called FROM the X GUI plugin with a
 * timeout is made a poll. Every other caller's select() is untouched.
 */
static int bx_from_xgui(void *ra)
{
    static void *yes, *no;
    Dl_info d;
    if (ra == yes)
        return 1;
    if (ra == no)
        return 0;
    if (dladdr(ra, &d) && d.dli_fname && strstr(d.dli_fname, "x_gui")) {
        yes = ra;
        return 1;
    }
    no = ra;
    return 0;
}

int select(int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *tv)
{
    static int (*real)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
    static int keep = -1;           /* OS88_BXIDLE=1: the hack's sleep back */
    struct timeval zero = {0, 0};
    if (!real) {
        real = (int (*)(int, fd_set *, fd_set *, fd_set *, struct timeval *))
            dlsym(RTLD_NEXT, "select");
        keep = getenv("OS88_BXIDLE") != NULL;
    }
    if (!keep && tv && (tv->tv_sec || tv->tv_usec) && !w && !e &&
            bx_from_xgui(__builtin_return_address(0)))
        tv = &zero;
    return real(n, r, w, e, tv);
}
